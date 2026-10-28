/* Port of src/app/bootstrap/simulation/bot-mover-connections.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_mover_connections.h"
#include <math.h>
qa_vec3 qa_app_sim_train_riding_point(qa_vec3 origin, qa_bounds local, double profile_min_z) {
  qa_vec3 out = {origin.x + (local.min.x + local.max.x) / 2, origin.y + (local.min.y + local.max.y) / 2,
      origin.z + local.max.z - profile_min_z + 0.125};
  return out;
}
double qa_app_sim_train_travel_seconds(double wait, qa_vec3 from, qa_vec3 to, double speed) {
  double dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
  return wait + sqrt(dx * dx + dy * dy + dz * dz) / speed;
}
