/* Port of src/compat/q2/rerelease/deferred-damage.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_DEFERRED_DAMAGE_H
#define QA_COMPAT_Q2_RERELEASE_DEFERRED_DAMAGE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/q2/rerelease/module.h"
#include "qa/content/q2/foundation/checkpoint.h"

typedef struct qa_compat_q2_rerelease_q2_guest_host qa_compat_q2_rerelease_q2_guest_host;
typedef struct qa_compat_q2_rerelease_foreign_damage_services qa_compat_q2_rerelease_foreign_damage_services;

/* TS RereleaseSavedActor: is_native selects slot/generation vs shared. */
typedef struct qa_compat_q2_rerelease_saved_actor {
  bool is_native;
  double slot, generation;    /* native */
  qa_saved_actor_id shared;   /* shared */
} qa_compat_q2_rerelease_saved_actor;
typedef struct qa_compat_q2_rerelease_saved_reference {
  qa_saved_actor_id actor;
  qa_compat_q2_rerelease_saved_actor reference;
} qa_compat_q2_rerelease_saved_reference;
/* TS RereleaseDeferredDamageSave. request holds the Omit<DamageRequest,
 * "target"|"attack"> fields. references is malloc'd; release a save
 * list with qa_compat_q2_rerelease_deferred_damage_free_saves. */
typedef struct qa_compat_q2_rerelease_deferred_damage_save {
  qa_compat_q2_rerelease_saved_actor target;
  qa_q2_attack_checkpoint attack;
  qa_compat_q2_rerelease_saved_reference *references; size_t reference_count;
  double amount, knockback;
  qa_vec3 direction, point, normal;
  const char *delivery;
  double blood, save_knockback;
  qa_vec3 save_point;
  double mod[3]; size_t mod_count;
  double attacker_slot, inflictor_slot;
} qa_compat_q2_rerelease_deferred_damage_save;

typedef struct qa_compat_q2_rerelease_deferred_damage qa_compat_q2_rerelease_deferred_damage;
/* current_request false return is TS null. NULL only on OOM. */
qa_compat_q2_rerelease_deferred_damage *qa_compat_q2_rerelease_deferred_damage_create(
    qa_compat_q2_rerelease_q2_guest_host *host, qa_compat_q2_rerelease_foreign_damage_services services,
    void *ctx, bool (*current_request)(void *ctx, qa_actor_id actor, qa_damage_request *out),
    bool (*intercepted)(void *ctx));
void qa_compat_q2_rerelease_deferred_damage_destroy(qa_compat_q2_rerelease_deferred_damage *d);
void qa_compat_q2_rerelease_deferred_damage_track(qa_compat_q2_rerelease_deferred_damage *d, qa_raw_entity_view view);
void qa_compat_q2_rerelease_deferred_damage_release(qa_compat_q2_rerelease_deferred_damage *d, qa_owned_actor actor);
void qa_compat_q2_rerelease_deferred_damage_clear(qa_compat_q2_rerelease_deferred_damage *d);
void qa_compat_q2_rerelease_deferred_damage_close(qa_compat_q2_rerelease_deferred_damage *d);
qa_compat_q2_rerelease_saved_actor qa_compat_q2_rerelease_deferred_damage_save_actor(qa_compat_q2_rerelease_deferred_damage *d, qa_actor_id actor);
/* domain: "checkpoint" | "current". */
qa_actor_id qa_compat_q2_rerelease_deferred_damage_restore_actor(qa_compat_q2_rerelease_deferred_damage *d,
                                                                 qa_compat_q2_rerelease_saved_actor saved, const char *domain);
/* Malloc'd list; NULL only on OOM (count may still be 0). TS method is
 * save(), renamed to _save_entries: a C function cannot share the
 * qa_compat_q2_rerelease_deferred_damage_save type name. */
qa_compat_q2_rerelease_deferred_damage_save *qa_compat_q2_rerelease_deferred_damage_save_entries(
    qa_compat_q2_rerelease_deferred_damage *d, size_t *out_count);
void qa_compat_q2_rerelease_deferred_damage_free_saves(qa_compat_q2_rerelease_deferred_damage_save *saves, size_t count);
void qa_compat_q2_rerelease_deferred_damage_restore(qa_compat_q2_rerelease_deferred_damage *d,
                                                    const qa_compat_q2_rerelease_deferred_damage_save *saved, size_t count, const char *domain);
#endif
