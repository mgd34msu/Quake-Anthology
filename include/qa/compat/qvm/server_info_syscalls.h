/* Port of src/compat/qvm/server-info-syscalls.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_SERVER_INFO_SYSCALLS_H
#define QA_COMPAT_QVM_SERVER_INFO_SYSCALLS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/core/cvars/index.h"

/* TS QvmServerInformationServices. configstrings get returns a
 * borrowed string; set is synchronous (TS Promise flattened). */
typedef struct qa_qvm_server_information_services {
  qa_qvm_abi_profile abi_profile;
  qa_cvar_registry *cvars;
  void *configstrings_ctx;
  const char *(*configstring_get)(void *ctx, int32_t index);
  void (*configstring_set)(void *ctx, int32_t index, const char *value);
} qa_qvm_server_information_services;

bool qa_qvm_server_information_syscall(const qa_qvm_host_call *call,
                                       const qa_qvm_server_information_services *services, int32_t *out);

#endif
