/* Port of src/app/bootstrap/simulation/bot-mover-connections.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_MOVER_CONNECTIONS_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_MOVER_CONNECTIONS_H
#include "qa/contracts/math.h"
/* TS riding(origin): center x/y of local bounds, top z minus profile min z + 0.125. */
qa_vec3 qa_app_sim_train_riding_point(qa_vec3 origin, qa_bounds local, double profile_min_z);
double qa_app_sim_train_travel_seconds(double wait, qa_vec3 from, qa_vec3 to, double speed);
#endif
