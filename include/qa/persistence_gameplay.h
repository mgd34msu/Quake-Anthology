#ifndef QA_PERSISTENCE_GAMEPLAY_H
#define QA_PERSISTENCE_GAMEPLAY_H

#include "qa/source_save.h"
#include "qa/inventory.h"
#include "qa/targets.h"

/* Resolvers borrow restored source continuations. They only return callbacks;
 * they must not initialize source state or mutate shared stores. Saved lease
 * serials remain owner identities within the candidate's fresh actor registry. */
typedef struct qa_persistence_gameplay_resolvers {
    void *context;
    bool (*combat)(void *, qa_actor_id, uint64_t serial, qa_combat_binding *, qa_error *);
    bool (*admission)(void *, qa_actor_id, qa_combat_admission *, qa_error *);
    bool (*protection)(void *, qa_actor_id, qa_protection_channel,
                       const qa_protection_claim *, qa_protection_binding *, qa_error *);
    bool (*inventory_primary)(void *, qa_actor_id, uint64_t serial, qa_inventory_binding *, qa_error *);
    bool (*inventory_group)(void *, qa_actor_id, uint64_t serial, const qa_inventory_source_group *,
                            qa_inventory_items *, qa_error *);
    bool (*pickup_rule)(void *, qa_actor_id, qa_actor_owner, uint64_t serial, uint32_t rule,
                        qa_pickup_rule *, qa_error *);
    bool (*pickup_observer)(void *, qa_actor_id, qa_actor_owner, uint64_t serial, qa_pickup_observer *, qa_error *);
    bool (*target)(void *, qa_actor_id, qa_clock_kind, qa_target_binding *, qa_error *);
} qa_persistence_gameplay_resolvers;

bool qa_persistence_combat_capture(qa_session *, qa_combat *, qa_inventory *, qa_buffer *, qa_error *);
/* Candidate-only: on failure the caller must discard the entire candidate.
 * Prepared policies must match the saved ordered provider/family inventory. */
bool qa_persistence_combat_restore(qa_session *, qa_combat *, qa_inventory *,
    const qa_persistence_gameplay_resolvers *, qa_bytes, qa_error *);
bool qa_persistence_combat_validate(qa_combat *, qa_inventory *, qa_error *);
bool qa_persistence_combat_admission(const qa_combat *, qa_actor_id, qa_combat_admission *);
bool qa_persistence_inventory_capture(qa_session *, qa_inventory *, qa_buffer *, qa_error *);
bool qa_persistence_inventory_restore(qa_session *, qa_inventory *,
    const qa_persistence_gameplay_resolvers *, qa_bytes, qa_error *);
bool qa_persistence_pickups_capture(qa_session *, qa_pickups *, qa_buffer *, qa_error *);
bool qa_persistence_pickups_restore(qa_session *, qa_pickups *,
    const qa_persistence_gameplay_resolvers *, qa_bytes, qa_error *);
bool qa_persistence_targets_capture(qa_targets *, qa_buffer *, qa_error *);
bool qa_persistence_targets_restore(qa_targets *,
    const qa_persistence_gameplay_resolvers *, qa_bytes, qa_error *);
bool qa_persistence_targets_binding(const qa_targets *, qa_actor_id, qa_target_binding *);

#endif
