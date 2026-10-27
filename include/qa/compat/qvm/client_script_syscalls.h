/*
 * Port of src/compat/qvm/client-script-syscalls.ts.
 * qa_qvmx_* mirrors (bots/ui modules unported):
 *  - qa_qvmx_bot_memory mirrors bots/behavior/library/memory.ts
 *    BotMemory (checkpoint/restore/dispose surface used here);
 *  - qa_qvmx_script_globals mirrors ScriptGlobalDefines;
 *  - qa_qvmx_script_source mirrors SourceScriptStorage and
 *    allocateScriptSource (ui/common/legacy/script);
 *  - qa_qvmx_script_reader mirrors ScriptSourceReader.
 * Async include/load/read flows are synchronous in C.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_SCRIPT_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_SCRIPT_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/script_record.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/content/mounts.h"
#include "qa/persistence/value.h"

typedef struct qa_qvmx_bot_memory_capture {
  const qa_save_value *image; /* borrowed */
  void *ctx;
  double (*reference)(void *ctx, void *allocation);
} qa_qvmx_bot_memory_capture;

typedef struct qa_qvmx_bot_memory_restore {
  void *ctx;
  void *(*allocation)(void *ctx, double reference);
} qa_qvmx_bot_memory_restore;

typedef struct qa_qvmx_bot_memory {
  void *ctx;
  /* Owned checkpoint image written to *out_image (NULL only on OOM). */
  bool (*checkpoint)(void *ctx, qa_save_value **out_image, qa_qvmx_bot_memory_capture *out_capture);
  qa_qvmx_bot_memory_restore (*restore)(void *ctx, const qa_save_value *value);
  void (*dispose)(void *ctx);
} qa_qvmx_bot_memory;

typedef struct qa_qvmx_script_globals {
  void *ctx;
  bool (*add)(void *ctx, const char *text);
  qa_save_value *(*capture_owned)(void *ctx); /* owned; NULL only on OOM */
  void (*restore_owned)(void *ctx, const qa_save_value *value);
} qa_qvmx_script_globals;

typedef struct qa_qvmx_script_source {
  void *ctx;
  uint8_t *buffer;
  size_t length;
  void (*compress)(void *ctx);
  void (*dispose)(void *ctx);
} qa_qvmx_script_source;

typedef struct qa_qvmx_include_request {
  const char *kind; /* "system" | "quoted" */
  const char *requested_path;
  const char *include_path; /* NULL when absent */
} qa_qvmx_include_request;

typedef struct qa_qvmx_script_resolver {
  void *ctx;
  /* False is the TS undefined source. */
  bool (*resolve)(void *ctx, qa_qvmx_include_request request, qa_qvmx_script_source *out);
} qa_qvmx_script_resolver;

typedef struct qa_qvmx_script_diagnostic {
  const char *path;
  double line;
  const char *message;
} qa_qvmx_script_diagnostic;

typedef struct qa_qvmx_script_reader_options {
  qa_qvmx_bot_memory *memory;
  qa_qvmx_script_globals *globals;
  void *report_ctx;
  void (*report)(void *ctx, qa_qvmx_script_diagnostic diagnostic);
} qa_qvmx_script_reader_options;

typedef struct qa_qvmx_script_position {
  const char *filename; /* borrowed */
  double line;
} qa_qvmx_script_position;

typedef struct qa_qvmx_script_reader {
  void *ctx;
  qa_save_value *(*capture)(void *ctx, qa_qvmx_bot_memory_capture capture); /* owned */
  void (*dispose)(void *ctx);
  /* 1 = token, 0 = undefined (EOF), -1 = source failure. */
  int (*next)(void *ctx, void (*validate)(void *validate_ctx), void *validate_ctx);
  bool (*get_position)(void *ctx, qa_qvmx_script_position *out);
  qa_qvmy_script_token_record (*raw_token)(void *ctx);
} qa_qvmx_script_reader;

typedef struct qa_qvmx_script_reader_factory {
  void *ctx;
  qa_qvmx_script_reader (*open)(void *ctx, qa_qvmx_script_source root,
                                qa_qvmx_script_resolver resolver,
                                qa_qvmx_script_reader_options options);
  qa_qvmx_script_reader (*restore)(void *ctx, const qa_save_value *state,
                                   qa_qvmx_script_resolver resolver,
                                   qa_qvmx_script_reader_options options,
                                   qa_qvmx_bot_memory_restore memory);
  /* allocateScriptSource(length, filename, memory). */
  qa_qvmx_script_source (*allocate_source)(void *ctx, size_t length, const char *filename,
                                           qa_qvmx_bot_memory *memory);
} qa_qvmx_script_reader_factory;

typedef struct qa_qvm_client_script_services {
  qa_content_mounted *mounts;
  qa_qvmx_script_globals *globals;
  qa_qvmx_script_reader_factory *factory;
  qa_qvmx_bot_memory *memory; /* owned by QvmClientScripts in TS; supplied here */
  void *ctx;
  void (*assert_current)(void *ctx);
  void (*print)(void *ctx, const char *text);
} qa_qvm_client_script_services;

typedef struct qa_qvm_client_scripts qa_qvm_client_scripts;

qa_qvm_client_scripts *qa_qvm_client_scripts_create(qa_qvm_client_script_services services);
void qa_qvm_client_scripts_destroy(qa_qvm_client_scripts *scripts);
qa_qvm_client_script_services qa_qvm_client_scripts_get_services(
    const qa_qvm_client_scripts *scripts);
/* Owned checkpoint tree {memory,globals,readers}; NULL only on OOM. */
qa_save_value *qa_qvm_client_scripts_capture(qa_qvm_client_scripts *scripts);
void qa_qvm_client_scripts_restore(qa_qvm_client_scripts *scripts, const qa_save_value *value);
void qa_qvm_client_scripts_assert_current(qa_qvm_client_scripts *scripts);
void qa_qvm_client_scripts_close_all(qa_qvm_client_scripts *scripts);
double qa_qvm_client_scripts_add_define(qa_qvm_client_scripts *scripts, const char *text);
double qa_qvm_client_scripts_load(qa_qvm_client_scripts *scripts, const char *filename);
double qa_qvm_client_scripts_free(qa_qvm_client_scripts *scripts, double handle);
typedef struct qa_qvm_script_publish {
  void *ctx;
  void (*call)(void *ctx, qa_qvmx_script_reader *reader);
} qa_qvm_script_publish;
double qa_qvm_client_scripts_read(qa_qvm_client_scripts *scripts, double handle,
                                  qa_qvm_script_publish publish);
/* False is the TS undefined position. */
bool qa_qvm_client_scripts_position(qa_qvm_client_scripts *scripts, double handle,
                                    qa_qvmx_script_position *out);

bool qa_qvm_client_script_syscall(const qa_qvm_host_call *call, qa_qvm_client_scripts *scripts,
                                  qa_qvm_host_result *out);

#endif
