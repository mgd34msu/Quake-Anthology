#ifndef QA_GAMEPLAY_H
#define QA_GAMEPLAY_H

#include "qa/actors.h"
#include "qa/math.h"

typedef uint32_t qa_item_id;
/* Session-interned identity; zero is unteamed. Original protocol team numbers
 * are translated by their adapters, never used as private shared-world IDs. */
typedef uint32_t qa_team_id;
typedef struct qa_operation qa_operation;
typedef struct qa_combat qa_combat;
typedef struct qa_inventory qa_inventory;
typedef struct qa_pickups qa_pickups;

typedef enum qa_game_family { QA_GAME_Q1, QA_GAME_Q2, QA_GAME_Q3 } qa_game_family;
typedef enum qa_hazard { QA_HAZARD_FALL, QA_HAZARD_DROWN, QA_HAZARD_LAVA, QA_HAZARD_SLIME, QA_HAZARD_CRUSH, QA_HAZARD_TRIGGER } qa_hazard;
typedef enum qa_cause_kind { QA_CAUSE_Q1, QA_CAUSE_Q2, QA_CAUSE_Q3, QA_CAUSE_ENVIRONMENT } qa_cause_kind;
typedef enum qa_q1_armor_effect { QA_Q1_ARMOR_NORMAL, QA_Q1_ARMOR_BYPASS, QA_Q1_ARMOR_HALF } qa_q1_armor_effect;
typedef enum qa_q2_native_edition { QA_Q2_CAUSE_NONE, QA_Q2_CAUSE_CLASSIC, QA_Q2_CAUSE_RERELEASE } qa_q2_native_edition;
typedef struct qa_damage_cause {
    qa_cause_kind kind;
    union {
        struct { uint32_t death_type; qa_q1_armor_effect armor; } q1;
        struct { int32_t means_of_death; uint32_t flags; qa_q2_native_edition native;
                 int32_t native_value; uint32_t classic_product; bool friendly_fire, no_point_loss; } q2;
        struct { int32_t means_of_death; uint32_t flags; } q3;
        qa_hazard hazard;
    } source;
} qa_damage_cause;
/* Zero registry denotes an absent actor; zero item denotes an absent weapon.
 * References may outlive actors. Providers/items are session-interned IDs. */
typedef struct qa_attack {
    uint64_t sequence, time_ns;
    qa_actor_id attacker, inflictor, projectile;
    qa_item_id weapon;
    qa_actor_owner weapon_provider, combat_provider, inventory_provider, movement_provider;
    bool powerup_applied;
    qa_actor_owner powerup_owner;
    qa_damage_cause cause;
} qa_attack;
typedef struct qa_damage_request {
    qa_attack attack;
    qa_actor_id target;
    float amount, knockback;
    qa_vec3 direction, point, normal;
    bool radius;
} qa_damage_request;
bool qa_damage_request_validate(const qa_damage_request *, qa_error *);
bool qa_attack_next(uint64_t *sequence, qa_attack *, qa_error *);
typedef struct qa_damage_modifier {
    qa_actor_owner owner;
    void *context;
    bool (*transform)(void *, qa_actor_id attacker, float amount, float *, qa_error *);
} qa_damage_modifier;
bool qa_damage_apply_modifier(const qa_actor_registry *, const qa_damage_request *, const qa_damage_modifier *, qa_damage_request *, qa_error *);

typedef enum qa_regular_armor_kind { QA_ARMOR_NONE, QA_ARMOR_Q1, QA_ARMOR_Q2, QA_ARMOR_Q3, QA_ARMOR_SOURCE } qa_regular_armor_kind;
/* Canonical source storage retains binary64 counts and authored Q2 rates.
 * Compiled family arithmetic applies its own explicit binary32 boundary. */
typedef struct qa_regular_armor {
    qa_regular_armor_kind kind;
    double points;
    qa_item_id item;
    union { float q1_absorption; struct { double normal, energy; } q2; float q3_protection; } protection;
} qa_regular_armor;
typedef enum qa_power_kind { QA_POWER_NONE, QA_POWER_SCREEN, QA_POWER_SHIELD } qa_power_kind;
typedef enum qa_q2_power_armor_edition {
    QA_Q2_POWER_ARMOR_NONE, QA_Q2_POWER_ARMOR_CLASSIC, QA_Q2_POWER_ARMOR_RERELEASE
} qa_q2_power_armor_edition;
typedef enum qa_power_armor_source {
    QA_POWER_SOURCE_Q2, QA_POWER_SOURCE_GENERIC
} qa_power_armor_source;
typedef struct qa_powered_armor {
    qa_power_kind kind;
    double cells;
    qa_actor_owner source_owner;
    qa_q2_power_armor_edition source_edition;
    /* Generic storage requires an actual owner-bound absorption lease.
     * Q2 storage retains its separate original edition qualification. */
    qa_power_armor_source source_kind;
} qa_powered_armor;
typedef struct qa_armor { qa_regular_armor regular; qa_powered_armor powered; } qa_armor;
typedef enum qa_protection_channel { QA_PROTECTION_REGULAR, QA_PROTECTION_POWERED } qa_protection_channel;
typedef struct qa_damage_flags {
    bool no_armor, no_power_armor, no_regular_armor, energy;
    bool no_knockback, no_protection, no_team_protection, destroy_armor;
    float regular_scale;
} qa_damage_flags;
typedef struct qa_armor_context { float screen_facing_dot; bool q2_profile, rerelease, ctf, alive; } qa_armor_context;
typedef struct qa_damage_geometry { qa_vec3 direction, point, normal; } qa_damage_geometry;
typedef struct qa_armor_result {
    qa_armor armor;
    float power_saved, regular_saved;
    /* Actual source power-armor effect boundary, including Classic zero saves. */
    bool power_activated;
} qa_armor_result;
qa_damage_flags qa_attack_flags(const qa_attack *);
bool qa_armor_validate(const qa_armor *, qa_error *);
bool qa_regular_armor_equal(qa_regular_armor, qa_regular_armor);
bool qa_powered_armor_equal(qa_powered_armor, qa_powered_armor);
bool qa_armor_equal(qa_armor, qa_armor);
bool qa_armor_absorb(const qa_armor *, float damage, qa_damage_flags,
                     const qa_armor_context *, const qa_protection_channel *stage,
                     qa_armor_result *, qa_error *);

typedef struct qa_combat_state {
    float health, mass;
    qa_armor armor;
    bool can_take_damage, invulnerable, no_knockback;
    qa_team_id team;
} qa_combat_state;
typedef enum qa_reaction { QA_REACTION_NONE, QA_REACTION_PAIN, QA_REACTION_DEATH } qa_reaction;
typedef enum qa_mutation_kind { QA_MUTATION_HEALTH, QA_MUTATION_ARMOR, QA_MUTATION_SOURCE_VELOCITY, QA_MUTATION_IMPULSE } qa_mutation_kind;
typedef struct qa_damage_mutation {
    qa_mutation_kind kind;
    union {
        struct { float before, after; } health;
        struct { qa_armor before, after; } armor;
        struct { qa_vec3 before, after; qa_actor_owner movement; } velocity;
        struct { qa_vec3 value; qa_actor_owner movement; } impulse;
    } value;
} qa_damage_mutation;
typedef struct qa_damage_result {
    float applied_damage;
    qa_reaction reaction;
    qa_game_family feedback_family;
    bool has_feedback, battlesuit;
    float power_saved, armor_saved, blood, knockback;
    /* Actual selected Q2 damage after source modifiers, before protection.
     * It remains distinct from blood and armor savings. */
    bool has_q2_damage;
    float q2_damage;
} qa_damage_result;
typedef struct qa_damage_inflictor_center {
    qa_actor_id inflictor;
    double center[3];
    bool present;
} qa_damage_inflictor_center;
typedef struct qa_damage_outcome {
    bool stale, survived;
    qa_damage_request request;
    qa_damage_result result;
    qa_damage_inflictor_center inflictor_center;
    qa_damage_mutation *mutations;
    size_t mutation_count;
} qa_damage_outcome;
/* Outcomes own their journal. Hooks borrow it only for the duration of a call. */
void qa_damage_outcome_free(qa_damage_outcome *);
typedef struct qa_damage_observer qa_damage_observer;
bool qa_damage_observe(qa_damage_observer *, const qa_damage_mutation *, qa_error *);
bool qa_damage_before_reaction(qa_damage_observer *, const qa_damage_result *, qa_error *);
typedef bool (*qa_source_damage_fn)(void *, qa_combat *, const qa_damage_request *, qa_damage_observer *, qa_damage_result *, qa_error *);
typedef bool (*qa_damage_admit_fn)(void *, const qa_damage_request *, bool *handled, qa_error *);
typedef struct qa_combat_admission {
    void *context;
    qa_damage_admit_fn admit;
} qa_combat_admission;

typedef struct qa_combat_binding {
    void *context;
    bool (*read)(void *, qa_combat_state *, qa_error *);
    bool (*write_health)(void *, float, qa_error *);
    bool (*write_armor)(void *, const qa_armor *, qa_error *);
    bool (*validate_armor)(void *, const qa_armor *, qa_error *);
    bool (*write_traits)(void *, const qa_combat_state *, qa_error *);
    bool (*empty_regular_armor)(void *, double points, qa_regular_armor *, bool *selected, qa_error *);
    bool (*normalize_legacy_armor)(void *, const qa_armor *, qa_armor *, qa_error *);
    qa_damage_admit_fn admit;
    bool (*adjust)(void *, const qa_damage_request *, float *amount, float *knockback, qa_error *);
    qa_source_damage_fn source_damage;
    bool source_armor_stages[2];
    bool has_primary_protection[2];
    qa_actor_owner primary_protection[2];
} qa_combat_binding;
typedef enum qa_damage_effect_stage {
    QA_DAMAGE_BEFORE_QUAD, QA_DAMAGE_AFTER_QUAD, QA_DAMAGE_ARMOR_ALLOWED,
    QA_DAMAGE_PROTECTION_APPLIES, QA_DAMAGE_BEFORE_HEALTH, QA_DAMAGE_AFTER_ARMOR,
    QA_DAMAGE_LETHAL_HEALTH, QA_DAMAGE_BEFORE_MOMENTUM, QA_DAMAGE_POWER_ALLOWED,
    QA_DAMAGE_AFTER_POWER, QA_DAMAGE_AFTER_HEALTH
} qa_damage_effect_stage;
/* *_ALLOWED and PROTECTION_APPLIES use allowed as their gate. BEFORE_HEALTH
 * receives full pre-armor damage for reflection and may veto health loss;
 * changing its amount does not repeat absorption. AFTER_POWER/AFTER_ARMOR
 * transform the remaining amount. Each family retains its source ordering. */
typedef struct qa_damage_effect { float amount; bool allowed; qa_reaction reaction; } qa_damage_effect;
typedef enum qa_damage_feedback_stage {
    QA_DAMAGE_FEEDBACK_PROTECTION, QA_DAMAGE_FEEDBACK_POWER,
    QA_DAMAGE_FEEDBACK_ARMOR, QA_DAMAGE_FEEDBACK_HEALTH
} qa_damage_feedback_stage;
typedef struct qa_damage_feedback {
    qa_damage_feedback_stage stage;
    float blood, power_saved, armor_saved;
    qa_powered_armor powered;
} qa_damage_feedback;
typedef bool (*qa_source_reaction_body)(void *, qa_error *);
typedef struct qa_source_reaction_observer qa_source_reaction_observer;
typedef bool (*qa_source_reaction_executor)(void *, qa_source_reaction_observer *, qa_error *);
typedef struct qa_combat_hooks {
    void *context;
    /* Read the actual inflictor body before damage callbacks can retire it.
     * Absent bodies leave found=false; impact points are not body centers. */
    bool (*inflictor_center)(void *, const qa_damage_request *, double center[3],
                              bool *found, qa_error *);
    qa_team_id (*team)(void *, qa_actor_id, qa_team_id);
    bool (*damage_allowed)(void *, const qa_damage_request *);
    bool (*impulse)(void *, qa_actor_id, qa_vec3, qa_actor_owner, qa_error *);
    bool (*before_reaction)(void *, const qa_damage_outcome *, qa_error *);
    bool (*reaction)(void *, const qa_damage_outcome *, qa_error *);
    bool (*confirmed)(void *, const qa_damage_outcome *, qa_error *);
    /* Dispatch the selected character at the actual source callback boundary.
     * The original body is borrowed only for this synchronous call. */
    bool (*source_reaction)(void *, const qa_damage_outcome *, qa_actor_owner,
                            qa_source_reaction_body, void *, qa_error *);
    /* Deferred completion reports reaction/scoring only. Health damage effects
     * were already confirmed by the original source damage requests. */
    bool (*source_reaction_confirmed)(void *, const qa_damage_outcome *, qa_error *);
    /* OR ordinary timed invulnerability with primary godmode. Team Arena's
     * bubble uses request-aware damage_allowed instead (juiced bypass).
     * Read-only; each provider keeps its own expiry and checkpoint state. */
    bool (*invulnerable)(void *, qa_actor_id);
    /* Attached effects compose independently of the selected combat policy.
     * The shared hook follows that policy's local effect once per stage. */
    bool (*effect)(void *, qa_combat *, qa_damage_effect_stage,
                    const qa_damage_request *, qa_damage_effect *, qa_error *);
    bool (*armor_context)(void *, const qa_damage_request *, const qa_combat_state *,
                            const qa_damage_geometry *, qa_armor_context *, qa_error *);
} qa_combat_hooks;
typedef enum qa_protection_admission { QA_PROTECTION_CLAIM, QA_PROTECTION_REPLACE_PRIMARY, QA_PROTECTION_REPLACE_CURRENT } qa_protection_admission;
typedef struct qa_protection_claim {
    qa_actor_owner owner, expected_owner;
    uint32_t rule;
    qa_protection_admission admission;
} qa_protection_claim;
typedef struct qa_protection_lease { qa_actor_id actor; uint64_t serial; qa_protection_channel channel; } qa_protection_lease;
typedef struct qa_protection_store { bool regular, powered; qa_armor before, after; } qa_protection_store;
typedef struct qa_protection_observer qa_protection_observer;
bool qa_protection_observe(qa_protection_observer *, const qa_protection_store *, qa_error *);
typedef struct qa_protection_binding {
    void *context;
    bool (*read)(void *, qa_armor *, qa_error *);
    bool (*validate_write)(void *, const qa_armor *, qa_error *);
    bool (*write)(void *, const qa_armor *, qa_error *);
    bool (*absorb)(void *, const qa_damage_request *, const qa_damage_geometry *, float, qa_damage_flags, qa_protection_observer *, float *saved, qa_error *);
} qa_protection_binding;

typedef struct qa_q1_combat_context { bool quad, walk, has_momentum_direction, skip_base_team_health; int32_t teamplay; qa_vec3 momentum_direction; } qa_q1_combat_context;
typedef struct qa_q2_combat_context {
    bool player, monster, attacker_player, has_enemy, easy_skill, deathmatch, rerelease;
    bool defender_sphere, team_damage_enabled, friendly_fire, nuke, no_knockback;
    bool movable, reject_team_damage, suppress_pain, team_armor_protect, reject_friendly_damage;
    int32_t damage_scale;
} qa_q2_combat_context;
typedef struct qa_q3_combat_context {
    bool player, attacker_player, attacker_guard, intermission, noclip;
    bool missionpack_invulnerability, no_knockback, friendly_fire, battlesuit;
    bool falling, juiced, proximity_protected, missionpack;
    int32_t attacker_max_health;
    float knockback_scale;
} qa_q3_combat_context;
typedef struct qa_combat_context {
    qa_armor_context armor;
    union { qa_q1_combat_context q1; qa_q2_combat_context q2; qa_q3_combat_context q3; } game;
} qa_combat_context;
typedef struct qa_combat_policy {
    qa_actor_owner provider;
    qa_game_family family;
    void *context;
    /* Runs once on the actual canonical request before its damage cursor.
     * Original source executors retain their own preparation. */
    bool (*prepare)(void *, qa_damage_request *, bool *allowed, qa_error *);
    bool (*describe)(void *, const qa_damage_request *, const qa_combat_state *, const qa_combat_state *, qa_combat_context *, qa_error *);
    /* Effects can reenter combat. Each subsequent stage rereads authority.
     * LETHAL_HEALTH uses amount as proposed health, not damage. */
    bool (*effect)(void *, qa_combat *, qa_damage_effect_stage, const qa_damage_request *, qa_damage_effect *, qa_error *);
    /* Synchronous source presentation at the policy's actual protection,
     * armor and pre-health sites. Original source executors emit their own
     * events. The callback may reenter; its target is reread before continuing. */
    bool (*feedback)(void *, qa_combat *, const qa_damage_request *,
                     const qa_damage_feedback *, qa_error *);
} qa_combat_policy;

/* One thread owns these services; actors are borrowed. The session forwards
 * every registry release before retiring provider callback contexts. Hooks and
 * bindings can reenter through public services, but cannot destroy an active
 * service. Read/validation callbacks must not mutate the state they expose. */
bool qa_combat_create(qa_actor_registry *, const qa_combat_hooks *, qa_combat **, qa_error *);
bool qa_combat_destroy(qa_combat *, qa_error *);
bool qa_combat_idle(const qa_combat *);
void qa_combat_actor_released(qa_combat *, qa_actor_record);
bool qa_combat_register_policy(qa_combat *, const qa_combat_policy *, qa_error *);
/* Pure published descriptor query; absence leaves the output unchanged. */
bool qa_combat_policy_family(const qa_combat *, qa_actor_owner, qa_game_family *);
typedef struct qa_combat_policy_admission qa_combat_policy_admission;
/* Prepare reserves capacity without publishing a policy. Remove an existing
 * owner before replacement commit. Commit allocates/calls nothing and consumes
 * success; abort consumes a pending token. Combat must outlive its tokens. */
bool qa_combat_prepare_policy(qa_combat *, const qa_combat_policy *, bool replace_owner,
                               qa_combat_policy_admission **, qa_error *);
bool qa_combat_policy_admission_validate(qa_combat_policy_admission *, qa_error *);
bool qa_combat_policy_admission_commit(qa_combat_policy_admission *, qa_error *);
void qa_combat_policy_admission_abort(qa_combat_policy_admission *);
bool qa_combat_unregister_policy(qa_combat *, qa_actor_owner, qa_error *);
bool qa_combat_create_actor(qa_combat *, qa_actor_id, const qa_combat_state *, qa_error *);
/* Optional source admission for locally stored combat state, before a selected
 * policy changes health/armor. handled consumes the hit with no damage reaction.
 * NULL clears the callback. Cannot change it during this actor's admission or
 * damage callback, or attach it to external storage (use binding.admit there).
 * Context lives until explicit clear or actor retirement. Provider restoration
 * rebinds it; shared state checkpoints contain no callback pointers. Explicit
 * qa_combat_run_source executors own their admission, as external executors do. */
bool qa_combat_set_admission(qa_combat *, qa_actor_id, const qa_combat_admission *, qa_error *);
uint64_t qa_combat_storage_serial(const qa_combat *, qa_actor_id);
bool qa_combat_bind(qa_combat *, qa_actor_id, const qa_combat_binding *, bool replace, qa_error *);
/* Pure published-primary identity query. Detach requires idle combat and
 * snapshots original primary traits/armor and its authoritative fuel before
 * releasing callbacks. Component protection leases and fuel identity remain. */
bool qa_combat_primary_current(const qa_combat *, qa_actor_id, uint64_t serial, const void *context);
bool qa_combat_detach_primary(qa_combat *, qa_actor_id, uint64_t serial, const void *context, qa_error *);
/* The fuel owner is usually inventory. Effective armor reads it directly;
 * binding it does not create another spendable reservoir. */
/* One canonical reservoir per actor. Repeating the same inventory/item pair
 * is idempotent across providers; inventory must outlive combat. Component
 * removal closes its inventory leases without detaching this shared fuel. */
bool qa_combat_bind_power_inventory(qa_combat *, qa_actor_id, qa_inventory *, qa_item_id, qa_error *);
/* Read-only checkpoint/composition query; false means no reservoir. */
bool qa_combat_power_inventory(qa_combat *, qa_actor_id, qa_inventory **, qa_item_id *);
bool qa_combat_read(qa_combat *, qa_actor_id, qa_combat_state *, qa_error *);
/* Pure full-actor presence. Absence differs from a failing source reader. */
bool qa_combat_has(qa_combat *, qa_actor_id);
/* The actual before-reaction attack, retained for this full actor's lifetime.
 * Actor references keep their original generations even after retirement.
 * This query calls no source callbacks and does not require an attacker body. */
bool qa_combat_last_attack_read(const qa_combat *, qa_actor_id, qa_attack *, bool *present, qa_error *);
/* Uncomposed primary state for editing traits. Unlike effective read, this
 * cannot copy another provider's temporary protection into primary godmode. */
bool qa_combat_read_traits(qa_combat *, qa_actor_id, qa_combat_state *, qa_error *);
/* Checkpoint reads exclude component overlays and include authoritative fuel.
 * External source storage is identified by local=false and saves itself. */
bool qa_combat_primary_read(qa_combat *, qa_actor_id, qa_combat_state *, bool *local, qa_error *);
bool qa_combat_set_health(qa_combat *, qa_actor_id, float, qa_error *);
bool qa_combat_set_armor(qa_combat *, qa_actor_id, const qa_armor *, qa_error *);
bool qa_combat_set_regular_points(qa_combat *, qa_actor_id, double, const qa_regular_armor *initial, qa_error *);
bool qa_combat_set_regular_armor(qa_combat *, qa_actor_id, const qa_regular_armor *, qa_error *);
bool qa_combat_set_powered_armor(qa_combat *, qa_actor_id, const qa_powered_armor *, qa_error *);
bool qa_combat_normalize_legacy_armor(qa_combat *, qa_actor_id, const qa_armor *, qa_armor *, qa_error *);
bool qa_combat_set_traits(qa_combat *, qa_actor_id, const qa_combat_state *, qa_error *);
bool qa_combat_reserve_protection(qa_combat *, qa_actor_id, qa_protection_channel, const qa_protection_claim *, qa_protection_lease *, qa_error *);
bool qa_combat_bind_protection(qa_combat *, qa_protection_lease, const qa_protection_binding *, qa_error *);
bool qa_combat_close_protection(qa_combat *, qa_protection_lease, qa_error *);
bool qa_combat_protection_current(qa_combat *, qa_protection_lease);
bool qa_combat_protection_bound(qa_combat *, qa_protection_lease);
bool qa_combat_protection_owner(qa_combat *, qa_actor_id, qa_protection_channel, qa_actor_owner *, uint64_t *serial);
/* Run an original guest's armor site through the same selected owner. A null
 * geometry selects request geometry; source-local geometry leaves provenance
 * unchanged. The victim context is calculated from the selected geometry. */
/* power_effect optionally receives the actual compiled power-armor recipe at
 * its reached effect boundary; NONE means no boundary was reached. Source-owned
 * absorbers emit their own source effects and return NONE here. */
bool qa_combat_absorb(qa_combat *, const qa_damage_request *, qa_protection_channel, const qa_damage_geometry *, float, qa_damage_flags, const qa_armor_context *, float *, qa_powered_armor *power_effect, qa_error *);
bool qa_combat_apply(qa_combat *, const qa_damage_request *, qa_damage_outcome *, qa_error *);
bool qa_combat_run_source(qa_combat *, const qa_damage_request *, qa_source_damage_fn, void *, qa_damage_outcome *, qa_error *);
bool qa_damage_dispatch_source_reaction(qa_damage_observer *, const qa_damage_result *,
    float callback_knockback, qa_vec3 callback_point, qa_actor_owner, qa_source_reaction_body, void *, qa_error *);
/* For captured pending hits whose feedback/confirmation already completed.
 * The observer lives on the stack through one original helper, is consumed at
 * most once by its actual pain/death entry, and never enters persistence. */
bool qa_combat_run_source_reaction(qa_combat *, const qa_damage_request *, const qa_damage_result *,
    qa_source_reaction_executor, void *, qa_error *);
bool qa_source_reaction_dispatch(qa_source_reaction_observer *, const qa_damage_result *,
    float callback_knockback, qa_vec3 callback_point, qa_actor_owner, qa_source_reaction_body, void *, qa_error *);
bool qa_combat_source_reaction(qa_combat *, const qa_damage_request *, const qa_damage_result *, qa_error *);
qa_operation *qa_combat_damage_operation(qa_combat *);

/* Original pickup admission. Offers and callback arguments are borrowed only
 * during synchronous dispatch. Registrations copy offered/write arrays. */
typedef enum qa_pickup_resource_kind { QA_PICKUP_NO_RESOURCE, QA_PICKUP_INVENTORY, QA_PICKUP_PROTECTION } qa_pickup_resource_kind;
typedef struct qa_pickup_resource { qa_pickup_resource_kind kind; qa_item_id item; qa_protection_channel channel; } qa_pickup_resource;
typedef enum qa_pickup_fields { QA_PICKUP_COUNT, QA_PICKUP_CAPACITY, QA_PICKUP_COUNT_CAPACITY } qa_pickup_fields;
typedef struct qa_pickup_write { qa_pickup_resource resource; qa_pickup_fields fields; } qa_pickup_write;
typedef struct qa_pickup_cargo { qa_item_id item; double count; bool weapon; } qa_pickup_cargo;
typedef enum qa_pickup_grant_kind { QA_PICKUP_RESOURCE_GRANT, QA_PICKUP_MAP_COUPLED, QA_PICKUP_SOURCE_EFFECT } qa_pickup_grant_kind;
typedef struct qa_pickup_offer {
    qa_actor_id recipient, pickup;
    qa_actor_owner source;
    qa_item_id item;
    qa_pickup_resource default_resource;
    bool override_count, dropped;
    double count;
    uint64_t time_ns;
    const qa_pickup_cargo *cargo;
    size_t cargo_count;
    qa_pickup_grant_kind grant;
} qa_pickup_offer;
typedef enum qa_pickup_outcome { QA_PICKUP_REFUSED, QA_PICKUP_ACCEPTED, QA_PICKUP_STALE } qa_pickup_outcome;
typedef struct qa_pickup_execution qa_pickup_execution;
bool qa_pickup_current(const qa_pickup_execution *);
/* Pure proof that this current grant receipt owns the actual full recipient. */
bool qa_pickup_recipient_is(const qa_pickup_execution *,qa_actor_id);
/* Borrows the innermost execution for this full recipient until dispatch
 * returns. A nonzero owner selects its actual replacement registration;
 * zero accepts any owner. No matching execution is true with found=false. */
bool qa_pickups_execution_read(qa_pickups *,qa_actor_id,qa_actor_owner,
    const qa_pickup_execution **,bool *found,qa_error *);
const qa_pickup_write *qa_pickup_writes(const qa_pickup_execution *, size_t *count);
bool qa_pickup_store_protection(qa_pickup_execution *, const qa_protection_store *, qa_error *);
typedef struct qa_pickup_rule {
    uint32_t id;
    const qa_item_id *offered;
    size_t offered_count;
    const qa_pickup_write *writes;
    size_t write_count;
    void *context;
    bool (*take)(void *, const qa_pickup_offer *, qa_pickup_execution *, qa_pickup_outcome *, qa_error *);
    /* Synchronous read-only projection of this same recipient grant policy.
     * Keep context alive through the callback; do not take or mutate items. */
    bool (*preview)(void *, const qa_pickup_offer *, float *utility, bool *accepted, qa_error *);
} qa_pickup_rule;
typedef struct qa_pickup_lease { qa_actor_id actor; uint64_t serial; } qa_pickup_lease;
typedef struct qa_pickup_observer {
    void *context;
    /* Inspect must not take an item or change gameplay. The source keeps its
     * owner alive throughout this callback and computes utility using the same
     * source offer and recipient preview as pickup execution. Offer cargo is
     * borrowed only within the callback; the registry copies scalar fields. */
    bool (*inspect)(void *, qa_actor_id pickup, qa_actor_id recipient,
                    qa_pickup_offer *, float *utility, bool *available, qa_error *);
} qa_pickup_observer;
typedef struct qa_pickup_observation {
    qa_actor_id pickup, recipient;
    qa_actor_owner source;
    qa_item_id item;
    float utility;
    bool available, dropped;
} qa_pickup_observation;
/* One live offer owner per world pickup. Rebinding retires the old lease;
 * retirement closes automatically and an explicit close is idempotent.
 * Recipient replacement rules remain separate from world observations. */
bool qa_pickups_observe(qa_pickups *, qa_actor_id, qa_actor_owner,
                         const qa_pickup_observer *, qa_pickup_lease *, qa_error *);
bool qa_pickups_observation_close(qa_pickups *, qa_pickup_lease, qa_error *);
bool qa_pickups_observation_current(qa_pickups *, qa_pickup_lease);
bool qa_pickups_inspect(qa_pickups *, qa_actor_id pickup, qa_actor_id recipient,
                         qa_pickup_observation *, bool *found, qa_error *);
/* Source observers call this while their offer cargo remains borrowed. When
 * handled=false the selected original source computes its own native preview.
 * Blocked/stale grants are handled refusals; replacements use their own policy. */
bool qa_pickups_preview(qa_pickups *, const qa_pickup_offer *, float *utility,
                         bool *accepted, bool *handled, qa_error *);
typedef struct qa_pickup_continuation {
    void *context;
    bool (*eligible)(void *, const qa_pickup_offer *, bool *, qa_error *);
    bool (*original)(void *, const qa_pickup_offer *, bool *, qa_error *);
    bool (*complete)(void *, const qa_pickup_offer *, bool taken, qa_error *);
} qa_pickup_continuation;
typedef enum qa_pickup_selection { QA_PICKUP_SELECT_ORIGINAL, QA_PICKUP_SELECT_BLOCKED, QA_PICKUP_SELECT_REPLACEMENT, QA_PICKUP_SELECT_STALE } qa_pickup_selection;
typedef bool (*qa_pickup_source_fn)(void *, const qa_pickup_offer *, qa_pickup_selection, qa_pickup_execution *, qa_error *);
bool qa_pickups_create(qa_actor_registry *, qa_combat *, qa_inventory *, qa_pickups **, qa_error *);
bool qa_pickups_destroy(qa_pickups *, qa_error *);
bool qa_pickups_idle(const qa_pickups *);
bool qa_pickups_set_eligibility(qa_pickups *, bool (*)(void *, const qa_pickup_offer *, bool *, qa_error *), void *, qa_error *);
void qa_pickups_actor_released(qa_pickups *, qa_actor_record);
bool qa_pickups_bind(qa_pickups *, qa_actor_id, qa_actor_owner, const qa_pickup_rule *, size_t, qa_pickup_lease *, qa_error *);
/* Pure qualification of the actual recipient registration and resource leases. */
bool qa_pickups_registration_current(qa_pickups *,qa_pickup_lease,qa_actor_owner);
/* Retained active ownership only; resource retirement does not erase the lease. */
bool qa_pickups_registration_owned(const qa_pickups *,qa_pickup_lease,qa_actor_owner);
bool qa_pickups_close(qa_pickups *, qa_pickup_lease, qa_error *);
bool qa_pickups_touch(qa_pickups *, const qa_pickup_offer *, const qa_pickup_continuation *, qa_pickup_outcome *, qa_error *);
bool qa_pickups_run_source(qa_pickups *, const qa_pickup_offer *, qa_pickup_source_fn, void *, qa_error *);
bool qa_pickup_execute_grant(qa_pickup_execution *, qa_pickup_outcome *, qa_error *);
bool qa_pickup_consume(qa_pickup_execution *, bool (*remove)(void *, qa_error *), void *, qa_error *);
/* Validates source stores and advances any surrounding combat cursor. */
bool qa_combat_pickup_store(qa_combat *, qa_actor_id, qa_actor_owner, const qa_protection_store *, qa_error *);
typedef struct qa_combat_pickup_scope {
    qa_combat *combat;
    struct qa_combat_pickup_scope *previous;
    qa_actor_id actor;
    uint64_t binding_serial;
    bool open;
} qa_combat_pickup_scope;
/* Keep combat alive across pickup source callbacks, including inventory-only
 * grants. Always end a successful begin. Reconcile replacement grants to reject
 * omitted stores and source binding changes before returning to the map.
 * Scopes are address-bound, cannot be copied, and end in reverse begin order. */
/* A stale recipient is allowed to hold service lifetime while an original
 * source callback handles its own eligibility. */
bool qa_combat_pickup_begin(qa_combat *, qa_actor_id, qa_combat_pickup_scope *, qa_error *);
bool qa_combat_pickup_end(qa_combat_pickup_scope *, bool reconcile, qa_error *);

#endif
