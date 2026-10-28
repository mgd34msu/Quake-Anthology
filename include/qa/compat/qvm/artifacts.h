/*
 * Port of src/compat/qvm/artifacts.ts. QvmRole/QvmHost are the
 * landed qa_qvm_role / qa_qvm_host_fn (syscalls.h).
 * Artifact bridge: TS has ONE ResolvedQvmArtifact; the C compat
 * slices split it into qa_resolved_qvm_artifact (owner: this file,
 * holds the owned qa_qvm_image) and qa_qvmy_artifact (module.h:
 * the same bytecode artifact flattened to borrowed image extents).
 * qa_qvm_artifact_bridge is the lossless bytecode view of the
 * former in the latter shape: every extent/instruction field maps
 * 1:1 (operands widen int32 -> int64), data borrows the resolved
 * image, and the converted instruction array is bridge-owned.
 * The "typescript" kind has no image and cannot bridge (TS
 * qvm-mod.ts rejects it before validateQvmMod).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_ARTIFACTS_H
#define QA_COMPAT_QVM_ARTIFACTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"

typedef struct qa_known_qvm_artifact {
  qa_qvm_role role;
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

typedef struct qa_qvm_replacement {
  qa_known_qvm_artifact artifact;
  qa_provider_id implementation;
  void *ctx;
  qa_qvm_replacement_instance (*create)(void *ctx, qa_module_identity module, qa_qvm_host_fn host,
                                        void *host_ctx);
} qa_qvm_replacement;

/* TS ResolvedQvmArtifact union on kind ("bytecode" | "typescript"). */
typedef struct qa_resolved_qvm_artifact {
  bool is_typescript;
  bool has_abi_profile;
  qa_qvm_abi_profile abi_profile;
  qa_module_identity module;
  qa_qvm_role role;
  const qa_known_qvm_artifact *known; /* borrowed, NULL for TS null */
  qa_qvm_image image;                 /* owned, bytecode kind */
  qa_qvm_replacement replacement;     /* typescript kind */
} qa_resolved_qvm_artifact;

typedef struct qa_qvm_resolve_artifact_options {
  qa_module_identity module;
  qa_qvm_role role;
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

/* Lossless bytecode bridge (see header note). The view borrows the
 * resolved artifact's image data; keep the resolved artifact alive
 * while the bridge is in use. */
typedef struct qa_qvm_artifact_bridge {
  qa_qvmy_region_instruction *instructions; /* owned conversion */
  qa_qvmy_artifact view;
} qa_qvm_artifact_bridge;
/* False for the typescript kind or on OOM. */
bool qa_qvm_artifact_bridge_init(qa_qvm_artifact_bridge *bridge,
                                 const qa_resolved_qvm_artifact *artifact);
void qa_qvm_artifact_bridge_destroy(qa_qvm_artifact_bridge *bridge);

#endif
