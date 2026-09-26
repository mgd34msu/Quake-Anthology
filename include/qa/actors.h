/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_ACTORS_H
#define QA_ACTORS_H

#include "qa/common.h"

typedef uint32_t qa_actor_owner;
typedef uint32_t qa_actor_definition;
typedef struct qa_actor_registry qa_actor_registry;

/* Value handle, never a pointer or a serialized C struct. Only the registry
 * creates live IDs. A zero registry value is invalid. */
typedef struct qa_actor_id {
    uint64_t registry;
    uint64_t generation;
    uint32_t slot;
} qa_actor_id;

typedef struct qa_actor_record {
    qa_actor_id id;
    qa_actor_owner owner;
    qa_actor_definition definition;
    uint32_t source_slot;
    bool has_source;
} qa_actor_record;

/* Relative to one registry checkpoint. Use save_reference to reject IDs from
 * other worlds before encoding this pair. Retain the registry across travel. */
typedef struct qa_saved_actor_id {
    uint64_t generation;
    uint32_t slot;
} qa_saved_actor_id;

/* Array position is the saved host slot. A free slot stores the next unused
 * generation; UINT64_MAX permanently retires that slot. */
typedef struct qa_actor_slot_checkpoint {
    uint64_t generation;
    qa_actor_owner owner;
    qa_actor_definition definition;
    uint32_t source_slot;
    bool active;
    bool has_source;
} qa_actor_slot_checkpoint;

typedef struct qa_actor_checkpoint {
    qa_actor_slot_checkpoint *slots;
    uint32_t count;
    uint32_t capacity;
} qa_actor_checkpoint;

/* The released record is a value snapshot. Its ID and source binding are
 * invalid before dispatch. Ordinary release callbacks may allocate/release.
 * Clear/destroy callbacks may release other actors but cannot allocate.
 * Recursive clear/destroy is rejected while a callback is active. */
typedef void (*qa_actor_release_fn)(void *context, qa_actor_registry *registry,
                                    qa_actor_record released);

bool qa_actor_id_equal(qa_actor_id left, qa_actor_id right);
bool qa_actors_create(uint32_t capacity, qa_actor_release_fn release,
                      void *context, qa_actor_registry **out, qa_error *error);
bool qa_actors_destroy(qa_actor_registry *registry, qa_error *error);
bool qa_actors_clear(qa_actor_registry *registry, qa_error *error);

/* Registries have one thread owner. Owner and definition IDs are interned by
 * the session; zero is permitted. Failed calls leave output values unchanged. */
bool qa_actors_allocate(qa_actor_registry *registry, qa_actor_owner owner,
                        qa_actor_definition definition, qa_actor_id *out,
                        qa_error *error);
bool qa_actors_allocate_source(qa_actor_registry *registry, qa_actor_owner owner,
                               uint32_t source_slot, qa_actor_definition definition,
                               qa_actor_id *out, qa_error *error);
bool qa_actors_release(qa_actor_registry *registry, qa_actor_id actor,
                       qa_error *error);

/* Record addresses remain fixed until destruction. Contents belong to the
 * current occupant and change on release/reuse; retain IDs for authority. */
const qa_actor_record *qa_actors_get(const qa_actor_registry *registry, qa_actor_id actor);
const qa_actor_record *qa_actors_at_source(const qa_actor_registry *registry,
                                         qa_actor_owner owner, uint32_t source_slot);
/* Set cursor to zero. Visits current live records in ascending host-slot order.
 * Exhaustion leaves out unchanged. Do not retain observations across mutation. */
bool qa_actors_next(const qa_actor_registry *registry, uint32_t *cursor,
                    const qa_actor_record **out);
uint32_t qa_actors_count(const qa_actor_registry *registry);
uint32_t qa_actors_capacity(const qa_actor_registry *registry);
uint64_t qa_actors_revision(const qa_actor_registry *registry);

/* Checkpoints are owned memory, separate from wire/file encoding. Snapshot and
 * restore require no active release callback. Restore creates a fresh namespace
 * and publishes out only on success. Free an existing output checkpoint before
 * replacing it. Clear retires saved-reference mappings. */
bool qa_actors_checkpoint(const qa_actor_registry *registry,
                          qa_actor_checkpoint *out, qa_error *error);
void qa_actor_checkpoint_free(qa_actor_checkpoint *checkpoint);
bool qa_actors_restore(const qa_actor_checkpoint *checkpoint,
                       qa_actor_release_fn release, void *context,
                       qa_actor_registry **out, qa_error *error);
const qa_actor_record *qa_actors_resolve_saved(const qa_actor_registry *registry,
                                             qa_saved_actor_id saved);
bool qa_actors_save_reference(const qa_actor_registry *registry, qa_actor_id actor,
                              qa_saved_actor_id *out, qa_error *error);
/* Reference provenance can name a retired actor without granting live access.
 * checkpoint_domain selects the most recent restoration's identity history. */
bool qa_actors_reference_saved(const qa_actor_registry *registry,
                               qa_saved_actor_id saved, bool checkpoint_domain,
                               qa_actor_id *out, qa_error *error);
/* Original save callbacks may reconstruct source actors in another host order.
 * Retire restored source actors, recreate them, then rebind the entire owner.
 * Failure leaves all mappings unchanged. Each owner can rebind once per restore. */
bool qa_actors_rebind_restored_source(qa_actor_registry *registry,
                                     qa_actor_owner owner, qa_error *error);

#endif
