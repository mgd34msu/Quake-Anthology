/* Port of src/content/q3/base/game/radius-damage.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_RADIUS_DAMAGE_H
#define QA_CONTENT_Q3_BASE_GAME_RADIUS_DAMAGE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
/* Mirror of ../world.ts ActorSpatialQueries trace/area (TS owner unported). */
typedef enum qa_q3_radius_hit_kind { QA_Q3_RADIUS_HIT_NONE, QA_Q3_RADIUS_HIT_ACTOR, QA_Q3_RADIUS_HIT_WORLD } qa_q3_radius_hit_kind;
typedef struct qa_q3_radius_trace_result { double fraction; qa_q3_radius_hit_kind hit_kind; qa_actor_id hit_actor; } qa_q3_radius_trace_result;
typedef struct qa_q3_radius_trace_query { qa_vec3 start, end; bool has_pass_actor; qa_actor_id pass_actor; double mask; } qa_q3_radius_trace_query;
typedef struct qa_q3_radius_spatial {
  void *ctx; qa_q3_radius_trace_result (*trace_actor)(void *ctx, qa_q3_radius_trace_query query);
  size_t (*area_actors)(void *ctx, qa_bounds bounds, double max_count, qa_actor_id *out, size_t out_cap);
} qa_q3_radius_spatial;
typedef struct qa_q3_radius_target { qa_vec3 origin; qa_bounds bounds; bool accuracy_eligible; } qa_q3_radius_target;
typedef struct qa_q3_radius_host {
  qa_q3_radius_spatial spatial; void *ctx;
  bool (*target)(void *ctx, qa_actor_id actor, qa_q3_radius_target *out);
  void (*damage)(void *ctx, qa_actor_id actor, qa_vec3 direction, qa_vec3 point, double amount);
} qa_q3_radius_host;
bool qa_q3_can_damage(qa_q3_radius_spatial spatial, qa_actor_id actor, qa_bounds bounds, qa_vec3 origin);
bool qa_q3_radius_damage(qa_q3_radius_host host, qa_vec3 origin, double amount, double radius, const qa_actor_id *ignore);
#endif
