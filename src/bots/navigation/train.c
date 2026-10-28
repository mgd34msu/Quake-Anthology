/* Port of src/bots/navigation/train.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/train.h"
#include <math.h>
#include <stdlib.h>

static bool supports(const qa_navigation_entity_state *state, const qa_navigation_train_stop *stop,
                     qa_vec3 point, const qa_navigation_profile *profile) {
  double x = stop->origin.x - state->train_origin.x, y = stop->origin.y - state->train_origin.y,
         z = stop->origin.z - state->train_origin.z;
  qa_bounds bounds = profile->shape.bounds;
  return point.x + bounds.max.x > state->bounds.min.x + x && point.x + bounds.min.x < state->bounds.max.x + x
      && point.y + bounds.max.y > state->bounds.min.y + y && point.y + bounds.min.y < state->bounds.max.y + y
      && fabs(point.z + bounds.min.z - (state->bounds.max.z + z)) <= profile->maximum_step;
}
static const qa_navigation_train_stop *find_stop(const qa_navigation_entity_state *state, qa_vec3 point,
                                                 const qa_navigation_profile *profile) {
  for (size_t i = 0; i < state->train_stop_count; i++) {
    const qa_navigation_train_stop *s = &state->train_stops[i];
    if (!s->teleport && supports(state, s, point, profile)) return s;
  }
  return NULL;
}
bool qa_navigation_find_train_ride(const qa_navigation_entity_state *state, const qa_navigation_edge *edge,
                              const qa_navigation_profile *profile, qa_navigation_train_ride *out) {
  if (!state->has_train || !state->enabled || state->locked || !(profile->capabilities & (1u << QA_TM_MOVER))) return false;
  const qa_navigation_train_stop *boarding = find_stop(state, edge->start, profile);
  const qa_navigation_train_stop *arrival = find_stop(state, edge->end, profile);
  if (!boarding || !arrival || boarding->id == arrival->id) return false;
  const qa_navigation_train_stop *current = boarding;
  /* TS visited Set<number>. */
  int64_t *visited = malloc((state->train_stop_count ? state->train_stop_count : 1) * sizeof *visited);
  if (!visited) return false;
  size_t visited_count = 0;
  while (current) {
    bool seen = false;
    for (size_t i = 0; i < visited_count; i++) if (visited[i] == current->id) seen = true;
    if (seen) break;
    if (current->id == arrival->id) { out->boarding = *boarding; out->arrival = *arrival; free(visited); return true; }
    if (current->wait < 0 || current->teleport) { free(visited); return false; }
    visited[visited_count++] = current->id;
    const qa_navigation_train_stop *next = NULL;
    if (current->has_next)
      for (size_t i = 0; i < state->train_stop_count; i++)
        if (state->train_stops[i].id == current->next) { next = &state->train_stops[i]; break; }
    current = next;
  }
  free(visited);
  return false;
}
