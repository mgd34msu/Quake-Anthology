/*
 * Port of src/world/gameplay/authority.ts: the gameplay (combat)
 * authority. TS frozen decisions/requests become values whose
 * pointer fields the authority retargets onto retained storage;
 * mutation arrays in returned outcomes are authority-owned and stay
 * valid until destroy. Protection bindings use per-channel fn ptrs
 * (the unused channel's ptrs are NULL). TS Error throws abort
 * identically; RangeError uses qa_range_error. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_AUTHORITY_H
#define QA_WORLD_GAMEPLAY_AUTHORITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/original_pickups.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/mod_composition.h"
#include "qa/world/gameplay/original_pickups.h"

/** TS CombatTraits = Pick<CombatState, ...>. noKnockback optional. */
typedef struct qa_combat_traits {
  bool can_take_damage;
  double mass;
  bool invulnerable;
  const char *team; /* nullable */
  bool has_no_knockback;
  bool no_knockback;
} qa_combat_traits;
/* TS Partial<CombatTraits> for setTraits. */
typedef struct qa_combat_trait_changes {
  bool has_can_take_damage; bool can_take_damage;
  bool has_mass; double mass;
  bool has_invulnerable; bool invulnerable;
  bool has_team; const char *team;
  bool has_no_knockback; bool no_knockback;
} qa_combat_trait_changes;

/** TS PowerArmorCellBinding. */
typedef struct qa_power_armor_cell_binding {
  void *ctx;
  double (*read)(void *ctx);
  void (*write)(void *ctx, double count);
} qa_power_armor_cell_binding;

/** TS SourceDamageResult = Pick<DamageDecision, ...>. */
typedef struct qa_source_damage_result {
  double applied_damage;
  const char *reaction; /* "none" | "pain" | "death" */
} qa_source_damage_result;

/** TS SourceDamageObserver. stored()'s write excludes impulses. */
typedef struct qa_source_damage_observer {
  void *ctx;
  void (*stored)(void *ctx, qa_damage_mutation write);
  void (*before_reaction)(void *ctx, qa_source_damage_result result);
} qa_source_damage_observer;

/* TS ProtectionAdmission: "claim" | "replace-primary"(owner) |
 * "replace-current-primary". */
typedef struct qa_protection_admission {
  const char *kind;
  qa_provider_id owner; /* replace-primary */
} qa_protection_admission;

/** TS ProtectionClaim. */
typedef struct qa_protection_claim {
  qa_provider_id owner;
  const char *rule;
  qa_protection_admission admission;
} qa_protection_claim;

/* Remove closure returned by SourceArmorStage.bind / reservations. */
typedef struct qa_armor_stage_removal {
  void *ctx;
  void (*remove)(void *ctx); /* NULL = TS null (no stage) */
} qa_armor_stage_removal;

/* TS armor-stage intercept: original() runs the source formula. */
typedef double (*qa_armor_stage_original_fn)(void *ctx);
typedef double (*qa_armor_stage_intercept_fn)(void *ctx, qa_armor_stage_input input,
                                              qa_armor_stage_original_fn original,
                                              void *original_ctx);

/** TS SourceArmorStage. bind returns the remove closure. */
typedef struct qa_source_armor_stage {
  void *ctx;
  qa_armor_stage_removal (*bind)(void *ctx, qa_armor_stage_intercept_fn intercept,
                                 void *intercept_ctx);
} qa_source_armor_stage;

/** TS ProtectionBinding<K>: one struct, channel selects the fns. */
typedef struct qa_protection_binding {
  void *ctx;
  qa_protection_claim claim; /* owner/rule/admission */
  qa_protection_channel channel;
  const qa_item_id *inventory_items;
  size_t inventory_item_count;
  const qa_original_pickup_rule *pickups; /* optional */
  size_t pickup_count;
  qa_regular_armor_state (*read_regular)(void *ctx);
  qa_powered_protection_state (*read_powered)(void *ctx);
  void (*validate_write_regular)(void *ctx, qa_regular_armor_state next);
  void (*validate_write_powered)(void *ctx, qa_powered_protection_state next);
  void (*write_regular)(void *ctx, qa_regular_armor_state next);
  void (*write_powered)(void *ctx, qa_powered_protection_state next);
  qa_armor_stage_result (*absorb)(void *ctx, qa_armor_stage_input input,
                                  qa_protection_observer observer);
} qa_protection_binding;

/** TS ProtectionReservation<K>. */
typedef struct qa_protection_reservation {
  qa_protection_channel channel;
  void *ctx; /* authority-owned */
  qa_armor_stage_removal (*bind)(void *ctx, qa_protection_binding binding);
  void (*close)(void *ctx);
} qa_protection_reservation;

/* TS CombatStateBinding.protection[channel]: owner may be declared
 * null (has_owner + owner_is_null); stage optional. */
typedef struct qa_combat_protection_declaration {
  bool declared;
  bool owner_is_null;
  qa_provider_id owner;
  bool has_stage;
  qa_source_armor_stage stage;
} qa_combat_protection_declaration;

typedef struct qa_damage_adjustment { double amount; double knockback; } qa_damage_adjustment;
typedef struct qa_damage_adjustment_result {
  bool has_value;
  qa_damage_adjustment value;
} qa_damage_adjustment_result;

/** TS CombatStateBinding. Optional methods may be NULL. */
typedef struct qa_combat_state_binding {
  void *ctx;
  qa_damage_outcome (*source_damage)(void *ctx, qa_damage_request request);
  qa_combat_protection_declaration protection_regular;
  qa_combat_protection_declaration protection_powered;
  const char *(*admit_damage)(void *ctx, qa_damage_request request); /* "continue"|"handled" */
  qa_damage_adjustment_result (*adjust_damage)(void *ctx, qa_damage_request request);
  qa_combat_state (*read)(void *ctx);
  void (*write_health)(void *ctx, double health);
  void (*write_armor)(void *ctx, qa_armor_state armor);
  qa_regular_armor_state (*empty_regular_armor)(void *ctx, double points);
  void (*validate_armor)(void *ctx, qa_armor_state armor);
  qa_armor_state (*normalize_legacy_armor)(void *ctx, qa_armor_state armor);
  void (*write_traits)(void *ctx, qa_combat_traits traits);
} qa_combat_state_binding;

/** TS GameplayHooks. team / damage_allowed may be NULL. */
typedef struct qa_gameplay_hooks {
  void *ctx;
  const char *(*team)(void *ctx, qa_actor_id actor, const char *original);
  bool (*damage_allowed)(void *ctx, qa_damage_request request);
  void (*impulse)(void *ctx, qa_owned_actor actor, qa_vec3 impulse, qa_provider_id movement);
  void (*before_reaction)(void *ctx, qa_owned_actor actor, qa_damage_decision decision);
  void (*confirmed)(void *ctx, qa_damage_outcome outcome);
} qa_gameplay_hooks;

qa_armor_state qa_copy_armor(qa_armor_state armor);
qa_combat_state qa_copy_combat(qa_combat_state state);
qa_damage_request qa_capture_request(qa_damage_request request);
qa_armor_state qa_normalize_legacy_power_only_armor(qa_armor_state legacy,
                                                    qa_armor_state current,
                                                    qa_item_id placeholder);

typedef struct qa_gameplay_authority qa_gameplay_authority;
/* NULL only on OOM. Borrowed deps. */
qa_gameplay_authority *qa_gameplay_authority_create(qa_session_actor_registry *actors,
                                                    qa_actor_callback_table *callbacks,
                                                    qa_gameplay_hooks hooks);
void qa_gameplay_authority_destroy(qa_gameplay_authority *authority);
qa_mod_operation *qa_gameplay_authority_damage_operation(qa_gameplay_authority *authority);
/* Returns an unregister id (TS returned closure). */
int64_t qa_gameplay_authority_register(qa_gameplay_authority *authority, qa_combat_policy policy);
void qa_gameplay_authority_unregister(qa_gameplay_authority *authority, int64_t registration);
void qa_gameplay_authority_bind(qa_gameplay_authority *authority, qa_owned_actor actor,
                                qa_combat_state_binding binding);
void qa_gameplay_authority_rebind(qa_gameplay_authority *authority, qa_owned_actor actor,
                                  qa_combat_state_binding binding);
void qa_gameplay_authority_bind_damage_adjustment(
    qa_gameplay_authority *authority, qa_owned_actor actor,
    qa_damage_adjustment_result (*adjust_damage)(void *ctx, qa_damage_request request),
    void *adjust_ctx);
void qa_gameplay_authority_bind_damage_admission(
    qa_gameplay_authority *authority, qa_owned_actor actor,
    const char *(*admit_damage)(void *ctx, qa_damage_request request), void *admit_ctx);
void qa_gameplay_authority_bind_power_armor_cells(qa_gameplay_authority *authority,
                                                  qa_owned_actor actor,
                                                  qa_power_armor_cell_binding cells);
qa_protection_reservation qa_gameplay_authority_reserve_protection(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_protection_channel channel,
    qa_protection_claim claim);
qa_armor_stage_removal qa_gameplay_authority_bind_protection(qa_gameplay_authority *authority,
                                                             qa_owned_actor actor,
                                                             qa_protection_binding binding);
bool qa_gameplay_authority_protection_owner(qa_gameplay_authority *authority,
                                            qa_owned_actor actor, qa_protection_channel channel,
                                            qa_provider_id *out);
const qa_item_id *qa_gameplay_authority_protection_inventory_items(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_protection_channel channel,
    size_t *out_count);
qa_original_pickup_resolution qa_gameplay_authority_resolve_pickup(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_original_pickup_offer offer);
/* TS withPickupProtection<T>: operation returns an opaque result. */
void *qa_gameplay_authority_with_pickup_protection(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_provider_id owner,
    void *(*operation)(void *ctx, qa_protection_observer observer), void *operation_ctx);
bool qa_gameplay_authority_copied_primary_armor(qa_gameplay_authority *authority,
                                                qa_owned_actor actor, qa_armor_state *out);
void qa_gameplay_authority_assert_idle(qa_gameplay_authority *authority);
/* admit_damage may be NULL. */
void qa_gameplay_authority_create_state(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_combat_state initial,
    const char *(*admit_damage)(void *ctx, qa_damage_request request), void *admit_ctx);
qa_combat_state_result qa_gameplay_authority_read(qa_gameplay_authority *authority,
                                                  qa_actor_id actor);
void qa_gameplay_authority_set_health(qa_gameplay_authority *authority, qa_owned_actor actor,
                                      double health);
void qa_gameplay_authority_set_armor(qa_gameplay_authority *authority, qa_owned_actor actor,
                                     qa_armor_state armor);
qa_armor_state qa_gameplay_authority_normalize_legacy_armor(qa_gameplay_authority *authority,
                                                            qa_owned_actor actor,
                                                            qa_armor_state armor);
/* initial may be NULL. */
void qa_gameplay_authority_set_regular_points(qa_gameplay_authority *authority,
                                              qa_owned_actor actor, double points,
                                              const qa_regular_armor_state *initial);
void qa_gameplay_authority_set_regular_armor(qa_gameplay_authority *authority,
                                             qa_owned_actor actor, qa_regular_armor_state regular);
void qa_gameplay_authority_set_powered_protection(qa_gameplay_authority *authority,
                                                  qa_owned_actor actor,
                                                  qa_powered_protection_state powered);
void qa_gameplay_authority_set_traits(qa_gameplay_authority *authority, qa_owned_actor actor,
                                      qa_combat_trait_changes changes);
qa_damage_outcome qa_gameplay_authority_run_source_damage(
    qa_gameplay_authority *authority, qa_damage_request input,
    qa_source_damage_result (*execute)(void *ctx, qa_source_damage_observer observer,
                                       qa_damage_request request),
    void *execute_ctx);
void qa_gameplay_authority_source_reaction(qa_gameplay_authority *authority,
                                           qa_damage_request input,
                                           qa_source_damage_result result);
/* source_damage may be NULL. */
qa_damage_outcome qa_gameplay_authority_apply(
    qa_gameplay_authority *authority, qa_damage_request input,
    qa_damage_outcome (*source_damage)(void *ctx, qa_damage_request request),
    void *source_damage_ctx);
qa_damage_authority qa_gameplay_authority_as_contract(qa_gameplay_authority *authority);

#endif
