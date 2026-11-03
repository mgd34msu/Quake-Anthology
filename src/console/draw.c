/* Port of src/console/draw.ts. GPL-2.0-or-later. */
#include "qa/console/draw.h"
#include "qa/foundation/js.h"

#include "qa/foundation/support.h"
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}

static const qa_vec4 colors[8] = {
    {0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}, {1, 1, 0, 1},
    {0, 0, 1, 1}, {0, 1, 1, 1}, {1, 0, 1, 1}, {1, 1, 1, 1}};
static const qa_vec4 white = {1, 1, 1, 1};

static char *format_text(const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  char *out = NULL;
  if (needed >= 0) {
    out = (char *)malloc((size_t)needed + 1);
    if (out != NULL) vsnprintf(out, (size_t)needed + 1, format, args);
  }
  va_end(args);
  return out;
}

/* Same JSON.stringify(string) helper as discovery.c. */
static char *json_stringify(const char *text) {
  size_t length = strlen(text);
  size_t capacity = length * 6 + 3;
  char *out = (char *)malloc(capacity);
  if (out == NULL) return NULL;
  size_t at = 0;
  out[at++] = '"';
  for (size_t i = 0; i < length; i++) {
    unsigned char ch = (unsigned char)text[i];
    switch (ch) {
      case '"': out[at++] = '\\'; out[at++] = '"'; break;
      case '\\': out[at++] = '\\'; out[at++] = '\\'; break;
      case '\b': out[at++] = '\\'; out[at++] = 'b'; break;
      case '\f': out[at++] = '\\'; out[at++] = 'f'; break;
      case '\n': out[at++] = '\\'; out[at++] = 'n'; break;
      case '\r': out[at++] = '\\'; out[at++] = 'r'; break;
      case '\t': out[at++] = '\\'; out[at++] = 't'; break;
      default:
        if (ch < 0x20) {
          at += (size_t)snprintf(out + at, capacity - at, "\\u%04x", ch);
        } else {
          out[at++] = (char)ch;
        }
        break;
    }
  }
  out[at++] = '"';
  out[at] = '\0';
  return out;
}

/* Same UTF-8 code-point decode as buffer.c. */
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

typedef struct cp_text {
  uint32_t *points;
  size_t count;
} cp_text;

static cp_text decode_text(const char *text) {
  cp_text out = {NULL, 0};
  size_t length = strlen(text);
  out.points = (uint32_t *)malloc((length + 1) * sizeof *out.points);
  if (out.points == NULL) return out;
  size_t at = 0;
  while (at < length) {
    at += decode_one(text, length, at, &out.points[out.count++]);
  }
  return out;
}

typedef struct draw_state {
  const qa_console_draw_options *options;
  qa_rect clip;
  double line_height;
  double cell_width;
} draw_state;

static double measure(const draw_state *state, size_t count) {
  return (double)count * state->cell_width;
}

static void draw_cells(draw_state *state, const uint32_t *points, size_t count,
                       double x, double y, qa_vec4 color, bool alternate) {
  const qa_console_text_presentation *text = state->options->text;
  for (size_t i = 0; i < count; i++) {
    double glyph_line_height = state->line_height;
    double top = 0;
    text->glyph_metrics(text->ctx, text->font, points[i], state->line_height,
                        state->cell_width, &glyph_line_height, &top);
    double width = text->layout_width(text->ctx, points[i],
                                      state->options->scale, color, alternate,
                                      glyph_line_height);
    text->draw_character(text->ctx, points[i], state->options->scale, color,
                         alternate, glyph_line_height,
                         x + (state->cell_width - width) / 2, y - top,
                         state->clip);
    x += state->cell_width;
  }
}

/* fit(): returns a malloc'd code-point array in out (caller frees
 * out->points); \r \n \t become spaces first, as in TS. */
static void fit_text(draw_state *state, const uint32_t *points, size_t count,
                     double maximum, bool ellipsis, cp_text *out) {
  out->points = (uint32_t *)malloc((count + 3) * sizeof *out->points);
  out->count = 0;
  if (out->points == NULL) return;
  uint32_t *clean = (uint32_t *)malloc((count + 1) * sizeof *clean);
  if (clean == NULL) {
    free(out->points);
    out->points = NULL;
    return;
  }
  for (size_t i = 0; i < count; i++) {
    uint32_t cp = points[i];
    clean[i] = cp == '\r' || cp == '\n' || cp == '\t' ? (uint32_t)' ' : cp;
  }
  if (measure(state, count) <= maximum) {
    memcpy(out->points, clean, count * sizeof *clean);
    out->count = count;
    free(clean);
    return;
  }
  size_t suffix = ellipsis && measure(state, 3) <= maximum ? 3 : 0;
  size_t low = 0;
  size_t high = count;
  while (low < high) {
    size_t middle = (low + high + 1) / 2; /* ceil((low+high)/2) */
    if (measure(state, middle + suffix) <= maximum) {
      low = middle;
    } else {
      high = middle - 1;
    }
  }
  memcpy(out->points, clean, low * sizeof *clean);
  out->count = low;
  for (size_t i = 0; i < suffix; i++) out->points[out->count++] = '.';
  free(clean);
}

static int64_t clamp_index(int64_t index, size_t count) {
  if (index < 0) return 0;
  if (index > (int64_t)count) return (int64_t)count;
  return index;
}

void qa_draw_console(const qa_console_draw_options *options) {
  const qa_console_draw_target *draw = options->draw;
  const qa_console_text_presentation *text = options->text;
  double scale = options->scale;
  qa_rect bounds = {0, 0, draw->width,
                    options->height < draw->height ? options->height
                                                     : draw->height};
  if (!qa_same_seat(draw->seat, text->seat)) {
    qa_error("Console text belongs to another viewport seat");
  }
  if (options->background != NULL) {
    qa_rect rect = {0, 0, draw->width, options->height};
    draw->draw_pic(draw->ctx, rect, options->background, bounds);
  }
  draw_state state = {options, bounds, 8 * scale, 0};
  state.cell_width = options->has_cell_width ? options->cell_width
                                             : state.line_height;
  int64_t field_lines = options->field == NULL ? 0 : 1;
  double available_width = draw->width - 16 * scale;
  if (available_width < 1) available_width = 1;

  char *help[3] = {NULL, NULL, NULL};
  size_t help_count = 0;
  const qa_console_discovery_entry *entry = options->selected_entry;
  if (options->field != NULL && entry != NULL) {
    help[help_count++] = entry->usage == NULL
        ? format_text("%s", entry->name != NULL ? entry->name : "")
        : format_text("Usage: %s", entry->usage);
    if (entry->summary != NULL) {
      help[help_count++] = format_text("%s", entry->summary);
    }
    if (entry->kind == QA_CONSOLE_ENTRY_CVAR) {
      char *value = json_stringify(entry->value != NULL ? entry->value : "");
      help[help_count++] =
          format_text("Current: %s", value != NULL ? value : "\"\"");
      free(value);
    } else if (entry->kind == QA_CONSOLE_ENTRY_ALIAS) {
      help[help_count++] =
          format_text("Expands to: %s", entry->value != NULL ? entry->value : "");
    }
  }

  double height_limit =
      options->height < draw->height ? options->height : draw->height;
  double bottom = height_limit - 2 * scale;
  if (bottom < 0) bottom = 0;
  int64_t total_rows = (int64_t)floor(bottom / state.line_height);
  if (total_rows < 0) total_rows = 0;
  if (total_rows < field_lines) {
    for (size_t i = 0; i < help_count; i++) free(help[i]);
    return;
  }
  int64_t spare = total_rows - field_lines;
  size_t help_lines = help_count;
  if (help_lines > 3) help_lines = 3;
  if (help_lines > (size_t)spare) help_lines = (size_t)spare;
  int64_t room = spare - (int64_t)help_lines;
  size_t row_count = options->row_count;
  if (row_count > (size_t)room) row_count = (size_t)room;
  double y = bottom - state.line_height *
                          (double)(row_count + (size_t)field_lines +
                                   help_lines);
  size_t first_row = options->row_count - row_count;
  for (size_t r = first_row; r < options->row_count; r++) {
    const qa_console_row *row = &options->rows[r];
    double x = 8 * scale;
    size_t index = 0;
    while (index < row->cell_count) {
      const qa_console_cell *first = &row->cells[index];
      size_t run_start = index;
      index++;
      while (index < row->cell_count &&
             row->cells[index].color == first->color &&
             row->cells[index].alternate == first->alternate) {
        index++;
      }
      size_t run = index - run_start;
      uint32_t *points = (uint32_t *)malloc(run * sizeof *points);
      if (points != NULL) {
        for (size_t i = 0; i < run; i++) {
          points[i] = row->cells[run_start + i].character;
        }
        qa_vec4 color = first->color >= 0 && first->color <= 7
                            ? colors[first->color]
                            : white;
        draw_cells(&state, points, run, x, y, color, first->alternate);
        free(points);
      }
      x += measure(&state, run);
    }
    y += state.line_height;
  }

  const qa_console_field_view *field = options->field;
  if (field != NULL) {
    cp_text characters = decode_text(field->text != NULL ? field->text : "");
    double cursor_width = measure(&state, 1);
    int64_t scroll = field->scroll < field->cursor ? field->scroll
                                                    : field->cursor;
    /* TS slices clamp to the array; positions beyond a hand-built
     * oversized cursor keep counting in TS `measure`, so only the
     * slice bounds are clamped here. */
    while (scroll < field->cursor) {
      int64_t from = clamp_index(scroll, characters.count);
      int64_t to = clamp_index(field->cursor, characters.count);
      size_t slice = to > from ? (size_t)(to - from) : 0;
      if (measure(&state, 1 + slice) + cursor_width <= available_width) break;
      scroll++;
    }
    double fit_width = available_width - measure(&state, 1) - cursor_width;
    if (fit_width < 0) fit_width = 0;
    int64_t visible_from = clamp_index(scroll, characters.count);
    cp_text visible = {NULL, 0};
    fit_text(&state, characters.points + visible_from,
             characters.count - (size_t)visible_from, fit_width, false,
             &visible);
    if (visible.points != NULL) {
      uint32_t *line =
          (uint32_t *)malloc((visible.count + 1) * sizeof *line);
      if (line != NULL) {
        line[0] = ']';
        memcpy(line + 1, visible.points, visible.count * sizeof *line);
        draw_cells(&state, line, visible.count + 1, 8 * scale, y, white,
                   false);
        free(line);
      }
      free(visible.points);
    }
    int32_t blink = qa_js_to_int32(trunc(options->now_ms / 256)); /* Math.trunc(t / 256) & 1 */
    if ((blink & 1) == 0) {
      int64_t from = clamp_index(scroll, characters.count);
      int64_t to = clamp_index(field->cursor, characters.count);
      size_t slice = to > from ? (size_t)(to - from) : 0;
      double width = measure(&state, 1 + slice);
      uint32_t caret = field->overstrike ? (uint32_t)'_' : (uint32_t)'|';
      draw_cells(&state, &caret, 1, 8 * scale + width, y, white, false);
    }
    free(characters.points);
    y += state.line_height;
    qa_vec4 help_color = {0.65, 0.85, 1, 1};
    for (size_t i = 0; i < help_lines; i++) {
      if (help[i] != NULL) {
        cp_text decoded = decode_text(help[i]);
        cp_text fitted = {NULL, 0};
        fit_text(&state, decoded.points, decoded.count, available_width,
                 true, &fitted);
        if (fitted.points != NULL) {
          draw_cells(&state, fitted.points, fitted.count, 8 * scale, y,
                     help_color, false);
          free(fitted.points);
        }
        free(decoded.points);
      }
      y += state.line_height;
    }
  }
  for (size_t i = 0; i < help_count; i++) free(help[i]);
}
