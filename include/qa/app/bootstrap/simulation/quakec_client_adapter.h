/* Port of src/app/bootstrap/simulation/quakec-client-adapter.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_CLIENT_ADAPTER_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_CLIENT_ADAPTER_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"
qa_q1_user_command qa_app_sim_quakec_client_command(qa_user_command command);
typedef struct qa_app_sim_quakec_client_transition_state { double flags; qa_vec3 velocity; } qa_app_sim_quakec_client_transition_state;
bool qa_app_sim_quakec_source_jump(qa_user_command command, qa_app_sim_quakec_client_transition_state before, qa_app_sim_quakec_client_transition_state after);
typedef struct qa_app_sim_quakec_client_movement { qa_user_command command; bool source_jump; } qa_app_sim_quakec_client_movement;
qa_user_command qa_app_sim_consume_quakec_jump(qa_user_command command);
qa_movement_state qa_app_sim_quakec_free_movement(qa_movement_state state);
#endif
