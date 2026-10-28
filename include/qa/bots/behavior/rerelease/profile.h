/* Port of src/bots/behavior/rerelease/profile.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_PROFILE_H
#define QA_BOTS_BEHAVIOR_RERELEASE_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/behavior/rerelease/brain.h"
#include "qa/bots/behavior/rerelease/math.h"
#include "qa/bots/behavior/rerelease/rng.h"
#include "qa/bots/behavior/rerelease/world.h"

typedef enum qa_rerelease_source { QA_RERELEASE_Q1 = 0, QA_RERELEASE_Q2 = 1 } qa_rerelease_source;
const char *qa_rerelease_source_name(qa_rerelease_source source);
bool qa_rerelease_source_from_name(const char *name, qa_rerelease_source *out);

/* TS RereleaseOrders as fn+ctx. */
typedef struct qa_rerelease_orders {
  void *ctx;
  void (*request_move_to_point)(void *ctx, qa_bot_vec3 point);
  void (*request_follow_entity)(void *ctx, double entity, qa_bot_vec3 origin);
  void (*clear_explicit_goal)(void *ctx);
  int64_t (*goal_status)(void *ctx);
} qa_rerelease_orders;

typedef struct qa_rerelease_callbacks {
  void *ctx;
  double (*time)(void *ctx);
  void (*pre_think)(void *ctx, const char *name, qa_rerelease_orders orders);
  void (*post_think)(void *ctx, const char *name, qa_bot_usercmd command);
  void (*chat)(void *ctx, qa_bot_chat_event event);
  void (*select_weapon)(void *ctx, double weapon_number);
  double (*weapon_impulse)(void *ctx, double weapon_number);
  bool (*human_teammate_near)(void *ctx);
} qa_rerelease_callbacks;

typedef struct qa_rerelease_profile_options {
  const char *definition;
  qa_rerelease_source source;
  qa_bot_knowledge *knowledge;
  const char *skill;
  int64_t seed;
  qa_bot_game_mode game_mode; /* copied (string duplicated into brain) */
  const qa_character_entry *character; /* NULL = TS undefined */
  double max_health, run_speed, walk_speed;
  qa_bot_movement_projection movement;
  qa_rerelease_callbacks callbacks;
} qa_rerelease_profile_options;

typedef struct qa_rerelease_pending_chat {
  double time;
  char *locstring, *type; /* owned in behavior/checkpoint copies */
  double delay_ms; bool team_only;
} qa_rerelease_pending_chat;

typedef struct qa_rerelease_behavior_checkpoint {
  int64_t version;
  qa_rerelease_source source;
  char *definition; /* owned */
  int64_t rng;
  qa_bot_brain_checkpoint brain;
  qa_rerelease_pending_chat *pending_chats; size_t pending_count;
} qa_rerelease_behavior_checkpoint;
void qa_rerelease_behavior_checkpoint_destroy(qa_rerelease_behavior_checkpoint *checkpoint);

typedef struct qa_rerelease_bot_behavior {
  char *definition; /* owned */
  qa_rerelease_source source;
  qa_rerelease_callbacks callbacks;
  qa_bot_brain brain;
  qa_xorshift32 random;
  qa_rerelease_pending_chat *pending_chats; size_t pending_count, pending_cap;
} qa_rerelease_bot_behavior;
/* False only on OOM; brain init errors abort with TS text. */
bool qa_rerelease_bot_behavior_init(qa_rerelease_bot_behavior *behavior, qa_rerelease_profile_options options);
void qa_rerelease_bot_behavior_destroy(qa_rerelease_bot_behavior *behavior);
qa_rerelease_orders qa_rerelease_bot_behavior_as_orders(qa_rerelease_bot_behavior *behavior);
qa_bot_usercmd qa_rerelease_bot_behavior_think(qa_rerelease_bot_behavior *behavior, qa_bot_world *world);
void qa_rerelease_bot_behavior_set_objective_goal(qa_rerelease_bot_behavior *behavior, const qa_bot_vec3 *point);
void qa_rerelease_bot_behavior_request_move_to_point(qa_rerelease_bot_behavior *behavior, qa_bot_vec3 point);
void qa_rerelease_bot_behavior_request_follow_entity(qa_rerelease_bot_behavior *behavior, double entity, qa_bot_vec3 origin);
void qa_rerelease_bot_behavior_clear_explicit_goal(qa_rerelease_bot_behavior *behavior);
int64_t qa_rerelease_bot_behavior_goal_status(qa_rerelease_bot_behavior *behavior);
bool qa_rerelease_bot_behavior_reset_for_level(qa_rerelease_bot_behavior *behavior, qa_bot_game_mode mode);
bool qa_rerelease_bot_behavior_checkpoint_make(qa_rerelease_bot_behavior *behavior, qa_rerelease_behavior_checkpoint *out);
/* Wrong version/source/definition aborts with TS Error text. False only on OOM. */
bool qa_rerelease_bot_behavior_restore(qa_rerelease_bot_behavior *behavior, const qa_rerelease_behavior_checkpoint *checkpoint);
#endif
