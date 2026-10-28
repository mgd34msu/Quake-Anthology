/* Port of src/bots/behavior/rerelease/nav.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_NAV_H
#define QA_BOTS_BEHAVIOR_RERELEASE_NAV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/behavior/rerelease/math.h"
#include "qa/bots/navigation/runtime.h"
#include "qa/bots/navigation/types.h"
#include "qa/contracts/identity.h"

typedef enum qa_nav_link_type {
  QA_NAV_LINK_WALK = 0, QA_NAV_LINK_LONG_JUMP = 1, QA_NAV_LINK_TELEPORT = 2, QA_NAV_LINK_WALK_OFF_LEDGE = 3,
  QA_NAV_LINK_PUSHER = 4, QA_NAV_LINK_BARRIER_JUMP = 5, QA_NAV_LINK_ELEVATOR = 6, QA_NAV_LINK_TRAIN = 7,
  QA_NAV_LINK_MANUAL_LONG_JUMP = 8, QA_NAV_LINK_CROUCH = 9, QA_NAV_LINK_LADDER = 10
} qa_nav_link_type;
typedef struct qa_nav_traversal { qa_bot_vec3 funnel, start, end; } qa_nav_traversal;
typedef struct qa_nav_graph_link {
  int64_t from, to; qa_nav_link_type type;
  bool has_traversal; qa_nav_traversal traversal;
  bool has_entity_bounds; qa_bot_vec3 entity_mins, entity_maxs;
} qa_nav_graph_link;
typedef struct qa_nav_graph_node { int64_t index; qa_bot_vec3 origin; double radius; int64_t flags; } qa_nav_graph_node;
typedef bool (*qa_nav_avoid_fn)(void *ctx, qa_nav_graph_node node);
typedef struct qa_nav_traverse_caps {
  bool jump, walk_off_ledge, entity_traversal, swim;
  double max_drop, max_jump_height;
  void *avoid_ctx; qa_nav_avoid_fn avoid; /* NULL = TS undefined */
} qa_nav_traverse_caps;
typedef struct qa_nav_path {
  int64_t *nodes; size_t node_count;
  qa_bot_vec3 *points; size_t point_count;
  /* parallel to points; has_link false = TS null entry */
  qa_nav_graph_link *links; bool *has_link; size_t link_count;
  double cost; int64_t generation; const char *map_digest; /* borrowed */
} qa_nav_path;
void qa_nav_path_destroy(qa_nav_path *path);
typedef bool (*qa_nav_visible_fn)(void *ctx, qa_bot_vec3 from, qa_bot_vec3 to);
typedef struct qa_nav_plan_options {
  bool has_caps; qa_nav_traverse_caps caps;
  void *visible_ctx; qa_nav_visible_fn visible; /* NULL = TS undefined */
  bool has_max_radius; double max_radius;
  bool has_start_above; double start_above;
} qa_nav_plan_options;
/* TS BotNavigation interface as fn+ctx; transport has=false models the
 * optional method being absent, a true value with kind unavailable is a
 * real TS result, and plan_path returning false is TS null. */
typedef struct qa_bot_navigation_iface {
  void *ctx;
  size_t (*node_count)(void *ctx);
  /* Fills caller array (cap from node_count); returns count written. */
  size_t (*nodes)(void *ctx, qa_nav_graph_node *out, size_t cap);
  bool (*plan_path)(void *ctx, qa_bot_vec3 start, qa_bot_vec3 goal, const qa_nav_plan_options *options, qa_nav_path *out);
  bool (*path_valid)(void *ctx, const qa_nav_path *path);
  bool has_transport;
  qa_bot_transport_step (*transport)(void *ctx, qa_nav_graph_link link, qa_bot_vec3 origin);
} qa_bot_navigation_iface;
qa_nav_traverse_caps qa_default_traverse_caps(void);
bool qa_nav_link_is_jump(qa_nav_link_type type);
bool qa_nav_link_is_entity(qa_nav_link_type type);
#define QA_PLAN_START_ABOVE 56
qa_bot_vec3 qa_steer_direction(qa_bot_vec3 from, qa_bot_vec3 to);

typedef bool (*qa_nav_ground_fn)(void *ctx, qa_actor_id *out);
typedef struct qa_source_rerelease_navigation {
  qa_navigation_runtime *runtime; /* borrowed */
  void *ground_ctx; qa_nav_ground_fn ground; /* NULL ground = TS () => null */
} qa_source_rerelease_navigation;
void qa_source_rerelease_navigation_init(qa_source_rerelease_navigation *nav, qa_navigation_runtime *runtime,
                                         void *ground_ctx, qa_nav_ground_fn ground);
qa_bot_navigation_iface qa_source_rerelease_navigation_as_iface(qa_source_rerelease_navigation *nav);
qa_bot_transport_step qa_source_rerelease_navigation_transport(qa_source_rerelease_navigation *nav,
                                                               qa_nav_graph_link link, qa_bot_vec3 origin);
size_t qa_source_rerelease_navigation_node_count(qa_source_rerelease_navigation *nav);
size_t qa_source_rerelease_navigation_nodes(qa_source_rerelease_navigation *nav, qa_nav_graph_node *out, size_t cap);
bool qa_source_rerelease_navigation_path_valid(qa_source_rerelease_navigation *nav, const qa_nav_path *path);
/* False = TS null. Unsupported admitted traversals / absent points abort. */
bool qa_source_rerelease_navigation_plan_path(qa_source_rerelease_navigation *nav, qa_bot_vec3 start, qa_bot_vec3 goal,
                                              const qa_nav_plan_options *options, qa_nav_path *out);
#endif
