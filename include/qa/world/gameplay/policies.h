/*
 * Port of src/world/gameplay/policies.ts: Q1/Q2/Q3 combat policies.
 * TS continuation closures become heap fn+ctx states that free
 * themselves when resumed (each resume runs at most once, driven by
 * the authority loop); mutation arrays in progress values are heap
 * copies retained by the progress. Math.fround / Math.imul staging
 * reproduced exactly. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_POLICIES_H
#define QA_WORLD_GAMEPLAY_POLICIES_H

#include <stdbool.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/world/gameplay/armor.h"

/** TS Q1CombatContext. arithmetic: "binary32" | "binary64". */
typedef struct qa_q1_combat_context {
  const char *arithmetic;
  bool quad;
  double teamplay;
  bool has_base_team_health;
  bool base_team_health;
  bool walk;
  bool has_momentum_direction;
  qa_vec3 momentum_direction;
} qa_q1_combat_context;

typedef struct qa_q1_lethal_health {
  bool has_value;
  double health;
  const char *reaction; /* "none" | "death" */
} qa_q1_lethal_health;

/** TS Q1DamageSourceEffects. Optional members may be NULL. */
typedef struct qa_q1_damage_source_effects {
  void *ctx;
  /* before/after quad return a preparation; has_value=false = TS undefined. */
  bool (*before_quad)(void *ctx, qa_damage_request request, double damage,
                      qa_combat_state target, const qa_combat_state *attacker,
                      qa_damage_preparation *out);
  bool (*after_quad)(void *ctx, qa_damage_request request, double damage,
                     qa_combat_state target, const qa_combat_state *attacker,
                     qa_damage_preparation *out);
  bool (*armor_allowed)(void *ctx, qa_damage_request request, double damage,
                        qa_combat_state target, const qa_combat_state *attacker);
  bool (*protection_applies)(void *ctx, qa_damage_request request, qa_combat_state target,
                             const qa_combat_state *attacker);
  bool (*before_health)(void *ctx, qa_damage_request request, double damage,
                        qa_combat_state target, const qa_combat_state *attacker);
  double (*after_armor)(void *ctx, qa_damage_request request, double take,
                        qa_combat_state target, const qa_combat_state *attacker);
  qa_q1_lethal_health (*lethal_health)(void *ctx, qa_damage_request request, double health,
                                        qa_combat_state target, const qa_combat_state *attacker);
} qa_q1_damage_source_effects;

/** TS Q1CombatPolicyOptions. */
typedef struct qa_q1_combat_policy_options {
  qa_provider_id id;
  void *ctx;
  qa_q1_combat_context (*context)(void *ctx, qa_damage_request request, qa_combat_state target,
                                  const qa_combat_state *attacker);
  qa_victim_armor_policy armor;
  bool has_source_effects;
  qa_q1_damage_source_effects source_effects;
} qa_q1_combat_policy_options;
qa_combat_policy qa_create_q1_combat_policy(qa_q1_combat_policy_options options);

/** TS Q2CombatContext. */
typedef struct qa_q2_combat_context {
  const char *arithmetic;
  bool player, monster, attacker_player, has_enemy, easy_skill, deathmatch;
  bool defender_sphere, team_damage_enabled, friendly_fire, nuke;
  bool no_knockback, movable, reject_team_damage, suppress_pain;
} qa_q2_combat_context;

/** TS Q2DamageSourceEffects. Optional members may be NULL. */
typedef struct qa_q2_damage_source_effects {
  void *ctx;
  double (*before_momentum)(void *ctx, qa_damage_request request, double damage,
                            qa_combat_state target, const qa_combat_state *attacker);
  bool (*power_armor_allowed)(void *ctx, qa_damage_request request, qa_combat_state target,
                              const qa_combat_state *attacker);
  double (*after_power_armor)(void *ctx, qa_damage_request request, double take,
                              qa_combat_state target, const qa_combat_state *attacker);
  bool (*armor_allowed)(void *ctx, qa_damage_request request, qa_combat_state target,
                        const qa_combat_state *attacker);
  double (*after_armor)(void *ctx, qa_damage_request request, double take,
                        qa_combat_state target, const qa_combat_state *attacker);
  void (*after_health)(void *ctx, qa_damage_decision decision, qa_current_combat_state current);
} qa_q2_damage_source_effects;

/** TS Q2CombatPolicyOptions. */
typedef struct qa_q2_combat_policy_options {
  qa_provider_id id;
  void *ctx;
  qa_q2_combat_context (*context)(void *ctx, qa_damage_request request, qa_combat_state target,
                                  const qa_combat_state *attacker);
  qa_victim_armor_policy armor;
  bool has_source_effects;
  qa_q2_damage_source_effects source_effects;
} qa_q2_combat_policy_options;
qa_combat_policy qa_create_q2_combat_policy(qa_q2_combat_policy_options options);

/** TS Q3CombatContext. product: "baseq3" | "missionpack". */
typedef struct qa_q3_combat_context {
  bool player, attacker_player;
  double attacker_max_health;
  bool attacker_guard, intermission, noclip, missionpack_invulnerability;
  bool no_knockback;
  double knockback_scale;
  bool friendly_fire, battlesuit, falling, juiced, proximity_protected;
  const char *product;
} qa_q3_combat_context;

/** TS PolicyOptions<Q3CombatContext>. */
typedef struct qa_q3_combat_policy_options {
  qa_provider_id id;
  void *ctx;
  qa_q3_combat_context (*context)(void *ctx, qa_damage_request request, qa_combat_state target,
                                  const qa_combat_state *attacker);
  qa_victim_armor_policy armor;
} qa_q3_combat_policy_options;
qa_combat_policy qa_create_q3_combat_policy(qa_q3_combat_policy_options options);

qa_victim_armor_policy qa_native_victim_armor(
    qa_victim_armor_context (*context)(void *ctx, qa_damage_request request), void *ctx);

#endif
