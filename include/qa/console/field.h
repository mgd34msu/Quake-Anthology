/*
 * Port of src/console/field.ts: the rerelease console edit field
 * (ConsoleField), command completion, and ConsoleHistory over
 * BoundedQueue. Re-exports EditField/FieldControls/FieldClipboard
 * from source-field.ts, as the TS file does.
 * C accommodations: the TS field edits an array of Unicode code
 * points; here text is NUL-terminated bytes and cursor/scroll/
 * maximumLength count bytes, so positions differ for non-ASCII
 * input (see docs/PORTING.md). The TS \s classes likewise match
 * ASCII whitespace bytes only. Key codes are the numeric KeyCode
 * values of src/input/key-codes.ts (input module not yet ported;
 * values are named in field.c). The clipboard closure becomes a
 * function pointer + ctx returning a borrowed string or NULL.
 * Ownership: fields/histories own their buffers and completion
 * state; init returns false only on OOM. qa_console_field_text
 * returns a malloc'd copy. History stores malloc'd copies of its
 * lines; accessors return borrowed pointers valid until the next
 * mutation. GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_FIELD_H
#define QA_CONSOLE_FIELD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/console/source_field.h"
#include "qa/foundation/bounded_queue.h"

#define QA_CONSOLE_FIELD_DEFAULT_MAXIMUM_LENGTH 1023
#define QA_CONSOLE_FIELD_DEFAULT_WIDTH_IN_CHARS 78
#define QA_CONSOLE_HISTORY_DEFAULT_CAPACITY 32

/** TS clipboard: () => string | null; borrowed result. */
typedef const char *(*qa_console_clipboard_fn)(void *ctx);

typedef struct qa_console_field {
  char *characters; /* byte buffer, not NUL-terminated internally */
  size_t length;
  size_t capacity;
  int64_t cursor;
  int64_t scroll;
  int64_t maximum_length;
  int64_t width_in_chars;
  bool overstrike;
  /* Completion cycle; has_completion false mirrors the TS null. */
  bool has_completion;
  char *completion_text;
  int64_t completion_cursor;
  char *completion_tail;
  char **completion_matches;
  size_t completion_match_count;
  int64_t completion_index;
  char *completion_selected;
} qa_console_field;

bool qa_console_field_init(qa_console_field *field, int64_t maximum_length,
                           int64_t width_in_chars);
void qa_console_field_destroy(qa_console_field *field);
/* Malloc'd field text; caller frees. NULL only on OOM. */
char *qa_console_field_text(const qa_console_field *field);
/* Borrowed selected completion or NULL (TS null). */
const char *qa_console_field_selected_completion(const qa_console_field *field);
void qa_console_field_complete(qa_console_field *field, const char *const *names,
                               size_t name_count, bool reverse);
void qa_console_field_set_text(qa_console_field *field, const char *text);
void qa_console_field_clear(qa_console_field *field);
void qa_console_field_insert(qa_console_field *field, const char *text);
bool qa_console_field_key(qa_console_field *field, int64_t code, bool control, bool shift,
                          qa_console_clipboard_fn clipboard, void *clipboard_ctx);

/** TS CompletionResult. text is malloc'd; matches is a malloc'd
 * array of borrowed name pointers. */
typedef struct qa_completion_result {
  char *text;
  const char **matches;
  size_t match_count;
} qa_completion_result;

/* False only on OOM (out is left zeroed). */
bool qa_complete_command(const char *text, const char *const *names, size_t name_count,
                         qa_completion_result *out);
void qa_completion_result_destroy(qa_completion_result *result);

typedef struct qa_console_history {
  qa_bounded_queue entries; /* of char *, owned copies */
  int64_t capacity;
  int64_t position;
  char *draft; /* owned */
} qa_console_history;

/* Capacity < 1 aborts as BoundedQueue does. False only on OOM. */
bool qa_console_history_init(qa_console_history *history, int64_t capacity);
void qa_console_history_destroy(qa_console_history *history);
size_t qa_console_history_line_count(const qa_console_history *history);
/* Malloc'd array of borrowed line pointers, oldest first. */
const char **qa_console_history_lines(const qa_console_history *history, size_t *out_count);
void qa_console_history_replace(qa_console_history *history, const char *const *lines,
                                size_t count);
void qa_console_history_add(qa_console_history *history, const char *line);
/* Borrowed: a stored entry, `current`, or the draft. */
const char *qa_console_history_previous(qa_console_history *history, const char *current);
const char *qa_console_history_next(qa_console_history *history);

#endif
