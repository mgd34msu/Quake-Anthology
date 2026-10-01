#ifndef QA_LAUNCH_H
#define QA_LAUNCH_H

#include "qa/catalog.h"
#include "qa/equipment.h"

typedef struct qa_launch_draft qa_launch_draft;
typedef struct qa_launch_snapshot qa_launch_snapshot;
typedef struct qa_configuration qa_configuration;
typedef struct qa_configuration_transaction qa_configuration_transaction;
typedef struct qa_launch_instance_storage qa_launch_instance_storage;
typedef struct qa_launch_instance_lease qa_launch_instance_lease;

typedef enum qa_launch_role {
    QA_ROLE_ENTITIES, QA_ROLE_CAMPAIGN, QA_ROLE_TRANSITION, QA_ROLE_MOVEMENT,
    QA_ROLE_CHARACTER, QA_ROLE_BODY, QA_ROLE_SKIN, QA_ROLE_VOICE, QA_ROLE_ARSENAL,
    QA_ROLE_COMBAT, QA_ROLE_INVENTORY, QA_ROLE_PICKUPS, QA_ROLE_MONSTERS,
    QA_ROLE_MODE, QA_ROLE_EQUIPMENT, QA_ROLE_ENGINE_BEHAVIOR, QA_ROLE_HUD,
    QA_ROLE_EFFECTS, QA_ROLE_AUDIO, QA_ROLE_MUSIC, QA_ROLE_MENU, QA_ROLE_BOTS,
    QA_ROLE_TRAJECTORY, QA_ROLE_COUNT
} qa_launch_role;
#define QA_ROLE_BIT(role) (UINT64_C(1) << (role))
typedef enum qa_launch_scope_kind {
    QA_SCOPE_WORLD, QA_SCOPE_DEFAULT_PLAYER, QA_SCOPE_ACTOR, QA_SCOPE_SEAT
} qa_launch_scope_kind;
typedef struct qa_launch_scope {
    qa_launch_scope_kind kind;
    qa_actor_id actor;
    uint32_t seat;
} qa_launch_scope;

typedef struct qa_launch_provider {
    /* Stable selected instance identity, separate from its implementation.
     * Equal programs may have arbitrarily many distinct instance keys. */
    const char *instance;
    qa_product_id product;
    qa_program_kind runtime;
    const char *implementation;
    const char *artifact;
    const char *component;
    qa_clock_config clock;
    /* Exact configuration bytes are copied and included in state identity. */
    qa_bytes options;
} qa_launch_provider;

typedef struct qa_launch_binding {
    qa_launch_scope scope;
    qa_launch_role role;
    /* Empty selector names the default. Named weapon/monster/item selectors
     * permit mixed definitions without making another inventory or world. */
    const char *selector;
    const char *instance;
    const char *definition;
} qa_launch_binding;
typedef struct qa_launch_mod_selection {
    const char *instance, *component;
    bool enabled;
} qa_launch_mod_selection;
typedef struct qa_launch_mode {
    const char *instance;
    qa_mode_rules rules;
    const char *teams[3], *forced_team;
    bool primary_score;
} qa_launch_mode;
typedef struct qa_launch_equipment {
    qa_launch_scope scope;
    const char *instance, *grapple_source, *grenade_source;
    qa_equipment_selection selection;
} qa_launch_equipment;
typedef struct qa_launch_seat {
    uint32_t id;
    qa_actor_id actor;
    const char *name, *team;
    uint32_t input_device;
    bool local, spectator, bot;
    float bot_skill;
    /* Definition alias is separate from the public name; NULL uses name. */
    const char *bot_definition;
    int32_t bot_delay_ms;
    /* Constructor declaration, independent of model resource paths. All four
     * NULL denotes no declaration. An empty head model follows the body. */
    const char *character_model, *character_skin;
    const char *character_head_model, *character_head_skin;
} qa_launch_seat;
typedef struct qa_launch_loadout {
    qa_launch_scope scope;
    const char *item;
    int32_t quantity, capacity;
    bool override_capacity, drop_on_death;
} qa_launch_loadout;
typedef struct qa_launch_monster {
    const char *authored_classname;
    const char *instance, *classname;
    bool map_defined;
} qa_launch_monster;
typedef struct qa_launch_weapon_behavior {
    qa_launch_scope scope;
    const char *weapon, *instance, *behavior;
    qa_builtin_projectile_role role;
    bool enabled;
} qa_launch_weapon_behavior;
typedef enum qa_launch_environment {
    QA_ENVIRONMENT_AUDIO_SOURCE, QA_ENVIRONMENT_DISABLED, QA_ENVIRONMENT_SELECTED
} qa_launch_environment;
typedef struct qa_launch_world {
    qa_product_id preset, geometry, presentation;
    const char *map;
    /* Preserves authored cinematic/spawn-point/unit transition syntax. */
    const char *start_command;
    /* Typed map loads retain their spawn selection independently of path
     * bytes. Presets without this selection use their authored start command. */
    const char *spawn_point;
    bool explicit_spawn_point;
    bool explicit_presentation, doppler, campaign;
    qa_launch_environment environment;
    qa_product_id environment_product;
    const char *environment_path;
    int32_t skill;
} qa_launch_world;
typedef struct qa_launch_choices {
    qa_launch_world world;
    const qa_launch_provider *providers; size_t provider_count;
    const qa_launch_binding *bindings; size_t binding_count;
    const qa_launch_mod_selection *mods; size_t mod_count;
    const qa_launch_mode *modes; size_t mode_count;
    const qa_launch_equipment *equipment; size_t equipment_count;
    const qa_launch_seat *seats; size_t seat_count;
    const qa_launch_loadout *loadout; size_t loadout_count;
    const qa_launch_monster *monsters; size_t monster_count;
    const qa_launch_weapon_behavior *behaviors; size_t behavior_count;
} qa_launch_choices;

/* A draft copies all strings/arrays/options and holds its catalog. Editing one
 * choice never silently rewrites another choice. Borrowed views expire on edit. */
bool qa_launch_draft_create(qa_catalog *, qa_product_id preset, const char *map,
                             qa_launch_draft **, qa_error *);
/* Save/migration reconstruct every choice explicitly, without preset defaults. */
bool qa_launch_draft_create_empty(qa_catalog *, qa_launch_draft **, qa_error *);
bool qa_launch_draft_copy(const qa_launch_draft *, qa_launch_draft **, qa_error *);
/* Preserve every choice without inserting defaults. Product IDs are resolved
 * by stable identity in the replacement catalog; missing products fail without
 * changing the source draft or publishing a partial result. */
bool qa_launch_draft_rebase(const qa_launch_draft *, qa_catalog *,
                            qa_launch_draft **, qa_error *);
bool qa_launch_snapshot_draft_copy(const qa_launch_snapshot *, qa_launch_draft **, qa_error *);
void qa_launch_draft_destroy(qa_launch_draft *);
const qa_launch_choices *qa_launch_draft_choices(const qa_launch_draft *);
qa_catalog *qa_launch_draft_catalog(const qa_launch_draft *);
bool qa_launch_set_world(qa_launch_draft *, const qa_launch_world *, qa_error *);
bool qa_launch_set_provider(qa_launch_draft *, const qa_launch_provider *, qa_error *);
bool qa_launch_remove_provider(qa_launch_draft *, const char *instance, qa_error *);
bool qa_launch_bind(qa_launch_draft *, const qa_launch_binding *, qa_error *);
bool qa_launch_unbind(qa_launch_draft *, qa_launch_scope, qa_launch_role,
                      const char *selector, qa_error *);
bool qa_launch_set_mod(qa_launch_draft *, const qa_launch_mod_selection *, qa_error *);
bool qa_launch_set_mode(qa_launch_draft *, const qa_launch_mode *, qa_error *);
bool qa_launch_remove_mode(qa_launch_draft *, const char *instance, qa_error *);
bool qa_launch_set_equipment(qa_launch_draft *, const qa_launch_equipment *, qa_error *);
bool qa_launch_set_seat(qa_launch_draft *, const qa_launch_seat *, qa_error *);
bool qa_launch_remove_seat(qa_launch_draft *, uint32_t id, qa_error *);
bool qa_launch_set_loadout(qa_launch_draft *, const qa_launch_loadout *, qa_error *);
bool qa_launch_set_monster(qa_launch_draft *, const qa_launch_monster *, qa_error *);
bool qa_launch_set_weapon_behavior(qa_launch_draft *, const qa_launch_weapon_behavior *, qa_error *);
/* Static selection/dependency validation only; external program qualification
 * and actual native provider construction occur during transaction prepare. */
bool qa_launch_validate(const qa_launch_draft *, qa_error *);
const qa_launch_binding *qa_launch_binding_for(const qa_launch_choices *, qa_launch_scope,
                                               qa_launch_role, const char *selector);

typedef struct qa_launch_instance {
    qa_launch_provider selection;
    /* Snapshot-local selected roles; role changes retain implementation state. */
    uint64_t roles;
    qa_vfs *content;
    const qa_resource *artifact, *declaration;
    const qa_vfs_acquisition *artifact_acquisition;
    const struct qa_launch_resource *interfaces;
    size_t interface_count;
    const qa_catalog_weapon_behavior *const *behaviors;
    size_t behavior_count;
    qa_sha256_digest identity; /* Implementation/configuration identity, excluding roles. */
    void *state;
    /* Private immutable storage identity used by detached metadata leases. */
    qa_launch_instance_storage *storage;
} qa_launch_instance;
typedef struct qa_launch_resource {
    qa_product_id product;
    const char *path;
    const qa_resource *resource;
} qa_launch_resource;

/* Retains the immutable descriptor/resources without retaining execution or
 * the snapshot. The lease view preserves this view's snapshot-local roles.
 * state remains borrowed: this lease does not defer close_instance. Acquire
 * during prepare_instance or while the source view is still alive; release
 * only after the consumer has stopped using all descriptor/resource views. */
bool qa_launch_instance_retain_metadata(const qa_launch_instance *,
                                        qa_launch_instance_lease **, qa_error *);
const qa_launch_instance *qa_launch_instance_lease_view(const qa_launch_instance_lease *);
void qa_launch_instance_lease_release(qa_launch_instance_lease *);
/* The implementation's retained catalog can precede the current snapshot. */
qa_catalog *qa_launch_instance_catalog(const qa_launch_instance *);
/* Own a client-only descriptor over a clone of the genuinely prepared view.
 * Opens its selected artifact once and retains that acquisition. This creates
 * metadata only; it never prepares a GAME provider or changes a snapshot.
 * runtime is the source factory's independently selected client backend. */
bool qa_launch_instance_prepare_client_metadata(const qa_launch_instance *, qa_catalog *,
    qa_product_id, qa_vfs *, const char *artifact_path, qa_program_kind,
    qa_launch_instance_lease **, qa_error *);
struct qa_launch_restored_instance;
/* Takes the actual claimed private content view, including on failure, after
 * the enclosing source owner has decoded and qualified its complete inventory. */
bool qa_launch_instance_restore_client_metadata(const qa_launch_instance *,
    const struct qa_launch_restored_instance *, qa_launch_instance_lease **, qa_error *);
/* Builtin client metadata retains the actual prepared content without opening
 * a program artifact. Execution remains owned by the original compiled source. */
bool qa_launch_instance_prepare_builtin_client_metadata(const qa_launch_instance *, qa_catalog *,
    qa_product_id, const qa_vfs *, qa_launch_instance_lease **, qa_error *);
/* Transfers the claimed saved view, also on failure after argument admission.
 * The saved descriptor must retain the same builtin source and empty artifact. */
bool qa_launch_instance_restore_builtin_client_metadata(const qa_launch_instance *,
    const struct qa_launch_restored_instance *, qa_launch_instance_lease **, qa_error *);

/* B25/B34 prepare native or qualified external state in detached ownership.
 * Preparation may warm the session's append-only string table, because an
 * aborted candidate cannot change or invalidate an existing string identity.
 * It must not otherwise mutate the live session. prepare_publication reserves
 * and validates bindings, registrations, roster changes and removals;
 * rollback releases only that candidate work.
 *
 * Before publish, every failure is reversible and leaves the active snapshot
 * unchanged. The manager installs the candidate snapshot before publish.
 * publish may then retire actors, detach removed instances and replace world
 * geometry exactly once. Those callbacks can fail after committed mutations;
 * the hook owner must capture that failure, enter its faulted state, and must
 * neither retry release callbacks nor claim rollback. publish itself therefore
 * has no error return. Retained instances keep their state; changed bindings
 * are described by both snapshots. A private instance is closed only after it
 * has detached and its final snapshot reader releases it.
 *
 * prepare_instance receives stable construction metadata. Its roles are the
 * initial selection only; current routing comes from the published snapshot.
 * Snapshot instance views are distinct even when they share private state.
 *
 * A non-NULL prepare_instance output transfers ownership even on failure, and
 * is then closed by the manager. Hook context must outlive all snapshots. The
 * manager is single-threaded; callbacks may inspect snapshots but cannot
 * reenter a mutating configuration operation on the same manager. */
typedef struct qa_configuration_hooks {
    void *context;
    bool (*safe)(void *);
    /* Optional pure construction fingerprint, computed before state reuse.
     * Include only choice-derived inputs consumed by provider construction;
     * use the same normalized values in the constructor. User options already
     * participate in identity. Routing roles alone must not reset state. */
    bool (*instance_configuration)(void *, const qa_launch_instance *,
                                    const qa_launch_choices *, qa_sha256_digest *, qa_error *);
    bool (*prepare_instance)(void *, const qa_launch_instance *, void **state, qa_error *);
    void (*close_instance)(void *, void *state);
    bool (*prepare_publication)(void *, const qa_launch_snapshot *previous,
                                const qa_launch_snapshot *candidate, void **ticket, qa_error *);
    void (*rollback_publication)(void *, void *ticket);
    void (*publish)(void *, const qa_launch_snapshot *previous,
                     const qa_launch_snapshot *candidate, void *ticket);
    /* Checked retirement runs with the current snapshot still retained.
     * Failure keeps the manager available so the owner can finish retirement.
     * The callback must retain incomplete owners and allow retry. */
    bool (*retire)(void *, const qa_launch_snapshot *, qa_error *);
} qa_configuration_hooks;
bool qa_configuration_create(const qa_configuration_hooks *, qa_configuration **, qa_error *);
/* Destroy is permitted only at a safe point and with no open transactions. */
bool qa_configuration_destroy(qa_configuration *, qa_error *);
uint64_t qa_configuration_generation(const qa_configuration *);
const qa_launch_snapshot *qa_configuration_current(const qa_configuration *);
bool qa_configuration_prepare(qa_configuration *, const qa_launch_draft *,
                               qa_configuration_transaction **, qa_error *);
/* A full world replacement constructs fresh instance owners even when their
 * immutable identities equal the current selections. Abort retains the old
 * owners; publication retires them through the ordinary checked lifecycle.
 * The resulting identities still permit ordinary later configuration reuse. */
bool qa_configuration_prepare_replacing(qa_configuration *, const qa_launch_draft *,
                                         qa_configuration_transaction **, qa_error *);
bool qa_configuration_validate(qa_configuration_transaction *, qa_error *);
/* On success consumes the transaction. Failure leaves it available to abort.
 * An intervening commit invalidates its expected generation, never the live state. */
bool qa_configuration_commit(qa_configuration_transaction *, qa_error *);
bool qa_configuration_abort(qa_configuration_transaction *, qa_error *);
const qa_launch_snapshot *qa_configuration_candidate(const qa_configuration_transaction *);
void qa_launch_snapshot_retain(const qa_launch_snapshot *);
void qa_launch_snapshot_release(const qa_launch_snapshot *);
const qa_launch_choices *qa_launch_snapshot_choices(const qa_launch_snapshot *);
qa_catalog *qa_launch_snapshot_catalog(const qa_launch_snapshot *);
qa_vfs *qa_launch_snapshot_mounts(const qa_launch_snapshot *);
size_t qa_launch_snapshot_instance_count(const qa_launch_snapshot *);
const qa_launch_instance *qa_launch_snapshot_instance(const qa_launch_snapshot *, size_t);
const qa_launch_instance *qa_launch_snapshot_find(const qa_launch_snapshot *, const char *instance);
size_t qa_launch_snapshot_resource_count(const qa_launch_snapshot *);
const qa_launch_resource *qa_launch_snapshot_resource(const qa_launch_snapshot *, size_t);

#endif
