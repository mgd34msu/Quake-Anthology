/* Port of src/app/bootstrap/simulation/shared-quakeworld-movement.ts.
 * The provider ctx is heap storage freed by
 * qa_app_sim_shared_quakeworld_movement_destroy. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_SHARED_QUAKEWORLD_MOVEMENT_H
#define QA_APP_BOOTSTRAP_SIMULATION_SHARED_QUAKEWORLD_MOVEMENT_H
#include "qa/contracts/movement.h"
#include "qa/movement/q1/types.h"
#include "qa/movement/q3/types.h"
typedef struct qa_app_sim_qw_postures { double standing_view_height; qa_movement_posture crouched; } qa_app_sim_qw_postures;
/* publish_posture may be NULL. Returned provider is "q1-quakeworld". */
qa_movement_provider qa_app_sim_create_shared_quakeworld_movement(qa_provider_id id, qa_q1_movement_options options,
    qa_bounds standing_bounds, qa_app_sim_qw_postures postures,
    void (*publish_posture)(void *ctx, qa_bounds bounds, double view_height), void *publish_ctx);
void qa_app_sim_shared_quakeworld_movement_destroy(qa_movement_provider provider);
#endif
