/* Port of src/app/bootstrap/simulation/classic-guest-player.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_PLAYER_H
#define QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_PLAYER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/ui.h"
typedef struct qa_app_sim_configstring { double index; const char *value; } qa_app_sim_configstring;
qa_app_sim_player_view qa_app_sim_classic_guest_player_view(qa_q2_player_state state);
/* weapon_status storage is caller-owned scratch; result points into it (NULL weapon -> NULL). */
qa_app_sim_player_ui qa_app_sim_classic_guest_player_ui(qa_q2_player_state state,
    const qa_app_sim_configstring *configstrings, size_t configstring_count,
    qa_provider_reference source, const qa_q2w_definition *definitions, size_t definition_count,
    qa_weapon_hud_status *weapon_status_scratch, char *label_scratch, size_t label_scratch_size);
/* source_kind: TS ActorCommand source kind; command must be q2-classic. */
qa_q2_user_command qa_app_sim_classic_guest_local_command(const char *source_kind,
    qa_user_command command, qa_q2_player_state state);
#endif
