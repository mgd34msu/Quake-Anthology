/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/director.h"
#include <string.h>
void qa_source_bot_director_init(qa_source_bot_director *d) { memset(d, 0, sizeof *d); }
qa_bot_goal_status qa_source_bot_director_request_move_to_point(qa_source_bot_director *d,
                                                                int64_t c, qa_vec3 p) {
  return d->request_move ? d->request_move(d->host_ctx, c, p) : 0;
}
qa_bot_goal_status qa_source_bot_director_request_follow_entity(qa_source_bot_director *d,
                                                                int64_t c, int64_t e) {
  return d->request_follow ? d->request_follow(d->host_ctx, c, e) : 0;
}
void qa_source_bot_director_clear_goal(qa_source_bot_director *d, int64_t c) {
  if (d->clear_goal)
    d->clear_goal(d->host_ctx, c);
}
qa_bot_goal_status qa_source_bot_director_goal_status(qa_source_bot_director *d, int64_t c) {
  return d->goal_status ? d->goal_status(d->host_ctx, c) : 0;
}
const qa_source_bot_roster_entry *qa_source_bot_director_get_roster(const qa_source_bot_director *d,
                                                                    size_t *n) {
  *n = d->roster_count;
  return d->roster_entries;
}
size_t qa_source_bot_director_frame(qa_source_bot_director *d, double t,
                                    qa_bot_actor_command_bots_a *o, size_t m) {
  return d->frame ? d->frame(d->host_ctx, t, o, m) : 0;
}
