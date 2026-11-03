/*
 * Port of src/compat/qvm/cvar-syscalls.ts. QvmCvarServices is a Pick
 * of CvarRegistry, which is qa_cvar_registry in C.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CVAR_SYSCALLS_H
#define QA_COMPAT_QVM_CVAR_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/core/cvars/index.h"

typedef qa_cvar_registry qa_qvm_cvar_services;

/* TS QvmCvarServices as an interface: Pick<CvarRegistry, "bindVm" | "readVm" |
 * "get" | "set" | "setValue" | "reset" | "register" | "infoString">. Snapshot
 * outs follow the registry convention (true + owned snapshot, freed by the
 * syscall with qa_cvar_read_free). register has_flags false is the TS omitted
 * flags; info_string maximum_length < 0 is the TS default. A concrete
 * registry is one implementation (qa_qvm_registry_cvar_table). */
typedef struct qa_qvm_cvar_table {
  void *ctx;
  bool (*get)(void *ctx, const char *name, qa_cvar_snapshot *out);
  bool (*set)(void *ctx, const char *name, const char *value, bool force, qa_cvar_snapshot *out);
  bool (*set_value)(void *ctx, const char *name, double value, qa_cvar_snapshot *out);
  bool (*reset)(void *ctx, const char *name, bool force, qa_cvar_snapshot *out);
  bool (*register_cvar)(void *ctx, const char *name, const char *value, bool has_flags,
                        double flags, qa_cvar_snapshot *out);
  double (*bind_vm)(void *ctx, const char *name, const char *value, double flags);
  bool (*read_vm)(void *ctx, double handle, qa_cvar_snapshot *out);
  /* Malloc'd result. */
  char *(*info_string)(void *ctx, double flags, double maximum_length);
} qa_qvm_cvar_table;

/* The registry as QvmCvarServices (ctx = the registry). */
qa_qvm_cvar_table qa_qvm_registry_cvar_table(qa_cvar_registry *cvars);

/* TS qvmCvarSyscall over any QvmCvarServices. */
bool qa_qvm_cvar_table_syscall(qa_qvm_role role, const uint8_t *words, size_t words_length,
                               const qa_qvm_game_memory *memory, const qa_qvm_cvar_table *cvars,
                               qa_qvm_host_result *out);

/* TS qvmCvarSyscall(role, words, memory, cvars): number|null.
 * `words` is the live syscall word area (DataView). */
bool qa_qvm_cvar_syscall(qa_qvm_role role, const uint8_t *words, size_t words_length,
                         const qa_qvm_game_memory *memory, qa_qvm_cvar_services *cvars,
                         qa_qvm_host_result *out);

#endif
