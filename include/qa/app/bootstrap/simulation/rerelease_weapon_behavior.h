/* Port of src/app/bootstrap/simulation/rerelease-weapon-behavior.ts.
 * Native private state executes in its own guest address space; async
 * loading (create, checkpoint, restore) runs synchronously with a
 * next-frame callback.
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
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_services.h"
#include "qa/app/bootstrap/simulation/classic_guest_world.h"
#include "qa/compat/q2/rerelease/weapon_behavior_profile.h"
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
  qa_compat_q2_rerelease_source_save game, level;
  qa_app_sim_rerelease_weapon_configstring *configstrings; size_t configstring_count;
  qa_app_sim_rerelease_weapon_retired *retired; size_t retired_count;
  qa_app_sim_rerelease_weapon_binding *bindings; size_t binding_count;
} qa_app_sim_rerelease_weapon_behavior_checkpoint;
/* TS retireRereleaseWeaponActor over caller-owned slot/binding/
 * retired arrays (compacted in place; counts updated). */
typedef struct qa_app_sim_rerelease_slot_entry { qa_actor_id actor; double slot; } qa_app_sim_rerelease_slot_entry;
typedef struct qa_app_sim_rerelease_binding_entry { double slot; qa_actor_id actor; const char *kind; double generation; qa_owned_actor owned; } qa_app_sim_rerelease_binding_entry;
typedef struct qa_app_sim_rerelease_retired_entry { qa_actor_id actor; qa_app_sim_rerelease_weapon_trajectory trajectory; } qa_app_sim_rerelease_retired_entry;
typedef struct qa_app_sim_rerelease_retire_state {
  qa_app_sim_rerelease_slot_entry *slots; size_t *slot_count;
  qa_app_sim_rerelease_binding_entry *bindings; size_t *binding_count;
  qa_app_sim_rerelease_retired_entry *retired; size_t *retired_count;
} qa_app_sim_rerelease_retire_state;
void qa_app_sim_retire_rerelease_weapon_actor(qa_actor_id actor, qa_app_sim_rerelease_retire_state state,
    void (*dispose)(void *ctx, double slot, qa_app_sim_rerelease_binding_entry binding), void *dispose_ctx);
typedef struct qa_app_sim_rerelease_weapon_behavior_source qa_app_sim_rerelease_weapon_behavior_source;
/* TS RereleaseWeaponActor: the primary actor plus its shooter projection. */
typedef struct qa_app_sim_rerelease_weapon_actor { qa_owned_actor actor; const char *userinfo; qa_compat_q2_rerelease_weapon_shooter shooter; } qa_app_sim_rerelease_weapon_actor;
/* TS RereleaseWeaponBehaviorOptions. ownership is the literal
 * { inventory: "source-private", presentation: "selected-weapon" } in TS
 * and has no C member. */
typedef struct qa_app_sim_rerelease_weapon_behavior_options {
  const qa_app_sim_prepared_rerelease_guest *prepared;
  const qa_native_weapon_behavior_declaration *declaration;
  qa_app_sim_rerelease_guest_services_options services;
  void *clock_ctx; bool (*now_milliseconds)(void *ctx, double *out); bool (*performance_counter)(void *ctx, int64_t *out); int64_t performance_frequency;
  qa_app_sim_classic_guest_map map;
  /* TS ownership: source inventory is private; primary trigger, ammo, impact and presentation retain authority.
   * The only TS values are inventory "source-private" and presentation "selected-weapon". */
  struct { const char *inventory, *presentation; } ownership;
  void *ctx;
  qa_app_sim_rerelease_weapon_actor (*actor)(void *ctx, qa_actor_id id);
  void *next_frame_ctx; void (*next_frame)(void *ctx);
} qa_app_sim_rerelease_weapon_behavior_options;
/* TS RereleaseWeaponBehaviorSource.create (async loading made synchronous). Aborts as the TS throws. */
qa_app_sim_rerelease_weapon_behavior_source *qa_app_sim_rerelease_weapon_behavior_source_create(const qa_app_sim_rerelease_weapon_behavior_options *options);
void qa_app_sim_rerelease_weapon_behavior_source_destroy(qa_app_sim_rerelease_weapon_behavior_source *source);
qa_native_weapon_behavior_declaration qa_app_sim_rerelease_weapon_behavior_source_declaration(const qa_app_sim_rerelease_weapon_behavior_source *source);
qa_weapon_behavior_definition qa_app_sim_rerelease_weapon_behavior_source_definition(const qa_app_sim_rerelease_weapon_behavior_source *source);
qa_weapon_behavior_source qa_app_sim_rerelease_weapon_behavior_source_as_source(qa_app_sim_rerelease_weapon_behavior_source *source);
qa_weapon_behavior_instance_result qa_app_sim_rerelease_weapon_behavior_source_attach(qa_app_sim_rerelease_weapon_behavior_source *source, qa_weapon_behavior_launch launch);
qa_weapon_behavior_instance qa_app_sim_rerelease_weapon_behavior_source_resume(qa_app_sim_rerelease_weapon_behavior_source *source, qa_actor_id actor);
/* The checkpoint's game/level native bytes and lists are owned; free with _checkpoint_free. */
qa_app_sim_rerelease_weapon_behavior_checkpoint qa_app_sim_rerelease_weapon_behavior_source_checkpoint(
    qa_app_sim_rerelease_weapon_behavior_source *source, void (*next_frame)(void *ctx), void *frame_ctx);
void qa_app_sim_rerelease_weapon_behavior_checkpoint_free(qa_app_sim_rerelease_weapon_behavior_checkpoint *checkpoint);
/* resolve maps saved actors to owned actors (TS restore's actor callback); NULL uses the session registry's "checkpoint" domain. */
void qa_app_sim_rerelease_weapon_behavior_source_restore_with(qa_app_sim_rerelease_weapon_behavior_source *source,
    qa_app_sim_rerelease_weapon_behavior_checkpoint checkpoint, void *resolve_ctx, qa_owned_actor (*resolve)(void *ctx, qa_saved_actor_id saved),
    void (*next_frame)(void *ctx), void *frame_ctx);
void qa_app_sim_rerelease_weapon_behavior_source_restore(qa_app_sim_rerelease_weapon_behavior_source *source,
    qa_app_sim_rerelease_weapon_behavior_checkpoint checkpoint, void (*next_frame)(void *ctx), void *frame_ctx);
void qa_app_sim_rerelease_weapon_behavior_source_close(qa_app_sim_rerelease_weapon_behavior_source *source);
#endif
