/* Port of src/app/bootstrap/simulation/q3-commands.ts. The
 * MovementPlayer fields read here are carried in the view below
 * (players.ts owner slice provides the full class). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_COMMANDS_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_COMMANDS_H
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
#include "qa/network/q3/message.h"
typedef struct qa_app_sim_q3_command_player {
  qa_arsenal_state arsenal;
  const char *entities_content; /* recipe.map.entities.content */
  qa_movement_profile profile;
} qa_app_sim_q3_command_player;
/* dialect is a SelectedCommand kind string; angle_space may be NULL. */
qa_q3_base_user_command qa_app_sim_relative_q3_source_command(qa_command_source source, const char *dialect,
    qa_q3_base_user_command command, qa_vec3 delta, const char *angle_space);
qa_q3_base_user_command qa_app_sim_q3_source_command(qa_actor_command input, qa_app_sim_q3_command_player player, double milliseconds, double native_weapon);
qa_q3_base_user_command qa_app_sim_q3_command_for_controls(qa_user_command command, double milliseconds, double requested_weapon, bool use_holdable);
qa_q3_wire_user_command qa_app_sim_wire_q3_user_command(qa_user_command command, double milliseconds, double weapon);
qa_user_command qa_app_sim_selected_q3_command(qa_q3_base_user_command command, qa_movement_profile profile, double milliseconds);
typedef struct qa_app_sim_relative_movement_input { qa_user_command command; const char *angle_space; } qa_app_sim_relative_movement_input;
qa_app_sim_relative_movement_input qa_app_sim_relative_movement_command(qa_app_sim_relative_movement_input input, qa_movement_state state);
#endif
