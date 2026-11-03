/*
 * Port of src/console/llm-batch.ts: whole-batch validation of
 * LLM-generated console commands and the instruction/catalog
 * text builders. C accommodations:
 *  - The TS functions throw plain Errors that llm.ts catches and
 *    prints; here they return NULL/false with a malloc'd message
 *    in *out_error (caller frees) carrying the exact TS text.
 *    Functions without out_error cannot throw in TS.
 *  - Lengths count bytes where TS counts UTF-16 units; batch text
 *    is restricted to printable ASCII by validation itself, so
 *    counts agree on every admitted batch.
 *  - Ranking ties use localeCompare (qa_js_locale_compare), as in TS.
 * All returned strings/arrays are malloc'd; caller frees.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_LLM_BATCH_H
#define QA_CONSOLE_LLM_BATCH_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/common.h"
#include "qa/core/commands/index.h"

/** TS "ask" | "exec" catalog mode. */
typedef enum qa_llm_catalog_mode {
  QA_LLM_CATALOG_ASK,
  QA_LLM_CATALOG_EXEC
} qa_llm_catalog_mode;

/* Resolves aliases now, so the checked batch is the exact batch
 * submitted later. On success *out_lines is a malloc'd array of
 * malloc'd command lines. */
bool qa_validate_llm_batch(const char *text, qa_command_buffer *commands,
                           const qa_command_context *source, char ***out_lines,
                           size_t *out_count, char **out_error);
void qa_llm_batch_lines_free(char **lines, size_t count);

char *qa_llm_console_catalog(qa_command_buffer *commands,
                             const qa_command_context *source, const char *prompt,
                             qa_llm_catalog_mode mode, char **out_error);
/* Prompt-specific documentation only; the full catalog is in the
 * session primer. TS default mode is "ask". */
char *qa_llm_relevant_console_docs(qa_command_buffer *commands,
                                   const qa_command_context *source,
                                   const char *prompt, qa_llm_catalog_mode mode);
/* Static context sent once when the process-lifetime session opens. */
char *qa_llm_session_instructions(qa_command_buffer *commands,
                                  const qa_command_context *source, char **out_error);
char *qa_llm_exec_instructions(qa_command_buffer *commands,
                               const qa_command_context *source, const char *prompt,
                               char **out_error);
/* Per-request exec rules for a primed session. */
char *qa_llm_exec_turn_instructions(qa_command_buffer *commands,
                                    const qa_command_context *source,
                                    const char *prompt);

#endif
