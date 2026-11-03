/* Port of src/app/bootstrap/presentation-scene.ts: seatModelVisible and
 * ApplicationWorldScene, the seat's model, character, held-weapon, flare,
 * shader-beam and brush presentation over the real ApplicationAssets,
 * SceneModelRenderer groups and the loaded WorldScene.
 *
 * C accommodations:
 *  - prepare is synchronous; errors raise through qa_raise as the TS
 *    promise rejects.
 *  - Values a prepare derives (entities, option closures, resolved held
 *    weapons) live in the scene until the next prepare or close. Operation
 *    lists returned by view/supplemental/effect_operations, and the batches
 *    behind them, live in the scene until the next prepare or close: render
 *    a frame before preparing the next one, as the TS seat does.
 *  - TS SceneOperation is the submissions qa_scene_operation; the
 *    renderer's model groups (qa_scene_model_group) are converted to scene
 *    groups with the same order and batches.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PRESENTATION_SCENE_H
#define QA_APP_BOOTSTRAP_PRESENTATION_SCENE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/component_bodies.h"
#include "qa/app/bootstrap/effects/q3.h"
#include "qa/app/bootstrap/q3_client/qvm.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q3/foundation/assets.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/materials/lighting.h"
#include "qa/render/scene/submissions.h"
#include "qa/render/scene/world.h"

/* TS seatModelVisible(viewer, source). viewer NULL is TS null. */
bool qa_app_seat_model_visible(const qa_actor_id *viewer, qa_actor_id actor, bool view_weapon);

typedef struct qa_app_application_world_scene qa_app_application_world_scene;
/* TS new ApplicationWorldScene(assets, characterAssets, planarShadows = () => false).
 * assets and character_assets are borrowed (character_assets NULL is TS null);
 * planar_shadows NULL is the TS default. NULL only on allocation failure. */
qa_app_application_world_scene *qa_app_application_world_scene_create(qa_app_application_assets *assets,
    const qa_q3_character_assets *character_assets, bool (*planar_shadows)(void *ctx), void *planar_shadows_ctx);
/* Closes, then frees. */
void qa_app_application_world_scene_destroy(qa_app_application_world_scene *scene);
qa_app_application_assets *qa_app_application_world_scene_assets(const qa_app_application_world_scene *scene);

void qa_app_application_world_scene_receive(qa_app_application_world_scene *scene,
    const qa_app_sim_presentation_event *events, size_t count);
double qa_app_application_world_scene_style(const qa_app_application_world_scene *scene, double index, double absent);

/* TS prepare's optional arguments (fieldOfView = 90, heldWeapons = [],
 * viewWeaponVisible = true, bodies = [], primaryBodies = []). */
typedef struct qa_app_world_scene_prepare_options {
  double field_of_view;
  const qa_app_qvm_held_weapon *held_weapons; size_t held_weapon_count;
  bool view_weapon_visible;
  const qa_app_component_body *bodies; size_t body_count;
  const qa_app_prepared_primary_body *primary_bodies; size_t primary_body_count;
} qa_app_world_scene_prepare_options;
qa_app_world_scene_prepare_options qa_app_world_scene_prepare_defaults(void);
/* TS prepare(viewer, snapshot, presentations, characters, ...). viewer NULL is
 * TS null; options NULL takes every default. */
void qa_app_application_world_scene_prepare(qa_app_application_world_scene *scene, const qa_actor_id *viewer,
    const qa_world_snapshot *snapshot, const qa_app_simulation_presentation *presentations, size_t presentation_count,
    const qa_q3_character_view *characters, size_t character_count, const qa_app_world_scene_prepare_options *options);

/* TS styles(): Pick<WorldViewInput, "q1Styles" | "q2Styles">, 256 rows each,
 * owned by the scene and valid until the next styles() call that rebuilds. */
typedef struct qa_app_world_scene_styles {
  const double *q1_styles; size_t q1_style_count;
  const qa_q2_light_style *q2_styles; size_t q2_style_count;
} qa_app_world_scene_styles;
qa_app_world_scene_styles qa_app_application_world_scene_styles(qa_app_application_world_scene *scene);

/* TS view(input, operations, shadowLights, infrared, weaponCamera = input.camera).
 * weapon_camera NULL is the default. The caller destroys *out
 * (qa_prepared_world_view_destroy). */
void qa_app_application_world_scene_view(qa_app_application_world_scene *scene, const qa_world_view_input *input,
    const qa_scene_operation *operations, size_t operation_count, const qa_scene_light *shadow_lights,
    size_t shadow_light_count, bool infrared, const qa_scene_camera *weapon_camera, qa_prepared_world_view *out);
/* TS supplemental(input, weaponCamera = input.camera, infrared = false). */
const qa_scene_operation *qa_app_application_world_scene_supplemental(qa_app_application_world_scene *scene,
    const qa_world_view_input *input, const qa_scene_camera *weapon_camera, bool infrared, size_t *out_count);
/* The TS SceneOperation[] of an ApplicationEffectFrame in submissions form. */
const qa_scene_operation *qa_app_application_world_scene_effect_operations(qa_app_application_world_scene *scene,
    const qa_app_effect_operations *operations, size_t *out_count);
/* TS close(). */
void qa_app_application_world_scene_close(qa_app_application_world_scene *scene);
#endif
