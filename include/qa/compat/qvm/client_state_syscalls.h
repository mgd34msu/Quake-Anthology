/*
 * Port of src/compat/qvm/client-state-syscalls.ts.
 * qa_qvmx_snapshot_source mirrors content/q3/presentation/
 * snapshots.ts SnapshotSource. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_STATE_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_STATE_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/client_state.h"
#include "qa/compat/qvm/client_state_record.h"
#include "qa/compat/qvm/syscalls.h"

typedef struct qa_qvmx_snapshot_current { double number, server_time; } qa_qvmx_snapshot_current;

typedef struct qa_qvmx_snapshot_source {
  void *ctx;
  qa_qvmx_snapshot_current (*current)(void *ctx);
  /* TS read(number): Snapshot|null. */
  bool (*read)(void *ctx, double number, qa_qvmx_snapshot *out);
} qa_qvmx_snapshot_source;

typedef struct qa_qvm_client_state_services {
  qa_q3_client_state connection;
  qa_qvmx_snapshot_source snapshots;
  void *ctx;
  bool (*snapshot_ping)(void *ctx, double number, double *out);
  /* TS Promise<readonly string[]|null>, synchronous in C. */
  bool (*get_server_command)(void *ctx, double number, const char *const **out_tokens,
                             size_t *out_count);
  void (*set_user_command_value)(void *ctx, double weapon, double sensitivity);
  /* Optional TS userCommand? override; NULL uses connection.commands.read. */
  bool (*user_command)(void *ctx, double number, qa_qvmx_wire_user_command *out);
} qa_qvm_client_state_services;

bool qa_qvm_client_state_syscall(const qa_qvm_host_call *call,
                                 qa_qvm_client_state_services services, qa_qvm_host_result *out);

#endif
