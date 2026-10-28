/* Port of src/app/bootstrap/simulation/native-commands.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_COMMANDS_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_COMMANDS_H
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
typedef struct qa_app_sim_native_player_view { const char *profile_kind; const char *arsenal_kind; double source_weapon; } qa_app_sim_native_player_view;
qa_user_command qa_app_sim_selected_native_command(qa_user_command command, qa_vec3 aim,
    qa_app_sim_native_player_view player, double server_time_milliseconds);
#endif
