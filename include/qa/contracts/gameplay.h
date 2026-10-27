/*
 * Port of src/contracts/gameplay.ts (types only): combat, inventory,
 * and transition contract shapes. Method interfaces become function
 * tables + ctx; discriminated unions become kind enum + fields.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_GAMEPLAY_H
#define QA_CONTRACTS_GAMEPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/time.h"

typedef const char *qa_item_id;      /* `${string}:${string}` */
typedef const char *qa_objective_id; /* `${string}:${string}` */

typedef struct qa_active_powerup_timer {
  qa_item_id item;
  const char *label;
  double remaining_seconds;
} qa_active_powerup_timer;

typedef struct qa_arsenal_intent {
  qa_provider_id provider;
  qa_item_id weapon; /* NULL retains the current selection */
  bool use_holdable;
  bool has_impulse;
  double impulse;
} qa_arsenal_intent;

/* TS Q2NativeCause union on edition: "classic" (game
 * "base"|"xatrix"|"rogue"|"ctf", value) | "rerelease" (id, flags). */
typedef struct qa_q2_native_cause {
  const char *edition;
  const char *game;
  double value;
  double id;
  bool friendly_fire;
  bool no_point_loss;
} qa_q2_native_cause;

/** TS SourceDamageModifier: function table + ctx. */
typedef struct qa_source_damage_modifier {
  void *ctx;
  qa_provider_id owner;
  double (*transform)(void *ctx, const qa_actor_id *attacker, double amount);
} qa_source_damage_modifier;

/* TS AttackProvenance cause union on kind: "q1" (deathType, optional
 * armorEffect "bypass"|"half-effectiveness") | "q2" (meansOfDeath,
 * damageFlags, optional native) | "q3" (meansOfDeath, damageFlags) |
 * "environment" (hazard "fall"|"drown"|"lava"|"slime"|"crush"|"trigger"). */
typedef enum qa_attack_cause_kind {
  QA_ATTACK_CAUSE_Q1, QA_ATTACK_CAUSE_Q2, QA_ATTACK_CAUSE_Q3, QA_ATTACK_CAUSE_ENVIRONMENT
} qa_attack_cause_kind;
typedef struct qa_attack_cause {
  qa_attack_cause_kind kind;
  const char *death_type;
  const char *armor_effect; /* nullable */
  double means_of_death;
  double damage_flags;
  bool has_native;
  qa_q2_native_cause native;
  const char *hazard;
} qa_attack_cause;

typedef struct qa_attack_provenance {
  double sequence;
  qa_source_time time;
  const qa_actor_id *attacker;   /* nullable */
  const qa_actor_id *inflictor;  /* nullable */
  bool has_originating_projectile;
  qa_actor_id originating_projectile;
  qa_item_id weapon; /* nullable */
  qa_provider_id weapon_provider;
  bool has_damage_powerup_owner;
  qa_provider_id damage_powerup_owner;
  qa_provider_id combat_provider;
  qa_provider_id inventory_provider;
  qa_provider_id movement_provider;
  qa_attack_cause cause;
} qa_attack_provenance;

typedef struct qa_damage_request {
  qa_attack_provenance attack;
  qa_actor_id target;
  double amount;
  double knockback;
  qa_vec3 direction;
  qa_vec3 point;
  qa_vec3 normal;
  const char *delivery; /* "direct" | "radius" */
} qa_damage_request;

/* TS RegularArmorState union on kind: "none"|"q1"|"q2"|"q3"|"source". */
typedef enum qa_regular_armor_kind {
  QA_REGULAR_ARMOR_NONE, QA_REGULAR_ARMOR_Q1, QA_REGULAR_ARMOR_Q2,
  QA_REGULAR_ARMOR_Q3, QA_REGULAR_ARMOR_SOURCE
} qa_regular_armor_kind;
typedef struct qa_regular_armor_state {
  qa_regular_armor_kind kind;
  double points;
  double absorption;         /* q1 */
  double normal_protection;  /* q2 */
  double energy_protection;  /* q2 */
  double protection;         /* q3 */
  qa_item_id item;           /* q1/q2; source: nullable */
} qa_regular_armor_state;

/* TS PoweredProtectionState: {kind:"none"} | {kind:"screen"|"shield", cells}. */
typedef struct qa_powered_protection_state {
  const char *kind;
  double cells;
} qa_powered_protection_state;

typedef struct qa_armor_state {
  qa_regular_armor_state regular;
  qa_powered_protection_state powered;
} qa_armor_state;

/* TS ProtectionChannel = keyof ArmorState: "regular" | "powered". */
typedef enum qa_protection_channel {
  QA_PROTECTION_REGULAR, QA_PROTECTION_POWERED
} qa_protection_channel;

typedef struct qa_armor_damage_flags {
  bool has_stage;
  const char *stage; /* "power" | "regular" */
  bool no_armor;
  bool no_power_armor;
  bool no_regular_armor;
  bool energy;
  bool has_regular_protection_scale;
  double regular_protection_scale;
} qa_armor_damage_flags;

typedef struct qa_armor_result {
  qa_armor_state armor;
  double power_saved;
  double regular_saved;
} qa_armor_result;

typedef struct qa_armor_stage_input {
  qa_damage_request request;
  qa_vec3 direction; /* geometry: Pick<DamageRequest, ...> */
  qa_vec3 point;
  qa_vec3 normal;
  double amount;
  qa_armor_damage_flags flags;
} qa_armor_stage_input;

typedef struct qa_armor_stage_result { double saved; } qa_armor_stage_result;

/* TS ProtectionStore: at least one channel change is present. */
typedef struct qa_protection_store {
  bool has_regular;
  qa_regular_armor_state regular_before, regular_after;
  bool has_powered;
  qa_powered_protection_state powered_before, powered_after;
} qa_protection_store;

/** TS ProtectionObserver. */
typedef struct qa_protection_observer {
  void *ctx;
  void (*stored)(void *ctx, qa_protection_store change);
} qa_protection_observer;

typedef struct qa_combat_state {
  double health;
  qa_armor_state armor;
  double mass;
  bool can_take_damage;
  bool invulnerable;
  bool has_no_knockback;
  bool no_knockback;
  const char *team; /* nullable */
} qa_combat_state;

/* TS DamageMutation union on kind:
 * "health"|"armor"|"source-velocity"|"impulse". */
typedef enum qa_damage_mutation_kind {
  QA_DAMAGE_MUTATION_HEALTH, QA_DAMAGE_MUTATION_ARMOR,
  QA_DAMAGE_MUTATION_SOURCE_VELOCITY, QA_DAMAGE_MUTATION_IMPULSE
} qa_damage_mutation_kind;
typedef struct qa_damage_mutation {
  qa_damage_mutation_kind kind;
  double health_before, health_after;
  qa_armor_state armor_before, armor_after;
  qa_vec3 velocity_before, velocity_after;
  qa_vec3 impulse;
  qa_provider_id movement_provider;
} qa_damage_mutation;

/* TS DamageDecision feedback union on kind: "q2" | "q3". */
typedef struct qa_damage_decision {
  qa_damage_request request;
  const qa_damage_mutation *mutations;
  size_t mutation_count;
  double applied_damage;
  const char *reaction; /* "none" | "pain" | "death" */
  bool has_feedback;
  const char *feedback_kind; /* "q2" | "q3" */
  double feedback_power_armor, feedback_armor, feedback_blood, feedback_knockback;
  bool feedback_battlesuit;
} qa_damage_decision;

/** TS CurrentCombatState: nullable state lookups via fn ptr + ctx. */
typedef struct qa_combat_state_result { bool has_value; qa_combat_state value; } qa_combat_state_result;
typedef struct qa_current_combat_state {
  void *ctx;
  qa_combat_state_result (*target)(void *ctx);
  qa_combat_state_result (*attacker)(void *ctx);
} qa_current_combat_state;

/* TS CombatResult = Pick<DamageDecision, "appliedDamage"|"reaction"|"feedback">. */
typedef struct qa_combat_result {
  double applied_damage;
  const char *reaction;
  bool has_feedback;
  const char *feedback_kind;
  double feedback_power_armor, feedback_armor, feedback_blood, feedback_knockback;
  bool feedback_battlesuit;
} qa_combat_result;

/* TS CombatProgress union on kind: "complete" | "armor-stage" |
 * "source-continuation". TS closures (fallback/resume) become fn ptr +
 * ctx; the recursive result is returned through an out parameter. */
typedef struct qa_combat_progress qa_combat_progress;
struct qa_combat_progress {
  const char *kind;
  qa_damage_request request;
  const qa_damage_mutation *mutations;
  size_t mutation_count;
  qa_combat_result result;               /* complete */
  qa_protection_channel channel;         /* armor-stage */
  qa_armor_stage_input input;            /* armor-stage */
  void *ctx;
  qa_armor_result (*fallback)(void *ctx, qa_armor_state current);
  void (*resume_armor)(void *ctx, qa_armor_stage_result result,
                       qa_current_combat_state current, qa_combat_progress *out);
  void (*resume_source)(void *ctx, qa_current_combat_state current, qa_combat_progress *out);
};

/* TS DamagePreparation: {kind:"continue",amount} | {kind:"cancel"}. */
typedef struct qa_damage_preparation {
  bool is_continue;
  double amount;
} qa_damage_preparation;

/** TS CombatPolicy. Optional TS methods may be NULL. */
typedef struct qa_combat_policy {
  void *ctx;
  qa_provider_id id;
  qa_damage_preparation (*prepare)(void *ctx, qa_damage_request request,
                                   qa_combat_state target, const qa_combat_state *attacker);
  void (*decide)(void *ctx, qa_damage_request request, qa_combat_state target,
                 const qa_combat_state *attacker, bool has_prepared_amount,
                 double prepared_amount, qa_combat_progress *out);
  const char *(*after_health)(void *ctx, qa_damage_decision decision,
                              qa_current_combat_state current); /* reaction string */
} qa_combat_policy;

/* TS DamageOutcome union on kind: "stale-target" | "committed". */
typedef struct qa_damage_outcome {
  const char *kind;
  qa_damage_request request;
  qa_damage_decision decision;
  bool survived;
} qa_damage_outcome;

/** TS DamageAuthority. */
typedef struct qa_damage_authority {
  void *ctx;
  qa_damage_outcome (*apply)(void *ctx, qa_damage_request request);
} qa_damage_authority;

/* TS InventoryCountPolicy: {kind:"stack"} | {kind:"source-counter",
 * arithmetic:"binary32"|"binary64"|"int32"}. */
typedef struct qa_inventory_count_policy {
  const char *kind;
  const char *arithmetic;
} qa_inventory_count_policy;

typedef struct qa_inventory_entry {
  qa_item_id item;
  double count;
  double capacity;
  bool has_count_policy;
  qa_inventory_count_policy count_policy;
} qa_inventory_entry;

/** TS InventoryTable. entries() returns pointer + count. */
typedef struct qa_inventory_table {
  void *ctx;
  const qa_inventory_entry *(*entries)(void *ctx, qa_actor_id actor, size_t *out_count);
  double (*count)(void *ctx, qa_actor_id actor, qa_item_id item);
  bool (*consume)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
  double (*give)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
} qa_inventory_table;

typedef struct qa_mission_gate {
  qa_objective_id objective;
  bool satisfied;
} qa_mission_gate;

/* TS TransitionIntent union on kind: "campaign-level"|"campaign-complete"|
 * "round-complete"|"match-rotation". Extract<> aliases
 * CampaignTransitionIntent / MatchTransitionIntent are the same struct. */
typedef struct qa_transition_intent {
  const char *kind;
  qa_provider_id campaign;
  qa_provider_id match;
  const char *map; /* `${string}:${string}` */
  const char *spawn_point;
  const qa_mission_gate *gates;
  size_t gate_count;
  const qa_actor_id *cause; /* nullable */
  const char *winner;       /* nullable */
} qa_transition_intent;
typedef qa_transition_intent qa_campaign_transition_intent;
typedef qa_transition_intent qa_match_transition_intent;

/* TS TransitionMode union on kind: "campaign"|"competitive"|"combined"
 * (combined: levelAuthority "campaign-gates", simultaneous
 * "campaign-first"|"round-first"). */
typedef struct qa_transition_mode {
  const char *kind;
  qa_provider_id campaign;
  qa_provider_id match;
  bool allow_round_restart;
  const char *simultaneous;
} qa_transition_mode;

/* TS TransitionDecision union on kind: "stay"|"round"|"travel"|"campaign-complete". */
typedef struct qa_transition_decision {
  const char *kind;
  const qa_objective_id *blocked;
  size_t blocked_count;
  const char *winner; /* nullable */
  const char *map;
  const char *spawn_point;
  bool complete_campaign;
  qa_provider_id campaign;
} qa_transition_decision;

/** TS CampaignController / MatchController / TransitionCoordinator. */
typedef struct qa_transition_intent_result {
  bool has_value;
  qa_transition_intent value;
} qa_transition_intent_result;
typedef struct qa_campaign_controller {
  void *ctx;
  qa_provider_id id;
  const qa_mission_gate *(*gates)(void *ctx, size_t *out_count);
  qa_transition_intent_result (*propose_transition)(void *ctx);
} qa_campaign_controller;
typedef struct qa_match_controller {
  void *ctx;
  qa_provider_id id;
  void (*confirmed_damage)(void *ctx, qa_damage_outcome outcome);
  qa_transition_intent_result (*propose_transition)(void *ctx);
} qa_match_controller;
typedef struct qa_transition_coordinator {
  void *ctx;
  qa_transition_decision (*resolve)(void *ctx, qa_transition_mode mode,
                                    const qa_transition_intent *intents, size_t intent_count);
  void (*commit)(void *ctx, qa_transition_decision decision);
} qa_transition_coordinator;

#endif
