/* Port of src/content/q3/base/world.ts (types only: interfaces become fn
 * tables + ctx). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_WORLD_H
#define QA_CONTENT_Q3_BASE_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/state.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* TS TraceShape: "point" has no bounds; "box"|"capsule" carry mins/maxs. */
typedef enum qa_q3_base_trace_shape_kind { QA_Q3_BASE_SHAPE_POINT, QA_Q3_BASE_SHAPE_BOX, QA_Q3_BASE_SHAPE_CAPSULE } qa_q3_base_trace_shape_kind;
typedef struct qa_q3_base_trace_shape { qa_q3_base_trace_shape_kind kind; qa_vec3 mins, maxs; } qa_q3_base_trace_shape;
typedef struct qa_q3_base_server_trace_query {
  qa_vec3 start, end; qa_q3_base_trace_shape shape; double pass_entity_num, mask;
} qa_q3_base_server_trace_query;
typedef enum qa_q3_base_trace_solidity { QA_Q3_BASE_SOLID_CLEAR, QA_Q3_BASE_SOLID_START_SOLID, QA_Q3_BASE_SOLID_ALL_SOLID } qa_q3_base_trace_solidity;
typedef struct qa_q3_base_server_trace_result {
  double fraction; qa_vec3 end; double entity_num;
  qa_q3_base_trace_solidity solidity;
  bool contact_is_plane; qa_plane contact_plane; /* false = {kind:"none"} */
  double contents, surface_flags;
} qa_q3_base_server_trace_result;
/* TS ActorTraceQuery: pass_actor NULL is TS null. */
typedef struct qa_q3_base_actor_trace_query {
  qa_vec3 start, end; qa_q3_base_trace_shape shape; double mask;
  const qa_actor_id *pass_actor;
} qa_q3_base_actor_trace_query;
typedef enum qa_q3_base_actor_hit_kind { QA_Q3_BASE_HIT_NONE, QA_Q3_BASE_HIT_WORLD, QA_Q3_BASE_HIT_ACTOR } qa_q3_base_actor_hit_kind;
typedef struct qa_q3_base_actor_trace_result {
  double fraction; qa_vec3 end;
  qa_q3_base_trace_solidity solidity;
  bool contact_is_plane; qa_plane contact_plane;
  double contents, surface_flags;
  qa_q3_base_actor_hit_kind hit_kind; qa_actor_id hit_actor;
} qa_q3_base_actor_trace_result;
/* TS ActorSpatialQueries. area_actors returns borrowed array + count;
 * contact_actor's optional capsule defaults false in TS callers. */
typedef struct qa_q3_base_actor_spatial_queries {
  void *ctx;
  const qa_actor_id *(*area_actors)(void *ctx, qa_bounds bounds, double maximum, size_t *out_count);
  qa_q3_base_actor_trace_result (*trace_actor)(void *ctx, qa_q3_base_actor_trace_query query);
  bool (*contact_actor)(void *ctx, qa_bounds bounds, qa_actor_id actor, bool capsule);
} qa_q3_base_actor_spatial_queries;
typedef struct qa_q3_base_link_state { qa_bounds absbounds; bool linked; double linkcount; } qa_q3_base_link_state;
/* TS ServerWorld. area_entities' optional maximum: has_maximum false is TS
 * undefined; result is a borrowed array + count. link_state false is TS
 * undefined. */
typedef struct qa_q3_base_server_world {
  void *ctx;
  bool (*entity_contact)(void *ctx, qa_bounds bounds, double entity_num, bool capsule);
  qa_q3_base_server_trace_result (*trace)(void *ctx, qa_q3_base_server_trace_query query);
  const double *(*area_entities)(void *ctx, qa_bounds bounds, bool has_maximum, double maximum, size_t *out_count);
  double (*point_contents)(void *ctx, qa_vec3 point, double pass_entity_num);
  bool (*link_state)(void *ctx, double number, qa_q3_base_link_state *out);
  void (*link)(void *ctx, qa_q3_game_entity *entity);
  void (*unlink)(void *ctx, double number);
} qa_q3_base_server_world;
#endif
