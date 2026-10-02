#ifndef QA_EXECUTABLE_RECIPE_H
#define QA_EXECUTABLE_RECIPE_H

#include "qa/application.h"
#include "qa/network_unified.h"

typedef struct qa_recipe_actor {
    uint32_t slot;
    uint64_t generation;
    bool present;
} qa_recipe_actor;
/* Source identities belong to the offered composition, never a local registry. */
typedef struct qa_recipe_scope {
    qa_launch_scope_kind kind;
    qa_recipe_actor actor;
    uint32_t seat;
} qa_recipe_scope;
typedef struct qa_recipe_binding {
    qa_recipe_scope scope;
    qa_launch_role role;
    const char *selector, *instance, *definition;
} qa_recipe_binding;
typedef struct qa_recipe_equipment {
    qa_recipe_scope scope;
    const char *instance, *grapple_source, *grenade_source;
    qa_equipment_selection selection;
} qa_recipe_equipment;
typedef struct qa_recipe_seat {
    qa_recipe_actor actor;
    qa_launch_seat selection; /* selection.actor is empty until real admission. */
} qa_recipe_seat;
typedef struct qa_recipe_loadout {
    qa_recipe_scope scope;
    const char *item;
    int32_t quantity, capacity;
    bool override_capacity, drop_on_death;
} qa_recipe_loadout;
typedef struct qa_recipe_behavior {
    qa_recipe_scope scope;
    const char *weapon, *instance, *behavior;
    qa_builtin_projectile_role role;
    bool enabled;
} qa_recipe_behavior;
typedef struct qa_recipe_choices {
    qa_launch_world world;
    const qa_launch_provider *providers; size_t provider_count;
    const qa_recipe_binding *bindings; size_t binding_count;
    const qa_launch_mod_selection *mods; size_t mod_count;
    const qa_launch_mode *modes; size_t mode_count;
    const qa_recipe_equipment *equipment; size_t equipment_count;
    const qa_recipe_seat *seats; size_t seat_count;
    const qa_recipe_loadout *loadout; size_t loadout_count;
    const qa_launch_monster *monsters; size_t monster_count;
    const qa_recipe_behavior *behaviors; size_t behavior_count;
} qa_recipe_choices;
typedef struct qa_recipe_sidecar {
    qa_product_id product;
    const char *path;
    const qa_resource *resource; /* NULL records an actual observed miss. */
} qa_recipe_sidecar;
typedef struct qa_recipe_provider {
    qa_launch_provider selection;
    qa_actor_owner source_owner;
    uint64_t roles;
    bool registered;
    qa_vfs *content;
    const qa_resource *artifact, *declaration;
    const qa_vfs_acquisition *artifact_acquisition;
    const qa_launch_resource *interfaces; size_t interface_count;
    const qa_catalog_weapon_behavior *const *behaviors; size_t behavior_count;
} qa_recipe_provider;
typedef struct qa_executable_recipe qa_executable_recipe;
struct qa_application_content_visitor;

/* Publication uses the actual retained initial map sidecars, including misses.
 * Optional supplied rows must exactly match that genuine admission inventory. */
bool qa_application_unified_offer(qa_application *, uint32_t epoch,
    const char *mode, uint32_t max_clients, const qa_recipe_sidecar *, size_t,
    qa_unified_document **owned, qa_error *);
/* Resolves only against installed catalog capabilities. Native paths and
 * process-local pool/mount/product IDs never appear in the wire identity. */
bool qa_executable_recipe_prepare(const qa_unified_document *, qa_catalog *,
    qa_resource_pool *, qa_executable_recipe **owned, qa_error *);
bool qa_executable_recipe_current(const qa_executable_recipe *, const qa_catalog *);
bool qa_executable_recipe_close(qa_executable_recipe *, qa_error *);
/* Enumerates every genuine holder, including admitted lookup-policy views. */
bool qa_executable_recipe_content_visit(const qa_executable_recipe *,
    const struct qa_application_content_visitor *, qa_error *);
const qa_sha256_digest *qa_executable_recipe_digest(const qa_executable_recipe *);
uint32_t qa_executable_recipe_epoch(const qa_executable_recipe *);
const char *qa_executable_recipe_mode(const qa_executable_recipe *);
uint32_t qa_executable_recipe_max_clients(const qa_executable_recipe *);
qa_catalog *qa_executable_recipe_catalog(const qa_executable_recipe *);
qa_vfs *qa_executable_recipe_mounts(const qa_executable_recipe *);
/* Borrows the admitted product scope, retaining a real local catalog view
 * when subsequent model/style content was not needed by the initial offer. */
bool qa_executable_recipe_content(qa_executable_recipe *, const char *content_identity,
    qa_vfs **, const qa_product **, qa_error *);
qa_resource *qa_executable_recipe_map(const qa_executable_recipe *);
qa_collision_geometry *qa_executable_recipe_geometry(const qa_executable_recipe *);
const qa_recipe_choices *qa_executable_recipe_choices(const qa_executable_recipe *);
size_t qa_executable_recipe_provider_count(const qa_executable_recipe *);
/* Descriptor rows own actual local held resources but have no execution state. */
const qa_recipe_provider *qa_executable_recipe_provider(const qa_executable_recipe *, size_t);
qa_actor_owner qa_executable_recipe_source_owner(const qa_executable_recipe *, size_t);
bool qa_executable_recipe_mixed_order(const qa_executable_recipe *);
size_t qa_executable_recipe_order_count(const qa_executable_recipe *);
const qa_recipe_provider *qa_executable_recipe_order_at(const qa_executable_recipe *, size_t);
size_t qa_executable_recipe_resource_count(const qa_executable_recipe *);
bool qa_executable_recipe_resource(const qa_executable_recipe *, size_t,
    qa_launch_resource *, qa_vfs **, const qa_vfs_acquisition **);
bool qa_executable_recipe_find_resource(const qa_executable_recipe *,
    const char *content_identity, const char *path, const qa_sha256_digest *, uint64_t byte_length,
    qa_launch_resource *, qa_vfs **, const qa_vfs_acquisition **);
/* Resolves a subsequent wire resource through the admitted content policy and
 * retains the actual matching bytes. The caller borrows the result until close. */
bool qa_executable_recipe_acquire_resource(qa_executable_recipe *,
    const char *content_identity, const char *path, const qa_sha256_digest *, uint64_t byte_length,
    qa_launch_resource *, qa_vfs **, const qa_vfs_acquisition **, qa_error *);
size_t qa_executable_recipe_sidecar_count(const qa_executable_recipe *);
const qa_recipe_sidecar *qa_executable_recipe_sidecar(const qa_executable_recipe *, size_t);

#endif
