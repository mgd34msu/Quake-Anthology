/* Port of src/app/bootstrap/simulation/rerelease-guest-player.ts.
 * q2WeaponStatus (simulation/arsenal sibling slice) is supplied by the
 * caller until that slice lands. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_PLAYER_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_PLAYER_H
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
qa_app_sim_player_view qa_app_sim_rerelease_guest_player_view(qa_q2_rerelease_player_state state);
typedef struct qa_app_sim_rerelease_guest_configstring { double index; const char *value; } qa_app_sim_rerelease_guest_configstring;
/* definitions NULL uses the landed Q2 base weapons. weapon_status may
 * be NULL, leaving ui.weapon_status NULL. */
qa_app_sim_player_ui qa_app_sim_rerelease_guest_player_ui(qa_q2_rerelease_player_state state,
    const qa_app_sim_rerelease_guest_configstring *configstrings, size_t configstring_count, qa_provider_reference source,
    const qa_q2w_definition *definitions, size_t definition_count,
    const qa_weapon_hud_status *(*weapon_status)(void *ctx, const qa_q2w_definition *definition, double ammo, qa_provider_reference source), void *weapon_status_ctx);
qa_q2_rerelease_user_command qa_app_sim_rerelease_guest_local_command(qa_actor_command input, qa_q2_rerelease_player_state state);
#endif
