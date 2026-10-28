/* Port of src/app/bootstrap/q3-client/scene.ts. The per-provider
 * SceneModelRenderer and world material/batch construction are
 * host fn+ctx (render owners); admission routing, render-flag
 * guards, view-weapon offsets and operation ordering of this
 * file are ported here. Preload is synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_SCENE_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_SCENE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/content/q3/presentation/scene.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/render/scene/submissions.h"
#include "qa/render/scene/world.h"
/* TS Q3SceneRenderOptions. view_offset NULL = TS undefined. */
typedef struct qa_app_q3_scene_render_options {
  const qa_vec3 *view_offset;
  bool no_world_model;
  bool split_screen;
  bool supplemental_view_weapon;
} qa_app_q3_scene_render_options;
/* Append-sink for host-produced operations. */
typedef struct qa_app_q3_scene_operation_sink {
  qa_scene_operation *operations;
  size_t count, capacity;
} qa_app_q3_scene_operation_sink;
void qa_app_q3_scene_operation_sink_push(qa_app_q3_scene_operation_sink *sink, qa_scene_operation operation);
/* Host: SceneModelRenderer per provider + world batches. */
typedef struct qa_app_q3_scene_renderer_host {
  void *ctx;
  /* renderer(provider).preload(entities, optionsOf). */
  void (*preload_models)(void *ctx, const qa_app_q3_provider_scene_assets *provider,
                         const qa_q3_pres_presented_model *models, size_t count);
  /* Admission polygon -> one sourceDrawGroup operation. */
  void (*poly_operation)(void *ctx, const qa_q3_pres_admitted_poly *poly, size_t surface_index,
                         const qa_world_view_input *input, qa_app_q3_scene_operation_sink *out);
  /* Default-model batch operation. */
  void (*default_model_operation)(void *ctx, const qa_q3_pres_ref_model_entity *entity, double entity_index,
                                  const qa_world_view_input *input, qa_app_q3_scene_operation_sink *out);
  /* world.prepareModel for brush models. */
  void (*brush_model_operations)(void *ctx, const qa_q3_pres_presented_model *model,
                                 const qa_scene_entity *rendered, const qa_q3_pres_ref_model_entity *entity,
                                 double entity_index, const qa_world_view_input *input, bool weapon_input,
                                 qa_app_q3_scene_operation_sink *out);
  /* renderer(provider).prepare([rendered], selected, options). */
  void (*provider_model_operations)(void *ctx, const qa_app_q3_provider_scene_assets *provider,
                                    const qa_q3_pres_presented_model *model, const qa_scene_entity *rendered,
                                    const qa_q3_pres_ref_model_entity *entity, double entity_index,
                                    const qa_world_view_input *input, bool weapon_input, bool no_world_model,
                                    qa_app_q3_scene_operation_sink *out);
  /* Beam/sprite/rail geometry operation. */
  void (*procedural_operation)(void *ctx, const qa_q3_pres_admitted_ref_entity *entity, double entity_index,
                               const qa_world_view_input *input, bool no_world_model,
                               qa_app_q3_scene_operation_sink *out);
} qa_app_q3_scene_renderer_host;
/* TS ApplicationQ3SceneRenderer. */
typedef struct qa_app_application_q3_scene_renderer qa_app_application_q3_scene_renderer;
qa_app_application_q3_scene_renderer *qa_app_application_q3_scene_renderer_create(
    qa_app_application_q3_assets *media, qa_q3_pres_q3_renderer_resources *resources,
    qa_app_q3_scene_renderer_host host);
void qa_app_application_q3_scene_renderer_destroy(qa_app_application_q3_scene_renderer *renderer);
/* TS resources access used by sibling slices. */
qa_q3_pres_q3_renderer_resources *qa_app_application_q3_scene_renderer_resources(qa_app_application_q3_scene_renderer *renderer);
void qa_app_application_q3_scene_renderer_preload(qa_app_application_q3_scene_renderer *renderer,
    const qa_q3_pres_scene_content *const *contents, size_t content_count);
/* TS operations(): malloc'd operations (caller destroys groups +
 * frees the array). additions may be NULL/0. */
qa_scene_operation *qa_app_application_q3_scene_renderer_operations(
    qa_app_application_q3_scene_renderer *renderer, const qa_q3_pres_scene_content *scene,
    const qa_world_view_input *input, double first_entity, qa_app_q3_scene_render_options options,
    const qa_scene_operation *additions, size_t addition_count, size_t *out_count);
void qa_app_application_q3_scene_renderer_close(qa_app_application_q3_scene_renderer *renderer);
#endif
