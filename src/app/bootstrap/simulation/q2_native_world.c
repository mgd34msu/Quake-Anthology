/* Port of src/app/bootstrap/simulation/q2-native-world.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q2_native_world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/guest/pe/format.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
void *qa_app_sim_prepare_q2_guest_mounted(const qa_resolved_execution_module *execution, qa_content_mounted *mounts,
    const char **out_edition) {
  if (!is(execution->role, "server-game")) qa_range_error("Q2 guest preparation requires a server-game artifact");
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_reference(mounts, execution->artifact, &length);
  if (bytes == NULL) qa_range_error("Native artifact could not be read");
  qa_pe_file pe; qa_parse_pe(&pe, bytes, length);
  qa_guest_native_abi abi = pe.abi;
  qa_pe_file_free(&pe);
  free(bytes);
  qa_resolved_execution_module corrected = *execution;
  corrected.profile = (qa_native_abi){abi.kind, abi.image, abi.pointer_bytes, abi.call};
  corrected.api.kind = QA_GAME_API_Q2_GAME;
  if (is(abi.kind, "windows-x86-64")) {
    corrected.api.as.q2_game.kind = QA_Q2_GAME_API_RERELEASE; corrected.api.as.q2_game.version = 2023;
    qa_app_sim_prepared_rerelease_guest *prepared = calloc(1, sizeof *prepared);
    if (prepared == NULL) qa_range_error("Out of memory");
    qa_app_sim_prepare_rerelease_guest(&corrected, mounts, prepared);
    *out_edition = "rerelease";
    return prepared;
  }
  corrected.api.as.q2_game.kind = QA_Q2_GAME_API_CLASSIC; corrected.api.as.q2_game.version = 3;
  qa_app_sim_prepared_classic_guest *prepared = calloc(1, sizeof *prepared);
  if (prepared == NULL) qa_range_error("Out of memory");
  qa_app_sim_prepare_classic_guest(&corrected, mounts, prepared);
  *out_edition = "classic";
  return prepared;
}
void qa_app_sim_prepared_q2_guest_free(void *prepared, const char *edition) {
  if (prepared == NULL) return;
  if (is(edition, "rerelease")) qa_app_sim_prepared_rerelease_guest_free(prepared);
  else qa_app_sim_prepared_classic_guest_free(prepared);
  free(prepared);
}
qa_app_sim_prepared_q2_native_guest qa_app_sim_prepare_q2_guest(qa_execution_module execution, qa_app_sim_q2_guest_prepare_host host) {
  if (!is(execution.role, "server-game")) qa_range_error("Q2 guest preparation requires a server-game artifact");
  const qa_resolved_resource_reference *artifact = execution.artifact;
  size_t length = 0;
  uint8_t *bytes = host.read_artifact(host.ctx, artifact, &length);
  qa_native_abi abi; memset(&abi, 0, sizeof abi);
  host.pe_abi(host.ctx, bytes, length, &abi);
  free(bytes);
  qa_execution_module corrected = execution;
  corrected.profile = abi;
  bool x64 = is(abi.kind, "windows-x86-64");
  if (x64) {
    corrected.api.kind = QA_GAME_API_Q2_GAME;
    corrected.api.as.q2_game.kind = QA_Q2_GAME_API_RERELEASE;
    corrected.api.as.q2_game.version = 2023;
    return host.prepare_rerelease(host.ctx, corrected);
  }
  corrected.api.kind = QA_GAME_API_Q2_GAME;
  corrected.api.as.q2_game.kind = QA_Q2_GAME_API_CLASSIC;
  corrected.api.as.q2_game.version = 3;
  return host.prepare_classic(host.ctx, corrected);
}
qa_module_identity qa_app_sim_native_module_identity(qa_app_sim_prepared_q2_native_guest prepared, char *scratch, size_t scratch_size) {
  const qa_resolved_resource_reference *artifact = prepared.execution.artifact;
  qa_module_identity out;
  out.id = prepared.execution.owner.provider;
  out.artifact_path = artifact->requested_path;
  out.digest = artifact->digest;
  if (!prepared.has_primary) out.revision = artifact->digest;
  else {
    snprintf(scratch, scratch_size, "%s/%s/%s", artifact->digest,
        prepared.primary_requested_path ? prepared.primary_requested_path : "",
        prepared.primary_digest ? prepared.primary_digest : "");
    out.revision = scratch;
  }
  return out;
}
#define PRIMARY_CACHE 32
static struct { const void *prepared; const void *profile; } primary_cache[PRIMARY_CACHE];
static size_t primary_cache_count;
const void *qa_app_sim_prepared_native_primary(qa_app_sim_prepared_q2_native_guest *prepared, qa_app_sim_q2_guest_prepare_host host) {
  if (prepared->has_primary) return prepared->primary_profile;
  for (size_t i = 0; i < primary_cache_count; i++)
    if (primary_cache[i].prepared == prepared) return primary_cache[i].profile;
  const qa_resolved_resource_reference *artifact = prepared->execution.artifact;
  const void *profile = host.builtin_primary ? host.builtin_primary(host.ctx, artifact->digest, prepared->edition) : NULL;
  if (primary_cache_count < PRIMARY_CACHE) {
    primary_cache[primary_cache_count].prepared = prepared;
    primary_cache[primary_cache_count++].profile = profile;
  }
  return profile;
}
