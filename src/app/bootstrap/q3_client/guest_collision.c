/* Port of src/app/bootstrap/q3-client/guest-collision.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/guest_collision.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/shared.h"
#include "qa/world/collision/q3/model.h"
static const qa_vec3 zero_v = {0, 0, 0};
struct qa_app_shared_qvm_client_clip_models {
  qa_shared_scene_queries *scene;
  qa_app_qvm_cvar_services cvars;
  qa_bounds bounds;
  qa_collision_counters counters;
  qa_temporary_collision_model *box;
  qa_collision_box_storage *box_storage;
  bool has_nodes;
};
static double geometry_model_count(const qa_decoded_world *world) {
  /* TS geometry.models.length; the guest clip runs on Q3 worlds. */
  return world->kind == QA_DECODED_WORLD_Q3 && world->as.q3 != NULL
    ? (double)world->as.q3->model_count : 0;
}
static bool geometry_has_nodes(const qa_decoded_world *world) {
  return world->kind == QA_DECODED_WORLD_Q3 && world->as.q3 != NULL
    ? world->as.q3->node_count != 0 : false;
}
qa_app_shared_qvm_client_clip_models *qa_app_shared_qvm_client_clip_models_create(
    qa_shared_scene_queries *scene, qa_app_qvm_cvar_services cvars) {
  qa_app_shared_qvm_client_clip_models *m = calloc(1, sizeof *m);
  if (m == NULL) qa_range_error("Out of memory");
  m->scene = scene;
  m->cvars = cvars;
  m->bounds.min = zero_v;
  m->bounds.max = zero_v;
  qa_collision_counters_init(&m->counters);
  m->box = qa_create_box_model(m->bounds, &m->counters, m->box_storage);
  m->has_nodes = geometry_has_nodes(&scene->geometry);
  return m;
}
void qa_app_shared_qvm_client_clip_models_destroy(qa_app_shared_qvm_client_clip_models *models) {
  if (models == NULL) return;
  qa_temporary_collision_model_destroy(models->box);
  free(models);
}
bool qa_app_shared_qvm_client_clip_models_has_nodes(const qa_app_shared_qvm_client_clip_models *models) {
  return models->has_nodes;
}
static qa_save_value *bounds_value(qa_bounds b) {
  qa_save_value *o = qa_save_value_object();
  qa_save_value *mn = qa_save_value_object(), *mx = qa_save_value_object();
  qa_save_value_set(mn, "x", qa_save_value_number(b.min.x));
  qa_save_value_set(mn, "y", qa_save_value_number(b.min.y));
  qa_save_value_set(mn, "z", qa_save_value_number(b.min.z));
  qa_save_value_set(mx, "x", qa_save_value_number(b.max.x));
  qa_save_value_set(mx, "y", qa_save_value_number(b.max.y));
  qa_save_value_set(mx, "z", qa_save_value_number(b.max.z));
  qa_save_value_set(o, "min", mn);
  qa_save_value_set(o, "max", mx);
  return o;
}
qa_save_value *qa_app_shared_qvm_client_clip_models_capture_temporary_checkpoint(
    qa_app_shared_qvm_client_clip_models *models) {
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "bounds", bounds_value(models->bounds));
  qa_save_value_set(o, "box", bounds_value(qa_temporary_collision_model_bounds(models->box)));
  return o;
}
void qa_app_shared_qvm_client_clip_models_restore_temporary_checkpoint(
    qa_app_shared_qvm_client_clip_models *models, const qa_save_value *value) {
  qa_save_reader r;
  qa_save_reader_init(&r, value, "cgame-temporary-collision");
  qa_save_reader f;
  qa_save_reader_field(&r, "box", &f);
  qa_bounds box = qa_read_bounds(&f);
  qa_save_reader_destroy(&f);
  qa_save_reader_field(&r, "bounds", &f);
  qa_bounds bounds = qa_read_bounds(&f);
  qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&r);
  qa_app_shared_qvm_client_clip_models_temp_box_model(models, box.min, box.max, false);
  qa_app_shared_qvm_client_clip_models_temp_box_model(models, bounds.min, bounds.max, true);
}
double qa_app_shared_qvm_client_clip_models_model_count(const qa_app_shared_qvm_client_clip_models *models) {
  return geometry_model_count(&models->scene->geometry);
}
double qa_app_shared_qvm_client_clip_models_inline_model(qa_app_shared_qvm_client_clip_models *models, double index) {
  if (index != trunc(index) || index < 0 || index >= qa_app_shared_qvm_client_clip_models_model_count(models))
    qa_range_error("CM_InlineModel: bad number");
  return index;
}
double qa_app_shared_qvm_client_clip_models_temp_box_model(
    qa_app_shared_qvm_client_clip_models *models, qa_vec3 min, qa_vec3 max, bool capsule) {
  models->bounds.min = min;
  models->bounds.max = max;
  if (!capsule) {
    qa_temporary_collision_model_destroy(models->box);
    models->box = qa_create_box_model(models->bounds, &models->counters, models->box_storage);
  }
  return capsule ? QA_SOURCE_CAPSULE_MODEL_HANDLE : QA_SOURCE_BOX_MODEL_HANDLE;
}
/* TS temporary(): NULL return marks a real model handle via *is_model. */
static qa_temporary_collision_model *temporary(qa_app_shared_qvm_client_clip_models *m, double handle, bool *is_model) {
  *is_model = false;
  if (handle == trunc(handle) && handle >= 0 && handle < qa_app_shared_qvm_client_clip_models_model_count(m)) {
    *is_model = true;
    return NULL;
  }
  if (handle == QA_SOURCE_BOX_MODEL_HANDLE) return m->box;
  if (handle == QA_SOURCE_CAPSULE_MODEL_HANDLE) {
    /* TS constructs a fresh capsule per query; leaked capsule models
     * are parked in a small freestanding form: created on demand and
     * destroyed by the caller via capsule_out pattern below. */
    return NULL; /* handled by callers through make_capsule */
  }
  qa_range_error("CM_ClipHandleToModel: bad handle %g", handle);
}
static qa_trace_policy policy(qa_app_shared_qvm_client_clip_models *m) {
  qa_cvar_snapshot snap;
  double no_curves = 0, player_clip = 1;
  if (m->cvars.get(m->cvars.ctx, "cm_noCurves", &snap)) { no_curves = snap.integer_value; qa_cvar_read_free(&snap); }
  if (m->cvars.get(m->cvars.ctx, "cm_playerCurveClip", &snap)) { player_clip = snap.integer_value; qa_cvar_read_free(&snap); }
  return (qa_trace_policy){"q3", NULL, false, 0, -1, NULL, no_curves == 0, player_clip != 0};
}
double qa_app_shared_qvm_client_clip_models_point_contents(
    qa_app_shared_qvm_client_clip_models *models, qa_vec3 point, double handle) {
  return qa_app_shared_qvm_client_clip_models_transformed_point_contents(models, point, handle, zero_v, zero_v);
}
double qa_app_shared_qvm_client_clip_models_transformed_point_contents(
    qa_app_shared_qvm_client_clip_models *models, qa_vec3 point, double handle, qa_vec3 origin, qa_vec3 angles) {
  bool is_model = false;
  qa_temporary_collision_model *temp = temporary(models, handle, &is_model);
  if (!models->has_nodes) return 0;
  if (!is_model) {
    if (temp == NULL) { /* capsule */
      qa_temporary_collision_model *capsule = qa_create_capsule_model(models->bounds, &models->counters, NULL);
      double out = qa_temporary_model_transformed_point_contents(capsule, point, origin, angles);
      qa_temporary_collision_model_destroy(capsule);
      return out;
    }
    return qa_temporary_model_transformed_point_contents(temp, point, origin, angles);
  }
  qa_point_contents_query q;
  q.point = point;
  q.target.is_model = true;
  q.target.model = handle;
  q.target.origin = origin;
  q.target.angles = angles;
  q.pass_actor = NULL;
  q.numeric = qa_q3_binary32_profile;
  q.policy = policy(models);
  qa_point_contents_result result = qa_shared_point_contents(models->scene, q);
  if (result.kind == NULL || strcmp(result.kind, "q3") != 0)
    qa_range_error("QVM client contents lost Q3 policy");
  return result.contents;
}
bool qa_app_shared_qvm_client_clip_models_trace_without_nodes(
    qa_app_shared_qvm_client_clip_models *models, double handle, qa_source_trace_result *out) {
  bool is_model = false;
  (void)temporary(models, handle, &is_model);
  if (models->has_nodes) return false;
  *out = qa_empty_source_trace();
  return true;
}
qa_source_trace_result qa_app_shared_qvm_client_clip_models_trace(
    qa_app_shared_qvm_client_clip_models *models, qa_temporary_trace_query query, double handle) {
  return qa_app_shared_qvm_client_clip_models_transformed_trace(models, query, handle, zero_v, zero_v);
}
qa_source_trace_result qa_app_shared_qvm_client_clip_models_transformed_trace(
    qa_app_shared_qvm_client_clip_models *models, qa_temporary_trace_query query, double handle,
    qa_vec3 origin, qa_vec3 angles) {
  bool is_model = false;
  qa_temporary_collision_model *temp = temporary(models, handle, &is_model);
  if (!models->has_nodes) {
    qa_source_trace_result out = qa_empty_source_trace();
    out.end = query.end;
    return out;
  }
  if (!is_model) {
    if (temp == NULL) {
      qa_temporary_collision_model *capsule = qa_create_capsule_model(models->bounds, &models->counters, NULL);
      qa_source_trace_result out = qa_temporary_model_transformed_trace_source(capsule, query, origin, angles);
      qa_temporary_collision_model_destroy(capsule);
      return out;
    }
    return qa_temporary_model_transformed_trace_source(temp, query, origin, angles);
  }
  qa_trace_query q;
  q.start = query.start;
  q.end = query.end;
  q.shape.kind = query.shape.kind;
  q.shape.bounds.min = query.shape.mins;
  q.shape.bounds.max = query.shape.maxs;
  q.target.is_model = true;
  q.target.model = handle;
  q.target.origin = origin;
  q.target.angles = angles;
  q.pass_actor = NULL;
  q.numeric = qa_q3_binary32_profile;
  q.policy = policy(models);
  q.policy.contents_mask = query.mask;
  qa_trace_result result = qa_shared_geometry_trace(models->scene, q);
  if (result.kind == NULL || strcmp(result.kind, "q3") != 0)
    qa_range_error("QVM client trace lost Q3 policy");
  qa_source_trace_result out;
  out.fraction = result.fraction;
  out.end = result.end;
  out.all_solid = result.all_solid;
  out.start_solid = result.start_solid;
  out.contents = result.contents;
  out.surface_flags = result.surface_flags;
  out.plane.plane = result.source_plane.plane;
  out.plane.type = result.source_plane.type;
  out.plane.signbits = result.source_plane.signbits;
  return out;
}
