/*
 * Port of src/content/q1/addons/monsters/ai/path.ts. GPL-2.0-or-later.
 * qa_q1am_nav_host is TS Mg3MonsterNavigationHost (a host interface in
 * TS too); it returns the real shared NavigationRuntime.
 */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_AI_PATH_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_AI_PATH_H

#include "qa/bots/navigation/runtime.h"
#include "qa/content/q1/base/monsters.h"

/* TS Mg3PathResult. */
enum {
  QA_Q1AM_PATH_ERROR = 0,
  QA_Q1AM_PATH_REACHED_GOAL = 1,
  QA_Q1AM_PATH_REACHED_PATH_END = 2,
  QA_Q1AM_PATH_MOVE_BLOCKED = 3,
  QA_Q1AM_PATH_IN_PROGRESS = 4
};

/* TS Mg3MonsterNavigationHost: forActor returns this actual actor's
 * selected movement profile, shape and collision exclusion (NULL = TS
 * null). The runtime is borrowed. */
typedef struct qa_q1am_nav_host {
  void *ctx;
  qa_navigation_runtime *(*for_actor)(void *ctx, qa_owned_actor actor);
} qa_q1am_nav_host;

/* Install with the map runtime before source checkpoint restore or the
 * first Horde think. A second registration for the same game aborts. */
void qa_q1am_register_mg3_monster_navigation(qa_q1_services *game, qa_q1am_nav_host host);
double qa_q1am_walk_mg3_path_to_goal(qa_q1_base_monster *monster, double distance, qa_vec3 goal);
/* C ownership (TS WeakMap entry dies with the game): frees the game's
 * registration. Call after the last use of the game, before its actor
 * registry is destroyed. No-op when none is registered. */
void qa_q1am_release_mg3_monster_navigation(qa_q1_services *game);

#endif
