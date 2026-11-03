/* Port of src/app/bootstrap/q3-selected-weapon.ts (SelectedQ3WeaponPresenter):
 * CG_AddViewWeapon uses the hand model as a tag anchor, then draws gun,
 * barrel and flash; the world form draws the gun on a character torso.
 *
 * C accommodations:
 *  - Async loads are synchronous over the real ApplicationAssets. The TS
 *    promise caches keep settled values only (cacheEvictingRejection): a
 *    failure raises through qa_raise and leaves no entry.
 *  - Results are presenter scratch: the world passes stay valid until the
 *    next world() call, the frame entity until the next frame() call, and
 *    both until destroy. Entities point at the source's actor; resources and
 *    models belong to the assets.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_SELECTED_WEAPON_H
#define QA_APP_BOOTSTRAP_Q3_SELECTED_WEAPON_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q3/foundation/animation_config.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

typedef struct qa_app_selected_q3_weapon_presenter qa_app_selected_q3_weapon_presenter;
/* TS new SelectedQ3WeaponPresenter(assets, characterAnimation). Both are
 * borrowed; character_animation NULL is TS null. NULL only on OOM. */
qa_app_selected_q3_weapon_presenter *qa_app_selected_q3_weapon_presenter_create(
    qa_app_application_assets *assets, const qa_q3_player_animation_config *character_animation);
void qa_app_selected_q3_weapon_presenter_destroy(qa_app_selected_q3_weapon_presenter *presenter);
/* TS world(source, character: Pick<Q3CharacterView, "origin" | "powerups">,
 * personalModel). Writes the pass count; the passes are presenter scratch. */
const qa_q3_character_pass *qa_app_selected_q3_weapon_presenter_world(qa_app_selected_q3_weapon_presenter *presenter,
    const qa_app_simulation_presentation *source, qa_vec3 character_origin, double character_powerups,
    bool personal_model, size_t *out_count);
/* TS frame(source, fieldOfView = 90). False is the TS null (the hands model
 * has no anchor tag; warned once). Raises "Selected Q3 weapon presentation
 * state is missing" without q3Weapon. *out is presenter scratch. */
bool qa_app_selected_q3_weapon_presenter_frame(qa_app_selected_q3_weapon_presenter *presenter,
    const qa_app_simulation_presentation *source, double field_of_view, qa_scene_entity *out);
#endif
