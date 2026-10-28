/* Port of src/app/bootstrap/simulation/player-input-application.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PLAYER_INPUT_APPLICATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_PLAYER_INPUT_APPLICATION_H
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/time.h"
qa_vec3 qa_app_sim_movement_application_aim(qa_user_command command, qa_movement_state state, double health, qa_numeric_operations numeric);
qa_frame_context qa_app_sim_movement_application_frame(qa_user_command command, qa_movement_state state, qa_movement_profile profile, qa_frame_context frame);
#endif
