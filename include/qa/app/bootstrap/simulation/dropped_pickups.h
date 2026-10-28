/* Port of src/app/bootstrap/simulation/dropped-pickups.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_DROPPED_PICKUPS_H
#define QA_APP_BOOTSTRAP_SIMULATION_DROPPED_PICKUPS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
typedef struct qa_app_sim_dropped_pickup_cargo { qa_item_id item; double count; } qa_app_sim_dropped_pickup_cargo;
typedef struct qa_app_sim_dropped_pickup_slot { qa_item_id item; double count; double slot; } qa_app_sim_dropped_pickup_slot;
typedef struct qa_app_sim_dropped_pickup_level { const char *map; qa_app_sim_dropped_pickup_slot *items; size_t item_count; } qa_app_sim_dropped_pickup_level;
typedef struct qa_app_sim_source_pickup_cargo qa_app_sim_source_pickup_cargo;
/* Registry callbacks (TS SessionActorRegistry subset). source_of returns false for TS null. */
typedef struct qa_app_sim_pickup_registry {
  void *ctx;
  bool (*is_live)(void *ctx, qa_actor_id actor);
  bool (*source_of)(void *ctx, qa_actor_id actor, qa_provider_id *out_provider, double *out_slot);
  bool (*at_source)(void *ctx, qa_provider_id provider, double slot, qa_actor_id *out);
  bool (*resolve_saved)(void *ctx, qa_saved_actor_id saved, qa_actor_id *out);
} qa_app_sim_pickup_registry;
qa_app_sim_source_pickup_cargo *qa_app_sim_source_pickup_cargo_create(qa_app_sim_pickup_registry registry, qa_provider_id provider);
void qa_app_sim_source_pickup_cargo_destroy(qa_app_sim_source_pickup_cargo *c);
bool qa_app_sim_source_pickup_cargo_get(qa_app_sim_source_pickup_cargo *c, qa_actor_id actor, qa_app_sim_dropped_pickup_cargo *out);
void qa_app_sim_source_pickup_cargo_set(qa_app_sim_source_pickup_cargo *c, qa_actor_id actor, qa_app_sim_dropped_pickup_cargo cargo);
void qa_app_sim_source_pickup_cargo_delete(qa_app_sim_source_pickup_cargo *c, qa_actor_id actor);
void qa_app_sim_source_pickup_cargo_release(qa_app_sim_source_pickup_cargo *c, qa_actor_id actor);
/* travel result: owned levels array; free with _levels_destroy. */
qa_app_sim_dropped_pickup_level *qa_app_sim_source_pickup_cargo_travel(qa_app_sim_source_pickup_cargo *c, const char *map, bool new_unit, size_t *out_count);
void qa_app_sim_dropped_pickup_levels_destroy(qa_app_sim_dropped_pickup_level *levels, size_t count);
void qa_app_sim_source_pickup_cargo_revisit(qa_app_sim_source_pickup_cargo *c, const qa_app_sim_dropped_pickup_level *levels, size_t count, const char *map);
#endif
