/* Port of src/app/bootstrap/simulation/q3/guest-spatial.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_spatial.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/entity_record.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/world/collision/q3/clip_models.h"
#include "qa/world/collision/q3/model.h"
#include "qa/world/spatial/index.h"

struct qa_app_q3_guest_spatial {
  qa_app_q3_guest_spatial_dependencies dependencies;
  qa_collision_counters counters;
  qa_collision_box_storage box_storage;
  qa_actor_id ignore_storage[1024];
  qa_actor_id owner_storage;
};

static qa_qvm_entity_state state_of(qa_qvm_shared_entity *entity) {
  qa_qvmx_view view = entity->s.view;
  return qa_qvm_borrow_entity_state(view, entity->profile);
}
static bool passes(qa_app_q3_guest_spatial *spatial, double entity_num, int32_t pass_entity_num) {
  qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(spatial->dependencies.records, entity_num);
  int32_t owner = qa_qvm_shared_entity_get_owner_num(entity);
  if (entity_num == pass_entity_num || owner == pass_entity_num) return false;
  if ((double)pass_entity_num < qa_qvm_game_data_get_num_entities(qa_app_q3_guest_records_data(spatial->dependencies.records))) {
    qa_qvm_shared_entity *pass = qa_app_q3_guest_records_entity(spatial->dependencies.records, pass_entity_num);
    if (qa_qvm_shared_entity_get_owner_num(pass) == (int32_t)entity_num) return false;
  }
  return true;
}
static qa_trace_result scene_trace(qa_app_q3_guest_spatial *spatial, const qa_qvm_server_trace_query *input,
    const qa_actor_id *excluded, size_t excluded_count) {
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = input->start; query.end = input->end;
  bool point = input->shape_bounds.min.x == 0 && input->shape_bounds.min.y == 0 && input->shape_bounds.min.z == 0
      && input->shape_bounds.max.x == 0 && input->shape_bounds.max.y == 0 && input->shape_bounds.max.z == 0;
  query.shape.kind = point ? "point" : input->shape_kind;
  query.shape.bounds = input->shape_bounds;
  query.policy.kind = "q3"; query.policy.contents_mask = input->mask; query.policy.curves = true; query.policy.player_curve_clip = true;
  query.numeric = qa_q3_binary32_profile;
  return qa_shared_trace_excluding(spatial->dependencies.scene, query, excluded, excluded_count);
}
qa_qvm_trace_record qa_app_q3_guest_spatial_trace(qa_app_q3_guest_spatial *spatial, const qa_qvm_server_trace_query *input) {
  double entities = qa_qvm_game_data_get_num_entities(qa_app_q3_guest_records_data(spatial->dependencies.records));
  size_t ignore_count = 0;
  if (input->pass_entity_num != 1023 && (double)input->pass_entity_num < entities) {
    spatial->ignore_storage[ignore_count++] =
        qa_app_q3_guest_records_actor(spatial->dependencies.records, input->pass_entity_num).id;
  }
  for (size_t attempt = 0; attempt <= 1024; attempt++) {
    qa_trace_result hit = scene_trace(spatial, input, spatial->ignore_storage, ignore_count);
    double entity_num = hit.hit.kind == QA_TRACE_HIT_WORLD ? 1022 : 1023;
    if (hit.hit.kind == QA_TRACE_HIT_ACTOR)
      (void)qa_app_q3_guest_records_slot(spatial->dependencies.records, hit.hit.actor, &entity_num);
    if (entity_num == 1023 || entity_num == 1022 || passes(spatial, entity_num, input->pass_entity_num)) {
      qa_qvm_trace_record result; memset(&result, 0, sizeof result);
      result.result.all_solid = hit.all_solid; result.result.start_solid = hit.start_solid;
      result.result.fraction = hit.fraction; result.result.end = hit.end;
      result.result.plane.plane = hit.contact.plane;
      result.result.plane.type = hit.source_plane.type; result.result.plane.signbits = hit.source_plane.signbits;
      result.result.surface_flags = hit.surface_flags; result.result.contents = hit.contents;
      result.entity_num = (int32_t)entity_num;
      return result;
    }
    if (hit.hit.kind == QA_TRACE_HIT_ACTOR && ignore_count < 1024) spatial->ignore_storage[ignore_count++] = hit.hit.actor;
  }
  qa_range_error("Q3 guest trace did not terminate");
}
static double transformed_contents(qa_app_q3_guest_spatial *spatial, qa_qvm_shared_entity *entity, qa_vec3 point) {
  qa_qvm_entity_collision_model model = qa_qvm_shared_entity_get_model(entity);
  if (model.kind && strcmp(model.kind, "inline") == 0) {
    qa_source_clip_models *models = qa_shared_native_q3_clip_models(spatial->dependencies.scene);
    if (!models) qa_range_error("Q3 pointContents requires native clip model support");
    return qa_source_clip_point_contents(models, point, qa_source_clip_inline_model(models, model.index));
  }
  qa_bounds bounds = {qa_qvm_shared_entity_get_mins(entity), qa_qvm_shared_entity_get_maxs(entity)};
  qa_temporary_collision_model *temporary = model.kind && strcmp(model.kind, "capsule") == 0
      ? qa_create_capsule_model(bounds, &spatial->counters, &spatial->box_storage)
      : qa_create_box_model(bounds, &spatial->counters, &spatial->box_storage);
  double contents = qa_temporary_model_transformed_point_contents(temporary, point,
      qa_qvm_shared_entity_get_current_origin(entity), qa_qvm_shared_entity_get_current_angles(entity));
  qa_temporary_collision_model_destroy(temporary);
  return contents;
}
int32_t qa_app_q3_guest_spatial_point_contents(qa_app_q3_guest_spatial *spatial, qa_vec3 point, int32_t pass_entity_num) {
  qa_point_contents_query query; memset(&query, 0, sizeof query);
  query.point = point;
  query.policy.kind = "q3"; query.policy.curves = true; query.policy.player_curve_clip = true;
  query.numeric = qa_q3_binary32_profile;
  int32_t contents = (int32_t)qa_shared_point_contents(spatial->dependencies.scene, query).contents;
  double entities = qa_qvm_game_data_get_num_entities(qa_app_q3_guest_records_data(spatial->dependencies.records));
  for (double slot = 0; slot < entities; slot++) {
    if (!passes(spatial, slot, pass_entity_num)) continue;
    qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(spatial->dependencies.records, slot);
    if (!qa_qvm_shared_entity_get_linked(entity) || ((qa_qvm_shared_entity_get_sv_flags(entity)) & QA_Q3_BASE_SVF_NOCLIENT) != 0) continue;
    qa_vec3 absmin = qa_qvm_shared_entity_get_absmin(entity), absmax = qa_qvm_shared_entity_get_absmax(entity);
    if (point.x < absmin.x || point.x > absmax.x || point.y < absmin.y || point.y > absmax.y || point.z < absmin.z || point.z > absmax.z) continue;
    contents |= (int32_t)transformed_contents(spatial, entity, point);
  }
  return contents;
}
size_t qa_app_q3_guest_spatial_area_entities(qa_app_q3_guest_spatial *spatial, qa_bounds bounds, int32_t maximum, int32_t *out, size_t capacity) {
  size_t count = 0;
  const qa_spatial_actor *found = qa_shared_query_actors(spatial->dependencies.scene, bounds, "solid", &count);
  size_t total = 0;
  for (size_t i = 0; i < count; i++) {
    if (!qa_bounds_intersect(found[i].body.absolute_bounds, bounds)) continue;
    double slot;
    if (!qa_app_q3_guest_records_slot(spatial->dependencies.records, found[i].body.actor, &slot)) continue;
    /* TS areaEntities: a negative maximum returns the full list. */
    if (maximum >= 0 && (int64_t)total >= maximum) break;
    if (total < capacity) out[total] = (int32_t)slot;
    total++;
  }
  return total;
}
bool qa_app_q3_guest_spatial_entity_contact(qa_app_q3_guest_spatial *spatial, qa_bounds bounds, int32_t slot, bool capsule) {
  qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(spatial->dependencies.records, slot);
  qa_qvm_entity_collision_model model = qa_qvm_shared_entity_get_model(entity);
  qa_vec3 origin = qa_qvm_shared_entity_get_current_origin(entity);
  if (model.kind && strcmp(model.kind, "inline") == 0) {
    qa_source_clip_models *models = qa_shared_native_q3_clip_models(spatial->dependencies.scene);
    if (!models) qa_range_error("Q3 entityContact requires native clip model support");
    qa_temporary_trace_query query; memset(&query, 0, sizeof query);
    query.start = origin; query.end = origin;
    query.shape.kind = capsule ? "capsule" : "box"; query.shape.mins = bounds.min; query.shape.maxs = bounds.max;
    return qa_source_clip_trace(models, query, qa_source_clip_inline_model(models, model.index)).start_solid
        || qa_source_clip_point_contents(models, origin, qa_source_clip_inline_model(models, model.index)) != 0;
  }
  qa_bounds entity_bounds = {qa_qvm_shared_entity_get_mins(entity), qa_qvm_shared_entity_get_maxs(entity)};
  qa_temporary_collision_model *temporary = model.kind && strcmp(model.kind, "capsule") == 0
      ? qa_create_capsule_model(entity_bounds, &spatial->counters, &spatial->box_storage)
      : qa_create_box_model(entity_bounds, &spatial->counters, &spatial->box_storage);
  qa_temporary_trace_query query; memset(&query, 0, sizeof query);
  query.start = origin; query.end = origin;
  query.shape.kind = capsule ? "capsule" : "box"; query.shape.mins = bounds.min; query.shape.maxs = bounds.max;
  bool contact = qa_temporary_model_transformed_trace_source(temporary, query, origin,
      qa_qvm_shared_entity_get_current_angles(entity)).start_solid
      || qa_temporary_model_transformed_point_contents(temporary, origin, origin,
          qa_qvm_shared_entity_get_current_angles(entity)) != 0;
  qa_temporary_collision_model_destroy(temporary);
  return contact;
}
void qa_app_q3_guest_spatial_set_brush_model(qa_app_q3_guest_spatial *spatial, int32_t slot, const char *name) {
  qa_owned_actor actor = qa_app_q3_guest_records_actor(spatial->dependencies.records, slot);
  qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(spatial->dependencies.records, slot);
  qa_qvm_entity_state state = state_of(entity);
  qa_shared_body_table_unlink(qa_app_q3_guest_records_host_of(spatial->dependencies.records)->bodies, actor);
  if (!name || name[0] != '*') qa_range_error("G_ModelIndex: model name '%s' is not an inline model", name ? name : "");
  double model_index = qa_native_atoi(name + 1);
  qa_source_clip_models *models = qa_shared_native_q3_clip_models(spatial->dependencies.scene);
  if (!models) qa_range_error("Q3 setBrushModel requires native clip model support");
  double handle = qa_source_clip_inline_model(models, model_index);
  qa_bounds bounds = qa_source_clip_model_bounds(models, handle);
  qa_qvm_entity_collision_model model = {"inline", (int32_t)model_index};
  qa_qvm_shared_entity_set_model(entity, model);
  qa_qvm_shared_entity_set_mins(entity, bounds.min);
  qa_qvm_shared_entity_set_maxs(entity, bounds.max);
  qa_qvm_shared_entity_set_linked(entity, false);
  qa_qvm_entity_state_set_solid(&state, 0xffffff);
  qa_qvm_shared_entity_set_contents(entity, 1);
  spatial->dependencies.collision(spatial->dependencies.ctx, actor,
      qa_app_q3_guest_records_collision(spatial->dependencies.records, slot, &spatial->owner_storage));
}
void qa_app_q3_guest_spatial_adjust_area_portal_state(qa_app_q3_guest_spatial *spatial, int32_t slot, bool open) {
  qa_app_q3_visibility_link link;
  if (!qa_app_q3_guest_records_visibility(spatial->dependencies.records, slot, &link)) return;
  if (link.areanum >= 0 && link.areanum2 >= 0)
    qa_app_q3_guest_world_adjust_area_portal_state(spatial->dependencies.world, (double)link.areanum, (double)link.areanum2, open);
}
static bool point_connected(qa_app_q3_guest_spatial *spatial, qa_vec3 first, qa_vec3 second, bool ignore_portals) {
  double first_area = qa_shared_leaf_area(spatial->dependencies.scene, qa_shared_point_leaf(spatial->dependencies.scene, first));
  double second_area = qa_shared_leaf_area(spatial->dependencies.scene, qa_shared_point_leaf(spatial->dependencies.scene, second));
  if (first_area == -1 || second_area == -1) return false;
  if (!ignore_portals && !qa_shared_areas_connected(spatial->dependencies.scene, first_area, second_area)) return false;
  double first_cluster = qa_shared_leaf_cluster(spatial->dependencies.scene, qa_shared_point_leaf(spatial->dependencies.scene, first));
  double second_cluster = qa_shared_leaf_cluster(spatial->dependencies.scene, qa_shared_point_leaf(spatial->dependencies.scene, second));
  return qa_shared_cluster_visible(spatial->dependencies.scene, first_cluster, second_cluster, "pvs");
}
bool qa_app_q3_guest_spatial_in_pvs(qa_app_q3_guest_spatial *spatial, qa_vec3 first, qa_vec3 second, bool ignore_portals) {
  qa_decoded_world geometry = spatial->dependencies.scene->geometry;
  if (geometry.kind != QA_DECODED_WORLD_Q3) return point_connected(spatial, first, second, ignore_portals);
  qa_source_clip_models *models = qa_shared_native_q3_clip_models(spatial->dependencies.scene);
  if (!models) qa_range_error("Q3 inPVS requires the exact selected Q3 world");
  double first_cluster = qa_collision_world_leaf_cluster(models->world, qa_collision_world_point_leafnum(models->world, first));
  double second_cluster = qa_collision_world_leaf_cluster(models->world, qa_collision_world_point_leafnum(models->world, second));
  (void)ignore_portals;
  return qa_collision_world_cluster_visible(models->world, first_cluster, second_cluster);
}
bool qa_app_q3_guest_spatial_areas_connected(qa_app_q3_guest_spatial *spatial, int32_t first, int32_t second) {
  qa_decoded_world geometry = spatial->dependencies.scene->geometry;
  if (geometry.kind != QA_DECODED_WORLD_Q3) return qa_shared_areas_connected(spatial->dependencies.scene, first, second);
  qa_source_clip_models *models = qa_shared_native_q3_clip_models(spatial->dependencies.scene);
  if (!models) qa_range_error("Q3 areasConnected requires the exact selected Q3 world");
  return qa_collision_world_areas_connected(models->world, first, second);
}
static qa_qvm_trace_record ops_trace(void *v, const qa_qvm_server_trace_query *input) {
  return qa_app_q3_guest_spatial_trace((qa_app_q3_guest_spatial *)v, input);
}
static int32_t ops_contents(void *v, qa_vec3 point, int32_t pass) {
  return qa_app_q3_guest_spatial_point_contents((qa_app_q3_guest_spatial *)v, point, pass);
}
static size_t ops_area(void *v, qa_bounds bounds, int32_t maximum, int32_t *out, size_t capacity) {
  return qa_app_q3_guest_spatial_area_entities((qa_app_q3_guest_spatial *)v, bounds, maximum, out, capacity);
}
static bool ops_contact(void *v, qa_bounds bounds, int32_t slot, bool capsule) {
  return qa_app_q3_guest_spatial_entity_contact((qa_app_q3_guest_spatial *)v, bounds, slot, capsule);
}
static void ops_brush(void *v, int32_t slot, const char *name) { qa_app_q3_guest_spatial_set_brush_model((qa_app_q3_guest_spatial *)v, slot, name); }
static void ops_portal(void *v, int32_t slot, bool open) { qa_app_q3_guest_spatial_adjust_area_portal_state((qa_app_q3_guest_spatial *)v, slot, open); }
static bool ops_pvs(void *v, qa_vec3 first, qa_vec3 second, bool ignore) { return qa_app_q3_guest_spatial_in_pvs((qa_app_q3_guest_spatial *)v, first, second, ignore); }
static bool ops_areas(void *v, int32_t first, int32_t second) { return qa_app_q3_guest_spatial_areas_connected((qa_app_q3_guest_spatial *)v, first, second); }
static void ops_link(void *v, int32_t slot) { qa_app_q3_guest_records_link(((qa_app_q3_guest_spatial *)v)->dependencies.records, slot); }
static void ops_unlink(void *v, int32_t slot) { qa_app_q3_guest_records_unlink(((qa_app_q3_guest_spatial *)v)->dependencies.records, slot); }
qa_qvm_server_spatial_operations qa_app_q3_guest_spatial_operations(qa_app_q3_guest_spatial *spatial) {
  qa_qvm_server_spatial_operations ops; memset(&ops, 0, sizeof ops);
  ops.ctx = spatial; ops.trace = ops_trace; ops.point_contents = ops_contents; ops.area_entities = ops_area;
  ops.entity_contact = ops_contact; ops.set_brush_model = ops_brush; ops.adjust_area_portal_state = ops_portal;
  ops.in_pvs = ops_pvs; ops.areas_connected = ops_areas; ops.link = ops_link; ops.unlink = ops_unlink;
  return ops;
}
qa_app_q3_guest_spatial *qa_app_q3_guest_spatial_create(qa_app_q3_guest_spatial_dependencies dependencies) {
  qa_app_q3_guest_spatial *spatial = calloc(1, sizeof *spatial);
  if (!spatial) return NULL;
  spatial->dependencies = dependencies;
  memset(&spatial->counters, 0, sizeof spatial->counters);
  memset(&spatial->box_storage, 0, sizeof spatial->box_storage);
  return spatial;
}
void qa_app_q3_guest_spatial_destroy(qa_app_q3_guest_spatial *spatial) { free(spatial); }
