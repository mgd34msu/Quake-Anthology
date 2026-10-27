/* Port of src/bots/navigation/types.ts (types only). TS string unions become
 * enums; ReadonlySet<TravelMode> becomes a mode bitmask; SceneQueries /
 * MovementProfile / TraceShape / TracePolicy reuse contracts headers.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_TYPES_H
#define QA_BOTS_NAVIGATION_TYPES_H
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/scene.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum qa_travel_mode {
  QA_TM_WALK,
  QA_TM_CROUCH,
  QA_TM_JUMP,
  QA_TM_DROP,
  QA_TM_SWIM,
  QA_TM_WATER_JUMP,
  QA_TM_LADDER,
  QA_TM_TELEPORT,
  QA_TM_MOVER,
  QA_TM_JUMP_PAD,
  QA_TM_ROCKET_JUMP,
  QA_TM_BFG_JUMP,
  QA_TM_GRAPPLE,
  QA_TM_DOUBLE_JUMP,
  QA_TM_RAMP_JUMP,
  QA_TM_STRAFE_JUMP,
  QA_TM_UNKNOWN,
  QA_TM_COUNT
} qa_travel_mode;
const char *qa_travel_mode_name(qa_travel_mode mode);
bool qa_travel_mode_from_name(const char *name, qa_travel_mode *out);
typedef struct qa_navigation_map_identity {
  const char *name;
  qa_decoded_world_kind format;
  qa_content_digest digest;
} qa_navigation_map_identity;
typedef struct qa_navigation_profile {
  qa_movement_profile movement;
  qa_trace_shape shape;
  bool has_crouched_shape;
  qa_trace_shape crouched_shape;
  qa_trace_policy policy;
  uint32_t capabilities; /* 1u<<mode */
  double maximum_step, minimum_floor_normal, maximum_drop;
  int team; /* -1 null, 0 red, 1 blue */
  bool monster;
} qa_navigation_profile;
typedef enum qa_navigation_source_kind {
  QA_NS_AAS,
  QA_NS_NAV2,
  QA_NS_NAV3,
  QA_NS_CONSTRUCTED
} qa_navigation_source_kind;
/* aas: primary=area secondary=reachability; nav2/3: node/link; constructed: surface/leaf. */
typedef struct qa_navigation_source {
  qa_navigation_source_kind kind;
  int64_t primary;
  bool has_secondary;
  int64_t secondary;
} qa_navigation_source;
typedef struct qa_navigation_node {
  int64_t id;
  qa_vec3 origin;
  qa_bounds bounds;
  double radius;
  int64_t contents, flags, presence;
  bool has_source_cluster;
  int64_t source_cluster;
  qa_navigation_source source;
} qa_navigation_node;
typedef struct qa_traversal_hint {
  qa_vec3 funnel, start, end;
  bool has_ladder_plane;
  qa_vec3 ladder_plane;
} qa_traversal_hint;
typedef struct qa_navigation_entity_binding {
  bool has_model;
  int64_t model;
  qa_bounds bounds;
  const double *raw;
  size_t raw_count;
} qa_navigation_entity_binding;
typedef struct qa_navigation_edge {
  int64_t id, from, to;
  qa_travel_mode mode;
  qa_vec3 start, end;
  double travel_seconds;
  int64_t source_travel_type, source_flags;
  bool has_hint;
  qa_traversal_hint hint;
  bool has_entity;
  qa_navigation_entity_binding entity;
  qa_navigation_source source;
} qa_navigation_edge;
typedef struct qa_navigation_rejected {
  qa_navigation_source source;
  const char *reason;
} qa_navigation_rejected;
typedef struct qa_navigation_cluster_list {
  const int64_t *nodes;
  size_t count;
} qa_navigation_cluster_list;
struct qa_aas_asset;
struct qa_kex_navigation_asset;
typedef struct qa_navigation_graph {
  qa_navigation_map_identity map;
  qa_navigation_profile profile;
  const struct qa_aas_asset *aas_asset;
  const struct qa_kex_navigation_asset *kex_asset; /* TS asset union, at most one non-NULL */
  qa_navigation_node *nodes;
  size_t node_count;
  qa_navigation_edge *edges;
  size_t edge_count;
  qa_navigation_cluster_list *clusters;
  size_t cluster_count;
  qa_navigation_rejected *rejected;
  size_t rejected_count;
} qa_navigation_graph;
void qa_navigation_graph_destroy(qa_navigation_graph *g);
typedef struct qa_traversal_request {
  qa_vec3 from, to;
  qa_travel_mode mode;
  bool has_hint;
  qa_traversal_hint hint;
  bool has_entity;
  qa_navigation_entity_binding entity;
} qa_traversal_request;
typedef struct qa_traversal_admission {
  bool admitted;
  double seconds;
  qa_vec3 *trajectory;
  size_t trajectory_count;
  const char *reason;
} qa_traversal_admission;
void qa_traversal_admission_destroy(qa_traversal_admission *a);
typedef struct qa_navigation_route_prediction {
  void *ctx;
  qa_traversal_admission (*admit)(void *ctx, qa_traversal_request req);
} qa_navigation_route_prediction;
typedef struct qa_navigation_train_stop {
  int64_t id;
  qa_vec3 origin;
  bool has_next;
  int64_t next;
  double wait;
  bool teleport;
} qa_navigation_train_stop;
typedef struct qa_navigation_entity_state {
  qa_actor_id actor;
  bool enabled, locked;
  qa_bounds bounds;
  qa_vec3 velocity;
  bool has_destination;
  qa_vec3 destination;
  bool has_elevator;
  qa_vec3 elevator_origin, elevator_bottom, elevator_top;
  int elevator_phase; /*0 bottom 1 up 2 top 3 down*/
  bool has_train;
  qa_vec3 train_origin;
  bool train_running;
  const qa_navigation_train_stop *train_stops;
  size_t train_stop_count;
} qa_navigation_entity_state;
typedef struct qa_navigation_world {
  qa_scene_queries scene;
  const qa_actor_id *pass_actor;
  int64_t revision;
  void *ctx;
  qa_traversal_admission (*admit)(void *ctx, qa_traversal_request req,
                                  qa_navigation_profile profile);
  qa_navigation_route_prediction (*begin_route)(void *ctx, qa_navigation_profile profile);
  bool (*get_entity)(void *ctx, qa_navigation_entity_binding binding,
                     qa_navigation_entity_state *out);
  bool (*hazard)(void *ctx, qa_bounds bounds);
} qa_navigation_world;
typedef struct qa_navigation_route {
  qa_navigation_map_identity map;
  int64_t *nodes;
  size_t node_count;
  qa_navigation_edge *edges;
  size_t edge_count;
  qa_vec3 *points;
  size_t point_count;
  double travel_seconds;
  int64_t generation;
} qa_navigation_route;
void qa_navigation_route_destroy(qa_navigation_route *r);
typedef struct qa_navigation_route_result {
  bool is_route;
  qa_navigation_route route;
  const char *reason;
} qa_navigation_route_result;
typedef struct qa_navigation_estimate_query {
  int64_t start_node, goal_node;
  bool has_origin;
  qa_vec3 origin;
  bool has_travel_flags;
  int64_t travel_flags;
} qa_navigation_estimate_query;
typedef struct qa_navigation_estimate_result {
  bool is_estimate;
  double travel_time;
  bool has_first_edge;
  qa_navigation_edge first_edge;
} qa_navigation_estimate_result;
#endif
