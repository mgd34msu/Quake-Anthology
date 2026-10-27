/*
 * Port of src/console/buffer.ts (ConsoleBuffer): Quake console word
 * wrapping, carriage-return overwrite, colors and notify times.
 * C accommodations:
 *  - A TS cell `character: string` (one code point) becomes a uint32_t
 *    code point. Input text is NUL-terminated bytes decoded as UTF-8;
 *    a byte that does not start a valid UTF-8 sequence is one code
 *    point of its own value (Quake high bytes). dump() re-encodes
 *    UTF-8, so dump bytes can differ from the TS UTF-16 string for
 *    non-ASCII input; code-point counts (width, wrapping) match.
 *  - `timeMilliseconds: number | null` becomes has_time + double.
 *  - visible()/notifications() snapshots are malloc'd row copies;
 *    free with qa_console_rows_free(). dump() is malloc'd.
 *  - The `wrapped` field of the internal TS row is exposed; the
 *    exported TS ConsoleRow type omits it but the value is present.
 * Ownership: the buffer owns its rows; qa_console_buffer_destroy()
 * frees them. GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_BUFFER_H
#define QA_CONSOLE_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/common.h"

#define QA_CONSOLE_DEFAULT_WIDTH 78
#define QA_CONSOLE_DEFAULT_CHARACTER_CAPACITY 32768

typedef struct qa_console_cell {
  uint32_t character;
  int32_t color;
  bool alternate;
} qa_console_cell;

typedef struct qa_console_row {
  int64_t sequence;
  bool wrapped;
  qa_console_cell *cells;
  size_t cell_count;
  size_t cell_capacity;
  bool has_time;
  double time_ms;
} qa_console_row;

typedef struct qa_console_buffer {
  qa_command_dialect dialect;
  int64_t width;
  int64_t character_capacity;
  qa_console_row *rows;
  size_t row_count;
  size_t row_capacity;
  int64_t x;
  int64_t write_row;
  int64_t next_sequence;
  int64_t backscroll;
} qa_console_buffer;

/* Invalid width aborts (TS RangeError). False only on OOM. */
bool qa_console_buffer_init(qa_console_buffer *buffer,
                            qa_command_dialect dialect, int64_t width,
                            int64_t character_capacity);
void qa_console_buffer_destroy(qa_console_buffer *buffer);
qa_command_dialect qa_console_buffer_dialect_of(const qa_console_buffer *buffer);
void qa_console_buffer_set_dialect(qa_console_buffer *buffer,
                                   qa_command_dialect dialect);
void qa_console_buffer_print(qa_console_buffer *buffer, const char *text,
                             double time_ms);
/* Invalid width aborts (TS RangeError). */
void qa_console_buffer_resize(qa_console_buffer *buffer, int64_t width);
void qa_console_buffer_clear(qa_console_buffer *buffer);
void qa_console_buffer_clear_notify(qa_console_buffer *buffer);
void qa_console_buffer_scroll(qa_console_buffer *buffer, double lines);
void qa_console_buffer_bottom(qa_console_buffer *buffer);
void qa_console_buffer_top(qa_console_buffer *buffer);
/* Malloc'd snapshot rows in display order; caller frees. */
qa_console_row *qa_console_buffer_visible(const qa_console_buffer *buffer,
                                          int64_t count, size_t *out_count);
/* TS defaults are duration 3000 and count 4. Caller frees. */
qa_console_row *qa_console_buffer_notifications(const qa_console_buffer *buffer,
                                                double now, double duration,
                                                int64_t count, size_t *out_count);
void qa_console_rows_free(qa_console_row *rows, size_t count);
/* Malloc'd dump text; caller frees. */
char *qa_console_buffer_dump(const qa_console_buffer *buffer);

#endif
