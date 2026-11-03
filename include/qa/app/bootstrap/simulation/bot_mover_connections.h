/* Port of src/app/bootstrap/simulation/bot-mover-connections.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_MOVER_CONNECTIONS_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_MOVER_CONNECTIONS_H
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/bots/navigation/construct.h"
#include "qa/contracts/math.h"
/* TS riding(origin): center x/y of local bounds, top z minus profile min z + 0.125. */
qa_vec3 qa_app_sim_train_riding_point(qa_vec3 origin, qa_bounds local, double profile_min_z);
double qa_app_sim_train_travel_seconds(double wait, qa_vec3 from, qa_vec3 to, double speed);
/* TS applicationTrainConnections result; entity.raw points into raw (owned). */
typedef struct qa_app_sim_train_connections {
  qa_navigation_connection *connections; size_t count;
  double (*raw)[3];
} qa_app_sim_train_connections;
/* Construction borrows actual source corner routes and shared brush bounds. */
qa_app_sim_train_connections qa_app_sim_application_train_connections(const qa_app_sim_bot_simulation *simulation,
    const qa_navigation_profile *profile);
void qa_app_sim_train_connections_free(qa_app_sim_train_connections *connections);
#endif
