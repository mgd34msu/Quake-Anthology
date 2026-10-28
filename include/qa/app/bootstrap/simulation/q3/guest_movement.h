/* Port of src/app/bootstrap/simulation/q3/guest-movement.ts.
 * TS getters become qa_app_q3_guest_movement_projection_refresh(),
 * which re-reads public VM records through the host and refills the
 * landed qa_app_sim_movement_prediction_player snapshot. Arsenal ammo
 * storage owned by the projection. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_MOVEMENT_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_MOVEMENT_H
#include "qa/app/bootstrap/network/q3_types.h"
#include "qa/app/bootstrap/simulation/player_movement.h"
#include "qa/app/bootstrap/simulation/q3/types.h"

typedef struct qa_app_q3_guest_movement_host {
  void *ctx;
  qa_q3_player_state (*player_state)(void *ctx, double source_entity);
  qa_owned_actor (*actor_at)(void *ctx, double source_entity);
  bool (*reference_at)(void *ctx, double source_entity, qa_actor_id *out);
  void (*entity_bounds)(void *ctx, double source_entity, qa_vec3 *out_mins, qa_vec3 *out_maxs);
  double (*cvar_value)(void *ctx, const char *name);
  bool (*player_arsenal)(void *ctx, qa_actor_id actor, qa_arsenal_state *out); /* false = TS null */
} qa_app_q3_guest_movement_host;

typedef struct qa_app_q3_guest_movement_projection qa_app_q3_guest_movement_projection;
qa_app_q3_guest_movement_projection *qa_app_guest_movement_projection_create(qa_app_q3_guest_movement_host host,
    qa_app_q3_player player, qa_executable_recipe recipe);
void qa_app_q3_guest_movement_projection_destroy(qa_app_q3_guest_movement_projection *projection);
qa_app_sim_movement_prediction_player qa_app_q3_guest_movement_projection_refresh(qa_app_q3_guest_movement_projection *projection);
#endif
