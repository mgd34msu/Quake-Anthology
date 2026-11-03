/* Port of src/app/bootstrap/simulation/native-q1-pusher.ts. GPL-2.0-or-later.
 * TS createNativeQ1PusherServices(game, physics) returns a fresh services
 * object per call; the C services' ctx is this owner, which keeps the
 * physics candidates scratch between calls (free with _destroy). */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q1_PUSHER_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q1_PUSHER_H
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/movement/q1/types.h"
typedef struct qa_app_sim_native_q1_pusher {
  qa_app_sim_q1_pusher_env env; /* first: the services ctx is this record */
  qa_q1_services *game;
  qa_app_sim_shared_physics *physics;
} qa_app_sim_native_q1_pusher;
/* NULL only on OOM. */
qa_app_sim_native_q1_pusher *qa_app_sim_native_q1_pusher_create(qa_q1_services *game, qa_app_sim_shared_physics *physics);
void qa_app_sim_native_q1_pusher_destroy(qa_app_sim_native_q1_pusher *pusher);
/* TS createNativeQ1PusherServices(game, physics): valid while the owner lives. */
qa_q1_pusher_services qa_app_sim_native_q1_pusher_services(qa_app_sim_native_q1_pusher *pusher);
#endif
