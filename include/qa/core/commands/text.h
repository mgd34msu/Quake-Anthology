/*
 * Port of src/core/commands/text.ts: command-text validation, token
 * parsing, Q2 macro expansion, and tokenization. C accommodations:
 * strings are NUL-terminated bytes, so sourceCommandText's cut at an
 * embedded NUL and its charCode > 255 rejection are inherent; string
 * results are malloc'd (NULL only on allocation failure) and byte
 * values are the TS charCodes, including high bytes. Token lists are
 * caller-owned (qa_command_tokens_destroy). RangeErrors abort.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_COMMANDS_TEXT_H
#define QA_CORE_COMMANDS_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/common.h"

/* Malloc'd copy of the input byte text. */
char *qa_source_command_text(const char *input);
/* Malloc'd copy with ASCII A-Z folded to a-z. */
char *qa_ascii_fold(const char *text);

bool qa_command_dialect_is_q1(qa_command_dialect dialect);
bool qa_command_dialect_is_q2(qa_command_dialect dialect);

/* Offset of the first unquoted ';' (or newline; CR too for q3). */
size_t qa_command_separator_offset(const char *text, qa_command_dialect dialect);

/* TS CommandTextMode. */
typedef enum qa_command_text_mode {
  QA_COMMAND_TEXT_SOURCE,  /* "source" */
  QA_COMMAND_TEXT_CONSOLE  /* "console" */
} qa_command_text_mode;

/** TS (name: string) => string, borrowed result. */
typedef const char *(*qa_command_variable_fn)(void *ctx, const char *name);
/** TS (text: string) => void. */
typedef void (*qa_command_print_fn)(void *ctx, const char *text);

/*
 * Q2 macro expansion. Returns true with *out holding a malloc'd result
 * (NULL only on allocation failure); false is the TS undefined (the
 * line was discarded and `print` was told why).
 */
bool qa_expand_command_macros(const char *input, qa_command_variable_fn variable,
                              void *variable_ctx, qa_command_print_fn print,
                              void *print_ctx, qa_command_text_mode mode, char **out);

/* Text after `count` leading tokens; count < 0 is the TS RangeError. */
char *qa_command_text_tail(const char *input, qa_command_dialect dialect, int64_t count);

/** TS CommandTokens. argv entries and args_text are malloc'd. */
typedef struct qa_command_tokens {
  char **argv;
  size_t argc;
  char *args_text;
} qa_command_tokens;

/* False only on allocation failure (out is left zeroed). */
bool qa_tokenize_command(const char *input, qa_command_dialect dialect,
                         qa_command_text_mode mode, qa_command_tokens *out);
void qa_command_tokens_destroy(qa_command_tokens *tokens);

#endif
