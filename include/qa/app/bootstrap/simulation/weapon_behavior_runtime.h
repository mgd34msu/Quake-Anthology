/* Port of src/app/bootstrap/simulation/weapon-behavior-runtime.ts.
 * Async loading is pumped synchronously through next_frame
 * callbacks (PORTING.md). QuakeC sources are constructed here, as TS
 * does; the qvm and native ones through host.create_qvm / create_native
 * (TS host.qvm / host.native). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_WEAPON_BEHAVIOR_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_WEAPON_BEHAVIOR_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/quakec_weapon_behavior.h"
#include "qa/app/bootstrap/simulation/qvm_weapon_behavior.h"
#include "qa/app/bootstrap/simulation/rerelease_weapon_behavior.h"
#include "qa/app/bootstrap/weapon_behavior_selection.h"
#include "qa/contracts/scene.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/registry.h"
#include "qa/persistence/native_weapon.h"
#include "qa/world/gameplay/weapon_behaviors.h"
typedef struct qa_app_sim_weapon_behavior_runtime_checkpoint {
  double version; /* 1 */
  qa_save_value *value; /* owned tree form (sources + attachments) */
} qa_app_sim_weapon_behavior_runtime_checkpoint;
typedef struct qa_app_sim_weapon_behavior_runtime_host {
  qa_session_actor_registry *actors;
  qa_scene_queries scene;
  const char *mode;
  double seed;
  void *ctx;
  const qa_app_sim_qc_weapon_target *(*targets)(void *ctx, size_t *out_count);
  qa_vec3 (*aim)(void *ctx, qa_actor_id shooter, double speed);
  void (*print)(void *ctx, const qa_actor_id *recipient, const char *text);
  qa_app_sim_qvm_weapon_behavior_source *(*create_qvm)(void *ctx, qa_app_prepared_weapon_behavior entry);
  qa_app_sim_rerelease_weapon_behavior_source *(*create_native)(void *ctx, qa_app_prepared_weapon_behavior entry,
      void (*next_frame)(void *frame_ctx), void *frame_ctx);
  const qa_native_weapon_profiles *profiles;
} qa_app_sim_weapon_behavior_runtime_host;
typedef struct qa_app_sim_simulation_weapon_behaviors qa_app_sim_simulation_weapon_behaviors;
qa_app_sim_simulation_weapon_behaviors *qa_app_sim_simulation_weapon_behaviors_create(
    const qa_app_prepared_weapon_behavior *prepared, size_t prepared_count,
    qa_app_sim_weapon_behavior_runtime_host host);
void qa_app_sim_simulation_weapon_behaviors_destroy(qa_app_sim_simulation_weapon_behaviors *behaviors);
void qa_app_sim_simulation_weapon_behaviors_initialize_loading(qa_app_sim_simulation_weapon_behaviors *behaviors,
    void (*next_frame)(void *ctx), void *frame_ctx);
qa_weapon_trajectory_update_result qa_app_sim_simulation_weapon_behaviors_launch(
    qa_app_sim_simulation_weapon_behaviors *behaviors, qa_weapon_behavior_launch input);
bool qa_app_sim_simulation_weapon_behaviors_controls_trajectory(qa_app_sim_simulation_weapon_behaviors *behaviors, qa_actor_id projectile);
qa_weapon_trajectory_update_result qa_app_sim_simulation_weapon_behaviors_step(
    qa_app_sim_simulation_weapon_behaviors *behaviors, qa_owned_actor projectile, qa_body_state body, double time_seconds);
/* Checkpoint trees are built from each source checkpoint via the
 * host checkpoint structs; see .c note on serialization. */
qa_app_sim_weapon_behavior_runtime_checkpoint qa_app_sim_simulation_weapon_behaviors_checkpoint(qa_app_sim_simulation_weapon_behaviors *behaviors);
qa_app_sim_weapon_behavior_runtime_checkpoint qa_app_sim_simulation_weapon_behaviors_checkpoint_loading(
    qa_app_sim_simulation_weapon_behaviors *behaviors, void (*next_frame)(void *ctx), void *frame_ctx);
void qa_app_sim_simulation_weapon_behaviors_restore(qa_app_sim_simulation_weapon_behaviors *behaviors, const qa_save_reader *reader);
void qa_app_sim_simulation_weapon_behaviors_close(qa_app_sim_simulation_weapon_behaviors *behaviors);
#endif
