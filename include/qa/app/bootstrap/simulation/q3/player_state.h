/* Port of src/app/bootstrap/simulation/q3/player-state.ts over landed
 * game/state mirrors. jump_pad actor storage is caller-provided.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_PLAYER_STATE_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_PLAYER_STATE_H
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/contracts/movement.h"
qa_q3_movement_state qa_app_read_q3_movement_state(qa_q3_game_entity *entity, qa_q3_base_entity_records *records,
    qa_actor_id *jump_pad_storage);
void qa_app_write_q3_movement_state(qa_q3_game_entity *entity, qa_q3_movement_state state, qa_q3_base_entity_records *records);
void qa_app_write_q3_character_animation(qa_q3_game_entity *entity, qa_actor_animation_state animation);
qa_movement_environment qa_app_read_q3_movement_environment(qa_q3_game_entity *entity, qa_movement_environment base);
qa_q3_arsenal_runtime_state qa_app_read_q3_arsenal_runtime(qa_q3_game_entity *entity, qa_q3_arsenal_runtime_state previous);
void qa_app_write_q3_arsenal_runtime(qa_q3_game_entity *entity, qa_q3_arsenal_runtime_state runtime);
#endif
