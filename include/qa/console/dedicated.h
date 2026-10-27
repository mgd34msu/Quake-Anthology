/*
 * Port of src/console/dedicated.ts (DedicatedConsole): dedicated
 * stdin lines with partial-input retention between polls, drained
 * into a command buffer.
 * C accommodations (per docs/PORTING.md, Node streams have no C
 * counterpart): the Readable's data/end/error events become the
 * pumped qa_dedicated_console_feed()/_end()/_fail() calls; close()
 * detaches them (later feeds are ignored, as off() makes them).
 * StringDecoder("utf8") retention is byte-exact here for the line
 * split itself ('\n'/'\r' never appear inside a UTF-8 sequence),
 * but invalid or unfinished UTF-8 that the decoder would replace
 * with U+FFFD is kept as raw bytes.
 * Ownership: the console owns queued line strings until drained;
 * qa_dedicated_console_destroy() frees any left queued.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_DEDICATED_H
#define QA_CONSOLE_DEDICATED_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/core/commands/index.h"
#include "qa/foundation/event_ring.h"

typedef struct qa_dedicated_console {
  qa_event_ring lines; /* char * payloads, capacity 16384 */
  char *pending;
  size_t pending_length;
  size_t pending_capacity;
  bool ended;
  bool closed;
  char *error_message; /* NULL = TS error null */
} qa_dedicated_console;

/* False only on OOM. */
bool qa_dedicated_console_init(qa_dedicated_console *console);
void qa_dedicated_console_destroy(qa_dedicated_console *console);
/* The Readable "data" event (Buffer|string reach C as bytes). */
void qa_dedicated_console_feed(qa_dedicated_console *console,
                               const char *data, size_t length);
void qa_dedicated_console_feed_text(qa_dedicated_console *console,
                                    const char *text);
/* The Readable "end" event: a final unterminated line is queued. */
void qa_dedicated_console_end(qa_dedicated_console *console);
/* The Readable "error" event: the message is copied and rethrown
 * (aborted) by the next drain, as the TS throws the Error. */
void qa_dedicated_console_fail(qa_dedicated_console *console,
                               const char *message);
bool qa_dedicated_console_eof(const qa_dedicated_console *console);
/* Appends every queued line plus '\n'; returns the line count.
 * Aborts when closed or after an input error (TS throws). */
size_t qa_dedicated_console_drain(qa_dedicated_console *console,
                                  qa_command_buffer *commands);
void qa_dedicated_console_close(qa_dedicated_console *console);

#endif
