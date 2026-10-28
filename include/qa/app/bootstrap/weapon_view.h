/* Port of src/app/bootstrap/weapon-view.ts. SimulationPresentation
 * (app/bootstrap/simulation/types.ts, sibling slice) mirrored by the
 * minimal fields weaponViewOrigin reads. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_WEAPON_VIEW_H
#define QA_APP_BOOTSTRAP_WEAPON_VIEW_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
typedef struct qa_app_view_weapon_entity {
  qa_model_transform transform; qa_vec3 previous_origin, lighting_origin; double shadow_plane;
} qa_app_view_weapon_entity;
/* Mirror: TS SimulationPresentation fields read here. family is "q1"|... */
typedef struct qa_app_simulation_presentation_view {
  bool has_view_weapon; const char *family; qa_vec3 origin;
} qa_app_simulation_presentation_view;
qa_app_view_weapon_entity qa_app_anchored_view_weapon(qa_app_view_weapon_entity entity, qa_vec3 anchor, qa_vec3 camera);
qa_axis qa_app_anchored_view_weapon_axis(qa_axis axis, qa_axis anchor, qa_axis camera);
qa_vec3 qa_app_weapon_view_origin(qa_app_simulation_presentation_view source);
qa_scene_camera qa_app_weapon_view_camera(qa_scene_camera camera, const qa_rect *occupied, size_t count);
#endif
