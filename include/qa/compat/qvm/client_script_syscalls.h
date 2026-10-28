/*
 * Port of src/compat/qvm/client-script-syscalls.ts over the landed
 * script engine (ui/common/legacy/script): services.globals is the
 * real ScriptGlobalDefines, readers are real ScriptSourceReaders,
 * and the TS-owned BotMemory is a private qa_bot_memory behind a
 * qa_ui_script_memory adapter. qa_bot_memory has no checkpoint/
 * restore C API yet, so BotMemory.checkpoint/restore (TS owner
 * bots/behavior/library/memory.ts) are implemented locally in the
 * .c over tracked allocations, with the TS save format and error
 * strings. Async include/load/read flows are synchronous in C.
 * Uncaught engine source failures abort (the engine exports no
 * catch for ScriptSourceReader), so the TS read() partial-false
 * path for source failures is unreachable. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_SCRIPT_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_SCRIPT_SYSCALLS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/content/mounts.h"
#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/preprocessor.h"

typedef struct qa_qvm_client_script_services {
  qa_content_mounted *mounts;
  qa_ui_script_global_defines *globals;
  void *ctx;
  void (*assert_current)(void *ctx);
  void (*print)(void *ctx, const char *text);
} qa_qvm_client_script_services;

/** TS ScriptSourcePosition (reader.position). */
typedef struct qa_qvm_script_position {
  char filename[1024];
  int64_t line;
} qa_qvm_script_position;

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
  void (*call)(void *ctx, qa_ui_script_source_reader *reader);
} qa_qvm_script_publish;
double qa_qvm_client_scripts_read(qa_qvm_client_scripts *scripts, double handle,
                                  qa_qvm_script_publish publish);
/* False is the TS undefined position. */
bool qa_qvm_client_scripts_position(qa_qvm_client_scripts *scripts, double handle,
                                    qa_qvm_script_position *out);

bool qa_qvm_client_script_syscall(const qa_qvm_host_call *call, qa_qvm_client_scripts *scripts,
                                  qa_qvm_host_result *out);

#endif
