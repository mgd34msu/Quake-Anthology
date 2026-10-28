/* Port of src/app/bootstrap/simulation/quakec-source.ts. Program
 * loading runs through the landed compat/qc loader; the id1/QW
 * source game execution runs through the host bundle (the QC
 * machine and content bindings are caller-constructed). prepare*
 * mount probings named in the TS run through host callbacks.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/qc/program.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
typedef struct qa_app_sim_prepared_quakec_source {
  qa_execution_module execution;
  qa_compat_qc_program program; /* owned; free via qa_compat_qc_program_free */
  const char *kind; /* "netquake"|"quakeworld" */
  const void *resources; /* PreparedQuakeCSource["resources"] (opaque) */
} qa_app_sim_prepared_quakec_source;
typedef struct qa_app_sim_quakec_prepare_host {
  void *ctx;
  /* mounts.read(artifact): malloc'd or NULL. */
  bool (*read_artifact)(void *ctx, const qa_resolved_resource_reference *artifact, uint8_t **out, size_t *out_length);
  /* prepareQuakeCResources probing (wav/model/bsp mounts). */
  const void *(*prepare_resources)(void *ctx, qa_compat_qc_program *program, const char *entity_text);
} qa_app_sim_quakec_prepare_host;
/* Synchronous forms of the TS async prepares. */
qa_app_sim_prepared_quakec_source qa_app_sim_prepare_quakec_source(qa_execution_module execution,
    const char *entity_text, qa_app_sim_quakec_prepare_host host);
const void *qa_app_sim_prepare_quakec_resources(qa_compat_qc_program *program, const char *entity_text,
    qa_app_sim_quakec_prepare_host host);
typedef struct qa_app_sim_quakec_source qa_app_sim_quakec_source;
typedef struct qa_app_sim_netquake_source_game qa_app_sim_netquake_source_game;
typedef struct qa_app_sim_quakeworld_source_game qa_app_sim_quakeworld_source_game;
typedef struct qa_app_sim_quakec_source_host {
  void *ctx;
  const void *world;      /* ApplicationWorld subset (opaque) */
  const void *simulation; /* SharedSimulation */
  /* Per-kind source game construction (compat/qc machine + id1
   * bindings are caller-built; see unresolved note). */
  qa_app_sim_netquake_source_game *(*create_netquake)(void *ctx, qa_app_sim_quakec_source *source);
  qa_app_sim_quakeworld_source_game *(*create_quakeworld)(void *ctx, qa_app_sim_quakec_source *source);
} qa_app_sim_quakec_source_host;
qa_app_sim_quakec_source *qa_app_sim_quakec_source_create(qa_app_sim_prepared_quakec_source prepared,
    qa_app_sim_quakec_source_host host);
void qa_app_sim_quakec_source_destroy(qa_app_sim_quakec_source *source);
const char *qa_app_sim_quakec_source_kind(qa_app_sim_quakec_source *source);
qa_compat_qc_program *qa_app_sim_quakec_source_program(qa_app_sim_quakec_source *source);
qa_execution_module qa_app_sim_quakec_source_execution(qa_app_sim_quakec_source *source);
qa_app_sim_netquake_source_game *qa_app_sim_quakec_source_netquake(qa_app_sim_quakec_source *source);
qa_app_sim_quakeworld_source_game *qa_app_sim_quakec_source_quakeworld(qa_app_sim_quakec_source *source);
#endif
