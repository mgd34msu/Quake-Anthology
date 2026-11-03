/*
 * Port of src/compat/qvm/legacy-client-abi.ts. The returned argv is a
 * malloc'd array of malloc'd strings the caller frees with
 * qa_qvm_legacy_client_command_free; *out_count receives its length.
 * NULL (with *out_count 0) is the TS null: a non-integer index or a
 * configstring with no legacy client mapping.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_LEGACY_CLIENT_ABI_H
#define QA_COMPAT_QVM_LEGACY_CLIENT_ABI_H

#include <stddef.h>

#include "qa/contracts/execution.h"

char **qa_qvm_legacy_client_command(const char *const *argv, size_t count, qa_qvm_abi_profile profile, size_t *out_count);
void qa_qvm_legacy_client_command_free(char **argv, size_t count);

#endif
