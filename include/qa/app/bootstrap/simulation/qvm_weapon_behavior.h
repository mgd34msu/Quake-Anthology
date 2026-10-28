/* Port of src/app/bootstrap/simulation/qvm-weapon-behavior.ts.
 * Source VM execution runs through the host bundle. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QVM_WEAPON_BEHAVIOR_H
#define QA_APP_BOOTSTRAP_SIMULATION_QVM_WEAPON_BEHAVIOR_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"
typedef struct qa_app_sim_qvm_weapon_target {
  qa_actor_id actor; qa_body_state body; double health;
  const char *kind; const char *userinfo; const char *team;
} qa_app_sim_qvm_weapon_target;
typedef struct qa_app_sim_qvm_weapon_binding { qa_saved_actor_id actor; double pointer; const char *kind; } qa_app_sim_qvm_weapon_binding;
typedef struct qa_app_sim_qvm_weapon_retired { qa_saved_actor_id actor; qa_vec3 origin, velocity, angles; } qa_app_sim_qvm_weapon_retired;
typedef struct qa_app_sim_qvm_weapon_behavior_checkpoint {
  double version;
  qa_weapon_behavior_definition definition;
  qa_guest_checkpoint module;
  const char *profile_digest;
  qa_saved_actor_id *activated; size_t activated_count;
  qa_app_sim_qvm_weapon_binding *bindings; size_t binding_count;
  qa_app_sim_qvm_weapon_retired *retired; size_t retired_count;
} qa_app_sim_qvm_weapon_behavior_checkpoint;
typedef struct qa_app_sim_qvm_weapon_behavior_source qa_app_sim_qvm_weapon_behavior_source;
typedef struct qa_app_sim_qvm_weapon_host {
  void *ctx;
  qa_weapon_behavior_instance_result (*attach)(void *ctx, qa_weapon_behavior_launch launch);
  qa_app_sim_qvm_weapon_behavior_checkpoint (*checkpoint)(void *ctx);
  void (*restore)(void *ctx, qa_app_sim_qvm_weapon_behavior_checkpoint checkpoint);
  void (*close)(void *ctx);
} qa_app_sim_qvm_weapon_host;
qa_app_sim_qvm_weapon_behavior_source *qa_app_sim_qvm_weapon_behavior_source_create(
    qa_weapon_behavior_definition definition, qa_app_sim_qvm_weapon_host host);
void qa_app_sim_qvm_weapon_behavior_source_destroy(qa_app_sim_qvm_weapon_behavior_source *source);
qa_weapon_behavior_definition qa_app_sim_qvm_weapon_behavior_source_definition(const qa_app_sim_qvm_weapon_behavior_source *source);
qa_weapon_behavior_source qa_app_sim_qvm_weapon_behavior_source_as_source(qa_app_sim_qvm_weapon_behavior_source *source);
qa_app_sim_qvm_weapon_behavior_checkpoint qa_app_sim_qvm_weapon_behavior_source_checkpoint(qa_app_sim_qvm_weapon_behavior_source *source);
void qa_app_sim_qvm_weapon_behavior_source_restore(qa_app_sim_qvm_weapon_behavior_source *source,
    qa_app_sim_qvm_weapon_behavior_checkpoint checkpoint);
void qa_app_sim_qvm_weapon_behavior_source_close(qa_app_sim_qvm_weapon_behavior_source *source);
qa_app_sim_qvm_weapon_behavior_checkpoint qa_app_sim_read_qvm_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected);
/* Malloc'd entity text; caller frees. */
char *qa_app_sim_qvm_weapon_initialization_entities(const char *text);
#endif
