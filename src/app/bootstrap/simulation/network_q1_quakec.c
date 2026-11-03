/* Port of src/app/bootstrap/simulation/network-q1-quakec.ts: native NetQuake serializes
 * the executed program's fields and ordered precaches. The host ctx begins with its
 * destroy function (see qa_app_sim_q1_application_server_host_destroy). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q1_quakec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qc/machine.h"
#include "qa/compat/qc/memory.h"
#include "qa/compat/qc/program.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/common/session.h"
#include "qa/network/q1/codecs/codec.h"
#include "qa/network/q1/constants.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/profile.h"

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count ? count : 1, size ? size : 1);
  if (!p) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *out = realloc(p, size ? size : 1);
  if (!out) qa_range_error("Out of memory");
  return out;
}
static char *xstrdup(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (!out) qa_range_error("Out of memory");
  memcpy(out, text, length + 1);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

static qa_netquake_message clone_message(const qa_netquake_message *message) {
  qa_netquake_message out = *message;
  out.text = xstrdup(message->text); out.level = xstrdup(message->level); out.str_value = xstrdup(message->str_value);
  out.models = message->model_count ? xcalloc(message->model_count, sizeof *out.models) : NULL;
  for (size_t i = 0; i < message->model_count; i++) out.models[i] = xstrdup(message->models[i]);
  out.sounds = message->sound_count ? xcalloc(message->sound_count, sizeof *out.sounds) : NULL;
  for (size_t i = 0; i < message->sound_count; i++) out.sounds[i] = xstrdup(message->sounds[i]);
  return out;
}
typedef struct message_list { qa_netquake_message *values; size_t count, capacity; } message_list;
static void list_push(message_list *list, qa_netquake_message message) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 16;
    list->values = xrealloc(list->values, list->capacity * sizeof *list->values);
  }
  list->values[list->count++] = message;
}
static void list_clear(message_list *list) {
  for (size_t i = 0; i < list->count; i++) qa_netquake_message_destroy(&list->values[i]);
  free(list->values);
  memset(list, 0, sizeof *list);
}
typedef struct routed { bool has_recipient; qa_actor_id recipient; bool reliable; qa_netquake_message message; } routed;
typedef struct routed_list { routed *values; size_t count, capacity; } routed_list;
static void routed_push(routed_list *list, routed value) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 16;
    list->values = xrealloc(list->values, list->capacity * sizeof *list->values);
  }
  list->values[list->count++] = value;
}
static void routed_clear(routed_list *list) {
  for (size_t i = 0; i < list->count; i++) qa_netquake_message_destroy(&list->values[i].message);
  free(list->values);
  memset(list, 0, sizeof *list);
}
/* An ordered name -> ordinal Map built from a precache list (a repeated name keeps its
 * first position and its last ordinal). */
typedef struct ordinal_map { const char **keys; double *values; size_t count; } ordinal_map;
static void map_build(ordinal_map *map, const char *const *names, size_t count) {
  map->keys = xcalloc(count, sizeof *map->keys);
  map->values = xcalloc(count, sizeof *map->values);
  map->count = 0;
  for (size_t i = 0; i < count; i++) {
    size_t at = 0;
    while (at < map->count && strcmp(map->keys[at], names[i]) != 0) at++;
    if (at == map->count) map->keys[map->count++] = names[i];
    map->values[at] = (double)i + 1;
  }
}
static bool map_get(const ordinal_map *map, const char *key, double *out) {
  for (size_t i = 0; i < map->count; i++) if (strcmp(map->keys[i], key) == 0) { *out = map->values[i]; return true; }
  return false;
}
typedef struct board_entry { bool used; char *name; double colors, frags; } board_entry;
typedef struct per_player {
  bool used; qa_app_q1_player player;
  message_list spawn, messages, reliable, datagram;
  qa_q1_extended_entity_state *entities; size_t entity_count;
  message_list signon; qa_netquake_message info; bool has_info;
  qa_app_q1_baseline_entry *baselines; size_t baseline_count; qa_app_q1_baselines baseline_view;
} per_player;
typedef struct quakec_host {
  void (*destroy)(void *ctx);
  qa_app_sim_q1_server_binding_options options;
  const qa_app_sim_network_simulation *simulation;
  qa_app_sim_quakec_source *game;
  double max_clients;
  bool wide;
  ordinal_map models, sounds;
  routed_list routed, commands;
  bool previous_pause;
  board_entry *board; size_t board_count;
  per_player *players; size_t player_count;
  double *order; size_t order_count;            /* clients Map insertion order */
  double *board_order; size_t board_order_count; /* board Map insertion order */
  qa_wire_admission support;
  char *map_name;
  qa_actor_command **inputs; size_t input_count;
} quakec_host;

static per_player *player_slot(quakec_host *h, double slot) {
  if (slot < 0 || slot != trunc(slot)) qa_range_error("Invalid Q1 client slot");
  if ((size_t)slot >= h->player_count) {
    size_t count = (size_t)slot + 1;
    h->players = xrealloc(h->players, count * sizeof *h->players);
    memset(h->players + h->player_count, 0, (count - h->player_count) * sizeof *h->players);
    h->player_count = count;
  }
  return &h->players[(size_t)slot];
}
static void order_add(double **order, size_t *count, double slot) {
  for (size_t i = 0; i < *count; i++) if ((*order)[i] == slot) return;
  *order = xrealloc(*order, (*count + 1) * sizeof **order);
  (*order)[(*count)++] = slot;
}
static void order_remove(double *order, size_t *count, double slot) {
  for (size_t i = 0; i < *count; i++)
    if (order[i] == slot) { memmove(order + i, order + i + 1, (*count - i - 1) * sizeof *order); (*count)--; return; }
}
static void client_set(quakec_host *h, double slot, qa_app_q1_player player);
static board_entry *board_slot(quakec_host *h, double slot) {
  if ((size_t)slot >= h->board_count) {
    size_t count = (size_t)slot + 1;
    h->board = xrealloc(h->board, count * sizeof *h->board);
    memset(h->board + h->board_count, 0, (count - h->board_count) * sizeof *h->board);
    h->board_count = count;
  }
  return &h->board[(size_t)slot];
}
static double index_of(const ordinal_map *table, const char *path) {
  if (path == NULL || path[0] == '\0') return 0;
  double value;
  if (!map_get(table, path, &value)) qa_raise("Error", 0, 0, "Unprecached QuakeC resource %s", path);
  return value;
}
static int64_t field(quakec_host *h, const char *name) {
  const qa_compat_qc_definition *definition = qa_compat_qc_program_field_named(qa_app_sim_quakec_source_program(h->game), name);
  if (definition == NULL) qa_raise("Error", 0, 0, "Missing NetQuake ABI field %s", name);
  return definition->offset;
}
static qa_compat_qc_words *entity_at(quakec_host *h, double slot) {
  return qa_compat_qc_entity_memory_at(qa_app_sim_quakec_source_entities(h->game), (int64_t)slot);
}
static double scalar(quakec_host *h, double slot, const char *name) {
  qa_compat_qc_words *entity = entity_at(h, slot);
  return qa_compat_qc_words_float(entity, field(h, name));
}
static double optional(quakec_host *h, double slot, const char *name) {
  const qa_compat_qc_definition *definition = qa_compat_qc_program_field_named(qa_app_sim_quakec_source_program(h->game), name);
  return definition == NULL ? 0 : qa_compat_qc_words_float(entity_at(h, slot), definition->offset);
}
static const char *string_field(quakec_host *h, double slot, const char *name) {
  qa_compat_qc_machine *machine = qa_app_sim_quakec_source_machine(h->game);
  qa_compat_qc_words *entity = entity_at(h, slot);
  return qa_compat_qc_strings_get(&machine->strings, qa_compat_qc_words_int(entity, field(h, name)));
}
static qa_vec3 vector(quakec_host *h, double slot, const char *name) {
  qa_compat_qc_words *entity = entity_at(h, slot);
  return qa_compat_qc_words_vector(entity, field(h, name));
}
static double global(quakec_host *h, const char *name) {
  qa_compat_qc_machine *machine = qa_app_sim_quakec_source_machine(h->game);
  return qa_compat_qc_words_float(&machine->globals, qa_compat_qc_machine_global_offset(machine, name));
}
static const char *global_string(quakec_host *h, const char *name) {
  qa_compat_qc_machine *machine = qa_app_sim_quakec_source_machine(h->game);
  return qa_compat_qc_strings_get(&machine->strings, qa_compat_qc_words_int(&machine->globals, qa_compat_qc_machine_global_offset(machine, name)));
}
static double number(quakec_host *h, qa_actor_id actor) {
  int64_t slot;
  if (!qa_app_sim_quakec_source_source_slot(h->game, actor, &slot)) qa_raise("Error", 0, 0, "NetQuake actor has no source slot");
  return (double)slot;
}
/* entities() */
static qa_q1_extended_entity_state *entities(quakec_host *h, size_t *out_count) {
  int64_t count = qa_compat_qc_entity_memory_get_count(qa_app_sim_quakec_source_entities(h->game));
  qa_q1_extended_entity_state *result = xcalloc((size_t)(count > 0 ? count : 1), sizeof *result);
  size_t kept = 0;
  qa_source_actor_slots *slots = qa_app_sim_quakec_source_slots(h->game);
  for (int64_t slot = 1; slot < count; slot++) {
    qa_owned_actor owner;
    if (!qa_source_actor_slots_at(slots, slot, &owner) || scalar(h, (double)slot, "modelindex") == 0
        || string_field(h, (double)slot, "model")[0] == '\0') continue;
    qa_q1_extended_entity_state state;
    memset(&state, 0, sizeof state);
    double s = (double)slot;
    state.base.number = s;
    state.base.origin = vector(h, s, "origin");
    state.base.angles = vector(h, s, "angles");
    state.base.model_index = trunc(scalar(h, s, "modelindex"));
    state.base.frame = trunc(scalar(h, s, "frame"));
    state.base.color_map = trunc(scalar(h, s, "colormap"));
    state.base.skin = trunc(scalar(h, s, "skin"));
    state.base.effects = trunc(scalar(h, s, "effects"));
    state.alpha = h->wide ? qa_q1_entalpha_encode(optional(h, s, "alpha")) : 0;
    state.scale = h->wide ? qa_js_bit_and(trunc(qa_q1_entscale_encode(optional(h, s, "scale"))), 255) : 16;
    state.lerp_finish_seconds = 0;
    state.step = scalar(h, s, "movetype") == 4;
    result[kept++] = state;
  }
  *out_count = kept;
  return result;
}
static qa_q1_extended_entity_state baseline(quakec_host *h, qa_q1_extended_entity_state state) {
  bool player = state.base.number <= h->max_clients;
  double model_index = player ? index_of(&h->models, "progs/player.mdl") : state.base.model_index;
  qa_q1_extended_entity_state out = state;
  out.base.model_index = !h->wide && model_index > 255 ? 0 : model_index;
  out.base.frame = !h->wide && state.base.frame > 255 ? 0 : state.base.frame;
  out.base.effects = 0;
  out.base.color_map = player ? state.base.number : 0;
  out.alpha = player ? 0 : state.alpha;
  out.scale = player || h->options.protocol.version != 999 ? 16 : state.scale;
  out.step = false;
  return out;
}
static qa_netquake_message client_data(quakec_host *h, qa_app_q1_player player) {
  double slot = number(h, player.actor);
  const qa_compat_qc_definition *items2 = qa_compat_qc_program_field_named(qa_app_sim_quakec_source_program(h->game), "items2");
  double base = trunc(scalar(h, slot, "items"));
  double extra = items2 == NULL ? qa_js_shl(trunc(global(h, "serverflags")), 28)
      : qa_js_shl(trunc(qa_compat_qc_words_float(entity_at(h, slot), items2->offset)), 23);
  double items = qa_js_bit_or(base, extra);
  double weapon = trunc(scalar(h, slot, "weapon"));
  /* hipnotic/rogue native clients interpret the active weapon as its bit ordinal. */
  const char *content = h->simulation->recipe->map.entities.content;
  double active = weapon;
  if (strstr(content, ":rogue:") != NULL || strstr(content, ":hipnotic:") != NULL) {
    uint32_t low = (uint32_t)qa_js_to_int32(qa_js_bit_and(weapon, -weapon));
    active = weapon == 0 ? 0 : 31 - (double)(low == 0 ? 32 : __builtin_clz(low));
  }
  qa_netquake_message message;
  memset(&message, 0, sizeof message);
  message.kind = "client-data";
  message.weapon_alpha = h->wide ? qa_q1_entalpha_encode(optional(h, slot, "alpha")) : 0;
  qa_q1_client_data *data = &message.data;
  data->view_height = vector(h, slot, "view_ofs").z;
  data->ideal_pitch = scalar(h, slot, "idealpitch");
  data->punch_angles = vector(h, slot, "punchangle");
  data->velocity = vector(h, slot, "velocity");
  data->items = items;
  data->on_ground = qa_js_bit_and(trunc(scalar(h, slot, "flags")), 512) != 0;
  data->in_water = scalar(h, slot, "waterlevel") >= 2;
  data->weapon_frame = scalar(h, slot, "weaponframe");
  data->armor = scalar(h, slot, "armorvalue");
  data->weapon_model = index_of(&h->models, string_field(h, slot, "weaponmodel"));
  data->health = scalar(h, slot, "health");
  data->ammo = scalar(h, slot, "currentammo");
  data->shells = scalar(h, slot, "ammo_shells");
  data->nails = scalar(h, slot, "ammo_nails");
  data->rockets = scalar(h, slot, "ammo_rockets");
  data->cells = scalar(h, slot, "ammo_cells");
  data->active_weapon = active;
  return message;
}
static void send(quakec_host *h, qa_netquake_message message, bool reliable, const qa_actor_id *recipient) {
  routed value;
  memset(&value, 0, sizeof value);
  value.message = message; value.reliable = reliable;
  if (recipient) { value.has_recipient = true; value.recipient = *recipient; }
  routed_push(&h->routed, value);
}
static void admit_command(quakec_host *h, qa_netquake_message message, bool reliable, qa_actor_id recipient) {
  if (h->commands.count >= 4096)
    qa_raise("RangeError", 0, 0, "Event ring capacity exhausted (4096 undrained records); drain before admitting more");
  routed value;
  memset(&value, 0, sizeof value);
  value.message = message; value.reliable = reliable; value.has_recipient = true; value.recipient = recipient;
  routed_push(&h->commands, value);
}
static void source_messages(quakec_host *h) {
  size_t count = 0;
  const qa_app_sim_quakec_netquake_routed *entries = qa_app_sim_quakec_source_drain_netquake_messages(h->game, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_quakec_netquake_routed *entry = &entries[i];
    if (entry->destination.kind == QA_QC_DEST_MULTICAST || entry->destination.kind == QA_QC_DEST_SIGNON)
      qa_raise("Error", 0, 0, "Unexpected NetQuake runtime destination");
    for (size_t m = 0; m < entry->message_count; m++) {
      if (is(entry->messages[m].kind, "entity")) qa_raise("Error", 0, 0, "QuakeC cannot replace the host entity snapshot");
      send(h, clone_message(&entry->messages[m]), entry->destination.reliable,
          entry->destination.kind == QA_QC_DEST_CLIENT ? &entry->destination.actor : NULL);
    }
  }
}
static qa_netquake_message static_sound(quakec_host *h, const qa_q1_event *event) {
  qa_netquake_message message;
  memset(&message, 0, sizeof message);
  message.kind = "static-sound"; message.entity = 0; message.channel = 0;
  message.index = index_of(&h->sounds, event->path);
  message.volume = trunc(event->volume * 255); message.attenuation = event->attenuation; message.origin = event->origin;
  return message;
}
static qa_netquake_message static_model(quakec_host *h, const qa_q1_event *event) {
  qa_netquake_message message;
  memset(&message, 0, sizeof message);
  message.kind = "static";
  message.state.base.number = 0; message.state.base.model_index = index_of(&h->models, event->path);
  message.state.base.frame = event->frame; message.state.base.color_map = event->color_map; message.state.base.skin = event->skin;
  message.state.base.effects = 0; message.state.base.origin = event->origin; message.state.base.angles = event->angles;
  message.state.alpha = 0; message.state.scale = 16; message.state.lerp_finish_seconds = 0; message.state.step = false;
  return message;
}
static void persistent_signon(quakec_host *h, message_list *out) {
  size_t count = 0;
  const qa_netquake_message *signon = qa_app_sim_quakec_source_netquake_signon_messages(h->game, &count);
  for (size_t i = 0; i < count; i++) {
    if (is(signon[i].kind, "entity")) qa_raise("Error", 0, 0, "Entity update in QuakeC signon");
    list_push(out, clone_message(&signon[i]));
  }
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t persistent_count = 0;
  const qa_app_sim_presentation_event *const *persistent = s->persistent_events(s->ctx, &persistent_count);
  for (size_t i = 0; i < persistent_count; i++) {
    if (!is(persistent[i]->kind, "q1")) continue;
    const qa_q1_event *event = persistent[i]->q1;
    if (is(event->kind, "ambient")) list_push(out, static_sound(h, event));
    if (is(event->kind, "static-model")) list_push(out, static_model(h, event));
  }
}
static qa_netquake_message text_message(const char *kind, double slot, const char *value) {
  qa_netquake_message message;
  memset(&message, 0, sizeof message);
  message.kind = kind; message.slot = slot; message.str_value = xstrdup(value);
  return message;
}
static qa_netquake_message value_message(const char *kind, double slot, double value) {
  qa_netquake_message message;
  memset(&message, 0, sizeof message);
  message.kind = kind; message.slot = slot; message.value = value;
  return message;
}
static double info_number(const qa_app_sim_quakec_info *info, const char *key) {
  for (size_t i = 0; i < info->count; i++)
    if (strcmp(info->items[i].name, key) == 0) return qa_js_string_to_number(info->items[i].value);
  return 0;
}
static void host_observe(void *ctx, const void *output, const void *const *events_value, size_t count) {
  (void)output; /* TS _output */
  quakec_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  const qa_app_sim_presentation_event *const *events = (const qa_app_sim_presentation_event *const *)events_value;
  for (size_t i = 0; i < h->input_count; i++) free(h->inputs[i]);
  h->input_count = 0;
  routed_clear(&h->routed);
  h->routed = h->commands; /* routed = commandMessages.drainAll() */
  memset(&h->commands, 0, sizeof h->commands);
  source_messages(h);
  bool paused = s->q1_paused(s->ctx);
  if (h->previous_pause != paused) {
    h->previous_pause = paused;
    qa_netquake_message message; memset(&message, 0, sizeof message);
    message.kind = "pause"; message.paused = paused;
    send(h, message, true, NULL);
  }
  for (size_t o = 0; o < h->order_count; o++) {
    qa_app_q1_player player = h->players[(size_t)h->order[o]].player;
    double slot = player.source_entity, client_slot = (double)player.client.slot;
    qa_app_sim_quakec_info info = qa_app_sim_quakec_source_client_info(h->game, player.client);
    board_entry *previous = board_slot(h, client_slot);
    const char *name = string_field(h, slot, "netname");
    double top = info_number(&info, "topcolor");
    double colors = qa_js_bit_or(qa_js_shl(qa_js_bit_and(top, 15), 4), qa_js_bit_and(info_number(&info, "bottomcolor"), 15));
    double frags = scalar(h, slot, "frags");
    if (!previous->used || !is(previous->name, name)) send(h, text_message("name", client_slot, name), true, NULL);
    if (!previous->used || previous->colors != colors) send(h, value_message("colors", client_slot, colors), true, NULL);
    if (!previous->used || previous->frags != frags) send(h, value_message("frags", client_slot, frags), true, NULL);
    char *copy = xstrdup(name);
    free(previous->name);
    previous->used = true; previous->name = copy; previous->colors = colors; previous->frags = frags;
    order_add(&h->board_order, &h->board_order_count, client_slot);
  }
  /* for (const slot of board.keys()) if (!clients.has(slot)) ... board.delete(slot) */
  size_t snapshot_count = h->board_order_count;
  double *snapshot = xcalloc(snapshot_count, sizeof *snapshot);
  if (snapshot_count) memcpy(snapshot, h->board_order, snapshot_count * sizeof *snapshot);
  for (size_t k = 0; k < snapshot_count; k++) {
    size_t slot = (size_t)snapshot[k];
    if (slot < h->player_count && h->players[slot].used) continue;
    send(h, text_message("name", (double)slot, ""), true, NULL);
    send(h, value_message("colors", (double)slot, 0), true, NULL);
    send(h, value_message("frags", (double)slot, 0), true, NULL);
    board_entry *entry = &h->board[slot];
    free(entry->name);
    memset(entry, 0, sizeof *entry);
    order_remove(h->board_order, &h->board_order_count, (double)slot);
  }
  free(snapshot);
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *record = events[i];
    if (is(record->kind, "view-reset")) {
      qa_netquake_message message; memset(&message, 0, sizeof message);
      message.kind = "set-angle"; message.angles = record->view_reset.angles;
      send(h, message, true, &record->view_reset.actor);
      continue;
    }
    if (!is(record->kind, "q1")) continue;
    const qa_q1_event *event = record->q1;
    if (is(event->kind, "particles")) {
      qa_netquake_message message; memset(&message, 0, sizeof message);
      message.kind = "particle"; message.origin = event->origin;
      double d[3] = {event->direction.x, event->direction.y, event->direction.z};
      for (int k = 0; k < 3; k++) d[k] = qa_js_max(-128, qa_js_min(127, trunc(d[k] * 16))) / 16;
      message.direction = (qa_vec3){d[0], d[1], d[2]};
      message.count = event->count; message.color_value = event->color;
      send(h, message, false, NULL);
    } else if (is(event->kind, "lightstyle")) {
      qa_netquake_message message = text_message("light-style", 0, event->pattern);
      message.index = event->style;
      send(h, message, true, NULL);
    }
  }
}
typedef struct admit_scope { quakec_host *h; qa_client_id id; double slot; bool has_actor; qa_actor_id actor; qa_app_q1_player player; } admit_scope;
static void admit_body(void *ctx) {
  admit_scope *a = ctx;
  const qa_app_sim_network_simulation *s = a->h->simulation;
  a->actor = s->reserve_netquake_client(s->ctx, a->id);
  a->has_actor = true;
  qa_app_q1_player player = {a->id, a->actor, number(a->h, a->actor)};
  client_set(a->h, a->slot, player);
  a->player = player;
}
static qa_app_q1_admission host_admit(void *ctx, const void *from_value) {
  quakec_host *h = ctx;
  const qa_net_address *from = from_value;
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_app_q1_admission result;
  memset(&result, 0, sizeof result);
  double slot = 0;
  for (; slot < h->max_clients; slot++) {
    bool taken = slot < (double)h->player_count && h->players[(size_t)slot].used;
    size_t count = 0;
    const qa_actor_id *players = s->players(s->ctx, &count);
    for (size_t i = 0; i < count && !taken; i++) {
      int64_t source;
      if (qa_app_sim_quakec_source_source_slot(h->game, players[i], &source) && (double)source == slot + 1) taken = true;
    }
    if (!taken) break;
  }
  if (slot == h->max_clients) { result.reason = "Server is full"; return result; }
  qa_session_client *client = qa_engine_session_create_client(h->options.session, slot);
  qa_session_client_connect(client, from->kind == QA_NET_LOOPBACK ? "loopback" : "remote");
  admit_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.h = h; scope.id = qa_session_client_id(client); scope.slot = slot;
  qa_caught_error error;
  if (qa_try(admit_body, &scope, &error)) { result.accepted = true; result.player = scope.player; return result; }
  if (scope.has_actor) qa_app_sim_quakec_source_disconnect_client(h->game, qa_app_sim_quakec_source_reserved_client(h->game, scope.id));
  qa_engine_session_close_client(h->options.session, scope.id);
  qa_rethrow(&error);
}
static qa_app_q1_player host_carried_player(void *ctx, qa_client_id client) {
  quakec_host *h = ctx;
  qa_actor_id actor;
  if (!qa_app_sim_quakec_source_client_actor(h->game, client, &actor)) qa_raise("Error", 0, 0, "Carried QuakeC client has not been reserved");
  qa_app_q1_player player = {client, actor, number(h, actor)};
  client_set(h, (double)client.slot, player);
  return player;
}
static void clear_player(per_player *entry) {
  list_clear(&entry->spawn); list_clear(&entry->messages); list_clear(&entry->reliable); list_clear(&entry->datagram);
  list_clear(&entry->signon);
  if (entry->has_info) qa_netquake_message_destroy(&entry->info);
  entry->has_info = false;
  free(entry->entities); entry->entities = NULL; entry->entity_count = 0;
  free(entry->baselines); entry->baselines = NULL; entry->baseline_count = 0;
}
static void host_disconnect(void *ctx, qa_app_q1_player player, const char *reason) {
  (void)reason; /* TS disconnect(player) */
  quakec_host *h = ctx;
  if (qa_app_sim_quakec_source_is_active_client(h->game, player.actor)) h->simulation->disconnect_player(h->simulation->ctx, player.actor);
  else qa_app_sim_quakec_source_disconnect_client(h->game, qa_app_sim_quakec_source_reserved_client(h->game, player.client));
  qa_engine_session_close_client(h->options.session, player.client);
  per_player *entry = player_slot(h, (double)player.client.slot);
  clear_player(entry);
  entry->used = false;
  order_remove(h->order, &h->order_count, (double)player.client.slot);
}
static void set_baseline(per_player *entry, qa_q1_extended_entity_state state) {
  for (size_t i = 0; i < entry->baseline_count; i++)
    if (entry->baselines[i].number == state.base.number) { entry->baselines[i].state = state; return; }
  entry->baselines = xrealloc(entry->baselines, (entry->baseline_count + 1) * sizeof *entry->baselines);
  entry->baselines[entry->baseline_count].number = state.base.number;
  entry->baselines[entry->baseline_count].state = state;
  entry->baseline_count++;
}
static bool has_baseline(const per_player *entry, double number_value) {
  for (size_t i = 0; i < entry->baseline_count; i++) if (entry->baselines[i].number == number_value) return true;
  return false;
}
static qa_app_q1_game_state host_game_state(void *ctx, qa_app_q1_player player) {
  quakec_host *h = ctx;
  per_player *entry = player_slot(h, (double)player.client.slot);
  clear_player(entry);
  size_t count = 0;
  qa_q1_extended_entity_state *states = entities(h, &count);
  for (size_t i = 0; i < count; i++) set_baseline(entry, baseline(h, states[i]));
  free(states);
  for (double slot = 1; slot <= h->max_clients; slot++)
    if (!has_baseline(entry, slot)) {
      qa_q1_extended_entity_state state;
      memset(&state, 0, sizeof state);
      state.base.number = slot; state.base.origin = vector(h, slot, "origin"); state.base.angles = vector(h, slot, "angles");
      state.base.color_map = slot; state.scale = 16;
      set_baseline(entry, baseline(h, state));
    }
  memset(&entry->info, 0, sizeof entry->info);
  entry->info.kind = "server-info";
  entry->info.protocol = h->options.protocol;
  entry->info.max_clients = h->max_clients;
  entry->info.game_type = qa_cvar_registry_variable_value(qa_app_sim_quakec_source_cvars(h->game), "deathmatch") == 0 ? 0 : 1;
  const char *message = string_field(h, 0, "message");
  entry->info.level = xstrdup(message[0] ? message : global_string(h, "mapname"));
  entry->info.models = h->models.count ? xcalloc(h->models.count, sizeof *entry->info.models) : NULL;
  for (size_t i = 0; i < h->models.count; i++) entry->info.models[i] = xstrdup(h->models.keys[i]);
  entry->info.model_count = h->models.count;
  entry->info.sounds = h->sounds.count ? xcalloc(h->sounds.count, sizeof *entry->info.sounds) : NULL;
  for (size_t i = 0; i < h->sounds.count; i++) entry->info.sounds[i] = xstrdup(h->sounds.keys[i]);
  entry->info.sound_count = h->sounds.count;
  entry->has_info = true;
  persistent_signon(h, &entry->signon);
  entry->baseline_view.entries = entry->baselines; entry->baseline_view.count = entry->baseline_count;
  qa_app_q1_game_state state = {&entry->info, &entry->baseline_view, (const void *const *)entry->signon.values, entry->signon.count};
  return state;
}
static const void *const *host_spawn(void *ctx, qa_app_q1_player player, size_t *out_count) {
  quakec_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  if (!qa_app_sim_quakec_source_is_active_client(h->game, player.actor)) {
    qa_app_sim_player_admission admitted = s->admit_player(s->ctx, player.client);
    if (!qa_same_actor(admitted.actor, player.actor)) qa_raise("Error", 0, 0, "QuakeC spawn replaced reserved client identity");
  }
  per_player *entry = player_slot(h, (double)player.client.slot);
  list_clear(&entry->spawn);
  qa_netquake_message message;
  memset(&message, 0, sizeof message);
  message.kind = "pause"; message.paused = s->q1_paused(s->ctx);
  list_push(&entry->spawn, message);
  memset(&message, 0, sizeof message);
  message.kind = "time"; message.seconds = qa_app_sim_quakec_source_time_seconds(h->game);
  list_push(&entry->spawn, message);
  for (double index = 0; index < 64; index++) {
    message = text_message("light-style", 0, s->light_style(s->ctx, index));
    message.index = index;
    list_push(&entry->spawn, message);
  }
  for (size_t o = 0; o < h->order_count; o++) {
    const qa_app_q1_player *client = &h->players[(size_t)h->order[o]].player;
    double slot = (double)client->client.slot;
    list_push(&entry->spawn, text_message("name", slot, string_field(h, client->source_entity, "netname")));
    list_push(&entry->spawn, value_message("frags", slot, scalar(h, client->source_entity, "frags")));
    double colors = slot < (double)h->board_count && h->board[(size_t)slot].used ? h->board[(size_t)slot].colors : 0;
    list_push(&entry->spawn, value_message("colors", slot, colors));
  }
  static const struct { double stat; const char *name; } stats[4] = {
    {11, "total_secrets"}, {12, "total_monsters"}, {13, "found_secrets"}, {14, "killed_monsters"}};
  for (int i = 0; i < 4; i++) {
    memset(&message, 0, sizeof message);
    message.kind = "stat"; message.index = stats[i].stat; message.value = global(h, stats[i].name);
    list_push(&entry->spawn, message);
  }
  memset(&message, 0, sizeof message);
  message.kind = "set-angle"; message.angles = vector(h, player.source_entity, "angles");
  list_push(&entry->spawn, message);
  list_push(&entry->spawn, client_data(h, player));
  *out_count = entry->spawn.count;
  return (const void *const *)entry->spawn.values;
}
static void routed_for(quakec_host *h, qa_app_q1_player player, bool reliable, message_list *out) {
  list_clear(out);
  for (size_t i = 0; i < h->routed.count; i++) {
    const routed *event = &h->routed.values[i];
    if (event->reliable == reliable && (!event->has_recipient || qa_same_actor(event->recipient, player.actor)))
      list_push(out, clone_message(&event->message));
  }
}
static qa_app_q1_frame host_frame(void *ctx, qa_app_q1_player player, const void *output) {
  (void)output; /* TS frame(player) */
  quakec_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_shared_scene_queries *scene = s->scene;
  per_player *entry = player_slot(h, (double)player.client.slot);
  qa_app_q1_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.seconds = qa_app_sim_quakec_source_time_seconds(h->game);
  list_clear(&entry->messages); list_clear(&entry->datagram);
  free(entry->entities); entry->entities = NULL; entry->entity_count = 0;
  routed_for(h, player, true, &entry->reliable);
  if (qa_app_sim_quakec_source_is_active_client(h->game, player.actor)) {
    double cluster = qa_shared_leaf_cluster(scene, qa_shared_point_leaf(scene, s->player_view(s->ctx, player.actor).origin));
    qa_app_sim_quakec_netquake_damage damage;
    if (qa_app_sim_quakec_source_consume_netquake_damage(h->game, player.actor, &damage)) {
      qa_netquake_message message;
      memset(&message, 0, sizeof message);
      message.kind = "damage"; message.armor = damage.armor; message.blood = damage.blood; message.source = damage.source;
      list_push(&entry->messages, message);
    }
    list_push(&entry->messages, client_data(h, player));
    routed_for(h, player, false, &entry->datagram);
    size_t count = 0;
    qa_q1_extended_entity_state *states = entities(h, &count);
    entry->entities = states;
    for (size_t i = 0; i < count; i++) {
      const qa_q1_extended_entity_state *state = &states[i];
      if (!(!h->wide || state->alpha != QA_Q1_ENTALPHA_ZERO || state->base.effects != 0)) continue;
      if (state->base.number != player.source_entity
          && !qa_shared_cluster_visible(scene, cluster, qa_shared_leaf_cluster(scene, qa_shared_point_leaf(scene, state->base.origin)), "pvs"))
        continue;
      states[entry->entity_count++] = *state;
    }
  }
  frame.messages = (const void *const *)entry->messages.values; frame.message_count = entry->messages.count;
  frame.reliable = (const void *const *)entry->reliable.values; frame.reliable_count = entry->reliable.count;
  frame.datagram = (const void *const *)entry->datagram.values; frame.datagram_count = entry->datagram.count;
  frame.entities = (const void *const *)entry->entities; frame.entity_count = entry->entity_count;
  return frame;
}
static const void *host_input(void *ctx, qa_app_q1_player player, const qa_q1_user_command *command, double sequence) {
  quakec_host *h = ctx;
  qa_actor_command *out = xcalloc(1, sizeof *out);
  out->actor = player.actor;
  out->source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT; out->source.client = player.client;
  out->sequence = sequence;
  out->command.kind = QA_USER_COMMAND_Q1; out->command.as.q1 = *command;
  h->inputs = xrealloc(h->inputs, (h->input_count + 1) * sizeof *h->inputs);
  h->inputs[h->input_count++] = out;
  return out;
}
static char *slice_15(const char *text) {
  /* (args[0] ?? 'unconnected').slice(0, 15): UTF-16 units equal bytes for ASCII names. */
  size_t length = strlen(text);
  if (length > 15) length = 15;
  char *out = xcalloc(length + 1, 1);
  memcpy(out, text, length);
  return out;
}
static void host_command(void *ctx, qa_app_q1_player player, const char *name, const char *const *args, size_t count) {
  quakec_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  if (is(name, "pause")) {
    bool before = s->q1_paused(s->ctx);
    const char *text = s->toggle_q1_pause(s->ctx, &player.actor);
    qa_netquake_message message;
    memset(&message, 0, sizeof message);
    message.kind = "print"; message.text = xstrdup(text);
    if (h->commands.count >= 4096) { free(message.text); qa_raise("RangeError", 0, 0, "Event ring capacity exhausted (4096 undrained records); drain before admitting more"); }
    routed value;
    memset(&value, 0, sizeof value);
    value.message = message; value.reliable = true;
    if (before == s->q1_paused(s->ctx)) { value.has_recipient = true; value.recipient = player.actor; }
    routed_push(&h->commands, value);
  } else if (is(name, "name") || is(name, "color")) {
    qa_app_sim_quakec_info current = qa_app_sim_quakec_source_client_info(h->game, player.client);
    qa_app_sim_info info;
    memset(&info, 0, sizeof info);
    for (size_t i = 0; i < current.count; i++) qa_app_sim_info_set(&info, current.items[i].name, current.items[i].value);
    if (is(name, "name")) {
      char *value = slice_15(count > 0 ? args[0] : "unconnected");
      qa_app_sim_info_set(&info, "name", value);
      free(value);
    } else {
      double first = count > 0 ? qa_js_string_to_number(args[0]) : 0;
      double second = count > 1 ? qa_js_string_to_number(args[1]) : first;
      double top = qa_js_min(13, qa_js_bit_and(trunc(first), 15)), bottom = qa_js_min(13, qa_js_bit_and(trunc(second), 15));
      char text[40];
      snprintf(text, sizeof text, "%s", QA_JS_NUMBER(top));
      qa_app_sim_info_set(&info, "topcolor", text);
      snprintf(text, sizeof text, "%s", QA_JS_NUMBER(bottom));
      qa_app_sim_info_set(&info, "bottomcolor", text);
      qa_compat_qc_words *entity = entity_at(h, player.source_entity);
      qa_compat_qc_words_set_float(entity, field(h, "team"), bottom + 1);
    }
    qa_app_sim_cvar_name_value *items = xcalloc(info.count, sizeof *items);
    for (size_t i = 0; i < info.count; i++) { items[i].name = info.pairs[i].key; items[i].value = info.pairs[i].value; }
    qa_app_sim_quakec_info values = {items, info.count};
    qa_app_sim_quakec_source_set_client_info(h->game, player.client, values);
    free(items);
    qa_app_sim_info_clear(&info);
    s->notify_client_event(s->ctx, "userinfo", player.actor);
  } else if (is(name, "say") || is(name, "say_team")) {
    size_t length = 1;
    for (size_t i = 0; i < count; i++) length += strlen(args[i]) + 1;
    char *joined = xcalloc(length, 1);
    for (size_t i = 0; i < count; i++) { if (i) strcat(joined, " "); strcat(joined, args[i]); }
    if (strlen(joined) > 126) joined[126] = '\0'; /* slice(0, 126) over ASCII text */
    const char *netname = string_field(h, player.source_entity, "netname");
    size_t total = strlen(netname) + strlen(joined) + 5;
    char *text = xcalloc(total, 1);
    snprintf(text, total, "\x01%s: %s\n", netname, joined);
    free(joined);
    double teamplay = qa_cvar_registry_variable_value(qa_app_sim_quakec_source_cvars(h->game), "teamplay");
    for (size_t o = 0; o < h->order_count; o++) {
      const qa_app_q1_player *recipient = &h->players[(size_t)h->order[o]].player;
      bool same_team = false;
      if (is(name, "say_team") && teamplay != 0) {
        double recipient_team = scalar(h, recipient->source_entity, "team");
        same_team = recipient_team == scalar(h, player.source_entity, "team");
      }
      if (!is(name, "say_team") || teamplay == 0 || same_team) {
        qa_netquake_message message;
        memset(&message, 0, sizeof message);
        message.kind = "print"; message.text = xstrdup(text);
        admit_command(h, message, true, recipient->actor);
      }
    }
    free(text);
  } else s->player_command(s->ctx, player.actor, name, args, count);
}
static bool q1_provider(const char *provider) { return strncmp(provider, "q1:", 3) == 0; }
static const void *host_supports_source_wire(void *ctx) {
  quakec_host *h = ctx;
  const qa_executable_recipe *recipe = h->simulation->recipe;
  bool weapons = true;
  for (size_t i = 0; i < recipe->weapons.count; i++) if (!q1_provider(recipe->weapons.values[i].provider)) weapons = false;
  qa_wire_admission_destroy(&h->support);
  h->support.supported = q1_provider(recipe->movement.provider) && q1_provider(recipe->character.definition.provider)
      && q1_provider(recipe->inventory.provider) && weapons;
  if (!h->support.supported) {
    h->support.reasons = xcalloc(1, sizeof *h->support.reasons);
    h->support.reasons[0] = xstrdup("Mixed composition requires unified serialization");
    h->support.reason_count = 1;
  }
  return &h->support;
}
static bool host_rejects(void *ctx, const void *address) { quakec_host *h = ctx; return h->options.rejects(h->options.rejects_ctx, address); }
static void host_print(void *ctx, const char *text) { quakec_host *h = ctx; h->options.print(h->options.print_ctx, text); }
static void client_set(quakec_host *h, double slot, qa_app_q1_player player) {
  per_player *entry = player_slot(h, slot);
  entry->used = true; entry->player = player;
  order_add(&h->order, &h->order_count, slot);
}
static void host_destroy(void *ctx) {
  quakec_host *h = ctx;
  free(h->order); free(h->board_order);
  for (size_t i = 0; i < h->player_count; i++) clear_player(&h->players[i]);
  free(h->players);
  for (size_t i = 0; i < h->board_count; i++) free(h->board[i].name);
  free(h->board);
  routed_clear(&h->routed); routed_clear(&h->commands);
  for (size_t i = 0; i < h->input_count; i++) free(h->inputs[i]);
  free(h->inputs);
  free(h->models.keys); free(h->models.values); free(h->sounds.keys); free(h->sounds.values);
  qa_wire_admission_destroy(&h->support);
  free(h->map_name);
  free(h);
}

qa_app_q1_server_host qa_app_sim_create_quakec_netquake_host(qa_app_sim_q1_server_binding_options options) {
  const qa_app_sim_network_simulation *s = options.simulation;
  qa_app_sim_quakec_source *game = options.quakec;
  if (game == NULL || !is(qa_app_sim_quakec_source_kind(game), "netquake"))
    qa_raise("Error", 0, 0, "Native NetQuake requires a NetQuake QuakeC source");
  quakec_host *h = xcalloc(1, sizeof *h);
  qa_cleanup guard;
  qa_cleanup_push(&guard, host_destroy, h);
  h->destroy = host_destroy;
  h->options = options; h->simulation = s; h->game = game;
  /* game.options.maxClients: the QuakeC source is built with the simulation's maxClients. */
  h->max_clients = s->options->max_clients;
  qa_message_reader empty;
  qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec;
  qa_create_netquake_codec(options.protocol, &empty, &codec);
  h->wide = options.protocol.version != 15;
  size_t model_count = 0, sound_count = 0;
  const char *const *models = qa_app_sim_quakec_source_precache_names(game, "model", &model_count);
  map_build(&h->models, models, model_count);
  const char *const *sounds = qa_app_sim_quakec_source_precache_names(game, "sound", &sound_count);
  map_build(&h->sounds, sounds, sound_count);
  if ((double)h->models.count + 1 > codec.max_precache || (double)h->sounds.count + 1 > codec.max_precache)
    qa_raise("Error", 0, 0, "Native QuakeC precaches exceed selected NetQuake protocol");
  qa_content_id owner = qa_app_sim_quakec_source_prepared(game)->execution.owner.content;
  const qa_app_sim_network_content *content = options.content;
  for (size_t i = 0; i < h->models.count; i++) {
    qa_resolved_resource_reference resource;
    if (h->models.keys[i][0] != '*' && !content->resolve(content->ctx, owner, h->models.keys[i], &resource))
      qa_raise("Error", 0, 0, "Missing QuakeC model %s", h->models.keys[i]);
  }
  for (size_t i = 0; i < h->sounds.count; i++) {
    size_t length = strlen(h->sounds.keys[i]) + 7;
    char *path = xcalloc(length, 1);
    snprintf(path, length, "sound/%s", h->sounds.keys[i]);
    qa_resolved_resource_reference resource;
    if (!content->resolve(content->ctx, owner, path, &resource)) {
      free(path);
      qa_raise("Error", 0, 0, "Missing QuakeC sound %s", h->sounds.keys[i]);
    }
    s->register_resource(s->ctx, owner, path, resource);
    free(path);
  }
  h->previous_pause = s->q1_paused(s->ctx);
  qa_app_sim_quakec_source_attach_netquake_wire(game);
  h->map_name = xstrdup(global_string(h, "mapname"));
  qa_cleanup_pop(&guard);
  qa_app_q1_server_host host;
  memset(&host, 0, sizeof host);
  host.ctx = h;
  if (options.rejects) host.rejects = host_rejects;
  host.protocol = options.protocol;
  host.max_clients = h->max_clients;
  host.map_name = h->map_name;
  host.supports_source_wire = host_supports_source_wire;
  host.admit = host_admit;
  host.carried_player = host_carried_player;
  host.disconnect = host_disconnect;
  host.game_state = host_game_state;
  host.spawn = host_spawn;
  host.frame = host_frame;
  host.input = host_input;
  host.command = host_command;
  host.observe = host_observe;
  host.print = host_print;
  return host;
}
