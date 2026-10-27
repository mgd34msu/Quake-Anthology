/* Port of src/console/buffer.ts. GPL-2.0-or-later. */
#include "qa/console/buffer.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

static bool row_reserve(qa_console_row *row, size_t needed) {
  if (needed <= row->cell_capacity) return true;
  size_t cap = row->cell_capacity == 0 ? 16 : row->cell_capacity;
  while (cap < needed) cap *= 2;
  qa_console_cell *cells =
      (qa_console_cell *)realloc(row->cells, cap * sizeof *cells);
  if (cells == NULL) return false;
  row->cells = cells;
  row->cell_capacity = cap;
  return true;
}

static bool rows_reserve(qa_console_buffer *buffer, size_t needed) {
  if (needed <= buffer->row_capacity) return true;
  size_t cap = buffer->row_capacity == 0 ? 8 : buffer->row_capacity;
  while (cap < needed) cap *= 2;
  qa_console_row *rows =
      (qa_console_row *)realloc(buffer->rows, cap * sizeof *rows);
  if (rows == NULL) return false;
  buffer->rows = rows;
  buffer->row_capacity = cap;
  return true;
}

static void row_init(qa_console_row *row, int64_t sequence, bool has_time,
                     double time_ms) {
  memset(row, 0, sizeof *row);
  row->sequence = sequence;
  row->has_time = has_time;
  row->time_ms = time_ms;
}

bool qa_console_buffer_init(qa_console_buffer *buffer,
                            qa_command_dialect dialect, int64_t width,
                            int64_t character_capacity) {
  if (width < 1 || width > character_capacity) {
    qa_range_error("Invalid console width");
  }
  memset(buffer, 0, sizeof *buffer);
  buffer->dialect = dialect;
  buffer->width = width;
  buffer->character_capacity = character_capacity;
  buffer->next_sequence = 1;
  if (!rows_reserve(buffer, 1)) return false;
  row_init(&buffer->rows[0], 0, false, 0);
  buffer->row_count = 1;
  return true;
}

void qa_console_buffer_destroy(qa_console_buffer *buffer) {
  for (size_t i = 0; i < buffer->row_count; i++) free(buffer->rows[i].cells);
  free(buffer->rows);
  memset(buffer, 0, sizeof *buffer);
}

qa_command_dialect qa_console_buffer_dialect_of(const qa_console_buffer *buffer) {
  return buffer->dialect;
}

void qa_console_buffer_set_dialect(qa_console_buffer *buffer,
                                   qa_command_dialect dialect) {
  buffer->dialect = dialect;
}

static qa_console_row *current_row(qa_console_buffer *buffer) {
  if (buffer->write_row < 0 || (size_t)buffer->write_row >= buffer->row_count) {
    qa_error("Console has no current row");
  }
  return &buffer->rows[buffer->write_row];
}

static void trim_rows(qa_console_buffer *buffer) {
  int64_t maximum = buffer->character_capacity / buffer->width;
  if (maximum < 1) maximum = 1;
  if ((int64_t)buffer->row_count > maximum) {
    size_t removed = buffer->row_count - (size_t)maximum;
    for (size_t i = 0; i < removed; i++) free(buffer->rows[i].cells);
    memmove(buffer->rows, buffer->rows + removed,
            (buffer->row_count - removed) * sizeof *buffer->rows);
    buffer->row_count -= removed;
    buffer->write_row -= (int64_t)removed;
    if (buffer->write_row < 0) buffer->write_row = 0;
  }
  int64_t max_back = buffer->row_count == 0 ? 0 : (int64_t)buffer->row_count - 1;
  if (buffer->backscroll > max_back) buffer->backscroll = max_back;
}

static void linefeed(qa_console_buffer *buffer, bool has_time, double time_ms,
                     bool wrapped) {
  qa_console_row *row = current_row(buffer);
  row->wrapped = wrapped;
  row->has_time = has_time;
  row->time_ms = time_ms;
  buffer->write_row++;
  if ((size_t)buffer->write_row == buffer->row_count) {
    if (buffer->backscroll != 0) buffer->backscroll++;
    if (rows_reserve(buffer, buffer->row_count + 1)) {
      row_init(&buffer->rows[buffer->row_count], buffer->next_sequence++,
               has_time, time_ms);
      buffer->row_count++;
    } else {
      buffer->write_row--;
    }
  }
  buffer->x = 0;
  trim_rows(buffer);
}

/* Decodes one code point; invalid bytes decode as their own value. */
static size_t decode_one(const char *text, size_t length, size_t at,
                         uint32_t *out) {
  const uint8_t *bytes = (const uint8_t *)text;
  uint8_t first = bytes[at];
  if (first < 0x80) {
    *out = first;
    return 1;
  }
  int extra = 0;
  uint32_t value = 0;
  uint32_t minimum = 0;
  if ((first & 0xe0) == 0xc0) {
    extra = 1; value = first & 0x1f; minimum = 0x80;
  } else if ((first & 0xf0) == 0xe0) {
    extra = 2; value = first & 0x0f; minimum = 0x800;
  } else if ((first & 0xf8) == 0xf0) {
    extra = 3; value = first & 0x07; minimum = 0x10000;
  } else {
    *out = first;
    return 1;
  }
  if (at + (size_t)extra >= length) {
    *out = first;
    return 1;
  }
  for (int i = 1; i <= extra; i++) {
    uint8_t next = bytes[at + (size_t)i];
    if ((next & 0xc0) != 0x80) {
      *out = first;
      return 1;
    }
    value = (value << 6) | (uint32_t)(next & 0x3f);
  }
  if (value < minimum || value > 0x10ffff ||
      (value >= 0xd800 && value <= 0xdfff)) {
    *out = first;
    return 1;
  }
  *out = value;
  return (size_t)extra + 1;
}

static uint32_t *decode_all(const char *text, size_t length, size_t *out_count) {
  uint32_t *points = (uint32_t *)malloc((length + 1) * sizeof *points);
  if (points == NULL) {
    *out_count = 0;
    return NULL;
  }
  size_t count = 0;
  size_t at = 0;
  while (at < length) at += decode_one(text, length, at, &points[count++]);
  *out_count = count;
  return points;
}

void qa_console_buffer_print(qa_console_buffer *buffer, const char *text,
                             double time_ms) {
  const char *input = text;
  bool skip_notify = strncmp(input, "[skipnotify]", 12) == 0;
  bool has_time = !skip_notify;
  double time_value = skip_notify ? 0 : time_ms;
  if (skip_notify) input += 12;
  bool alternate = false;
  int32_t color = 7;
  if (buffer->dialect != QA_COMMAND_DIALECT_Q3 &&
      (input[0] == '\x01' || input[0] == '\x02')) {
    alternate = true;
    input += 1;
  }
  size_t count = 0;
  uint32_t *characters = decode_all(input, strlen(input), &count);
  if (characters == NULL) return;
  for (size_t index = 0; index < count; index++) {
    uint32_t character = characters[index];
    bool has_next = index + 1 < count;
    uint32_t next = has_next ? characters[index + 1] : 0;
    if ((buffer->dialect == QA_COMMAND_DIALECT_Q3 ||
         buffer->dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) &&
        character == (uint32_t)'^' && has_next && next != (uint32_t)'^' &&
        next >= (uint32_t)'0' && next <= (uint32_t)'9') {
      color = (int32_t)(next - 48) & 7;
      index++;
      continue;
    }
    if (character == (uint32_t)'\n') {
      linefeed(buffer, has_time, time_value, false);
      continue;
    }
    if (character == (uint32_t)'\r') {
      buffer->x = 0;
      continue;
    }
    if (character > (uint32_t)' ') {
      int64_t word = 0;
      while (word < buffer->width && index + (size_t)word < count &&
             characters[index + (size_t)word] > (uint32_t)' ') {
        word++;
      }
      if (word < buffer->width && buffer->x + word >= buffer->width) {
        linefeed(buffer, has_time, time_value, true);
      }
    }
    qa_console_row *row = current_row(buffer);
    if (!row_reserve(row, (size_t)buffer->x + 1)) {
      free(characters);
      return;
    }
    row->cells[buffer->x].character = character;
    row->cells[buffer->x].color = color;
    row->cells[buffer->x].alternate = alternate;
    if ((size_t)buffer->x >= row->cell_count) {
      row->cell_count = (size_t)buffer->x + 1;
    }
    buffer->x++;
    row->has_time = has_time;
    row->time_ms = time_value;
    if (buffer->x >= buffer->width) linefeed(buffer, has_time, time_value, true);
  }
  free(characters);
}

typedef struct cell_list {
  qa_console_cell *cells;
  size_t count;
  size_t capacity;
} cell_list;

static bool cell_list_push(cell_list *list, const qa_console_cell *cells,
                           size_t count) {
  if (count == 0) return true;
  if (list->count + count > list->capacity) {
    size_t cap = list->capacity == 0 ? 32 : list->capacity;
    while (cap < list->count + count) cap *= 2;
    qa_console_cell *grown =
        (qa_console_cell *)realloc(list->cells, cap * sizeof *grown);
    if (grown == NULL) return false;
    list->cells = grown;
    list->capacity = cap;
  }
  memcpy(list->cells + list->count, cells, count * sizeof *cells);
  list->count += count;
  return true;
}

typedef struct row_list {
  qa_console_row *rows;
  size_t count;
  size_t capacity;
} row_list;

static bool row_list_push(row_list *list, int64_t sequence, bool wrapped,
                          const qa_console_cell *cells, size_t count) {
  if (list->count == list->capacity) {
    size_t cap = list->capacity == 0 ? 8 : list->capacity * 2;
    qa_console_row *grown =
        (qa_console_row *)realloc(list->rows, cap * sizeof *grown);
    if (grown == NULL) return false;
    list->rows = grown;
    list->capacity = cap;
  }
  qa_console_row *row = &list->rows[list->count];
  row_init(row, sequence, false, 0);
  row->wrapped = wrapped;
  if (count > 0) {
    if (!row_reserve(row, count)) return false;
    memcpy(row->cells, cells, count * sizeof *cells);
    row->cell_count = count;
  }
  list->count++;
  return true;
}

void qa_console_buffer_resize(qa_console_buffer *buffer, int64_t width) {
  if (width < 1 || width > buffer->character_capacity) {
    qa_range_error("Invalid console width");
  }
  if (width == buffer->width) return;
  qa_console_row *previous = buffer->rows;
  size_t previous_count = buffer->row_count;
  int64_t anchor = (int64_t)previous_count - 1 - buffer->backscroll;
  bool following = buffer->backscroll == 0;
  row_list rebuilt = {NULL, 0, 0};
  cell_list paragraph = {NULL, 0, 0};
  int64_t cursor_offset = -1;
  int64_t anchor_offset = -1;
  int64_t new_anchor = 0;
  int64_t new_cursor = 0;
  int64_t new_write_row = 0;

  for (size_t index = 0; index < previous_count; index++) {
    if ((int64_t)index == anchor) anchor_offset = (int64_t)paragraph.count;
    if ((int64_t)index == buffer->write_row) {
      cursor_offset = (int64_t)paragraph.count + buffer->x;
    }
    (void)cell_list_push(&paragraph, previous[index].cells,
                         previous[index].cell_count);
    bool flush = !previous[index].wrapped;
    bool last_flush = index + 1 == previous_count && paragraph.count > 0;
    if (!flush && !last_flush) continue;
    /* flush() */
    size_t start = rebuilt.count;
    size_t total = paragraph.count;
    for (size_t offset = 0; offset < total || offset == 0;
         offset += (size_t)width) {
      size_t slice = total - offset;
      if (slice > (size_t)width) slice = (size_t)width;
      bool wrapped = offset + (size_t)width < total;
      const qa_console_cell *from =
          slice == 0 ? NULL : paragraph.cells + offset;
      (void)row_list_push(&rebuilt, buffer->next_sequence++, wrapped, from,
                          slice);
      if (total == 0) break;
    }
    if (anchor_offset >= 0) {
      int64_t span = (int64_t)(rebuilt.count - start) - 1;
      int64_t at = anchor_offset / width;
      new_anchor = (int64_t)start + (span < at ? span : at);
    }
    if (cursor_offset >= 0) {
      new_cursor = cursor_offset % width;
      new_write_row = (int64_t)start + cursor_offset / width;
      if (new_write_row == (int64_t)rebuilt.count) {
        if (rebuilt.count > 0) rebuilt.rows[rebuilt.count - 1].wrapped = true;
        (void)row_list_push(&rebuilt, buffer->next_sequence++, false, NULL, 0);
      }
    }
    paragraph.count = 0;
    anchor_offset = -1;
    cursor_offset = -1;
    if (!flush && last_flush) break;
  }
  free(paragraph.cells);
  for (size_t i = 0; i < previous_count; i++) free(previous[i].cells);
  free(previous);
  buffer->rows = rebuilt.rows;
  buffer->row_count = rebuilt.count;
  buffer->row_capacity = rebuilt.capacity;
  buffer->width = width;
  buffer->x = new_cursor;
  buffer->write_row = new_write_row;
  buffer->backscroll =
      following ? 0 : (int64_t)rebuilt.count - 1 - new_anchor;
  trim_rows(buffer);
}

void qa_console_buffer_clear(qa_console_buffer *buffer) {
  for (size_t i = 0; i < buffer->row_count; i++) free(buffer->rows[i].cells);
  buffer->row_count = 0;
  if (rows_reserve(buffer, 1)) {
    row_init(&buffer->rows[0], buffer->next_sequence++, false, 0);
    buffer->row_count = 1;
  }
  buffer->x = 0;
  buffer->write_row = 0;
  buffer->backscroll = 0;
}

void qa_console_buffer_clear_notify(qa_console_buffer *buffer) {
  for (size_t i = 0; i < buffer->row_count; i++) {
    buffer->rows[i].has_time = false;
    buffer->rows[i].time_ms = 0;
  }
}

void qa_console_buffer_scroll(qa_console_buffer *buffer, double lines) {
  int64_t max_back =
      buffer->row_count == 0 ? 0 : (int64_t)buffer->row_count - 1;
  int64_t value = buffer->backscroll + (int64_t)trunc(lines);
  if (value < 0) value = 0;
  if (value > max_back) value = max_back;
  buffer->backscroll = value;
}

void qa_console_buffer_bottom(qa_console_buffer *buffer) {
  buffer->backscroll = 0;
}

void qa_console_buffer_top(qa_console_buffer *buffer) {
  buffer->backscroll =
      buffer->row_count == 0 ? 0 : (int64_t)buffer->row_count - 1;
}

static qa_console_row *copy_rows(const qa_console_buffer *buffer, size_t begin,
                                 size_t end, size_t *out_count) {
  if (end <= begin) {
    *out_count = 0;
    return NULL;
  }
  size_t count = end - begin;
  qa_console_row *out = (qa_console_row *)calloc(count, sizeof *out);
  if (out == NULL) {
    *out_count = 0;
    return NULL;
  }
  for (size_t i = 0; i < count; i++) {
    const qa_console_row *source = &buffer->rows[begin + i];
    out[i] = *source;
    out[i].cells = NULL;
    out[i].cell_capacity = 0;
    if (source->cell_count > 0 && row_reserve(&out[i], source->cell_count)) {
      memcpy(out[i].cells, source->cells,
             source->cell_count * sizeof *source->cells);
      out[i].cell_count = source->cell_count;
    } else {
      out[i].cell_count = 0;
    }
  }
  *out_count = count;
  return out;
}

qa_console_row *qa_console_buffer_visible(const qa_console_buffer *buffer,
                                          int64_t count, size_t *out_count) {
  int64_t end = (int64_t)buffer->row_count - buffer->backscroll;
  if (end < 0) end = 0;
  int64_t begin = end - count;
  if (begin < 0) begin = 0;
  return copy_rows(buffer, (size_t)begin, (size_t)end, out_count);
}

qa_console_row *qa_console_buffer_notifications(const qa_console_buffer *buffer,
                                                double now, double duration,
                                                int64_t count,
                                                size_t *out_count) {
  /* JS rows.slice(-count): -0 is 0 (all rows); negative counts slice
   * from a positive start. */
  int64_t start = -count;
  int64_t begin = start < 0
      ? (int64_t)buffer->row_count + start
      : start;
  if (begin < 0) begin = 0;
  if (begin > (int64_t)buffer->row_count) begin = (int64_t)buffer->row_count;
  size_t kept = 0;
  for (size_t i = (size_t)begin; i < buffer->row_count; i++) {
    if (buffer->rows[i].has_time &&
        now - buffer->rows[i].time_ms <= duration) {
      kept++;
    }
  }
  if (kept == 0) {
    *out_count = 0;
    return NULL;
  }
  qa_console_row *out = (qa_console_row *)calloc(kept, sizeof *out);
  if (out == NULL) {
    *out_count = 0;
    return NULL;
  }
  size_t at = 0;
  for (size_t i = (size_t)begin; i < buffer->row_count; i++) {
    const qa_console_row *source = &buffer->rows[i];
    if (!source->has_time || now - source->time_ms > duration) continue;
    out[at] = *source;
    out[at].cells = NULL;
    out[at].cell_capacity = 0;
    out[at].cell_count = 0;
    if (source->cell_count > 0 && row_reserve(&out[at], source->cell_count)) {
      memcpy(out[at].cells, source->cells,
             source->cell_count * sizeof *source->cells);
      out[at].cell_count = source->cell_count;
    }
    at++;
  }
  *out_count = at;
  return out;
}

void qa_console_rows_free(qa_console_row *rows, size_t count) {
  if (rows == NULL) return;
  for (size_t i = 0; i < count; i++) free(rows[i].cells);
  free(rows);
}

static bool is_js_whitespace(uint32_t cp) {
  return cp == 0x09 || cp == 0x0a || cp == 0x0b || cp == 0x0c || cp == 0x0d ||
         cp == 0x20 || cp == 0xa0 || cp == 0x1680 ||
         (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 ||
         cp == 0x202f || cp == 0x205f || cp == 0x3000 || cp == 0xfeff;
}

typedef struct byte_list {
  char *data;
  size_t length;
  size_t capacity;
} byte_list;

static bool byte_list_push(byte_list *list, const char *bytes, size_t count) {
  if (list->length + count + 1 > list->capacity) {
    size_t cap = list->capacity == 0 ? 64 : list->capacity;
    while (cap < list->length + count + 1) cap *= 2;
    char *grown = (char *)realloc(list->data, cap);
    if (grown == NULL) return false;
    list->data = grown;
    list->capacity = cap;
  }
  memcpy(list->data + list->length, bytes, count);
  list->length += count;
  list->data[list->length] = '\0';
  return true;
}

static bool byte_list_push_cp(byte_list *list, uint32_t cp) {
  char encoded[4];
  size_t count = 0;
  if (cp < 0x80) {
    encoded[0] = (char)cp;
    count = 1;
  } else if (cp < 0x800) {
    encoded[0] = (char)(0xc0 | (cp >> 6));
    encoded[1] = (char)(0x80 | (cp & 0x3f));
    count = 2;
  } else if (cp < 0x10000) {
    encoded[0] = (char)(0xe0 | (cp >> 12));
    encoded[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    encoded[2] = (char)(0x80 | (cp & 0x3f));
    count = 3;
  } else {
    encoded[0] = (char)(0xf0 | (cp >> 18));
    encoded[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    encoded[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    encoded[3] = (char)(0x80 | (cp & 0x3f));
    count = 4;
  }
  return byte_list_push(list, encoded, count);
}

char *qa_console_buffer_dump(const qa_console_buffer *buffer) {
  byte_list out = {NULL, 0, 0};
  for (size_t r = 0; r < buffer->row_count; r++) {
    const qa_console_row *row = &buffer->rows[r];
    size_t end = row->cell_count;
    while (end > 0 && is_js_whitespace(row->cells[end - 1].character)) end--;
    for (size_t i = 0; i < end; i++) {
      (void)byte_list_push_cp(&out, row->cells[i].character);
    }
    (void)byte_list_push(&out, "\n", 1);
  }
  if (out.data == NULL) {
    out.data = (char *)malloc(2);
    if (out.data != NULL) {
      out.data[0] = '\n';
      out.data[1] = '\0';
    }
    /* Zero rows cannot occur through the public API; TS join is "\n". */
    return out.data;
  }
  return out.data;
}
