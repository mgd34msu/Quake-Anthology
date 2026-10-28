/* TS NativeQ3EngineServices seam for src/compat/q3/native/imports.ts:
 * Pick<Q3SourceHost, "engine" | "cvars" | "configstrings"> +
 * commandArguments(). This header is guest-free on purpose: the
 * landed owner header (qa/app/bootstrap/simulation/q3/types.h,
 * qa_app_q3_source_engine / qa_app_q3_source_host) cannot share a
 * TU with the guest ABI headers imports.h needs (both
 * qa/contracts/execution.h and qa/guest/abi/values.h define
 * struct qa_guest_call_context). The fn+ctx seam below is filled
 * from the landed host by qa_compat_q3_native_host_adapter_init
 * (src/compat/q3/native/imports_host.c, the TU that sees types.h).
 * GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q3_NATIVE_ENGINE_SERVICES_H
#define QA_COMPAT_Q3_NATIVE_ENGINE_SERVICES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_app_q3_source_host qa_app_q3_source_host;
typedef struct qa_compat_q3_native_engine_services {
  void *ctx;
  void (*append_console_command)(void *ctx, const char *text);
  void (*print)(void *ctx, const char *text);
  void (*drop_client)(void *ctx, double client, const char *reason);
  void (*send_server_command)(void *ctx, double client, const char *text);
  const char *(*get_userinfo)(void *ctx, double client);
  void (*set_userinfo)(void *ctx, double client, const char *value);
  /* CvarRegistry picks. cvar_variable_string and the cvar_read_vm
   * value are malloc'd (caller frees). */
  double (*cvar_variable_value)(void *ctx, const char *name);
  char *(*cvar_variable_string)(void *ctx, const char *name);
  void (*cvar_set)(void *ctx, const char *name, const char *value, bool force);
  int64_t (*cvar_bind_vm)(void *ctx, const char *name, const char *default_value, double flags);
  bool (*cvar_read_vm)(void *ctx, int64_t handle, double *out_modification_count,
    char **out_value, double *out_numeric_value, double *out_integer_value);
  void (*configstring_set)(void *ctx, double index, const char *value);
  const char *(*configstring_get)(void *ctx, double index);
  const char *const *(*command_arguments)(void *ctx, size_t *out_count);
} qa_compat_q3_native_engine_services;
/* Binds the seam to the landed qa_app_q3_source_host (borrowed):
 * engine, cvars registry and configstrings forward to it.
 * command_arguments stays caller-supplied (host has none in TS;
 * NativeQ3EngineServices adds it). */
typedef struct qa_compat_q3_native_host_adapter {
  qa_app_q3_source_host *host; /* borrowed */
  void *arguments_ctx;
  const char *const *(*command_arguments)(void *ctx, size_t *out_count);
  char *userinfo; /* owned scratch for engine.getUserinfo */
  qa_compat_q3_native_engine_services services;
} qa_compat_q3_native_host_adapter;
void qa_compat_q3_native_host_adapter_init(qa_compat_q3_native_host_adapter *adapter,
    qa_app_q3_source_host *host,
    const char *const *(*command_arguments)(void *ctx, size_t *out_count), void *arguments_ctx);
void qa_compat_q3_native_host_adapter_destroy(qa_compat_q3_native_host_adapter *adapter);
#endif
