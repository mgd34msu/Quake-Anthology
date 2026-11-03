/* Port of src/app/bootstrap/q3-client/scene.ts (ApplicationQ3SceneRenderer):
 * captured source geometry rendered inside the destination's admitted view,
 * over the ApplicationAssets behind the ApplicationQ3Assets (its world scene,
 * material registrations and providers) and one SceneModelRenderer per
 * provider. Preload is synchronous. TS errors are qa_raise("Error").
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_SCENE_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_SCENE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/effects/q3.h"
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/content/q3/presentation/scene.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/render/scene/particles/primitives.h"
#include "qa/render/scene/submissions.h"
#include "qa/render/scene/world.h"
/* TS Q3SceneRenderOptions. view_offset NULL = TS undefined; rail NULL = TS
 * undefined (DEFAULT_RAIL_SETTINGS). Both are borrowed for the call. */
typedef struct qa_app_q3_scene_render_options {
  const qa_vec3 *view_offset;
  bool no_world_model;
  const qa_rail_settings *rail;
  bool split_screen;
  bool supplemental_view_weapon;
} qa_app_q3_scene_render_options;
/* TS operations() result. items holds the caller's additions by value
 * (still caller-owned) and the renderer's own operations, whose groups,
 * batches, geometry and material contexts live in storage. Release with
 * qa_app_q3_scene_operations_release. */
typedef struct qa_app_q3_scene_operations {
  qa_scene_operation *items; size_t count, capacity;
  qa_app_effect_storage storage;
} qa_app_q3_scene_operations;
void qa_app_q3_scene_operations_release(qa_app_q3_scene_operations *operations);
/* TS ApplicationQ3SceneRenderer(media, { picture }). media must be built
 * over an ApplicationAssets (qa_app_application_q3_assets_application);
 * resources is the services' Q3RendererResources (TS picture). Both are
 * borrowed and must outlive the renderer. */
typedef struct qa_app_application_q3_scene_renderer qa_app_application_q3_scene_renderer;
qa_app_application_q3_scene_renderer *qa_app_application_q3_scene_renderer_create(
    qa_app_application_q3_assets *media, qa_q3_pres_q3_renderer_resources *resources);
/* Closes, then frees. */
void qa_app_application_q3_scene_renderer_destroy(qa_app_application_q3_scene_renderer *renderer);
qa_q3_pres_q3_renderer_resources *qa_app_application_q3_scene_renderer_resources(qa_app_application_q3_scene_renderer *renderer);
void qa_app_application_q3_scene_renderer_preload(qa_app_application_q3_scene_renderer *renderer,
    const qa_q3_pres_scene_content *const *contents, size_t content_count);
/* TS operations(scene, input, firstEntity, options, additions = []).
 * additions may be NULL/0; *out is initialized here. */
void qa_app_application_q3_scene_renderer_operations(
    qa_app_application_q3_scene_renderer *renderer, const qa_q3_pres_scene_content *scene,
    const qa_world_view_input *input, double first_entity, qa_app_q3_scene_render_options options,
    const qa_scene_operation *additions, size_t addition_count, qa_app_q3_scene_operations *out);
void qa_app_application_q3_scene_renderer_close(qa_app_application_q3_scene_renderer *renderer);
#endif
