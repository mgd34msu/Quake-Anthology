/*
 * Port of src/compat/qvm/artifacts.ts. QvmRole/QvmHost mirror
 * syscalls.ts (qvm-b slice) as qa_qvmx_role / qa_qvmx_host.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_ARTIFACTS_H
#define QA_COMPAT_QVM_ARTIFACTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"

typedef struct qa_known_qvm_artifact {
  qa_qvmx_role role;
  const char *product; /* "baseq3" | "missionpack" */
  const char *reference_package;
  const char *related_game_build_date;
  double byte_length;
  qa_content_digest digest;
} qa_known_qvm_artifact;

extern const qa_known_qvm_artifact qa_known_qvm_artifacts[6];
#define QA_KNOWN_QVM_ARTIFACT_COUNT 6

typedef struct qa_qvm_replacement_instance {
  void *ctx;
  double (*invoke)(void *ctx, qa_qvm_interpreter_arguments arguments);
  void (*shutdown)(void *ctx);
} qa_qvm_replacement_instance;

/* Mirror of syscalls.ts QvmHost (qvm-b slice). */
typedef struct qa_qvmx_host {
  void *ctx;
  double (*call)(void *ctx, void *host_call);
} qa_qvmx_host;

typedef struct qa_qvm_replacement {
  qa_known_qvm_artifact artifact;
  qa_provider_id implementation;
  void *ctx;
  qa_qvm_replacement_instance (*create)(void *ctx, qa_module_identity module, qa_qvmx_host host);
} qa_qvm_replacement;

/* TS ResolvedQvmArtifact union on kind ("bytecode" | "typescript"). */
typedef struct qa_resolved_qvm_artifact {
  bool is_typescript;
  bool has_abi_profile;
  qa_qvm_abi_profile abi_profile;
  qa_module_identity module;
  qa_qvmx_role role;
  const qa_known_qvm_artifact *known; /* borrowed, NULL for TS null */
  qa_qvm_image image;                 /* owned, bytecode kind */
  qa_qvm_replacement replacement;     /* typescript kind */
} qa_resolved_qvm_artifact;

typedef struct qa_qvm_resolve_artifact_options {
  qa_module_identity module;
  qa_qvmx_role role;
  const uint8_t *bytes;
  size_t length;
  const qa_qvm_replacement *replacements; /* optional */
  size_t replacement_count;
  bool has_abi_profile;
  qa_qvm_abi_profile abi_profile;
} qa_qvm_resolve_artifact_options;

/* False only on OOM. Caller destroys `out` with qa_resolved_qvm_artifact_destroy. */
bool qa_qvm_resolve_artifact(qa_qvm_resolve_artifact_options options,
                             qa_resolved_qvm_artifact *out);
void qa_resolved_qvm_artifact_destroy(qa_resolved_qvm_artifact *artifact);

#endif
