/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/population.h"
#include "qa/foundation/support.h"
#include <string.h>
void qa_shared_bot_population_init(qa_shared_bot_population *p, bool (*is_live)(void *ctx, qa_actor_id actor),
                                   void *is_live_ctx, qa_source_bot_director *director) {
  memset(p, 0, sizeof *p);
  p->is_live = is_live;
  p->is_live_ctx = is_live_ctx;
  p->director = director;
}
static bool client_for(qa_shared_bot_population *p, qa_actor_id actor, int64_t *out) {
  if (!p->is_live(p->is_live_ctx, actor))
    return false;
  size_t n = 0;
  const qa_source_bot_roster_entry *roster = qa_source_bot_director_roster(p->director, &n);
  for (size_t i = 0; i < n; i++)
    if (qa_same_actor(roster[i].actor.id, actor)) {
      *out = roster[i].source_client;
      return true;
    }
  return false;
}
qa_bot_goal_status qa_shared_bot_population_request_move_to_point(qa_shared_bot_population *p,
                                                                  qa_actor_id actor, qa_vec3 point) {
  int64_t client;
  return client_for(p, actor, &client) ? qa_source_bot_director_request_move_to_point(p->director, client, point)
                                       : 0;
}
qa_bot_goal_status qa_shared_bot_population_request_follow_entity(qa_shared_bot_population *p,
                                                                  qa_actor_id actor, int64_t entity) {
  int64_t client;
  return client_for(p, actor, &client) ? qa_source_bot_director_request_follow_entity(p->director, client, entity)
                                       : 0;
}
void qa_shared_bot_population_clear_goal(qa_shared_bot_population *p, qa_actor_id actor) {
  int64_t client;
  if (client_for(p, actor, &client))
    qa_source_bot_director_clear_goal(p->director, client);
}
qa_bot_goal_status qa_shared_bot_population_goal_status(qa_shared_bot_population *p, qa_actor_id actor) {
  int64_t client;
  return client_for(p, actor, &client) ? qa_source_bot_director_goal_status(p->director, client) : 0;
}
const qa_actor_command *qa_shared_bot_population_frame(qa_shared_bot_population *p, qa_bot_frame frame,
                                                       size_t *out_count) {
  size_t n = 0;
  const qa_actor_command *commands = qa_source_bot_director_frame(p->director, frame.time_milliseconds, &n);
  for (size_t i = 0; i < n; i++) {
    if (!p->is_live(p->is_live_ctx, commands[i].actor))
      qa_range_error("Bot command targets an actor outside the shared world");
    for (size_t j = 0; j < i; j++)
      if (qa_same_actor(commands[j].actor, commands[i].actor))
        qa_range_error("Two bot commands target the same actor in one frame");
  }
  *out_count = n;
  return commands;
}
