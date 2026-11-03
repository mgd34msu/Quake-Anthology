/* Port of src/app/bootstrap/simulation/network.ts (createQ2ApplicationServerHost): the
 * adapter reads shared actor/body/combat state and source player fields on every
 * frame. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include "qa/app/bootstrap/network/q2_effects.h"
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/content/q2/base/player/index.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/core/commands/text.h"
#include "qa/core/md4.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/common/session.h"
#include "qa/network/q2/adapters.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/connectionless.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/server_write.h"
#include "qa/network/q2/solid.h"

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
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (!out) qa_range_error("Out of memory");
  memcpy(out, text, length + 1);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static const char *text_or_empty(const char *text) { return text ? text : ""; }

/* ---- shared helpers ------------------------------------------------------------------ */
const char *qa_app_sim_network_gamedir(const char *content_directory) {
  /* split('/').at(-1) is never undefined for a string, so the ?? fallback cannot apply. */
  const char *slash = strrchr(content_directory, '/');
  return slash ? slash + 1 : content_directory;
}
void qa_app_sim_info_set(qa_app_sim_info *info, const char *key, const char *value) {
  for (size_t i = 0; i < info->count; i++)
    if (strcmp(info->pairs[i].key, key) == 0) {
      char *copy = xstrdup(value);
      free(info->pairs[i].value);
      info->pairs[i].value = copy;
      return;
    }
  if (info->count == info->capacity) {
    info->capacity = info->capacity ? info->capacity * 2 : 8;
    info->pairs = xrealloc(info->pairs, info->capacity * sizeof *info->pairs);
  }
  info->pairs[info->count].key = xstrdup(key);
  info->pairs[info->count].value = xstrdup(value);
  info->count++;
}
const char *qa_app_sim_info_get(const qa_app_sim_info *info, const char *key) {
  for (size_t i = 0; i < info->count; i++) if (strcmp(info->pairs[i].key, key) == 0) return info->pairs[i].value;
  return NULL;
}
void qa_app_sim_info_delete(qa_app_sim_info *info, const char *key) {
  for (size_t i = 0; i < info->count; i++)
    if (strcmp(info->pairs[i].key, key) == 0) {
      free(info->pairs[i].key); free(info->pairs[i].value);
      memmove(info->pairs + i, info->pairs + i + 1, (info->count - i - 1) * sizeof *info->pairs);
      info->count--;
      return;
    }
}
void qa_app_sim_info_clear(qa_app_sim_info *info) {
  for (size_t i = 0; i < info->count; i++) { free(info->pairs[i].key); free(info->pairs[i].value); }
  free(info->pairs);
  memset(info, 0, sizeof *info);
}
void qa_app_sim_info_copy(qa_app_sim_info *out, const qa_app_sim_info *info) {
  memset(out, 0, sizeof *out);
  for (size_t i = 0; i < info->count; i++) qa_app_sim_info_set(out, info->pairs[i].key, info->pairs[i].value);
}
void qa_app_sim_info_parse_q2(qa_app_sim_info *info, const char *source) {
  memset(info, 0, sizeof *info);
  const char *cursor = source[0] == '\\' ? source + 1 : source;
  /* split("\\") then pairwise; a trailing odd token is dropped. */
  size_t token_count = 1;
  for (const char *p = cursor; *p; p++) if (*p == '\\') token_count++;
  char **tokens = xcalloc(token_count, sizeof *tokens);
  size_t index = 0;
  const char *start = cursor;
  for (const char *p = cursor;; p++)
    if (*p == '\\' || *p == '\0') {
      size_t length = (size_t)(p - start);
      tokens[index] = xcalloc(length + 1, 1);
      memcpy(tokens[index], start, length);
      index++;
      if (*p == '\0') break;
      start = p + 1;
    }
  for (size_t i = 0; i + 1 < token_count; i += 2)
    if (qa_app_sim_info_get(info, tokens[i]) == NULL) qa_app_sim_info_set(info, tokens[i], tokens[i + 1]);
  for (size_t i = 0; i < token_count; i++) free(tokens[i]);
  free(tokens);
}
char *qa_app_sim_info_text(const qa_app_sim_info *info) {
  size_t length = 1;
  for (size_t i = 0; i < info->count; i++) length += strlen(info->pairs[i].key) + strlen(info->pairs[i].value) + 2;
  char *out = xcalloc(length, 1);
  char *cursor = out;
  for (size_t i = 0; i < info->count; i++) {
    *cursor++ = '\\';
    size_t key = strlen(info->pairs[i].key), value = strlen(info->pairs[i].value);
    memcpy(cursor, info->pairs[i].key, key); cursor += key;
    *cursor++ = '\\';
    memcpy(cursor, info->pairs[i].value, value); cursor += value;
  }
  *cursor = '\0';
  return out;
}
void qa_app_sim_info_fold_seat(qa_app_sim_info *info, double seat) {
  char suffix[48];
  snprintf(suffix, sizeof suffix, "_%s", QA_JS_NUMBER(seat));
  size_t suffix_length = strlen(suffix);
  /* for (const [key, value] of [...pairs]): a snapshot of the pairs. */
  qa_app_sim_info snapshot;
  qa_app_sim_info_copy(&snapshot, info);
  for (size_t i = 0; i < snapshot.count; i++) {
    const char *key = snapshot.pairs[i].key;
    size_t length = strlen(key);
    if (length < suffix_length || strcmp(key + length - suffix_length, suffix) != 0) continue;
    char *base = xcalloc(length - suffix_length + 1, 1);
    memcpy(base, key, length - suffix_length);
    qa_app_sim_info_set(info, base, snapshot.pairs[i].value);
    free(base);
  }
  qa_app_sim_info_clear(&snapshot);
}
char *qa_app_sim_address_key(const qa_net_address *address) {
  char *out = qa_net_address_key(address, true);
  if (!out) qa_range_error("Out of memory");
  return out;
}

/* ---- createQ2ApplicationServerHost --------------------------------------------------- */
/* An insertion-ordered Map<number, string> over config-string indexes. */
typedef struct config_map { char **values; size_t capacity; double *order; size_t count, order_capacity; } config_map;
static const char *config_get(const config_map *map, double index) {
  return index >= 0 && index < (double)map->capacity ? map->values[(size_t)index] : NULL;
}
static void config_set(config_map *map, double index, const char *value) {
  if (!(index >= 0) || index != trunc(index) || index > 1e7) qa_range_error("Invalid Q2 config-string index");
  if ((size_t)index >= map->capacity) {
    size_t capacity = (size_t)index + 1 > map->capacity * 2 ? (size_t)index + 1 : map->capacity * 2;
    map->values = xrealloc(map->values, capacity * sizeof *map->values);
    memset(map->values + map->capacity, 0, (capacity - map->capacity) * sizeof *map->values);
    map->capacity = capacity;
  }
  char *copy = xstrdup(value);
  char **slot = &map->values[(size_t)index];
  if (*slot == NULL) {
    if (map->count == map->order_capacity) {
      map->order_capacity = map->order_capacity ? map->order_capacity * 2 : 64;
      map->order = xrealloc(map->order, map->order_capacity * sizeof *map->order);
    }
    map->order[map->count++] = index;
  }
  free(*slot);
  *slot = copy;
}
static void config_clear(config_map *map) {
  for (size_t i = 0; i < map->capacity; i++) free(map->values[i]);
  free(map->values); free(map->order);
  memset(map, 0, sizeof *map);
}
static void config_copy(config_map *out, const config_map *map) {
  memset(out, 0, sizeof *out);
  for (size_t i = 0; i < map->count; i++) config_set(out, map->order[i], map->values[(size_t)map->order[i]]);
}
/* A resource name table: name -> ordinal (insertion order is ordinal order). */
typedef struct name_table { char **names; size_t count; } name_table;
static void table_clear(name_table *table) {
  for (size_t i = 0; i < table->count; i++) free(table->names[i]);
  free(table->names);
  memset(table, 0, sizeof *table);
}

typedef struct q2_discovery_value {
  void *ctx;
  qa_q2_status (*status)(void *ctx);
  qa_q2_host_info (*info)(void *ctx);
} q2_discovery_value;
typedef struct actor_number { qa_actor_id actor; double value; } actor_number;
typedef struct owned_events { qa_q2_server_event *events; qa_app_q2_server_event *wrapped; size_t count, capacity; } owned_events;
typedef struct per_player {
  bool used; qa_app_q2_player player;
  bool has_known; config_map known;
  bool has_state; qa_q2_server_data_result data; qa_app_q2_config_snapshot configs; qa_q2_baseline_list baselines;
  bool has_frame; qa_q2_wire_frame frame;
  owned_events events;
} per_player;
typedef struct q2_host {
  qa_app_sim_q2_server_binding_options options;
  const qa_app_sim_network_simulation *simulation;
  qa_q2_product_runtime *source;
  qa_q2_game_services *game;
  qa_cvar_registry *cvars;
  qa_app_q2_downloads *downloads;
  qa_app_q2_layout layout;
  name_table models, sounds, images;
  config_map configs;
  per_player *players; size_t player_count; /* clients + knownConfigs by client slot */
  actor_number *entity_events; size_t entity_event_count;
  double event_frame;
  /* cachedStates */
  bool has_cached; const void *cached_output; qa_q2_protocol_identity cached_protocol;
  qa_q2_wire_entity *cached_states; size_t cached_count;
  qa_q2_server_message_options message_options;
  q2_discovery_value discovery;
  qa_wire_admission support;
  qa_q2_status_player *status_players; size_t status_player_count; char *server_info; char *hostname;
  /* mvdCapture storage */
  qa_q2_mvd_capture capture;
  qa_q2_mvd_capture_config *capture_configs; qa_q2_mvd_capture_player *capture_players;
  qa_q2_wire_entity *capture_entities; qa_q2_mvd_emission *capture_messages; size_t capture_message_count;
  uint8_t *portal_bits; char *capture_gamedir;
  /* input storage, released at observe */
  qa_actor_command **commands; size_t command_count;
} q2_host;

static void print_to(q2_host *h, const char *text) { h->options.print(h->options.print_ctx, text); }
static per_player *player_slot(q2_host *h, double slot) {
  if (slot < 0 || slot != trunc(slot)) qa_range_error("Invalid Q2 client slot");
  if ((size_t)slot >= h->player_count) {
    size_t count = (size_t)slot + 1;
    h->players = xrealloc(h->players, count * sizeof *h->players);
    memset(h->players + h->player_count, 0, (count - h->player_count) * sizeof *h->players);
    h->player_count = count;
  }
  return &h->players[(size_t)slot];
}
static void clear_events(owned_events *events) {
  for (size_t i = 0; i < events->count; i++) qa_q2_server_event_clear(&events->events[i]);
  free(events->events); free(events->wrapped);
  memset(events, 0, sizeof *events);
}
static qa_q2_server_event *push_event(owned_events *events, bool has_reliable, bool reliable) {
  if (events->count == events->capacity) {
    events->capacity = events->capacity ? events->capacity * 2 : 16;
    events->events = xrealloc(events->events, events->capacity * sizeof *events->events);
    events->wrapped = xrealloc(events->wrapped, events->capacity * sizeof *events->wrapped);
  }
  qa_q2_server_event *event = &events->events[events->count];
  memset(event, 0, sizeof *event);
  events->wrapped[events->count].event = NULL;
  events->wrapped[events->count].has_reliable = has_reliable;
  events->wrapped[events->count].reliable = reliable;
  events->count++;
  return event;
}
static void clear_state(per_player *entry) {
  if (!entry->has_state) return;
  for (size_t i = 0; i < entry->configs.count; i++) free(entry->configs.entries[i].value);
  free(entry->configs.entries);
  qa_q2_baseline_list_free(&entry->baselines);
  memset(&entry->configs, 0, sizeof entry->configs);
  entry->has_state = false;
}
static void clear_player(per_player *entry) {
  clear_state(entry);
  if (entry->has_frame) qa_q2_wire_frame_free(&entry->frame);
  entry->has_frame = false;
  clear_events(&entry->events);
}

/* sourceNumber(actor) */
static double source_number(q2_host *h, qa_actor_id actor) {
  qa_actor_source_result address = qa_session_actor_registry_source_of(h->simulation->actors, actor);
  if (!address.has_value || !is(address.provider, h->simulation->recipe->map.entities.provider))
    qa_raise("Error", 0, 0, "Actor has no Q2 source entity address");
  return (double)address.slot;
}
/* index(path, table, base, maximum) */
static double resource_index(q2_host *h, const char *path, name_table *table, double base, double maximum) {
  if (path == NULL || path[0] == '\0') return 0;
  for (size_t i = 0; i < table->count; i++) if (strcmp(table->names[i], path) == 0) return (double)i + 1;
  double value = (double)table->count + 1;
  if (value >= maximum) qa_raise("Error", 0, 0, "Native Q2 resource table is full");
  table->names = xrealloc(table->names, (table->count + 1) * sizeof *table->names);
  table->names[table->count++] = xstrdup(path);
  config_set(&h->configs, base + value, path);
  return value;
}
static double model(q2_host *h, const char *path) { return resource_index(h, path, &h->models, h->layout.models, h->layout.max_models); }
static double sound(q2_host *h, const char *path) { return resource_index(h, path, &h->sounds, h->layout.sounds, h->layout.max_sounds); }
static double image(q2_host *h, const char *path) { return resource_index(h, path, &h->images, h->layout.images, h->layout.max_images); }
static void update_movement_configs(q2_host *h) {
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_app_sim_q2_movement_config movement;
  if (!s->q2_movement_config(s->ctx, &movement)) qa_raise("Error", 0, 0, "Q2 host lost source movement configuration");
  config_set(&h->configs, h->layout.air_accelerate, QA_JS_NUMBER(movement.air_accelerate));
  if (h->layout.has_n64_physics) config_set(&h->configs, h->layout.n64_physics, movement.n64_physics ? "1" : "0");
}
static double inventory_ordinal(q2_host *h, qa_item_id item) {
  size_t count = 0;
  const qa_q2_inventory_item *items = qa_q2_item_module_list(h->source->items, &count);
  for (size_t i = 0; i < count; i++) if (is(items[i].id, item)) return (double)i + 1;
  return 0; /* findIndex -1 + 1 */
}
static qa_body_state_result body_of(q2_host *h, qa_actor_id actor) { return qa_shared_body_table_read(h->simulation->bodies, actor); }
static void vector(qa_q2_wire_vec3 target, qa_vec3 value) { qa_q2_vec3_make(target, value.x, value.y, value.z); }
static qa_q2_solid_encoding solid_encoding(qa_q2_protocol_identity protocol) {
  qa_q2_solid_encoding encoding = QA_Q2_SOLID_SHORT;
  (void)qa_q2_get_solid_encoding(protocol, false, &encoding);
  return encoding;
}
static const char *spawn_value(const qa_q2_entity *entity, const char *key) {
  const char *found = NULL; /* Map construction: the last pair wins. */
  for (size_t i = 0; i < entity->spawn.value_count; i++)
    if (strcmp(entity->spawn.values[i].key, key) == 0) found = entity->spawn.values[i].value;
  return found;
}
/* presentations().filter(!viewWeapon) keyed by actor (the last one wins). */
static const qa_app_simulation_presentation *presentation_of(const qa_app_simulation_presentation *values, size_t count, qa_actor_id actor) {
  const qa_app_simulation_presentation *found = NULL;
  for (size_t i = 0; i < count; i++) if (!values[i].view_weapon && qa_same_actor(values[i].actor, actor)) found = &values[i];
  return found;
}
static int compare_numbers(const void *a, const void *b) {
  double left = ((const qa_q2_wire_entity *)a)->number, right = ((const qa_q2_wire_entity *)b)->number;
  return left < right ? -1 : left > right ? 1 : 0;
}
/* entityStates(protocol): malloc'd array. */
static qa_q2_wire_entity *entity_states(q2_host *h, qa_q2_protocol_identity protocol, size_t *out_count) {
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t presentation_count = 0;
  const qa_app_simulation_presentation *presentations = s->presentations(s->ctx, &presentation_count);
  size_t entity_count = 0;
  qa_q2_entity *const *entities = h->game->entities(h->game->ctx, &entity_count);
  qa_q2_wire_entity *result = xcalloc(entity_count, sizeof *result);
  size_t count = 0;
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, result);
  for (size_t i = 0; i < entity_count; i++) {
    const qa_q2_entity *entity = entities[i];
    double number = source_number(h, entity->actor.id);
    qa_body_state_result body = body_of(h, entity->actor.id);
    if (number == 0 || !body.has_value || !entity->visible || qa_js_bit_and(entity->server_flags, 1) != 0) continue;
    const qa_app_simulation_presentation *presentation = presentation_of(presentations, presentation_count, entity->actor.id);
    qa_q2_wire_entity wire;
    qa_q2_wire_entity_init(&wire);
    wire.number = number;
    vector(wire.origin, body.value.origin);
    vector(wire.old_origin, presentation && presentation->has_previous_origin ? presentation->previous_origin : body.value.origin);
    vector(wire.angles, body.value.angles);
    wire.modelindex = model(h, presentation ? presentation->path : entity->model);
    wire.modelindex2 = model(h, entity->model2);
    wire.modelindex3 = model(h, entity->model3);
    wire.modelindex4 = model(h, entity->model4);
    wire.frame = presentation ? presentation->frame : entity->frame;
    qa_scene_flare flare; qa_vec3 rim;
    if (qa_q2_entity_flare(entity, &flare, &rim)) {
      wire.modelindex = 1;
      wire.modelindex2 = flare.fade_start;
      wire.modelindex3 = flare.fade_end;
      wire.frame = qa_js_bit_and(entity->render_flags, 256) != 0 ? image(h, flare.image) : 0;
    }
    wire.skinnum = presentation ? presentation->skin : entity->skin;
    wire.effects = presentation ? presentation->effects : entity->effects;
    wire.renderfx = presentation ? presentation->render_flags : entity->render_flags;
    wire.event = 0;
    for (size_t e = 0; e < h->entity_event_count; e++)
      if (qa_same_actor(h->entity_events[e].actor, entity->actor.id)) wire.event = h->entity_events[e].value;
    qa_q2_base_player_state *native = qa_q2_players_get_state(h->source->players, entity->actor.id);
    if (native != NULL) {
      wire.modelindex = 255;
      wire.skinnum = native->slot;
      qa_q2w_state *weapon = qa_q2_players_weapon_state(h->source->players, entity->actor.id);
      if (weapon != NULL && weapon->weapon != NULL) {
        wire.modelindex2 = 255;
        const qa_q2w_definition *definition = qa_q2w_weapons_definition(h->source->weapons, weapon->weapon);
        wire.skinnum = qa_js_bit_or(wire.skinnum, qa_js_shl(definition->player_model, 8));
      }
    }
    wire.sound = sound(h, entity->sound);
    wire.loop_volume = is(entity->classname, "target_speaker") && qa_js_bit_and(entity->spawnflags, 3) != 0 ? 1 : entity->volume;
    wire.loop_attenuation = entity->attenuation;
    wire.scale = entity->scale == 1 ? 0 : entity->scale;
    if (is(entity->solid, "brush")) wire.solid = 31;
    else if (is(entity->solid, "box")) wire.solid = qa_q2_pack_solid(body.value.bounds, solid_encoding(protocol));
    if (wire.modelindex != 0 || wire.sound != 0 || wire.effects != 0 || wire.event != 0) result[count++] = wire;
  }
  qa_cleanup_pop(&guard);
  /* Entity numbers are unique source slots, so the unstable sort orders as TS does. */
  qsort(result, count, sizeof *result, compare_numbers);
  *out_count = count;
  return result;
}
static bool same_protocol(qa_q2_protocol_identity a, qa_q2_protocol_identity b) {
  return is(a.kind, b.kind) && a.version == b.version && a.revision == b.revision && a.flags == b.flags;
}
static const qa_q2_wire_entity *frame_entity_states(q2_host *h, qa_q2_protocol_identity protocol, const void *output, size_t *out_count) {
  if (h->has_cached && h->cached_output == output && same_protocol(h->cached_protocol, protocol)) {
    *out_count = h->cached_count;
    return h->cached_states;
  }
  size_t count = 0;
  qa_q2_wire_entity *states = entity_states(h, protocol, &count);
  free(h->cached_states);
  h->cached_states = states; h->cached_count = count;
  h->has_cached = true; h->cached_output = output; h->cached_protocol = protocol;
  *out_count = count;
  return states;
}
static double *copy_leaves(qa_leaf_query_result result, size_t *out_count) {
  double *leaves = xcalloc(result.leaves.count, sizeof *leaves);
  if (result.leaves.count) memcpy(leaves, result.leaves.values, result.leaves.count * sizeof *leaves);
  *out_count = result.leaves.count;
  return leaves;
}
typedef struct visibility { double area, cluster; double *clusters; size_t cluster_count; } visibility;
static bool entity_visible(q2_host *h, qa_app_q2_player player, const qa_q2_wire_entity *state, const qa_q2_entity *entity,
    qa_vec3 origin, const visibility *view) {
  qa_shared_scene_queries *scene = h->simulation->scene;
  if (state->number == player.source_entity) return true;
  /* Rerelease speaker ATTN_LOOP_NONE carries source SVF_NOCULL semantics. */
  if (h->game->options.edition == QA_Q2_EDITION_RERELEASE && is(entity->classname, "target_speaker")
      && qa_js_bit_and(entity->spawnflags, 3) != 0 && entity->attenuation == -1) return true;
  qa_body_state_result body = body_of(h, entity->actor.id);
  if (!body.has_value) return false;
  qa_linked_body_result linked = qa_shared_body_table_linked(h->simulation->bodies, entity->actor.id);
  qa_bounds bounds;
  if (linked.has_value) bounds = linked.value.absolute_bounds;
  else {
    bounds.min.x = body.value.origin.x + body.value.bounds.min.x - 1; bounds.min.y = body.value.origin.y + body.value.bounds.min.y - 1;
    bounds.min.z = body.value.origin.z + body.value.bounds.min.z - 1;
    bounds.max.x = body.value.origin.x + body.value.bounds.max.x + 1; bounds.max.y = body.value.origin.y + body.value.bounds.max.y + 1;
    bounds.max.z = body.value.origin.z + body.value.bounds.max.z + 1;
  }
  const qa_decoded_world *world = &h->options.content->world;
  double limit = world->kind == QA_DECODED_WORLD_Q2 ? (double)world->as.q2->leaf_count : 65536;
  size_t leaf_count = 0;
  double *leaves = copy_leaves(qa_shared_box_leaves(scene, bounds, limit), &leaf_count);
  bool connected = false;
  for (size_t i = 0; i < leaf_count && !connected; i++)
    connected = qa_shared_areas_connected(scene, view->area, qa_shared_leaf_area(scene, leaves[i]));
  if (!connected) { free(leaves); return false; }
  if (qa_js_bit_and(state->renderfx, 128) != 0) {
    bool visible = leaf_count != 0 && qa_shared_cluster_visible(scene, view->cluster, qa_shared_leaf_cluster(scene, leaves[0]), "phs");
    free(leaves);
    return visible;
  }
  bool seen = false;
  for (size_t i = 0; i < leaf_count && !seen; i++) {
    double target = qa_shared_leaf_cluster(scene, leaves[i]);
    for (size_t c = 0; c < view->cluster_count && !seen; c++)
      seen = qa_shared_cluster_visible(scene, view->clusters[c], target, "pvs");
  }
  free(leaves);
  if (!seen) return false;
  return state->modelindex != 0
      || qa_js_hypot3(origin.x - body.value.origin.x, origin.y - body.value.origin.y, origin.z - body.value.origin.z) <= 400;
}
/* visibleEntities(player, states, origin) into frame->entities. */
static void visible_entities(q2_host *h, qa_app_q2_player player, const qa_q2_wire_entity *states, size_t state_count,
    qa_vec3 origin, qa_q2_wire_frame *frame) {
  qa_shared_scene_queries *scene = h->simulation->scene;
  visibility view;
  double leaf = qa_shared_point_leaf(scene, origin);
  view.area = qa_shared_leaf_area(scene, leaf);
  view.cluster = qa_shared_leaf_cluster(scene, leaf);
  qa_bounds box = {{origin.x - 8, origin.y - 8, origin.z - 8}, {origin.x + 8, origin.y + 8, origin.z + 8}};
  size_t fat_count = 0;
  double *fat = copy_leaves(qa_shared_box_leaves(scene, box, 64), &fat_count);
  view.clusters = xcalloc(fat_count, sizeof *view.clusters);
  view.cluster_count = 0;
  for (size_t i = 0; i < fat_count; i++) {
    double cluster = qa_shared_leaf_cluster(scene, fat[i]);
    bool known = false;
    for (size_t c = 0; c < view.cluster_count && !known; c++) known = view.clusters[c] == cluster;
    if (!known) view.clusters[view.cluster_count++] = cluster;
  }
  free(fat);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, view.clusters);
  /* byNumber: Map(entities by source number), the last one wins. */
  size_t entity_count = 0;
  qa_q2_entity *const *entities = h->game->entities(h->game->ctx, &entity_count);
  double *numbers = xcalloc(entity_count, sizeof *numbers);
  qa_cleanup numbers_guard;
  qa_cleanup_push(&numbers_guard, free, numbers);
  for (size_t i = 0; i < entity_count; i++) numbers[i] = source_number(h, entities[i]->actor.id);
  frame->entities = xcalloc(state_count, sizeof *frame->entities);
  for (size_t i = 0; i < state_count; i++) {
    const qa_q2_entity *entity = NULL;
    for (size_t e = 0; e < entity_count; e++) if (numbers[e] == states[i].number) entity = entities[e];
    if (entity == NULL) continue;
    /* Own projectiles stop being solid for their owner alone, so the shared record is copied first. */
    qa_q2_wire_entity state = states[i];
    if (entity->has_owner && qa_same_actor(entity->owner, player.actor)) state.solid = 0;
    if (entity_visible(h, player, &state, entity, origin, &view)) frame->entities[frame->entity_count++] = state;
  }
  qa_cleanup_pop(&numbers_guard);
  free(numbers);
  qa_cleanup_pop(&guard);
  free(view.clusters);
}
/* playerState(player) */
static void player_state(q2_host *h, qa_actor_id actor, qa_q2_wire_player *state) {
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_app_sim_movement_player *movement = s->movement_player(s->ctx, actor);
  qa_q2_base_player_view view;
  bool has_view = s->q2_player_view(s->ctx, actor, &view);
  if (movement == NULL) qa_raise("Error", 0, 0, "Network player has no movement state");
  qa_q2_wire_player_init(state);
  const qa_movement_state *native = &movement->state;
  if (native->kind == QA_MOVEMENT_STATE_Q2) {
    const qa_q2_movement_state *q2 = &native->as.q2;
    state->pmove.pm_type = q2->type;
    for (int i = 0; i < 3; i++) {
      state->pmove.origin[i] = qa_js_to_int32(q2->origin_eighths.values[i]);
      state->pmove.velocity[i] = qa_js_to_int32(q2->velocity_eighths.values[i]);
      state->pmove.origin_f[i] = (float)(q2->origin_eighths.values[i] / 8);
      state->pmove.velocity_f[i] = (float)(q2->velocity_eighths.values[i] / 8);
      state->pmove.delta_angles[i] = qa_js_to_int16(q2->delta_angle_shorts.values[i]);
    }
    state->pmove.pm_flags = q2->flags;
    state->pmove.pm_time = q2->time_eight_milliseconds;
    state->pmove.gravity = q2->gravity;
  } else if (native->kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    const qa_q2_rerelease_movement_state *q2 = &native->as.q2_rerelease;
    state->pmove.pm_type = q2->type;
    vector(state->pmove.origin_f, q2->origin);
    vector(state->pmove.velocity_f, q2->velocity);
    vector(state->pmove.delta_angles_f, q2->delta_angles);
    state->pmove.delta_angle_float = true;
    double angles[3] = {q2->delta_angles.x, q2->delta_angles.y, q2->delta_angles.z};
    for (int i = 0; i < 3; i++) state->pmove.delta_angles[i] = qa_js_to_int16(qa_js_bit_and(trunc(angles[i] * 65536 / 360), 65535));
    state->pmove.pm_flags = q2->flags;
    state->pmove.pm_time = q2->time_milliseconds;
    state->pmove.gravity = q2->gravity;
    state->pmove.viewheight = q2->view_height;
  } else qa_raise("Error", 0, 0, "Native Q2 playerstate requires a Q2 movement provider");
  qa_vec3 zero = {0, 0, 0};
  qa_vec3 offset = {0, 0, movement->view_height};
  vector(state->viewangles, has_view ? view.angles : movement->view_angles);
  vector(state->viewoffset, has_view ? view.offset : offset);
  vector(state->kick_angles, has_view ? view.kick_angles : zero);
  vector(state->gunangles, has_view ? view.gun_angles : zero);
  vector(state->gunoffset, has_view ? view.gun_offset : zero);
  if (has_view) {
    state->blend[0] = (float)view.blend.x; state->blend[1] = (float)view.blend.y;
    state->blend[2] = (float)view.blend.z; state->blend[3] = (float)view.blend.w;
  }
  state->fov = has_view ? view.fov : 90;
  state->rdflags = has_view && view.underwater ? 1 : 0;
  qa_q2w_state *weapon = qa_q2_players_weapon_state(h->source->players, actor);
  if (weapon != NULL) {
    state->gunindex = weapon->weapon == NULL ? 0 : model(h, qa_q2w_weapons_definition(h->source->weapons, weapon->weapon)->view_model);
    state->gunframe = weapon->frame;
    state->gunrate = weapon->gun_rate;
  }
  qa_app_sim_player_ui ui = s->player_ui(s->ctx, actor);
  state->stats[0] = qa_js_to_int16(image(h, "i_health"));
  state->stats[1] = qa_js_to_int16(has_view ? view.health : ui.health);
  state->stats[3] = qa_js_to_int16(has_view ? view.ammo : ui.has_ammo ? ui.ammo_count : 0);
  state->stats[5] = qa_js_to_int16(has_view ? view.armor : ui.armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : ui.armor.regular.points);
  state->stats[9] = qa_js_to_int16(has_view && view.has_timer ? view.timer_seconds : 0);
  state->stats[11] = qa_js_to_int16(has_view && view.help_icon ? image(h, "i_help") : 0);
  state->stats[13] = qa_js_to_int16(has_view ? view.layouts : 0);
  state->stats[14] = qa_js_to_int16(has_view ? view.score : 0);
  state->stats[16] = qa_js_to_int16(!has_view || view.selected_item == NULL ? 0 : inventory_ordinal(h, view.selected_item));
  state->stats[17] = qa_js_to_int16(has_view && view.spectator ? 1 : 0);
}
static double print_level(const char *level) {
  return is(level, "chat") ? 3 : is(level, "high") ? 2 : is(level, "medium") ? 1 : 0;
}
static bool targets(qa_app_q2_player player, bool has_actor, qa_actor_id actor) {
  return !has_actor || qa_same_actor(actor, player.actor);
}
static void inventory_counts(q2_host *h, const qa_q2_player_event *event, qa_q2_server_event *out) {
  out->kind = QA_Q2_SERVER_INVENTORY;
  out->counts = xcalloc(256, sizeof *out->counts);
  out->count_count = 256;
  for (size_t i = 0; i < event->entry_count; i++) {
    double ordinal = inventory_ordinal(h, event->entries[i].item);
    if (ordinal > 0 && ordinal < 256) out->counts[(size_t)ordinal] = event->entries[i].count;
  }
}
/* TS configs.set(layout.playerSkins + event.slot, `${event.name}\\${event.skin}`). */
static void set_userinfo(q2_host *h, const qa_q2_player_event *event) {
  size_t name = strlen(event->name), skin = strlen(event->skin);
  char *value = xrealloc(NULL, name + skin + 2);
  memcpy(value, event->name, name);
  value[name] = '\\';
  memcpy(value + name + 1, event->skin, skin + 1);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, value);
  config_set(&h->configs, h->layout.player_skins + event->slot, value);
  qa_cleanup_pop(&guard);
  free(value);
}
static double event_entity(q2_host *h, const qa_app_sim_presentation_event *item, qa_actor_id actor) {
  return item->has_source_entity ? item->source_entity : source_number(h, actor);
}

/* ---- MVD capture --------------------------------------------------------------------- */
static void clear_capture(q2_host *h) {
  free(h->capture_configs); free(h->capture_players); free(h->capture_entities);
  for (size_t i = 0; i < h->capture_message_count; i++) free((void *)h->capture_messages[i].bytes);
  free(h->capture_messages); free(h->portal_bits); free(h->capture_gamedir);
  h->capture_configs = NULL; h->capture_players = NULL; h->capture_entities = NULL; h->capture_messages = NULL;
  h->capture_message_count = 0; h->portal_bits = NULL; h->capture_gamedir = NULL;
  memset(&h->capture, 0, sizeof h->capture);
}
typedef struct mvd_scope { q2_host *h; qa_q2_wire_codec *wire; size_t capacity; } mvd_scope;
static void emit(mvd_scope *m, qa_q2_server_event *event, qa_q2_mvd_recipient recipient, bool reliable) {
  q2_host *h = m->h;
  size_t length = 0;
  uint8_t *bytes = qa_q2_encode_server_event(m->wire, event, &length);
  qa_q2_server_event_clear(event);
  if (!bytes) qa_range_error("Out of memory");
  if (h->capture_message_count == m->capacity) {
    m->capacity = m->capacity ? m->capacity * 2 : 16;
    h->capture_messages = xrealloc(h->capture_messages, m->capacity * sizeof *h->capture_messages);
  }
  qa_q2_mvd_emission emission = {recipient, reliable, bytes, length};
  h->capture_messages[h->capture_message_count++] = emission;
}
static qa_q2_mvd_recipient target(q2_host *h, bool has_actor, qa_actor_id actor) {
  qa_q2_mvd_recipient recipient = {QA_Q2_MVD_RECIPIENT_ALL, 0, 0};
  if (has_actor) { recipient.kind = QA_Q2_MVD_RECIPIENT_PLAYER; recipient.number = source_number(h, actor) - 1; }
  return recipient;
}
static const void *host_mvd_capture(void *ctx, const void *output, const void *const *events, size_t count, double servercount) {
  (void)output; /* TS _output */
  q2_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  update_movement_configs(h);
  clear_capture(h);
  bool rerelease = h->game->options.edition == QA_Q2_EDITION_RERELEASE;
  qa_q2_protocol_identity protocol;
  memset(&protocol, 0, sizeof protocol);
  protocol.kind = rerelease ? "q2-rerelease" : "q2-classic";
  protocol.version = rerelease ? 1038 : 34;
  qa_q2_wire_codec wire;
  if (!qa_q2_wire_codec_init(&wire, protocol)) qa_range_error("Out of memory");
  qa_cleanup wire_guard;
  qa_cleanup_push(&wire_guard, (void (*)(void *))qa_q2_wire_codec_destroy, &wire);
  size_t state_count = qa_q2_players_state_count(h->source->players);
  h->capture_players = xcalloc(state_count, sizeof *h->capture_players);
  size_t players = 0;
  for (size_t i = 0; i < state_count; i++) {
    qa_actor_id actor; qa_q2_base_player_state *state;
    if (!qa_q2_players_state_at(h->source->players, i, &actor, &state)) continue;
    if (!state->connected || s->movement_player(s->ctx, actor) == NULL) continue;
    double number = source_number(h, actor) - 1;
    qa_q2_wire_player value;
    player_state(h, actor, &value);
    size_t at = 0; /* players.set(number, ...): a repeated number keeps its position */
    while (at < players && h->capture_players[at].number != number) at++;
    h->capture_players[at].number = number; h->capture_players[at].player = value;
    if (at == players) players++;
  }
  size_t entity_count = 0;
  h->capture_entities = entity_states(h, protocol, &entity_count);
  const qa_decoded_world *geometry = &s->scene->geometry;
  if (geometry->kind != QA_DECODED_WORLD_Q2) qa_raise("Error", 0, 0, "MVD source capture requires Quake II portal topology");
  double max_portal = -1;
  for (size_t i = 0; i < geometry->as.q2->area_portal_count; i++) max_portal = qa_js_max(max_portal, geometry->as.q2->area_portals[i].portal);
  size_t bits_length = (size_t)ceil((max_portal + 1) / 8);
  h->portal_bits = xcalloc(bits_length, 1);
  size_t portal_count = 0;
  const double *portals = qa_shared_q2_portal_state(s->scene, &portal_count);
  for (size_t i = 0; i < portal_count; i++) {
    uint32_t index = qa_js_to_uint32(portals[i]) >> 3;
    /* Typed-array stores past the end are dropped. */
    if (index < bits_length) h->portal_bits[index] = qa_js_to_uint8(qa_js_bit_or(h->portal_bits[index], qa_js_shl(1, qa_js_bit_and(portals[i], 7))));
  }
  mvd_scope m = {h, &wire, 0};
  qa_q2_mvd_recipient all = {QA_Q2_MVD_RECIPIENT_ALL, 0, 0};
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *item = events[i];
    if (is(item->kind, "q2-player")) {
      const qa_q2_player_event *event = item->q2_player;
      qa_q2_server_event out; memset(&out, 0, sizeof out);
      if (is(event->kind, "print")) {
        out.kind = QA_Q2_SERVER_PRINT; out.level = print_level(event->level); out.text = xstrdup(event->text);
        emit(&m, &out, target(h, event->has_target, event->target), true);
      } else if (is(event->kind, "stufftext")) {
        out.kind = QA_Q2_SERVER_COMMAND_TEXT; out.text = xstrdup(event->text);
        emit(&m, &out, target(h, true, event->actor), true);
      } else if (is(event->kind, "inventory")) {
        inventory_counts(h, event, &out);
        emit(&m, &out, target(h, true, event->actor), true);
      }
    } else if (is(item->kind, "q2")) {
      const qa_q2_presentation_event *event = item->q2;
      qa_q2_server_event out; memset(&out, 0, sizeof out);
      if (is(event->kind, "print")) {
        out.kind = QA_Q2_SERVER_PRINT; out.level = print_level(event->level); out.text = xstrdup(event->text);
        emit(&m, &out, target(h, event->has_actor, event->actor), true);
      } else if (is(event->kind, "centerprint")) {
        out.kind = QA_Q2_SERVER_CENTER_PRINT; out.text = xstrdup(event->text);
        emit(&m, &out, target(h, event->has_actor, event->actor), true);
      } else if (is(event->kind, "effect")) {
        /* Presentation effects currently retain no native multicast scope; preserve existing server routing. */
        if (qa_app_q2_effect_to_wire(event, &out.temp)) { out.kind = QA_Q2_SERVER_TEMPORARY_ENTITY; emit(&m, &out, all, false); }
      } else if (is(event->kind, "sound") && is(event->loop, "once") && !event->local) {
        qa_q2_mvd_recipient recipient = all;
        if (!(event->attenuation == 0 || qa_js_bit_and(event->channel, 8) != 0)) {
          recipient.kind = QA_Q2_MVD_RECIPIENT_PHS;
          recipient.leaf = qa_shared_point_leaf(s->scene, event->origin);
        }
        out.kind = QA_Q2_SERVER_SOUND;
        out.sound.flags = 0;
        out.sound.index = sound(h, event->path);
        out.sound.entity = !event->has_actor ? 0 : event_entity(h, item, event->actor);
        out.sound.channel = qa_js_bit_and(event->channel, 7);
        out.sound.has_position = true; out.sound.position = event->origin;
        out.sound.volume = event->volume; out.sound.attenuation = event->attenuation; out.sound.delay_seconds = 0;
        emit(&m, &out, recipient, event->reliable || qa_js_bit_and(event->channel, 16) != 0);
      } else if (is(event->kind, "monster-muzzleflash")) {
        double entity = event_entity(h, item, event->actor);
        qa_body_state_result body = body_of(h, event->actor);
        if (body.has_value) {
          out.kind = QA_Q2_SERVER_MUZZLE_FLASH; out.flash_entity = entity; out.flash = event->flash; out.monster = true; out.silenced = false;
          qa_q2_mvd_recipient pvs = {QA_Q2_MVD_RECIPIENT_PVS, 0, qa_shared_point_leaf(s->scene, body.value.origin)};
          emit(&m, &out, pvs, false);
        }
      }
    } else if (is(item->kind, "q2-weapon") && item->q2_weapon->kind == QA_Q2W_EVENT_MUZZLEFLASH) {
      const qa_q2w_event *event = item->q2_weapon;
      qa_body_state_result body = body_of(h, *event->actor);
      if (body.has_value) {
        qa_q2_server_event out; memset(&out, 0, sizeof out);
        out.kind = QA_Q2_SERVER_MUZZLE_FLASH; out.flash_entity = event_entity(h, item, *event->actor);
        out.flash = event->flash; out.monster = false; out.silenced = event->silenced;
        qa_q2_mvd_recipient pvs = {QA_Q2_MVD_RECIPIENT_PVS, 0, qa_shared_point_leaf(s->scene, body.value.origin)};
        emit(&m, &out, pvs, false);
      }
    }
  }
  qa_cleanup_pop(&wire_guard);
  qa_q2_wire_codec_destroy(&wire);
  h->capture_configs = xcalloc(h->configs.count, sizeof *h->capture_configs);
  for (size_t i = 0; i < h->configs.count; i++) {
    h->capture_configs[i].index = h->configs.order[i];
    h->capture_configs[i].value = h->configs.values[(size_t)h->configs.order[i]];
  }
  h->capture_gamedir = xstrdup(qa_app_sim_network_gamedir(
      h->options.content->content_directory(h->options.content->ctx, s->recipe->map.entities.content)));
  h->capture.revision = rerelease ? 3038 : 2010;
  h->capture.flags = 0;
  h->capture.servercount = servercount;
  h->capture.gamedir = h->capture_gamedir;
  h->capture.dummy = -1;
  h->capture.config_strings = h->capture_configs; h->capture.config_string_count = h->configs.count;
  h->capture.portal_bits = h->portal_bits; h->capture.portal_bits_length = bits_length;
  h->capture.players = h->capture_players; h->capture.player_count = players;
  h->capture.entities = h->capture_entities; h->capture.entity_count = entity_count;
  h->capture.messages = h->capture_messages; h->capture.message_count = h->capture_message_count;
  return &h->capture;
}
static bool host_mvd_settings(void *ctx, bool *enabled, double *max_viewers, const char **password) {
  q2_host *h = ctx;
  *enabled = qa_cvar_registry_variable_value(h->cvars, "sv_mvd_enable") != 0;
  *max_viewers = qa_js_max(1, qa_js_min(256, trunc(qa_cvar_registry_variable_value(h->cvars, "sv_mvd_maxclients"))));
  if (password == NULL) return true; /* configureMvd reads only enabled and maxViewers */
  static _Thread_local char *value;
  free(value);
  value = qa_cvar_registry_variable_string(h->cvars, "sv_mvd_password");
  *password = value;
  return true;
}

/* ---- host members -------------------------------------------------------------------- */
static bool host_rejects(void *ctx, const void *address) { q2_host *h = ctx; return h->options.rejects(h->options.rejects_ctx, address); }
static const void *const *host_masters(void *ctx, size_t *out_count) {
  q2_host *h = ctx;
  *out_count = h->options.master_count;
  return h->options.masters;
}
static qa_q2_status discovery_status(void *ctx) {
  q2_host *h = ctx;
  free(h->server_info); free(h->status_players);
  h->server_info = qa_cvar_registry_info_string(h->cvars, QA_Q2_CVAR_FLAG_SERVER_INFO, -1);
  size_t state_count = qa_q2_players_state_count(h->source->players);
  h->status_players = xcalloc(state_count, sizeof *h->status_players);
  h->status_player_count = 0;
  for (size_t i = 0; i < state_count; i++) {
    qa_actor_id actor; qa_q2_base_player_state *state;
    if (!qa_q2_players_state_at(h->source->players, i, &actor, &state) || !state->connected) continue;
    qa_q2_status_player *row = &h->status_players[h->status_player_count++];
    row->name = state->name; row->score = state->score; row->ping = state->ping;
  }
  qa_q2_status status = {h->server_info, h->status_players, h->status_player_count};
  return status;
}
static qa_q2_host_info discovery_info(void *ctx) {
  q2_host *h = ctx;
  free(h->hostname);
  h->hostname = qa_cvar_registry_variable_string(h->cvars, "hostname");
  double players = 0;
  size_t state_count = qa_q2_players_state_count(h->source->players);
  for (size_t i = 0; i < state_count; i++) {
    qa_actor_id actor; qa_q2_base_player_state *state;
    if (qa_q2_players_state_at(h->source->players, i, &actor, &state) && state->connected) players++;
  }
  qa_q2_host_info info = {h->hostname, h->game->options.map_name, players, h->game->options.max_clients};
  return info;
}
static void host_observe(void *ctx, const void *output_value, const void *const *events_value, size_t count) {
  q2_host *h = ctx;
  const qa_simulation_output *output = output_value;
  const qa_app_sim_presentation_event *const *events = (const qa_app_sim_presentation_event *const *)events_value;
  for (size_t i = 0; i < h->command_count; i++) free(h->commands[i]);
  h->command_count = 0;
  if (h->event_frame != output->snapshot.frame.frame) {
    h->entity_event_count = 0;
    h->event_frame = output->snapshot.frame.frame;
  }
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *item = events[i];
    if (is(item->kind, "q2")) {
      const qa_q2_presentation_event *event = item->q2;
      if (is(event->kind, "lightstyle")) config_set(&h->configs, h->layout.lights + event->style, event->pattern);
      else if (is(event->kind, "entity-event")) {
        size_t at = 0;
        while (at < h->entity_event_count && !qa_same_actor(h->entity_events[at].actor, event->actor)) at++;
        if (at == h->entity_event_count) {
          h->entity_events = xrealloc(h->entity_events, (h->entity_event_count + 1) * sizeof *h->entity_events);
          h->entity_event_count++;
        }
        h->entity_events[at].actor = event->actor; h->entity_events[at].value = event->event;
      } else if (is(event->kind, "sound")) sound(h, event->path);
      else if (is(event->kind, "model")) {
        model(h, event->path);
        for (int m = 0; m < 3; m++) if (event->attached_models[m]) model(h, event->attached_models[m]);
      }
    } else if (is(item->kind, "q2-player")) {
      const qa_q2_player_event *event = item->q2_player;
      if (is(event->kind, "userinfo")) set_userinfo(h, event);
    }
  }
}
static const void *host_supports_source_wire(void *ctx) {
  q2_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_wire_admission_destroy(&h->support);
  const char *reasons[4]; size_t count = 0;
  const char *kind = h->options.protocol.kind;
  if (strncmp(s->recipe->movement.provider, "q2:", 3) != 0) reasons[count++] = "Selected movement requires unified peer serialization";
  if (strncmp(s->recipe->character.definition.provider, "q2:", 3) != 0) reasons[count++] = "Selected character requires unified peer serialization";
  if (is(kind, "q2-kex-demo")) reasons[count++] = "KEX native live transport is not bound";
  bool classic = h->game->options.edition == QA_Q2_EDITION_CLASSIC;
  if ((classic && (is(kind, "q2-rerelease") || is(kind, "q2-kex") || is(kind, "q2-kex-demo")))
      || (!classic && !is(kind, "q2-rerelease") && !is(kind, "q2-kex")))
    reasons[count++] = "Selected application game API and native message layout differ";
  h->support.supported = count == 0;
  h->support.reasons = count ? xcalloc(count, sizeof *h->support.reasons) : NULL;
  for (size_t i = 0; i < count; i++) h->support.reasons[i] = xstrdup(reasons[i]);
  h->support.reason_count = count;
  return &h->support;
}
static bool slot_taken(q2_host *h, double slot) {
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
typedef struct admit_scope {
  q2_host *h; qa_client_id client; const char *userinfo; double slot;
  bool has_actor; qa_actor_id actor; qa_app_q2_player player;
} admit_scope;
static void admit_body(void *ctx) {
  admit_scope *a = ctx;
  q2_host *h = a->h;
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_app_sim_player_admission admitted = s->admit_player(s->ctx, a->client);
  qa_q2_entity *entity = qa_q2_game_entity_of(h->game, &admitted.actor);
  a->has_actor = true; a->actor = admitted.actor;
  if (entity == NULL) qa_raise("Error", 0, 0, "Admitted Q2 player has no source entity");
  qa_q2_players_userinfo_changed(h->source->players, entity, h->game, a->userinfo);
  qa_app_q2_player player = {a->client, admitted.actor, source_number(h, admitted.actor)};
  per_player *entry = player_slot(h, a->slot);
  entry->used = true; entry->player = player;
  a->player = player;
}
static qa_app_q2_admission host_admit(void *ctx, const void *from_value, const void *request_value) {
  q2_host *h = ctx;
  const qa_net_address *from = from_value;
  const qa_q2_connect_request *request = request_value;
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_app_q2_admission result;
  memset(&result, 0, sizeof result);
  qa_app_sim_info pairs;
  qa_app_sim_info_parse_q2(&pairs, request->userinfo);
  if (is(request->protocol.kind, "q2-kex")) qa_app_sim_info_fold_seat(&pairs, request->has_split_seat ? request->split_seat : 0);
  char *ip = qa_app_sim_address_key(from);
  qa_app_sim_info_set(&pairs, "ip", ip);
  free(ip);
  char *userinfo = qa_app_sim_info_text(&pairs);
  qa_app_sim_info_clear(&pairs);
  const char *social_id = request->has_social_ids && request->social_id_count > 0 ? request->social_ids[0] : "";
  static _Thread_local char reason[300];
  if (social_id[0] != '\0' && h->options.player_identity == NULL) {
    free(userinfo);
    result.reason = "Server has no rerelease social identity owner";
    return result;
  }
  qa_q2_connection_result allowed = qa_q2_players_connect(h->source->players, h->game, userinfo);
  free(userinfo);
  if (!allowed.allowed) { snprintf(reason, sizeof reason, "%s", allowed.reason); result.reason = reason; return result; }
  double slot = 0;
  for (; slot < h->game->options.max_clients; slot++) if (!slot_taken(h, slot)) break;
  if (slot == h->game->options.max_clients) { result.reason = "Server is full"; return result; }
  qa_session_client *client = qa_engine_session_create_client(h->options.session, slot);
  qa_session_client_connect(client, from->kind == QA_NET_LOOPBACK ? "loopback" : "remote");
  qa_client_id id = qa_session_client_id(client);
  if (h->options.player_identity)
    h->options.player_identity(h->options.player_identity_ctx, id, true, request->has_split_seat ? request->split_seat : 0, social_id);
  admit_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.h = h; scope.client = id; scope.userinfo = allowed.userinfo; scope.slot = slot;
  qa_caught_error error;
  if (qa_try(admit_body, &scope, &error)) { result.accepted = true; result.player = scope.player; return result; }
  if (scope.has_actor) s->disconnect_player(s->ctx, scope.actor);
  if (h->options.player_identity) h->options.player_identity(h->options.player_identity_ctx, id, false, 0, NULL);
  qa_engine_session_close_client(h->options.session, id);
  qa_rethrow(&error);
}
static void host_disconnect(void *ctx, qa_app_q2_player player, const char *reason) {
  (void)reason; /* TS _reason */
  q2_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  s->disconnect_player(s->ctx, player.actor);
  if (h->options.player_identity) h->options.player_identity(h->options.player_identity_ctx, player.client, false, 0, NULL);
  qa_engine_session_close_client(h->options.session, player.client);
  per_player *entry = player_slot(h, (double)player.client.slot);
  entry->used = false;
  if (entry->has_known) config_clear(&entry->known);
  entry->has_known = false;
}
static qa_app_q2_player host_carried_player(void *ctx, qa_client_id client) {
  q2_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t count = 0;
  const qa_actor_id *players = s->players(s->ctx, &count);
  const qa_actor_id *found = NULL;
  for (size_t i = 0; i < count && found == NULL; i++) {
    qa_app_sim_movement_player *movement = s->movement_player(s->ctx, players[i]);
    if (movement != NULL && qa_same_client(movement->client, client)) found = &players[i];
  }
  if (found == NULL) qa_raise("Error", 0, 0, "Application has not admitted carried Q2 network client");
  qa_app_q2_player player = {client, *found, source_number(h, *found)};
  per_player *entry = player_slot(h, (double)client.slot);
  entry->used = true; entry->player = player;
  return player;
}
static qa_app_q2_game_state host_game_state(void *ctx, qa_app_q2_player player, const qa_q2_protocol_identity *protocol_value) {
  q2_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_q2_protocol_identity protocol = protocol_value ? *protocol_value : h->options.protocol;
  update_movement_configs(h);
  size_t entity_count = 0;
  qa_q2_wire_entity *entities = entity_states(h, protocol, &entity_count);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, entities);
  size_t state_count = qa_q2_players_state_count(h->source->players);
  for (size_t i = 0; i < state_count; i++) {
    qa_actor_id actor; qa_q2_base_player_state *state;
    if (!qa_q2_players_state_at(h->source->players, i, &actor, &state)) continue;
    char skin[4200];
    snprintf(skin, sizeof skin, "%s\\%s", state->name, state->skin);
    config_set(&h->configs, h->layout.player_skins + state->slot, skin);
  }
  per_player *entry = player_slot(h, (double)player.client.slot);
  if (entry->has_known) config_clear(&entry->known);
  config_copy(&entry->known, &h->configs);
  entry->has_known = true;
  clear_state(entry);
  memset(&entry->data, 0, sizeof entry->data);
  entry->data.servercount = 1;
  entry->data.attractloop = false;
  snprintf(entry->data.gamedir, sizeof entry->data.gamedir, "%s", qa_app_sim_network_gamedir(
      h->options.content->content_directory(h->options.content->ctx, s->recipe->map.entities.content)));
  entry->data.clientnum = player.source_entity - 1;
  snprintf(entry->data.levelname, sizeof entry->data.levelname, "%s", text_or_empty(config_get(&h->configs, 0)));
  entry->data.server_state = 2;
  if (is(protocol.kind, "q2-r1q2")) {
    entry->data.has_r1q2_version = true; entry->data.r1q2_version = (double)protocol.revision;
    entry->data.has_r1q2_strafejump_hack = true; entry->data.r1q2_strafejump_hack = false;
  } else if (is(protocol.kind, "q2-q2pro")) {
    entry->data.has_q2pro_version = true; entry->data.q2pro_version = (double)protocol.revision;
    entry->data.has_wire_flags = true; entry->data.wire_flags = 0;
  }
  entry->data.has_server_fps = true;
  entry->data.server_fps = h->game->options.edition == QA_Q2_EDITION_RERELEASE ? 40 : 10;
  entry->configs.entries = xcalloc(h->configs.count, sizeof *entry->configs.entries);
  for (size_t i = 0; i < h->configs.count; i++) {
    entry->configs.entries[i].index = h->configs.order[i];
    entry->configs.entries[i].value = xstrdup(h->configs.values[(size_t)h->configs.order[i]]);
    entry->configs.count = i + 1;
  }
  memset(&entry->baselines, 0, sizeof entry->baselines);
  for (size_t i = 0; i < entity_count; i++)
    if (!qa_q2_baseline_list_set(&entry->baselines, entities[i].number, &entities[i])) qa_range_error("Out of memory");
  entry->has_state = true;
  qa_cleanup_pop(&guard);
  free(entities);
  qa_app_q2_game_state result = {&entry->data, &entry->configs, &entry->baselines};
  return result;
}
static const void *host_frame(void *ctx, qa_app_q2_player player, const void *output_value, const qa_q2_protocol_identity *protocol_value) {
  q2_host *h = ctx;
  const qa_simulation_output *output = output_value;
  qa_shared_scene_queries *scene = h->simulation->scene;
  qa_q2_protocol_identity protocol = protocol_value ? *protocol_value : h->options.protocol;
  qa_body_state_result body = body_of(h, player.actor);
  if (!body.has_value) qa_raise("Error", 0, 0, "Network player body disappeared");
  qa_q2_wire_frame frame;
  memset(&frame, 0, sizeof frame);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_q2_wire_frame_free, &frame);
  player_state(h, player.actor, &frame.player);
  qa_vec3 origin = {body.value.origin.x + frame.player.viewoffset[0], body.value.origin.y + frame.player.viewoffset[1],
      body.value.origin.z + frame.player.viewoffset[2]};
  frame.server_frame = output->snapshot.frame.frame;
  frame.delta_frame = -1;
  frame.suppressed_count = 0;
  size_t bits_length = 0;
  const uint8_t *bits = qa_shared_area_bits(scene, qa_shared_leaf_area(scene, qa_shared_point_leaf(scene, origin)), &bits_length);
  frame.area_bits = xcalloc(bits_length, 1);
  if (bits_length) memcpy(frame.area_bits, bits, bits_length);
  frame.area_bits_length = bits_length;
  size_t state_count = 0;
  const qa_q2_wire_entity *states = frame_entity_states(h, protocol, output_value, &state_count);
  visible_entities(h, player, states, state_count, origin, &frame);
  qa_cleanup_pop(&guard);
  per_player *entry = player_slot(h, (double)player.client.slot);
  if (entry->has_frame) qa_q2_wire_frame_free(&entry->frame);
  entry->frame = frame; entry->has_frame = true;
  return &entry->frame;
}
/* SV_Multicast: the listener's cluster must see the origin's and the areas must be connected. */
static bool reaches(q2_host *h, qa_app_q2_player player, qa_vec3 origin, const char *kind) {
  qa_shared_scene_queries *scene = h->simulation->scene;
  qa_body_state_result body = body_of(h, player.actor);
  if (!body.has_value) return true;
  double here = qa_shared_point_leaf(scene, body.value.origin), there = qa_shared_point_leaf(scene, origin);
  return qa_shared_areas_connected(scene, qa_shared_leaf_area(scene, here), qa_shared_leaf_area(scene, there))
      && qa_shared_cluster_visible(scene, qa_shared_leaf_cluster(scene, here), qa_shared_leaf_cluster(scene, there), kind);
}
static bool flash_reaches(q2_host *h, qa_app_q2_player player, qa_actor_id actor) {
  qa_body_state_result body = body_of(h, actor);
  return !body.has_value || reaches(h, player, body.value.origin, "pvs");
}
static const qa_app_q2_server_event *host_events(void *ctx, qa_app_q2_player player, const void *output,
    const void *const *events_value, size_t count, size_t *out_count) {
  (void)output; /* TS _output */
  q2_host *h = ctx;
  const qa_app_sim_presentation_event *const *events = (const qa_app_sim_presentation_event *const *)events_value;
  update_movement_configs(h);
  owned_events messages;
  memset(&messages, 0, sizeof messages);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))clear_events, &messages);
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *item = events[i];
    if (is(item->kind, "q2-player")) {
      const qa_q2_player_event *event = item->q2_player;
      if (is(event->kind, "print") && targets(player, event->has_target, event->target)) {
        qa_q2_server_event *out = push_event(&messages, false, false);
        out->kind = QA_Q2_SERVER_PRINT; out->level = print_level(event->level); out->text = xstrdup(event->text);
      } else if (is(event->kind, "stufftext") && targets(player, true, event->actor)) {
        qa_q2_server_event *out = push_event(&messages, false, false);
        out->kind = QA_Q2_SERVER_COMMAND_TEXT; out->text = xstrdup(event->text);
      } else if (is(event->kind, "userinfo")) set_userinfo(h, event);
      else if (is(event->kind, "inventory") && targets(player, true, event->actor))
        inventory_counts(h, event, push_event(&messages, false, false));
    } else if (is(item->kind, "q2")) {
      const qa_q2_presentation_event *event = item->q2;
      if (is(event->kind, "print") && targets(player, event->has_actor, event->actor)) {
        qa_q2_server_event *out = push_event(&messages, false, false);
        out->kind = QA_Q2_SERVER_PRINT; out->level = print_level(event->level); out->text = xstrdup(event->text);
      } else if (is(event->kind, "centerprint") && targets(player, event->has_actor, event->actor)) {
        qa_q2_server_event *out = push_event(&messages, false, false);
        out->kind = QA_Q2_SERVER_CENTER_PRINT; out->text = xstrdup(event->text);
      } else if (is(event->kind, "lightstyle")) config_set(&h->configs, h->layout.lights + event->style, event->pattern);
      else if (is(event->kind, "effect")) {
        qa_q2_temp_entity value;
        memset(&value, 0, sizeof value);
        if (qa_app_q2_effect_to_wire(event, &value) && reaches(h, player, event->origin, "phs")) {
          qa_q2_server_event *out = push_event(&messages, false, false);
          out->kind = QA_Q2_SERVER_TEMPORARY_ENTITY; out->temp = value;
        } else qa_q2_temp_entity_free(&value);
      } else if (is(event->kind, "sound") && is(event->loop, "once")
          && (!event->local || targets(player, event->has_actor, event->actor))
          && (event->attenuation == 0 || qa_js_bit_and(event->channel, 8) != 0 || reaches(h, player, event->origin, "phs"))) {
        double entity = !event->has_actor ? 0 : event_entity(h, item, event->actor);
        qa_q2_server_event *out = push_event(&messages, true, event->reliable);
        out->kind = QA_Q2_SERVER_SOUND;
        out->sound.flags = 0; out->sound.index = sound(h, event->path); out->sound.entity = entity;
        out->sound.channel = event->channel; out->sound.has_position = true; out->sound.position = event->origin;
        out->sound.volume = event->volume; out->sound.attenuation = event->attenuation; out->sound.delay_seconds = 0;
      } else if (is(event->kind, "monster-muzzleflash") && flash_reaches(h, player, event->actor)) {
        double entity = event_entity(h, item, event->actor);
        qa_q2_server_event *out = push_event(&messages, false, false);
        out->kind = QA_Q2_SERVER_MUZZLE_FLASH; out->flash_entity = entity; out->flash = event->flash;
        out->monster = true; out->silenced = false;
      }
    } else if (is(item->kind, "q2-weapon")) {
      const qa_q2w_event *event = item->q2_weapon;
      if (event->kind == QA_Q2W_EVENT_MUZZLEFLASH && flash_reaches(h, player, *event->actor)) {
        double entity = event_entity(h, item, *event->actor);
        qa_q2_server_event *out = push_event(&messages, false, false);
        out->kind = QA_Q2_SERVER_MUZZLE_FLASH; out->flash_entity = entity; out->flash = event->flash;
        out->monster = false; out->silenced = event->silenced;
      }
    }
  }
  per_player *entry = player_slot(h, (double)player.client.slot);
  if (!entry->has_known) qa_raise("Error", 0, 0, "Q2 client has no configstring history");
  owned_events result;
  memset(&result, 0, sizeof result);
  qa_cleanup result_guard;
  qa_cleanup_push(&result_guard, (void (*)(void *))clear_events, &result);
  for (size_t i = 0; i < h->configs.count; i++) {
    double index = h->configs.order[i];
    const char *value = h->configs.values[(size_t)index];
    if (is(config_get(&entry->known, index), value)) continue;
    qa_q2_server_event *out = push_event(&result, false, false);
    out->kind = QA_Q2_SERVER_CONFIG_STRING; out->index = index; out->text = xstrdup(value);
    config_set(&entry->known, index, value);
  }
  for (size_t i = 0; i < messages.count; i++) {
    qa_q2_server_event *out = push_event(&result, messages.wrapped[i].has_reliable, messages.wrapped[i].reliable);
    *out = messages.events[i];
  }
  free(messages.events); free(messages.wrapped);
  qa_cleanup_pop(&result_guard);
  qa_cleanup_pop(&guard);
  for (size_t i = 0; i < result.count; i++) result.wrapped[i].event = &result.events[i];
  clear_events(&entry->events);
  entry->events = result;
  *out_count = result.count;
  return result.wrapped;
}
static const void *host_input(void *ctx, qa_app_q2_player player, const void *command_value, double sequence) {
  q2_host *h = ctx;
  const qa_q2_wire_usercmd *command = command_value;
  qa_actor_command *out = xcalloc(1, sizeof *out);
  out->actor = player.actor;
  out->source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT; out->source.client = player.client;
  out->sequence = sequence;
  if (h->game->options.edition == QA_Q2_EDITION_CLASSIC) {
    out->command.kind = QA_USER_COMMAND_Q2;
    qa_q2_to_command(command, &out->command.as.q2);
  } else {
    out->command.kind = QA_USER_COMMAND_Q2_RERELEASE;
    qa_q2_to_rerelease_command(command, is(h->options.protocol.kind, "q2-kex") ? command->server_frame : sequence,
        &out->command.as.q2_rerelease);
  }
  h->commands = xrealloc(h->commands, (h->command_count + 1) * sizeof *h->commands);
  h->commands[h->command_count++] = out;
  return out;
}
static const char *variable(void *ctx, const char *name) {
  q2_host *h = ctx;
  static _Thread_local char *value;
  free(value);
  value = qa_cvar_registry_variable_string(h->cvars, name);
  return value;
}
static void print_callback(void *ctx, const char *text) { print_to(ctx, text); }
static const char *host_expand_client_command(void *ctx, const char *text) {
  char *out = NULL;
  if (!qa_expand_command_macros(text, variable, ctx, print_callback, ctx, QA_COMMAND_TEXT_SOURCE, &out)) return NULL;
  if (!out) qa_range_error("Out of memory");
  return out;
}
static void host_command(void *ctx, qa_app_q2_player player, const char *name, const char *const *args, size_t count) {
  q2_host *h = ctx;
  qa_q2_entity *entity = qa_q2_game_entity_of(h->game, &player.actor);
  if (entity == NULL) qa_raise("Error", 0, 0, "Q2 command has no source player");
  qa_q2_players_client_command(h->source->players, entity, h->game, name, args, count);
}
static void host_userinfo(void *ctx, qa_app_q2_player player, const char *value) {
  q2_host *h = ctx;
  qa_q2_entity *entity = qa_q2_game_entity_of(h->game, &player.actor);
  if (entity == NULL) qa_raise("Error", 0, 0, "Q2 userinfo has no source entity");
  qa_q2_players_userinfo_changed(h->source->players, entity, h->game, value);
  h->simulation->notify_client_event(h->simulation->ctx, "userinfo", player.actor);
}
static void host_print(void *ctx, const char *text) { print_to(ctx, text); }

qa_app_q2_server_host qa_app_sim_create_q2_application_server_host(qa_app_sim_q2_server_binding_options options) {
  const qa_app_sim_network_simulation *s = options.simulation;
  qa_q2_product_runtime *source = s->q2_source(s->ctx);
  if (source == NULL) qa_raise("Error", 0, 0, "Q2 server network host requires the Q2 source game provider");
  qa_cvar_registry *cvars = s->q2_server_cvars(s->ctx);
  if (cvars == NULL) qa_raise("Error", 0, 0, "Q2 server network host requires the source cvar registry");
  qa_q2_game_services *game = source->services;
  qa_cvar_snapshot snapshot;
  qa_cvar_registry_register(cvars, "sv_mvd_enable", "0", QA_Q2_CVAR_FLAG_LATCH, &snapshot); qa_cvar_read_free(&snapshot);
  qa_cvar_registry_register(cvars, "sv_mvd_maxclients", "8", QA_Q2_CVAR_FLAG_LATCH, &snapshot); qa_cvar_read_free(&snapshot);
  qa_cvar_registry_register(cvars, "sv_mvd_password", "", QA_Q2_CVAR_FLAG_PRIVATE, &snapshot); qa_cvar_read_free(&snapshot);
  qa_cvar_registry_register(cvars, "hostname", "noname", QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_ARCHIVE, &snapshot); qa_cvar_read_free(&snapshot);
  char protocol_text[40], max_clients_text[40];
  snprintf(protocol_text, sizeof protocol_text, "%s", QA_JS_NUMBER((double)options.protocol.version));
  snprintf(max_clients_text, sizeof max_clients_text, "%s", QA_JS_NUMBER(game->options.max_clients));
  const char *names[3] = {"protocol", "mapname", "maxclients"};
  const char *values[3] = {protocol_text, game->options.map_name, max_clients_text};
  for (int i = 0; i < 3; i++) {
    qa_cvar_registry_register(cvars, names[i], values[i],
        QA_Q2_CVAR_FLAG_SERVER_INFO | (i == 2 ? QA_Q2_CVAR_FLAG_LATCH : QA_Q2_CVAR_FLAG_NO_SET), &snapshot); qa_cvar_read_free(&snapshot);
    qa_cvar_registry_set(cvars, names[i], values[i], true, &snapshot); qa_cvar_read_free(&snapshot);
  }
  q2_host *h = xcalloc(1, sizeof *h);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, h);
  h->options = options; h->simulation = s; h->source = source; h->game = game; h->cvars = cvars;
  h->event_frame = -1;
  const qa_executable_recipe *recipe = options.content->recipe;
  h->downloads = qa_app_create_q2_application_downloads(options.content->for_content(options.content->ctx, recipe->map.entities.content),
      cvars, game->options.edition == QA_Q2_EDITION_RERELEASE);
  if (!h->downloads) qa_range_error("Out of memory");
  h->layout = qa_app_q2_application_layout(options.protocol);
  size_t geometry_length = 0;
  uint8_t *geometry = options.content->read(options.content->ctx, &recipe->map.geometry, &geometry_length);
  uint32_t checksum = qa_block_checksum(geometry, geometry_length);
  free(geometry);
  size_t entity_count = 0;
  qa_q2_entity *const *entities = game->entities(game->ctx, &entity_count);
  const qa_q2_entity *world = NULL;
  for (size_t i = 0; i < entity_count && world == NULL; i++) if (is(entities[i]->classname, "worldspawn")) world = entities[i];
  config_set(&h->configs, 0, world && world->message && world->message[0] ? world->message : game->options.map_name);
  const char *sky = world ? spawn_value(world, "sky") : NULL, *axis = world ? spawn_value(world, "skyaxis") : NULL;
  const char *rotate = world ? spawn_value(world, "skyrotate") : NULL;
  config_set(&h->configs, 2, sky ? sky : "unit1_");
  config_set(&h->configs, 3, axis ? axis : "0 0 0");
  config_set(&h->configs, 4, rotate ? rotate : "0");
  config_set(&h->configs, h->layout.map_checksum, QA_JS_NUMBER((double)checksum));
  config_set(&h->configs, h->layout.max_clients, QA_JS_NUMBER(game->options.max_clients));
  update_movement_configs(h);
  model(h, recipe->map.geometry.requested_path);
  for (size_t i = 0; i < entity_count; i++) {
    const qa_q2_entity *entity = entities[i];
    model(h, entity->model); model(h, entity->model2); model(h, entity->model3); model(h, entity->model4);
    if (entity->sound && entity->sound[0] != '\0') sound(h, entity->sound);
  }
  for (size_t i = 0; i < qa_q2w_base_weapon_count; i++) {
    model(h, qa_q2w_base_weapons[i].view_model);
    model(h, qa_q2w_base_weapons[i].world_model);
  }
  image(h, "i_health");
  size_t item_count = 0;
  const qa_q2_inventory_item *items = qa_q2_item_module_list(source->items, &item_count);
  for (size_t i = 0; i < item_count; i++) config_set(&h->configs, h->layout.items + (double)i + 1, items[i].name);
  h->message_options.max_config_strings = h->layout.max_config_strings;
  h->message_options.inventory_slots = 256;
  h->discovery.ctx = h; h->discovery.status = discovery_status; h->discovery.info = discovery_info;
  qa_cleanup_pop(&guard);
  qa_app_q2_server_host host;
  memset(&host, 0, sizeof host);
  host.ctx = h;
  host.downloads = h->downloads;
  if (options.rejects) host.rejects = host_rejects;
  host.mvd_capture = host_mvd_capture;
  host.mvd_settings = host_mvd_settings;
  host.administration = options.administration;
  if (options.masters) host.masters = host_masters;
  host.discovery = &h->discovery;
  host.protocol = options.protocol;
  host.message_options = &h->message_options;
  host.max_clients = game->options.max_clients;
  host.observe = host_observe;
  host.supports_source_wire = host_supports_source_wire;
  host.admit = host_admit;
  host.disconnect = host_disconnect;
  host.carried_player = host_carried_player;
  host.game_state = host_game_state;
  host.frame = host_frame;
  host.events = host_events;
  host.input = host_input;
  host.expand_client_command = host_expand_client_command;
  host.command = host_command;
  host.userinfo = host_userinfo;
  host.print = host_print;
  return host;
}

void qa_app_sim_q2_application_server_host_destroy(qa_app_q2_server_host host) {
  q2_host *h = host.ctx;
  if (h == NULL) return;
  for (size_t i = 0; i < h->player_count; i++) {
    clear_player(&h->players[i]);
    if (h->players[i].has_known) config_clear(&h->players[i].known);
  }
  free(h->players);
  for (size_t i = 0; i < h->command_count; i++) free(h->commands[i]);
  free(h->commands);
  clear_capture(h);
  free(h->cached_states);
  free(h->entity_events);
  free(h->status_players); free(h->server_info); free(h->hostname);
  qa_wire_admission_destroy(&h->support);
  table_clear(&h->models); table_clear(&h->sounds); table_clear(&h->images);
  config_clear(&h->configs);
  qa_app_q2_downloads_destroy(h->downloads);
  free(h);
}
