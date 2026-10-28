/* Port of src/app/bootstrap/simulation/player-jump.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PLAYER_JUMP_H
#define QA_APP_BOOTSTRAP_SIMULATION_PLAYER_JUMP_H
#include <stdbool.h>
#include "qa/contracts/content.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/scene.h"
bool qa_app_sim_movement_jumped(qa_movement_state before, qa_trace_hit ground, qa_user_command command, qa_movement_result result);
bool qa_app_sim_publish_q3_character_movement_event(qa_game_family character, double event);
#endif
