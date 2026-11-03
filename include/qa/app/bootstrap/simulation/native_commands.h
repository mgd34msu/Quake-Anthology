/* Port of src/app/bootstrap/simulation/native-commands.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_COMMANDS_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_COMMANDS_H
#include "qa/contracts/math.h"
#include <stddef.h>
#include "qa/contracts/protocol.h"
#include "qa/contracts/time.h"
typedef struct qa_app_sim_native_player_view { const char *profile_kind; const char *arsenal_kind; double source_weapon; } qa_app_sim_native_player_view;
qa_user_command qa_app_sim_selected_native_command(qa_user_command command, qa_vec3 aim,
    qa_app_sim_native_player_view player, double server_time_milliseconds);
/* TS nativeMoveSlices(clock, milliseconds, carry = 0): the guest interval
 * quoted in the selected provider's quanta. slices is malloc'd (free()). */
typedef struct qa_app_sim_native_move_slices { double *slices; size_t count; double carry; } qa_app_sim_native_move_slices;
qa_app_sim_native_move_slices qa_app_sim_native_move_slices_quote(qa_clock_profile clock, double milliseconds, double carry);
#endif
