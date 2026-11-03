/* Port of src/content/q3/base/game/projectile.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_PROJECTILE_H
#define QA_CONTENT_Q3_BASE_GAME_PROJECTILE_H
#include <stdbool.h>
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/world.h"
typedef struct qa_q3_projectile {
  qa_owned_actor actor; qa_actor_id owner; double weapon, direct, splash, radius, method, splash_method;
  qa_vec3 damage_point; qa_q3_base_trajectory trajectory; double flags; bool has_pass; qa_actor_id pass;
} qa_q3_projectile;
typedef struct qa_q3_projectile_target { bool damageable, player, accuracy_eligible, invulnerable; } qa_q3_projectile_target;
typedef enum qa_q3_projectile_impact_kind { QA_Q3_IMPACT_BOUNCE, QA_Q3_IMPACT_IMPACT } qa_q3_projectile_impact_kind;
typedef struct qa_q3_projectile_impact {
  qa_q3_projectile_impact_kind kind; qa_vec3 normal; bool has_target; qa_actor_id target; bool flesh; double surface_flags;
} qa_q3_projectile_impact;
typedef enum qa_q3_projectile_phase { QA_Q3_PHASE_FLIGHT, QA_Q3_PHASE_EVENT, QA_Q3_PHASE_ATTACHED } qa_q3_projectile_phase;
typedef enum qa_q3_reflection_kind { QA_Q3_REFLECTION_MISS, QA_Q3_REFLECTION_HIT } qa_q3_reflection_kind;
typedef struct qa_q3_reflection_result { qa_q3_reflection_kind kind; qa_vec3 bounce_direction; } qa_q3_reflection_result;
typedef struct qa_q3_projectile_host {
  double time, previous_time; void *ctx;
  bool (*live)(void *ctx); qa_q3_projectile_phase (*phase)(void *ctx); double (*event_time)(void *ctx);
  void (*clear_event)(void *ctx); qa_vec3 (*origin)(void *ctx);
  void (*move)(void *ctx, qa_vec3 origin, qa_vec3 velocity); void (*set_origin)(void *ctx, qa_vec3 origin);
  void (*link)(void *ctx); void (*release)(void *ctx);
  qa_q3_base_actor_trace_result (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass);
  bool (*target)(void *ctx, qa_actor_id actor, qa_q3_projectile_target *out);
  qa_actor_id (*world_actor)(void *ctx); void (*emit)(void *ctx, qa_q3_projectile_impact event);
  void (*retain)(void *ctx); void (*damage)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point);
  bool (*radius)(void *ctx, qa_vec3 origin, const qa_actor_id *ignore); void (*accuracy)(void *ctx);
  void (*think)(void *ctx); void (*moved)(void *ctx);
  bool has_reflection; qa_q3_reflection_result (*reflection_impact)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point);
  bool has_special; bool (*special_impact)(void *ctx, qa_q3_base_actor_trace_result trace, qa_actor_id target);
  void (*special_after_move)(void *ctx); void (*special_no_impact)(void *ctx);
} qa_q3_projectile_host;
typedef struct qa_q3_launch_result { double expires; qa_q3_base_trajectory trajectory; } qa_q3_launch_result;
qa_q3_launch_result qa_q3_launch_projectile(qa_vec3 start, qa_vec3 direction, double speed, bool gravity, double duration, double time);
void qa_q3_bounce_projectile(qa_q3_projectile *p, qa_q3_projectile_host host, qa_q3_base_actor_trace_result trace);
void qa_q3_explode_projectile(qa_q3_projectile *p, qa_q3_projectile_host host);
void qa_q3_impact_projectile(qa_q3_projectile *p, qa_q3_projectile_host host, qa_q3_base_actor_trace_result trace);
void qa_q3_step_projectile(qa_q3_projectile *p, qa_q3_projectile_host host);
#endif
