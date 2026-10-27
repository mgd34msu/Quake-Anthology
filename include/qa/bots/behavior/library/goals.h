/* Port of src/bots/behavior/library/goals.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_GOALS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_GOALS_H
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { QA_GOAL_FLAG_ITEM = 1, QA_GOAL_FLAG_ROAM = 2, QA_GOAL_FLAG_DROPPED = 4 };
enum {
  QA_GOAL_ERROR_NONE = 0,
  QA_GOAL_ERROR_CANNOT_LOAD_ITEM_WEIGHTS = 9,
  QA_GOAL_ERROR_CANNOT_LOAD_ITEM_CONFIG = 10
};
#define QA_MAX_GOAL_STATES 64
#define QA_MAX_GOAL_STACK 8
#define QA_MAX_AVOID_GOALS 256
#define QA_SOURCE_GOAL_NUMBER_MIN 0x40000000
typedef struct qa_bot_goal {
  qa_vec3 origin;
  int64_t area;
  qa_vec3 mins, maxs;
  int64_t entity, number, flags, item_info;
} qa_bot_goal;
typedef struct qa_item_info_bots_a {
  char classname[32];
  char name[80];
  char model[80];
  int64_t model_index, type, index;
  float respawn_time;
  qa_vec3 mins, maxs;
  int64_t number;
} qa_item_info_bots_a;
typedef struct qa_item_config_bots_a {
  qa_item_info_bots_a *items;
  size_t item_count;
  char path[256];
} qa_item_config_bots_a;
bool qa_is_source_goal_number(double number);
bool qa_touching_goal(qa_vec3 origin, qa_bot_goal goal);
typedef struct qa_bot_goal_library {
  qa_bot_goal states[QA_MAX_GOAL_STATES + 1];
  bool allocated[QA_MAX_GOAL_STATES + 1];
  int64_t avoid_numbers[QA_MAX_GOAL_STATES + 1][QA_MAX_AVOID_GOALS];
  float avoid_times[QA_MAX_GOAL_STATES + 1][QA_MAX_AVOID_GOALS];
  qa_item_config_bots_a *item_config;
  double (*time_fn)(void *ctx);
  void *time_ctx;
} qa_bot_goal_library;
void qa_bot_goal_library_init(qa_bot_goal_library *l);
void qa_bot_goal_library_destroy(qa_bot_goal_library *l);
int64_t qa_bot_goal_allocate(qa_bot_goal_library *l);
void qa_bot_goal_free(qa_bot_goal_library *l, int64_t handle);
void qa_bot_goal_reset(qa_bot_goal_library *l, int64_t handle);
void qa_bot_goal_set_avoid(qa_bot_goal_library *l, int64_t handle, int64_t number, double duration);
bool qa_bot_goal_is_avoided(qa_bot_goal_library *l, int64_t handle, int64_t number);
#endif
