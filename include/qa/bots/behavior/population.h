/* Port of src/bots/behavior/population.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_POPULATION_H
#define QA_BOTS_BEHAVIOR_POPULATION_H
#include "qa/bots/behavior/director.h"
#include <stdbool.h>
#include <stddef.h>
typedef struct qa_bot_frame {
  double time_milliseconds, elapsed_milliseconds;
} qa_bot_frame;
typedef struct qa_shared_bot_population {
  bool (*is_live)(void *ctx, qa_actor_id actor);
  void *is_live_ctx;
  qa_source_bot_director *director;
} qa_shared_bot_population;
void qa_shared_bot_population_init(qa_shared_bot_population *p, qa_source_bot_director *director);
qa_bot_goal_status qa_shared_bot_population_request_move_to_point(qa_shared_bot_population *p,
                                                                  qa_actor_id actor, qa_vec3 point);
qa_bot_goal_status qa_shared_bot_population_request_follow_entity(qa_shared_bot_population *p,
                                                                  qa_actor_id actor,
                                                                  int64_t entity);
void qa_shared_bot_population_clear_goal(qa_shared_bot_population *p, qa_actor_id actor);
qa_bot_goal_status qa_shared_bot_population_goal_status(qa_shared_bot_population *p,
                                                        qa_actor_id actor);
size_t qa_shared_bot_population_frame(qa_shared_bot_population *p, qa_bot_frame frame,
                                      qa_bot_actor_command_bots_a *out, size_t max_out);
#endif
