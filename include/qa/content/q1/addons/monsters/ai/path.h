/*
 * Port of src/content/q1/addons/monsters/ai/path.ts.
 * Mirrored types (qa_q1am_nav_*; TS owners:
 * src/bots/navigation/types.ts and runtime.ts): NavigationRoute and
 * NavigationRuntime reduced to the fields/hooks this module reads;
 * the route query keeps the TS edgeFilter as fn+ctx.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_AI_PATH_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_AI_PATH_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

enum {
  QA_Q1AM_PATH_ERROR = 0,
  QA_Q1AM_PATH_REACHED_GOAL = 1,
  QA_Q1AM_PATH_REACHED_PATH_END = 2,
  QA_Q1AM_PATH_MOVE_BLOCKED = 3,
  QA_Q1AM_PATH_IN_PROGRESS = 4
};

typedef struct qa_q1am_nav_edge { double id; const char *mode; } qa_q1am_nav_edge;
typedef struct qa_q1am_nav_route {
  const char *map_digest;
  double *nodes; size_t node_count;
  const qa_q1am_nav_edge **edges; size_t edge_count;
  qa_vec3 *points; size_t point_count;
  double travel_seconds, generation;
  void *raw;
  void (*destroy)(void *raw);
} qa_q1am_nav_route;
typedef struct qa_q1am_nav_query {
  qa_vec3 start, goal;
  void *filter_ctx;
  bool (*edge_filter)(void *ctx, const qa_q1am_nav_edge *edge);
} qa_q1am_nav_query;
typedef struct qa_q1am_nav_runtime {
  void *ctx;
  const char *map_digest;
  bool profile_monster;
  bool has_pass_actor; qa_actor_id pass_actor;
  const qa_q1am_nav_edge *graph_edges; size_t graph_edge_count;
  bool (*node_exists)(void *ctx, double id);
  bool (*route)(void *ctx, const qa_q1am_nav_query *query, qa_q1am_nav_route *out);
  bool (*route_still_valid)(void *ctx, const qa_q1am_nav_route *route);
} qa_q1am_nav_runtime;
typedef struct qa_q1am_nav_host {
  void *ctx;
  qa_q1am_nav_runtime *(*for_actor)(void *ctx, qa_owned_actor actor);
} qa_q1am_nav_host;

void qa_q1am_register_mg3_monster_navigation(qa_q1am_game *game, qa_q1am_nav_host host);
double qa_q1am_walk_mg3_path_to_goal(qa_q1am_monster *monster, double distance, qa_vec3 goal);

#endif
