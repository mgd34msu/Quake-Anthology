/* Port of src/app/bootstrap/simulation/classic-guest-services.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/classic_guest_services.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/combat_binding.h"
#include "qa/compat/q2/classic/combat_profile.h"
#include "qa/compat/q2/classic/inventory.h"
#include "qa/compat/q2/classic/pmove.h"
#include "qa/compat/q2/classic/records.h"
#include "qa/core/resource_name_index.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/message.h"

#define CONFIGSTRINGS 2080
static const double resource_bases[3] = {32, 288, 544};
static const char *const resource_kinds[3] = {"model", "sound", "image"};
static const qa_vec3 zero = {0, 0, 0};

typedef struct body_binding { struct qa_app_sim_classic_guest_services *services; qa_raw_entity_view record; qa_owned_actor actor; qa_actor_id ground; struct body_binding *next; } body_binding;
typedef struct motion_entry { qa_actor_id actor; qa_compat_q2_native_input_motion projection; bool has_body; qa_body_state body; qa_actor_id ground; } motion_entry;
typedef struct proxy_entry { qa_actor_id actor; qa_raw_entity_view view; double health; } proxy_entry;
typedef struct pending_proxy { double slot; qa_raw_entity_view view; } pending_proxy;

struct qa_app_sim_classic_guest_services {
  qa_compat_q2_memory *memory;
  qa_app_sim_classic_guest_services_options options;
  qa_compat_q2_classic_engine_services services;
  /* TS `get engine()` reads the current options: the host keeps this port, refreshed on rebind. */
  qa_q2_foundation_host engine_port;
  qa_original_pickup_admission pickups_port;
  qa_compat_q2_classic_damage_provenance provenance_port;
  qa_app_sim_configstring *strings; size_t string_count; int32_t positions[CONFIGSTRINGS];
  qa_resource_name_index names[3];
  qa_event_ring messages; qa_event_cursor cursor;
  qa_app_sim_classic_guest_message *drained; size_t drained_count;
  uint8_t buffer_data[1400]; qa_q2_sizebuf buffer;
  qa_compat_q2_classic_guest_host *host;
  qa_compat_q2_classic_combat_bindings *combat, *disposed_combat;
  qa_compat_q2_classic_source_inventory *inventory;
  const qa_equipment_movement *player_movement;
  bool has_velocity; qa_app_sim_player_velocity velocity;
  motion_entry *motions; size_t motion_count, motion_capacity;
  pending_proxy *pending; size_t pending_count, pending_capacity;
  proxy_entry *proxies; size_t proxy_count, proxy_capacity;
  /* TS #proxyActors: guest slot to shared monster, for routing guest T_Damage on a stand-in to shared combat. */
  struct { bool set; qa_actor_id actor; } *proxy_actors; size_t proxy_actor_capacity;
  body_binding *bindings;
  bool loading;
  double clusters[16]; qa_actor_id *edicts_scratch; size_t edicts_capacity;
  char *appearance_strings[4];
  double stats[32];
  char *command_args;
};
typedef qa_app_sim_classic_guest_services services_t;
static bool stand_in_actor(void *ctx, qa_raw_entity_view record, qa_actor_id *out);
static void set_proxy_actor(services_t *s, double slot, bool set, qa_actor_id actor);

static void *grow(void *items, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return items;
  size_t next = *capacity == 0 ? 8 : *capacity * 2;
  void *grown = realloc(items, next * size); if (grown == NULL) qa_range_error("Out of memory");
  *capacity = next; return grown;
}
static char *dup_text(const char *text) { size_t n = strlen(text); char *p = malloc(n + 1); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, text, n + 1); return p; }
static bool same_actor(qa_actor_id a, qa_actor_id b) { return a.slot == b.slot && a.generation == b.generation; }
static size_t model_count(const qa_shared_scene_queries *scene) {
  switch (scene->geometry.kind) {
    case QA_DECODED_WORLD_Q1: return scene->geometry.as.q1->model_count;
    case QA_DECODED_WORLD_Q2: return scene->geometry.as.q2->model_count;
    default: return scene->geometry.as.q3->model_count;
  }
}
static qa_vec3 vector(const uint8_t *view, size_t offset) {
  qa_vec3 out = {qa_compat_q2_view_f32(view, offset), qa_compat_q2_view_f32(view, offset + 4), qa_compat_q2_view_f32(view, offset + 8)}; return out;
}
static void store_vector(const uint8_t *view, size_t offset, qa_vec3 value) {
  uint8_t *bytes = (uint8_t *)view; /* the live edict view (TS DataView) */
  qa_compat_q2_view_set_f32(bytes, offset, value.x); qa_compat_q2_view_set_f32(bytes, offset + 4, value.y); qa_compat_q2_view_set_f32(bytes, offset + 8, value.z);
}
static void tuple(qa_vec3 value, qa_q2_wire_vec3 out) { out[0] = (float)value.x; out[1] = (float)value.y; out[2] = (float)value.z; }
static int kind_index(const char *kind) {
  for (int i = 0; i < 3; i++) if (strcmp(kind, resource_kinds[i]) == 0) return i;
  qa_range_error("Invalid API 3 resource kind %s", kind);
  return 0;
}
static bool current_actor(qa_raw_entity_view record, qa_actor_id *out) {
  qa_actor_id_result result = record.current_actor(record.ctx); if (result.has_value) *out = result.value; return result.has_value;
}
static qa_compat_q2_classic_edicts *edicts(services_t *s) { return qa_compat_q2_classic_guest_host_get_edicts(qa_app_sim_classic_guest_services_host(s)); }
static motion_entry *motion_of(services_t *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->motion_count; i++) if (same_actor(s->motions[i].actor, actor)) return &s->motions[i];
  return NULL;
}
static bool accepts_client(services_t *s, double slot) {
  if (s->options.accepts_client.accepts_client != NULL) return s->options.accepts_client.accepts_client(s->options.accepts_client.ctx, slot);
  return slot >= 1 && slot <= s->options.max_clients;
}

/* --- configstrings --- */
static const char *configstring(services_t *s, double index) {
  if (!(index >= 0 && index < CONFIGSTRINGS) || index != floor(index)) return NULL;
  int32_t position = s->positions[(size_t)index];
  return position < 0 ? NULL : s->strings[position].value;
}
static void store_configstring(services_t *s, double index, const char *value) {
  int32_t position = s->positions[(size_t)index];
  char *copy = dup_text(value);
  if (position >= 0) { free((char *)s->strings[position].value); s->strings[position].value = copy; }
  else {
    s->strings[s->string_count].index = index; s->strings[s->string_count].value = copy;
    s->positions[(size_t)index] = (int32_t)s->string_count++;
  }
  for (int kind = 0; kind < 3; kind++) {
    double slot = index - resource_bases[kind];
    if (slot > 0 && slot < 256) { qa_resource_name_index_set(&s->names[kind], (int64_t)slot, copy); break; }
  }
}
static void clear_configstrings(services_t *s) {
  for (size_t i = 0; i < s->string_count; i++) free((char *)s->strings[i].value);
  s->string_count = 0; for (size_t i = 0; i < CONFIGSTRINGS; i++) s->positions[i] = -1;
}
const char *qa_app_sim_classic_guest_services_resource(services_t *s, const char *kind, double index) {
  const char *value = configstring(s, resource_bases[kind_index(kind)] + index); return value == NULL ? "" : value;
}

/* --- messages --- */
static void free_drained(services_t *s) {
  for (size_t i = 0; i < s->drained_count; i++) free(s->drained[i].bytes);
  free(s->drained); s->drained = NULL; s->drained_count = 0;
}
static void free_message(const void *record, int64_t id, void *ctx) { (void)id; (void)ctx; free(((const qa_app_sim_classic_guest_message *)record)->bytes); }
static void admit_message(services_t *s, qa_app_sim_classic_guest_audience audience, bool reliable, const uint8_t *bytes, size_t length) {
  qa_app_sim_classic_guest_message message; memset(&message, 0, sizeof message);
  message.audience = audience; message.reliable = reliable; message.length = length;
  message.bytes = malloc(length == 0 ? 1 : length); if (message.bytes == NULL) qa_range_error("Out of memory");
  if (length != 0) memcpy(message.bytes, bytes, length);
  (void)qa_event_ring_admit(&s->messages, &message);
}
static void enqueue(services_t *s, qa_app_sim_classic_guest_audience audience, bool reliable) {
  admit_message(s, audience, reliable, s->buffer.data, (size_t)s->buffer.cursize); qa_q2_sz_clear(&s->buffer);
}
static void multicast(services_t *s, qa_vec3 origin, double destination) {
  if (destination != floor(destination) || destination < 0 || destination > 5) qa_range_error("API 3 invalid multicast destination");
  int mode = (int)destination % 3;
  qa_app_sim_classic_guest_audience audience = {false, 0, origin, mode == 0 ? "all" : mode == 1 ? "phs" : "pvs"};
  enqueue(s, audience, destination >= 3);
}
static void unicast(services_t *s, bool has_entity, qa_guest_address entity, bool reliable) {
  if (!has_entity) return;
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_from_pointer(edicts(s), entity);
  if (!accepts_client(s, record.slot)) return;
  qa_app_sim_classic_guest_audience audience = {true, record.slot, zero, NULL};
  enqueue(s, audience, reliable);
}
const qa_app_sim_classic_guest_message *qa_app_sim_classic_guest_services_drain_messages(services_t *s, size_t *out_count) {
  free_drained(s);
  s->drained = qa_event_ring_take(&s->messages, &s->cursor, &s->drained_count); qa_event_ring_retire(&s->messages);
  *out_count = s->drained_count; return s->drained;
}

/* --- configstring writes --- */
void qa_app_sim_classic_guest_services_set_configstring(services_t *s, double index, const char *value) {
  if (index != floor(index) || index < 0 || index >= CONFIGSTRINGS) qa_range_error("API 3 configstring index outside MAX_CONFIGSTRINGS");
  store_configstring(s, index, value);
  const char *text = configstring(s, index);
  if (index > 32 && index < 288 && s->host != NULL) qa_compat_q2_classic_guest_host_set_model_name(s->host, index - 32, text);
  if (index >= 800 && index < 1056) {
    qa_q2_presentation_event event; memset(&event, 0, sizeof event); event.kind = "lightstyle"; event.style = index - 800; event.pattern = text;
    s->options.engine->emit(s->options.engine->ctx, event);
  }
  if (index == 1) { qa_q2_presentation_event event; memset(&event, 0, sizeof event); event.kind = "music"; event.track = text; s->options.engine->emit(s->options.engine->ctx, event); }
  if (!s->loading) {
    qa_q2_sz_clear(&s->buffer); qa_q2_msg_write_byte(&s->buffer, 13); qa_q2_msg_write_short(&s->buffer, index); qa_q2_msg_write_string(&s->buffer, text);
    multicast(s, zero, 3);
  }
}
double qa_app_sim_classic_guest_services_resource_index(services_t *s, const char *kind, const char *name) {
  int k = kind_index(kind);
  int64_t index = qa_resource_name_index_find(&s->names[k], name);
  if (index < 0) qa_raise("Error", 0, 0, "API 3 %s index overflow", kind);
  if (index != 0 && strcmp(qa_app_sim_classic_guest_services_resource(s, kind, (double)index), name) != 0)
    qa_app_sim_classic_guest_services_set_configstring(s, resource_bases[k] + (double)index, name);
  return (double)index;
}

/* --- bodies --- */
static qa_body_state body(services_t *s, qa_raw_entity_view record, qa_actor_id *ground) {
  qa_actor_id cached_actor; bool has_cached = current_actor(record, &cached_actor);
  if (has_cached) { motion_entry *cached = motion_of(s, cached_actor); if (cached != NULL && cached->has_body) return cached->body; }
  const uint8_t *view = record.bytes.bytes;
  const uint8_t *client = record.slot > 0 && record.slot <= s->options.max_clients ? qa_compat_q2_classic_edicts_client_prefix(edicts(s), record.slot) : NULL;
  qa_actor_id actor; bool has_actor = current_actor(record, &actor);
  qa_vec3 pending; bool has_pending = has_actor && s->has_velocity && s->velocity.read(s->velocity.ctx, actor, &pending);
  motion_entry *motion = has_actor ? motion_of(s, actor) : NULL;
  qa_compat_q2_native_input_motion_state moved; if (motion != NULL) moved = motion->projection.read(motion->projection.ctx);
  qa_vec3 velocity = motion != NULL ? moved.velocity : has_pending ? pending : client == NULL ? zero
    : (qa_vec3){qa_compat_q2_view_i16(client, 10) * 0.125, qa_compat_q2_view_i16(client, 12) * 0.125, qa_compat_q2_view_i16(client, 14) * 0.125};
  /* Clients ground on the world while their pmove state says so; the guest's private groundentity is outside the public edict prefix. */
  bool grounded = client != NULL && (client[16] & 4) != 0;
  qa_body_state out; memset(&out, 0, sizeof out);
  out.origin = motion != NULL ? moved.origin : vector(view, 4); out.angles = vector(view, 16); out.velocity = velocity;
  out.bounds.min = vector(view, 188); out.bounds.max = vector(view, 200);
  if (grounded && current_actor(qa_compat_q2_classic_edicts_at(edicts(s), 0), ground)) out.ground = ground;
  return out;
}
static qa_body_state binding_read(void *ctx) { body_binding *b = ctx; return body(b->services, b->record, &b->ground); }
static void binding_write(void *ctx, qa_body_state state) {
  body_binding *b = ctx; services_t *s = b->services; qa_actor_id scratch;
  qa_vec3 velocity = body(s, b->record, &scratch).velocity;
  motion_entry *motion = motion_of(s, b->actor.id);
  if (motion != NULL) {
    motion->has_body = true; motion->body = state;
    if (state.ground != NULL) { motion->ground = *state.ground; motion->body.ground = &motion->ground; }
    qa_compat_q2_native_input_motion_state value = {state.origin, state.velocity};
    motion->projection.write(motion->projection.ctx, value);
  } else if (state.velocity.x != velocity.x || state.velocity.y != velocity.y || state.velocity.z != velocity.z) {
    if (b->record.slot < 1 || b->record.slot > s->options.max_clients || !s->has_velocity)
      qa_raise("Error", 0, 0, "API3 velocity writes require a source semantic binding");
    s->velocity.write(s->velocity.ctx, b->actor, state.velocity);
  }
  if (motion == NULL) store_vector(b->record.bytes.bytes, 4, state.origin);
  store_vector(b->record.bytes.bytes, 16, state.angles);
  store_vector(b->record.bytes.bytes, 188, state.bounds.min); store_vector(b->record.bytes.bytes, 200, state.bounds.max);
}
static void bind_entity(void *ctx, qa_raw_entity_view record, qa_owned_actor actor) {
  services_t *s = ctx; body_binding *b = calloc(1, sizeof *b); if (b == NULL) qa_range_error("Out of memory");
  b->services = s; b->record = record; b->actor = actor; b->next = s->bindings; s->bindings = b;
  qa_body_state_binding binding = {b, binding_read, binding_write, NULL};
  qa_shared_body_table_bind(s->options.engine->bodies, actor, binding);
  if (s->combat != NULL) qa_compat_q2_classic_combat_bindings_bind(s->combat, record, actor);
  s->options.admit.admit(s->options.admit.ctx, record, actor);
}
static void link_body(void *ctx, qa_raw_entity_view record, qa_owned_actor actor) {
  services_t *s = ctx; const uint8_t *view = record.bytes.bytes;
  int32_t solid = qa_compat_q2_view_i32(view, 248), flags = qa_compat_q2_view_i32(view, 184);
  qa_guest_address owner_address; qa_owned_actor owner; bool has_owner = false;
  if (qa_compat_q2_mem_read_pointer(s->memory, qa_compat_q2_mem_offset(s->memory, record.address, 256), &owner_address))
    has_owner = qa_compat_q2_classic_edicts_observe(edicts(s), owner_address, &owner);
  const char *model_name = qa_app_sim_classic_guest_services_resource(s, "model", qa_compat_q2_view_i32(view, 40));
  bool dead_monster = (flags & 2) != 0, monster = (flags & 4) != 0;
  qa_actor_collision collision; memset(&collision, 0, sizeof collision);
  collision.family = "q2"; collision.shape_kind = solid == 3 ? "model" : "box";
  if (solid == 3) collision.shape_model = qa_js_string_to_number(model_name[0] == '\0' ? "" : model_name + 1);
  collision.contents = solid == 3 ? 1 : dead_monster ? 0x4000000 : 0x2000000;
  collision.owner = has_owner ? &owner.id : NULL; collision.role = solid == 1 ? "trigger" : "solid";
  collision.monster = monster; collision.dead_monster = dead_monster;
  s->options.collision.collision(s->options.collision.ctx, actor, collision);
  qa_actor_id ground; qa_body_link_state link; memset(&link, 0, sizeof link);
  link.link_count = qa_compat_q2_view_i32(view, 92); link.has_linked = true;
  link.linked.actor = actor.id; link.linked.state = body(s, record, &ground);
  link.linked.absolute_bounds.min = vector(view, 212); link.linked.absolute_bounds.max = vector(view, 224);
  qa_shared_body_table_restore_link_state(s->options.engine->bodies, actor, link);
}
qa_compat_q2_classic_world_link qa_app_sim_classic_guest_services_world_link(services_t *s, qa_bounds bounds) {
  qa_leaf_query_result query = qa_shared_box_leaves(s->options.scene, bounds, 128);
  size_t count = 0; bool too_many = false; double first = 0, second = 0;
  for (size_t i = 0; i < query.leaves.count; i++) {
    double leaf = query.leaves.values[i];
    double area = qa_shared_leaf_area(s->options.scene, leaf), cluster = qa_shared_leaf_cluster(s->options.scene, leaf);
    if (area != 0) { if (first != 0 && first != area) second = area; else first = area; }
    if (cluster >= 0) {
      bool seen = false; for (size_t c = 0; c < count && !seen; c++) seen = s->clusters[c] == cluster;
      if (!seen) { if (count < 16) s->clusters[count++] = cluster; else too_many = true; }
    }
  }
  qa_compat_q2_classic_world_link link; memset(&link, 0, sizeof link);
  link.clusters_null = query.overflow || query.leaves.count >= 128 || too_many;
  link.clusters = link.clusters_null ? NULL : s->clusters; link.cluster_count = link.clusters_null ? 0 : count;
  link.headnode = query.has_topnode ? query.topnode : 0; link.areas[0] = first; link.areas[1] = second;
  return link;
}

/* --- engine services table --- */
/* TS dispose closes the bindings and drops them; the closed bindings' storage
 * stays until destroy, since bound actor callbacks may still hold it (as TS closures). */
static void dispose(void *ctx) {
  services_t *s = ctx; if (s->combat == NULL) return;
  qa_compat_q2_classic_combat_bindings_close(s->combat);
  if (s->disposed_combat != NULL) qa_compat_q2_classic_combat_bindings_destroy(s->disposed_combat);
  s->disposed_combat = s->combat; s->combat = NULL;
}
static void *pickups_run(void *ctx, qa_original_pickup_offer offer, void *(*execute)(void *, qa_source_pickup_selection, qa_source_pickup_lifetime), void *execute_ctx) {
  services_t *s = ctx; const qa_original_pickup_admission *pickups = s->options.pickups;
  if (pickups == NULL) qa_raise("Error", 0, 0, "Native pickup authority disappeared during travel");
  return pickups->run_source(pickups->ctx, offer, execute, execute_ctx);
}
static qa_original_pickup_outcome pickups_touch(void *ctx, qa_original_pickup_offer offer, qa_original_pickup_continuation continuation) {
  services_t *s = ctx; const qa_original_pickup_admission *pickups = s->options.pickups;
  if (pickups == NULL) qa_raise("Error", 0, 0, "Native pickup authority disappeared during travel");
  return pickups->touch(pickups->ctx, offer, continuation);
}
static qa_attack_provenance provenance(void *ctx, qa_actor_id attacker, qa_actor_id inflictor, qa_actor_id target) {
  services_t *s = ctx; const qa_compat_q2_classic_damage_provenance *port = s->options.damage_provenance;
  if (port == NULL) qa_raise("Error", 0, 0, "Native damage provenance disappeared during travel");
  return port->provenance(port->ctx, attacker, inflictor, target);
}
static void engine_print(void *ctx, const char *destination, bool has_entity, qa_guest_address entity, double level, const char *text) {
  services_t *s = ctx; bool debug = strcmp(destination, "debug") == 0, broadcast = strcmp(destination, "broadcast") == 0, center = strcmp(destination, "center") == 0;
  if (debug || (strcmp(destination, "client") == 0 && !has_entity)) { s->options.print.print(s->options.print.ctx, text); return; }
  if (!broadcast && !has_entity) return;
  double slot = 0;
  if (!broadcast && has_entity) {
    slot = qa_compat_q2_classic_edicts_from_pointer(edicts(s), entity).slot;
    if (!accepts_client(s, slot)) { if (center) return; qa_raise("Error", 0, 0, "API 3 cprintf on non-client edict"); }
  }
  uint8_t data[1400]; qa_q2_sizebuf packet; qa_q2_sizebuf_init_empty(&packet); qa_q2_sz_init(&packet, data, sizeof data, 1400);
  qa_q2_msg_write_byte(&packet, center ? 15 : 10);
  if (!center) qa_q2_msg_write_byte(&packet, level);
  qa_q2_msg_write_string(&packet, text);
  qa_app_sim_classic_guest_audience audience = {false, 0, zero, "all"};
  if (!broadcast) { audience.unicast = true; audience.scope = NULL; audience.slot = slot; }
  admit_message(s, audience, true, packet.data, (size_t)packet.cursize);
  if (broadcast) s->options.print.print(s->options.print.ctx, text);
}
static void engine_configstring(void *ctx, double index, const char *value) { qa_app_sim_classic_guest_services_set_configstring(ctx, index, value); }
static double engine_resource_index(void *ctx, const char *kind, const char *name) { return qa_app_sim_classic_guest_services_resource_index(ctx, kind, name); }
void qa_app_sim_classic_guest_services_sound(services_t *s, const qa_vec3 *explicit_origin, bool has_entity, qa_guest_address entity,
                                             double channel, double index, double volume, double attenuation, double offset) {
  if (volume < 0 || volume > 1 || attenuation < 0 || attenuation > 4 || offset < 0 || offset > 0.255) qa_range_error("API 3 sound parameters outside source ranges");
  if (!has_entity) qa_raise("Error", 0, 0, "API 3 sound requires an entity");
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_from_pointer(edicts(s), entity); const uint8_t *view = record.bytes.bytes;
  int32_t solid = qa_compat_q2_view_i32(view, 248); qa_actor_id ground; qa_bounds bounds = body(s, record, &ground).bounds;
  qa_vec3 origin;
  if (explicit_origin != NULL) origin = *explicit_origin;
  else if (solid == 3) {
    origin.x = vector(view, 4).x + (bounds.min.x + bounds.max.x) * 0.5; origin.y = vector(view, 4).y + (bounds.min.y + bounds.max.y) * 0.5;
    origin.z = vector(view, 4).z + (bounds.min.z + bounds.max.z) * 0.5;
  } else origin = vector(view, 4);
  bool positioned = explicit_origin != NULL || (qa_compat_q2_view_i32(view, 184) & 1) != 0 || solid == 3;
  int flags = 8 | (volume != 1 ? 1 : 0) | (attenuation != 1 ? 2 : 0) | (positioned ? 4 : 0) | (offset != 0 ? 16 : 0);
  qa_q2_msg_write_byte(&s->buffer, 9); qa_q2_msg_write_byte(&s->buffer, flags); qa_q2_msg_write_byte(&s->buffer, index);
  if ((flags & 1) != 0) qa_q2_msg_write_byte(&s->buffer, volume * 255);
  if ((flags & 2) != 0) qa_q2_msg_write_byte(&s->buffer, attenuation * 64);
  if ((flags & 16) != 0) qa_q2_msg_write_byte(&s->buffer, offset * 1000);
  int32_t ch = qa_js_to_int32(channel);
  qa_q2_msg_write_short(&s->buffer, (double)(qa_js_to_int32(record.slot) << 3 | (ch & 7)));
  if (positioned) { qa_q2_wire_vec3 wire; tuple(origin, wire); qa_q2_msg_write_pos(&s->buffer, wire); }
  multicast(s, origin, ((ch & 8) != 0 || attenuation == 0 ? 0 : 1) + ((ch & 8) == 0 && (ch & 16) != 0 ? 3 : 0));
}
static void engine_sound(void *ctx, const qa_vec3 *origin, bool has_entity, qa_guest_address entity, double channel, double index, double volume, double attenuation, double offset) {
  qa_app_sim_classic_guest_services_sound(ctx, origin, has_entity, entity, channel, index, volume, attenuation, offset);
}
static bool engine_areas_connected(void *ctx, double first, double second) { services_t *s = ctx; return qa_shared_areas_connected(s->options.scene, first, second); }
static qa_compat_q2_classic_world_link engine_world_link(void *ctx, qa_bounds bounds) { return qa_app_sim_classic_guest_services_world_link(ctx, bounds); }
static const qa_actor_id *engine_box_edicts(void *ctx, qa_bounds bounds, const char *kind, size_t *out_count) {
  services_t *s = ctx; size_t count = 0;
  const qa_spatial_actor *found = qa_shared_query_actors(s->options.scene, bounds, kind, &count);
  if (count > s->edicts_capacity) { qa_actor_id *grown = realloc(s->edicts_scratch, count * sizeof *grown); if (grown == NULL) qa_range_error("Out of memory"); s->edicts_scratch = grown; s->edicts_capacity = count; }
  for (size_t i = 0; i < count; i++) s->edicts_scratch[i] = found[i].body.actor;
  *out_count = count; return s->edicts_scratch;
}
static void engine_message(void *ctx, const char *operation, const qa_guest_call_value *values, size_t count, const qa_guest_call_context *context) {
  services_t *s = ctx; (void)context; qa_guest_address address; qa_q2_wire_vec3 wire;
  if (strcmp(operation, "WriteChar") == 0) qa_q2_msg_write_char(&s->buffer, qa_compat_q2_classic_number(values, count, 0));
  else if (strcmp(operation, "WriteByte") == 0) qa_q2_msg_write_byte(&s->buffer, qa_compat_q2_classic_number(values, count, 0));
  else if (strcmp(operation, "WriteShort") == 0) qa_q2_msg_write_short(&s->buffer, qa_compat_q2_classic_number(values, count, 0));
  else if (strcmp(operation, "WriteLong") == 0) qa_q2_msg_write_long(&s->buffer, qa_compat_q2_classic_number(values, count, 0));
  else if (strcmp(operation, "WriteFloat") == 0) qa_q2_msg_write_float(&s->buffer, qa_compat_q2_classic_number(values, count, 0));
  else if (strcmp(operation, "WriteString") == 0) {
    bool has = qa_compat_q2_classic_pointer(values, count, 0, &address);
    char *text = has ? qa_compat_q2_read_classic_string(s->memory, true, address, 65536) : dup_text("");
    qa_q2_msg_write_string(&s->buffer, text); free(text);
  } else if (strcmp(operation, "WritePosition") == 0) {
    tuple(qa_compat_q2_read_classic_vector(s->memory, qa_compat_q2_classic_required_pointer(values, count, 0)), wire); qa_q2_msg_write_pos(&s->buffer, wire);
  } else if (strcmp(operation, "WriteDir") == 0) {
    if (!qa_compat_q2_classic_pointer(values, count, 0, &address)) qa_q2_msg_write_byte(&s->buffer, 0);
    else { tuple(qa_compat_q2_read_classic_vector(s->memory, address), wire); qa_q2_msg_write_dir(&s->buffer, wire); }
  } else if (strcmp(operation, "WriteAngle") == 0) qa_q2_msg_write_angle(&s->buffer, qa_compat_q2_classic_number(values, count, 0));
  else if (strcmp(operation, "multicast") == 0) multicast(s, qa_compat_q2_read_classic_vector(s->memory, qa_compat_q2_classic_required_pointer(values, count, 0)), qa_compat_q2_classic_number(values, count, 1));
  else if (strcmp(operation, "unicast") == 0) { bool has = qa_compat_q2_classic_pointer(values, count, 0, &address); unicast(s, has, address, qa_compat_q2_classic_number(values, count, 1) != 0); }
  else qa_raise("Error", 0, 0, "Invalid API 3 message operation %s", operation);
}
static const char *const *engine_command(void *ctx, size_t *out_count, const char **out_args) { services_t *s = ctx; return s->options.command.command(s->options.command.ctx, out_count, out_args); }
static void engine_add_command(void *ctx, const char *text) { services_t *s = ctx; s->options.add_command.add_command(s->options.add_command.ctx, text); }
static void engine_debug_graph(void *ctx, double value, double color) { services_t *s = ctx; s->options.debug_graph.debug_graph(s->options.debug_graph.ctx, value, color); }
static void engine_pmove(void *ctx, qa_guest_address address, qa_compat_q2_classic_guest_host *host) {
  services_t *s = ctx; const qa_equipment_movement *equipment = s->player_movement; s->player_movement = NULL;
  qa_compat_q2_classic_guest_pmove_options options; memset(&options, 0, sizeof options);
  options.numeric = s->options.numeric; options.air_accelerate = qa_cvar_registry_variable_value(s->options.cvars, "sv_airaccelerate"); options.has_air_accelerate = true;
  options.equipment = equipment;
  qa_compat_q2_run_classic_guest_pmove(address, host, options);
}

static void seed_map(services_t *s, const char *map_path) {
  store_configstring(s, 33, map_path);
  size_t models = model_count(s->options.scene);
  for (size_t model = 1; model < models; model++) { char name[32]; snprintf(name, sizeof name, "*%zu", model); store_configstring(s, 33 + (double)model, name); }
}
qa_app_sim_classic_guest_services *qa_app_sim_classic_guest_services_create(qa_compat_q2_memory *memory, const qa_app_sim_classic_guest_services_options *options) {
  if (options->max_clients != floor(options->max_clients) || options->max_clients < 1 || options->max_clients > 256) qa_range_error("Invalid API 3 maxclients");
  if (model_count(options->scene) > 255) qa_range_error("API 3 map exceeds MAX_MODELS");
  services_t *s = calloc(1, sizeof *s); if (s == NULL) return NULL;
  s->memory = memory; s->options = *options; s->loading = true;
  s->strings = calloc(CONFIGSTRINGS, sizeof *s->strings); if (s->strings == NULL) { free(s); return NULL; }
  for (size_t i = 0; i < CONFIGSTRINGS; i++) s->positions[i] = -1;
  for (int kind = 0; kind < 3; kind++) if (!qa_resource_name_index_init(&s->names[kind], 256, NULL, 0)) qa_range_error("Out of memory");
  if (!qa_event_ring_init(&s->messages, 8192, sizeof(qa_app_sim_classic_guest_message)) || !qa_event_ring_open_cursor(&s->messages, &s->cursor, false)) qa_range_error("Out of memory");
  char text[32]; snprintf(text, sizeof text, "%d", (int)options->max_clients);
  qa_cvar_snapshot set; qa_cvar_registry_set(options->cvars, "maxclients", text, true, &set); qa_cvar_read_free(&set);
  qa_q2_sizebuf_init_empty(&s->buffer); qa_q2_sz_init(&s->buffer, s->buffer_data, sizeof s->buffer_data, 1400);
  seed_map(s, options->map_path);
  qa_compat_q2_classic_engine_services *e = &s->services;
  e->ctx = s; e->dispose = dispose;
  if (options->pickups != NULL) { s->pickups_port.ctx = s; s->pickups_port.run_source = pickups_run; s->pickups_port.touch = pickups_touch; e->pickups = &s->pickups_port; }
  if (options->damage_provenance != NULL) { s->provenance_port.ctx = s; s->provenance_port.provenance = provenance; e->damage_provenance = &s->provenance_port; }
  s->engine_port = *options->engine; e->engine = &s->engine_port; e->cvars = options->cvars;
  e->stand_in_ctx = s; e->stand_in_actor = stand_in_actor;
  e->bind_ctx = s; e->bind_entity = bind_entity; e->link_ctx = s; e->link_body = link_body;
  e->print = engine_print; e->configstring = engine_configstring; e->resource_index = engine_resource_index; e->sound = engine_sound;
  e->areas_connected = engine_areas_connected; e->world_link = engine_world_link; e->box_edicts = engine_box_edicts; e->message = engine_message;
  e->command = engine_command; e->add_command = engine_add_command; e->debug_graph = engine_debug_graph; e->pmove_ctx = s; e->pmove = engine_pmove;
  return s;
}
void qa_app_sim_classic_guest_services_destroy(services_t *s) {
  if (s == NULL) return;
  if (s->combat != NULL) qa_compat_q2_classic_combat_bindings_destroy(s->combat);
  if (s->disposed_combat != NULL) qa_compat_q2_classic_combat_bindings_destroy(s->disposed_combat);
  if (s->inventory != NULL) qa_compat_q2_classic_source_inventory_destroy(s->inventory);
  clear_configstrings(s); free(s->strings);
  for (int kind = 0; kind < 3; kind++) qa_resource_name_index_destroy(&s->names[kind]);
  qa_event_ring_for_each_live(&s->messages, free_message, NULL); free_drained(s);
  qa_event_ring_close_cursor(&s->messages, &s->cursor); qa_event_ring_destroy(&s->messages);
  for (body_binding *b = s->bindings; b != NULL;) { body_binding *next = b->next; free(b); b = next; }
  free(s->motions); free(s->pending); free(s->proxies); free(s->proxy_actors); free(s->edicts_scratch);
  for (int i = 0; i < 4; i++) free(s->appearance_strings[i]);
  free(s);
}
qa_compat_q2_classic_engine_services qa_app_sim_classic_guest_services_engine(services_t *s) { return s->services; }
const qa_app_sim_classic_guest_services_options *qa_app_sim_classic_guest_services_options_of(const services_t *s) { return &s->options; }
void qa_app_sim_classic_guest_services_set_command(services_t *s, qa_app_sim_guest_command command) { s->options.command = command; }
/* TS passes the whole ClassicGuestServicesOptions where a ClassicGuestMapServices is expected (structural Pick). */
void qa_app_sim_classic_guest_map_services_of(const qa_app_sim_classic_guest_services_options *options, qa_app_sim_classic_guest_map_services *out) {
  out->engine = options->engine; out->scene = options->scene; out->map_path = options->map_path; out->admit = options->admit;
  out->collision = options->collision; out->print = options->print; out->command = options->command; out->add_command = options->add_command;
  out->debug_graph = options->debug_graph; out->damage_provenance = options->damage_provenance; out->pickups = options->pickups;
}
void qa_app_sim_classic_guest_services_validate_map(services_t *s, const qa_app_sim_classic_guest_map_services *binding) {
  (void)s; if (model_count(binding->scene) > 255) qa_range_error("API 3 map exceeds MAX_MODELS");
}
void qa_app_sim_classic_guest_services_rebind_world(services_t *s, const qa_app_sim_classic_guest_map_services *b) {
  qa_app_sim_classic_guest_services_validate_map(s, b);
  s->options.engine = b->engine; s->options.scene = b->scene; s->options.map_path = b->map_path; s->options.admit = b->admit; s->options.collision = b->collision;
  s->options.print = b->print; s->options.command = b->command; s->options.add_command = b->add_command; s->options.debug_graph = b->debug_graph;
  s->options.damage_provenance = b->damage_provenance; s->options.pickups = b->pickups;
  s->engine_port = *b->engine;
  s->loading = true; clear_configstrings(s);
  qa_event_ring_for_each_live(&s->messages, free_message, NULL); qa_event_ring_reset(&s->messages); qa_q2_sz_clear(&s->buffer);
  /* spawnEntities reinitializes every edict, so stale proxies are dropped without calling the DLL's G_FreeEdict */
  s->proxy_count = 0; s->pending_count = 0;
  if (s->proxy_actor_capacity != 0) memset(s->proxy_actors, 0, s->proxy_actor_capacity * sizeof *s->proxy_actors);
  for (int kind = 0; kind < 3; kind++) qa_resource_name_index_clear(&s->names[kind]);
  seed_map(s, b->map_path);
}
void qa_app_sim_classic_guest_services_bind_host(services_t *s, qa_compat_q2_classic_guest_host *host, const qa_guest_address *image_base) {
  if (s->host != NULL || qa_compat_q2_classic_guest_host_get_memory(host) != s->memory) qa_raise("Error", 0, 0, "API 3 services already bound or guest memory mismatch");
  s->host = host;
  const qa_compat_q2_classic_combat_profile *combat = s->options.primary_world != NULL ? &s->options.primary_world->base : NULL;
  s->combat = image_base == NULL ? NULL : qa_compat_q2_classic_combat_bindings_create(host, *image_base, combat, s->options.has_primary_world);
  s->inventory = image_base == NULL ? NULL : qa_compat_q2_classic_source_inventory_create(host, *image_base, s->options.primary_world, s->options.has_primary_world);
  if (image_base != NULL) qa_compat_q2_classic_guest_host_bind_pickups(host, *image_base, s->options.pickup_profile);
}
qa_compat_q2_classic_guest_host *qa_app_sim_classic_guest_services_host(services_t *s) {
  if (s->host == NULL) qa_raise("Error", 0, 0, "API 3 services have no guest host");
  return s->host;
}
int qa_app_sim_classic_guest_services_notarget(services_t *s, double slot) {
  if (s->combat == NULL) return -1;
  return qa_compat_q2_classic_combat_bindings_notarget(s->combat, qa_compat_q2_classic_edicts_at(edicts(s), slot)) ? 1 : 0;
}
static void require_client_actor(services_t *s, double slot, qa_actor_id actor, const char *message) {
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(edicts(s), slot); qa_actor_id current;
  if (slot < 1 || slot > s->options.max_clients || !qa_session_actor_registry_is_live(s->options.engine->actors, actor)
      || !current_actor(record, &current) || !same_actor(current, actor)) qa_raise("Error", 0, 0, "%s", message);
}
bool qa_app_sim_classic_guest_services_player_grounded(services_t *s, double slot, qa_actor_id actor) {
  require_client_actor(s, slot, actor, "API3 movement state requires the current client actor");
  motion_entry *motion = motion_of(s, actor);
  if (motion != NULL) return motion->projection.grounded(motion->projection.ctx);
  const uint8_t *client = qa_compat_q2_classic_edicts_client_prefix(edicts(s), slot);
  if (client == NULL) qa_raise("Error", 0, 0, "API3 movement state has no client");
  return (client[16] & 4) != 0;
}
qa_compat_q2_native_input_motion_view qa_app_sim_classic_guest_services_player_view(services_t *s, double slot, qa_actor_id actor) {
  require_client_actor(s, slot, actor, "API3 player view requires the current client actor");
  motion_entry *motion = motion_of(s, actor);
  if (motion != NULL) return motion->projection.view(motion->projection.ctx);
  const uint8_t *client = qa_compat_q2_classic_edicts_client_prefix(edicts(s), slot);
  if (client == NULL) qa_raise("Error", 0, 0, "API3 player view has no client");
  qa_compat_q2_native_input_motion_view view = {vector(client, 40), (client[16] & 1) != 0};
  return view;
}
void qa_app_sim_classic_guest_services_set_player_view_roll(services_t *s, double slot, qa_actor_id actor, double roll) {
  const qa_compat_q2_classic_combat_profile *profile = !s->options.has_primary_world ? qa_compat_q2_classic_get_combat_profile(s->memory->module.digest)
    : s->options.primary_world != NULL ? &s->options.primary_world->base : NULL;
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(edicts(s), slot);
  if (profile == NULL) qa_raise("Error", 0, 0, "API3 source view writes require a declared private client layout");
  require_client_actor(s, slot, actor, "API3 source view requires the current client actor");
  qa_guest_address client;
  if (!qa_compat_q2_mem_read_pointer(s->memory, qa_compat_q2_mem_offset(s->memory, record.address, 84), &client)) qa_raise("Error", 0, 0, "API3 source view has no client");
  qa_compat_q2_mem_write_f32(s->memory, qa_compat_q2_mem_offset(s->memory, client, (int64_t)profile->client.view_angles + 8), roll);
}
bool qa_app_sim_classic_guest_services_has_source_inventory(services_t *s) { return s->inventory != NULL; }
bool qa_app_sim_classic_guest_services_source_inventory(services_t *s, double slot, qa_inventory_state_binding *out) {
  if (s->inventory == NULL) return false;
  *out = qa_compat_q2_classic_source_inventory_bind(s->inventory, qa_compat_q2_classic_edicts_at(edicts(s), slot)); return true;
}
void qa_app_sim_classic_guest_services_complete_spawn(services_t *s) { s->loading = false; }
void qa_app_sim_classic_guest_services_set_player_velocity_writer(services_t *s, qa_app_sim_player_velocity velocity) { s->has_velocity = true; s->velocity = velocity; }

typedef struct movement_scope { services_t *s; const qa_equipment_movement *previous; } movement_scope;
static void restore_movement(void *arg) { movement_scope *scope = arg; scope->s->player_movement = scope->previous; }
void qa_app_sim_classic_guest_services_with_player_movement(services_t *s, const qa_equipment_movement *movement, void (*run)(void *), void *ctx) {
  movement_scope scope = {s, s->player_movement}; s->player_movement = movement;
  qa_cleanup node; qa_cleanup_push(&node, restore_movement, &scope);
  run(ctx);
  qa_cleanup_pop(&node); restore_movement(&scope);
}
typedef struct input_scope { services_t *s; qa_actor_id actor; bool had_previous; qa_compat_q2_native_input_motion previous; } input_scope;
static void restore_input(void *arg) {
  input_scope *scope = arg; services_t *s = scope->s; motion_entry *entry = motion_of(s, scope->actor);
  if (!scope->had_previous) {
    if (entry != NULL) { size_t index = (size_t)(entry - s->motions); memmove(entry, entry + 1, (s->motion_count - index - 1) * sizeof *entry); s->motion_count--; }
  } else if (entry != NULL) entry->projection = scope->previous;
}
void qa_app_sim_classic_guest_services_with_input_movement(services_t *s, qa_actor_id actor, const qa_compat_q2_native_input_motion *projection, void (*run)(void *), void *ctx) {
  motion_entry *entry = motion_of(s, actor); input_scope scope; memset(&scope, 0, sizeof scope); scope.s = s; scope.actor = actor;
  if (entry != NULL) { scope.had_previous = true; scope.previous = entry->projection; entry->projection = *projection; }
  else {
    s->motions = grow(s->motions, &s->motion_capacity, s->motion_count, sizeof *s->motions);
    entry = &s->motions[s->motion_count++]; memset(entry, 0, sizeof *entry); entry->actor = actor; entry->projection = *projection;
  }
  qa_cleanup node; qa_cleanup_push(&node, restore_input, &scope);
  run(ctx);
  qa_cleanup_pop(&node); restore_input(&scope);
}
const qa_app_sim_configstring *qa_app_sim_classic_guest_services_configstrings(services_t *s, size_t *out_count) { *out_count = s->string_count; return s->strings; }
const char *qa_app_sim_classic_guest_services_configstring(services_t *s, double index) { return configstring(s, index); }

qa_q2_entity_state qa_app_sim_classic_guest_services_entity_state(services_t *s, double slot) {
  const uint8_t *view = qa_compat_q2_classic_edicts_at(edicts(s), slot).bytes.bytes;
  qa_q2_entity_state out; memset(&out, 0, sizeof out);
  out.number = qa_compat_q2_view_i32(view, 0); out.origin = vector(view, 4); out.angles = vector(view, 16); out.old_origin = vector(view, 28);
  for (int i = 0; i < 4; i++) out.model_indexes[i] = qa_compat_q2_view_i32(view, 40 + (size_t)i * 4);
  out.frame = qa_compat_q2_view_i32(view, 56); out.skin = qa_compat_q2_view_i32(view, 60); out.effects = qa_compat_q2_view_u32(view, 64);
  out.render_effects = qa_compat_q2_view_i32(view, 68); out.solid = qa_compat_q2_view_i32(view, 72); out.sound = qa_compat_q2_view_i32(view, 76); out.event = qa_compat_q2_view_i32(view, 80);
  return out;
}
qa_q2_player_state qa_app_sim_classic_guest_services_player_state(services_t *s, double slot) {
  const uint8_t *view = qa_compat_q2_classic_edicts_client_prefix(edicts(s), slot);
  if (view == NULL) qa_raise("Error", 0, 0, "API 3 client slot has no gclient prefix");
  qa_q2_player_state out; memset(&out, 0, sizeof out);
  out.movement.type = qa_compat_q2_view_i32(view, 0);
  for (int i = 0; i < 3; i++) {
    out.movement.origin_eighths.values[i] = qa_compat_q2_view_i16(view, 4 + (size_t)i * 2);
    out.movement.velocity_eighths.values[i] = qa_compat_q2_view_i16(view, 10 + (size_t)i * 2);
    out.movement.delta_angle_shorts.values[i] = qa_compat_q2_view_i16(view, 20 + (size_t)i * 2);
  }
  out.movement.flags = view[16]; out.movement.time_eight_milliseconds = view[17]; out.movement.gravity = qa_compat_q2_view_i16(view, 18);
  out.view.view_angles = vector(view, 28); out.view.view_offset = vector(view, 40); out.view.kick_angles = vector(view, 52);
  out.view.gun_angles = vector(view, 64); out.view.gun_offset = vector(view, 76); out.view.gun_index = qa_compat_q2_view_i32(view, 88); out.view.gun_frame = qa_compat_q2_view_i32(view, 92);
  out.blend.x = qa_compat_q2_view_f32(view, 96); out.blend.y = qa_compat_q2_view_f32(view, 100); out.blend.z = qa_compat_q2_view_f32(view, 104); out.blend.w = qa_compat_q2_view_f32(view, 108);
  out.view.fov = qa_compat_q2_view_f32(view, 112); out.view.render_flags = qa_compat_q2_view_i32(view, 116);
  for (int i = 0; i < 32; i++) s->stats[i] = qa_compat_q2_view_i16(view, 120 + (size_t)i * 2);
  out.view.stats.values = s->stats; out.view.stats.count = 32;
  return out;
}
static const char *keep_appearance(services_t *s, int index, char *text) { free(s->appearance_strings[index]); s->appearance_strings[index] = text; return text; }
static char *join_text(const char *const *parts, size_t count) {
  size_t n = 0; for (size_t i = 0; i < count; i++) n += strlen(parts[i]);
  char *out = malloc(n + 1); if (out == NULL) qa_range_error("Out of memory");
  out[0] = '\0'; for (size_t i = 0; i < count; i++) strcat(out, parts[i]);
  return out;
}
static char *slice_text(const char *text, size_t length) { char *out = malloc(length + 1); if (out == NULL) qa_range_error("Out of memory"); memcpy(out, text, length); out[length] = '\0'; return out; }
qa_app_sim_model_appearance qa_app_sim_classic_guest_services_model_appearance(services_t *s, double slot) {
  qa_q2_entity_state state = qa_app_sim_classic_guest_services_entity_state(s, slot);
  qa_app_sim_model_appearance out; memset(&out, 0, sizeof out); out.attached_model_count = 3;
  bool every = true; for (int i = 0; i < 4; i++) every = every && state.model_indexes[i] != 255;
  if (every) {
    out.path = qa_app_sim_classic_guest_services_resource(s, "model", state.model_indexes[0]); out.skin = state.skin;
    for (int i = 0; i < 3; i++) out.attached_models[i] = qa_app_sim_classic_guest_services_resource(s, "model", state.model_indexes[i + 1]);
    return out;
  }
  const char *value = configstring(s, 1312 + (qa_js_to_int32(state.skin) & 255)); if (value == NULL) value = "player\\male/grunt";
  const char *backslash = strchr(value, '\\'); const char *appearance = backslash == NULL ? value : backslash + 1;
  const char *slash = strchr(appearance, '/');
  char *model = slash == NULL ? dup_text("male") : slice_text(appearance, (size_t)(slash - appearance));
  const char *skin = slash == NULL ? "grunt" : slash + 1;
  const char *weapons[20]; size_t weapon_count = 0; weapons[weapon_count++] = "weapon.md2";
  for (int index = 1; index < 256; index++) {
    const char *name = qa_app_sim_classic_guest_services_resource(s, "model", index);
    if (name[0] == '#' && weapon_count < 20) weapons[weapon_count++] = name + 1;
  }
  uint32_t weapon_index = (qa_js_to_uint32(state.skin) >> 8) & 255;
  const char *weapon = weapon_index < weapon_count ? weapons[weapon_index] : weapons[0];
  bool player = state.model_indexes[0] == 255;
  const char *tris[] = {"players/", model, "/tris.md2"}, *skin_parts[] = {"players/", model, "/", skin, ".pcx"}, *weapon_parts[] = {"players/", model, "/", weapon};
  out.path = player ? keep_appearance(s, 0, join_text(tris, 3)) : qa_app_sim_classic_guest_services_resource(s, "model", state.model_indexes[0]);
  out.skin = player ? 0 : state.skin;
  out.skin_path = player ? keep_appearance(s, 1, join_text(skin_parts, 5)) : NULL;
  keep_appearance(s, 2, join_text(weapon_parts, 4));
  for (int i = 0; i < 3; i++) out.attached_models[i] = state.model_indexes[i + 1] == 255 ? s->appearance_strings[2] : qa_app_sim_classic_guest_services_resource(s, "model", state.model_indexes[i + 1]);
  free(model);
  return out;
}

/* --- monster proxies --- */
static void set_proxy_actor(services_t *s, double slot, bool set, qa_actor_id actor) {
  if (!(slot >= 0)) return;
  size_t index = (size_t)slot;
  if (index >= s->proxy_actor_capacity) {
    if (!set) return;
    size_t next = s->proxy_actor_capacity == 0 ? 64 : s->proxy_actor_capacity;
    while (next <= index) next *= 2;
    void *grown = realloc(s->proxy_actors, next * sizeof *s->proxy_actors);
    if (grown == NULL) qa_range_error("Out of memory");
    s->proxy_actors = grown;
    memset(s->proxy_actors + s->proxy_actor_capacity, 0, (next - s->proxy_actor_capacity) * sizeof *s->proxy_actors);
    s->proxy_actor_capacity = next;
  }
  s->proxy_actors[index].set = set; s->proxy_actors[index].actor = actor;
}
/* TS standInActor: record => this.#proxyActors[record.slot] ?? null. */
static bool stand_in_actor(void *ctx, qa_raw_entity_view record, qa_actor_id *out) {
  services_t *s = ctx;
  if (!(record.slot >= 0) || (size_t)record.slot >= s->proxy_actor_capacity || !s->proxy_actors[(size_t)record.slot].set) return false;
  *out = s->proxy_actors[(size_t)record.slot].actor;
  return true;
}
double qa_app_sim_classic_guest_services_allocate_monster_proxy(services_t *s) {
  if (s->combat == NULL || s->host == NULL) return -1;
  qa_raw_entity_view view = qa_compat_q2_classic_combat_bindings_spawn_proxy_edict(s->combat);
  for (size_t i = 0; i < s->pending_count; i++) if (s->pending[i].slot == view.slot) { s->pending[i].view = view; return view.slot; }
  s->pending = grow(s->pending, &s->pending_capacity, s->pending_count, sizeof *s->pending);
  s->pending[s->pending_count].slot = view.slot; s->pending[s->pending_count++].view = view;
  return view.slot;
}
static proxy_entry *proxy_of(services_t *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->proxy_count; i++) if (same_actor(s->proxies[i].actor, actor)) return &s->proxies[i];
  return NULL;
}
static void sync_monster_proxy(services_t *s, qa_actor_id actor) {
  proxy_entry *entry = proxy_of(s, actor);
  if (entry == NULL || s->combat == NULL) return;
  qa_body_state_result body_result = qa_shared_body_table_read(s->options.engine->bodies, actor);
  if (!body_result.has_value) return;
  qa_combat_state_result state = qa_gameplay_authority_read(s->options.engine->combat, actor);
  qa_compat_q2_proxy_body body = {body_result.value.origin, body_result.value.angles, body_result.value.velocity, body_result.value.bounds};
  qa_compat_q2_proxy_combat combat = {0, false, 0};
  if (state.has_value) { combat.health = state.value.health; combat.can_take_damage = state.value.can_take_damage; combat.mass = state.value.mass; }
  qa_compat_q2_classic_combat_bindings_sync_proxy_edict(s->combat, entry->view, body, state.has_value ? &combat : NULL);
  entry->health = state.has_value ? state.value.health : 0;
}
void qa_app_sim_classic_guest_services_bind_monster_proxy(services_t *s, double slot, qa_owned_actor actor) {
  size_t index = s->pending_count;
  for (size_t i = 0; i < s->pending_count; i++) if (s->pending[i].slot == slot) index = i;
  if (index == s->pending_count) qa_raise("Error", 0, 0, "Monster proxy slot was not allocated");
  qa_raw_entity_view view = s->pending[index].view;
  memmove(&s->pending[index], &s->pending[index + 1], (s->pending_count - index - 1) * sizeof *s->pending); s->pending_count--;
  qa_combat_state_result combat = qa_gameplay_authority_read(s->options.engine->combat, actor.id);
  proxy_entry *entry = proxy_of(s, actor.id);
  if (entry == NULL) { s->proxies = grow(s->proxies, &s->proxy_capacity, s->proxy_count, sizeof *s->proxies); entry = &s->proxies[s->proxy_count++]; }
  entry->actor = actor.id; entry->view = view; entry->health = combat.has_value ? combat.value.health : 0;
  set_proxy_actor(s, view.slot, true, actor.id);
  sync_monster_proxy(s, actor.id);
}
void qa_app_sim_classic_guest_services_sync_monster_proxies(services_t *s) {
  size_t count = s->proxy_count; qa_actor_id *actors = calloc(count == 0 ? 1 : count, sizeof *actors); if (actors == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) actors[i] = s->proxies[i].actor;
  for (size_t i = 0; i < count; i++) sync_monster_proxy(s, actors[i]);
  free(actors);
}
static void remove_proxy(services_t *s, qa_actor_id actor) {
  proxy_entry *entry = proxy_of(s, actor); if (entry == NULL) return;
  size_t index = (size_t)(entry - s->proxies); memmove(entry, entry + 1, (s->proxy_count - index - 1) * sizeof *entry); s->proxy_count--;
}
typedef struct free_proxy { services_t *s; qa_raw_entity_view view; } free_proxy;
static void run_free_proxy(void *arg) { free_proxy *f = arg; qa_compat_q2_classic_combat_bindings_free_proxy_edict(f->s->combat, f->view); }
/* After a guest frame, forward guest-inflicted proxy health loss into shared combat so TS pain/death behavior runs;
 * the guest already applied its own damage rules to the proxy fields. */
void qa_app_sim_classic_guest_services_forward_monster_proxy_damage(services_t *s) {
  if (s->combat == NULL) return;
  qa_q2_foundation_host *engine = s->options.engine;
  size_t count = s->proxy_count; proxy_entry *snapshot = calloc(count == 0 ? 1 : count, sizeof *snapshot); if (snapshot == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(snapshot, s->proxies, count * sizeof *snapshot);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = snapshot[i].actor; proxy_entry *entry = proxy_of(s, actor);
    if (entry == NULL) entry = &snapshot[i];
    if (!qa_session_actor_registry_is_live(engine->actors, actor)) {
      free_proxy f = {s, entry->view}; qa_caught_error caught;
      bool completed = qa_try(run_free_proxy, &f, &caught);
      remove_proxy(s, actor);
      set_proxy_actor(s, f.view.slot, false, actor);
      if (!completed) { free(snapshot); qa_rethrow(&caught); }
      continue;
    }
    double health = qa_compat_q2_classic_combat_bindings_proxy_health(s->combat, entry->view);
    qa_body_state_result body = qa_shared_body_table_read(engine->bodies, actor);
    if (body.has_value) {
      qa_vec3 guest_velocity = qa_compat_q2_classic_combat_bindings_proxy_velocity(s->combat, entry->view);
      if (guest_velocity.x != body.value.velocity.x || guest_velocity.y != body.value.velocity.y || guest_velocity.z != body.value.velocity.z) {
        qa_owned_actor owned;
        if (!qa_session_actor_registry_resolve_owned(engine->actors, actor, &owned)) qa_raise("Error", 0, 0, "Monster proxy actor was released");
        qa_body_state next = body.value; next.velocity = guest_velocity;
        qa_shared_body_table_write(engine->bodies, owned, next);
      }
    }
    if (health >= entry->health) { qa_combat_state_result state = qa_gameplay_authority_read(engine->combat, actor); entry->health = state.has_value ? state.value.health : health; continue; }
    double amount = entry->health - health;
    entry->health = health;
    if (s->options.damage_provenance == NULL || s->host == NULL) continue;
    /* T_Damage on a stand-in reaches shared combat with its real attacker (standInActor); this covers
     * health the guest changed without T_Damage, which has no attacker, so the world takes credit. */
    qa_owned_actor world_owned; qa_actor_id world = actor;
    if (qa_session_actor_registry_at_source(engine->actors, qa_compat_q2_classic_guest_host_get_options(s->host)->provider, 0, &world_owned)) world = world_owned.id;
    qa_damage_request request; memset(&request, 0, sizeof request);
    request.target = actor; request.amount = amount; request.knockback = 0; request.normal.z = 1; request.delivery = "direct";
    if (body.has_value) request.point = body.value.origin;
    request.attack = s->options.damage_provenance->provenance(s->options.damage_provenance->ctx, world, world, actor);
    qa_actor_id credit = world; request.attack.attacker = &credit; request.attack.inflictor = &credit;
    memset(&request.attack.cause, 0, sizeof request.attack.cause); request.attack.cause.kind = QA_ATTACK_CAUSE_Q2; request.attack.cause.means_of_death = 0; request.attack.cause.damage_flags = 0;
    (void)qa_gameplay_authority_apply(engine->combat, request, NULL, NULL);
    qa_combat_state_result after = qa_gameplay_authority_read(engine->combat, actor);
    entry = proxy_of(s, actor); if (entry != NULL && after.has_value) entry->health = after.value.health;
  }
  free(snapshot);
}
void qa_app_sim_classic_guest_services_publish_entities(services_t *s) {
  qa_compat_q2_classic_guest_host *host = qa_app_sim_classic_guest_services_host(s);
  qa_session_actor_registry *actors = s->options.engine->actors; size_t count = 0;
  const qa_owned_actor *owned = qa_session_actor_registry_owned_by(actors, qa_compat_q2_classic_guest_host_get_options(host)->provider, &count);
  qa_owned_actor *copy = calloc(count == 0 ? 1 : count, sizeof *copy); if (copy == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(copy, owned, count * sizeof *copy);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = copy[i].id;
    /* Shared actors admitted under the map provider without a guest edict (selected TS monsters) publish through their own sources. */
    if (!qa_compat_q2_classic_edicts_resolves(edicts(s), actor)) continue;
    qa_raw_entity_view record = qa_compat_q2_classic_edicts_from_pointer(edicts(s), qa_compat_q2_classic_edicts_pointer(edicts(s), actor));
    if (record.slot == 0) continue;
    qa_q2_entity_state state = qa_app_sim_classic_guest_services_entity_state(s, record.slot);
    qa_q2_presentation_event visibility; memset(&visibility, 0, sizeof visibility);
    visibility.kind = "visibility"; visibility.has_actor = true; visibility.actor = actor; visibility.visible = (qa_compat_q2_view_i32(record.bytes.bytes, 184) & 1) == 0;
    s->options.engine->emit(s->options.engine->ctx, visibility);
    qa_app_sim_model_appearance appearance = qa_app_sim_classic_guest_services_model_appearance(s, record.slot);
    qa_q2_presentation_event model; memset(&model, 0, sizeof model);
    model.kind = "model"; model.has_actor = true; model.actor = actor; model.path = appearance.path;
    for (int m = 0; m < 3; m++) model.attached_models[m] = appearance.attached_models[m];
    model.frame = state.frame; model.old_frame = state.frame; model.scale = 1; model.alpha = (qa_js_to_int32(state.render_effects) & 32) != 0 ? 0.3 : 1;
    model.skin = appearance.skin; model.effects = state.effects; model.render_flags = state.render_effects;
    s->options.engine->emit(s->options.engine->ctx, model);
    if (state.event != 0) {
      qa_q2_presentation_event event; memset(&event, 0, sizeof event);
      event.kind = "entity-event"; event.has_actor = true; event.actor = actor; event.event = state.event;
      s->options.engine->emit(s->options.engine->ctx, event);
    }
  }
  free(copy);
}
