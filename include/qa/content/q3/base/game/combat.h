/* Port of src/content/q3/base/game/combat.ts. GPL-2.0-or-later.
 * TS CombatServices live on qa_q3_game_combat_context as fn+ctx. A context with
 * authority NULL has no GameplayAuthority: entity/actor damage goes through the
 * apply_damage callback, which unit fixtures supply. Everything else requires
 * qa_q3_base_combat_bridge_bind_game_services (or equivalent fields). */
#ifndef QA_CONTENT_Q3_BASE_GAME_COMBAT_H
#define QA_CONTENT_Q3_BASE_GAME_COMBAT_H
#include <stdbool.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/radius_damage.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/contracts/gameplay.h"
#include "qa/world/gameplay/authority.h"
enum { QA_Q3_DAMAGE_RADIUS = 0x1, QA_Q3_DAMAGE_NO_ARMOR = 0x2, QA_Q3_DAMAGE_NO_KNOCKBACK = 0x4,
  QA_Q3_DAMAGE_NO_PROTECTION = 0x8, QA_Q3_DAMAGE_NO_TEAM_PROTECTION = 0x10 };
typedef struct qa_q3_damage_diagnostic { double time, entity_num, health, damage, armor; } qa_q3_damage_diagnostic;
typedef qa_vec3 qa_q3_damage_direction;
/* TS Q3DamageCall; source/owner are participants, direction/point borrow the caller's storage. */
typedef struct qa_q3_damage_call {
  qa_q3_game_entity *target; qa_q3_damage_participant source, owner;
  qa_q3_damage_direction *direction; const qa_vec3 *point; double amount, flags, method_of_death;
} qa_q3_damage_call;
typedef struct qa_q3_game_combat_context {
  qa_q3_base_product product; double time, intermission_queued, game_type; bool friendly_fire; double knockback;
  qa_q3_entity_pool *entities; qa_q3_base_actor_spatial_queries spatial; void *ctx;
  bool (*is_live)(void *ctx, qa_actor_id actor);
  void (*apply_damage)(void *ctx, qa_actor_id target, double amount, double knockback, qa_vec3 direction, qa_vec3 point, bool radius);
  void (*debug_damage)(void *ctx, qa_q3_damage_diagnostic diagnostic);
  void (*check_hurt_carrier)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
  bool (*log_accuracy_hit)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
  bool (*check_obelisk_attack)(void *ctx, qa_q3_game_entity *target, qa_q3_use_participant attacker);
  void (*invulnerability_effect)(void *ctx, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point);
  /* TS CombatServices. services_ctx is passed to the functions below. */
  qa_gameplay_authority *authority; const qa_source_damage_modifier *source_damage_modifier;
  void *services_ctx;
  qa_attack_provenance (*attack)(void *services_ctx, qa_q3_damage_participant inflictor, qa_q3_use_participant attacker,
      qa_item_id weapon, double means_of_death, double flags, const qa_actor_id *originating_projectile);
  /* Runs operation synchronously while retaining call for beforeReaction feedback. */
  qa_damage_outcome (*dispatch)(void *services_ctx, qa_q3_damage_call call, qa_damage_outcome (*operation)(void *op_ctx), void *op_ctx);
  qa_q3_damage_participant (*participant)(void *services_ctx, qa_actor_id actor);
  bool (*parent)(void *services_ctx, qa_actor_id actor, qa_actor_id *out); /* false = TS null */
  bool (*linked_bounds)(void *services_ctx, qa_actor_id actor, qa_bounds *out); /* false = TS null */
  bool (*is_player)(void *services_ctx, qa_actor_id actor);
} qa_q3_game_combat_context;
struct qa_q3_base_combat_bridge;
/* Fills the TS Q3CombatBridge context closures that bridge init does not set. Idempotent. */
void qa_q3_base_combat_bridge_bind_game_services(struct qa_q3_base_combat_bridge *bridge);
bool qa_q3_invulnerability_blocks(qa_q3_game_combat_context *context, qa_q3_game_entity *target,
    const qa_vec3 *direction, const qa_vec3 *point, double method_of_death);
double qa_q3_check_armor(qa_q3_game_entity *target, double damage, double flags);
typedef enum qa_q3_game_admit_result { QA_Q3_GAME_ADMIT_CONTINUE, QA_Q3_GAME_ADMIT_HANDLED } qa_q3_game_admit_result;
qa_q3_game_admit_result qa_q3_admit_target_damage(double intermission_queued, qa_q3_game_entity *target,
    const qa_q3_use_participant *source, const qa_q3_use_participant *owner);
void qa_q3_combat_damage(qa_q3_game_combat_context *context, qa_q3_damage_participant target,
    const qa_q3_damage_participant *inflictor, const qa_q3_use_participant *attacker,
    qa_q3_damage_direction *direction, const qa_vec3 *point, double amount, double flags, double method_of_death);
/* TS damage(..., originatingProjectile); originating_projectile NULL is TS undefined. */
void qa_q3_combat_damage_from(qa_q3_game_combat_context *context, qa_q3_damage_participant target,
    const qa_q3_damage_participant *inflictor, const qa_q3_use_participant *attacker,
    qa_q3_damage_direction *direction, const qa_vec3 *point, double amount, double flags, double method_of_death,
    const qa_actor_id *originating_projectile);
void qa_q3_damage_feedback(qa_q3_game_combat_context *context, qa_q3_damage_call call, qa_damage_decision decision);
void qa_q3_foreign_damage_feedback(qa_q3_game_combat_context *context, qa_q3_game_entity *target, qa_q3_game_entity *owner, qa_damage_decision decision);
bool qa_q3_combat_can_damage(qa_q3_game_combat_context *context, qa_q3_damage_participant target, qa_vec3 origin);
bool qa_q3_combat_radius_damage(qa_q3_game_combat_context *context, qa_vec3 origin, qa_q3_damage_participant attacker,
    double amount, double radius, const qa_q3_damage_participant *ignore, double method_of_death);
bool qa_q3_combat_radius_damage_from(qa_q3_game_combat_context *context, qa_vec3 origin, qa_q3_damage_participant attacker,
    double amount, double radius, const qa_q3_damage_participant *ignore, double method_of_death,
    const qa_actor_id *originating_projectile);
#endif
