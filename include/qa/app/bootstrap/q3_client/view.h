/* Port of src/app/bootstrap/q3-client/view.ts. Offsets deep-copy
 * the entity (attachments recursively); free results with
 * qa_app_offset_q3_view_entity_free. A NULL delta returns a copy.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_VIEW_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_VIEW_H
#include <stdbool.h>
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
/* TS q3WeaponCamera. */
qa_scene_camera qa_app_q3_weapon_camera(qa_scene_camera camera, bool split_screen);
/* TS offsetQ3ViewEntity (delta NULL = TS undefined). Owned copy. */
qa_scene_entity *qa_app_offset_q3_view_entity(const qa_scene_entity *entity, const qa_vec3 *delta);
void qa_app_offset_q3_view_entity_free(qa_scene_entity *entity);
/* TS offsetQ3ViewReference (delta NULL = TS undefined). */
qa_q3_pres_admitted_ref_entity qa_app_offset_q3_view_reference(
    qa_q3_pres_admitted_ref_entity entity, const qa_vec3 *delta);
#endif
