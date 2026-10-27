/* Port of src/bots/behavior/director.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_DIRECTOR_H
#define QA_BOTS_BEHAVIOR_DIRECTOR_H
#include "qa/bots/behavior/orders.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* TS ActorCommand (owner: contracts/session.ts) mirrored minimally. */
typedef struct qa_bot_actor_command_bots_a {
  qa_actor_id actor;
  int64_t buttons;
  qa_vec3 move, angles;
  int64_t weapon;
} qa_bot_actor_command_bots_a;
typedef struct qa_source_bot_roster_entry {
  qa_actor_id actor;
  int64_t source_client;
} qa_source_bot_roster_entry;
typedef struct qa_source_bot_director {
  qa_source_bot_roster_entry *roster_entries;
  size_t roster_count;
  void *host_ctx;
  qa_bot_goal_status (*request_move)(void *ctx, int64_t client, qa_vec3 point);
  qa_bot_goal_status (*request_follow)(void *ctx, int64_t client, int64_t entity);
  void (*clear_goal)(void *ctx, int64_t client);
  qa_bot_goal_status (*goal_status)(void *ctx, int64_t client);
  size_t (*frame)(void *ctx, double time_ms, qa_bot_actor_command_bots_a *out, size_t max_out);
} qa_source_bot_director;
void qa_source_bot_director_init(qa_source_bot_director *d);
qa_bot_goal_status qa_source_bot_director_request_move_to_point(qa_source_bot_director *d,
                                                                int64_t client, qa_vec3 point);
qa_bot_goal_status qa_source_bot_director_request_follow_entity(qa_source_bot_director *d,
                                                                int64_t client, int64_t entity);
void qa_source_bot_director_clear_goal(qa_source_bot_director *d, int64_t client);
qa_bot_goal_status qa_source_bot_director_goal_status(qa_source_bot_director *d, int64_t client);
const qa_source_bot_roster_entry *qa_source_bot_director_get_roster(const qa_source_bot_director *d,
                                                                    size_t *out_count);
size_t qa_source_bot_director_frame(qa_source_bot_director *d, double time_ms,
                                    qa_bot_actor_command_bots_a *out, size_t max_out);
#endif
