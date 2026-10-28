/* Port of src/app/bootstrap/simulation/q3/guest-records.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_records.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/entity_record.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/world/collision/q3/clip_models.h"
#include "qa/world/collision/q3/world.h"

typedef struct actor_entry { double slot; qa_owned_actor actor; bool foreign; } actor_entry;
typedef struct link_entry {
  double slot; int64_t areanum, areanum2; int64_t clusters[16]; size_t cluster_count; int64_t last_cluster;
} link_entry;
typedef struct slot_ctx { struct qa_app_q3_guest_records *records; double slot; qa_owned_actor actor; } slot_ctx;
struct qa_app_q3_guest_records {
  qa_qvm_game_data *data;
  qa_app_q3_guest_record_host host;
  void (*velocity_writer)(void *ctx, qa_owned_actor actor, qa_vec3 velocity);
  void *velocity_ctx;
  actor_entry actors[1024]; size_t actor_count;
  link_entry links[1024]; size_t link_count;
  double retired[64]; size_t retired_count;
  double input_motion[64]; size_t input_motion_count;
  slot_ctx slot_contexts[1024];
  qa_actor_id ground_storage[1024];
  bool closed;
  int64_t release_subscription;
};

static qa_qvm_entity_state state_of(qa_qvm_shared_entity *entity) {
  qa_qvmx_view view = {entity->s.view.bytes + entity->s.view.start, (size_t)entity->s.view.length};
  return qa_qvm_borrow_entity_state(view, entity->profile);
}
static actor_entry *find_actor(qa_app_q3_guest_records *records, double slot) {
  for (size_t i = 0; i < records->actor_count; i++) if (records->actors[i].slot == slot) return &records->actors[i];
  return NULL;
}
static bool in_list(const double *list, size_t count, double slot) {
  for (size_t i = 0; i < count; i++) if (list[i] == slot) return true;
  return false;
}
static void list_add(double *list, size_t *count, double slot) {
  if (!in_list(list, *count, slot) && *count < 64) list[(*count)++] = slot;
}
static void list_remove(double *list, size_t *count, double slot) {
  for (size_t i = 0; i < *count; i++) if (list[i] == slot) {
    memmove(list + i, list + i + 1, (*count - i - 1) * sizeof *list); (*count)--; return;
  }
}
qa_qvm_shared_entity *qa_app_q3_guest_records_entity(qa_app_q3_guest_records *records, double slot) {
  if (records->closed) qa_range_error("Q3 guest records are retired");
  return qa_qvm_game_data_entity(records->data, slot);
}
static qa_body_state body_read(qa_app_q3_guest_records *records, double slot, qa_actor_id *ground_storage) {
  qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(records, slot);
  qa_qvm_entity_state state = state_of(entity);
  qa_body_state body; memset(&body, 0, sizeof body);
  if (in_list(records->input_motion, records->input_motion_count, slot)) {
    qa_q3_player_state player; double stats[16], persistent[16], powerups[16], ammo[16];
    qa_qvm_game_data_copy_player_state(records->data, slot, &player, stats, persistent, powerups, ammo);
    body.origin = player.origin;
  } else body.origin = qa_qvm_shared_entity_get_current_origin(entity);
  body.angles = qa_qvm_shared_entity_get_current_angles(entity);
  if (slot < qa_qvm_game_data_get_num_clients(records->data)) {
    qa_q3_player_state player; double stats[16], persistent[16], powerups[16], ammo[16];
    qa_qvm_game_data_copy_player_state(records->data, slot, &player, stats, persistent, powerups, ammo);
    body.velocity = player.velocity;
  } else body.velocity = qa_qvm_entity_state_get_pos(&state).delta;
  body.bounds.min = qa_qvm_shared_entity_get_mins(entity);
  body.bounds.max = qa_qvm_shared_entity_get_maxs(entity);
  double ground_num = qa_qvm_entity_state_get_ground_entity_num(&state);
  actor_entry *ground = find_actor(records, ground_num);
  if (ground) { *ground_storage = ground->actor.id; body.ground = ground_storage; }
  return body;
}
static qa_body_state binding_read(void *v) {
  slot_ctx *ctx = (slot_ctx *)v;
  return body_read(ctx->records, ctx->slot, &ctx->records->ground_storage[(size_t)ctx->slot < 1024 ? (size_t)ctx->slot : 0]);
}
static void binding_write(void *v, qa_body_state body) {
  slot_ctx *ctx = (slot_ctx *)v;
  qa_app_q3_guest_records *records = ctx->records;
  double slot = ctx->slot;
  qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(records, slot);
  qa_qvm_entity_state state = state_of(entity);
  if (in_list(records->input_motion, records->input_motion_count, slot)) {
    qa_q3_player_state player; double stats[16], persistent[16], powerups[16], ammo[16];
    qa_qvm_game_data_copy_player_state(records->data, slot, &player, stats, persistent, powerups, ammo);
    player.origin = body.origin; player.velocity = body.velocity;
    qa_qvm_game_data_write_player_state(records->data, slot, &player);
  } else if (slot < qa_qvm_game_data_get_num_clients(records->data)) {
    if (records->velocity_writer) records->velocity_writer(records->velocity_ctx, ctx->actor, body.velocity);
  }
  qa_qvm_shared_entity_set_current_origin(entity, body.origin);
  qa_qvm_shared_entity_set_current_angles(entity, body.angles);
  qa_qvm_shared_entity_set_mins(entity, body.bounds.min);
  qa_qvm_shared_entity_set_maxs(entity, body.bounds.max);
  qa_qvm_trajectory pos = qa_qvm_entity_state_get_pos(&state);
  pos.delta = body.velocity;
  qa_qvm_entity_state_set_pos(&state, pos);
  if (!in_list(records->input_motion, records->input_motion_count, slot)) {
    double ground_num = 1023;
    if (body.ground) ground_num = qa_app_q3_guest_records_require_slot(records, *body.ground);
    qa_qvm_entity_state_set_ground_entity_num(&state, ground_num);
  }
}
static void bind_actor(qa_app_q3_guest_records *records, double slot, qa_owned_actor actor) {
  if (records->actor_count >= 1024) qa_range_error("Out of memory");
  actor_entry *entry = &records->actors[records->actor_count++];
  entry->slot = slot; entry->actor = actor; entry->foreign = false;
  slot_ctx *ctx = &records->slot_contexts[(size_t)slot < 1024 ? (size_t)slot : 0];
  ctx->records = records; ctx->slot = slot; ctx->actor = actor;
  qa_body_state_binding binding; memset(&binding, 0, sizeof binding);
  binding.ctx = ctx; binding.read = binding_read; binding.write = binding_write;
  qa_shared_body_table_bind(records->host.bodies, actor, binding);
  if (records->host.admit) records->host.admit(records->host.ctx, actor);
}
static void release_cb(void *v, qa_owned_actor actor) {
  qa_app_q3_guest_records *records = (qa_app_q3_guest_records *)v;
  for (size_t i = 0; i < records->actor_count; i++) {
    if (qa_same_actor(records->actors[i].actor.id, actor.id) && records->actors[i].actor.token == actor.token) {
      double slot = records->actors[i].slot;
      memmove(records->actors + i, records->actors + i + 1, (records->actor_count - i - 1) * sizeof *records->actors);
      records->actor_count--;
      for (size_t j = 0; j < records->link_count; j++) {
        if (records->links[j].slot == slot) {
          memmove(records->links + j, records->links + j + 1, (records->link_count - j - 1) * sizeof *records->links);
          records->link_count--; break;
        }
      }
      return;
    }
  }
}
static bool collision_cb(void *v, qa_actor_id actor, qa_actor_collision *out) {
  qa_app_q3_guest_records *records = (qa_app_q3_guest_records *)v;
  double slot;
  if (!qa_app_q3_guest_records_slot(records, actor, &slot) || slot >= qa_qvm_game_data_get_num_entities(records->data)) return false;
  (void)out;
  return false; /* scene collision binding result fills via records_collision in link path */
}
qa_app_q3_guest_records *qa_app_q3_guest_records_create(qa_qvm_game_data *data, qa_app_q3_guest_record_host host) {
  qa_app_q3_guest_records *records = calloc(1, sizeof *records);
  if (!records) return NULL;
  records->data = data; records->host = host;
  qa_shared_bind_actor_collision(host.scene, collision_cb, records);
  records->release_subscription = qa_session_actor_registry_on_release(host.actors, release_cb, records);
  return records;
}
void qa_app_q3_guest_records_destroy(qa_app_q3_guest_records *records) {
  if (!records) return;
  if (!records->closed) qa_session_actor_registry_off_release(records->host.actors, records->release_subscription);
  free(records);
}
qa_qvm_game_data *qa_app_q3_guest_records_data(qa_app_q3_guest_records *records) { return records->data; }
const qa_app_q3_guest_record_host *qa_app_q3_guest_records_host_of(const qa_app_q3_guest_records *records) { return &records->host; }
void qa_app_q3_guest_records_set_player_velocity_writer(qa_app_q3_guest_records *records,
    void (*write)(void *ctx, qa_owned_actor actor, qa_vec3 velocity), void *ctx) {
  records->velocity_writer = write; records->velocity_ctx = ctx;
}
void qa_app_q3_guest_records_player(qa_app_q3_guest_records *records, double slot, qa_q3_player_state *out,
    double stats[16], double persistent[16], double powerups[16], double ammo[16]) {
  if (records->closed) qa_range_error("Q3 guest records are retired");
  qa_qvm_game_data_copy_player_state(records->data, slot, out, stats, persistent, powerups, ammo);
}
void qa_app_q3_guest_records_set_player_view_roll(qa_app_q3_guest_records *records, qa_actor_id actor, double roll) {
  double slot;
  bool found = qa_app_q3_guest_records_slot(records, actor, &slot);
  if (!found || slot >= qa_qvm_game_data_get_num_clients(records->data) || in_list(records->retired, records->retired_count, slot))
    qa_range_error("Q3 source view requires the current client actor");
  qa_q3_player_state player; double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(records, slot, &player, stats, persistent, powerups, ammo);
  player.view_angles.z = (double)(float)roll;
  qa_qvm_game_data_write_player_state(records->data, slot, &player);
}
qa_owned_actor qa_app_q3_guest_records_actor(qa_app_q3_guest_records *records, double slot) {
  (void)qa_app_q3_guest_records_entity(records, slot);
  if (in_list(records->retired, records->retired_count, slot)) qa_range_error("Q3 input client is retiring");
  if (slot >= 1022) qa_range_error("Q3 world and none slots cannot own shared actors");
  actor_entry *current = find_actor(records, slot);
  if (current) {
    qa_session_actor_registry_assert_owned(records->host.actors, current->actor);
    return current->actor;
  }
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(records->host.actors, records->host.provider, (int64_t)slot, "q3:guest-slot");
  bind_actor(records, slot, actor);
  return actor;
}
void qa_app_q3_guest_records_register_foreign(qa_app_q3_guest_records *records, double slot, qa_owned_actor actor) {
  (void)qa_app_q3_guest_records_entity(records, slot);
  if (slot >= 1022 || in_list(records->retired, records->retired_count, slot))
    qa_range_error("Q3 foreign proxy slot cannot own a shared actor");
  if (find_actor(records, slot)) qa_range_error("Q3 foreign proxy slot is occupied");
  qa_actor_source_result source = qa_session_actor_registry_source_of(records->host.actors, actor.id);
  if (!source.has_value || !source.provider || strcmp(source.provider, records->host.provider) != 0 || source.slot != (int64_t)slot)
    qa_range_error("Q3 foreign proxy actor has no matching source binding");
  if (records->actor_count >= 1024) qa_range_error("Out of memory");
  actor_entry *entry = &records->actors[records->actor_count++];
  entry->slot = slot; entry->actor = actor; entry->foreign = true;
}
bool qa_app_q3_guest_records_slot(qa_app_q3_guest_records *records, qa_actor_id actor, double *out_slot) {
  if (records->closed) return false;
  qa_actor_source_result source = qa_session_actor_registry_source_of(records->host.actors, actor);
  if (!source.has_value || !source.provider || strcmp(source.provider, records->host.provider) != 0) return false;
  actor_entry *entry = find_actor(records, (double)source.slot);
  if (!entry || !qa_same_actor(entry->actor.id, actor)) return false;
  *out_slot = (double)source.slot;
  return true;
}
double qa_app_q3_guest_records_require_slot(qa_app_q3_guest_records *records, qa_actor_id actor) {
  double slot;
  if (!qa_app_q3_guest_records_slot(records, actor, &slot)) qa_range_error("Shared actor has no Q3 guest slot");
  return slot;
}
bool qa_app_q3_guest_records_reference(qa_app_q3_guest_records *records, double slot, qa_actor_id *out) {
  if (slot < 0 || slot >= 1022 || in_list(records->retired, records->retired_count, slot)) return false;
  *out = qa_app_q3_guest_records_actor(records, slot).id;
  return true;
}
qa_body_state qa_app_q3_guest_records_body(qa_app_q3_guest_records *records, double slot, qa_actor_id *ground_storage) {
  return body_read(records, slot, ground_storage);
}
qa_actor_collision qa_app_q3_guest_records_collision(qa_app_q3_guest_records *records, double slot, qa_actor_id *owner_storage) {
  qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(records, slot);
  qa_qvm_entity_collision_model model = qa_qvm_shared_entity_get_model(entity);
  qa_actor_collision collision; memset(&collision, 0, sizeof collision);
  collision.family = "q3";
  collision.shape_kind = model.kind && strcmp(model.kind, "inline") == 0 ? "model" : model.kind;
  collision.shape_model = model.index;
  collision.contents = qa_qvm_shared_entity_get_contents(entity);
  actor_entry *owner = find_actor(records, qa_qvm_shared_entity_get_owner_num(entity));
  if (owner) { *owner_storage = owner->actor.id; collision.owner = owner_storage; }
  collision.has_q3_owner = true;
  collision.q3_entity_number = slot;
  collision.q3_owner_number = qa_qvm_shared_entity_get_owner_num(entity);
  collision.role = "solid";
  return collision;
}
static double byte_value(double value) {
  double truncated = trunc(value);
  return truncated < 1 ? 1 : truncated > 255 ? 255 : truncated;
}
void qa_app_q3_guest_records_link(qa_app_q3_guest_records *records, double slot) {
  if (in_list(records->retired, records->retired_count, slot)) {
    qa_qvm_shared_entity_set_linked(qa_app_q3_guest_records_entity(records, slot), false);
    return;
  }
  qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(records, slot);
  qa_qvm_entity_state state = state_of(entity);
  qa_owned_actor actor = qa_app_q3_guest_records_actor(records, slot);
  qa_shared_body_table_unlink(records->host.bodies, actor);
  qa_qvm_shared_entity_set_linked(entity, false);
  qa_qvm_entity_collision_model model = qa_qvm_shared_entity_get_model(entity);
  bool inline_model = model.kind && strcmp(model.kind, "inline") == 0;
  qa_vec3 mins = qa_qvm_shared_entity_get_mins(entity), maxs = qa_qvm_shared_entity_get_maxs(entity);
  int32_t contents = qa_qvm_shared_entity_get_contents(entity);
  double solid = inline_model ? 0xffffff : (contents & (1 | 0x2000000)) == 0 ? 0
      : (double)(((long long)byte_value((double)(float)(maxs.z + 32)) << 16)
          | ((long long)byte_value(-mins.z) << 8) | (long long)byte_value(maxs.x));
  qa_qvm_entity_state_set_solid(&state, solid);
  qa_vec3 angles = qa_qvm_shared_entity_get_current_angles(entity);
  qa_vec3 origin = qa_qvm_shared_entity_get_current_origin(entity);
  bool rotated = inline_model && (angles.x != 0 || angles.y != 0 || angles.z != 0);
  double radius = rotated ? qa_radius_from_bounds((qa_bounds){mins, maxs}) : 0;
  qa_vec3 epsilon = {1, 1, 1};
  qa_vec3 absmin = rotated ? qa_sub3(qa_sub3(origin, (qa_vec3){radius, radius, radius}), epsilon)
      : qa_sub3(qa_add3(origin, mins), epsilon);
  qa_vec3 absmax = rotated ? qa_add3(qa_add3(origin, (qa_vec3){radius, radius, radius}), epsilon)
      : qa_add3(qa_add3(origin, maxs), epsilon);
  qa_qvm_shared_entity_set_absmin(entity, absmin);
  qa_qvm_shared_entity_set_absmax(entity, absmax);
  qa_source_clip_models *models = qa_shared_native_q3_clip_models(records->host.scene);
  qa_bounds bounds = {absmin, absmax};
  qa_leaf_query_result leaves;
  bool native_leaves = false;
  double last_leaf = 0;
  if (models) {
    /* Landed native boxLeafnums equivalent is the shared query. */
    (void)native_leaves;
  }
  leaves = qa_shared_box_leaves(records->host.scene, bounds, 128);
  /* cluster order: sort by cluster when not native (approximation of TS sort). */
  double sorted[512]; size_t sorted_count = leaves.leaves.count < 512 ? leaves.leaves.count : 512;
  for (size_t i = 0; i < sorted_count; i++) sorted[i] = leaves.leaves.values[i];
  for (size_t i = 0; i < sorted_count; i++)
    for (size_t j = i + 1; j < sorted_count; j++)
      if (qa_shared_leaf_cluster(records->host.scene, sorted[j]) < qa_shared_leaf_cluster(records->host.scene, sorted[i])) {
        double tmp = sorted[i]; sorted[i] = sorted[j]; sorted[j] = tmp;
      }
  if (sorted_count) last_leaf = sorted[sorted_count - 1];
  link_entry link; memset(&link, 0, sizeof link);
  link.slot = slot; link.areanum = -1; link.areanum2 = -1;
  for (size_t i = 0; i < leaves.leaves.count; i++) {
    double area = qa_shared_leaf_area(records->host.scene, leaves.leaves.values[i]);
    if (area == -1) continue;
    if (link.areanum != -1 && link.areanum != (int64_t)area) link.areanum2 = (int64_t)area;
    else link.areanum = (int64_t)area;
  }
  for (size_t i = 0; i < sorted_count; i++) {
    double cluster = qa_shared_leaf_cluster(records->host.scene, sorted[i]);
    if (cluster == -1) continue;
    if (link.cluster_count < 16) link.clusters[link.cluster_count++] = (int64_t)cluster;
    if (link.cluster_count == 16) { link.last_cluster = (int64_t)qa_shared_leaf_cluster(records->host.scene, last_leaf); break; }
  }
  bool stored = false;
  for (size_t i = 0; i < records->link_count; i++) if (records->links[i].slot == slot) { records->links[i] = link; stored = true; }
  if (!stored && records->link_count < 1024) records->links[records->link_count++] = link;
  if (leaves.leaves.count == 0) return;
  qa_actor_id owner_storage;
  records->host.collision(records->host.ctx, actor, qa_app_q3_guest_records_collision(records, slot, &owner_storage));
  qa_shared_body_table_link(records->host.bodies, actor, NULL);
  qa_qvm_shared_entity_set_linkcount(entity, (int32_t)(qa_qvm_shared_entity_get_linkcount(entity) + 1));
  qa_qvm_shared_entity_set_linked(entity, true);
}
void qa_app_q3_guest_records_unlink(qa_app_q3_guest_records *records, double slot) {
  qa_qvm_shared_entity_set_linked(qa_app_q3_guest_records_entity(records, slot), false);
  actor_entry *entry = find_actor(records, slot);
  if (entry) qa_shared_body_table_unlink(records->host.bodies, entry->actor);
}
bool qa_app_q3_guest_records_visibility(qa_app_q3_guest_records *records, double slot, qa_app_q3_visibility_link *out) {
  (void)qa_app_q3_guest_records_entity(records, slot);
  for (size_t i = 0; i < records->link_count; i++) {
    if (records->links[i].slot == slot) {
      out->areanum = records->links[i].areanum; out->areanum2 = records->links[i].areanum2;
      out->clusters = records->links[i].clusters; out->cluster_count = records->links[i].cluster_count;
      out->last_cluster = records->links[i].last_cluster;
      return true;
    }
  }
  return false;
}
static qa_save_value *saved_actor_value(qa_saved_actor_id saved) {
  qa_save_value *object = qa_save_value_object();
  if (!object) return NULL;
  if (!qa_save_value_set(object, "slot", qa_save_value_number(saved.slot))
      || !qa_save_value_set(object, "generation", qa_save_value_number(saved.generation))) { qa_save_value_free(object); return NULL; }
  return object;
}
qa_save_value *qa_app_q3_guest_records_capture_checkpoint(qa_app_q3_guest_records *records) {
  if (records->closed) qa_range_error("Q3 guest records are retired");
  if (records->retired_count || records->input_motion_count) qa_range_error("Q3 input must finish before checkpoint");
  qa_save_value *list = qa_save_value_array();
  if (!list) return NULL;
  bool ok = true;
  for (size_t i = 0; ok && i < records->actor_count; i++) {
    if (records->actors[i].foreign) continue;
    qa_session_actor_registry_assert_owned(records->host.actors, records->actors[i].actor);
    qa_save_value *entry = qa_save_value_object();
    ok = entry && qa_save_value_set(entry, "slot", qa_save_value_number(records->actors[i].slot))
        && qa_save_value_set(entry, "actor", saved_actor_value(qa_saved_actor_id_of(records->actors[i].actor.id)));
    link_entry *link = NULL;
    for (size_t j = 0; j < records->link_count; j++) if (records->links[j].slot == records->actors[i].slot) link = &records->links[j];
    if (ok && !link) ok = qa_save_value_set(entry, "visibility", qa_save_value_null());
    if (ok && link) {
      qa_save_value *visibility = qa_save_value_object(), *clusters = qa_save_value_array();
      ok = visibility && clusters;
      for (size_t j = 0; ok && j < link->cluster_count; j++) ok = qa_save_value_push(clusters, qa_save_value_number((double)link->clusters[j]));
      ok = ok && qa_save_value_set(visibility, "areanum", qa_save_value_number((double)link->areanum))
          && qa_save_value_set(visibility, "areanum2", qa_save_value_number((double)link->areanum2))
          && qa_save_value_set(visibility, "clusters", clusters)
          && qa_save_value_set(visibility, "lastCluster", qa_save_value_number((double)link->last_cluster))
          && qa_save_value_set(entry, "visibility", visibility);
      if (!ok) { qa_save_value_free(visibility); }
    }
    ok = ok && qa_save_value_push(list, entry);
    if (!ok) qa_save_value_free(entry);
  }
  if (!ok) { qa_save_value_free(list); return NULL; }
  return list;
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
void qa_app_q3_guest_records_restore_checkpoint(qa_app_q3_guest_records *records, const qa_save_value *value) {
  if (records->closed || records->actor_count != 0) qa_range_error("Q3 guest records restore requires a fresh candidate");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "q3.guest.records")) qa_range_error("Out of memory");
  typedef struct binding_row { double slot; qa_owned_actor actor; bool has_link; link_entry link; } binding_row;
  binding_row bindings[1024]; size_t binding_count = 0;
  size_t count = qa_save_reader_list_count(&reader);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&reader, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader slot_reader = field(&entry, "slot");
    double slot = qa_save_reader_integer_min(&slot_reader, 0);
    qa_save_reader_destroy(&slot_reader);
    qa_save_reader actor_reader = field(&entry, "actor");
    qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_owned_actor actor;
    bool resolved = qa_session_actor_registry_resolve_saved(records->host.actors, saved, &actor);
    qa_owned_actor at_source;
    bool at = qa_session_actor_registry_at_source(records->host.actors, records->host.provider, (int64_t)slot, &at_source);
    bool duplicate_slot = false, duplicate_actor = false;
    for (size_t j = 0; j < binding_count; j++) {
      if (bindings[j].slot == slot) duplicate_slot = true;
      if (resolved && qa_same_actor(bindings[j].actor.id, actor.id) && bindings[j].actor.token == actor.token) duplicate_actor = true;
    }
    qa_save_reader visibility = field(&entry, "visibility");
    binding_row row; memset(&row, 0, sizeof row);
    row.slot = slot;
    if (resolved) row.actor = actor;
    if (!qa_save_reader_is_null(&visibility)) {
      row.has_link = true; row.link.slot = slot;
      qa_save_reader r = field(&visibility, "areanum"); row.link.areanum = (int64_t)qa_save_reader_integer_min(&r, -1); qa_save_reader_destroy(&r);
      r = field(&visibility, "areanum2"); row.link.areanum2 = (int64_t)qa_save_reader_integer_min(&r, -1); qa_save_reader_destroy(&r);
      r = field(&visibility, "clusters");
      size_t clusters = qa_save_reader_list_count(&r);
      for (size_t j = 0; j < clusters && j < 16; j++) {
        qa_save_reader item;
        if (!qa_save_reader_item(&r, j, &item)) qa_range_error("Out of memory");
        row.link.clusters[row.link.cluster_count++] = (int64_t)qa_save_reader_integer_min(&item, 0);
        qa_save_reader_destroy(&item);
      }
      if (clusters > 16) row.link.cluster_count = clusters;
      qa_save_reader_destroy(&r);
      r = field(&visibility, "lastCluster"); row.link.last_cluster = (int64_t)qa_save_reader_integer_min(&r, -1); qa_save_reader_destroy(&r);
    }
    qa_save_reader_destroy(&visibility);
    (void)qa_app_q3_guest_records_entity(records, slot);
    if (slot >= 1022 || duplicate_slot || !resolved || duplicate_actor
        || !actor.owner || !records->host.provider || strcmp(actor.owner, records->host.provider) != 0
        || !at || !qa_same_actor(at_source.id, actor.id) || at_source.token != actor.token
        || (row.has_link && row.link.cluster_count > 16))
      qa_save_reader_fail(&reader, "invalid source actor or visibility binding");
    bindings[binding_count++] = row;
    qa_save_reader_destroy(&entry);
  }
  size_t owned_count = 0;
  const qa_owned_actor *owned = qa_session_actor_registry_owned_by(records->host.actors, records->host.provider, &owned_count);
  size_t expected = 0;
  for (size_t i = 0; i < owned_count; i++) {
    qa_actor_source_result source = qa_session_actor_registry_source_of(records->host.actors, owned[i].id);
    if (source.has_value && source.provider && strcmp(source.provider, records->host.provider) == 0 && source.slot != 1022) expected++;
  }
  if (binding_count != expected) qa_save_reader_fail(&reader, "guest record checkpoint omits owned actors");
  qa_save_reader_destroy(&reader);
  for (size_t i = 0; i < binding_count; i++) {
    bind_actor(records, bindings[i].slot, bindings[i].actor);
    if (bindings[i].has_link && records->link_count < 1024) records->links[records->link_count++] = bindings[i].link;
  }
}
void qa_app_q3_guest_records_release_client(qa_app_q3_guest_records *records, double slot) {
  if (slot != (double)(long long)slot || slot < 0 || slot >= qa_qvm_game_data_get_num_clients(records->data))
    qa_range_error("Q3 client slot is outside configured game data");
  if (slot < qa_qvm_game_data_get_num_entities(records->data)) qa_app_q3_guest_records_unlink(records, slot);
  actor_entry *entry = find_actor(records, slot);
  if (entry) qa_session_actor_registry_release(records->host.actors, entry->actor);
}
void qa_app_q3_guest_records_retire_input_client(qa_app_q3_guest_records *records, double slot) {
  list_add(records->retired, &records->retired_count, slot);
  qa_app_q3_guest_records_unlink(records, slot);
}
void qa_app_q3_guest_records_finish_input_retirement(qa_app_q3_guest_records *records, double slot) {
  list_remove(records->retired, &records->retired_count, slot);
}
bool qa_app_q3_guest_records_is_input_retired(qa_app_q3_guest_records *records, double slot) {
  return in_list(records->retired, records->retired_count, slot);
}
void qa_app_q3_guest_records_with_input_motion(qa_app_q3_guest_records *records, double slot,
    void (*run)(void *ctx), void *run_ctx) {
  bool previous = in_list(records->input_motion, records->input_motion_count, slot);
  list_add(records->input_motion, &records->input_motion_count, slot);
  run(run_ctx);
  if (!previous) list_remove(records->input_motion, &records->input_motion_count, slot);
}
void qa_app_q3_guest_records_close(qa_app_q3_guest_records *records) {
  if (records->closed) return;
  for (size_t i = 0; i < records->actor_count; i++) {
    if (records->actors[i].slot < qa_qvm_game_data_get_num_entities(records->data))
      qa_qvm_shared_entity_set_linked(qa_qvm_game_data_entity(records->data, records->actors[i].slot), false);
    qa_session_actor_registry_release(records->host.actors, records->actors[i].actor);
  }
  records->closed = true;
  qa_session_actor_registry_off_release(records->host.actors, records->release_subscription);
  records->link_count = 0;
}
