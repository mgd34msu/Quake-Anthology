/* Port of src/app/bootstrap/simulation/rerelease-guest-services.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_guest_services.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/combat_binding.h"
#include "qa/compat/q2/rerelease/public_state.h"
#include "qa/compat/q2/rerelease/semantics.h"
#include "qa/compat/q2/rerelease/source_state.h"
#include "qa/compat/q2/rerelease/spatial.h"
#include "qa/core/resource_name_index.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/message.h"
#include "qa/world/collision/body.h"

#define CONFIGSTRINGS 12448
static const double range_base[3] = {62, 8254, 10302}, range_count[3] = {8192, 2048, 512};
static const char *const kinds[3] = {"model", "sound", "image"};
static const qa_vec3 zero = {0, 0, 0};

typedef struct link_metadata { qa_actor_id actor; bool clusters_null; double clusters[16]; size_t cluster_count; double first_cluster, headnode, areas[2]; } link_metadata;
typedef struct motion_entry { qa_actor_id actor; qa_compat_q2_native_input_motion projection; bool has_body; qa_body_state body; qa_actor_id ground; } motion_entry;
typedef struct binding { struct qa_app_sim_rerelease_guest_services *services; qa_compat_q2_rerelease_public_edict *view; qa_owned_actor actor; qa_actor_id ground; struct binding *next; } binding;
typedef struct weapon_model { double index; char *name; } weapon_model;

struct qa_app_sim_rerelease_guest_services {
  qa_app_sim_rerelease_guest_services_options options;
  qa_q2_foundation_host engine_port; /* TS hostOptions.engine getters over the current options */
  qa_compat_q2_rerelease_semantic_bindings semantics;
  qa_compat_q2_rerelease_message_services messages_port;
  qa_original_pickup_admission pickups_port; qa_compat_q2_rerelease_foreign_damage_services damage_port;
  qa_app_sim_configstring *strings; size_t string_count; int32_t *positions;
  qa_resource_name_index names[3];
  weapon_model *weapons; size_t weapon_count, weapon_capacity;
  const char **weapon_list; size_t weapon_list_count;
  qa_event_ring messages; qa_event_cursor cursor;
  qa_app_sim_classic_guest_message *drained; size_t drained_count;
  link_metadata *links; size_t link_count, link_capacity;
  uint8_t buffer_data[65536]; qa_q2_sizebuf buffer;
  uint8_t scratch_data[65536]; qa_q2_sizebuf scratch;
  qa_compat_q2_memory *memory;
  qa_compat_q2_rerelease_q2_guest_host *host;
  qa_compat_q2_rerelease_combat_bindings *combat;
  const qa_compat_q2_rerelease_inventory_item *inventory_items; size_t inventory_item_count; bool has_inventory_items;
  qa_compat_q2_rerelease_source_client *inventory_client;
  bool loading; double frame;
  bool has_velocity; qa_app_sim_player_velocity velocity;
  motion_entry *motions; size_t motion_count, motion_capacity;
  binding *bindings;
  char *appearance_strings[3];
  qa_actor_id *edicts_scratch; size_t edicts_capacity;
};
typedef qa_app_sim_rerelease_guest_services services_t;

static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static char *dup_text(const char *text) { size_t n = strlen(text); char *p = malloc(n + 1); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, text, n + 1); return p; }
static void *grow(void *items, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return items;
  size_t next = *capacity == 0 ? 8 : *capacity * 2; void *grown = realloc(items, next * size); if (grown == NULL) qa_range_error("Out of memory");
  *capacity = next; return grown;
}
static bool same_actor(qa_actor_id a, qa_actor_id b) { return a.slot == b.slot && a.generation == b.generation; }
static int kind_index(const char *kind) { for (int i = 0; i < 3; i++) if (strcmp(kind, kinds[i]) == 0) return i; qa_range_error("Invalid API2023 resource kind %s", kind); return 0; }
static size_t model_count(const qa_shared_scene_queries *scene) {
  switch (scene->geometry.kind) {
    case QA_DECODED_WORLD_Q1: return scene->geometry.as.q1->model_count;
    case QA_DECODED_WORLD_Q2: return scene->geometry.as.q2->model_count;
    default: return scene->geometry.as.q3->model_count;
  }
}
static qa_compat_q2_rerelease_guest_module *module_of(services_t *s) { return qa_compat_q2_rerelease_q2_guest_host_get_module(qa_app_sim_rerelease_guest_services_host(s)); }
static qa_raw_entity_table table_of(services_t *s) { return qa_compat_q2_rerelease_guest_module_entities(module_of(s)); }
static qa_raw_entity_view at_slot(services_t *s, double slot) { qa_raw_entity_table t = table_of(s); return t.at_slot(t.ctx, slot); }
static qa_raw_entity_view from_pointer(services_t *s, qa_guest_address address) { qa_raw_entity_table t = table_of(s); return t.from_pointer(t.ctx, address); }
static qa_compat_q2_rerelease_public_edict *view_of(services_t *s, double slot) {
  qa_compat_q2_rerelease_public_edict *view = qa_compat_q2_rerelease_public_edict_create(qa_app_sim_rerelease_guest_services_memory(s), at_slot(s, slot));
  if (view == NULL) qa_range_error("Out of memory");
  return view;
}
static bool host_actor(services_t *s, qa_raw_entity_view record, qa_actor_id *out) {
  qa_owned_actor owned; if (!qa_compat_q2_rerelease_q2_guest_host_actor(qa_app_sim_rerelease_guest_services_host(s), record, &owned)) return false;
  *out = owned.id; return true;
}
static bool current_actor(qa_raw_entity_view record, qa_actor_id *out) { qa_actor_id_result r = record.current_actor(record.ctx); if (r.has_value) *out = r.value; return r.has_value; }
static motion_entry *motion_of(services_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->motion_count; i++) if (same_actor(s->motions[i].actor, actor)) return &s->motions[i]; return NULL; }
static link_metadata *link_of(services_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->link_count; i++) if (same_actor(s->links[i].actor, actor)) return &s->links[i]; return NULL; }
static void delete_link(services_t *s, qa_actor_id actor) {
  link_metadata *link = link_of(s, actor); if (link == NULL) return;
  size_t index = (size_t)(link - s->links); memmove(link, link + 1, (s->link_count - index - 1) * sizeof *link); s->link_count--;
}

/* --- configstrings --- */
static const char *configstring(services_t *s, double index) {
  if (!(index >= 0 && index < CONFIGSTRINGS) || index != floor(index)) return NULL;
  int32_t p = s->positions[(size_t)index]; return p < 0 ? NULL : s->strings[p].value;
}
static void store_configstring(services_t *s, double index, const char *value) {
  int32_t p = s->positions[(size_t)index]; char *copy = dup_text(value);
  if (p >= 0) { free((char *)s->strings[p].value); s->strings[p].value = copy; }
  else { s->strings[s->string_count].index = index; s->strings[s->string_count].value = copy; s->positions[(size_t)index] = (int32_t)s->string_count++; }
  for (int k = 0; k < 3; k++) { double slot = index - range_base[k]; if (slot > 0 && slot < range_count[k]) { qa_resource_name_index_set(&s->names[k], (int64_t)slot, copy); break; } }
}
static void clear_strings(services_t *s) {
  for (size_t i = 0; i < s->string_count; i++) free((char *)s->strings[i].value);
  s->string_count = 0; for (size_t i = 0; i < CONFIGSTRINGS; i++) s->positions[i] = -1;
}
static void clear_weapons(services_t *s) { for (size_t i = 0; i < s->weapon_count; i++) free(s->weapons[i].name); s->weapon_count = 0; }
static void rebuild_weapon_models(services_t *s) {
  /* TS sort(([a], [b]) => a - b) over distinct indexes: a stable insertion sort gives the same order. */
  for (size_t i = 1; i < s->weapon_count; i++) {
    weapon_model row = s->weapons[i]; size_t j = i;
    while (j > 0 && s->weapons[j - 1].index > row.index) { s->weapons[j] = s->weapons[j - 1]; j--; }
    s->weapons[j] = row;
  }
  free(s->weapon_list); s->weapon_list = calloc(s->weapon_count + 1, sizeof *s->weapon_list); if (s->weapon_list == NULL) qa_range_error("Out of memory");
  s->weapon_list[0] = "weapon.md2"; for (size_t i = 0; i < s->weapon_count; i++) s->weapon_list[i + 1] = s->weapons[i].name;
  s->weapon_list_count = s->weapon_count + 1;
}
static bool is_weapon_model(double index, const char *value) { return index > range_base[0] && index < range_base[0] + range_count[0] && value[0] == '#'; }
static bool delete_weapon(services_t *s, double index) {
  for (size_t i = 0; i < s->weapon_count; i++) if (s->weapons[i].index == index) { free(s->weapons[i].name); memmove(&s->weapons[i], &s->weapons[i + 1], (s->weapon_count - i - 1) * sizeof *s->weapons); s->weapon_count--; return true; }
  return false;
}
static void set_weapon(services_t *s, double index, const char *name) {
  (void)delete_weapon(s, index);
  s->weapons = grow(s->weapons, &s->weapon_capacity, s->weapon_count, sizeof *s->weapons);
  s->weapons[s->weapon_count].index = index; s->weapons[s->weapon_count++].name = dup_text(name);
}
static void validate_configstring(double index, const char *value) {
  (void)value; /* C strings cannot hold the NUL TS rejects */
  if (index != floor(index) || index < 0 || index >= CONFIGSTRINGS) qa_range_error("Invalid API2023 configstring");
}
static void seed_map(services_t *s) {
  for (int k = 0; k < 3; k++) qa_resource_name_index_clear(&s->names[k]);
  clear_weapons(s); rebuild_weapon_models(s);
  store_configstring(s, 63, s->options.base.map_path);
  size_t models = model_count(s->options.base.scene);
  for (size_t i = 1; i < models; i++) { char name[32]; snprintf(name, sizeof name, "*%zu", i); store_configstring(s, 63 + (double)i + (i >= 254 ? 1 : 0), name); }
}
const char *qa_app_sim_rerelease_guest_services_resource(services_t *s, const char *kind, double index) {
  const char *value = configstring(s, range_base[kind_index(kind)] + index); return value == NULL ? "" : value;
}

/* --- messages --- */
static void free_message(const void *record, int64_t id, void *ctx) { (void)id; (void)ctx; free(((const qa_app_sim_classic_guest_message *)record)->bytes); }
static void free_drained(services_t *s) { for (size_t i = 0; i < s->drained_count; i++) free(s->drained[i].bytes); free(s->drained); s->drained = NULL; s->drained_count = 0; }
static void enqueue(services_t *s, qa_app_sim_classic_guest_audience audience, bool reliable, const uint8_t *bytes, size_t length, double dupe_key) {
  qa_app_sim_classic_guest_message message; memset(&message, 0, sizeof message);
  message.source_dialect = "q2-multicast-float"; message.audience = audience; message.reliable = reliable; message.dupe_key = dupe_key; message.length = length;
  message.bytes = malloc(length == 0 ? 1 : length); if (message.bytes == NULL) qa_range_error("Out of memory");
  if (length != 0) memcpy(message.bytes, bytes, length);
  (void)qa_event_ring_admit(&s->messages, &message);
}
static qa_app_sim_classic_guest_audience multicast_all(void) { qa_app_sim_classic_guest_audience a = {false, 0, zero, "all"}; return a; }
static qa_q2_sizebuf *scratch_buffer(services_t *s) { qa_q2_sz_clear(&s->scratch); return &s->scratch; }
const qa_app_sim_rerelease_guest_message *qa_app_sim_rerelease_guest_services_drain_messages(services_t *s, size_t *out_count) {
  free_drained(s); s->drained = qa_event_ring_take(&s->messages, &s->cursor, &s->drained_count); qa_event_ring_retire(&s->messages);
  *out_count = s->drained_count; return s->drained;
}
static bool accepts_client(services_t *s, double slot) {
  if (s->options.base.accepts_client.accepts_client != NULL) return s->options.base.accepts_client.accepts_client(s->options.base.accepts_client.ctx, slot);
  qa_raw_entity_table t = table_of(s);
  return slot >= 1 && slot <= s->options.base.max_clients && slot < t.count && qa_compat_q2_rerelease_q2_guest_host_is_client_reserved(qa_app_sim_rerelease_guest_services_host(s), slot);
}
static bool m_accepts(void *ctx, double slot) { return accepts_client(ctx, slot); }
static void m_unicast(void *ctx, qa_compat_q2_rerelease_unicast value) {
  qa_app_sim_classic_guest_audience audience = {true, value.client_slot, zero, NULL};
  enqueue(ctx, audience, value.reliable, value.bytes, value.byte_length, value.dupe_key);
}
static void m_multicast(void *ctx, qa_compat_q2_rerelease_multicast value) {
  qa_app_sim_classic_guest_audience audience = {false, 0, value.has_origin ? value.origin : zero, value.destination};
  enqueue(ctx, audience, value.reliable, value.bytes, value.byte_length, 0);
}

void qa_app_sim_rerelease_guest_services_set_configstring(services_t *s, double index, const char *value) {
  validate_configstring(index, value);
  const char *current = configstring(s, index); if (current != NULL && strcmp(current, value) == 0) return;
  store_configstring(s, index, value); const char *text = configstring(s, index);
  bool removed = delete_weapon(s, index);
  if (is_weapon_model(index, text)) { set_weapon(s, index, text + 1); rebuild_weapon_models(s); }
  else if (removed) rebuild_weapon_models(s);
  qa_q2_foundation_host *engine = s->options.base.engine;
  if (index >= 10814 && index < 11070) { qa_q2_presentation_event e; memset(&e, 0, sizeof e); e.kind = "lightstyle"; e.style = index - 10814; e.pattern = text; engine->emit(engine->ctx, e); }
  if (index == 1) { qa_q2_presentation_event e; memset(&e, 0, sizeof e); e.kind = "music"; e.track = text; engine->emit(engine->ctx, e); }
  if (!s->loading) {
    qa_q2_sizebuf *buffer = scratch_buffer(s); qa_q2_msg_write_byte(buffer, 13); qa_q2_msg_write_short(buffer, index); qa_q2_msg_write_string(buffer, text);
    enqueue(s, multicast_all(), true, buffer->data, (size_t)buffer->cursize, 0);
  }
}
double qa_app_sim_rerelease_guest_services_resource_index(services_t *s, const char *kind, const char *name) {
  int k = kind_index(kind); int64_t index = qa_resource_name_index_find(&s->names[k], name);
  if (index < 0) qa_range_error("API2023 %s index overflow", kind);
  if (index != 0 && strcmp(qa_app_sim_rerelease_guest_services_resource(s, kind, (double)index), name) != 0)
    qa_app_sim_rerelease_guest_services_set_configstring(s, range_base[k] + (double)index, name);
  return (double)index;
}
void qa_app_sim_rerelease_guest_services_restore_configstrings(services_t *s, const qa_app_sim_configstring *values, size_t count) {
  for (size_t i = 0; i < count; i++) validate_configstring(values[i].index, values[i].value);
  clear_strings(s); clear_weapons(s);
  for (int k = 0; k < 3; k++) qa_resource_name_index_clear(&s->names[k]);
  for (size_t i = 0; i < count; i++) { store_configstring(s, values[i].index, values[i].value); if (is_weapon_model(values[i].index, values[i].value)) set_weapon(s, values[i].index, values[i].value + 1); }
  rebuild_weapon_models(s);
}
const qa_app_sim_configstring *qa_app_sim_rerelease_guest_services_configstrings(services_t *s, size_t *out_count) { *out_count = s->string_count; return s->strings; }
const char *qa_app_sim_rerelease_guest_services_configstring(services_t *s, double index) { return configstring(s, index); }

/* --- bodies and links --- */
static double inline_model(services_t *s, double index) {
  const char *path = qa_app_sim_rerelease_guest_services_resource(s, "model", index);
  if (index == 1 && strcmp(path, s->options.base.map_path) == 0) return 0;
  double model = qa_js_string_to_number(path[0] == '\0' ? "" : path + 1);
  if (path[0] != '*' || model != floor(model) || !isfinite(model) || model < 0 || model >= (double)model_count(s->options.base.scene))
    qa_raise("Error", 0, 0, "Invalid API2023 inline model %s", path);
  return model;
}
static qa_body_state body(services_t *s, qa_compat_q2_rerelease_public_edict *view, qa_actor_id *ground) {
  qa_raw_entity_view record = qa_compat_q2_rerelease_public_edict_get_record(view);
  qa_actor_id actor; bool has_actor = current_actor(record, &actor);
  if (has_actor) { motion_entry *cached = motion_of(s, actor); if (cached != NULL && cached->has_body) return cached->body; }
  qa_vec3 pending; bool has_pending = has_actor && s->has_velocity && s->velocity.read(s->velocity.ctx, actor, &pending);
  motion_entry *motion = has_actor ? motion_of(s, actor) : NULL;
  qa_compat_q2_native_input_motion_state moved; if (motion != NULL) moved = motion->projection.read(motion->projection.ctx);
  qa_guest_address client; bool has_client = qa_compat_q2_rerelease_public_edict_pointer(view, "client", &client);
  bool player = record.slot > 0 && record.slot <= s->options.base.max_clients && has_client;
  qa_vec3 velocity; const qa_vec3 *velocity_ptr = NULL;
  if (motion != NULL) { velocity = moved.velocity; velocity_ptr = &velocity; }
  else if (has_pending) { velocity = pending; velocity_ptr = &velocity; }
  else if (player) { velocity = qa_compat_q2_rerelease_public_edict_player_velocity(view); velocity_ptr = &velocity; }
  qa_body_state state = qa_compat_q2_rerelease_public_edict_body(view, velocity_ptr, motion != NULL ? &moved.origin : NULL);
  if (!player) return state;
  bool grounded = has_actor && motion != NULL ? motion->projection.grounded(motion->projection.ctx) : ((int64_t)qa_compat_q2_rerelease_public_edict_player_movement_flags(view) & 4) != 0;
  if (!grounded) return state;
  qa_actor_id world; if (!host_actor(s, at_slot(s, 0), &world)) return state;
  *ground = world; state.ground = ground; return state;
}
static qa_body_state binding_read(void *ctx) { binding *b = ctx; return body(b->services, b->view, &b->ground); }
static void binding_write(void *ctx, qa_body_state state) {
  binding *b = ctx; services_t *s = b->services; qa_actor_id scratch;
  qa_vec3 velocity = body(s, b->view, &scratch).velocity; motion_entry *motion = motion_of(s, b->actor.id);
  qa_raw_entity_view record = qa_compat_q2_rerelease_public_edict_get_record(b->view);
  if (motion != NULL) {
    motion->has_body = true; motion->body = state; if (state.ground != NULL) { motion->ground = *state.ground; motion->body.ground = &motion->ground; }
    qa_compat_q2_native_input_motion_state value = {state.origin, state.velocity}; motion->projection.write(motion->projection.ctx, value);
  } else if (state.velocity.x != velocity.x || state.velocity.y != velocity.y || state.velocity.z != velocity.z) {
    qa_guest_address client;
    if (record.slot < 1 || record.slot > s->options.base.max_clients || !qa_compat_q2_rerelease_public_edict_pointer(b->view, "client", &client) || !s->has_velocity)
      fail("API2023 velocity writes require a source semantic binding");
    s->velocity.write(s->velocity.ctx, b->actor, state.velocity);
  }
  if (motion == NULL) qa_compat_q2_rerelease_public_edict_set_vector(b->view, "s.origin", state.origin);
  qa_compat_q2_rerelease_public_edict_set_vector(b->view, "s.angles", state.angles);
  qa_compat_q2_rerelease_public_edict_set_vector(b->view, "mins", state.bounds.min); qa_compat_q2_rerelease_public_edict_set_vector(b->view, "maxs", state.bounds.max);
}
static qa_compat_q2_rerelease_actor_bindings bind_entity(services_t *s, qa_raw_entity_view record, qa_owned_actor actor) {
  binding *b = calloc(1, sizeof *b); if (b == NULL) qa_range_error("Out of memory");
  b->services = s; b->view = qa_compat_q2_rerelease_public_edict_create(qa_app_sim_rerelease_guest_services_memory(s), record); b->actor = actor;
  if (b->view == NULL) qa_range_error("Out of memory");
  b->next = s->bindings; s->bindings = b;
  delete_link(s, actor.id);
  qa_compat_q2_rerelease_actor_bindings out; memset(&out, 0, sizeof out);
  out.body.ctx = b; out.body.read = binding_read; out.body.write = binding_write;
  if (s->combat != NULL) out.has_combat = qa_compat_q2_rerelease_combat_bindings_bind(s->combat, record, &out.combat);
  return out;
}
static void world_link(services_t *s, qa_bounds bounds, link_metadata *out) {
  qa_shared_scene_queries *scene = s->options.base.scene; qa_leaf_query_result query = qa_shared_box_leaves(scene, bounds, 128);
  double first = 0, second = 0; size_t count = 0; bool too_many = false;
  for (size_t i = 0; i < query.leaves.count; i++) {
    double leaf = query.leaves.values[i], area = qa_shared_leaf_area(scene, leaf), cluster = qa_shared_leaf_cluster(scene, leaf);
    if (area != 0) { if (first != 0 && first != area) second = area; else first = area; }
    if (cluster >= 0) { bool seen = false; for (size_t c = 0; c < count && !seen; c++) seen = out->clusters[c] == cluster;
      if (!seen) { if (count < 16) out->clusters[count++] = cluster; else too_many = true; } }
  }
  out->clusters_null = query.overflow || query.leaves.count >= 128 || too_many; out->cluster_count = out->clusters_null ? 0 : count;
  out->first_cluster = count > 0 ? out->clusters[0] : 0; out->headnode = query.has_topnode ? query.topnode : 0; out->areas[0] = first; out->areas[1] = second;
}
static void prepare_link(void *ctx, qa_owned_actor actor, qa_raw_entity_view record) {
  services_t *s = ctx; qa_compat_q2_rerelease_public_edict *view = qa_compat_q2_rerelease_public_edict_create(s->memory, record);
  double solid = qa_compat_q2_rerelease_public_edict_byte(view, "solid"); int64_t flags = (int64_t)qa_compat_q2_rerelease_public_edict_uint(view, "svflags");
  qa_guest_address address; qa_actor_id owner; bool has_owner = false;
  if (qa_compat_q2_rerelease_public_edict_pointer(view, "owner", &address)) has_owner = host_actor(s, from_pointer(s, address), &owner);
  qa_actor_collision c; memset(&c, 0, sizeof c);
  c.family = "q2"; c.shape_kind = solid == 3 ? "model" : "box";
  if (solid == 3) c.shape_model = inline_model(s, qa_compat_q2_rerelease_public_edict_int(view, "s.modelindex"));
  c.contents = solid == 0 ? 0 : solid == 3 ? 1 : (flags & 2) != 0 ? 0x4000000 : (flags & 8) != 0 ? 0x40000000 : (flags & 128) != 0 ? 0x80000000 : 0x2000000;
  c.owner = has_owner ? &owner : NULL; c.role = solid == 1 ? "trigger" : "solid"; c.monster = (flags & 4) != 0; c.dead_monster = (flags & 2) != 0;
  s->options.base.collision.collision(s->options.base.collision.ctx, actor, c);
  qa_compat_q2_rerelease_public_edict_destroy(view);
}
static qa_compat_q2_rerelease_link_metadata link_metadata_of(void *ctx, qa_owned_actor actor, qa_raw_entity_view record) {
  services_t *s = ctx; qa_compat_q2_rerelease_public_edict *view = qa_compat_q2_rerelease_public_edict_create(s->memory, record);
  qa_actor_id ground; qa_body_state state = body(s, view, &ground);
  double solid = qa_compat_q2_rerelease_public_edict_byte(view, "solid"), flags = qa_compat_q2_rerelease_public_edict_uint(view, "svflags");
  link_metadata *link = link_of(s, actor.id);
  if (link == NULL) { s->links = grow(s->links, &s->link_capacity, s->link_count, sizeof *s->links); link = &s->links[s->link_count++]; }
  memset(link, 0, sizeof *link); link->actor = actor.id; world_link(s, qa_compat_q2_rerelease_link_bounds(state, solid), link);
  qa_compat_q2_rerelease_link_metadata out = {link->areas[0], link->areas[1], qa_compat_q2_rerelease_network_solid(state.bounds, solid, flags)};
  qa_compat_q2_rerelease_public_edict_destroy(view); return out;
}

/* --- spatial services --- */
static bool sp_areas(void *ctx, double a, double b) { services_t *s = ctx; return qa_shared_areas_connected(s->options.base.scene, a, b); }
static bool sp_visibility(void *ctx, const char *kind, qa_vec3 first, qa_vec3 second, bool portals) {
  services_t *s = ctx; qa_shared_scene_queries *scene = s->options.base.scene; double a = qa_shared_point_leaf(scene, first), b = qa_shared_point_leaf(scene, second);
  return qa_shared_cluster_visible(scene, qa_shared_leaf_cluster(scene, a), qa_shared_leaf_cluster(scene, b), kind) && (!portals || qa_shared_areas_connected(scene, qa_shared_leaf_area(scene, a), qa_shared_leaf_area(scene, b)));
}
static double sp_surface(void *ctx, qa_q2_surface_info surface) {
  services_t *s = ctx; if (s->options.base.scene->geometry.kind != QA_DECODED_WORLD_Q2) return 0;
  const qa_q2_world_geometry *g = s->options.base.scene->geometry.as.q2;
  for (size_t i = 0; i < g->texture_info_count; i++) {
    const qa_q2_surface_info *v = &g->texture_info[i].surface;
    if (v->name != NULL && surface.name != NULL && strcmp(v->name, surface.name) == 0 && v->flags == surface.flags && v->value == surface.value) return (double)i;
  }
  return 0;
}
static const qa_actor_id *sp_box(void *ctx, qa_vec3 min, qa_vec3 max, double area, size_t *out_count) {
  services_t *s = ctx; if (area != 1 && area != 2) qa_range_error("Invalid API2023 BoxEdicts area");
  qa_bounds bounds = {min, max}; size_t count = 0; const qa_spatial_actor *found = qa_shared_query_actors(s->options.base.scene, bounds, area == 1 ? "solid" : "trigger", &count);
  if (count > s->edicts_capacity) { qa_actor_id *g = realloc(s->edicts_scratch, count * sizeof *g); if (g == NULL) qa_range_error("Out of memory"); s->edicts_scratch = g; s->edicts_capacity = count; }
  for (size_t i = 0; i < count; i++) s->edicts_scratch[i] = found[i].body.actor;
  *out_count = count; return s->edicts_scratch;
}
static double sp_inline(void *ctx, double index) { return inline_model(ctx, index); }

/* --- semantics --- */
static bool se_project(void *ctx, qa_raw_entity_view view, qa_compat_q2_rerelease_guest_module *module, qa_owned_actor *out) {
  services_t *s = ctx; const qa_compat_q2_rerelease_semantic_bindings *b = s->options.semantic_bindings; return b->project(b->ctx, view, module, out);
}
static double se_generation(void *ctx, qa_raw_entity_view view) { services_t *s = ctx; const qa_compat_q2_rerelease_semantic_bindings *b = s->options.semantic_bindings; return b->generation(b->ctx, view); }
static void se_bound(void *ctx, qa_raw_entity_view view, qa_owned_actor actor) {
  services_t *s = ctx; const qa_compat_q2_rerelease_semantic_bindings *b = s->options.semantic_bindings;
  if (b != NULL && b->bound != NULL) b->bound(b->ctx, view, actor);
  s->options.base.admit.admit(s->options.base.admit.ctx, view, actor);
}
static qa_compat_q2_rerelease_actor_bindings se_bind(void *ctx, qa_raw_entity_view view, qa_owned_actor actor, qa_compat_q2_rerelease_guest_module *module) {
  services_t *s = ctx; const qa_compat_q2_rerelease_semantic_bindings *b = s->options.semantic_bindings;
  if (b != NULL) return b->bind(b->ctx, view, actor, module);
  return bind_entity(s, view, actor);
}
static qa_guest_address se_foreign(void *ctx, qa_actor_id actor) {
  services_t *s = ctx; const qa_compat_q2_rerelease_semantic_bindings *b = s->options.semantic_bindings;
  if (b != NULL) return b->foreign_address(b->ctx, actor);
  qa_raise("Error", 0, 0, "API2023 module requires a semantic projection extension for foreign actor %lld", (long long)actor.slot);
}

/* --- sound and print --- */
static void sound_event(void *ctx, qa_compat_q2_rerelease_sound_event event) {
  services_t *s = ctx;
  if (event.volume < 0 || event.volume > 1 || event.attenuation < 0 || event.attenuation > 4 || event.time_offset < 0 || event.time_offset > 0.255 || event.sound_index < 0 || event.sound_index >= 2048)
    qa_range_error("Invalid API2023 sound arguments");
  qa_compat_q2_rerelease_public_edict *view = event.has_entity_slot ? view_of(s, event.entity_slot) : NULL;
  qa_actor_id ground; qa_body_state state; if (view != NULL) state = body(s, view, &ground);
  if (view == NULL && !event.has_origin) fail("API2023 sound has neither entity nor origin");
  qa_vec3 origin;
  if (event.has_origin) origin = event.origin;
  else if (view == NULL) origin = zero;
  else if (qa_compat_q2_rerelease_public_edict_byte(view, "solid") == 3) {
    origin.x = state.origin.x + (state.bounds.min.x + state.bounds.max.x) * 0.5; origin.y = state.origin.y + (state.bounds.min.y + state.bounds.max.y) * 0.5;
    origin.z = state.origin.z + (state.bounds.min.z + state.bounds.max.z) * 0.5;
  } else origin = state.origin;
  if (view != NULL) qa_compat_q2_rerelease_public_edict_destroy(view);
  bool positioned = true;
  int flags = (event.has_entity_slot ? 8 : 0) | (event.volume != 1 ? 1 : 0) | (event.attenuation != 1 ? 2 : 0) | (positioned ? 4 : 0) | (event.time_offset != 0 ? 16 : 0) | (event.sound_index > 255 ? 32 : 0);
  qa_q2_sizebuf *buffer = scratch_buffer(s); qa_q2_msg_write_byte(buffer, 9); qa_q2_msg_write_byte(buffer, flags);
  if ((flags & 32) != 0) qa_q2_msg_write_short(buffer, event.sound_index); else qa_q2_msg_write_byte(buffer, event.sound_index);
  if ((flags & 1) != 0) qa_q2_msg_write_byte(buffer, event.volume * 255);
  if ((flags & 2) != 0) qa_q2_msg_write_byte(buffer, event.attenuation * 64);
  if ((flags & 16) != 0) qa_q2_msg_write_byte(buffer, event.time_offset * 1000);
  int32_t channel = qa_js_to_int32(event.channel);
  if (event.has_entity_slot) qa_q2_msg_write_short(buffer, (double)(qa_js_to_int32(event.entity_slot) << 3 | (channel & 7)));
  if (positioned) { qa_q2_msg_write_float(buffer, origin.x); qa_q2_msg_write_float(buffer, origin.y); qa_q2_msg_write_float(buffer, origin.z); }
  qa_app_sim_classic_guest_audience audience = {event.audience_is_client, event.client_slot, origin, NULL};
  if (!event.audience_is_client) { audience.slot = 0; audience.scope = (channel & 8) != 0 || event.attenuation == 0 ? "all" : "phs"; }
  enqueue(s, audience, (channel & 16) != 0, buffer->data, (size_t)buffer->cursize, event.audience_is_client ? event.dupe_key : 0);
}
static void print(services_t *s, bool has_slot, double slot, double level, const char *text, bool broadcast, bool center) {
  if (!broadcast && !has_slot) { s->options.base.print.print(s->options.base.print.ctx, text); return; }
  if (has_slot && !accepts_client(s, slot)) return;
  qa_q2_sizebuf *buffer = scratch_buffer(s); qa_q2_msg_write_byte(buffer, center ? 15 : 10); if (!center) qa_q2_msg_write_byte(buffer, level); qa_q2_msg_write_string(buffer, text);
  qa_app_sim_classic_guest_audience audience = broadcast ? multicast_all() : (qa_app_sim_classic_guest_audience){true, has_slot ? slot : 0, zero, NULL};
  enqueue(s, audience, true, buffer->data, (size_t)buffer->cursize, 0);
  if (broadcast) s->options.base.print.print(s->options.base.print.ctx, text);
}
static void ds_event(void *ctx, qa_compat_q2_rerelease_debug_shapes_event event) { services_t *s = ctx; s->options.debug_shapes.debug_shapes(s->options.debug_shapes.ctx, event); }
static void wt_event(void *ctx, qa_compat_q2_rerelease_world_text_event event) { services_t *s = ctx; s->options.world_text.world_text(s->options.world_text.ctx, event); }

/* --- core services --- */
static void cs_print(void *ctx, const char *text) { services_t *s = ctx; s->options.base.print.print(s->options.base.print.ctx, text); }
static const char *cs_get(void *ctx, double index) { const char *value = configstring(ctx, index); return value == NULL ? "" : value; }
static void cs_set(void *ctx, double index, const char *value) { qa_app_sim_rerelease_guest_services_set_configstring(ctx, index, value); }
static double cs_resource(void *ctx, const char *kind, const char *name) { return qa_app_sim_rerelease_guest_services_resource_index(ctx, kind, name); }
static double cs_frame(void *ctx) { return ((services_t *)ctx)->frame; }
static const char *const *cs_arguments(void *ctx, size_t *out_count) { services_t *s = ctx; const char *args; return s->options.base.command.command(s->options.base.command.ctx, out_count, &args); }
static const char *cs_tail(void *ctx) { services_t *s = ctx; const char *args = ""; size_t count; (void)s->options.base.command.command(s->options.base.command.ctx, &count, &args); return args; }
static void cs_add(void *ctx, const char *text) { services_t *s = ctx; s->options.base.add_command.add_command(s->options.base.add_command.ctx, text); }
static bool cs_extension(void *ctx, const char *name, const char *api, qa_guest_address *out) { (void)ctx; (void)name; (void)api; (void)out; return false; }
static double number_arg(const qa_guest_call_value *args, size_t count, size_t index) { return (double)qa_compat_q2_guest_integer(args, count, index); }
static char *text_arg(services_t *s, const qa_guest_call_value *args, size_t count, size_t index) { return qa_compat_q2_read_guest_string(s->memory, qa_compat_q2_guest_required_pointer(args, count, index), 65536); }
static bool slot_arg(services_t *s, const qa_guest_call_value *args, size_t count, size_t index, double *out) {
  qa_guest_address address; if (!qa_compat_q2_guest_pointer(args, count, index, &address)) return false;
  *out = from_pointer(s, address).slot; return true;
}
static bool nav_actor(void *ctx, qa_guest_address address, qa_actor_id *out) { services_t *s = ctx; return host_actor(s, from_pointer(s, address), out); }
static qa_vec3 vector_at(services_t *s, qa_guest_address address) { return qa_compat_q2_mem_read_f32_vector(s->memory, address); }
static qa_guest_call_result info(services_t *s, qa_compat_q2_rerelease_import_call call) {
  const qa_guest_call_value *args = call.arguments; size_t count = call.argument_count;
  qa_guest_address address = qa_compat_q2_guest_required_pointer(args, count, 0);
  char *key = qa_compat_q2_read_guest_string(s->memory, qa_compat_q2_guest_required_pointer(args, count, 1), 2048);
  char *input = qa_compat_q2_read_guest_string(s->memory, address, 2048);
  size_t length = strlen(input), key_length = strlen(key); char *output = calloc(length + 2048 + 4, 1); size_t out_length = 0; bool found = false;
  if (output == NULL) qa_range_error("Out of memory");
  size_t cursor = 0;
  while (cursor < length) {
    size_t start = cursor; if (input[cursor] == '\\') cursor++;
    const char *separator = strchr(input + cursor, '\\');
    if (separator == NULL) { memcpy(output + out_length, input + start, length - start); out_length += length - start; break; }
    size_t sep = (size_t)(separator - input); const char *next = strchr(input + sep + 1, '\\'); size_t end = next == NULL ? length : (size_t)(next - input);
    if (sep - cursor == key_length && strncmp(input + cursor, key, key_length) == 0) found = true;
    else { memcpy(output + out_length, input + start, end - start); out_length += end - start; }
    cursor = end;
  }
  output[out_length] = '\0';
  bool result = found;
  if (strcmp(call.name, "Info_SetValueForKey") == 0) {
    char *value = qa_compat_q2_read_guest_string(s->memory, qa_compat_q2_guest_required_pointer(args, count, 2), 2048); size_t value_length = strlen(value);
    if (key_length >= 64 || value_length >= 256 || strpbrk(key, "\\\";") != NULL || strpbrk(value, "\\\";") != NULL) { free(value); free(key); free(input); free(output); return qa_compat_q2_rerelease_guest_bool(false); }
    result = value_length == 0 || out_length + key_length + value_length + 2 < 2048;
    if (result && value_length != 0) {
      char *pair = malloc(key_length + value_length + 3); if (pair == NULL) qa_range_error("Out of memory");
      sprintf(pair, "\\%s\\%s", key, value);
      for (size_t i = 0; pair[i] != '\0'; i++) { unsigned byte = (unsigned char)pair[i] & 127; if (byte >= 32 && byte < 127) output[out_length++] = (char)byte; }
      output[out_length] = '\0'; free(pair);
    }
    free(value);
  }
  qa_compat_q2_memory *m = s->memory; m->write(m->ctx, address, (const uint8_t *)output, out_length); qa_compat_q2_mem_write_u8(m, qa_compat_q2_mem_offset(m, address, (int64_t)out_length), 0);
  free(key); free(input); free(output);
  return qa_compat_q2_rerelease_guest_bool(result);
}
static qa_guest_call_result clip(services_t *s, qa_compat_q2_rerelease_import_call call) {
  const qa_guest_call_value *args = call.arguments; size_t count = call.argument_count;
  qa_raw_entity_view view = from_pointer(s, qa_compat_q2_guest_required_pointer(args, count, 0));
  qa_actor_id actor; bool has_actor = host_actor(s, view, &actor);
  qa_guest_address min, max; bool has_min = qa_compat_q2_guest_pointer(args, count, 2, &min), has_max = qa_compat_q2_guest_pointer(args, count, 3, &max);
  if (has_min != has_max) fail("API2023 clip requires both bounds or neither");
  qa_compat_q2_rerelease_public_edict *source = qa_compat_q2_rerelease_public_edict_create(s->memory, view);
  qa_actor_id ground; qa_body_state state = body(s, source, &ground);
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = vector_at(s, qa_compat_q2_guest_required_pointer(args, count, 1)); query.end = vector_at(s, qa_compat_q2_guest_required_pointer(args, count, 4));
  query.shape.kind = has_min ? "box" : "point"; if (has_min) { query.shape.bounds.min = vector_at(s, min); query.shape.bounds.max = vector_at(s, max); }
  query.policy.kind = "q2"; query.policy.contents_mask = number_arg(args, count, 5); query.policy.leaf_contents = "merged"; query.numeric = s->options.base.numeric.profile;
  double solid = qa_compat_q2_rerelease_public_edict_byte(source, "solid"), modelindex = qa_compat_q2_rerelease_public_edict_int(source, "s.modelindex");
  const char *source_model = qa_app_sim_rerelease_guest_services_resource(s, "model", modelindex);
  qa_trace_result hit;
  if (view.slot == 0) hit = qa_shared_geometry_trace(s->options.base.scene, query);
  else if (solid == 3 || (solid == 1 && source_model[0] == '*')) {
    query.target.is_model = true; query.target.model = inline_model(s, modelindex); query.target.origin = state.origin; query.target.angles = state.angles;
    hit = qa_shared_geometry_trace(s->options.base.scene, query);
  } else {
    qa_spatial_actor body_actor; memset(&body_actor, 0, sizeof body_actor);
    body_actor.body.actor = has_actor ? actor : s->options.base.engine->world_actor(s->options.base.engine->ctx);
    body_actor.body.state = state; body_actor.body.absolute_bounds = qa_compat_q2_rerelease_link_bounds(state, solid); body_actor.body.link_count = qa_compat_q2_rerelease_public_edict_int(source, "linkcount");
    body_actor.collision.family = "q2"; body_actor.collision.shape_kind = "box"; body_actor.collision.role = "solid"; body_actor.collision.contents = 0x2000000;
    hit = qa_trace_actor_body(query, body_actor);
  }
  qa_compat_q2_rerelease_public_edict_destroy(source);
  return qa_compat_q2_rerelease_q2_guest_host_encode_trace(qa_app_sim_rerelease_guest_services_host(s), hit, &view.address);
}
static bool cs_invoke(void *ctx, qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out) {
  services_t *s = ctx; const qa_guest_call_value *args = call.arguments; size_t count = call.argument_count; const char *name = call.name; double slot = 0; bool has_slot;
  *out = qa_compat_q2_call_void();
  if (strcmp(name, "Broadcast_Print") == 0) { char *text = text_arg(s, args, count, 1); print(s, false, 0, number_arg(args, count, 0), text, true, false); free(text); }
  else if (strcmp(name, "Client_Print") == 0) { has_slot = slot_arg(s, args, count, 0, &slot); char *text = text_arg(s, args, count, 2); print(s, has_slot, slot, number_arg(args, count, 1), text, false, false); free(text); }
  else if (strcmp(name, "Center_Print") == 0) { has_slot = slot_arg(s, args, count, 0, &slot); char *text = text_arg(s, args, count, 1); print(s, has_slot, slot, 5, text, false, true); free(text); }
  else if (strcmp(name, "Loc_Print") == 0) {
    double n = number_arg(args, count, 4); if (n != floor(n) || n < 0 || n > 8) qa_range_error("API2023 localization argument count");
    char *values[8]; size_t value_count = 0;
    if (n > 0) { qa_guest_address base = qa_compat_q2_guest_required_pointer(args, count, 3);
      for (size_t i = 0; i < (size_t)n; i++) { qa_guest_address at; values[value_count++] = qa_compat_q2_mem_read_pointer(s->memory, qa_compat_q2_mem_offset(s->memory, base, (int64_t)i * 8), &at) ? qa_compat_q2_read_guest_string(s->memory, at, 65536) : dup_text(""); } }
    double level = number_arg(args, count, 1); has_slot = slot_arg(s, args, count, 0, &slot);
    char *key = text_arg(s, args, count, 2); char *text = s->options.localize.localize(s->options.localize.ctx, key, (const char *const *)values, value_count);
    int32_t bits = qa_js_to_int32(level);
    print(s, has_slot, slot, bits & ~8, text, (bits & 8) != 0, false);
    free(text); free(key); for (size_t i = 0; i < value_count; i++) free(values[i]);
  }
  else if (strcmp(name, "DebugGraph") == 0) {
    const qa_guest_call_value *value = qa_compat_q2_guest_argument(args, count, 0);
    if (value->kind != QA_GUEST_VALUE_FLOAT32) qa_raise("TypeError", 0, 0, "API2023 graph requires a float");
    s->options.base.debug_graph.debug_graph(s->options.base.debug_graph.ctx, value->number_value, number_arg(args, count, 1));
  }
  else if (strcmp(name, "SendToClipBoard") == 0) { if (s->options.clipboard.client) { char *text = text_arg(s, args, count, 0); s->options.clipboard.write(s->options.clipboard.ctx, text); free(text); } }
  else if (strcmp(name, "ReportMatchDetails_Multicast") == 0) qa_q2_sz_clear(&s->buffer);
  else if (strcmp(name, "Bot_MoveToPoint") == 0 || strcmp(name, "Bot_FollowActor") == 0 || strcmp(name, "GetPathToGoal") == 0) {
    qa_compat_q2_rerelease_navigation_imports *navigation = qa_compat_q2_rerelease_navigation_imports_create(s->memory, s->options.navigation, s, nav_actor);
    if (navigation == NULL) qa_range_error("Out of memory");
    *out = qa_compat_q2_rerelease_navigation_imports_invoke(navigation, name, args, count);
    qa_compat_q2_rerelease_navigation_imports_destroy(navigation);
  }
  else if (strcmp(name, "Info_RemoveKey") == 0 || strcmp(name, "Info_SetValueForKey") == 0) *out = info(s, call);
  else if (strcmp(name, "clip") == 0) *out = clip(s, call);
  else return false;
  return true;
}

/* --- construction --- */
static void *pk_run(void *ctx, qa_original_pickup_offer offer, void *(*execute)(void *, qa_source_pickup_selection, qa_source_pickup_lifetime), void *execute_ctx) {
  services_t *s = ctx; const qa_original_pickup_admission *p = s->options.base.pickups; if (p == NULL) fail("Native pickup authority is unavailable");
  return p->run_source(p->ctx, offer, execute, execute_ctx);
}
static qa_original_pickup_outcome pk_touch(void *ctx, qa_original_pickup_offer offer, qa_original_pickup_continuation continuation) {
  services_t *s = ctx; const qa_original_pickup_admission *p = s->options.base.pickups; if (p == NULL) fail("Native pickup authority is unavailable");
  return p->touch(p->ctx, offer, continuation);
}
static qa_attack_provenance fd_provenance(void *ctx, qa_actor_id attacker, qa_actor_id inflictor, qa_actor_id target) {
  services_t *s = ctx; const qa_compat_q2_rerelease_foreign_damage_services *b = s->options.foreign_damage; if (b == NULL) fail("Native damage provenance is unavailable");
  return b->provenance(b->ctx, attacker, inflictor, target);
}
services_t *qa_app_sim_rerelease_guest_services_create(const qa_app_sim_rerelease_guest_services_options *options) {
  if (options->base.max_clients != floor(options->base.max_clients) || options->base.max_clients < 1 || options->base.max_clients > 256) qa_range_error("Invalid API2023 maxclients");
  double frame = options->frame_milliseconds;
  if (frame != floor(frame) || frame <= 0 || fmod(1000, frame) != 0) qa_range_error("Invalid API2023 frame duration");
  if (model_count(options->base.scene) >= 8191) qa_range_error("API2023 map exceeds model capacity");
  services_t *s = calloc(1, sizeof *s); if (s == NULL) return NULL;
  s->options = *options; s->loading = true;
  s->strings = calloc(CONFIGSTRINGS, sizeof *s->strings); s->positions = calloc(CONFIGSTRINGS, sizeof *s->positions);
  if (s->strings == NULL || s->positions == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < CONFIGSTRINGS; i++) s->positions[i] = -1;
  static const int64_t reserved[] = {255};
  if (!qa_resource_name_index_init(&s->names[0], 8192, reserved, 1) || !qa_resource_name_index_init(&s->names[1], 2048, NULL, 0) || !qa_resource_name_index_init(&s->names[2], 512, NULL, 0)) qa_range_error("Out of memory");
  if (!qa_event_ring_init(&s->messages, 8192, sizeof(qa_app_sim_classic_guest_message)) || !qa_event_ring_open_cursor(&s->messages, &s->cursor, false)) qa_range_error("Out of memory");
  qa_q2_sizebuf_init_empty(&s->buffer); qa_q2_sz_init(&s->buffer, s->buffer_data, sizeof s->buffer_data, 65536);
  qa_q2_sizebuf_init_empty(&s->scratch); qa_q2_sz_init(&s->scratch, s->scratch_data, sizeof s->scratch_data, 65536);
  seed_map(s);
  char text[32]; snprintf(text, sizeof text, "%d", (int)options->base.max_clients);
  qa_cvar_snapshot set; qa_cvar_registry_set(options->base.cvars, "maxclients", text, true, &set); qa_cvar_read_free(&set);
  s->engine_port = *options->base.engine;
  qa_compat_q2_rerelease_semantic_bindings *se = &s->semantics; se->ctx = s;
  if (options->semantic_bindings != NULL && options->semantic_bindings->project != NULL) se->project = se_project;
  if (options->semantic_bindings != NULL && options->semantic_bindings->has_generation) { se->has_generation = true; se->generation = se_generation; }
  se->bound = se_bound; se->bind = se_bind; se->foreign_address = se_foreign;
  s->messages_port.ctx = s; s->messages_port.buffer = &s->buffer; s->messages_port.accepts_client = m_accepts; s->messages_port.unicast = m_unicast; s->messages_port.multicast = m_multicast;
  s->pickups_port.ctx = s; s->pickups_port.run_source = pk_run; s->pickups_port.touch = pk_touch;
  s->damage_port.ctx = s; s->damage_port.provenance = fd_provenance;
  return s;
}
void qa_app_sim_rerelease_guest_services_host_options(services_t *s, qa_compat_q2_rerelease_q2_host_options *out) {
  out->engine = &s->engine_port; out->module_options.frame_milliseconds = s->options.frame_milliseconds; out->module_options.has_frame_milliseconds = true;
  if (s->options.base.pickups != NULL) { out->has_pickups = true; out->pickup_admission = &s->pickups_port; }
  if (s->options.foreign_damage != NULL) out->foreign_damage = &s->damage_port;
  out->semantics = s->semantics;
  out->spatial.ctx = s; out->spatial.areas_connected = sp_areas; out->spatial.visibility = sp_visibility; out->spatial.surface_id = sp_surface;
  out->spatial.box_edicts = sp_box; out->spatial.inline_model = sp_inline; out->spatial.prepare_link = prepare_link; out->spatial.link_metadata = link_metadata_of;
  out->messages = &s->messages_port;
  out->sound_ctx = s; out->sound = sound_event; out->debug_shapes_ctx = s; out->debug_shapes = ds_event; out->world_text_ctx = s; out->world_text = wt_event;
}
void qa_app_sim_rerelease_guest_services_destroy(services_t *s) {
  if (s == NULL) return;
  if (s->combat != NULL) qa_compat_q2_rerelease_combat_bindings_destroy(s->combat);
  if (s->inventory_client != NULL) qa_compat_q2_rerelease_source_client_destroy(s->inventory_client);
  clear_strings(s); free(s->strings); free(s->positions); clear_weapons(s); free(s->weapons); free(s->weapon_list);
  for (int k = 0; k < 3; k++) qa_resource_name_index_destroy(&s->names[k]);
  qa_event_ring_for_each_live(&s->messages, free_message, NULL); free_drained(s);
  qa_event_ring_close_cursor(&s->messages, &s->cursor); qa_event_ring_destroy(&s->messages);
  for (binding *b = s->bindings; b != NULL;) { binding *next = b->next; qa_compat_q2_rerelease_public_edict_destroy(b->view); free(b); b = next; }
  free(s->links); free(s->motions); free(s->edicts_scratch);
  for (int i = 0; i < 3; i++) free(s->appearance_strings[i]);
  free(s);
}
const qa_app_sim_rerelease_guest_services_options *qa_app_sim_rerelease_guest_services_options_of(const services_t *s) { return &s->options; }
void qa_app_sim_rerelease_guest_services_set_command(services_t *s, qa_app_sim_guest_command command) { s->options.base.command = command; }
qa_compat_q2_rerelease_core_services qa_app_sim_rerelease_guest_services_bind_memory(services_t *s, qa_compat_q2_memory *memory) {
  if (s->memory != NULL) fail("API2023 memory already bound");
  s->memory = memory;
  qa_compat_q2_rerelease_core_services out; memset(&out, 0, sizeof out);
  out.ctx = s; out.cvars = s->options.base.cvars; out.print = cs_print; out.get_configstring = cs_get; out.set_configstring = cs_set; out.resource_index = cs_resource;
  out.server_frame = cs_frame; out.command_arguments = cs_arguments; out.command_tail = cs_tail; out.add_command = cs_add; out.extension = cs_extension; out.invoke = cs_invoke;
  return out;
}
qa_compat_q2_memory *qa_app_sim_rerelease_guest_services_memory(services_t *s) { if (s->memory == NULL) fail("API2023 services have no guest memory"); return s->memory; }
void qa_app_sim_rerelease_guest_services_bind_host(services_t *s, qa_compat_q2_rerelease_q2_guest_host *host) {
  if (s->host != NULL || qa_compat_q2_rerelease_guest_module_get_memory(qa_compat_q2_rerelease_q2_guest_host_get_module(host)) != qa_app_sim_rerelease_guest_services_memory(s)) fail("API2023 host/memory binding mismatch");
  s->host = host; s->combat = qa_compat_q2_rerelease_combat_bindings_create(host);
}
qa_compat_q2_rerelease_q2_guest_host *qa_app_sim_rerelease_guest_services_host(services_t *s) { if (s->host == NULL) fail("API2023 services have no guest host"); return s->host; }
double qa_app_sim_rerelease_guest_services_server_frame(const services_t *s) { return s->frame; }
int qa_app_sim_rerelease_guest_services_notarget(services_t *s, double slot) {
  if (s->combat == NULL) return -1;
  bool value = false; if (!qa_compat_q2_rerelease_combat_bindings_notarget(s->combat, at_slot(s, slot), &value)) return -1;
  return value ? 1 : 0;
}
static void require_client_actor(services_t *s, double slot, qa_actor_id actor, const char *message) {
  qa_raw_entity_view record = at_slot(s, slot); qa_actor_id current;
  if (slot < 1 || slot > s->options.base.max_clients || !qa_session_actor_registry_is_live(s->options.base.engine->actors, actor) || !current_actor(record, &current) || !same_actor(current, actor)) fail(message);
}
bool qa_app_sim_rerelease_guest_services_player_grounded(services_t *s, double slot, qa_actor_id actor) {
  require_client_actor(s, slot, actor, "API2023 movement state requires the current client actor");
  motion_entry *motion = motion_of(s, actor);
  if (motion != NULL) return motion->projection.grounded(motion->projection.ctx);
  qa_compat_q2_rerelease_public_edict *view = view_of(s, slot); bool grounded = ((int64_t)qa_compat_q2_rerelease_public_edict_player_movement_flags(view) & 4) != 0;
  qa_compat_q2_rerelease_public_edict_destroy(view); return grounded;
}
qa_compat_q2_native_input_motion_view qa_app_sim_rerelease_guest_services_player_view(services_t *s, double slot, qa_actor_id actor) {
  require_client_actor(s, slot, actor, "API2023 player view requires the current client actor");
  motion_entry *motion = motion_of(s, actor);
  if (motion != NULL) return motion->projection.view(motion->projection.ctx);
  qa_compat_q2_rerelease_public_edict *view = view_of(s, slot); qa_compat_q2_rerelease_player_view state = qa_compat_q2_rerelease_public_edict_player_view(view);
  qa_compat_q2_rerelease_public_edict_destroy(view);
  qa_compat_q2_native_input_motion_view out = {{state.view_offset.x, state.view_offset.y, state.view_offset.z + state.view_height}, ((int64_t)state.movement_flags & 1) != 0};
  return out;
}
void qa_app_sim_rerelease_guest_services_set_player_view_roll(services_t *s, double slot, qa_actor_id actor, double roll) {
  qa_compat_q2_rerelease_guest_module *module = module_of(s);
  const qa_compat_q2_rerelease_client_profile *profile = &qa_compat_q2_rerelease_guest_module_require_world_profile(module)->client;
  require_client_actor(s, slot, actor, "API2023 source view requires the current client actor");
  qa_compat_q2_rerelease_public_edict *view = view_of(s, slot); qa_guest_address client; bool has = qa_compat_q2_rerelease_public_edict_pointer(view, "client", &client);
  qa_compat_q2_rerelease_public_edict_destroy(view);
  if (!has) fail("API2023 source view has no client");
  qa_compat_q2_rerelease_source_client *source = qa_compat_q2_rerelease_source_client_create(client, module, profile); if (source == NULL) qa_range_error("Out of memory");
  qa_guest_address field = qa_compat_q2_rerelease_source_client_at(source, "v_angle");
  qa_compat_q2_mem_write_f32(s->memory, qa_compat_q2_mem_offset(s->memory, field, 8), roll);
  qa_compat_q2_rerelease_source_client_destroy(source);
}
bool qa_app_sim_rerelease_guest_services_has_source_inventory(services_t *s) { return qa_compat_q2_rerelease_guest_module_get_world_profile(module_of(s)) != NULL; }
static qa_guest_address core_string(void *ctx, const char *value) { services_t *s = ctx; return qa_compat_q2_rerelease_core_imports_string(qa_compat_q2_rerelease_q2_guest_host_get_core(s->host), value); }
bool qa_app_sim_rerelease_guest_services_source_inventory(services_t *s, double slot, qa_inventory_state_binding *out) {
  qa_compat_q2_rerelease_guest_module *module = module_of(s); const qa_compat_q2_rerelease_primary_world_profile *world = qa_compat_q2_rerelease_guest_module_get_world_profile(module);
  if (world == NULL) return false;
  if (!s->has_inventory_items) { s->inventory_items = qa_compat_q2_rerelease_inventory_items(module, s, core_string, &s->inventory_item_count); s->has_inventory_items = true; }
  qa_actor_id actor; if (!current_actor(at_slot(s, slot), &actor)) fail("Native inventory requires a live source actor");
  /* TS rebinds the client on each access; the C binding reads through the source client record for this slot. */
  qa_compat_q2_rerelease_public_edict *view = view_of(s, slot); qa_guest_address client; bool has = qa_compat_q2_rerelease_public_edict_pointer(view, "client", &client);
  qa_compat_q2_rerelease_public_edict_destroy(view);
  if (!has) fail("Native inventory requires a source client");
  if (s->inventory_client != NULL) qa_compat_q2_rerelease_source_client_destroy(s->inventory_client);
  s->inventory_client = qa_compat_q2_rerelease_source_client_create(client, module, &world->client); if (s->inventory_client == NULL) qa_range_error("Out of memory");
  *out = qa_compat_q2_rerelease_source_client_inventory(s->inventory_client, s->inventory_items, s->inventory_item_count);
  return true;
}
void qa_app_sim_rerelease_guest_services_set_player_velocity_writer(services_t *s, qa_app_sim_player_velocity velocity) { s->has_velocity = true; s->velocity = velocity; }
typedef struct input_scope { services_t *s; qa_actor_id actor; bool had_previous; qa_compat_q2_native_input_motion previous; } input_scope;
static void restore_input(void *arg) {
  input_scope *scope = arg; services_t *s = scope->s; motion_entry *entry = motion_of(s, scope->actor);
  if (!scope->had_previous) { if (entry != NULL) { size_t i = (size_t)(entry - s->motions); memmove(entry, entry + 1, (s->motion_count - i - 1) * sizeof *entry); s->motion_count--; } }
  else if (entry != NULL) entry->projection = scope->previous;
}
void qa_app_sim_rerelease_guest_services_with_input_movement(services_t *s, qa_actor_id actor, const qa_compat_q2_native_input_motion *projection, void (*run)(void *), void *ctx) {
  motion_entry *entry = motion_of(s, actor); input_scope scope; memset(&scope, 0, sizeof scope); scope.s = s; scope.actor = actor;
  if (entry != NULL) { scope.had_previous = true; scope.previous = entry->projection; entry->projection = *projection; }
  else { s->motions = grow(s->motions, &s->motion_capacity, s->motion_count, sizeof *s->motions); entry = &s->motions[s->motion_count++]; memset(entry, 0, sizeof *entry); entry->actor = actor; entry->projection = *projection; }
  qa_cleanup node; qa_cleanup_push(&node, restore_input, &scope); run(ctx); qa_cleanup_pop(&node); restore_input(&scope);
}
/* TS passes the whole RereleaseGuestServicesOptions where a RereleaseGuestMapServices is expected (structural Pick). */
void qa_app_sim_rerelease_guest_map_services_of(const qa_app_sim_rerelease_guest_services_options *options, qa_app_sim_rerelease_guest_map_services *out) {
  qa_app_sim_classic_guest_map_services_of(&options->base, &out->base);
  out->localize = options->localize; out->debug_shapes = options->debug_shapes; out->world_text = options->world_text;
  out->navigation = options->navigation; out->foreign_damage = options->foreign_damage;
}
void qa_app_sim_rerelease_guest_services_validate_map(services_t *s, const qa_shared_scene_queries *scene) { (void)s; if (model_count(scene) >= 8191) qa_range_error("API2023 map exceeds model capacity"); }
void qa_app_sim_rerelease_guest_services_rebind_world(services_t *s, const qa_app_sim_rerelease_guest_map_services *b) {
  qa_app_sim_rerelease_guest_services_validate_map(s, b->base.scene);
  s->options.base.engine = b->base.engine; s->options.base.scene = b->base.scene; s->options.base.map_path = b->base.map_path; s->options.base.admit = b->base.admit;
  s->options.base.collision = b->base.collision; s->options.base.print = b->base.print; s->options.base.command = b->base.command; s->options.base.add_command = b->base.add_command;
  s->options.base.debug_graph = b->base.debug_graph; s->options.base.damage_provenance = b->base.damage_provenance; s->options.base.pickups = b->base.pickups;
  s->options.localize = b->localize; s->options.debug_shapes = b->debug_shapes; s->options.world_text = b->world_text; s->options.navigation = b->navigation; s->options.foreign_damage = b->foreign_damage;
  s->engine_port = *b->base.engine;
  s->loading = true; s->frame = 0; clear_strings(s);
  qa_event_ring_for_each_live(&s->messages, free_message, NULL); qa_event_ring_reset(&s->messages);
  s->link_count = 0; qa_q2_sz_clear(&s->buffer); seed_map(s);
}
void qa_app_sim_rerelease_guest_services_complete_spawn(services_t *s) { s->loading = false; }
void qa_app_sim_rerelease_guest_services_begin_frame(services_t *s, double frame) {
  if (frame != floor(frame) || frame < 0 || frame > 4294967295.0) qa_range_error("Invalid API2023 frame");
  s->frame = frame;
  for (size_t i = 0; i < s->link_count;) { if (!qa_session_actor_registry_is_live(s->options.base.engine->actors, s->links[i].actor)) delete_link(s, s->links[i].actor); else i++; }
}
static void write_number(services_t *s, qa_compat_q2_rerelease_public_edict *view, double slot) { qa_compat_q2_mem_write_u32(s->memory, qa_compat_q2_rerelease_public_edict_address(view, "s.number"), (uint32_t)slot); }
qa_q2_rerelease_entity_state qa_app_sim_rerelease_guest_services_entity_state(services_t *s, double slot) {
  qa_compat_q2_rerelease_public_edict *view = view_of(s, slot); write_number(s, view, slot);
  qa_q2_rerelease_entity_state state = qa_compat_q2_rerelease_public_edict_state(view); qa_compat_q2_rerelease_public_edict_destroy(view); return state;
}
qa_q2_rerelease_player_state qa_app_sim_rerelease_guest_services_player_state(services_t *s, double slot) {
  qa_compat_q2_rerelease_public_edict *view = view_of(s, slot); qa_q2_rerelease_player_state state = qa_compat_q2_rerelease_public_edict_player_state(view);
  qa_compat_q2_rerelease_public_edict_destroy(view); return state;
}
double qa_app_sim_rerelease_guest_services_player_ping(services_t *s, double slot) { qa_compat_q2_rerelease_public_edict *v = view_of(s, slot); double p = qa_compat_q2_rerelease_public_edict_ping(v); qa_compat_q2_rerelease_public_edict_destroy(v); return p; }
void qa_app_sim_rerelease_guest_services_set_player_ping(services_t *s, double slot, double ping) { qa_compat_q2_rerelease_public_edict *v = view_of(s, slot); qa_compat_q2_rerelease_public_edict_set_ping(v, ping); qa_compat_q2_rerelease_public_edict_destroy(v); }
qa_app_sim_rerelease_entity_info qa_app_sim_rerelease_guest_services_entity_info(services_t *s, double slot) {
  qa_compat_q2_rerelease_public_edict *view = view_of(s, slot); qa_raw_entity_view record = qa_compat_q2_rerelease_public_edict_get_record(view);
  qa_app_sim_rerelease_entity_info info; memset(&info, 0, sizeof info);
  info.has_actor = host_actor(s, record, &info.actor);
  link_metadata *link = info.has_actor ? link_of(s, info.actor) : NULL;
  info.active = qa_compat_q2_rerelease_public_edict_byte(view, "inuse") != 0; info.server_flags = qa_compat_q2_rerelease_public_edict_uint(view, "svflags");
  info.areas[0] = qa_compat_q2_rerelease_public_edict_int(view, "areanum"); info.areas[1] = qa_compat_q2_rerelease_public_edict_int(view, "areanum2");
  if (link != NULL) { info.clusters_null = link->clusters_null; info.clusters = link->clusters; info.cluster_count = link->cluster_count; info.first_cluster = link->first_cluster; info.headnode = link->headnode; }
  qa_guest_address owner;
  if (qa_compat_q2_rerelease_public_edict_pointer(view, "owner", &owner)) { info.has_owner_slot = true; info.owner_slot = from_pointer(s, owner).slot; }
  qa_compat_q2_rerelease_public_edict_destroy(view); return info;
}
static const char *keep(services_t *s, int index, char *text) { free(s->appearance_strings[index]); s->appearance_strings[index] = text; return text; }
static char *join(const char *const *parts, size_t count) {
  size_t n = 0; for (size_t i = 0; i < count; i++) n += strlen(parts[i]);
  char *out = malloc(n + 1); if (out == NULL) qa_range_error("Out of memory"); out[0] = '\0'; for (size_t i = 0; i < count; i++) strcat(out, parts[i]); return out;
}
static qa_app_sim_model_appearance appearance(services_t *s, const double model_indexes[4], double skin) {
  qa_app_sim_model_appearance out; memset(&out, 0, sizeof out); out.attached_model_count = 3;
  bool every = true; for (int i = 0; i < 4; i++) every = every && model_indexes[i] != 255;
  if (every) {
    out.path = qa_app_sim_rerelease_guest_services_resource(s, "model", model_indexes[0]); out.skin = skin;
    for (int i = 0; i < 3; i++) out.attached_models[i] = qa_app_sim_rerelease_guest_services_resource(s, "model", model_indexes[i + 1]);
    return out;
  }
  const char *value = configstring(s, 11582 + (qa_js_to_int32(skin) & 255)); if (value == NULL) value = "player\\male/grunt";
  const char *backslash = strchr(value, '\\'), *look = backslash == NULL ? value : backslash + 1, *slash = strchr(look, '/');
  char *model; if (slash == NULL) model = dup_text("male"); else { model = malloc((size_t)(slash - look) + 1); if (model == NULL) qa_range_error("Out of memory"); memcpy(model, look, (size_t)(slash - look)); model[slash - look] = '\0'; }
  const char *skin_name = slash == NULL ? "grunt" : slash + 1;
  uint32_t weapon_index = (qa_js_to_uint32(skin) >> 8) & 255;
  const char *weapon = weapon_index < s->weapon_list_count ? s->weapon_list[weapon_index] : "weapon.md2";
  bool player = model_indexes[0] == 255;
  const char *tris[] = {"players/", model, "/tris.md2"}, *skin_parts[] = {"players/", model, "/", skin_name, ".pcx"}, *weapon_parts[] = {"players/", model, "/", weapon};
  out.path = player ? keep(s, 0, join(tris, 3)) : qa_app_sim_rerelease_guest_services_resource(s, "model", model_indexes[0]);
  out.skin = player ? 0 : skin; out.skin_path = player ? keep(s, 1, join(skin_parts, 5)) : NULL;
  keep(s, 2, join(weapon_parts, 4));
  for (int i = 0; i < 3; i++) out.attached_models[i] = model_indexes[i + 1] == 255 ? s->appearance_strings[2] : qa_app_sim_rerelease_guest_services_resource(s, "model", model_indexes[i + 1]);
  free(model); return out;
}
qa_app_sim_model_appearance qa_app_sim_rerelease_guest_services_model_appearance(services_t *s, double slot) {
  qa_compat_q2_rerelease_public_edict *view = view_of(s, slot); write_number(s, view, slot);
  qa_compat_q2_rerelease_model_state state = qa_compat_q2_rerelease_public_edict_model_state(view); qa_compat_q2_rerelease_public_edict_destroy(view);
  return appearance(s, state.model_indexes, state.skin);
}
void qa_app_sim_rerelease_guest_services_publish_entities(services_t *s) {
  qa_raw_entity_table table = table_of(s); qa_q2_foundation_host *engine = s->options.base.engine;
  for (double slot = 1; slot < table.count; slot++) {
    qa_raw_entity_view record = table.at_slot(table.ctx, slot); qa_actor_id actor; if (!host_actor(s, record, &actor)) continue;
    qa_compat_q2_rerelease_public_edict *view = qa_compat_q2_rerelease_public_edict_create(s->memory, record); write_number(s, view, slot);
    qa_q2_rerelease_entity_state state = qa_compat_q2_rerelease_public_edict_state(view);
    qa_app_sim_model_appearance look = appearance(s, state.base.model_indexes, state.base.skin);
    if (state.effects > (uint64_t)9007199254740991ULL) qa_range_error("Shared model effect projection requires a lossless API2023 effect value");
    qa_q2_presentation_event visibility; memset(&visibility, 0, sizeof visibility); visibility.kind = "visibility"; visibility.has_actor = true; visibility.actor = actor;
    visibility.visible = qa_compat_q2_rerelease_public_edict_byte(view, "inuse") != 0 && ((int64_t)qa_compat_q2_rerelease_public_edict_uint(view, "svflags") & 1) == 0;
    engine->emit(engine->ctx, visibility);
    qa_q2_presentation_event model; memset(&model, 0, sizeof model); model.kind = "model"; model.has_actor = true; model.actor = actor; model.path = look.path;
    for (int i = 0; i < 3; i++) model.attached_models[i] = look.attached_models[i];
    model.frame = state.base.frame; model.old_frame = state.old_frame; model.scale = state.scale == 0 ? 1 : state.scale;
    model.alpha = state.alpha == 0 ? ((qa_js_to_int32(state.base.render_effects) & 32) != 0 ? 0.3 : 1) : state.alpha;
    model.skin = look.skin; model.effects = (double)state.effects; model.render_flags = state.base.render_effects;
    engine->emit(engine->ctx, model);
    if (state.base.event != 0) { qa_q2_presentation_event e; memset(&e, 0, sizeof e); e.kind = "entity-event"; e.has_actor = true; e.actor = actor; e.event = state.base.event; engine->emit(engine->ctx, e); }
    qa_compat_q2_rerelease_public_edict_destroy(view);
  }
}
