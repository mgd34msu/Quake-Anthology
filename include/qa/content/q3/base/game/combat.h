/* Port of src/content/q3/base/game/combat.ts. GPL-2.0-or-later.
 * GameplayAuthority/DamageDecision mirrors: TS owners world/gameplay/ +
 * contracts/gameplay.ts (landed contracts used where available). */
#ifndef QA_CONTENT_Q3_BASE_GAME_COMBAT_H
#define QA_CONTENT_Q3_BASE_GAME_COMBAT_H
#include <stdbool.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/radius_damage.h"
#include "qa/content/q3/base/game/state.h"
enum { QA_Q3_DAMAGE_RADIUS = 0x1, QA_Q3_DAMAGE_NO_ARMOR = 0x2, QA_Q3_DAMAGE_NO_KNOCKBACK = 0x4,
  QA_Q3_DAMAGE_NO_PROTECTION = 0x8, QA_Q3_DAMAGE_NO_TEAM_PROTECTION = 0x10 };
typedef struct qa_q3_damage_diagnostic { double time, entity_num, health, damage, armor; } qa_q3_damage_diagnostic;
typedef qa_vec3 qa_q3_damage_direction;
/* Simplified authority mirror: apply receives computed amount/knockback. */
typedef struct qa_q3_damage_call {
  qa_q3_game_entity *target; qa_q3_damage_participant source, owner;
  qa_q3_damage_direction *direction; const qa_vec3 *point; double amount, flags, method_of_death;
} qa_q3_damage_call;
typedef struct qa_q3_game_combat_context {
  qa_q3_game_product product; double time, intermission_queued, game_type; bool friendly_fire; double knockback;
  qa_q3_entity_pool *entities; qa_q3_radius_spatial spatial; void *ctx;
  bool (*is_live)(void *ctx, qa_actor_id actor);
  void (*apply_damage)(void *ctx, qa_actor_id target, double amount, double knockback, qa_vec3 direction, qa_vec3 point, bool radius);
  void (*debug_damage)(void *ctx, qa_q3_damage_diagnostic diagnostic);
  void (*check_hurt_carrier)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
  bool (*log_accuracy_hit)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
  bool (*check_obelisk_attack)(void *ctx, qa_q3_game_entity *target, qa_q3_use_participant attacker);
  void (*invulnerability_effect)(void *ctx, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point);
  /* radius host delegation for qa_q3_combat_radius_damage */
  qa_q3_radius_host radius_host; bool has_radius_host;
} qa_q3_game_combat_context;
bool qa_q3_invulnerability_blocks(qa_q3_game_combat_context *context, qa_q3_game_entity *target,
    const qa_vec3 *direction, const qa_vec3 *point, double method_of_death);
double qa_q3_check_armor(qa_q3_game_entity *target, double damage, double flags);
typedef enum qa_q3_game_admit_result { QA_Q3_GAME_ADMIT_CONTINUE, QA_Q3_GAME_ADMIT_HANDLED } qa_q3_game_admit_result;
qa_q3_game_admit_result qa_q3_admit_target_damage(double intermission_queued, qa_q3_game_entity *target,
    const qa_q3_use_participant *source, const qa_q3_use_participant *owner);
void qa_q3_combat_damage(qa_q3_game_combat_context *context, qa_q3_damage_participant target,
    const qa_q3_damage_participant *inflictor, const qa_q3_use_participant *attacker,
    qa_q3_damage_direction *direction, const qa_vec3 *point, double amount, double flags, double method_of_death);
bool qa_q3_combat_can_damage(qa_q3_game_combat_context *context, qa_q3_damage_participant target, qa_vec3 origin);
bool qa_q3_combat_radius_damage(qa_q3_game_combat_context *context, qa_vec3 origin, qa_q3_damage_participant attacker,
    double amount, double radius, const qa_q3_damage_participant *ignore, double method_of_death);
#endif
