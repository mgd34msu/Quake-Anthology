/*
 * Port of src/compat/qvm/compatibility.ts. The TS SaveReader|null
 * fields become borrowed qa_save_value subtrees (NULL when absent);
 * build a qa_save_reader over one with path "qvm-compatibility.json"
 * to reproduce its diagnostics. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_COMPATIBILITY_H
#define QA_COMPAT_QVM_COMPATIBILITY_H

#include <stdbool.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/content/mounts.h"
#include "qa/contracts/execution.h"
#include "qa/persistence/value.h"

typedef struct qa_qvm_compatibility_declaration {
  qa_qvm_abi_profile profile;
  const qa_save_value *primary;               /* borrowed */
  const qa_save_value *equipment_presentation; /* borrowed */
} qa_qvm_compatibility_declaration;

typedef struct qa_qvm_module_path_digest {
  const char *artifact_path;
  qa_content_digest digest;
} qa_qvm_module_path_digest;

typedef struct qa_qvm_compatibility_reading {
  qa_qvm_compatibility_declaration declaration;
  const qa_resolved_resource_reference *resource; /* borrowed, NULL when absent */
  qa_save_value *parsed;                           /* owned tree keeping borrows alive */
} qa_qvm_compatibility_reading;

/* False only on OOM. Free with qa_qvm_compatibility_reading_destroy. */
bool qa_qvm_read_compatibility_declaration(qa_content_mounted *mounts,
                                           qa_qvm_module_path_digest module, qa_qvm_role role,
                                           qa_qvm_compatibility_reading *out);
void qa_qvm_compatibility_reading_destroy(qa_qvm_compatibility_reading *reading);
bool qa_qvm_read_compatibility(qa_content_mounted *mounts, qa_qvm_module_path_digest module,
                               qa_qvm_role role, qa_qvm_abi_profile *out);
qa_qvm_abi_profile qa_qvm_parse_compatibility(const qa_save_value *value,
                                              qa_qvm_module_path_digest module, qa_qvm_role role);
qa_qvm_compatibility_declaration qa_qvm_parse_compatibility_declaration(
    const qa_save_value *value, qa_qvm_module_path_digest module, qa_qvm_role role);

#endif
