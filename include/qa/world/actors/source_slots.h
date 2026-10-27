/*
 * Port of src/world/actors/source-slots.ts. The TS ActorLifetimePolicy
 * closure (reusableAfter) is the contracts fn ptr + ctx; the factory
 * helpers capture their constant in caller-owned storage passed back
 * as that ctx (see qa_quake_edict_lifetime / qa_q3_entity_lifetime).
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_ACTORS_SOURCE_SLOTS_H
#define QA_WORLD_ACTORS_SOURCE_SLOTS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/time.h"
#include "qa/contracts/world.h"
#include "qa/world/actors/registry.h"

/** TS SourceSlotState. */
typedef struct qa_source_slot_state {
  bool free;
  qa_source_time freed_at;
} qa_source_slot_state;

/** TS SourceSlotStorage. */
typedef struct qa_source_slot_storage {
  void *ctx;
  double (*count)(void *ctx);
  void (*set_count)(void *ctx, double count);
  qa_source_slot_state (*read)(void *ctx, int64_t slot);
  void (*initialize)(void *ctx, int64_t slot, qa_owned_actor actor);
  void (*clear_freed)(void *ctx, int64_t slot, qa_source_time now);
  bool (*can_free)(void *ctx, int64_t slot);
} qa_source_slot_storage;

/** TS SourceActorSlotsOptions. */
typedef struct qa_source_actor_slots_options {
  qa_provider_id provider;
  double capacity;
  qa_actor_lifetime_policy lifetime;
  qa_source_slot_storage storage;
  void *ctx;
  qa_source_time (*now)(void *ctx);
  void (*unlink)(void *ctx, qa_owned_actor actor);
  void (*exhausted)(void *ctx, int64_t slot);
} qa_source_actor_slots_options;

/* Factory storage: the returned policy's ctx points at the caller's
 * struct, which must outlive the policy. exhaustion is "fatal" or
 * "qw-last-slot" (exact strings). */
typedef struct qa_q3_lifetime_state { double map_start_milliseconds; } qa_q3_lifetime_state;
qa_actor_lifetime_policy qa_quake_edict_lifetime(double first_dynamic_slot,
                                                 const char *exhaustion);
qa_actor_lifetime_policy qa_q3_entity_lifetime(qa_q3_lifetime_state *state,
                                               double first_dynamic_slot,
                                               double map_start_milliseconds);

typedef struct qa_source_actor_slots qa_source_actor_slots;
/* NULL only on allocation failure; invalid range aborts (TS RangeError). */
qa_source_actor_slots *qa_source_actor_slots_create(qa_session_actor_registry *actors,
                                                    qa_source_actor_slots_options options);
void qa_source_actor_slots_destroy(qa_source_actor_slots *slots);
bool qa_source_actor_slots_at(qa_source_actor_slots *slots, int64_t slot, qa_owned_actor *out);
qa_owned_actor qa_source_actor_slots_bind_existing(qa_source_actor_slots *slots, int64_t slot,
                                                   const char *definition);
qa_owned_actor qa_source_actor_slots_allocate(qa_source_actor_slots *slots,
                                              const char *definition);
bool qa_source_actor_slots_free(qa_source_actor_slots *slots, qa_owned_actor actor);

#endif
