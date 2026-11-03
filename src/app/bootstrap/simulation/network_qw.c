/* Port of src/app/bootstrap/simulation/network-qw.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_qw.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qc/machine.h"
#include "qa/compat/qc/memory.h"
#include "qa/compat/qc/message_routing.h"
#include "qa/compat/qc/program.h"
#include "qa/content/mounts.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/session.h"
#include "qa/network/q1/checksum.h"
#include "qa/network/q1/handshake.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/quakeworld.h"
#include "qa/network/q1/qw_constants.h"
#include "qa/network/q1/session.h"
#include "qa/network/services/download_types.h"
#include "qa/platform/files/contained.h"

#define QUEUE_CAPACITY 8192

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
static void *copy_array(const void *values, size_t count, size_t size) {
  if (count == 0 || values == NULL) return NULL;
  void *out = xcalloc(count, size);
  memcpy(out, values, count * size);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static qa_quakeworld_message blank(const char *kind) {
  qa_quakeworld_message message;
  memset(&message, 0, sizeof message);
  message.kind = kind;
  return message;
}
static qa_quakeworld_message clone_message(const qa_quakeworld_message *message) {
  qa_quakeworld_message out = *message;
  out.text = xstrdup(message->text); out.str_value = xstrdup(message->str_value); out.key = xstrdup(message->key);
  out.game_directory = xstrdup(message->game_directory); out.level_name = xstrdup(message->level_name);
  out.download_bytes = copy_array(message->download_bytes, message->download_length, 1);
  out.projectiles = copy_array(message->projectiles, message->projectile_count, sizeof *message->projectiles);
  out.entities = copy_array(message->entities, message->entity_count, sizeof *message->entities);
  out.names = message->name_count ? xcalloc(message->name_count, sizeof *out.names) : NULL;
  for (size_t i = 0; i < message->name_count; i++) out.names[i] = xstrdup(message->names[i]);
  return out;
}
static qa_quakeworld_profile profile28(void) {
  qa_quakeworld_profile profile;
  memset(&profile, 0, sizeof profile);
  profile.kind = "q1-quakeworld"; profile.version = 28;
  return profile;
}
typedef struct routed { qa_quakeworld_message message; qa_compat_qc_message_destination destination; } routed;
typedef struct routed_list { routed *values; size_t count, capacity; } routed_list;
static void routed_push(routed_list *list, routed value) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 16;
    list->values = xrealloc(list->values, list->capacity * sizeof *list->values);
  }
  list->values[list->count++] = value;
}
static void routed_clear(routed_list *list) {
  for (size_t i = 0; i < list->count; i++) qa_quakeworld_message_destroy(&list->values[i].message);
  free(list->values);
  memset(list, 0, sizeof *list);
}
typedef struct message_list { qa_quakeworld_message *values; size_t count, capacity; } message_list;
static void list_push(message_list *list, qa_quakeworld_message message) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 16;
    list->values = xrealloc(list->values, list->capacity * sizeof *list->values);
  }
  list->values[list->count++] = message;
}
static void list_clear(message_list *list) {
  for (size_t i = 0; i < list->count; i++) qa_quakeworld_message_destroy(&list->values[i]);
  free(list->values);
  memset(list, 0, sizeof *list);
}
typedef struct buffers { uint8_t **values; size_t *lengths; size_t count; } buffers;
static void buffers_clear(buffers *b) {
  for (size_t i = 0; i < b->count; i++) free(b->values[i]);
  free(b->values); free(b->lengths);
  memset(b, 0, sizeof *b);
}
typedef struct stat_entry { double index, value; } stat_entry;
/* TS ClientState. */
typedef struct client_state {
  bool used;
  qa_app_qw_player player;
  bool spectator;           /* role */
  qa_qw_info info;
  bool begun;
  qa_qw_user_command command;
  double command_time;
  stat_entry stats[16]; bool has_stat[16];
  double frags;
} client_state;
typedef struct qw_host qw_host;
typedef struct signon_ctx { qw_host *h; qa_app_qw_player player; qa_qw_signon_host host; char *models_scratch; } signon_ctx;
typedef struct download_ctx { qa_download_source source; uint8_t *bytes; size_t length; bool closed; } download_ctx;
struct qw_host {
  qa_app_sim_qw_server_binding_options options;
  const qa_app_sim_network_simulation *simulation;
  qa_app_sim_quakec_source *game;
  const qa_q1_world_geometry *world;
  const char *const *models; size_t model_count;
  const char *const *sounds; size_t sound_count;
  int32_t checksum;
  client_state clients[32];
  double order[32]; size_t order_count;  /* clients Map insertion order */
  qa_quakeworld_entity *baselines; size_t baseline_count;
  const void **baseline_pointers;
  buffers signon;
  routed_list routed, queued;
  char *styles[64];
  bool previous_pause;
  double player_model;
  qa_app_qw_server_authentication authentication;
  char *password, *spectator_password;
  qa_wire_admission support;
  signon_ctx *signons[32];
  download_ctx **downloads; size_t download_count;
  /* Results owned until the next call. */
  message_list frame_messages, frame_reliable;
  const void **frame_message_pointers, **frame_reliable_pointers;
  qa_quakeworld_entity *frame_entities; const void **frame_entity_pointers;
  qa_app_qw_bytes *recording; buffers recording_buffers;
  struct phase_scope **phases; size_t phase_count;
};

static void print_to(qw_host *h, const char *text) { h->options.print(h->options.print_ctx, text); }
static int64_t field(qw_host *h, const char *name) {
  const qa_compat_qc_definition *definition = qa_compat_qc_program_field_named(qa_app_sim_quakec_source_program(h->game), name);
  if (definition == NULL) qa_raise("Error", 0, 0, "Missing QW source field %s", name);
  return definition->offset;
}
static double slot_of(qw_host *h, qa_actor_id actor) {
  int64_t value;
  if (!qa_app_sim_quakec_source_source_slot(h->game, actor, &value)) qa_raise("Error", 0, 0, "QW actor has no live source slot");
  return (double)value;
}
static qa_compat_qc_words *entity_at(qw_host *h, double slot) {
  return qa_compat_qc_entity_memory_at(qa_app_sim_quakec_source_entities(h->game), (int64_t)slot);
}
static double scalar(qw_host *h, qa_actor_id actor, const char *name) {
  qa_compat_qc_words *entity = entity_at(h, slot_of(h, actor));
  return qa_compat_qc_words_float(entity, field(h, name));
}
static qa_vec3 vector(qw_host *h, qa_actor_id actor, const char *name) {
  qa_compat_qc_words *entity = entity_at(h, slot_of(h, actor));
  return qa_compat_qc_words_vector(entity, field(h, name));
}
static const char *text(qw_host *h, qa_actor_id actor, const char *name) {
  qa_compat_qc_machine *machine = qa_app_sim_quakec_source_machine(h->game);
  qa_compat_qc_words *entity = entity_at(h, slot_of(h, actor));
  return qa_compat_qc_strings_get(&machine->strings, qa_compat_qc_words_int(entity, field(h, name)));
}
static double global(qw_host *h, const char *name) {
  qa_compat_qc_machine *machine = qa_app_sim_quakec_source_machine(h->game);
  return qa_compat_qc_words_float(&machine->globals, qa_compat_qc_machine_global_offset(machine, name));
}
static double cvar(qw_host *h, const char *name) { return qa_cvar_registry_variable_value(qa_app_sim_quakec_source_cvars(h->game), name); }
/* modelIndex.get(name) ?? fallback: Map(models.map((name, index) => [name, index + 1])). */
static double model_index(qw_host *h, const char *name, double fallback) {
  double found = fallback;
  for (size_t i = 0; i < h->model_count; i++) if (strcmp(h->models[i], name) == 0) found = (double)i + 1;
  return found;
}
static qa_quakeworld_entity state(qw_host *h, qa_actor_id actor) {
  qa_quakeworld_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.state.base.number = slot_of(h, actor);
  entity.state.base.model_index = trunc(scalar(h, actor, "modelindex"));
  entity.state.base.frame = trunc(scalar(h, actor, "frame"));
  entity.state.base.color_map = trunc(scalar(h, actor, "colormap"));
  entity.state.base.skin = trunc(scalar(h, actor, "skin"));
  entity.state.base.effects = trunc(scalar(h, actor, "effects"));
  entity.state.base.origin = vector(h, actor, "origin");
  entity.state.base.angles = vector(h, actor, "angles");
  entity.state.alpha = 0; entity.state.scale = 16; entity.state.step = false; entity.state.lerp_finish_seconds = 0;
  entity.quakeworld_flags = 0;
  return entity;
}
/* sourceActors(): malloc'd array. */
static qa_actor_id *source_actors(qw_host *h, size_t *out_count) {
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t count = 0;
  const qa_owned_actor *owned = qa_session_actor_registry_owned_by(s->actors, s->recipe->map.entities.provider, &count);
  qa_owned_actor *copy = copy_array(owned, count, sizeof *owned);
  qa_actor_id *result = xcalloc(count, sizeof *result);
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    qa_actor_id id = copy[i].id;
    if (slot_of(h, id) > 32 && scalar(h, id, "modelindex") != 0 && text(h, id, "model")[0] != '\0') result[kept++] = id;
  }
  free(copy);
  *out_count = kept;
  return result;
}
/* encode(records) */
static buffers encode(const qa_quakeworld_message *records, size_t count) {
  buffers result;
  memset(&result, 0, sizeof result);
  qa_size_buf buffer;
  if (!qa_size_buf_init(&buffer, 1400, false)) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_size_buf one;
    if (!qa_size_buf_init(&one, 1400, false)) qa_range_error("Out of memory");
    qa_write_quakeworld_message(&one, profile28(), &records[i]);
    if (buffer.cursize + one.cursize > 1400) {
      result.values = xrealloc(result.values, (result.count + 1) * sizeof *result.values);
      result.lengths = xrealloc(result.lengths, (result.count + 1) * sizeof *result.lengths);
      result.values[result.count] = qa_size_buf_bytes(&buffer, &result.lengths[result.count]);
      result.count++;
      qa_size_buf_clear(&buffer);
    }
    qa_sz_write(&buffer, one.data, one.cursize, one.cursize);
    qa_size_buf_destroy(&one);
  }
  if (buffer.cursize != 0) {
    result.values = xrealloc(result.values, (result.count + 1) * sizeof *result.values);
    result.lengths = xrealloc(result.lengths, (result.count + 1) * sizeof *result.lengths);
    result.values[result.count] = qa_size_buf_bytes(&buffer, &result.lengths[result.count]);
    result.count++;
  }
  qa_size_buf_destroy(&buffer);
  return result;
}
static char *info_text(const qa_qw_info *info) {
  size_t length = 1;
  for (size_t i = 0; i < info->count; i++) length += strlen(info->entries[i].key) + strlen(info->entries[i].value) + 2;
  char *out = xcalloc(length, 1);
  for (size_t i = 0; i < info->count; i++) { strcat(out, "\\"); strcat(out, info->entries[i].key); strcat(out, "\\"); strcat(out, info->entries[i].value); }
  return out;
}
static client_state *require_client(qw_host *h, qa_app_qw_player player) {
  client_state *client = player.slot >= 0 && player.slot < 32 ? &h->clients[(size_t)player.slot] : NULL;
  if (client == NULL || !client->used || !qa_same_client(client->player.client, player.client)) qa_raise("Error", 0, 0, "Stale QW client");
  return client;
}
static void order_add(qw_host *h, double slot) {
  for (size_t i = 0; i < h->order_count; i++) if (h->order[i] == slot) return;
  h->order[h->order_count++] = slot;
}
static void client_delete(qw_host *h, double slot) {
  client_state *client = &h->clients[(size_t)slot];
  if (client->used) qa_qw_info_destroy(&client->info);
  memset(client, 0, sizeof *client);
  for (size_t i = 0; i < h->order_count; i++)
    if (h->order[i] == slot) { memmove(h->order + i, h->order + i + 1, (h->order_count - i - 1) * sizeof *h->order); h->order_count--; break; }
}
static void client_set(qw_host *h, double slot, client_state value) {
  client_state *client = &h->clients[(size_t)slot];
  if (client->used) qa_qw_info_destroy(&client->info);
  *client = value;
  client->used = true;
  order_add(h, slot);
}
static void queue_admit(qw_host *h, qa_quakeworld_message message, qa_compat_qc_message_destination destination) {
  if (h->queued.count >= QUEUE_CAPACITY) {
    qa_quakeworld_message_destroy(&message);
    qa_raise("RangeError", 0, 0, "Event ring capacity exhausted (%d undrained records); drain before admitting more", QUEUE_CAPACITY);
  }
  routed value = {message, destination};
  routed_push(&h->queued, value);
}
static qa_compat_qc_message_destination destination(qa_compat_qc_message_destination_kind kind, bool reliable, const qa_actor_id *actor) {
  qa_compat_qc_message_destination out;
  memset(&out, 0, sizeof out);
  out.kind = kind; out.reliable = reliable;
  if (actor) out.actor = *actor;
  return out;
}
static qa_vec3 eye(qw_host *h, qa_actor_id actor) {
  qa_vec3 origin = vector(h, actor, "origin"), offset = vector(h, actor, "view_ofs");
  qa_vec3 out = {origin.x + offset.x, origin.y + offset.y, origin.z + offset.z};
  return out;
}
static double *copy_leaves(qa_leaf_query_result result, size_t *out_count) {
  *out_count = result.leaves.count;
  return copy_array(result.leaves.values, result.leaves.count, sizeof(double));
}
static bool visible(qw_host *h, qa_actor_id viewer, qa_actor_id target) {
  if (qa_same_actor(viewer, target)) return true;
  qa_shared_scene_queries *scene = h->simulation->scene;
  qa_vec3 origin = eye(h, viewer);
  qa_linked_body_result linked = qa_shared_body_table_linked(h->simulation->bodies, target);
  if (!linked.has_value) return false;
  qa_bounds box = {{origin.x - 8, origin.y - 8, origin.z - 8}, {origin.x + 8, origin.y + 8, origin.z + 8}};
  size_t from_count = 0, to_count = 0;
  double *from = copy_leaves(qa_shared_box_leaves(scene, box, (double)h->world->leaf_count), &from_count);
  /* SV_FindTouchedLeafs uses the source link envelope and retains at most MAX_ENT_LEAFS (16). */
  double *to = copy_leaves(qa_shared_box_leaves(scene, linked.value.absolute_bounds, 16), &to_count);
  bool seen = false;
  for (size_t i = 0; i < from_count && !seen; i++)
    for (size_t j = 0; j < to_count && !seen; j++)
      seen = qa_shared_cluster_visible(scene, qa_shared_leaf_cluster(scene, from[i]), qa_shared_leaf_cluster(scene, to[j]), "pvs");
  free(from); free(to);
  return seen;
}
typedef struct receive_scope { qw_host *h; qa_actor_id actor; } receive_scope;
static qa_vec3 receive_origin(void *ctx) { receive_scope *r = ctx; return vector(r->h, r->actor, "origin"); }
static double v_point_leaf(void *ctx, qa_vec3 point) { return qa_shared_point_leaf(ctx, point); }
static double v_leaf_cluster(void *ctx, double leaf) { return qa_shared_leaf_cluster(ctx, leaf); }
static bool v_cluster_visible(void *ctx, double from, double to, const char *kind) { return qa_shared_cluster_visible(ctx, from, to, kind); }
static qa_q1_client_visibility_scene receive_scene(void *ctx) {
  receive_scope *r = ctx;
  qa_q1_client_visibility_scene scene = {r->h->simulation->scene, r->h->world->leaf_count, v_point_leaf, v_leaf_cluster, v_cluster_visible};
  return scene;
}
static bool receives(qw_host *h, qa_app_qw_player player, qa_compat_qc_message_destination target) {
  receive_scope scope = {h, player.actor};
  qa_compat_qc_message_origin origin = {&scope, receive_origin};
  qa_compat_qc_message_visibility visibility = {&scope, receive_scene};
  return qa_compat_qc_receives_quakeworld_message(player.actor, target, origin, visibility);
}
static qa_qw_player_state player_state(qw_host *h, const client_state *client, qa_app_qw_player owner) {
  qa_actor_id actor = client->player.actor;
  qa_quakeworld_entity entity = state(h, actor);
  qa_vec3 velocity = vector(h, actor, "velocity");
  double flags = QA_QW_PF_MSEC | QA_QW_PF_COMMAND;
  if (entity.state.base.model_index != h->player_model) flags = qa_js_bit_or(flags, QA_QW_PF_MODEL);
  if (velocity.x != 0) flags = qa_js_bit_or(flags, QA_QW_PF_VELOCITY1);
  if (velocity.y != 0) flags = qa_js_bit_or(flags, QA_QW_PF_VELOCITY2);
  if (velocity.z != 0) flags = qa_js_bit_or(flags, QA_QW_PF_VELOCITY3);
  if (entity.state.base.effects != 0) flags = qa_js_bit_or(flags, QA_QW_PF_EFFECTS);
  if (entity.state.base.skin != 0) flags = qa_js_bit_or(flags, QA_QW_PF_SKINNUM);
  bool dead = scalar(h, actor, "health") <= 0;
  if (dead) flags = qa_js_bit_or(flags, QA_QW_PF_DEAD);
  if (vector(h, actor, "mins").z != -24) flags = qa_js_bit_or(flags, QA_QW_PF_GIB);
  double weapon_frame = trunc(scalar(h, actor, "weaponframe"));
  if (qa_same_client(owner.client, client->player.client)) {
    flags = qa_js_bit_and(flags, qa_js_bit_not(QA_QW_PF_MSEC | QA_QW_PF_COMMAND));
    if (weapon_frame != 0) flags = qa_js_bit_or(flags, QA_QW_PF_WEAPONFRAME);
  }
  qa_qw_player_state out;
  memset(&out, 0, sizeof out);
  out.number = client->player.slot; out.flags = flags; out.origin = entity.state.base.origin; out.frame = entity.state.base.frame;
  out.velocity = velocity; out.model_index = entity.state.base.model_index; out.skin = entity.state.base.skin;
  out.effects = entity.state.base.effects; out.weapon_frame = weapon_frame;
  out.milliseconds = qa_js_min(255, qa_js_max(0, trunc(1000 * (qa_app_sim_quakec_source_time_seconds(h->game) - client->command_time))));
  out.command = client->command;
  out.command.buttons = 0; out.command.impulse = 0;
  if (dead) { qa_vec3 angles = {0, entity.state.base.angles.y, client->command.angles.z}; out.command.angles = angles; }
  return out;
}
/* stats(actor): the TS Map in insertion order. */
static size_t stats(qw_host *h, qa_actor_id actor, stat_entry out[16]) {
  static const struct { double index; const char *field; } fields[] = {{0, "health"}, {3, "currentammo"}, {4, "armorvalue"},
    {5, "weaponframe"}, {6, "ammo_shells"}, {7, "ammo_nails"}, {8, "ammo_rockets"}, {9, "ammo_cells"}, {10, "weapon"}};
  size_t count = 0;
  out[count].index = fields[0].index; out[count++].value = scalar(h, actor, fields[0].field);
  out[count].index = 2; out[count++].value = model_index(h, text(h, actor, "weaponmodel"), 0);
  for (size_t i = 1; i < sizeof fields / sizeof *fields; i++) { out[count].index = fields[i].index; out[count++].value = scalar(h, actor, fields[i].field); }
  static const struct { double index; const char *global; } globals[] = {{11, "total_secrets"}, {12, "total_monsters"},
    {13, "found_secrets"}, {14, "killed_monsters"}};
  for (size_t i = 0; i < 4; i++) { out[count].index = globals[i].index; out[count++].value = global(h, globals[i].global); }
  double items = trunc(scalar(h, actor, "items"));
  out[count].index = 15; out[count++].value = qa_js_bit_or(items, qa_js_shl(trunc(global(h, "serverflags")), 28));
  return count;
}
static buffers spawn_messages(qw_host *h, qa_app_qw_player player, double start) {
  const qa_app_sim_network_simulation *s = h->simulation;
  message_list records;
  memset(&records, 0, sizeof records);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))list_clear, &records);
  qa_quakeworld_message message = blank("pause");
  message.paused = s->q1_paused(s->ctx);
  list_push(&records, message);
  for (size_t o = 0; o < h->order_count; o++) {
    const client_state *client = &h->clients[(size_t)h->order[o]];
    if (client->player.slot < start) continue;
    message = blank("userinfo");
    message.slot = client->player.slot;
    message.user_id = (double)client->player.client.generation * 32 + client->player.slot + 1;
    message.str_value = info_text(&client->info);
    list_push(&records, message);
  }
  for (size_t i = 0; i < 64; i++) {
    message = blank("light-style");
    message.index = (double)i; message.str_value = xstrdup(h->styles[i]);
    list_push(&records, message);
  }
  stat_entry values[16];
  size_t count = stats(h, player.actor, values);
  for (size_t i = 0; i < count; i++) {
    message = blank("stat");
    message.index = values[i].index; message.value = trunc(values[i].value);
    list_push(&records, message);
  }
  buffers result = encode(records.values, records.count);
  qa_cleanup_pop(&guard);
  list_clear(&records);
  return result;
}
/* game.messages.flush(), then drain into `pending`. */
typedef struct phase_scope {
  qw_host *h; qa_app_qw_player player;
  qa_app_qw_action_fn action; void *action_ctx;
  qa_app_qw_emit_fn emit; void *emit_ctx;
} phase_scope;
static void flush(phase_scope *p) {
  qw_host *h = p->h;
  qa_compat_qc_broadcast_messages_flush(qa_app_sim_quakec_source_messages(h->game));
  routed_list pending = h->queued; /* queued.take(cursor); queued.retire() */
  memset(&h->queued, 0, sizeof h->queued);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))routed_clear, &pending);
  size_t batch_count = 0;
  const qa_app_sim_quakec_routed *batches = qa_app_sim_quakec_source_drain_messages(h->game, &batch_count);
  for (size_t b = 0; b < batch_count; b++)
    for (size_t e = 0; e < batches[b].entry_count; e++) {
      const qa_quakeworld_message *message = &batches[b].entries[e].message;
      if (is(message->kind, "packet-entities") || is(message->kind, "invalid-delta")) continue;
      routed value = {clone_message(message), batches[b].destination};
      routed_push(&pending, value);
    }
  for (size_t i = 0; i < pending.count; i++) {
    routed *entry = &pending.values[i];
    if (entry->destination.kind == QA_QC_DEST_SIGNON || !entry->destination.reliable) {
      queue_admit(h, entry->message, entry->destination);
      memset(&entry->message, 0, sizeof entry->message);
      continue;
    }
    for (size_t o = 0; o < h->order_count; o++) {
      client_state *client = &h->clients[(size_t)h->order[o]];
      if ((client->begun || entry->destination.kind == QA_QC_DEST_CLIENT) && receives(h, client->player, entry->destination))
        p->emit(p->emit_ctx, client->player, &entry->message);
    }
  }
  qa_cleanup_pop(&guard);
  routed_clear(&pending);
}
static void phase_action(void *ctx) {
  phase_scope *p = ctx;
  client_state *current = &p->h->clients[(size_t)p->player.slot];
  if (!current->used || !qa_same_actor(current->player.actor, p->player.actor)) return;
  flush(p);
  p->action(p->action_ctx);
  flush(p);
}
static void host_command_phase(void *ctx, qa_app_qw_player player, qa_app_qw_action_fn action, void *action_ctx,
    qa_app_qw_emit_fn emit, void *emit_ctx) {
  qw_host *h = ctx;
  /* The queued action runs inside the next simulation step; its closure lives until observe(). */
  phase_scope *scope = xcalloc(1, sizeof *scope);
  scope->h = h; scope->player = player; scope->action = action; scope->action_ctx = action_ctx; scope->emit = emit; scope->emit_ctx = emit_ctx;
  h->phases = xrealloc(h->phases, (h->phase_count + 1) * sizeof *h->phases);
  h->phases[h->phase_count++] = scope;
  h->simulation->queue_quakeworld_action(h->simulation->ctx, player.client, phase_action, scope);
}
static void free_phases(qw_host *h) {
  for (size_t i = 0; i < h->phase_count; i++) free(h->phases[i]);
  free(h->phases); h->phases = NULL; h->phase_count = 0;
}
static void refresh_authentication(qw_host *h) {
  qa_cvar_registry *cvars = qa_app_sim_quakec_source_cvars(h->game);
  free(h->password); free(h->spectator_password);
  h->password = qa_cvar_registry_variable_string(cvars, "password");
  h->spectator_password = qa_cvar_registry_variable_string(cvars, "spectator_password");
  h->authentication.password = h->password;
  h->authentication.spectator_password = h->spectator_password;
  h->authentication.high_characters = cvar(h, "sv_highchars") != 0;
}
static qa_qw_user_command idle(void) { qa_qw_user_command command; memset(&command, 0, sizeof command); return command; }
typedef struct admit_scope { qw_host *h; const qa_qw_connect_request *request; qa_client_id id; double index; bool spectator; qa_app_qw_player player; } admit_scope;
static void admit_body(void *ctx) {
  admit_scope *a = ctx;
  qw_host *h = a->h;
  qa_qw_info info;
  qa_quakeworld_info_parse(a->request->userinfo, &info);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_qw_info_destroy, &info);
  qa_qw_info_delete(&info, "*spectator");
  if (a->spectator) qa_qw_info_set(&info, "*spectator", "1");
  qa_app_sim_quakec_source_set_client_role(h->game, a->id, a->spectator ? "spectator" : "player");
  qa_app_sim_cvar_name_value *items = xcalloc(info.count, sizeof *items);
  for (size_t i = 0; i < info.count; i++) { items[i].name = info.entries[i].key; items[i].value = info.entries[i].value; }
  qa_app_sim_quakec_info values = {items, info.count};
  qa_app_sim_quakec_source_set_client_info(h->game, a->id, values);
  free(items);
  qa_owned_actor actor = qa_app_sim_quakec_source_reserved_client(h->game, a->id);
  qa_app_qw_player player = {a->id, actor.id, a->index};
  client_state value;
  memset(&value, 0, sizeof value);
  value.player = player; value.spectator = a->spectator; value.begun = false; value.command = idle();
  value.command_time = qa_app_sim_quakec_source_time_seconds(h->game); value.frags = 0;
  qa_cleanup_pop(&guard);
  value.info = info;
  client_set(h, a->index, value);
  qa_quakeworld_message message = blank("userinfo");
  message.slot = a->index; message.user_id = (double)a->id.generation * 32 + a->index + 1;
  message.str_value = info_text(&h->clients[(size_t)a->index].info);
  queue_admit(h, message, destination(QA_QC_DEST_BROADCAST, true, NULL));
  a->player = player;
}
static qa_app_qw_admission host_admit(void *ctx, const void *request_value) {
  qw_host *h = ctx;
  const qa_qw_connect_request *request = request_value;
  refresh_authentication(h);
  qa_app_qw_admission result;
  memset(&result, 0, sizeof result);
  double limit = request->spectator ? qa_js_max(0, qa_js_min(32, trunc(cvar(h, "maxspectators")))) : h->simulation->options->max_clients;
  double same = 0;
  for (size_t o = 0; o < h->order_count; o++) if (h->clients[(size_t)h->order[o]].spectator == request->spectator) same++;
  if (same >= limit) { result.reason = "Server is full"; return result; }
  double index = 0;
  while (index < 32 && h->clients[(size_t)index].used) index++;
  if (index == 32) { result.reason = "Server is full"; return result; }
  qa_session_client *client = qa_engine_session_create_client(h->options.session, index);
  qa_session_client_connect(client, "remote");
  admit_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.h = h; scope.request = request; scope.id = qa_session_client_id(client); scope.index = index; scope.spectator = request->spectator;
  qa_caught_error error;
  if (qa_try(admit_body, &scope, &error)) { result.accepted = true; result.player = scope.player; return result; }
  qa_engine_session_close_client(h->options.session, scope.id);
  qa_rethrow(&error);
}
static bool find_player_actor(qw_host *h, qa_client_id client, qa_actor_id *out) {
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t count = 0;
  const qa_actor_id *players = s->players(s->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *movement = s->movement_player(s->ctx, players[i]);
    if (movement != NULL && qa_same_client(movement->client, client)) { *out = players[i]; return true; }
  }
  return false;
}
static qa_app_qw_player host_recording_player(void *ctx, qa_client_id client) {
  qw_host *h = ctx;
  qa_actor_id actor;
  if (!find_player_actor(h, client, &actor) || !qa_app_sim_quakec_source_is_active_client(h->game, actor))
    qa_raise("Error", 0, 0, "QW recording requires an active source player");
  client_state *existing = client.slot >= 0 && client.slot < 32 ? &h->clients[client.slot] : NULL;
  if (existing != NULL && existing->used) {
    if (!qa_same_client(existing->player.client, client) || !qa_same_actor(existing->player.actor, actor))
      qa_raise("Error", 0, 0, "QW recording client slot belongs to another source player");
    return existing->player;
  }
  qa_app_qw_player player = {client, actor, (double)client.slot};
  client_state value;
  memset(&value, 0, sizeof value);
  value.player = player; value.spectator = qa_app_sim_quakec_source_is_spectator_client(h->game, actor);
  qa_app_sim_quakec_info current = qa_app_sim_quakec_source_client_info(h->game, client);
  qa_qw_info_init(&value.info);
  for (size_t i = 0; i < current.count; i++) qa_qw_info_set(&value.info, current.items[i].name, current.items[i].value);
  value.begun = true; value.command = idle(); value.command_time = qa_app_sim_quakec_source_time_seconds(h->game);
  value.frags = scalar(h, actor, "frags");
  client_set(h, (double)client.slot, value);
  return player;
}
static const qa_app_qw_bytes *host_recording_signon(void *ctx, qa_app_qw_player player, size_t *out_count) {
  qw_host *h = ctx;
  require_client(h, player);
  buffers_clear(&h->recording_buffers);
  free(h->recording);
  h->recording_buffers = spawn_messages(h, player, 0);
  h->recording = xcalloc(h->recording_buffers.count, sizeof *h->recording);
  for (size_t i = 0; i < h->recording_buffers.count; i++) {
    h->recording[i].data = h->recording_buffers.values[i]; h->recording[i].length = h->recording_buffers.lengths[i];
  }
  *out_count = h->recording_buffers.count;
  return h->recording;
}
static qa_app_qw_player host_carried_player(void *ctx, qa_client_id client) {
  qw_host *h = ctx;
  qa_owned_actor actor = qa_app_sim_quakec_source_reserved_client(h->game, client);
  qa_app_qw_player player = {client, actor.id, (double)client.slot};
  client_state value;
  memset(&value, 0, sizeof value);
  value.player = player; value.spectator = qa_app_sim_quakec_source_is_spectator_client(h->game, actor.id);
  qa_qw_info_init(&value.info);
  qa_app_sim_quakec_info carried;
  if (h->options.travel_userinfo != NULL && h->options.travel_userinfo(h->options.travel_ctx, client, &carried))
    for (size_t i = 0; i < carried.count; i++) qa_qw_info_set(&value.info, carried.items[i].name, carried.items[i].value);
  value.begun = false; value.command = idle(); value.command_time = qa_app_sim_quakec_source_time_seconds(h->game);
  client_set(h, (double)client.slot, value);
  return player;
}
static void host_disconnect(void *ctx, qa_app_qw_player player, const char *reason) {
  qw_host *h = ctx;
  qa_owned_actor actor;
  if (qa_session_actor_registry_resolve_owned(h->simulation->actors, player.actor, &actor)
      && !qa_app_sim_quakec_source_is_active_client(h->game, actor.id))
    qa_app_sim_quakec_source_disconnect_client(h->game, actor);
  else h->simulation->disconnect_player(h->simulation->ctx, player.actor);
  qa_engine_session_close_client(h->options.session, player.client);
  client_delete(h, player.slot);
  qa_quakeworld_message message = blank("userinfo");
  message.slot = player.slot; message.user_id = 0; message.str_value = xstrdup("");
  queue_admit(h, message, destination(QA_QC_DEST_BROADCAST, true, NULL));
  print_to(h, reason);
}
static const void *const *host_baselines(void *ctx, qa_app_qw_player player, size_t *out_count) {
  (void)player; /* TS baselines: () => baselines */
  qw_host *h = ctx;
  *out_count = h->baseline_count;
  return (const void *const *)h->baseline_pointers;
}
/* prepareDownload: the source owns the mounted bytes until close(). */
static uint8_t *download_read(void *ctx, double offset, double maximum, size_t *out_length) {
  download_ctx *d = ctx;
  if (d->closed) qa_raise("Error", 0, 0, "QW mounted download is closed");
  /* bytes.slice(offset, offset + maximum) */
  double start = offset < 0 ? qa_js_max(0, (double)d->length + offset) : qa_js_min(offset, (double)d->length);
  double end_value = offset + maximum;
  double end = end_value < 0 ? qa_js_max(0, (double)d->length + end_value) : qa_js_min(end_value, (double)d->length);
  size_t length = end > start ? (size_t)(end - start) : 0;
  uint8_t *out = xcalloc(length, 1);
  if (length) memcpy(out, d->bytes + (size_t)start, length);
  *out_length = length;
  return out;
}
static void download_close(void *ctx) { download_ctx *d = ctx; free(d->bytes); d->bytes = NULL; d->closed = true; }
typedef struct path_scope { const char *path; } path_scope;
static void path_body(void *ctx) {
  path_scope *p = ctx;
  size_t count = 0;
  char **parts = qa_contained_file_parts(p->path, &count);
  qa_contained_file_parts_free(parts, count);
}
static const void *host_prepare_download(void *ctx, qa_app_qw_player player, const char *requested) {
  (void)player; /* TS _player */
  qw_host *h = ctx;
  size_t length = strlen(requested);
  char *path = xcalloc(length + 1, 1);
  for (size_t i = 0; i < length; i++) path[i] = requested[i] >= 'A' && requested[i] <= 'Z' ? (char)(requested[i] + 32) : requested[i];
  const qa_download_source *result = NULL;
  qa_caught_error caught;
  path_scope scope = {path};
  if (strstr(path, "..") != NULL || path[0] == '.' || strchr(path, '/') == NULL || !qa_is_served_download_type(path)
      || cvar(h, "allow_download") == 0 || !qa_try(path_body, &scope, &caught)) goto done;
  static const struct { const char *prefix, *setting; } settings[4] = {{"skins/", "skins"}, {"progs/", "models"}, {"sound/", "sounds"}, {"maps/", "maps"}};
  for (int i = 0; i < 4; i++) {
    char name[64];
    snprintf(name, sizeof name, "allow_download_%s", settings[i].setting);
    if (strncmp(path, settings[i].prefix, strlen(settings[i].prefix)) == 0 && cvar(h, name) == 0) goto done;
  }
  uint8_t *bytes = NULL; size_t byte_length = 0;
  qa_resolved_resource_reference reference;
  if (!h->options.content->open(h->options.content->ctx, path, &bytes, &byte_length, &reference)) goto done;
  if ((double)byte_length > 0x7fffffff || !qa_can_download_resource(&reference)
      || (strncmp(path, "maps/", 5) == 0 && reference.provenance.kind == QA_CONTENT_MOUNT_ARCHIVE)) { free(bytes); goto done; }
  download_ctx *download = xcalloc(1, sizeof *download);
  download->bytes = bytes; download->length = byte_length;
  download->source.ctx = download; download->source.byte_length = (double)byte_length;
  download->source.read = download_read; download->source.close = download_close;
  h->downloads = xrealloc(h->downloads, (h->download_count + 1) * sizeof *h->downloads);
  h->downloads[h->download_count++] = download;
  result = &download->source;
done:
  free(path);
  return result;
}
/* signon(player) */
static void s_server_data(void *ctx, qa_quakeworld_message *out) {
  signon_ctx *signon = ctx;
  qw_host *h = signon->h;
  const qa_app_sim_network_simulation *s = h->simulation;
  client_state *client = require_client(h, signon->player);
  qa_compat_qc_machine *machine = qa_app_sim_quakec_source_machine(h->game);
  int64_t message_field = field(h, "message");
  *out = blank("server-data");
  out->protocol = profile28();
  out->server_count = h->options.server_count;
  out->game_directory = xstrdup(qa_app_sim_network_gamedir(h->options.content->content_directory(h->options.content->ctx,
      s->recipe->map.entities.content)));
  out->player_slot = signon->player.slot;
  out->spectator = client->spectator;
  out->level_name = xstrdup(qa_compat_qc_strings_get(&machine->strings, qa_compat_qc_words_int(entity_at(h, 0), message_field)));
  qa_qw_move_variables *v = &out->move_variables;
  v->gravity = cvar(h, "sv_gravity"); v->stop_speed = cvar(h, "sv_stopspeed"); v->max_speed = cvar(h, "sv_maxspeed");
  v->spectator_max_speed = cvar(h, "sv_spectatormaxspeed"); v->accelerate = cvar(h, "sv_accelerate");
  v->air_accelerate = cvar(h, "sv_airaccelerate"); v->water_accelerate = cvar(h, "sv_wateraccelerate");
  v->friction = cvar(h, "sv_friction"); v->water_friction = cvar(h, "sv_waterfriction"); v->entity_gravity = 1;
}
static const char *const *s_models(void *ctx, size_t *out_count) { signon_ctx *s = ctx; *out_count = s->h->model_count; return s->h->models; }
static const char *const *s_sounds(void *ctx, size_t *out_count) { signon_ctx *s = ctx; *out_count = s->h->sound_count; return s->h->sounds; }
static void s_signon_buffers(void *ctx, const uint8_t *const **out, const size_t **lengths, size_t *out_count) {
  signon_ctx *s = ctx;
  *out = (const uint8_t *const *)s->h->signon.values; *lengths = s->h->signon.lengths; *out_count = s->h->signon.count;
}
static bool s_accepts_map_checksum(void *ctx, uint32_t value) { signon_ctx *s = ctx; return value == (uint32_t)s->h->checksum; }
static uint8_t **s_spawn(void *ctx, double start, size_t **lengths, size_t *out_count) {
  signon_ctx *s = ctx;
  qa_app_sim_quakec_source_prepare_client_spawn(s->h->game, s->player.client);
  buffers result = spawn_messages(s->h, s->player, start);
  *lengths = result.lengths; *out_count = result.count;
  return result.values;
}
static void s_begin(void *ctx) {
  signon_ctx *s = ctx;
  qw_host *h = s->h;
  client_state *client = require_client(h, s->player);
  qa_app_sim_player_admission admitted = h->simulation->admit_player(h->simulation->ctx, s->player.client);
  if (!qa_same_actor(admitted.actor, s->player.actor)) qa_raise("Error", 0, 0, "QW begin changed the reserved actor");
  client->begun = true;
}
static void s_disconnect(void *ctx, const char *reason) { signon_ctx *s = ctx; print_to(s->h, reason); }
static qa_download_source *s_open_download(void *ctx, const char *path) { (void)ctx; (void)path; return NULL; }
static const void *host_signon(void *ctx, qa_app_qw_player player) {
  qw_host *h = ctx;
  if (player.slot < 0 || player.slot >= 32) qa_raise("Error", 0, 0, "Stale QW client");
  signon_ctx *signon = h->signons[(size_t)player.slot];
  if (signon == NULL) { signon = xcalloc(1, sizeof *signon); h->signons[(size_t)player.slot] = signon; }
  signon->h = h; signon->player = player;
  signon->host.ctx = signon;
  signon->host.server_data = s_server_data; signon->host.models = s_models; signon->host.sounds = s_sounds;
  signon->host.signon_buffers = s_signon_buffers; signon->host.accepts_map_checksum = s_accepts_map_checksum;
  signon->host.spawn = s_spawn; signon->host.begin = s_begin; signon->host.disconnect = s_disconnect;
  signon->host.open_download = s_open_download;
  return &signon->host;
}
static void host_command_group(void *ctx, qa_app_qw_player player, const qa_qw_user_command *commands, size_t count, double sequence) {
  qw_host *h = ctx;
  client_state *client = require_client(h, player);
  h->simulation->queue_quakeworld_commands(h->simulation->ctx, player.client, commands, count, sequence);
  client->command = count ? commands[count - 1] : idle();
  client->command_time = qa_app_sim_quakec_source_time_seconds(h->game);
}
static size_t utf16_length(const char *text) {
  size_t units = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    if ((*p & 0xC0) != 0x80) units += (*p >= 0xF0) ? 2 : 1;
  return units;
}
static void host_command(void *ctx, qa_app_qw_player player, const char *name, const char *const *args, size_t count) {
  qw_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  if (is(name, "pause")) {
    bool previous = s->q1_paused(s->ctx);
    const char *value = s->toggle_q1_pause(s->ctx, &player.actor);
    qa_quakeworld_message message = blank("print");
    message.level = 2; message.text = xstrdup(value);
    queue_admit(h, message, previous == s->q1_paused(s->ctx) ? destination(QA_QC_DEST_CLIENT, true, &player.actor)
        : destination(QA_QC_DEST_BROADCAST, true, NULL));
  } else if (is(name, "kill")) {
    if (!qa_app_sim_quakec_source_client_kill(h->game, player.actor)) {
      qa_quakeworld_message message = blank("print");
      message.level = 2; message.text = xstrdup("Can't suicide -- allready dead!\n");
      queue_admit(h, message, destination(QA_QC_DEST_CLIENT, true, &player.actor));
    }
  } else if (is(name, "setinfo") && count == 2) {
    const char *key = args[0], *value = args[1];
    if (key == NULL || value == NULL || key[0] == '*') return;
    for (const char *p = key; *p; p++) if (*p == '\\' || *p == '"' || *p == '\n' || *p == '\r') return;
    for (const char *p = value; *p; p++) if (*p == '\\' || *p == '"' || *p == '\n' || *p == '\r') return;
    client_state *client = require_client(h, player);
    /* Info_SetValueForKey: keys and values stay under 64 characters, and the whole string under MAX_INFO_STRING (196). */
    if (utf16_length(key) >= 64 || utf16_length(value) >= 64) return;
    size_t length = 0;
    for (size_t i = 0; i < client->info.count; i++)
      if (strcmp(client->info.entries[i].key, key) != 0)
        length += utf16_length(client->info.entries[i].key) + utf16_length(client->info.entries[i].value) + 2;
    if (value[0] != '\0' && length + utf16_length(key) + utf16_length(value) + 2 > 196) return;
    if (value[0] == '\0') qa_qw_info_delete(&client->info, key); else qa_qw_info_set(&client->info, key, value);
    qa_app_sim_cvar_name_value *items = xcalloc(client->info.count, sizeof *items);
    for (size_t i = 0; i < client->info.count; i++) { items[i].name = client->info.entries[i].key; items[i].value = client->info.entries[i].value; }
    qa_app_sim_quakec_info values = {items, client->info.count};
    qa_app_sim_quakec_source_set_client_info(h->game, player.client, values);
    free(items);
    s->notify_client_event(s->ctx, "userinfo", player.actor);
    qa_quakeworld_message message = blank("set-info");
    message.slot = player.slot; message.key = xstrdup(key); message.str_value = xstrdup(value);
    queue_admit(h, message, destination(QA_QC_DEST_BROADCAST, true, NULL));
  } else {
    char text_value[512];
    snprintf(text_value, sizeof text_value, "Unhandled QW client command: %s", name);
    print_to(h, text_value);
  }
}
static void host_observe(void *ctx, const void *output, const void *const *events_value, size_t count) {
  (void)output; /* TS _output */
  qw_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  const qa_app_sim_presentation_event *const *events = (const qa_app_sim_presentation_event *const *)events_value;
  refresh_authentication(h);
  free_phases(h);
  routed_clear(&h->routed);
  h->routed = h->queued; /* routed = queued.take(cursor); queued.retire() */
  memset(&h->queued, 0, sizeof h->queued);
  bool paused = s->q1_paused(s->ctx);
  if (h->previous_pause != paused) {
    h->previous_pause = paused;
    qa_quakeworld_message message = blank("pause");
    message.paused = paused;
    routed value = {message, destination(QA_QC_DEST_BROADCAST, true, NULL)};
    routed_push(&h->routed, value);
  }
  size_t batch_count = 0;
  const qa_app_sim_quakec_routed *batches = qa_app_sim_quakec_source_drain_messages(h->game, &batch_count);
  for (size_t b = 0; b < batch_count; b++)
    for (size_t e = 0; e < batches[b].entry_count; e++) {
      const qa_quakeworld_message *message = &batches[b].entries[e].message;
      if (is(message->kind, "packet-entities") || is(message->kind, "invalid-delta")) continue;
      routed value = {clone_message(message), batches[b].destination};
      routed_push(&h->routed, value);
    }
  for (size_t i = 0; i < count; i++)
    if (is(events[i]->kind, "view-reset")) {
      qa_quakeworld_message message = blank("set-angle");
      message.angles = events[i]->view_reset.angles;
      routed value = {message, destination(QA_QC_DEST_CLIENT, true, &events[i]->view_reset.actor)};
      routed_push(&h->routed, value);
    }
  for (size_t index = 0; index < 64; index++) {
    const char *value = s->light_style(s->ctx, (double)index);
    if (is(value, h->styles[index])) continue;
    free(h->styles[index]);
    h->styles[index] = xstrdup(value);
    qa_quakeworld_message message = blank("light-style");
    message.index = (double)index; message.str_value = xstrdup(value);
    routed entry = {message, destination(QA_QC_DEST_BROADCAST, true, NULL)};
    routed_push(&h->routed, entry);
  }
  for (size_t o = 0; o < h->order_count; o++) {
    client_state *client = &h->clients[(size_t)h->order[o]];
    if (!client->begun) continue;
    double frags = scalar(h, client->player.actor, "frags");
    if (frags == client->frags) continue;
    client->frags = frags;
    qa_quakeworld_message message = blank("frags");
    message.slot = client->player.slot; message.value = trunc(frags);
    routed entry = {message, destination(QA_QC_DEST_BROADCAST, true, NULL)};
    routed_push(&h->routed, entry);
  }
}
static qa_app_qw_frame host_frame(void *ctx, qa_app_qw_player player, const void *output) {
  (void)output; /* TS frame(player) */
  qw_host *h = ctx;
  client_state *client = require_client(h, player);
  list_clear(&h->frame_messages); list_clear(&h->frame_reliable);
  for (size_t i = 0; i < h->routed.count; i++) {
    const routed *entry = &h->routed.values[i];
    if (!receives(h, player, entry->destination)) continue;
    list_push(entry->destination.kind != QA_QC_DEST_SIGNON && entry->destination.reliable ? &h->frame_reliable : &h->frame_messages,
        clone_message(&entry->message));
  }
  stat_entry values[16];
  size_t stat_count = stats(h, player.actor, values);
  for (size_t i = 0; i < stat_count; i++) {
    double value = trunc(values[i].value);
    size_t index = (size_t)values[i].index;
    if (client->has_stat[index] && client->stats[index].value == value) continue;
    client->has_stat[index] = true; client->stats[index].index = values[i].index; client->stats[index].value = value;
    qa_quakeworld_message message = blank("stat");
    message.index = values[i].index; message.value = value;
    list_push(&h->frame_reliable, message);
  }
  for (size_t o = 0; o < h->order_count; o++) {
    const client_state *other = &h->clients[(size_t)h->order[o]];
    if (other->begun && (!other->spectator || qa_same_actor(other->player.actor, player.actor)) && visible(h, player.actor, other->player.actor)) {
      qa_quakeworld_message message = blank("player");
      message.player = player_state(h, other, player);
      list_push(&h->frame_messages, message);
    }
  }
  size_t actor_count = 0;
  qa_actor_id *actors = source_actors(h, &actor_count);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, actors);
  free(h->frame_entities);
  h->frame_entities = xcalloc(64, sizeof *h->frame_entities);
  size_t entity_count = 0;
  qa_qw_nail nails[32];
  size_t nail_count = 0;
  double spike = model_index(h, "progs/spike.mdl", NAN), super_spike = model_index(h, "progs/s_spike.mdl", NAN);
  for (size_t i = 0; i < actor_count; i++) {
    if (!visible(h, player.actor, actors[i])) continue;
    qa_quakeworld_entity entity = state(h, actors[i]);
    if (entity.state.base.model_index == spike || entity.state.base.model_index == super_spike) {
      if (nail_count < 32) {
        nails[nail_count].origin = entity.state.base.origin;
        nails[nail_count].pitch = entity.state.base.angles.x; nails[nail_count].yaw = entity.state.base.angles.y;
        nail_count++;
      }
    } else if (entity_count < 64) h->frame_entities[entity_count++] = entity;
  }
  qa_cleanup_pop(&guard);
  free(actors);
  if (nail_count != 0) {
    qa_quakeworld_message message = blank("nails");
    message.projectiles = copy_array(nails, nail_count, sizeof *nails); message.projectile_count = nail_count;
    list_push(&h->frame_messages, message);
  }
  free(h->frame_message_pointers); free(h->frame_reliable_pointers); free(h->frame_entity_pointers);
  h->frame_message_pointers = xcalloc(h->frame_messages.count, sizeof(void *));
  for (size_t i = 0; i < h->frame_messages.count; i++) h->frame_message_pointers[i] = &h->frame_messages.values[i];
  h->frame_reliable_pointers = xcalloc(h->frame_reliable.count, sizeof(void *));
  for (size_t i = 0; i < h->frame_reliable.count; i++) h->frame_reliable_pointers[i] = &h->frame_reliable.values[i];
  h->frame_entity_pointers = xcalloc(entity_count, sizeof(void *));
  for (size_t i = 0; i < entity_count; i++) h->frame_entity_pointers[i] = &h->frame_entities[i];
  qa_app_qw_frame frame;
  frame.messages = (const void *const *)h->frame_message_pointers; frame.message_count = h->frame_messages.count;
  frame.reliable = (const void *const *)h->frame_reliable_pointers; frame.reliable_count = h->frame_reliable.count;
  frame.entities = (const void *const *)h->frame_entity_pointers; frame.entity_count = entity_count;
  return frame;
}
static const void *host_client_info(void *ctx, qa_app_qw_player player) { return &require_client(ctx, player)->info; }
static const void *host_supports_source_wire(void *ctx) { qw_host *h = ctx; h->support.supported = true; return &h->support; }
static const void *const *host_masters(void *ctx, size_t *out_count) {
  qw_host *h = ctx;
  *out_count = h->options.master_count;
  return h->options.masters;
}
static void host_print(void *ctx, const char *text_value) { print_to(ctx, text_value); }

void qa_app_sim_qw_application_server_host_destroy(qa_app_qw_server_host host) {
  qw_host *h = host.ctx;
  if (h == NULL) return;
  for (size_t i = 0; i < 32; i++) { if (h->clients[i].used) qa_qw_info_destroy(&h->clients[i].info); free(h->signons[i]); }
  for (size_t i = 0; i < 64; i++) free(h->styles[i]);
  free(h->baselines); free(h->baseline_pointers);
  free((void *)h->models); free((void *)h->sounds);
  buffers_clear(&h->signon); buffers_clear(&h->recording_buffers); free(h->recording);
  routed_clear(&h->routed); routed_clear(&h->queued);
  list_clear(&h->frame_messages); list_clear(&h->frame_reliable);
  free(h->frame_message_pointers); free(h->frame_reliable_pointers); free(h->frame_entity_pointers); free(h->frame_entities);
  for (size_t i = 0; i < h->download_count; i++) { free(h->downloads[i]->bytes); free(h->downloads[i]); }
  free(h->downloads);
  free(h->password); free(h->spectator_password);
  free_phases(h);
  free(h);
}

/* TS `get paused() { return simulation.q1Paused; }`. */
static bool host_paused(void *ctx) {
  qw_host *h = ctx;
  return h->simulation->q1_paused(h->simulation->ctx);
}
/* TS networkInfo: (player, info) => game.setClientNetwork(player.client, info). */
static void host_network_info(void *ctx, qa_app_qw_player player, qa_app_qw_network_info info) {
  qw_host *h = ctx;
  qa_app_sim_quakec_client_network network = { info.ctx, info.address, info.ping };
  qa_app_sim_quakec_source_set_client_network(h->game, player.client, network);
}
qa_app_qw_server_host qa_app_sim_create_qw_application_server_host(qa_app_sim_qw_server_binding_options options) {
  const qa_app_sim_network_simulation *s = options.simulation;
  const qa_app_sim_network_content *content = options.content;
  qa_app_sim_quakec_source *game = options.quakec;
  if (game == NULL || !is(qa_app_sim_quakec_source_kind(game), "quakeworld") || content->world.kind != QA_DECODED_WORLD_Q1)
    qa_raise("Error", 0, 0, "Native QW wire requires its admitted QuakeWorld source and 32 reserved players");
  qw_host *h = xcalloc(1, sizeof *h);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, h);
  h->options = options; h->simulation = s; h->game = game; h->world = content->world.as.q1;
  /* The source's name list is scratch storage the next precacheNames call replaces: keep copies (TS arrays). */
  const char *const *names = qa_app_sim_quakec_source_precache_names(game, "model", &h->model_count);
  const char **copy = xcalloc(h->model_count + 1, sizeof *copy);
  memcpy(copy, names, h->model_count * sizeof *copy);
  h->models = copy;
  names = qa_app_sim_quakec_source_precache_names(game, "sound", &h->sound_count);
  copy = xcalloc(h->sound_count + 1, sizeof *copy);
  memcpy(copy, names, h->sound_count * sizeof *copy);
  h->sounds = copy;
  if (h->model_count >= 256 || h->sound_count >= 256) qa_raise("Error", 0, 0, "Native QW precaches exceed 8-bit indices");
  size_t length = 0;
  uint8_t *bytes = content->read_path(content->ctx, s->recipe->map.geometry.requested_path, &length);
  h->checksum = qa_quake_world_map_checksum2(bytes, length);
  free(bytes);
  qa_cvar_registry *cvars = qa_app_sim_quakec_source_cvars(game);
  static const char *const downloads[5] = {"allow_download", "allow_download_skins", "allow_download_models", "allow_download_sounds", "allow_download_maps"};
  for (int i = 0; i < 5; i++) {
    qa_cvar_read read;
    if (qa_cvar_registry_find(cvars, downloads[i], &read)) { qa_cvar_read_free(&read); continue; }
    qa_cvar_snapshot snapshot;
    qa_cvar_registry_register(cvars, downloads[i], "1", 0, &snapshot); qa_cvar_read_free(&snapshot);
  }
  h->player_model = model_index(h, "progs/player.mdl", 0);
  size_t actor_count = 0;
  qa_actor_id *actors = source_actors(h, &actor_count);
  h->baselines = xcalloc(actor_count + 32, sizeof *h->baselines);
  for (size_t i = 0; i < actor_count; i++) { h->baselines[i] = state(h, actors[i]); h->baselines[i].state.base.effects = 0; }
  free(actors);
  h->baseline_count = actor_count;
  for (double index = 1; index <= 32; index++) {
    qa_quakeworld_entity *entity = &h->baselines[h->baseline_count++];
    memset(entity, 0, sizeof *entity);
    entity->state.base.number = index; entity->state.base.model_index = h->player_model; entity->state.base.color_map = index;
    entity->state.scale = 16;
  }
  h->baseline_pointers = xcalloc(h->baseline_count, sizeof *h->baseline_pointers);
  for (size_t i = 0; i < h->baseline_count; i++) h->baseline_pointers[i] = &h->baselines[i];
  message_list records;
  memset(&records, 0, sizeof records);
  for (size_t i = 0; i < h->baseline_count; i++) {
    qa_quakeworld_message message = blank("baseline");
    message.state = h->baselines[i].state;
    list_push(&records, message);
  }
  if (content->has_prepared_quakec) {
    size_t signon_count = 0;
    const qa_compat_qc_routed_message *signon = qa_app_sim_quakec_source_signon_messages(game, &signon_count);
    for (size_t i = 0; i < signon_count; i++) {
      if (is(signon[i].message.kind, "packet-entities") || is(signon[i].message.kind, "invalid-delta")) continue;
      list_push(&records, clone_message(&signon[i].message));
    }
  }
  size_t persistent_count = 0;
  const qa_app_sim_presentation_event *const *persistent = s->persistent_events(s->ctx, &persistent_count);
  for (size_t i = 0; i < persistent_count; i++) {
    if (!is(persistent[i]->kind, "q1")) continue;
    const qa_q1_event *event = persistent[i]->q1;
    if (is(event->kind, "ambient")) {
      double index = 0; /* sounds.indexOf(path) + 1 */
      for (size_t k = 0; k < h->sound_count && index == 0; k++) if (strcmp(h->sounds[k], event->path) == 0) index = (double)k + 1;
      qa_quakeworld_message message = blank("static-sound");
      message.entity = 0; message.channel = 0; message.index = index; message.volume = trunc(event->volume * 255);
      message.attenuation = event->attenuation; message.origin = event->origin;
      list_push(&records, message);
    } else if (is(event->kind, "static-model")) {
      qa_quakeworld_message message = blank("static");
      message.state.base.number = 0; message.state.base.model_index = model_index(h, event->path, 0);
      message.state.base.frame = event->frame; message.state.base.color_map = event->color_map; message.state.base.skin = event->skin;
      message.state.base.origin = event->origin; message.state.base.angles = event->angles; message.state.scale = 16;
      list_push(&records, message);
    }
  }
  h->signon = encode(records.values, records.count);
  list_clear(&records);
  for (size_t i = 0; i < 64; i++) h->styles[i] = xstrdup(s->light_style(s->ctx, (double)i));
  h->previous_pause = s->q1_paused(s->ctx);
  refresh_authentication(h);
  qa_cleanup_pop(&guard);
  qa_app_qw_server_host host;
  memset(&host, 0, sizeof host);
  host.ctx = h;
  host.administration = options.administration;
  if (options.masters) host.masters = host_masters;
  host.authentication = &h->authentication;
  host.max_clients = 32;
  host.paused = host_paused;
  host.network_info = host_network_info;
  host.supports_source_wire = host_supports_source_wire;
  host.client_info = host_client_info;
  host.command_phase = host_command_phase;
  host.admit = host_admit;
  host.recording_player = host_recording_player;
  host.recording_signon = host_recording_signon;
  host.carried_player = host_carried_player;
  host.disconnect = host_disconnect;
  host.baselines = host_baselines;
  host.prepare_download = host_prepare_download;
  host.signon = host_signon;
  host.command_group = host_command_group;
  host.command = host_command;
  host.observe = host_observe;
  host.frame = host_frame;
  host.print = host_print;
  return host;
}
