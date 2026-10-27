/* Port of src/compat/qvm/client-game-syscalls.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_CLIENT_GAME_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_GAME_SYSCALLS_H

#include <stdbool.h>

#include "qa/compat/qvm/client_state_record.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/execution.h"

typedef struct qa_qvm_client_game_services {
  qa_qvm_abi_profile abi_profile;
  double max_clients;
  void *ctx;
  const char *(*get_userinfo)(void *ctx, double slot);
  void (*set_userinfo)(void *ctx, double slot, const char *value);
  qa_qvmx_wire_user_command (*get_user_command)(void *ctx, double slot);
  void (*drop_client)(void *ctx, double slot, const char *reason);
  void (*send_server_command)(void *ctx, double slot, const char *text);
} qa_qvm_client_game_services;

bool qa_qvm_client_game_syscall(const qa_qvm_host_call *call,
                                qa_qvm_client_game_services services, qa_qvm_host_result *out);

#endif
