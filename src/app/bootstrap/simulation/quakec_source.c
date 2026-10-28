/* Port of src/app/bootstrap/simulation/quakec-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
const void *qa_app_sim_prepare_quakec_resources(qa_compat_qc_program *program, const char *entity_text,
    qa_app_sim_quakec_prepare_host host) {
  if (!host.prepare_resources) return NULL;
  return host.prepare_resources(host.ctx, program, entity_text ? entity_text : "");
}
qa_app_sim_prepared_quakec_source qa_app_sim_prepare_quakec_source(qa_execution_module execution,
    const char *entity_text, qa_app_sim_quakec_prepare_host host) {
  if (execution.kind != QA_EXECUTION_MODULE_QUAKEC)
    qa_range_error("QuakeC preparation requires a QuakeC execution module");
  const qa_resolved_resource_reference *artifact = execution.artifact;
  uint8_t *bytes = NULL; size_t length = 0;
  if (!host.read_artifact(host.ctx, artifact, &bytes, &length) || bytes == NULL)
    qa_range_error("Selected QuakeC program is not mounted");
  qa_app_sim_prepared_quakec_source out; memset(&out, 0, sizeof out);
  out.execution = execution;
  if (!qa_compat_qc_load_program(bytes, length, NULL, NULL, &out.program)) qa_range_error("Out of memory");
  free(bytes);
  out.kind = (execution.api.kind == QA_GAME_API_QUAKEC && execution.api.as.quakec.kind == QA_QUAKEC_API_Q1_QUAKEWORLD)
      ? "quakeworld" : "netquake";
  out.resources = qa_app_sim_prepare_quakec_resources(&out.program, entity_text, host);
  (void)is;
  return out;
}
struct qa_app_sim_quakec_source {
  qa_app_sim_prepared_quakec_source prepared;
  qa_app_sim_quakec_source_host host;
  qa_app_sim_netquake_source_game *netquake;
  qa_app_sim_quakeworld_source_game *quakeworld;
};
qa_app_sim_quakec_source *qa_app_sim_quakec_source_create(qa_app_sim_prepared_quakec_source prepared,
    qa_app_sim_quakec_source_host host) {
  qa_app_sim_quakec_source *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  s->prepared = prepared; s->host = host;
  if (is(prepared.kind, "quakeworld")) s->quakeworld = host.create_quakeworld(host.ctx, s);
  else s->netquake = host.create_netquake(host.ctx, s);
  return s;
}
void qa_app_sim_quakec_source_destroy(qa_app_sim_quakec_source *source) { free(source); }
const char *qa_app_sim_quakec_source_kind(qa_app_sim_quakec_source *source) { return source->prepared.kind; }
qa_compat_qc_program *qa_app_sim_quakec_source_program(qa_app_sim_quakec_source *source) { return &source->prepared.program; }
qa_execution_module qa_app_sim_quakec_source_execution(qa_app_sim_quakec_source *source) { return source->prepared.execution; }
qa_app_sim_netquake_source_game *qa_app_sim_quakec_source_netquake(qa_app_sim_quakec_source *source) { return source->netquake; }
qa_app_sim_quakeworld_source_game *qa_app_sim_quakec_source_quakeworld(qa_app_sim_quakec_source *source) { return source->quakeworld; }
