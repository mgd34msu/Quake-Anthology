/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/core/math.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
qa_bot_waypoint_bots_a *qa_bot_create_waypoint_bots_a(const char *name, qa_vec3 origin,
                                                      int64_t area) {
  qa_bot_waypoint_bots_a *w = calloc(1, sizeof *w);
  if (!w)
    return NULL;
  w->inuse = true;
  snprintf(w->name, sizeof w->name, "%s", name ? name : "");
  w->goal.origin = origin;
  w->goal.area = area;
  w->goal.mins = (qa_vec3){-8, -8, -8};
  w->goal.maxs = (qa_vec3){8, 8, 8};
  return w;
}
static bool ci_eq(const char *a, const char *b) {
  while (*a && *b) {
    int x = (uint8_t)*a, y = (uint8_t)*b;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (x != y)
      return false;
    a++;
    b++;
  }
  return *a == *b;
}
qa_bot_waypoint_bots_a *qa_bot_find_waypoint_bots_a(qa_bot_waypoint_bots_a *w, const char *name) {
  for (; w; w = w->next)
    if (ci_eq(w->name, name))
      return w;
  return NULL;
}
void qa_bot_free_waypoints_bots_a(qa_bot_waypoint_bots_a *first) {
  while (first) {
    qa_bot_waypoint_bots_a *n = first->next;
    free(first);
    first = n;
  }
}
void qa_bot_set_movedir_bots_a(qa_vec3 angles, qa_vec3 *movedir) {
  /* TS BotSetMovedir: special up/down else angle vectors forward (bot rule via core). */
  if (angles.x == -1 && angles.y == 0 && angles.z == 0) {
    *movedir = (qa_vec3){0, 0, 1};
    return;
  }
  if (angles.x == -2 && angles.y == 0 && angles.z == 0) {
    *movedir = (qa_vec3){0, 0, -1};
    return;
  }
  qa_angle_vectors av = qa_math_angle_vectors(angles);
  *movedir = av.forward;
}
void qa_bot_clear_activate_goal_stack_bots_a(qa_bot_state_bots_a *s,
                                             qa_bot_activate_goal_bots_a **top) {
  (void)s;
  *top = NULL;
}
bool qa_bot_push_activate_goal_bots_a(qa_bot_activate_goal_bots_a **top,
                                      qa_bot_activate_goal_bots_a *a, int64_t *depth) {
  if (*depth >= QA_MAX_ACTIVATESTACK)
    return false;
  a->next = *top;
  *top = a;
  (*depth)++;
  return true;
}
bool qa_bot_pop_activate_goal_bots_a(qa_bot_activate_goal_bots_a **top, int64_t *depth) {
  if (!*top)
    return false;
  *top = (*top)->next;
  if (*depth > 0)
    (*depth)--;
  return true;
}
