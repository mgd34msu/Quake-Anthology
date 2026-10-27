/*
 * Port of src/world/actors/registry.ts: the session actor registry.
 * Implements the contracts qa_actor_registry shape through the
 * qa_session_actor_registry_as_contract adapter. TS Error throws abort
 * with the identical message; RangeError throws use qa_range_error.
 * AggregateError fan-out degenerates to direct calls (C callbacks
 * abort on failure rather than throwing recoverably). Returned
 * arrays are registry-owned scratch, invalidated by the next call.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_ACTORS_REGISTRY_H
#define QA_WORLD_ACTORS_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/contracts/world.h"

/** TS SourceActorCheckpoint. */
typedef struct qa_source_actor_checkpoint {
  qa_provider_id provider;
  double source_slot;
  qa_saved_actor_id actor;
} qa_source_actor_checkpoint;

typedef struct qa_session_actor_registry qa_session_actor_registry;

int64_t qa_next_actor_generation(qa_session_id session, int64_t slot, int64_t minimum);

/* capacity must be a positive safe integer (TS RangeError).
 * Returns NULL only on allocation failure. */
qa_session_actor_registry *qa_session_actor_registry_create(
    const qa_identity_owner *identities, int64_t capacity);
void qa_session_actor_registry_destroy(qa_session_actor_registry *registry);
qa_session_id qa_session_actor_registry_session(const qa_session_actor_registry *registry);
int64_t qa_session_actor_registry_capacity(const qa_session_actor_registry *registry);
double qa_session_actor_registry_revision(const qa_session_actor_registry *registry);

qa_owned_actor qa_session_actor_registry_allocate(qa_session_actor_registry *registry,
                                                  qa_provider_id owner,
                                                  const char *definition);
qa_owned_actor qa_session_actor_registry_allocate_at_source(
    qa_session_actor_registry *registry, qa_provider_id owner, int64_t source_slot,
    const char *definition);
void qa_session_actor_registry_bind_source(qa_session_actor_registry *registry,
                                           qa_owned_actor actor, qa_provider_id provider,
                                           int64_t source_slot);
bool qa_session_actor_registry_at_source(const qa_session_actor_registry *registry,
                                         qa_provider_id owner, int64_t slot,
                                         qa_owned_actor *out);
typedef struct qa_actor_source_result {
  bool has_value;
  qa_provider_id provider;
  int64_t slot;
} qa_actor_source_result;
qa_actor_source_result qa_session_actor_registry_source_of(
    const qa_session_actor_registry *registry, qa_actor_id actor);
void qa_session_actor_registry_release(qa_session_actor_registry *registry,
                                       qa_owned_actor actor);
bool qa_session_actor_registry_is_live(const qa_session_actor_registry *registry,
                                       qa_actor_id actor);
qa_actor_observation_result qa_session_actor_registry_observe(
    const qa_session_actor_registry *registry, qa_actor_id actor);
bool qa_session_actor_registry_resolve_owned(const qa_session_actor_registry *registry,
                                             qa_actor_id actor, qa_owned_actor *out);
const qa_owned_actor *qa_session_actor_registry_owned_by(
    qa_session_actor_registry *registry, qa_provider_id owner, size_t *out_count);
const qa_actor_observation *qa_session_actor_registry_observations(
    qa_session_actor_registry *registry, size_t *out_count);
const qa_actor_slot_checkpoint *qa_session_actor_registry_checkpoint(
    qa_session_actor_registry *registry, size_t *out_count);
const qa_source_actor_checkpoint *qa_session_actor_registry_source_checkpoint(
    qa_session_actor_registry *registry, size_t *out_count);
bool qa_session_actor_registry_resolve_saved(const qa_session_actor_registry *registry,
                                             qa_saved_actor_id saved, qa_owned_actor *out);
void qa_session_actor_registry_rebind_restored_source(qa_session_actor_registry *registry,
                                                      qa_provider_id provider);
/* domain: "checkpoint" (default) | "current" (exact strings). */
qa_actor_id qa_session_actor_registry_reference_saved(qa_session_actor_registry *registry,
                                                      qa_saved_actor_id saved,
                                                      const char *domain);
qa_session_actor_registry *qa_session_actor_registry_restore(
    const qa_identity_owner *identities, const qa_actor_slot_checkpoint *checkpoints,
    size_t checkpoint_count, const qa_source_actor_checkpoint *sources, size_t source_count,
    int64_t capacity);

/* TS onRelease: returns a subscription id for _off_release. */
typedef void (*qa_actor_release_fn)(void *ctx, qa_owned_actor actor);
int64_t qa_session_actor_registry_on_release(qa_session_actor_registry *registry,
                                             qa_actor_release_fn fn, void *ctx);
void qa_session_actor_registry_off_release(qa_session_actor_registry *registry, int64_t id);
void qa_session_actor_registry_assert_owned(const qa_session_actor_registry *registry,
                                            qa_owned_actor actor);
void qa_session_actor_registry_close(qa_session_actor_registry *registry);

/* Contracts adapter: the returned table borrows the registry. */
qa_actor_registry qa_session_actor_registry_as_contract(qa_session_actor_registry *registry);

/* TS OwnedActor object identity: same actor, owner, and token. */
bool qa_same_owned_actor(qa_owned_actor left, qa_owned_actor right);

#endif
