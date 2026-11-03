/* Port of src/content/q3/base/game/radius-damage.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_RADIUS_DAMAGE_H
#define QA_CONTENT_Q3_BASE_GAME_RADIUS_DAMAGE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/world.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
typedef struct qa_q3_radius_target { qa_vec3 origin; qa_bounds bounds; bool accuracy_eligible; } qa_q3_radius_target;
/* TS Q3RadiusHost; spatial uses traceActor and areaActors. */
typedef struct qa_q3_radius_host {
  qa_q3_base_actor_spatial_queries spatial; void *ctx;
  bool (*target)(void *ctx, qa_actor_id actor, qa_q3_radius_target *out);
  void (*damage)(void *ctx, qa_actor_id actor, qa_vec3 direction, qa_vec3 point, double amount);
} qa_q3_radius_host;
bool qa_q3_can_damage(qa_q3_base_actor_spatial_queries spatial, qa_actor_id actor, qa_bounds bounds, qa_vec3 origin);
bool qa_q3_radius_damage(qa_q3_radius_host host, qa_vec3 origin, double amount, double radius, const qa_actor_id *ignore);
#endif
