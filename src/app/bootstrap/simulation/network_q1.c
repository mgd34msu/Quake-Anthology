/* Port of src/app/bootstrap/simulation/network-q1.ts. The host ctx of both Q1 hosts
 * begins with its destroy function. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q1.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/network_q1_quakec.h"
#include "qa/content/q1/foundation/entity.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/foundation/precache.h"
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
static qa_netquake_message clone_message(const qa_netquake_message *message) {
  qa_netquake_message out = *message;
  out.text = xstrdup(message->text); out.level = xstrdup(message->level); out.str_value = xstrdup(message->str_value);
  out.models = message->model_count ? xcalloc(message->model_count, sizeof *out.models) : NULL;
  for (size_t i = 0; i < message->model_count; i++) out.models[i] = xstrdup(message->models[i]);
  out.sounds = message->sound_count ? xcalloc(message->sound_count, sizeof *out.sounds) : NULL;
  for (size_t i = 0; i < message->sound_count; i++) out.sounds[i] = xstrdup(message->sounds[i]);
  return out;
}
static qa_netquake_message blank(const char *kind) {
  qa_netquake_message message;
  memset(&message, 0, sizeof message);
  message.kind = kind;
  return message;
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
typedef struct board_entry { bool used; char *name; double colors, frags; } board_entry;
typedef struct per_player {
  bool used; qa_app_q1_player player;
  message_list spawn, messages, reliable, datagram, signon;
  qa_q1_extended_entity_state *entities; size_t entity_count;
  qa_netquake_message info; bool has_info;
  qa_app_q1_baseline_entry *baselines; size_t baseline_count; qa_app_q1_baselines baseline_view;
} per_player;
typedef struct q1_host {
  void (*destroy)(void *ctx);
  qa_app_sim_q1_server_binding_options options;
  const qa_app_sim_network_simulation *simulation;
  qa_comp_q1_source_composition *source;
  qa_q1_services *game;
  double max_clients;
  bool wide;
  const char **models; size_t model_count;  /* precaches.models.slice(1) */
  const char **sounds; size_t sound_count;
  qa_resource_id *sound_resources;          /* parallel to sounds */
  routed_list routed, operators;
  bool previous_pause;
  board_entry *board; size_t board_count;
  double *board_order; size_t board_order_count;
  qa_actor_id *muzzle_flashes; size_t muzzle_flash_count;
  per_player *players; size_t player_count;
  qa_wire_admission support;
  qa_actor_command **inputs; size_t input_count;
} q1_host;

void qa_app_sim_q1_application_server_host_destroy(qa_app_q1_server_host host) {
  if (host.ctx == NULL) return;
  void (**destroy)(void *) = host.ctx;
  (*destroy)(host.ctx);
}

static per_player *player_slot(q1_host *h, double slot) {
  if (slot < 0 || slot != trunc(slot)) qa_range_error("Invalid Q1 client slot");
  if ((size_t)slot >= h->player_count) {
    size_t count = (size_t)slot + 1;
    h->players = xrealloc(h->players, count * sizeof *h->players);
    memset(h->players + h->player_count, 0, (count - h->player_count) * sizeof *h->players);
    h->player_count = count;
  }
  return &h->players[(size_t)slot];
}
static board_entry *board_slot(q1_host *h, double slot) {
  if ((size_t)slot >= h->board_count) {
    size_t count = (size_t)slot + 1;
    h->board = xrealloc(h->board, count * sizeof *h->board);
    memset(h->board + h->board_count, 0, (count - h->board_count) * sizeof *h->board);
    h->board_count = count;
  }
  return &h->board[(size_t)slot];
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
/* index(path, table): Map(precaches.slice(1).map((path, ordinal) => [path, ordinal + 1])). */
static bool table_get(const char *const *table, size_t count, const char *path, double *out) {
  bool found = false;
  for (size_t i = 0; i < count; i++) if (strcmp(table[i], path) == 0) { *out = (double)i + 1; found = true; }
  return found;
}
static double index_of(const char *const *table, size_t count, const char *path) {
  if (path == NULL || path[0] == '\0') return 0;
  double value;
  if (!table_get(table, count, path, &value)) qa_raise("Error", 0, 0, "Q1 resource was not precached before signon: %s", path);
  return value;
}
static double model_index(q1_host *h, const char *path) { return index_of(h->models, h->model_count, path); }
static double sound_index(q1_host *h, const char *path) { return index_of(h->sounds, h->sound_count, path); }
static double number(q1_host *h, qa_actor_id actor) {
  qa_actor_source_result address = qa_session_actor_registry_source_of(h->simulation->actors, actor);
  if (!address.has_value || !is(address.provider, h->simulation->recipe->map.entities.provider))
    qa_raise("Error", 0, 0, "Q1 actor has no source address");
  return (double)address.slot;
}
static void visual(q1_host *h, double alpha, double scale, qa_q1_extended_entity_state *state) {
  if (h->wide) { state->alpha = qa_q1_entalpha_encode(alpha); state->scale = qa_js_bit_and(trunc(qa_q1_entscale_encode(scale)), 255); }
  else { state->alpha = 0; state->scale = 16; }
}
static bool flashing(q1_host *h, qa_actor_id actor) {
  for (size_t i = 0; i < h->muzzle_flash_count; i++) if (qa_same_actor(h->muzzle_flashes[i], actor)) return true;
  return false;
}
static const qa_app_simulation_presentation *presentation_of(const qa_app_simulation_presentation *values, size_t count, qa_actor_id actor) {
  const qa_app_simulation_presentation *found = NULL;
  for (size_t i = 0; i < count; i++) if (!values[i].view_weapon && qa_same_actor(values[i].actor, actor)) found = &values[i];
  return found;
}
static int compare_numbers(const void *a, const void *b) {
  double left = ((const qa_q1_extended_entity_state *)a)->base.number, right = ((const qa_q1_extended_entity_state *)b)->base.number;
  return left < right ? -1 : left > right ? 1 : 0;
}
/* entities() */
static qa_q1_extended_entity_state *entities(q1_host *h, size_t *out_count) {
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t presentation_count = 0, player_count = 0;
  const qa_app_simulation_presentation *presentations = s->presentations(s->ctx, &presentation_count);
  const qa_actor_id *players = s->players(s->ctx, &player_count);
  qa_q1_extended_entity_state *states = xcalloc(h->game->entity_count + player_count, sizeof *states);
  size_t count = 0;
  for (size_t i = 0; i < h->game->entity_count; i++) {
    const qa_q1_actor *entity = h->game->entities[i];
    qa_actor_id actor = entity->actor.id;
    qa_body_state_result body = qa_shared_body_table_read(s->bodies, actor);
    const qa_app_simulation_presentation *presentation = presentation_of(presentations, presentation_count, actor);
    const char *path = presentation ? presentation->path : entity->model;
    if (!body.has_value || path == NULL || path[0] == '\0' || number(h, actor) == 0) continue;
    qa_q1_extended_entity_state state;
    memset(&state, 0, sizeof state);
    state.base.number = number(h, actor);
    state.base.origin = body.value.origin; state.base.angles = body.value.angles;
    state.base.model_index = model_index(h, path);
    state.base.frame = presentation ? presentation->frame : entity->frame;
    state.base.color_map = qa_q1_player(h->game, actor) == NULL ? 0 : number(h, actor);
    state.base.skin = presentation ? presentation->skin : entity->skin;
    state.base.effects = qa_js_bit_or(presentation ? presentation->effects : entity->effects, flashing(h, actor) ? 2 : 0);
    double alpha = qa_q1_actor_number(entity, "alpha");
    double scale = qa_q1_actor_number(entity, "scale");
    visual(h, alpha, scale, &state);
    state.lerp_finish_seconds = 0;
    state.step = is(entity->movement, "step");
    states[count++] = state;
  }
  for (size_t i = 0; i < player_count; i++) {
    qa_actor_id actor = players[i];
    qa_body_state_result body = qa_shared_body_table_read(s->bodies, actor);
    const qa_app_simulation_presentation *presentation = presentation_of(presentations, presentation_count, actor);
    if (!body.has_value) continue;
    qa_q1_extended_entity_state state;
    memset(&state, 0, sizeof state);
    state.base.number = number(h, actor);
    state.base.origin = body.value.origin; state.base.angles = body.value.angles;
    state.base.model_index = model_index(h, presentation ? presentation->path : "progs/player.mdl");
    state.base.frame = presentation ? presentation->frame : 0;
    state.base.color_map = number(h, actor);
    state.base.skin = presentation ? presentation->skin : 0;
    state.base.effects = qa_js_bit_or(presentation ? presentation->effects : 0, flashing(h, actor) ? 2 : 0);
    const qa_q1_player_state *native = qa_q1_player(h->game, actor);
    visual(h, native ? native->alpha : 0, native ? native->scale : 0, &state);
    state.lerp_finish_seconds = 0;
    state.step = false;
    states[count++] = state;
  }
  /* Entity numbers are unique source slots, so the unstable sort orders as TS does. */
  qsort(states, count, sizeof *states, compare_numbers);
  *out_count = count;
  return states;
}
static qa_q1_extended_entity_state baseline(q1_host *h, qa_q1_extended_entity_state state) {
  bool player = state.base.number > 0 && state.base.number <= h->max_clients;
  double model = player ? model_index(h, "progs/player.mdl") : state.base.model_index;
  double frame = trunc(state.base.frame);
  qa_q1_extended_entity_state out = state;
  out.base.model_index = !h->wide && qa_js_bit_and(model, 0xff00) != 0 ? 0 : model;
  out.base.frame = !h->wide && qa_js_bit_and(frame, 0xff00) != 0 ? 0 : frame;
  out.base.skin = trunc(state.base.skin);
  out.base.effects = 0;
  out.base.color_map = player ? state.base.number : 0;
  out.alpha = player ? 0 : state.alpha;
  out.scale = player || h->options.protocol.version != 999 ? 16 : state.scale;
  out.lerp_finish_seconds = 0;
  out.step = false;
  return out;
}
static double count_of(q1_host *h, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(h->simulation->inventory, actor, item);
}
static qa_netquake_message client_data(q1_host *h, qa_app_q1_player player) {
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_app_sim_movement_player *movement = s->movement_player(s->ctx, player.actor);
  qa_q1_player_state *native = qa_q1_player(h->game, player.actor);
  qa_app_sim_player_ui ui = s->player_ui(s->ctx, player.actor);
  if (movement == NULL || movement->state.kind != QA_MOVEMENT_STATE_Q1 || native == NULL)
    qa_raise("Error", 0, 0, "Q1 native movement/player state unavailable");
  const qa_q1_movement_state *state = &movement->state.as.q1;
  static const double weapon_bits[8] = {4096, 1, 2, 4, 8, 16, 32, 64};
  double items = 0, active = 0;
  for (size_t ordinal = 0; ordinal < 8; ordinal++) {
    if (count_of(h, player.actor, qa_q1_services_weapon_item(h->game, QA_Q1_WEAPONS[ordinal])) > 0)
      items = qa_js_bit_or(items, weapon_bits[ordinal]);
    if (active == 0 && is(QA_Q1_WEAPONS[ordinal], native->weapon)) active = weapon_bits[ordinal];
  }
  const qa_regular_armor_state *armor = &ui.armor.regular;
  if (armor->kind != QA_REGULAR_ARMOR_NONE)
    items = qa_js_bit_or(items, armor->kind == QA_REGULAR_ARMOR_Q1 && armor->absorption >= 0.8 ? 32768
        : armor->kind == QA_REGULAR_ARMOR_Q1 && armor->absorption >= 0.6 ? 16384 : 8192);
  for (size_t i = 0; i < native->powerup_count; i++) {
    const qa_q1_powerup_entry *entry = &native->powerups[i];
    if (entry->expires > h->game->time)
      items = qa_js_bit_or(items, is(entry->kind, "quad") ? 4194304 : is(entry->kind, "invulnerability") ? 1048576
          : is(entry->kind, "invisibility") ? 524288 : is(entry->kind, "suit") ? 2097152 : 0);
  }
  qa_netquake_message message = blank("client-data");
  message.weapon_alpha = h->wide ? qa_q1_entalpha_encode(native->alpha) : 0;
  qa_q1_client_data *data = &message.data;
  data->view_height = movement->view_height;
  data->ideal_pitch = state->ideal_pitch;
  data->punch_angles = state->punch_angles;
  data->velocity = state->velocity;
  data->items = items;
  data->on_ground = qa_js_bit_and(state->flags, 512) != 0;
  data->in_water = state->water_level >= 2;
  data->weapon_frame = native->weapon_frame;
  data->armor = armor->kind == QA_REGULAR_ARMOR_NONE ? 0 : armor->points;
  data->weapon_model = model_index(h, qa_q1_services_weapon_model(h->game, native->weapon, native));
  data->health = ui.health;
  data->ammo = ui.has_ammo ? ui.ammo_count : 0;
  data->shells = count_of(h, player.actor, "q1:ammo/shells");
  data->nails = count_of(h, player.actor, "q1:ammo/nails");
  data->rockets = count_of(h, player.actor, "q1:ammo/rockets");
  data->cells = count_of(h, player.actor, "q1:ammo/cells");
  data->active_weapon = active; /* weaponBits[findIndex] ?? 0 */
  return message;
}
static qa_netquake_message text_message(const char *kind, double slot, const char *value) {
  qa_netquake_message message = blank(kind);
  message.slot = slot; message.str_value = xstrdup(value);
  return message;
}
static qa_netquake_message value_message(const char *kind, double slot, double value) {
  qa_netquake_message message = blank(kind);
  message.slot = slot; message.value = value;
  return message;
}
static void persistent_signon(q1_host *h, message_list *out) {
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t count = 0;
  const qa_app_sim_presentation_event *const *persistent = s->persistent_events(s->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    if (!is(persistent[i]->kind, "q1")) continue;
    const qa_q1_event *event = persistent[i]->q1;
    if (is(event->kind, "ambient")) {
      qa_netquake_message message = blank("static-sound");
      message.entity = 0; message.channel = 0; message.index = sound_index(h, event->path);
      message.volume = trunc(event->volume * 255); message.attenuation = event->attenuation; message.origin = event->origin;
      list_push(out, message);
    } else if (is(event->kind, "static-model")) {
      qa_netquake_message message = blank("static");
      message.state.base.number = 0; message.state.base.model_index = model_index(h, event->path);
      message.state.base.frame = event->frame; message.state.base.color_map = event->color_map; message.state.base.skin = event->skin;
      message.state.base.effects = 0; message.state.base.origin = event->origin; message.state.base.angles = event->angles;
      message.state.alpha = 0; message.state.scale = 16; message.state.lerp_finish_seconds = 0; message.state.step = false;
      list_push(out, message);
    }
  }
}
static void send(q1_host *h, qa_netquake_message message, bool reliable, const qa_actor_id *recipient) {
  routed value;
  memset(&value, 0, sizeof value);
  value.message = message; value.reliable = reliable;
  if (recipient) { value.has_recipient = true; value.recipient = *recipient; }
  routed_push(&h->routed, value);
}
static double sound_channel(const qa_q1_event *event) {
  if (event->sound_channel == NULL) return event->channel;
  const char *name = event->sound_channel;
  return is(name, "auto") ? 0 : is(name, "weapon") ? 1 : is(name, "voice") ? 2 : is(name, "item") ? 3 : is(name, "body") ? 4 : NAN;
}
static void host_observe(void *ctx, const void *output_value, const void *const *events_value, size_t count) {
  q1_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  const qa_simulation_output *output = output_value;
  const qa_app_sim_presentation_event *const *events = (const qa_app_sim_presentation_event *const *)events_value;
  for (size_t i = 0; i < h->input_count; i++) free(h->inputs[i]);
  h->input_count = 0;
  routed_clear(&h->routed);
  h->routed = h->operators; /* routed = operatorMessages.drainAll() */
  memset(&h->operators, 0, sizeof h->operators);
  bool paused = s->q1_paused(s->ctx);
  if (h->previous_pause != paused) {
    h->previous_pause = paused;
    qa_netquake_message message = blank("pause");
    message.paused = paused;
    send(h, message, true, NULL);
  }
  h->muzzle_flash_count = 0;
  /* capturedSounds: the output's sound payloads, consumed as they match. */
  bool *used = xcalloc(output->event_count, sizeof *used);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, used);
  qa_comp_q1_source_clients *clients = &h->source->clients;
  double *live = xcalloc(clients->record_count, sizeof *live);
  qa_cleanup live_guard;
  qa_cleanup_push(&live_guard, free, live);
  for (size_t i = 0; i < clients->record_count; i++) {
    const qa_comp_q1_source_client *client = &clients->records[i];
    double slot = (double)client->slot;
    live[i] = slot;
    board_entry *previous = board_slot(h, slot);
    const char *name = qa_comp_q1_source_client_name(client);
    double shirt = (double)qa_comp_q1_source_client_shirt(client);
    double colors = shirt * 16 + (double)qa_comp_q1_source_client_pants(client);
    if (!previous->used || !is(previous->name, name)) send(h, text_message("name", slot, name), true, NULL);
    if (!previous->used || previous->colors != colors) send(h, value_message("colors", slot, colors), true, NULL);
    if (!previous->used || previous->frags != client->frags) send(h, value_message("frags", slot, client->frags), true, NULL);
    char *copy = xstrdup(name);
    free(previous->name);
    previous->used = true; previous->name = copy; previous->colors = colors; previous->frags = client->frags;
    order_add(&h->board_order, &h->board_order_count, slot);
  }
  size_t snapshot_count = h->board_order_count;
  double *snapshot = xcalloc(snapshot_count, sizeof *snapshot);
  if (snapshot_count) memcpy(snapshot, h->board_order, snapshot_count * sizeof *snapshot);
  for (size_t k = 0; k < snapshot_count; k++) {
    bool alive = false;
    for (size_t i = 0; i < clients->record_count && !alive; i++) alive = live[i] == snapshot[k];
    if (alive) continue;
    send(h, text_message("name", snapshot[k], ""), true, NULL);
    send(h, value_message("colors", snapshot[k], 0), true, NULL);
    send(h, value_message("frags", snapshot[k], 0), true, NULL);
    board_entry *entry = &h->board[(size_t)snapshot[k]];
    free(entry->name);
    memset(entry, 0, sizeof *entry);
    order_remove(h->board_order, &h->board_order_count, snapshot[k]);
  }
  free(snapshot);
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *record = events[i];
    if (is(record->kind, "view-reset")) {
      qa_netquake_message message = blank("set-angle");
      message.angles = record->view_reset.angles;
      send(h, message, true, &record->view_reset.actor);
      continue;
    }
    if (!is(record->kind, "q1")) continue;
    const qa_q1_event *event = record->q1;
    const char *kind = event->kind;
    if (is(kind, "sound")) {
      if (!is(record->content, s->recipe->map.entities.content)) qa_raise("Error", 0, 0, "Native Q1 sound belongs to another content provider");
      double sound;
      if (!table_get(h->sounds, h->sound_count, event->path, &sound)) {
        char text[512];
        snprintf(text, sizeof text, "SV_StartSound: %s not precached\n", event->path);
        h->options.print(h->options.print_ctx, text);
        continue;
      }
      double channel = sound_channel(event);
      qa_resource_id resource = NULL;
      for (size_t r = 0; r < h->sound_count; r++) if (strcmp(h->sounds[r], event->path) == 0) resource = h->sound_resources[r];
      const qa_simulation_event *captured = NULL;
      for (size_t c = 0; c < output->event_count && captured == NULL; c++) {
        const qa_simulation_event_payload *payload = &output->events[c].payload;
        if (used[c] || payload->kind != QA_SIM_EVENT_SOUND) continue;
        if (payload->actor != NULL && event->has_actor && qa_same_actor(*payload->actor, event->actor) && payload->channel == channel
            && is(payload->resource, resource)) { captured = &output->events[c]; used[c] = true; }
      }
      bool has_entity = record->has_source_entity;
      double entity = record->source_entity;
      if (!has_entity && event->has_actor) {
        qa_actor_source_result address = qa_session_actor_registry_source_of(s->actors, event->actor);
        if (address.has_value) { has_entity = true; entity = (double)address.slot; }
      }
      if (captured == NULL || !has_entity) {
        char text[512];
        snprintf(text, sizeof text, "Q1 sound has no emission-time origin: %s\n", event->path);
        h->options.print(h->options.print_ctx, text);
        continue;
      }
      qa_netquake_message message = blank("sound");
      message.entity = entity; message.channel = channel; message.index = sound;
      message.volume = trunc(event->volume * 255); message.attenuation = event->attenuation; message.origin = captured->payload.origin;
      send(h, message, false, NULL);
    } else if (is(kind, "message")) {
      qa_netquake_message message = blank(event->center ? "center-print" : "print");
      message.text = xstrdup(event->text);
      send(h, message, true, event->has_player ? &event->player : NULL);
    } else if (is(kind, "lightstyle")) {
      qa_netquake_message message = text_message("light-style", 0, event->pattern);
      message.index = event->style;
      send(h, message, true, NULL);
    } else if (is(kind, "particles")) {
      qa_netquake_message message = blank("particle");
      double d[3] = {event->direction.x, event->direction.y, event->direction.z};
      for (int k = 0; k < 3; k++) d[k] = qa_js_max(-128, qa_js_min(127, trunc(d[k] * 16))) / 16;
      message.origin = event->origin; message.direction = (qa_vec3){d[0], d[1], d[2]};
      message.count = event->count; message.color_value = event->color;
      send(h, message, false, NULL);
    } else if (is(kind, "colored-explosion")) {
      qa_netquake_message message = blank("temporary-entity");
      message.effect.kind = QA_TE_EXPLOSION_COLORS; message.effect.type = 12; message.effect.origin = event->origin;
      message.effect.color_start = event->color_start; message.effect.color_length = event->color_length;
      send(h, message, false, NULL);
    } else if (is(kind, "beam")) {
      const char *style = event->beam_style;
      qa_netquake_message message = blank("temporary-entity");
      message.effect.kind = QA_TE_BEAM;
      message.effect.type = is(style, "lightning1") ? 5 : is(style, "lightning2") ? 6 : is(style, "lightning3") ? 9 : 13;
      message.effect.entity = record->has_source_entity ? record->source_entity : number(h, event->actor);
      message.effect.start = event->start; message.effect.end = event->end;
      send(h, message, false, NULL);
    } else if (is(kind, "effect")) {
      const char *effect = event->effect;
      if (is(effect, "muzzleflash")) {
        if (event->has_actor) {
          h->muzzle_flashes = xrealloc(h->muzzle_flashes, (h->muzzle_flash_count + 1) * sizeof *h->muzzle_flashes);
          h->muzzle_flashes[h->muzzle_flash_count++] = event->actor;
        }
      } else if (is(effect, "pickup")) {
        if (event->has_actor) { qa_netquake_message message = blank("stufftext"); message.text = xstrdup("bf\n"); send(h, message, true, &event->actor); }
      } else if (is(effect, "blood")) {
        qa_netquake_message message = blank("particle");
        message.origin = event->origin; message.direction = (qa_vec3){0, 0, 0}; message.count = event->amount * 2; message.color_value = 73;
        send(h, message, false, NULL);
      } else if (is(effect, "meat-spray")) {
        h->options.print(h->options.print_ctx, "Q1 meat spray requires a source entity; no temporary-entity substitute emitted\n");
      } else {
        static const struct { const char *name; double type; } types[9] = {{"gunshot", 2}, {"spike", 0}, {"superspike", 1},
          {"explosion", 3}, {"teleport", 11}, {"lava-splash", 10}, {"tar-explosion", 4}, {"wizard-spike", 7}, {"knight-spike", 8}};
        double type = NAN; /* types[unknown] is undefined */
        for (int t = 0; t < 9; t++) if (is(effect, types[t].name)) type = types[t].type;
        qa_netquake_message message = blank("temporary-entity");
        message.effect.kind = QA_TE_POINT; message.effect.type = type; message.effect.origin = event->origin; message.effect.count = 1;
        send(h, message, false, NULL);
      }
    } else if (is(kind, "teleport-player") || is(kind, "camera")) {
      qa_netquake_message message = blank("set-angle");
      message.angles = event->angles;
      send(h, message, true, event->has_player ? &event->player : NULL);
    } else if (is(kind, "secret")) send(h, blank("found-secret"), true, NULL);
    else if (is(kind, "monster-killed")) send(h, blank("killed-monster"), true, NULL);
    else if (is(kind, "monster-total")) {
      qa_netquake_message message = blank("stat");
      message.index = 12; message.value = event->total;
      send(h, message, true, NULL);
    } else if (is(kind, "intermission")) send(h, blank("intermission"), true, NULL);
    else if (is(kind, "finale")) {
      qa_netquake_message message = blank("finale");
      message.text = xstrdup(event->text);
      send(h, message, true, NULL);
    }
    /* ambient, weapon, powerup, server-command, achievement: nothing on the wire. */
  }
  qa_cleanup_pop(&live_guard);
  free(live);
  qa_cleanup_pop(&guard);
  free(used);
}
static bool q1_provider(const char *provider) { return strncmp(provider, "q1:", 3) == 0; }
static const void *host_supports_source_wire(void *ctx) {
  q1_host *h = ctx;
  const qa_executable_recipe *recipe = h->simulation->recipe;
  const char *reasons[2]; size_t count = 0;
  if (h->source->selection.program != QA_COMP_Q1_ID1) reasons[count++] = "Native NetQuake application item serialization currently binds id1";
  bool weapons = true;
  for (size_t i = 0; i < recipe->weapons.count; i++) if (!q1_provider(recipe->weapons.values[i].provider)) weapons = false;
  if (!q1_provider(recipe->movement.provider) || !q1_provider(recipe->character.definition.provider)
      || !q1_provider(recipe->inventory.provider) || !weapons) reasons[count++] = "Mixed composition requires unified serialization";
  qa_wire_admission_destroy(&h->support);
  h->support.supported = count == 0;
  h->support.reasons = count ? xcalloc(count, sizeof *h->support.reasons) : NULL;
  for (size_t i = 0; i < count; i++) h->support.reasons[i] = xstrdup(reasons[i]);
  h->support.reason_count = count;
  return &h->support;
}
static bool slot_taken(q1_host *h, double slot) {
  if (slot < (double)h->player_count && h->players[(size_t)slot].used) return true;
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t count = 0;
  const qa_actor_id *players = s->players(s->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *movement = s->movement_player(s->ctx, players[i]);
    if (movement != NULL && (double)movement->client.slot == slot) return true;
  }
  return false;
}
typedef struct admit_scope { q1_host *h; qa_client_id id; double slot; bool has_actor; qa_actor_id actor; qa_app_q1_player player; } admit_scope;
static void admit_body(void *ctx) {
  admit_scope *a = ctx;
  const qa_app_sim_network_simulation *s = a->h->simulation;
  qa_app_sim_player_admission admitted = s->admit_player(s->ctx, a->id);
  a->actor = admitted.actor; a->has_actor = true;
  qa_app_q1_player player = {a->id, admitted.actor, number(a->h, admitted.actor)};
  per_player *entry = player_slot(a->h, a->slot);
  entry->used = true; entry->player = player;
  a->player = player;
}
static qa_app_q1_admission host_admit(void *ctx, const void *from_value) {
  q1_host *h = ctx;
  const qa_net_address *from = from_value;
  qa_app_q1_admission result;
  memset(&result, 0, sizeof result);
  double slot = 0;
  for (; slot < h->max_clients; slot++) if (!slot_taken(h, slot)) break;
  if (slot == h->max_clients) { result.reason = "Server is full"; return result; }
  qa_session_client *client = qa_engine_session_create_client(h->options.session, slot);
  qa_session_client_connect(client, from->kind == QA_NET_LOOPBACK ? "loopback" : "remote");
  admit_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.h = h; scope.id = qa_session_client_id(client); scope.slot = slot;
  qa_caught_error error;
  if (qa_try(admit_body, &scope, &error)) { result.accepted = true; result.player = scope.player; return result; }
  if (scope.has_actor) h->simulation->disconnect_player(h->simulation->ctx, scope.actor);
  qa_engine_session_close_client(h->options.session, scope.id);
  qa_rethrow(&error);
}
static qa_app_q1_player host_carried_player(void *ctx, qa_client_id client) {
  q1_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t count = 0;
  const qa_actor_id *players = s->players(s->ctx, &count);
  const qa_actor_id *found = NULL;
  for (size_t i = 0; i < count && found == NULL; i++) {
    qa_app_sim_movement_player *movement = s->movement_player(s->ctx, players[i]);
    if (movement != NULL && qa_same_client(movement->client, client)) found = &players[i];
  }
  if (found == NULL) qa_raise("Error", 0, 0, "Carried Q1 player has not been admitted");
  qa_app_q1_player player = {client, *found, number(h, *found)};
  per_player *entry = player_slot(h, (double)client.slot);
  entry->used = true; entry->player = player;
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
  q1_host *h = ctx;
  h->simulation->disconnect_player(h->simulation->ctx, player.actor);
  qa_engine_session_close_client(h->options.session, player.client);
  per_player *entry = player_slot(h, (double)player.client.slot);
  clear_player(entry);
  entry->used = false;
}
static qa_app_q1_game_state host_game_state(void *ctx, qa_app_q1_player player) {
  q1_host *h = ctx;
  per_player *entry = player_slot(h, (double)player.client.slot);
  clear_player(entry);
  size_t count = 0;
  qa_q1_extended_entity_state *states = entities(h, &count);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, states);
  qa_netquake_message data = client_data(h, player);
  qa_netquake_message_destroy(&data);
  entry->baselines = xcalloc(count, sizeof *entry->baselines);
  for (size_t i = 0; i < count; i++) {
    qa_q1_extended_entity_state value = baseline(h, states[i]);
    size_t at = 0; /* Map by number: a repeated number keeps its first position */
    while (at < entry->baseline_count && entry->baselines[at].number != value.base.number) at++;
    entry->baselines[at].number = value.base.number; entry->baselines[at].state = value;
    if (at == entry->baseline_count) entry->baseline_count++;
  }
  qa_cleanup_pop(&guard);
  free(states);
  entry->info = blank("server-info");
  entry->info.protocol = h->options.protocol;
  entry->info.max_clients = h->max_clients;
  entry->info.game_type = h->game->options.deathmatch == 0 ? 0 : 1;
  const char *message = h->game->world ? h->game->world->message : NULL;
  entry->info.level = xstrdup(message && message[0] ? message : h->game->map_name);
  entry->info.models = h->model_count ? xcalloc(h->model_count, sizeof *entry->info.models) : NULL;
  for (size_t i = 0; i < h->model_count; i++) entry->info.models[i] = xstrdup(h->models[i]);
  entry->info.model_count = h->model_count;
  entry->info.sounds = h->sound_count ? xcalloc(h->sound_count, sizeof *entry->info.sounds) : NULL;
  for (size_t i = 0; i < h->sound_count; i++) entry->info.sounds[i] = xstrdup(h->sounds[i]);
  entry->info.sound_count = h->sound_count;
  entry->has_info = true;
  persistent_signon(h, &entry->signon);
  entry->baseline_view.entries = entry->baselines; entry->baseline_view.count = entry->baseline_count;
  qa_app_q1_game_state state = {&entry->info, &entry->baseline_view, (const void *const *)entry->signon.values, entry->signon.count};
  return state;
}
static const void *const *host_spawn(void *ctx, qa_app_q1_player player, size_t *out_count) {
  q1_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  per_player *entry = player_slot(h, (double)player.client.slot);
  list_clear(&entry->spawn);
  qa_netquake_message message = blank("pause");
  message.paused = s->q1_paused(s->ctx);
  list_push(&entry->spawn, message);
  message = blank("time");
  message.seconds = h->game->time;
  list_push(&entry->spawn, message);
  for (double index = 0; index < 64; index++) {
    message = text_message("light-style", 0, s->light_style(s->ctx, index));
    message.index = index;
    list_push(&entry->spawn, message);
  }
  const qa_comp_q1_source_clients *clients = &h->source->clients;
  for (size_t i = 0; i < clients->record_count; i++) {
    const qa_comp_q1_source_client *client = &clients->records[i];
    double slot = (double)client->slot;
    list_push(&entry->spawn, text_message("name", slot, qa_comp_q1_source_client_name(client)));
    double shirt = (double)qa_comp_q1_source_client_shirt(client);
    list_push(&entry->spawn, value_message("colors", slot, shirt * 16 + (double)qa_comp_q1_source_client_pants(client)));
    list_push(&entry->spawn, value_message("frags", slot, client->frags));
  }
  double stats[4][2] = {{11, h->game->total_secrets}, {12, h->game->total_monsters}, {13, h->game->found_secrets}, {14, h->game->killed_monsters}};
  for (int i = 0; i < 4; i++) {
    message = blank("stat");
    message.index = stats[i][0]; message.value = stats[i][1];
    list_push(&entry->spawn, message);
  }
  message = blank("set-angle");
  message.angles = s->player_view(s->ctx, player.actor).angles;
  list_push(&entry->spawn, message);
  list_push(&entry->spawn, client_data(h, player));
  *out_count = entry->spawn.count;
  return (const void *const *)entry->spawn.values;
}
static void routed_for(q1_host *h, qa_app_q1_player player, bool reliable, message_list *out) {
  list_clear(out);
  for (size_t i = 0; i < h->routed.count; i++) {
    const routed *event = &h->routed.values[i];
    if (event->reliable == reliable && (!event->has_recipient || qa_same_actor(event->recipient, player.actor)))
      list_push(out, clone_message(&event->message));
  }
}
static qa_app_q1_frame host_frame(void *ctx, qa_app_q1_player player, const void *output) {
  (void)output; /* TS _output */
  q1_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_shared_scene_queries *scene = s->scene;
  per_player *entry = player_slot(h, (double)player.client.slot);
  qa_vec3 origin = s->player_view(s->ctx, player.actor).origin;
  double cluster = qa_shared_leaf_cluster(scene, qa_shared_point_leaf(scene, origin));
  list_clear(&entry->messages);
  list_push(&entry->messages, client_data(h, player));
  routed_for(h, player, true, &entry->reliable);
  routed_for(h, player, false, &entry->datagram);
  free(entry->entities);
  size_t count = 0;
  entry->entities = entities(h, &count);
  entry->entity_count = 0;
  for (size_t i = 0; i < count; i++) {
    const qa_q1_extended_entity_state *state = &entry->entities[i];
    if (!(!h->wide || state->alpha != QA_Q1_ENTALPHA_ZERO || state->base.effects != 0)) continue;
    if (state->base.number != player.source_entity
        && !qa_shared_cluster_visible(scene, cluster, qa_shared_leaf_cluster(scene, qa_shared_point_leaf(scene, state->base.origin)), "pvs"))
      continue;
    entry->entities[entry->entity_count++] = *state;
  }
  qa_app_q1_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.seconds = h->game->time;
  frame.messages = (const void *const *)entry->messages.values; frame.message_count = entry->messages.count;
  frame.reliable = (const void *const *)entry->reliable.values; frame.reliable_count = entry->reliable.count;
  frame.datagram = (const void *const *)entry->datagram.values; frame.datagram_count = entry->datagram.count;
  frame.entities = (const void *const *)entry->entities; frame.entity_count = entry->entity_count;
  return frame;
}
static const void *host_input(void *ctx, qa_app_q1_player player, const qa_q1_user_command *command, double sequence) {
  q1_host *h = ctx;
  qa_actor_command *out = xcalloc(1, sizeof *out);
  out->actor = player.actor;
  out->source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT; out->source.client = player.client;
  out->sequence = sequence;
  out->command.kind = QA_USER_COMMAND_Q1; out->command.as.q1 = *command;
  h->inputs = xrealloc(h->inputs, (h->input_count + 1) * sizeof *h->inputs);
  h->inputs[h->input_count++] = out;
  return out;
}
static void host_command(void *ctx, qa_app_q1_player player, const char *name, const char *const *args, size_t count) {
  q1_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  if (is(name, "pause")) {
    bool previous = s->q1_paused(s->ctx);
    const char *text = s->toggle_q1_pause(s->ctx, &player.actor);
    if (h->operators.count >= 4096)
      qa_raise("RangeError", 0, 0, "Event ring capacity exhausted (4096 undrained records); drain before admitting more");
    routed value;
    memset(&value, 0, sizeof value);
    value.message = blank("print"); value.message.text = xstrdup(text); value.reliable = true;
    if (previous == s->q1_paused(s->ctx)) { value.has_recipient = true; value.recipient = player.actor; }
    routed_push(&h->operators, value);
  } else if (is(name, "name")) {
    qa_comp_q1_source_client *record = qa_comp_q1_source_clients_require(&h->source->clients, player.actor);
    qa_comp_q1_client_snapshot snapshot = qa_comp_q1_source_client_snapshot(record);
    qa_app_sim_info values;
    memset(&values, 0, sizeof values);
    for (size_t i = 0; i < snapshot.userinfo_count; i++) qa_app_sim_info_set(&values, snapshot.userinfo[i].key, snapshot.userinfo[i].value);
    char value[16];
    snprintf(value, sizeof value, "%s", count > 0 ? args[0] : "unconnected"); /* slice(0, 15) over ASCII */
    qa_app_sim_info_set(&values, "name", value);
    qa_comp_userinfo_entry *entries = xcalloc(values.count, sizeof *entries);
    for (size_t i = 0; i < values.count; i++) { entries[i].key = values.pairs[i].key; entries[i].value = values.pairs[i].value; }
    bool ok = qa_comp_q1_source_composition_userinfo(h->source, player.actor, entries, values.count);
    free(entries);
    qa_app_sim_info_clear(&values);
    if (!ok) qa_range_error("Out of memory");
    s->notify_client_event(s->ctx, "userinfo", player.actor);
  } else if (is(name, "color")) {
    double shirt = qa_js_string_to_number(count > 0 ? args[0] : "0");
    double pants = qa_js_string_to_number(count > 1 ? args[1] : count > 0 ? args[0] : "0");
    if (!qa_comp_q1_source_clients_colors(&h->source->clients, player.actor, shirt, pants)) qa_range_error("Out of memory");
    s->notify_client_event(s->ctx, "userinfo", player.actor);
  } else if (is(name, "use") || is(name, "weapnext") || is(name, "weapprev") || is(name, "give") || is(name, "god")
      || is(name, "notarget") || is(name, "noclip") || is(name, "fly") || is(name, "kill"))
    s->player_command(s->ctx, player.actor, name, args, count);
  else {
    char text[512];
    snprintf(text, sizeof text, "Unsupported native Q1 client command: %s", name);
    h->options.print(h->options.print_ctx, text);
  }
}
static bool host_rejects(void *ctx, const void *address) { q1_host *h = ctx; return h->options.rejects(h->options.rejects_ctx, address); }
static void host_print(void *ctx, const char *text) { q1_host *h = ctx; h->options.print(h->options.print_ctx, text); }
static void host_destroy(void *ctx) {
  q1_host *h = ctx;
  for (size_t i = 0; i < h->player_count; i++) clear_player(&h->players[i]);
  free(h->players);
  for (size_t i = 0; i < h->board_count; i++) free(h->board[i].name);
  free(h->board); free(h->board_order);
  routed_clear(&h->routed); routed_clear(&h->operators);
  for (size_t i = 0; i < h->input_count; i++) free(h->inputs[i]);
  free(h->inputs);
  free(h->muzzle_flashes);
  for (size_t i = 0; i < h->sound_count; i++) free((void *)h->sound_resources[i]);
  free(h->sound_resources); free(h->models); free(h->sounds);
  qa_wire_admission_destroy(&h->support);
  free(h);
}

qa_app_q1_server_host qa_app_sim_create_q1_application_server_host(qa_app_sim_q1_server_binding_options options) {
  if (options.quakec != NULL && is(qa_app_sim_quakec_source_kind(options.quakec), "netquake"))
    return qa_app_sim_create_quakec_netquake_host(options);
  const qa_app_sim_network_simulation *s = options.simulation;
  qa_comp_q1_source_composition *source = s->q1_source(s->ctx);
  if (source == NULL) qa_raise("Error", 0, 0, "Q1 network requires the Q1 source game");
  qa_q1_services *game = source->game;
  if (!qa_q1_services_uses_id1(game) || !is(game->options.edition, "classic")
      || source->selection.program != QA_COMP_Q1_ID1)
    qa_raise("Error", 0, 0, "Native ordered Q1 precaches currently require classic id1 source declarations");
  if (!is(qa_q1_precache_phase(game->precaches), "frozen")) qa_raise("Error", 0, 0, "Q1 source precaches are still loading");
  q1_host *h = xcalloc(1, sizeof *h);
  qa_cleanup guard;
  qa_cleanup_push(&guard, host_destroy, h);
  h->destroy = host_destroy;
  h->options = options; h->simulation = s; h->source = source; h->game = game;
  h->max_clients = game->options.has_max_clients ? game->options.max_clients : 1;
  qa_message_reader empty;
  qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec;
  qa_create_netquake_codec(options.protocol, &empty, &codec);
  h->wide = options.protocol.version != 15;
  size_t model_total = 0, sound_total = 0;
  const char *const *models = qa_q1_precache_models(game->precaches, &model_total);
  const char *const *sounds = qa_q1_precache_sounds(game->precaches, &sound_total);
  if ((double)model_total > codec.max_precache || (double)sound_total > codec.max_precache)
    qa_raise("Error", 0, 0, "NetQuake %s precache overflow", QA_JS_NUMBER((double)options.protocol.version));
  h->model_count = model_total ? model_total - 1 : 0;
  h->models = xcalloc(h->model_count, sizeof *h->models);
  for (size_t i = 0; i < h->model_count; i++) h->models[i] = models[i + 1];
  h->sound_count = sound_total ? sound_total - 1 : 0;
  h->sounds = xcalloc(h->sound_count, sizeof *h->sounds);
  h->sound_resources = xcalloc(h->sound_count, sizeof *h->sound_resources);
  for (size_t i = 0; i < h->sound_count; i++) h->sounds[i] = sounds[i + 1];
  const qa_app_sim_network_content *content = options.content;
  qa_content_id owner = s->recipe->map.entities.content;
  for (size_t i = 0; i < h->model_count; i++) {
    qa_resolved_resource_reference resource;
    if (h->models[i][0] != '*' && !content->resolve(content->ctx, owner, h->models[i], &resource))
      qa_raise("Error", 0, 0, "Q1 model precache resource is missing: %s", h->models[i]);
  }
  for (size_t i = 0; i < h->sound_count; i++) {
    size_t length = strlen(h->sounds[i]) + 7;
    char *path = xcalloc(length, 1);
    snprintf(path, length, "sound/%s", h->sounds[i]);
    qa_resolved_resource_reference resource;
    if (!content->resolve(content->ctx, owner, path, &resource)) {
      free(path);
      qa_raise("Error", 0, 0, "Q1 sound precache resource is missing: %s", h->sounds[i]);
    }
    h->sound_resources[i] = xstrdup(resource.id);
    s->register_resource(s->ctx, owner, path, resource);
    free(path);
  }
  h->previous_pause = s->q1_paused(s->ctx);
  qa_cleanup_pop(&guard);
  qa_app_q1_server_host host;
  memset(&host, 0, sizeof host);
  host.ctx = h;
  if (options.rejects) host.rejects = host_rejects;
  host.protocol = options.protocol;
  host.max_clients = h->max_clients;
  host.map_name = game->map_name;
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
