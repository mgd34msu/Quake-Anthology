/* Port of src/compat/qvm/common-syscalls.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_COMMON_SYSCALLS_H
#define QA_COMPAT_QVM_COMMON_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/cvar_syscalls.h"
#include "qa/compat/qvm/syscalls.h"

typedef struct qa_qvm_calendar {
  double second, minute, hour, day, month, year, weekday, year_day, is_dst;
} qa_qvm_calendar;

typedef struct qa_qvm_console_commands {
  void *ctx;
  /* text NULL is the TS null argument. cgame uses append only. */
  void (*execute_now)(void *ctx, const char *text);
  void (*insert)(void *ctx, const char *text);
  void (*append)(void *ctx, const char *text);
  /* cgame register/remove/reliable. */
  void (*register_command)(void *ctx, const char *name);
  void (*remove_command)(void *ctx, const char *name);
  void (*reliable)(void *ctx, const char *text);
} qa_qvm_console_commands;

/* TS QvmCommonServices union on role. real_time's output callback is
 * has_output + out_calendar: the service fills the calendar and the
 * dispatcher writes it (TS writes inside the callback). */
typedef struct qa_qvm_common_services {
  qa_qvm_role role;
  qa_qvm_cvar_services *cvars;
  void *ctx;
  void (*print)(void *ctx, const char *text);
  double (*milliseconds)(void *ctx);
  const char *const *(*arguments)(void *ctx, size_t *out_count);
  qa_qvm_console_commands commands;
  double (*real_time)(void *ctx, bool has_output, qa_qvm_calendar *out_calendar);
} qa_qvm_common_services;

bool qa_qvm_common_syscall(const qa_qvm_host_call *call, qa_qvm_common_services services,
                           qa_qvm_host_result *out);

#endif
