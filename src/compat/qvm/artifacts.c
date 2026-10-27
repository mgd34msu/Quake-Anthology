/* Port of src/compat/qvm/artifacts.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/artifacts.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/mounts.h"
#include "qa/foundation/support.h"

const qa_known_qvm_artifact qa_known_qvm_artifacts[6] = {
    {"ui", "baseq3", "baseq3/pak8", "2002-09-30", 278308,
     "sha256:3a6fd12b889f5d35df20a09b51bf8eca46966d014be55ffad38ddc2ffb38c807"},
    {"cgame", "baseq3", "baseq3/pak8", "2002-09-30", 325220,
     "sha256:4ea18569bf56a282d26dc89eb9efcc5eedbe0b69c10182fc38446174c1e55b49"},
    {"qagame", "baseq3", "baseq3/pak8", "2002-09-30", 469796,
     "sha256:57c52bf22e4f528c064f8af1553a7103723bab0a02276bb11eed944bf829b219"},
    {"ui", "missionpack", "missionpack/pak0", "2000-12-04", 272040,
     "sha256:7b157f32acdb21a3904d078296672ed2d32195c5b7a206922f6f7d33c6c40e40"},
    {"cgame", "missionpack", "missionpack/pak0", "2000-12-04", 442304,
     "sha256:09d0b6eb41ea623d67031d2d7a73058ccb3bc6556ec044ead529d48b58d15f4c"},
    {"qagame", "missionpack", "missionpack/pak0", "2000-12-04", 547700,
     "sha256:da041f17f296feeaf8269eabc9062cefdecddfd24ff4d84eb291902e527d1d8a"},
};

bool qa_qvm_resolve_artifact(qa_qvm_resolve_artifact_options options,
                             qa_resolved_qvm_artifact *out) {
  char *digest = qa_digest_bytes(options.bytes, options.length);
  if (digest == NULL) return false;
  if (strcmp(options.module.digest, digest) != 0) {
    free(digest);
    qa_range_error("QVM artifact bytes do not match their module identity");
  }
  const qa_known_qvm_artifact *known = NULL;
  for (size_t i = 0; i < QA_KNOWN_QVM_ARTIFACT_COUNT; i++) {
    if (strcmp(qa_known_qvm_artifacts[i].digest, digest) == 0 &&
        qa_known_qvm_artifacts[i].byte_length == (double)options.length) {
      known = &qa_known_qvm_artifacts[i];
      break;
    }
  }
  if (known != NULL && strcmp(known->role, options.role) != 0) {
    const char *known_role = known->role;
    free(digest);
    qa_range_error("QVM artifact is %s, requested %s", known_role, options.role);
  }
  const qa_qvm_replacement *replacement = NULL;
  for (size_t i = 0; i < options.replacement_count; i++) {
    const qa_qvm_replacement *entry = &options.replacements[i];
    if (strcmp(entry->artifact.digest, digest) == 0 &&
        strcmp(entry->artifact.role, options.role) == 0 &&
        entry->artifact.byte_length == (double)options.length) {
      replacement = entry;
      break;
    }
  }
  free(digest);
  memset(out, 0, sizeof *out);
  out->module = options.module;
  out->role = options.role;
  out->known = known;
  if (replacement != NULL) {
    out->is_typescript = true;
    out->replacement = *replacement;
    return true;
  }
  out->is_typescript = false;
  out->has_abi_profile = true;
  out->abi_profile = options.has_abi_profile ? options.abi_profile : "q3-modern";
  return qa_qvm_parse(options.bytes, options.length, options.module.artifact_path, &out->image);
}

void qa_resolved_qvm_artifact_destroy(qa_resolved_qvm_artifact *artifact) {
  if (artifact == NULL) return;
  if (!artifact->is_typescript) qa_qvm_image_destroy(&artifact->image);
}
