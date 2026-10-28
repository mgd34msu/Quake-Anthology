/* Port of src/app/bootstrap/simulation/rerelease-weapon-behavior.ts.
 * Native execution runs through the host bundle (its own address
 * space); checkpoint capture/restore are pumped via next_frame.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_WEAPON_BEHAVIOR_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_WEAPON_BEHAVIOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/native_weapon_behavior.h"
#include "qa/contracts/session.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
typedef struct qa_app_sim_rerelease_weapon_trajectory { qa_vec3 origin, angles, velocity; } qa_app_sim_rerelease_weapon_trajectory;
typedef struct qa_app_sim_rerelease_weapon_retired { qa_saved_actor_id actor; qa_app_sim_rerelease_weapon_trajectory trajectory; } qa_app_sim_rerelease_weapon_retired;
typedef struct qa_app_sim_rerelease_weapon_binding { double slot; qa_saved_actor_id actor; const char *kind; double generation; } qa_app_sim_rerelease_weapon_binding;
typedef struct qa_app_sim_rerelease_weapon_configstring { double index; const char *value; } qa_app_sim_rerelease_weapon_configstring;
typedef struct qa_app_sim_rerelease_weapon_behavior_checkpoint {
  double version;
  qa_native_weapon_behavior_declaration declaration;
  qa_weapon_behavior_definition definition;
  const char *map, *entities, *spawn_point;
  double time;
  const uint8_t *cvars; size_t cvars_length;
  const void *game, *level; /* RereleaseSourceSave (compat opaque) */
  qa_app_sim_rerelease_weapon_configstring *configstrings; size_t configstring_count;
  qa_app_sim_rerelease_weapon_retired *retired; size_t retired_count;
  qa_app_sim_rerelease_weapon_binding *bindings; size_t binding_count;
} qa_app_sim_rerelease_weapon_behavior_checkpoint;
/* TS retireRereleaseWeaponActor over caller-owned slot/binding/
 * retired arrays (compacted in place; counts updated). */
typedef struct qa_app_sim_rerelease_slot_entry { qa_actor_id actor; double slot; } qa_app_sim_rerelease_slot_entry;
typedef struct qa_app_sim_rerelease_binding_entry { double slot; qa_actor_id actor; const char *kind; double generation; } qa_app_sim_rerelease_binding_entry;
typedef struct qa_app_sim_rerelease_retired_entry { qa_actor_id actor; qa_app_sim_rerelease_weapon_trajectory trajectory; } qa_app_sim_rerelease_retired_entry;
typedef struct qa_app_sim_rerelease_retire_state {
  qa_app_sim_rerelease_slot_entry *slots; size_t *slot_count;
  qa_app_sim_rerelease_binding_entry *bindings; size_t *binding_count;
  qa_app_sim_rerelease_retired_entry *retired; size_t *retired_count;
} qa_app_sim_rerelease_retire_state;
void qa_app_sim_retire_rerelease_weapon_actor(qa_actor_id actor, qa_app_sim_rerelease_retire_state state,
    void (*dispose)(void *ctx, double slot, qa_app_sim_rerelease_binding_entry binding), void *dispose_ctx);
typedef struct qa_app_sim_rerelease_weapon_behavior_source qa_app_sim_rerelease_weapon_behavior_source;
typedef struct qa_app_sim_rerelease_weapon_host {
  void *ctx;
  qa_weapon_behavior_instance_result (*attach)(void *ctx, qa_weapon_behavior_launch launch);
  qa_app_sim_rerelease_weapon_behavior_checkpoint (*checkpoint)(void *ctx, void (*next_frame)(void *frame_ctx), void *frame_ctx);
  void (*restore)(void *ctx, qa_app_sim_rerelease_weapon_behavior_checkpoint checkpoint, void (*next_frame)(void *frame_ctx), void *frame_ctx);
  void (*close)(void *ctx);
} qa_app_sim_rerelease_weapon_host;
qa_app_sim_rerelease_weapon_behavior_source *qa_app_sim_rerelease_weapon_behavior_source_create(
    qa_weapon_behavior_definition definition, qa_native_weapon_behavior_declaration declaration,
    qa_app_sim_rerelease_weapon_host host);
void qa_app_sim_rerelease_weapon_behavior_source_destroy(qa_app_sim_rerelease_weapon_behavior_source *source);
qa_native_weapon_behavior_declaration qa_app_sim_rerelease_weapon_behavior_source_declaration(const qa_app_sim_rerelease_weapon_behavior_source *source);
qa_weapon_behavior_definition qa_app_sim_rerelease_weapon_behavior_source_definition(const qa_app_sim_rerelease_weapon_behavior_source *source);
qa_weapon_behavior_source qa_app_sim_rerelease_weapon_behavior_source_as_source(qa_app_sim_rerelease_weapon_behavior_source *source);
qa_app_sim_rerelease_weapon_behavior_checkpoint qa_app_sim_rerelease_weapon_behavior_source_checkpoint(
    qa_app_sim_rerelease_weapon_behavior_source *source, void (*next_frame)(void *ctx), void *frame_ctx);
void qa_app_sim_rerelease_weapon_behavior_source_restore(qa_app_sim_rerelease_weapon_behavior_source *source,
    qa_app_sim_rerelease_weapon_behavior_checkpoint checkpoint, void (*next_frame)(void *ctx), void *frame_ctx);
void qa_app_sim_rerelease_weapon_behavior_source_close(qa_app_sim_rerelease_weapon_behavior_source *source);
#endif
