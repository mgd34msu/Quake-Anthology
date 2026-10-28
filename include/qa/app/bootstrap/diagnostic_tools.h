/* Port of src/app/bootstrap/diagnostic-tools.ts. Command registration
 * uses the landed command buffer; queued async work runs synchronously
 * through the queue callback. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_DIAGNOSTIC_TOOLS_H
#define QA_APP_BOOTSTRAP_DIAGNOSTIC_TOOLS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/core/commands/index.h"
#include "qa/core/omnitimer.h"
typedef struct qa_app_diagnostic_tools qa_app_diagnostic_tools;
typedef struct qa_app_capture_frame_result { double milliseconds; bool capture; } qa_app_capture_frame_result;
typedef struct qa_app_diagnostic_frame { double frame, milliseconds, clients; const char *map, *renderer; } qa_app_diagnostic_frame;
/* TS RuntimeDiagnosticServices mounts surface used by path/dir/touchFile/resourceinfo. */
typedef struct qa_app_runtime_mounts {
  void *ctx;
  void (*assert_open)(void *ctx);
  /* Malloc'd "path" report text / opened-resources JSON; caller frees. */
  char *(*path_report)(void *ctx);
  char *(*opened_resources_json)(void *ctx);
  char **(*list_files)(void *ctx, const char *directory, const char *extension, size_t *out_count);
  void (*open_file)(void *ctx, const char *path);
} qa_app_runtime_mounts;
typedef struct qa_app_runtime_diagnostic_services {
  void *ctx;
  qa_command_buffer *commands;
  qa_app_runtime_mounts (*mounts)(void *ctx);
  qa_app_diagnostic_frame (*frame)(void *ctx);
  void (*print)(void *ctx, const char *text);
  /* TS queue(Promise): run the operation now (synchronous port). */
  void (*queue)(void *ctx, void (*operation)(void *op_ctx), void *op_ctx);
} qa_app_runtime_diagnostic_services;
qa_app_capture_frame_result qa_app_source_capture_frame(double elapsed, double fps, double timescale, bool active, bool force);
/* Release handles; destroy unregisters (TS returned disposers). */
qa_app_diagnostic_tools *qa_app_register_diagnostic_tools(qa_command_buffer *commands, qa_omni_timer *timer,
                                                          void (*print)(void *ctx, const char *text), void *print_ctx);
qa_app_diagnostic_tools *qa_app_register_runtime_diagnostics(qa_app_runtime_diagnostic_services services);
void qa_app_diagnostic_tools_destroy(qa_app_diagnostic_tools *tools);
#endif
