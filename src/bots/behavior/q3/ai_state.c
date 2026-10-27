/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_state.h"
#include <stdlib.h>
#include <string.h>
void qa_bot_goal_state_copy_from(qa_bot_goal *d, qa_bot_goal s) { *d = s; }
void qa_bot_goal_state_clear(qa_bot_goal *g) { memset(g, 0, sizeof *g); }
bool qa_bot_state_store_bots_a_init(qa_bot_state_store_bots_a *s, int64_t cap) {
  s->capacity = cap;
  s->states = cap > 0 ? calloc((size_t)cap, sizeof *s->states) : NULL;
  return cap <= 0 || s->states != NULL;
}
void qa_bot_state_store_bots_a_destroy(qa_bot_state_store_bots_a *s) {
  free(s->states);
  s->states = NULL;
}
qa_bot_state_bots_a *qa_bot_state_store_get(qa_bot_state_store_bots_a *s, int64_t c) {
  return (c >= 0 && c < s->capacity) ? &s->states[c] : NULL;
}
void qa_copy_bot_player_state_bots_a(qa_bot_state_bots_a *d, const qa_bot_state_bots_a *s) {
  d->pm_type = s->pm_type;
  d->pm_flags = s->pm_flags;
  d->ps_origin = s->ps_origin;
  d->ps_velocity = s->ps_velocity;
  d->weapon_ps = s->weapon_ps;
  d->health_stat = s->health_stat;
  d->armor_stat = s->armor_stat;
  d->team_ps = s->team_ps;
}
