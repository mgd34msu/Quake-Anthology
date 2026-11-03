/* Port of src/app/bootstrap/simulation/bot-mover-connections.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_mover_connections.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

qa_vec3 qa_app_sim_train_riding_point(qa_vec3 origin, qa_bounds local, double profile_min_z) {
  qa_vec3 out = {origin.x + (local.min.x + local.max.x) / 2, origin.y + (local.min.y + local.max.y) / 2,
      origin.z + local.max.z - profile_min_z + 0.125};
  return out;
}
double qa_app_sim_train_travel_seconds(double wait, qa_vec3 from, qa_vec3 to, double speed) {
  return wait + qa_js_hypot3(to.x - from.x, to.y - from.y, to.z - from.z) / speed;
}
static qa_vec3 at_stop(qa_vec3 point, qa_vec3 stop) { return (qa_vec3){point.x + stop.x, point.y + stop.y, point.z + stop.z}; }

qa_app_sim_train_connections qa_app_sim_application_train_connections(const qa_app_sim_bot_simulation *simulation,
    const qa_navigation_profile *profile) {
  qa_app_sim_train_connections out = {NULL, 0, NULL};
  qa_q2_product_runtime *q2 = simulation->q2_source(simulation->ctx);
  if (q2 == NULL) return out;
  size_t mover_count = 0;
  const qa_spatial_actor *queried = qa_shared_query_actors(simulation->scene, qa_shared_model_bounds(simulation->scene, 0), NULL, &mover_count);
  qa_spatial_actor *movers = calloc(mover_count != 0 ? mover_count : 1, sizeof *movers);
  if (movers == NULL) qa_range_error("Out of memory");
  memcpy(movers, queried, mover_count * sizeof *movers);
  /* Array.prototype.sort by actor slot (stable). */
  for (size_t i = 1; i < mover_count; i++) {
    qa_spatial_actor value = movers[i];
    size_t j = i;
    while (j > 0 && movers[j - 1].body.actor.slot > value.body.actor.slot) { movers[j] = movers[j - 1]; j--; }
    movers[j] = value;
  }
  size_t capacity = 0;
  qa_q2_game_services *game = q2->services;
  for (size_t m = 0; m < mover_count; m++) {
    const qa_linked_body *body = &movers[m].body;
    const qa_actor_collision *collision = &movers[m].collision;
    if (!(collision->shape_kind != NULL && strcmp(collision->shape_kind, "model") == 0)) continue;
    qa_q2_entity *entity = game->entity(game->ctx, &body->actor);
    if (entity == NULL) continue;
    qa_q2_train_route route;
    if (!qa_q2_mover_module_train_route(q2->movers, entity, game, &route)) continue;
    qa_bounds local = body->state.bounds;
    for (size_t s = 0; s < route.stop_count; s++) {
      const qa_q2_train_stop *stop = &route.stops[s];
      const qa_q2_train_stop *next = NULL;
      if (stop->has_next)
        for (size_t c = 0; c < route.stop_count && next == NULL; c++) if (qa_same_actor(route.stops[c].actor, stop->next)) next = &route.stops[c];
      if (stop->wait < 0 || stop->teleport || next == NULL || next->teleport) continue;
      qa_q2_entity *destination = game->entity(game->ctx, &next->actor);
      double speed = game->options.edition == QA_Q2_EDITION_RERELEASE && destination != NULL && destination->speed != 0 ? destination->speed : entity->speed;
      if (speed <= 0) continue;
      double travel_seconds = stop->wait + qa_js_hypot3(next->origin.x - stop->origin.x, next->origin.y - stop->origin.y, next->origin.z - stop->origin.z) / speed;
      if (out.count == capacity) {
        capacity = capacity != 0 ? capacity * 2 : 8;
        qa_navigation_connection *grown = realloc(out.connections, capacity * sizeof *grown);
        double (*raw)[3] = realloc(out.raw, capacity * sizeof *raw);
        if (grown == NULL || raw == NULL) qa_range_error("Out of memory");
        out.connections = grown; out.raw = raw;
      }
      qa_navigation_connection *c = &out.connections[out.count];
      memset(c, 0, sizeof *c);
      c->id = stop->actor.slot; c->source_travel_type = 7; c->travel_seconds = travel_seconds;
      c->request.from = qa_app_sim_train_riding_point(stop->origin, local, profile->shape.bounds.min.z);
      c->request.to = qa_app_sim_train_riding_point(next->origin, local, profile->shape.bounds.min.z);
      c->request.mode = QA_TM_MOVER; c->request.has_hint = false;
      c->request.has_entity = true;
      c->request.entity.has_model = true; c->request.entity.model = (int64_t)collision->shape_model;
      c->request.entity.bounds.min = at_stop(local.min, stop->origin);
      c->request.entity.bounds.max = at_stop(local.max, stop->origin);
      out.raw[out.count][0] = (double)entity->actor.id.slot;
      out.raw[out.count][1] = (double)stop->actor.slot;
      out.raw[out.count][2] = (double)next->actor.slot;
      out.count++;
    }
  }
  for (size_t i = 0; i < out.count; i++) { out.connections[i].request.entity.raw = out.raw[i]; out.connections[i].request.entity.raw_count = 3; }
  free(movers);
  return out;
}
void qa_app_sim_train_connections_free(qa_app_sim_train_connections *connections) {
  free(connections->connections); free(connections->raw);
  memset(connections, 0, sizeof *connections);
}
