/*
 * Port of src/console/llm.ts: the llm_ask/llm_exec/llm_cancel
 * engine commands and session priming.
 * C accommodations (per docs/PORTING.md, async flows become
 * synchronous): the TS requester's Promise<string> request is a
 * synchronous call returning a malloc'd answer, or NULL with a
 * malloc'd message in *out_error (the TS Error.message; NULL
 * message mirrors a non-Error rejection, printed as
 * "Unknown error"). prime is a plain call (its TS rejection is
 * swallowed there). The 120 s timer becomes the abort signal's
 * deadline (qa_llm_signal_set_timeout); a request that overruns
 * it is reported with the TS timeout text when it returns.
 * Prompt length counts bytes where TS counts UTF-16 units.
 * The TS dispose closure becomes qa_llm_commands_registration_remove,
 * which unregisters exactly the names that were registered; call
 * it once. Registration is reference-counted so disposal from a
 * generated command unwinds safely. GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_LLM_H
#define QA_CONSOLE_LLM_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/common.h"
#include "qa/core/commands/index.h"
#include "qa/llm/request.h"

/** TS LlmCommandRequester. prime NULL mirrors the absent method. */
typedef struct qa_llm_command_requester {
  void *ctx;
  char *(*request)(void *ctx, const qa_llm_request_input *input, char **out_error);
  void (*prime)(void *ctx, const char *instructions);
} qa_llm_command_requester;

/* Start the requester's process-lifetime session with this
 * console's live catalog. requester NULL and source NULL mirror
 * the TS undefined/default arguments. */
void qa_prime_llm_console_session(qa_command_buffer *commands,
                                  const qa_llm_command_requester *requester,
                                  const qa_command_context *source);

typedef struct qa_llm_pending_request {
  char *key; /* owned: "seat:<index>" or "console" */
  qa_llm_abort_signal *signal; /* borrowed from the in-flight run */
} qa_llm_pending_request;

typedef struct qa_llm_commands_registration {
  qa_command_buffer *commands;
  qa_command_print_source_fn print;
  void *print_ctx;
  bool has_requester;
  qa_llm_command_requester requester;
  const char *names[3];
  size_t name_count;
  qa_llm_pending_request *pending;
  size_t pending_count;
  size_t pending_capacity;
  bool disposed;
  int refs;
} qa_llm_commands_registration;

/* NULL only on OOM. requester NULL mirrors the TS undefined. */
qa_llm_commands_registration *qa_register_llm_commands(
    qa_command_buffer *commands, qa_command_print_source_fn print, void *print_ctx,
    const qa_llm_command_requester *requester);
/* The TS returned closure: aborts pending requests, unregisters
 * the commands, and frees (once in-flight runs unwind). */
void qa_llm_commands_registration_remove(qa_llm_commands_registration *registration);

#endif
