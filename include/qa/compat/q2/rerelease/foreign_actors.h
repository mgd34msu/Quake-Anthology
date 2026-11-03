/* Port of src/compat/q2/rerelease/foreign-actors.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_FOREIGN_ACTORS_H
#define QA_COMPAT_Q2_RERELEASE_FOREIGN_ACTORS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/q2/rerelease/deferred_damage.h"
#include "qa/compat/q2/rerelease/native_entries.h"
#include "qa/world/gameplay/authority.h"

typedef struct qa_compat_q2_rerelease_q2_guest_host qa_compat_q2_rerelease_q2_guest_host;

/* TS RereleaseForeignDamageServices: provenance result's attacker /
 * inflictor / cause are overwritten by the caller. */
typedef struct qa_compat_q2_rerelease_foreign_damage_services {
  void *ctx;
  qa_attack_provenance (*provenance)(void *ctx, qa_actor_id attacker, qa_actor_id inflictor, qa_actor_id target);
} qa_compat_q2_rerelease_foreign_damage_services;

typedef struct qa_compat_q2_rerelease_projection_save { double slot; qa_saved_actor_id actor; } qa_compat_q2_rerelease_projection_save;
/* TS lookup(): OwnedActor | null | undefined. */
typedef enum qa_compat_q2_rerelease_lookup {
  QA_COMPAT_Q2_RR_LOOKUP_UNDEFINED, QA_COMPAT_Q2_RR_LOOKUP_NULL, QA_COMPAT_Q2_RR_LOOKUP_ACTOR
} qa_compat_q2_rerelease_lookup;

typedef struct qa_compat_q2_rerelease_foreign_actors qa_compat_q2_rerelease_foreign_actors;
/* NULL only on OOM. The deferred-damage member is created with it. */
qa_compat_q2_rerelease_foreign_actors *qa_compat_q2_rerelease_foreign_actors_create(
    qa_compat_q2_rerelease_q2_guest_host *host, qa_compat_q2_rerelease_native_entries entries,
    qa_compat_q2_rerelease_foreign_damage_services services);
void qa_compat_q2_rerelease_foreign_actors_destroy(qa_compat_q2_rerelease_foreign_actors *f);
qa_compat_q2_rerelease_q2_guest_host *qa_compat_q2_rerelease_foreign_actors_get_host(const qa_compat_q2_rerelease_foreign_actors *f);
qa_compat_q2_rerelease_native_entries qa_compat_q2_rerelease_foreign_actors_get_entries(const qa_compat_q2_rerelease_foreign_actors *f);
qa_compat_q2_rerelease_deferred_damage *qa_compat_q2_rerelease_foreign_actors_get_deferred(qa_compat_q2_rerelease_foreign_actors *f);
/* TS withInventoryPublication<T>: operation returns an opaque result. */
void *qa_compat_q2_rerelease_foreign_actors_with_inventory_publication(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id actor,
                                                                        void *ctx, bool (*committed)(void *ctx),
                                                                        void *(*operation)(void *ctx));
bool qa_compat_q2_rerelease_foreign_actors_observing_damage(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id actor);
qa_source_armor_stage qa_compat_q2_rerelease_foreign_actors_armor_stage(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view,
                                                                        void *armor_ctx, qa_armor_state (*armor)(void *ctx),
                                                                        qa_protection_channel channel);
void qa_compat_q2_rerelease_foreign_actors_released(qa_compat_q2_rerelease_foreign_actors *f, qa_owned_actor actor);
/* TS engaged: the slot's actor is owned by a projection, or a restore is rebinding slots. */
bool qa_compat_q2_rerelease_foreign_actors_engaged(qa_compat_q2_rerelease_foreign_actors *f, double slot);
qa_compat_q2_rerelease_lookup qa_compat_q2_rerelease_foreign_actors_lookup(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view,
                                                                           qa_owned_actor *out);
qa_guest_address qa_compat_q2_rerelease_foreign_actors_address(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id actor);
void qa_compat_q2_rerelease_foreign_actors_synchronize(qa_compat_q2_rerelease_foreign_actors *f);
/* source_args NULL/count 0 is the TS undefined. */
qa_damage_outcome qa_compat_q2_rerelease_foreign_actors_damage_native(qa_compat_q2_rerelease_foreign_actors *f, qa_damage_request input,
                                                                      const qa_guest_call_value *source_args, size_t source_count);
void qa_compat_q2_rerelease_foreign_actors_clear(qa_compat_q2_rerelease_foreign_actors *f);
/* Returned list is scratch owned by f, replaced by the next call. */
const qa_compat_q2_rerelease_projection_save *qa_compat_q2_rerelease_foreign_actors_save_projections(
    qa_compat_q2_rerelease_foreign_actors *f, size_t *out_count);
/* domain: "checkpoint" | "current". */
void qa_compat_q2_rerelease_foreign_actors_begin_restore(qa_compat_q2_rerelease_foreign_actors *f,
                                                         const qa_compat_q2_rerelease_projection_save *saved, size_t count, const char *domain);
void qa_compat_q2_rerelease_foreign_actors_end_restore(qa_compat_q2_rerelease_foreign_actors *f);
void qa_compat_q2_rerelease_foreign_actors_close(qa_compat_q2_rerelease_foreign_actors *f);
#endif
