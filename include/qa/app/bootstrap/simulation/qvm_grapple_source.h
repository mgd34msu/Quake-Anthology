/* Port of src/app/bootstrap/simulation/qvm-grapple-source.ts.
 * The source VM/core runs through the host bundle; binding/tether
 * bookkeeping and checkpoint restore validation are this file's.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QVM_GRAPPLE_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_QVM_GRAPPLE_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
typedef struct qa_app_sim_qvm_grapple_checkpoint {
  double version; const char *profile;
  qa_guest_checkpoint module;
  qa_saved_actor_id *owners; size_t owner_count;
} qa_app_sim_qvm_grapple_checkpoint;
typedef struct qa_app_sim_qvm_grapple_binding { qa_saved_actor_id actor; double pointer; bool client; qa_vec3 origin; } qa_app_sim_qvm_grapple_binding;
typedef struct qa_app_sim_qvm_grapple_tether { qa_saved_actor_id owner, actor; } qa_app_sim_qvm_grapple_tether;
typedef struct qa_app_sim_qvm_grapple_source_checkpoint {
  double version;
  qa_app_sim_qvm_grapple_checkpoint grapple;
  qa_app_sim_qvm_grapple_binding *bindings; size_t binding_count;
  qa_app_sim_qvm_grapple_tether *tethers; size_t tether_count;
} qa_app_sim_qvm_grapple_source_checkpoint;
qa_app_sim_qvm_grapple_source_checkpoint qa_app_sim_read_qvm_grapple_source_checkpoint(const qa_save_reader *reader);
typedef struct qa_app_sim_qvm_grapple_source qa_app_sim_qvm_grapple_source;
typedef struct qa_app_sim_qvm_grapple_host {
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_provider_id provider;
  void *ctx;
  qa_app_sim_qvm_grapple_checkpoint (*core_capture)(void *ctx);
  void (*core_restore)(void *ctx, qa_app_sim_qvm_grapple_checkpoint checkpoint);
  void (*core_pull)(void *ctx, qa_actor_id actor, qa_vec3 forward);
  bool (*pulling)(void *ctx, qa_actor_id actor);
  void (*synchronize)(void *ctx);
  void (*close_provider)(void *ctx);
  void (*close_files)(void *ctx);
  void (*retire_vm)(void *ctx);
} qa_app_sim_qvm_grapple_host;
qa_app_sim_qvm_grapple_source *qa_app_sim_qvm_grapple_source_create(qa_app_sim_qvm_grapple_host host);
void qa_app_sim_qvm_grapple_source_destroy(qa_app_sim_qvm_grapple_source *source);
bool qa_app_sim_qvm_grapple_source_pulling(qa_app_sim_qvm_grapple_source *source, qa_actor_id actor);
void qa_app_sim_qvm_grapple_source_pull(qa_app_sim_qvm_grapple_source *source, qa_actor_id actor);
qa_app_sim_qvm_grapple_source_checkpoint qa_app_sim_qvm_grapple_source_capture(qa_app_sim_qvm_grapple_source *source);
void qa_app_sim_qvm_grapple_source_restore(qa_app_sim_qvm_grapple_source *source, qa_app_sim_qvm_grapple_source_checkpoint checkpoint);
void qa_app_sim_qvm_grapple_source_close(qa_app_sim_qvm_grapple_source *source);
#endif
