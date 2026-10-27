/*
 * Port of src/core/commands/index.ts (CommandBuffer). The text.ts
 * and filter.ts re-exports are the qa/core/commands/text.h and
 * filter.h ports, included below as the TS barrel does.
 * C accommodations:
 *  - Async flows are synchronous (see docs/PORTING.md): readScript
 *    returns a qa_script_read_result immediately; a PENDING result
 *    carries a poll callback the drain re-checks (the async drain
 *    polls between after_dispatch calls, where TS awaits). Failed
 *    reads carry a message string (TS unknown error).
 *  - AbortSignal becomes an aborted() callback + ctx.
 *  - CommandInvocation is a struct valid during dispatch; its argv
 *    array is borrowed from the buffer. append/insert/executeNow/
 *    executeScript/assertActive are qa_command_invocation_* calls.
 *  - Handler identity is function pointer + ctx; a NULL handler is
 *    the TS null (fallback name). Symbol producer instances are
 *    opaque const void * (contracts qa_command_producer.instance).
 *  - Contexts stored in chunks deep-copy their origin chain (the TS
 *    freezes fresh objects); producer module strings stay borrowed.
 *  - bindOutput/bindScriptCompletion/bindProducer return handles.
 *  - prepareProgram's prefix runner is a synchronous bool callback;
 *    a throwing TS runner has no C counterpart (it aborts).
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_COMMANDS_INDEX_H
#define QA_CORE_COMMANDS_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/common.h"
#include "qa/core/commands/documentation.h"
#include "qa/core/commands/filter.h"
#include "qa/core/commands/text.h"
#include "qa/core/cvars/index.h"

typedef struct qa_command_buffer qa_command_buffer;
typedef struct qa_command_invocation qa_command_invocation;
struct qa_command_invocation {
  qa_command_context source;
  bool direct;
  qa_command_dialect dialect;
  char *const *argv; /* borrowed; argc entries */
  size_t argc;
  const char *args_text; /* borrowed */
  const char *raw;       /* borrowed */
  qa_command_buffer *buffer;
  void *frame; /* internal */
};
/* TS CommandHandler returns undefined. */
typedef void (*qa_command_handler_fn)(void *ctx, qa_command_invocation *command);
/* TS CommandFallback: -1 undefined, else 0/1. */
typedef int (*qa_command_fallback_fn)(void *ctx, qa_command_invocation *command);

void qa_command_invocation_append(qa_command_invocation *command, const char *text);
void qa_command_invocation_insert(qa_command_invocation *command, const char *text);
double qa_command_invocation_execute_now(qa_command_invocation *command, const char *text);
void qa_command_invocation_execute_script(qa_command_invocation *command);
void qa_command_invocation_assert_active(qa_command_invocation *command);

/** TS CommandCvarRouting: result registries are borrowed; visible
 * fills a caller array the callback owns (ptr + count). */
typedef struct qa_command_cvar_routing {
  void *ctx;
  qa_cvar_registry *(*owner)(void *ctx, const char *name, qa_command_context source);
  qa_cvar_registry *const *(*visible)(void *ctx, qa_command_context source, size_t *out_count);
} qa_command_cvar_routing;

/* TS ScriptCompletion. error_message is set for the failed kind. */
typedef enum qa_script_result_kind {
  QA_SCRIPT_COMPLETED, QA_SCRIPT_MISSING, QA_SCRIPT_FAILED
} qa_script_result_kind;
typedef struct qa_script_completion {
  char *name; /* owned */
  qa_command_context source; /* origin chain owned */
  qa_script_result_kind result;
  char *error_message; /* owned, failed only */
  qa_command_buffer *buffer; /* internal: frees the origin chain */
} qa_script_completion;
void qa_script_completion_free(qa_script_completion *event);

/* Synchronous readScript result (see header note). For READY, text
 * is a malloc'd script body or NULL for TS undefined (missing file);
 * has_text distinguishes them. For PENDING, poll is re-called until
 * it reports a settled kind. For FAILED, error_message is set. */
typedef enum qa_script_read_kind {
  QA_SCRIPT_READ_READY, QA_SCRIPT_READ_PENDING, QA_SCRIPT_READ_FAILED
} qa_script_read_kind;
typedef struct qa_script_read_result {
  qa_script_read_kind kind;
  bool has_text;
  char *text;
  char *error_message;
  void *poll_ctx;
  struct qa_script_read_result (*poll)(void *poll_ctx);
  void (*cancel)(void *poll_ctx);
} qa_script_read_result;
typedef qa_script_read_result (*qa_read_script_fn)(void *ctx, const char *name,
                                                   qa_command_context source);

/** TS CommandProducerServices. */
typedef struct qa_command_producer_services {
  void *ctx;
  qa_cvar_registry *(*cvars)(void *ctx, qa_command_context source);
  qa_read_script_fn read_script;
  void *read_script_ctx;
  void (*command)(void *ctx, qa_command_invocation *command);
  bool (*engine_command)(void *ctx, const char *name);
} qa_command_producer_services;

typedef void (*qa_command_print_source_fn)(void *ctx, const char *text,
                                           const qa_command_context *source);
typedef void (*qa_script_complete_fn)(void *ctx, const qa_script_completion *event);
typedef bool (*qa_allow_command_fn)(void *ctx, qa_command_invocation *command);
/* TS (command, registered) => boolean | undefined; -1 = undefined. */
typedef int (*qa_source_command_fn)(void *ctx, qa_command_invocation *command, bool registered);

typedef struct qa_command_buffer_options {
  qa_command_dialect dialect;
  qa_command_context context;
  qa_cvar_registry *cvars; /* NULL when absent */
  qa_command_cvar_routing cvar_routing; /* owner NULL when absent */
  qa_command_print_source_fn print;
  void *print_ctx;
  qa_read_script_fn read_script;
  void *read_script_ctx;
  qa_script_complete_fn on_script_complete;
  void *on_script_complete_ctx;
  const char *const *command_line;
  size_t command_line_count;
  const char *startup_command_text; /* NULL when absent */
  qa_allow_command_fn allow_command;
  void *allow_command_ctx;
  qa_source_command_fn source_command;
  void *source_command_ctx;
  qa_command_fallback_fn client_game;
  void *client_game_ctx;
  qa_command_fallback_fn server_game;
  void *server_game_ctx;
  qa_command_fallback_fn ui;
  void *ui_ctx;
  qa_command_handler_fn forward_to_server;
  void *forward_to_server_ctx;
  double max_buffer_length;  /* <= 0 selects the TS default */
  double max_command_length; /* <= 0 selects the TS default */
  bool builtins;             /* default true; set builtins_set false */
  bool builtins_set;
} qa_command_buffer_options;

/* NULL only on OOM; invalid profiles abort as the TS throws. */
qa_command_buffer *qa_command_buffer_create(const qa_command_buffer_options *options);
void qa_command_buffer_destroy(qa_command_buffer *buffer);
qa_command_dialect qa_command_buffer_dialect(const qa_command_buffer *buffer);
qa_command_context qa_command_buffer_context(const qa_command_buffer *buffer);
bool qa_command_buffer_program_complete(qa_command_buffer *buffer);
bool qa_command_buffer_has_preparation_prefix(qa_command_buffer *buffer);
bool qa_command_buffer_preparation_complete(qa_command_buffer *buffer);
bool qa_command_buffer_has_pending_commands(qa_command_buffer *buffer);
/* Malloc'd pending/deferred text. */
char *qa_command_buffer_pending_text(qa_command_buffer *buffer);
char *qa_command_buffer_deferred_text(qa_command_buffer *buffer);
/* Borrowed tokens from the most recent dispatch. */
char *const *qa_command_buffer_tokenized_arguments(qa_command_buffer *buffer, size_t *out_count);
double qa_command_buffer_maximum_command_length(const qa_command_buffer *buffer);
double qa_command_buffer_maximum_buffer_length(const qa_command_buffer *buffer);
/* Borrowed frame source context or NULL (TS undefined). */
const qa_command_context *qa_command_buffer_execution_context(qa_command_buffer *buffer);

typedef void (*qa_after_dispatch_fn)(void *ctx);
typedef bool (*qa_should_continue_fn)(void *ctx);
/* TS async advanceProgramFrame/executeAsync/executeScriptsAsync:
 * synchronous here. NULL after_dispatch means no per-command yield. */
void qa_command_buffer_advance_program_frame(qa_command_buffer *buffer);
double qa_command_buffer_execute_async(qa_command_buffer *buffer,
                                       qa_after_dispatch_fn after_dispatch, void *after_ctx,
                                       qa_should_continue_fn should_continue, void *continue_ctx);
double qa_command_buffer_execute_scripts_async(qa_command_buffer *buffer,
                                               qa_after_dispatch_fn after_dispatch, void *after_ctx,
                                               qa_should_continue_fn should_continue, void *continue_ctx);
void qa_command_buffer_validate_profile(qa_command_buffer *buffer, qa_command_dialect dialect,
                                        qa_cvar_registry *cvars);
void qa_command_buffer_set_profile(qa_command_buffer *buffer, qa_command_dialect dialect,
                                   qa_cvar_registry *cvars);

typedef bool (*qa_prepare_prefix_run_fn)(void *ctx);
typedef struct qa_prepared_program qa_prepared_program;
struct qa_prepared_program {
  void *ctx;
  qa_command_buffer *commands;
  bool (*prepare_prefix)(void *ctx, qa_prepare_prefix_run_fn run, void *run_ctx);
  void (*validate_publication)(void *ctx);
  void (*publish)(void *ctx);
  void (*destroy)(void *ctx);
};
qa_prepared_program *qa_command_buffer_prepare_program(
    qa_command_buffer *buffer, const qa_command_buffer_options *options);
void qa_command_buffer_finish_preparation(qa_command_buffer *buffer);
void qa_command_buffer_copy_pending_from(qa_command_buffer *buffer, qa_command_buffer *previous);

bool qa_command_buffer_producer_pending(qa_command_buffer *buffer, const void *instance);
/* Returns an unbind handle. */
int64_t qa_command_buffer_bind_producer(qa_command_buffer *buffer, const void *instance,
                                        qa_command_producer_services services);
void qa_command_buffer_unbind_producer(qa_command_buffer *buffer, int64_t binding);
void qa_command_buffer_discard_producer(qa_command_buffer *buffer, const void *instance);
void qa_command_buffer_discard_client(qa_command_buffer *buffer, qa_client_id client);
int64_t qa_command_buffer_bind_output(qa_command_buffer *buffer,
                                      qa_command_print_source_fn print, void *ctx);
void qa_command_buffer_unbind_output(qa_command_buffer *buffer, int64_t binding);
int64_t qa_command_buffer_bind_script_completion(qa_command_buffer *buffer,
                                                 qa_script_complete_fn listener, void *ctx);
void qa_command_buffer_unbind_script_completion(qa_command_buffer *buffer, int64_t binding);

/* `source` NULL uses the frame/buffer context. */
bool qa_command_buffer_find_cvar(qa_command_buffer *buffer, const char *name,
                                 const qa_command_context *source, qa_cvar_read *out);
/* Malloc'd snapshot array. */
qa_cvar_snapshot *qa_command_buffer_cvar_snapshots(qa_command_buffer *buffer,
                                                   const qa_command_context *source,
                                                   size_t *out_count);
char **qa_command_buffer_archive_commands(qa_command_buffer *buffer,
                                          const qa_command_context *source, size_t *out_count);

typedef enum qa_command_ownership {
  QA_COMMAND_WORLD, QA_COMMAND_ENGINE
} qa_command_ownership;
/* handler NULL registers a fallback name (TS null). */
bool qa_command_buffer_register(qa_command_buffer *buffer, const char *name,
                                qa_command_handler_fn handler, void *handler_ctx,
                                const qa_command_documentation *documentation,
                                qa_command_ownership ownership);
bool qa_command_buffer_register_engine(qa_command_buffer *buffer, const char *name,
                                       qa_command_handler_fn handler, void *handler_ctx,
                                       const qa_command_documentation *documentation);
bool qa_command_buffer_register_fallback_name(qa_command_buffer *buffer, const char *name);
bool qa_command_buffer_exists(qa_command_buffer *buffer, const char *name);
/* expected handler checked only when has_expected is true. */
bool qa_command_buffer_unregister(qa_command_buffer *buffer, const char *name,
                                  qa_command_handler_fn expected, void *expected_ctx,
                                  bool has_expected);
/* Malloc'd name array (each string malloc'd). */
char **qa_command_buffer_registered_names(qa_command_buffer *buffer, size_t *out_count);
const qa_command_documentation *qa_command_buffer_command_documentation(
    qa_command_buffer *buffer, const char *name);
const qa_command_documentation *qa_command_buffer_cvar_documentation(
    qa_command_buffer *buffer, const char *name, const qa_command_context *source);
/* Malloc'd alias value, NULL for TS undefined or OOM. */
char *qa_command_buffer_alias_value(qa_command_buffer *buffer, const char *name);
typedef void (*qa_command_name_visitor_fn)(void *ctx, const char *name);
void qa_command_buffer_complete_names(qa_command_buffer *buffer,
                                      qa_command_name_visitor_fn visitor, void *ctx);
char *qa_command_buffer_complete(qa_command_buffer *buffer, const char *partial);
bool qa_command_buffer_define_alias(qa_command_buffer *buffer, const char *name,
                                    const char *text);
char **qa_command_buffer_alias_names(qa_command_buffer *buffer, size_t *out_count);

/* dialect < 0 selects the execution dialect. */
void qa_command_buffer_append(qa_command_buffer *buffer, const char *text,
                              const qa_command_context *source, int dialect);
void qa_command_buffer_append_preparation(qa_command_buffer *buffer, const char *text,
                                          const qa_command_context *source, int dialect);
void qa_command_buffer_insert(qa_command_buffer *buffer, const char *text,
                              const qa_command_context *source, int dialect);
void qa_command_buffer_copy_to_defer(qa_command_buffer *buffer);
void qa_command_buffer_insert_from_defer(qa_command_buffer *buffer);
double qa_command_buffer_execute(qa_command_buffer *buffer);
/* text NULL is the TS null (drain the buffer). */
double qa_command_buffer_execute_now(qa_command_buffer *buffer, const char *text,
                                     const qa_command_context *source, int dialect);
typedef bool (*qa_abort_signal_fn)(void *ctx);
double qa_command_buffer_execute_batch(qa_command_buffer *buffer, const char *text,
                                       qa_command_context source,
                                       qa_abort_signal_fn aborted, void *signal_ctx);
double qa_command_buffer_execute_batch_async(qa_command_buffer *buffer, const char *text,
                                             qa_command_context source,
                                             qa_after_dispatch_fn after_dispatch, void *after_ctx,
                                             qa_abort_signal_fn aborted, void *signal_ctx);
/* macro_expansion mirrors the TS "source" | "none" choice. */
double qa_command_buffer_execute_command_async(qa_command_buffer *buffer, const char *text,
                                               qa_command_context source,
                                               qa_after_dispatch_fn after_dispatch, void *after_ctx,
                                               bool macro_expansion,
                                               qa_abort_signal_fn aborted, void *signal_ctx);
void qa_command_buffer_forward_to_server(qa_command_buffer *buffer,
                                         qa_command_invocation *command);

#endif
