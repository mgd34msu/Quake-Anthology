/* Port of src/app/bootstrap/simulation/arsenal/q3-ammo-regen.ts.
 * Pool timers are landed qa_q3_ta_mapped_ammo_timer views over owned
 * per-actor storage. Actors registry mirrors SessionActorRegistry
 * subset used here. capture() returns an owned qa_save_value tree.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_AMMO_REGEN_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_AMMO_REGEN_H
#include "qa/app/bootstrap/simulation/arsenal/selected.h"
#include "qa/content/q3/team_arena/client_effects.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

typedef struct qa_app_ammo_actor_registry {
  void *ctx;
  void (*assert_owned)(void *ctx, qa_owned_actor actor);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out); /* false = TS null */
  bool (*resolve_saved)(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out);
} qa_app_ammo_actor_registry;

typedef struct qa_app_q3_mapped_ammo_regeneration qa_app_q3_mapped_ammo_regeneration;
/* Validation errors abort; NULL only on OOM. */
qa_app_q3_mapped_ammo_regeneration *qa_app_q3_mapped_ammo_regeneration_create(qa_pickup_supply_profile profile,
    qa_app_ammo_actor_registry actors, qa_app_inventory_access inventory,
    const qa_pickup_supply_owner *legacy_owners, size_t legacy_owner_count);
void qa_app_q3_mapped_ammo_regeneration_destroy(qa_app_q3_mapped_ammo_regeneration *regen);
const qa_pickup_supply_profile *qa_app_q3_mapped_ammo_regeneration_profile(const qa_app_q3_mapped_ammo_regeneration *regen);
/* TS actors.onRelease cleanup. */
void qa_app_q3_mapped_ammo_regeneration_release_actor(qa_app_q3_mapped_ammo_regeneration *regen, qa_owned_actor actor);
const qa_q3_ta_mapped_ammo_timer *qa_app_q3_mapped_ammo_regeneration_timers(qa_app_q3_mapped_ammo_regeneration *regen,
    qa_owned_actor actor, size_t *out_count);
void qa_app_q3_mapped_ammo_regeneration_stored(qa_app_q3_mapped_ammo_regeneration *regen, qa_actor_id actor, double weapon, double value);
/* Owned tree; free with qa_save_value_free. NULL only on OOM. */
qa_save_value *qa_app_q3_mapped_ammo_regeneration_capture(qa_app_q3_mapped_ammo_regeneration *regen,
    const qa_owned_actor *actors, size_t actor_count);
void qa_app_q3_mapped_ammo_regeneration_restore(qa_app_q3_mapped_ammo_regeneration *regen, const qa_save_reader *reader,
    const qa_owned_actor *actors, size_t actor_count,
    double (*legacy)(void *ctx, qa_actor_id actor, double weapon), void *legacy_ctx);
void qa_app_q3_mapped_ammo_regeneration_close(qa_app_q3_mapped_ammo_regeneration *regen);
#endif
