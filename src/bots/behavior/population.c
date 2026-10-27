/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/population.h"
#include "qa/foundation/support.h"
#include <string.h>
void qa_shared_bot_population_init(qa_shared_bot_population *p, qa_source_bot_director *d) {
  memset(p, 0, sizeof *p);
  p->director = d;
}
static bool actor_eq(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token &&
         a.session.token == b.session.token;
}
static bool client_for(qa_shared_bot_population *p, qa_actor_id actor, int64_t *out) {
  if (!p->is_live || !p->is_live(p->is_live_ctx, actor))
    return false;
  size_t n = 0;
  const qa_source_bot_roster_entry *r = qa_source_bot_director_get_roster(p->director, &n);
  for (size_t i = 0; i < n; i++)
    if (actor_eq(r[i].actor, actor)) {
      *out = r[i].source_client;
      return true;
    }
  return false;
}
qa_bot_goal_status qa_shared_bot_population_request_move_to_point(qa_shared_bot_population *p,
                                                                  qa_actor_id a, qa_vec3 pt) {
  int64_t c;
  return client_for(p, a, &c) ? qa_source_bot_director_request_move_to_point(p->director, c, pt)
                              : 0;
}
qa_bot_goal_status qa_shared_bot_population_request_follow_entity(qa_shared_bot_population *p,
                                                                  qa_actor_id a, int64_t e) {
  int64_t c;
  return client_for(p, a, &c) ? qa_source_bot_director_request_follow_entity(p->director, c, e) : 0;
}
void qa_shared_bot_population_clear_goal(qa_shared_bot_population *p, qa_actor_id a) {
  int64_t c;
  if (client_for(p, a, &c))
    qa_source_bot_director_clear_goal(p->director, c);
}
qa_bot_goal_status qa_shared_bot_population_goal_status(qa_shared_bot_population *p,
                                                        qa_actor_id a) {
  int64_t c;
  return client_for(p, a, &c) ? qa_source_bot_director_goal_status(p->director, c) : 0;
}
size_t qa_shared_bot_population_frame(qa_shared_bot_population *p, qa_bot_frame f,
                                      qa_bot_actor_command_bots_a *out, size_t max_out) {
  size_t n = qa_source_bot_director_frame(p->director, f.time_milliseconds, out, max_out);
  for (size_t i = 0; i < n; i++) {
    if (!p->is_live || !p->is_live(p->is_live_ctx, out[i].actor))
      qa_range_error("Bot command targets an actor outside the shared world");
    for (size_t j = 0; j < i; j++)
      if (actor_eq(out[j].actor, out[i].actor))
        qa_range_error("Two bot commands target the same actor in one frame");
  }
  return n;
}
