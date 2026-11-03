/* Port of src/app/bootstrap/q3-client/guest-collision.ts
 * (SharedQvmClientClipModels over the shared scene).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_GUEST_COLLISION_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_GUEST_COLLISION_H
#include <stdbool.h>
#include "qa/app/bootstrap/q3_client/cvars.h"
#include "qa/compat/qvm/client_collision_syscalls.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include "qa/world/collision/index.h"
#include "qa/world/collision/q3/clip_models.h"
#include "qa/world/collision/q3/counters.h"
#include "qa/world/collision/q3/world.h"
/* TS SharedQvmClientClipModels. scene and cvars are borrowed. */
typedef struct qa_app_shared_qvm_client_clip_models qa_app_shared_qvm_client_clip_models;
qa_app_shared_qvm_client_clip_models *qa_app_shared_qvm_client_clip_models_create(
    qa_shared_scene_queries *scene, qa_app_qvm_cvar_services cvars);
void qa_app_shared_qvm_client_clip_models_destroy(qa_app_shared_qvm_client_clip_models *models);
/* TS world.hasNodes. */
bool qa_app_shared_qvm_client_clip_models_has_nodes(const qa_app_shared_qvm_client_clip_models *models);
/* TS captureTemporaryCheckpoint(): owned save tree. */
qa_save_value *qa_app_shared_qvm_client_clip_models_capture_temporary_checkpoint(
    qa_app_shared_qvm_client_clip_models *models);
void qa_app_shared_qvm_client_clip_models_restore_temporary_checkpoint(
    qa_app_shared_qvm_client_clip_models *models, const qa_save_value *value);
double qa_app_shared_qvm_client_clip_models_model_count(const qa_app_shared_qvm_client_clip_models *models);
/* Aborts RangeError 'CM_InlineModel: bad number'. */
double qa_app_shared_qvm_client_clip_models_inline_model(qa_app_shared_qvm_client_clip_models *models, double index);
double qa_app_shared_qvm_client_clip_models_temp_box_model(
    qa_app_shared_qvm_client_clip_models *models, qa_vec3 min, qa_vec3 max, bool capsule);
double qa_app_shared_qvm_client_clip_models_point_contents(
    qa_app_shared_qvm_client_clip_models *models, qa_vec3 point, double handle);
double qa_app_shared_qvm_client_clip_models_transformed_point_contents(
    qa_app_shared_qvm_client_clip_models *models, qa_vec3 point, double handle, qa_vec3 origin, qa_vec3 angles);
/* TS traceWithoutNodes: false is the TS null (world has nodes). */
bool qa_app_shared_qvm_client_clip_models_trace_without_nodes(
    qa_app_shared_qvm_client_clip_models *models, double handle, qa_source_trace_result *out);
qa_source_trace_result qa_app_shared_qvm_client_clip_models_trace(
    qa_app_shared_qvm_client_clip_models *models, qa_temporary_trace_query query, double handle);
qa_source_trace_result qa_app_shared_qvm_client_clip_models_transformed_trace(
    qa_app_shared_qvm_client_clip_models *models, qa_temporary_trace_query query, double handle,
    qa_vec3 origin, qa_vec3 angles);
/* TS `implements QvmClientClipModels`: the models as the client collision
 * interface (ctx is the borrowed models). */
qa_qvm_client_clip_models qa_app_shared_qvm_client_clip_models_as_client(
    qa_app_shared_qvm_client_clip_models *models);
#endif
