/* Port of src/app/bootstrap/simulation/quakec-weapon-behavior.ts.
 * The source exposes the landed qa_weapon_behavior_source shape;
 * donor QC execution runs through the host bundle (compat/qc machine
 * wiring is caller-supplied). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_WEAPON_BEHAVIOR_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_WEAPON_BEHAVIOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/contracts/session.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"
typedef struct qa_app_sim_qc_weapon_target {
  qa_actor_id actor; qa_body_state body; double health;
  const char *kind; /* "actor"|"player" (TS union member where present) */
} qa_app_sim_qc_weapon_target;
typedef struct qa_app_sim_qc_machine_snapshot {
  const uint8_t *globals; size_t globals_length;
  const uint8_t *entities; size_t entities_length;
  double entity_count;
  const uint8_t *strings; size_t strings_length;
  double statement, function_index, argument_count;
  double *profiling; size_t profiling_count;
  bool trace_enabled;
} qa_app_sim_qc_machine_snapshot;
typedef struct qa_app_sim_qc_weapon_binding { double slot; qa_saved_actor_id actor; const char *kind; } qa_app_sim_qc_weapon_binding;
typedef struct qa_app_sim_quakec_weapon_behavior_checkpoint {
  double version; /* 1 */
  qa_weapon_behavior_definition definition;
  const char *mode;
  double source_time;
  qa_app_sim_source_random_state random;
  qa_app_sim_qc_machine_snapshot machine;
  double *free_slots; size_t free_count;
  double *retired; size_t retired_count;
  qa_app_sim_qc_weapon_binding *bindings; size_t binding_count;
} qa_app_sim_quakec_weapon_behavior_checkpoint;
typedef struct qa_app_sim_quakec_weapon_behavior_source qa_app_sim_quakec_weapon_behavior_source;
typedef struct qa_app_sim_qc_weapon_host {
  void *ctx;
  qa_weapon_behavior_instance_result (*attach)(void *ctx, qa_weapon_behavior_launch launch);
  qa_app_sim_quakec_weapon_behavior_checkpoint (*checkpoint)(void *ctx);
  void (*restore)(void *ctx, qa_app_sim_quakec_weapon_behavior_checkpoint checkpoint);
} qa_app_sim_qc_weapon_host;
qa_app_sim_quakec_weapon_behavior_source *qa_app_sim_quakec_weapon_behavior_source_create(
    qa_weapon_behavior_definition definition, qa_app_sim_qc_weapon_host host);
void qa_app_sim_quakec_weapon_behavior_source_destroy(qa_app_sim_quakec_weapon_behavior_source *source);
qa_weapon_behavior_definition qa_app_sim_quakec_weapon_behavior_source_definition(const qa_app_sim_quakec_weapon_behavior_source *source);
qa_weapon_behavior_source qa_app_sim_quakec_weapon_behavior_source_as_source(qa_app_sim_quakec_weapon_behavior_source *source);
qa_app_sim_quakec_weapon_behavior_checkpoint qa_app_sim_quakec_weapon_behavior_source_checkpoint(qa_app_sim_quakec_weapon_behavior_source *source);
void qa_app_sim_quakec_weapon_behavior_source_restore(qa_app_sim_quakec_weapon_behavior_source *source,
    qa_app_sim_quakec_weapon_behavior_checkpoint checkpoint,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx,
    qa_app_sim_source_random *random);
qa_app_sim_quakec_weapon_behavior_checkpoint qa_app_sim_read_quakec_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected);
#endif
