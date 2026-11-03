/* Port of src/ui/common/controls.ts. GPL-2.0-or-later. */
#include "qa/ui/common/controls.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

qa_ui_control qa_ui_button_control(qa_ui_control_id id, const char *label, qa_rect rect,
                                   void (*activate)(void *ctx, qa_seat_id seat), void *ctx,
                                   bool enabled) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.kind = "button";
  control.label = label;
  control.rect = rect;
  control.enabled = enabled;
  control.visible = true;
  control.ctx = ctx;
  control.activate_seat = activate;
  return control;
}

/* One UTF-8 character: its byte length, code point and UTF-16 unit count. */
static size_t utf8_char(const char *text, uint32_t *code, size_t *units) {
  const unsigned char *p = (const unsigned char *)text;
  size_t n = p[0] < 0x80 ? 1 : (p[0] & 0xe0) == 0xc0 ? 2 : (p[0] & 0xf0) == 0xe0 ? 3
           : (p[0] & 0xf8) == 0xf0 ? 4 : 1;
  for (size_t i = 1; i < n; i++)
    if ((p[i] & 0xc0) != 0x80) n = 1;
  uint32_t cp = n == 1 ? p[0] : n == 2 ? (uint32_t)(p[0] & 0x1f) : n == 3 ? (uint32_t)(p[0] & 0x0f)
              : (uint32_t)(p[0] & 0x07);
  for (size_t i = 1; i < n; i++) cp = (cp << 6) | (uint32_t)(p[i] & 0x3f);
  *code = cp;
  *units = cp >= 0x10000 ? 2 : 1;
  return n;
}

/* JS \s: WhiteSpace and LineTerminator. */
static bool js_space(uint32_t cp) {
  return cp == 9 || cp == 10 || cp == 11 || cp == 12 || cp == 13 || cp == 32 || cp == 0xa0 ||
         cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 ||
         cp == 0x202f || cp == 0x205f || cp == 0x3000 || cp == 0xfeff;
}

/* Byte length of the first `width` UTF-16 units of [text, text + length). */
static size_t unit_prefix(const char *text, size_t length, size_t width, size_t *taken_units) {
  size_t at = 0, units = 0;
  while (at < length) {
    uint32_t cp;
    size_t n_units;
    size_t n = utf8_char(text + at, &cp, &n_units);
    if (units + n_units > width) break;
    units += n_units;
    at += n;
  }
  *taken_units = units;
  return at;
}

static size_t unit_count(const char *text, size_t length) {
  size_t units;
  (void)unit_prefix(text, length, SIZE_MAX, &units);
  return units;
}

typedef struct wrap_state {
  char **lines;
  size_t count, capacity;
  char *line;
  size_t line_bytes, line_units, line_capacity;
} wrap_state;

static void push_line(wrap_state *state) {
  if (state->count == state->capacity) {
    size_t capacity = state->capacity == 0 ? 4 : state->capacity * 2;
    char **grown = realloc(state->lines, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    state->lines = grown;
    state->capacity = capacity;
  }
  char *copy = malloc(state->line_bytes + 1);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, state->line, state->line_bytes);
  copy[state->line_bytes] = '\0';
  state->lines[state->count++] = copy;
  state->line_bytes = 0;
  state->line_units = 0;
}

static void append(wrap_state *state, const char *bytes, size_t length, size_t units) {
  if (state->line_bytes + length + 1 > state->line_capacity) {
    size_t capacity = (state->line_bytes + length + 1) * 2;
    char *grown = realloc(state->line, capacity);
    if (grown == NULL) qa_range_error("Out of memory");
    state->line = grown;
    state->line_capacity = capacity;
  }
  memcpy(state->line + state->line_bytes, bytes, length);
  state->line_bytes += length;
  state->line_units += units;
}

static void wrap_word(wrap_state *state, const char *word, size_t length, size_t width) {
  const char *rest = word;
  size_t rest_length = length;
  for (;;) {
    size_t piece_units;
    size_t piece_length = unit_prefix(rest, rest_length, width, &piece_units);
    bool last = unit_count(rest, rest_length) <= width;
    if (state->line_bytes != 0 && state->line_units + 1 + piece_units > width) push_line(state);
    if (state->line_bytes != 0) append(state, " ", 1, 1);
    append(state, rest, piece_length, piece_units);
    if (last) break;
    push_line(state);
    rest += piece_length;
    rest_length -= piece_length;
  }
}

char **qa_ui_wrap_words(const char *text, size_t width, size_t *out_count) {
  wrap_state state;
  memset(&state, 0, sizeof state);
  size_t length = strlen(text);
  size_t word_start = 0, at = 0;
  while (at < length) {
    uint32_t cp;
    size_t units;
    size_t n = utf8_char(text + at, &cp, &units);
    if (!js_space(cp)) {
      at += n;
      continue;
    }
    wrap_word(&state, text + word_start, at - word_start, width);
    while (at < length) {
      n = utf8_char(text + at, &cp, &units);
      if (!js_space(cp)) break;
      at += n;
    }
    word_start = at;
  }
  wrap_word(&state, text + word_start, length - word_start, width);
  if (state.line_bytes != 0) push_line(&state);
  free(state.line);
  *out_count = state.count;
  return state.lines;
}

void qa_ui_wrapped_lines_free(char **lines, size_t count) {
  for (size_t i = 0; i < count; i++) free(lines[i]);
  free(lines);
}
