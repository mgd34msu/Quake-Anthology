/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/goals.h"
#include "qa/core/math.h"
#include <math.h>
#include <string.h>
bool qa_is_source_goal_number(double n) {
  return trunc(n) == n && n >= QA_SOURCE_GOAL_NUMBER_MIN && n <= 2147483647.0;
}
bool qa_touching_goal(qa_vec3 o, qa_bot_goal g) {
  qa_vec3 pmin = {-15, -15, -24}, pmax = {15, 15, 32};
  qa_vec3 mn = qa_add3_double(qa_sub3_double(g.mins, pmax), g.origin);
  qa_vec3 mx = qa_add3_double(qa_sub3_double(g.maxs, pmin), g.origin);
  /* TS uses add3/sub3 (binary32-staged); recompute staged to match. */
  mn = qa_add3(qa_sub3(g.mins, pmax), g.origin);
  mx = qa_add3(qa_sub3(g.maxs, pmin), g.origin);
  if (o.x < mn.x || o.x > mx.x)
    return false;
  if (o.y < mn.y || o.y > mx.y)
    return false;
  if (o.z < mn.z || o.z > mx.z)
    return false;
  return true;
}
void qa_bot_goal_library_init(qa_bot_goal_library *l) { memset(l, 0, sizeof *l); }
void qa_bot_goal_library_destroy(qa_bot_goal_library *l) { (void)l; }
int64_t qa_bot_goal_allocate(qa_bot_goal_library *l) {
  for (int i = 1; i <= QA_MAX_GOAL_STATES; i++)
    if (!l->allocated[i]) {
      l->allocated[i] = true;
      memset(&l->states[i], 0, sizeof l->states[i]);
      return i;
    }
  return 0;
}
void qa_bot_goal_free(qa_bot_goal_library *l, int64_t h) {
  if (h >= 1 && h <= QA_MAX_GOAL_STATES)
    l->allocated[h] = false;
}
void qa_bot_goal_reset(qa_bot_goal_library *l, int64_t h) {
  if (h >= 1 && h <= QA_MAX_GOAL_STATES) {
    memset(&l->states[h], 0, sizeof l->states[h]);
    memset(l->avoid_numbers[h], 0, sizeof l->avoid_numbers[h]);
    memset(l->avoid_times[h], 0, sizeof l->avoid_times[h]);
  }
}
void qa_bot_goal_set_avoid(qa_bot_goal_library *l, int64_t h, int64_t num, double dur) {
  if (h < 1 || h > QA_MAX_GOAL_STATES)
    return;
  float now = l->time_fn ? (float)l->time_fn(l->time_ctx) : 0.0f;
  for (int i = 0; i < QA_MAX_AVOID_GOALS; i++)
    if (l->avoid_numbers[h][i] == num) {
      l->avoid_times[h][i] = (float)((double)now + (double)(float)dur);
      return;
    }
  for (int i = 0; i < QA_MAX_AVOID_GOALS; i++)
    if (l->avoid_numbers[h][i] == 0) {
      l->avoid_numbers[h][i] = num;
      l->avoid_times[h][i] = (float)((double)now + (double)(float)dur);
      return;
    }
}
bool qa_bot_goal_is_avoided(qa_bot_goal_library *l, int64_t h, int64_t num) {
  if (h < 1 || h > QA_MAX_GOAL_STATES)
    return false;
  float now = l->time_fn ? (float)l->time_fn(l->time_ctx) : 0.0f;
  for (int i = 0; i < QA_MAX_AVOID_GOALS; i++)
    if (l->avoid_numbers[h][i] == num && l->avoid_times[h][i] > now)
      return true;
  return false;
}
