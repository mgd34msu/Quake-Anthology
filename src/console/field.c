/* Port of src/console/field.ts. GPL-2.0-or-later. */
#include "qa/console/field.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

/* KeyCode values from src/input/key-codes.ts (input module pending). */
enum {
  KEY_BACKSPACE = 127,
  KEY_LEFT = 134,
  KEY_RIGHT = 135,
  KEY_CONTROL = 137,
  KEY_SHIFT = 138,
  KEY_INSERT = 139,
  KEY_DELETE = 140,
  KEY_HOME = 143,
  KEY_END = 144,
  KEY_KEYPAD_INSERT = 170
};

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}
/* The TS field is an array of code points: cursor, scroll and
 * maximumLength count code points. The C buffer keeps UTF-8 bytes and
 * converts positions; a byte that does not start a valid sequence is
 * one code point, as the console buffer decodes it. */
static size_t cp_step(const char *text, size_t length, size_t at) {
  const unsigned char *bytes = (const unsigned char *)text;
  unsigned char first = bytes[at];
  size_t extra = (first & 0xe0) == 0xc0 ? 1 : (first & 0xf0) == 0xe0 ? 2 : (first & 0xf8) == 0xf0 ? 3 : 0;
  if (extra == 0 || at + extra >= length) return 1;
  for (size_t i = 1; i <= extra; i++) {
    if ((bytes[at + i] & 0xc0) != 0x80) return 1;
  }
  return extra + 1;
}

static uint32_t cp_decode(const char *text, size_t length, size_t at, size_t *step) {
  const unsigned char *bytes = (const unsigned char *)text;
  *step = cp_step(text, length, at);
  if (*step == 1) return bytes[at];
  uint32_t value = bytes[at] & (*step == 2 ? 0x1fu : *step == 3 ? 0x0fu : 0x07u);
  for (size_t i = 1; i < *step; i++) value = (value << 6) | (bytes[at + i] & 0x3fu);
  return value;
}

static size_t cp_count(const char *text, size_t length) {
  size_t count = 0;
  for (size_t at = 0; at < length; at += cp_step(text, length, at)) count++;
  return count;
}

/* Byte offset of code point `index`, clamped to [0, count]. */
static size_t cp_offset(const char *text, size_t length, int64_t index) {
  size_t at = 0;
  for (int64_t i = 0; i < index && at < length; i++) at += cp_step(text, length, at);
  return at;
}

static int64_t field_count(const qa_console_field *field) {
  return field->characters == NULL ? 0 : (int64_t)cp_count(field->characters, field->length);
}

/* JS \s: WhiteSpace and LineTerminator code points. */
static bool is_js_space(uint32_t cp) {
  return cp == 0x09 || cp == 0x0a || cp == 0x0b || cp == 0x0c || cp == 0x0d || cp == 0x20 ||
         cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 ||
         cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000 || cp == 0xfeff;
}
static char fold_byte(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static bool folded_starts_with(const char *text, const char *prefix, size_t prefix_length) {
  for (size_t i = 0; i < prefix_length; i++) {
    if (text[i] == '\0' || fold_byte(text[i]) != fold_byte(prefix[i])) return false;
  }
  return true;
}

static bool field_grow(qa_console_field *field, size_t needed) {
  if (needed <= field->capacity) return true;
  size_t capacity = field->capacity == 0 ? 64 : field->capacity;
  while (capacity < needed) capacity *= 2;
  char *data = (char *)realloc(field->characters, capacity);
  if (data == NULL) return false;
  field->characters = data;
  field->capacity = capacity;
  return true;
}

static void completion_clear(qa_console_field *field) {
  free(field->completion_text);
  free(field->completion_tail);
  free(field->completion_selected);
  for (size_t i = 0; i < field->completion_match_count; i++) free(field->completion_matches[i]);
  free(field->completion_matches);
  field->completion_text = NULL;
  field->completion_tail = NULL;
  field->completion_selected = NULL;
  field->completion_matches = NULL;
  field->completion_match_count = 0;
  field->completion_cursor = 0;
  field->completion_index = 0;
  field->has_completion = false;
}

bool qa_console_field_init(qa_console_field *field, int64_t maximum_length,
                           int64_t width_in_chars) {
  memset(field, 0, sizeof *field);
  field->maximum_length = maximum_length;
  field->width_in_chars = width_in_chars;
  return true;
}

void qa_console_field_destroy(qa_console_field *field) {
  completion_clear(field);
  free(field->characters);
  memset(field, 0, sizeof *field);
}

char *qa_console_field_text(const qa_console_field *field) {
  return dup_range(field->characters == NULL ? "" : field->characters, field->length);
}

static void keep_visible(qa_console_field *field) {
  int64_t scroll = field->scroll < field->cursor ? field->scroll : field->cursor;
  field->scroll = scroll > 0 ? scroll : 0;
  if (field->cursor >= field->scroll + field->width_in_chars) {
    int64_t width = field->width_in_chars > 1 ? field->width_in_chars : 1;
    field->scroll = field->cursor - width + 1;
  }
}

const char *qa_console_field_selected_completion(const qa_console_field *field) {
  if (!field->has_completion || field->completion_cursor != field->cursor) return NULL;
  size_t length = strlen(field->completion_text);
  if (length != field->length) return NULL;
  if (length > 0 && memcmp(field->completion_text, field->characters, length) != 0) return NULL;
  return field->completion_selected;
}

void qa_console_field_set_text(qa_console_field *field, const char *text) {
  completion_clear(field);
  /* text.replace(/[\x00-\x1f\x7f]/g, "") as code points, sliced to maximumLength. */
  size_t text_length = strlen(text);
  char *clean = (char *)malloc(text_length + 1);
  if (clean == NULL) return; /* OOM only */
  size_t clean_length = 0;
  for (size_t i = 0; i < text_length; i++)
    if (!((unsigned char)text[i] <= 0x1f || text[i] == '\x7f')) clean[clean_length++] = text[i];
  int64_t maximum = field->maximum_length > 0 ? field->maximum_length : 0;
  size_t keep = cp_offset(clean, clean_length, maximum);
  if (!field_grow(field, keep == 0 ? 1 : keep)) {
    free(clean);
    return; /* OOM only */
  }
  memcpy(field->characters, clean, keep);
  free(clean);
  field->length = keep;
  field->cursor = field_count(field);
  keep_visible(field);
}

void qa_console_field_clear(qa_console_field *field) {
  completion_clear(field);
  field->length = 0;
  field->cursor = 0;
  field->scroll = 0;
}

/* JS Array.splice(start, count) over the code points. */
static void remove_range(qa_console_field *field, int64_t start, int64_t count) {
  int64_t length = field_count(field);
  if (start < 0) start = length + start > 0 ? length + start : 0;
  if (start > length) start = length;
  if (count < 0) count = 0;
  if (start + count > length) count = length - start;
  if (count <= 0) return;
  size_t from = cp_offset(field->characters, field->length, start);
  size_t to = cp_offset(field->characters, field->length, start + count);
  memmove(field->characters + from, field->characters + to, field->length - to);
  field->length -= to - from;
}

void qa_console_field_insert(qa_console_field *field, const char *text) {
  completion_clear(field);
  size_t text_length = strlen(text);
  for (size_t p = 0; p < text_length;) {
    size_t step = cp_step(text, text_length, p);
    const char *character = text + p;
    p += step;
    /* character < " " compares the first UTF-16 unit. */
    if (step == 1 && ((unsigned char)character[0] < ' ' || character[0] == '\x7f')) continue;
    int64_t count = field_count(field);
    if (field->overstrike && field->cursor >= 0 && field->cursor < count) {
      size_t from = cp_offset(field->characters, field->length, field->cursor);
      size_t to = cp_offset(field->characters, field->length, field->cursor + 1);
      size_t length = field->length - (to - from) + step;
      if (!field_grow(field, length)) return; /* OOM only */
      memmove(field->characters + from + step, field->characters + to, field->length - to);
      memcpy(field->characters + from, character, step);
      field->length = length;
      field->cursor++;
    } else if (count < field->maximum_length) {
      if (!field_grow(field, field->length + step)) return; /* OOM only */
      int64_t at = field->cursor;
      if (at < 0) at = 0;
      if (at > count) at = count;
      size_t from = cp_offset(field->characters, field->length, at);
      memmove(field->characters + from + step, field->characters + from, field->length - from);
      memcpy(field->characters + from, character, step);
      field->length += step;
      field->cursor++;
    }
  }
  keep_visible(field);
}

/* The code point at index, or -1 for TS "" (outside the array). */
static int64_t field_char(const qa_console_field *field, int64_t index, int64_t count) {
  if (index < 0 || index >= count) return -1;
  size_t at = cp_offset(field->characters, field->length, index), step = 0;
  return cp_decode(field->characters, field->length, at, &step);
}

static int64_t word_position(const qa_console_field *field, int direction) {
  int64_t cursor = field->cursor;
  int64_t length = field_count(field);
  const int64_t offset = direction == -1 ? -1 : 0;
  while (cursor + direction >= 0 && cursor + direction <= length) {
    int64_t c = field_char(field, cursor + offset, length);
    if (c < 0 || !is_js_space((uint32_t)c)) break;
    cursor += direction;
  }
  while (cursor + direction >= 0 && cursor + direction <= length) {
    int64_t c = field_char(field, cursor + offset, length);
    if (c < 0 || is_js_space((uint32_t)c)) break;
    cursor += direction;
  }
  return cursor;
}

static bool navigation(qa_console_field *field, int64_t code, bool control) {
  int64_t count = field_count(field);
  switch (code) {
    case KEY_BACKSPACE:
      if (field->cursor > 0) remove_range(field, --field->cursor, 1);
      break;
    case KEY_DELETE:
      remove_range(field, field->cursor, 1);
      break;
    case KEY_LEFT:
      field->cursor = control ? word_position(field, -1)
                               : (field->cursor - 1 > 0 ? field->cursor - 1 : 0);
      break;
    case KEY_RIGHT: {
      int64_t next = field->cursor + 1;
      field->cursor = control ? word_position(field, 1) : (next < count ? next : count);
      break;
    }
    case KEY_HOME: field->cursor = 0; break;
    case KEY_END: field->cursor = count; break;
    case KEY_INSERT: field->overstrike = !field->overstrike; break;
    default: return false;
  }
  keep_visible(field);
  return true;
}

bool qa_console_field_key(qa_console_field *field, int64_t code, bool control, bool shift,
                          qa_console_clipboard_fn clipboard, void *clipboard_ctx) {
  if (code != KEY_SHIFT && code != KEY_CONTROL) completion_clear(field);
  if ((code == 118 && control) ||
      ((code == KEY_INSERT || code == KEY_KEYPAD_INSERT) && shift)) {
    const char *text = clipboard(clipboard_ctx);
    if (text != NULL) {
      size_t length = strcspn(text, "\r\n");
      char *first = dup_range(text, length);
      if (first != NULL) {
        qa_console_field_insert(field, first);
        free(first);
      }
    }
    return true;
  }
  if (control) {
    switch (code) {
      case 97: field->cursor = 0; break;
      case 101: field->cursor = field_count(field); break;
      case 99:
      case 117: qa_console_field_clear(field); break;
      case 107: remove_range(field, field->cursor, field_count(field)); break;
      case 119: {
        int64_t start = word_position(field, -1);
        remove_range(field, start, field->cursor - start);
        field->cursor = start;
        break;
      }
      default: return navigation(field, code, true);
    }
    keep_visible(field);
    return true;
  }
  return navigation(field, code, false);
}

/* ^([\\/]?)([^\s]*)(.*)$ over a byte range. */
static void parse_line(const char *text, size_t length, size_t *token_start,
                       size_t *token_end) {
  size_t start = length > 0 && (text[0] == '/' || text[0] == '\\') ? 1 : 0;
  size_t end = start;
  while (end < length) {
    size_t step = 0;
    if (is_js_space(cp_decode(text, length, end, &step))) break;
    end += step;
  }
  *token_start = start;
  *token_end = end;
}

typedef struct match_entry {
  char *folded;      /* owned */
  const char *name;  /* borrowed */
} match_entry;

static int match_compare(const void *left, const void *right) {
  return strcmp(((const match_entry *)left)->folded, ((const match_entry *)right)->folded);
}

void qa_console_field_complete(qa_console_field *field, const char *const *names,
                               size_t name_count, bool reverse) {
  bool fresh = !field->has_completion || field->completion_cursor != field->cursor ||
               strlen(field->completion_text) != field->length ||
               (field->length > 0 &&
                memcmp(field->completion_text, field->characters, field->length) != 0);
  if (fresh) {
    completion_clear(field);
    size_t token_start = 0, token_end = 0;
    parse_line(field->characters == NULL ? "" : field->characters, field->length,
               &token_start, &token_end);
    /* tokenStart/tokenEnd and the cursor count code points. */
    const char *chars = field->characters == NULL ? "" : field->characters;
    int64_t start_cp = (int64_t)token_start;
    int64_t end_cp = start_cp + (int64_t)cp_count(chars + token_start, token_end - token_start);
    if (field->cursor < start_cp || field->cursor > end_cp) return;
    size_t prefix_length = cp_offset(chars + token_start, token_end - token_start,
                                     field->cursor - start_cp);
    match_entry *entries = NULL;
    size_t count = 0, capacity = 0;
    for (size_t i = 0; i < name_count; i++) {
      const char *name = names[i];
      if (!folded_starts_with(name, field->characters + token_start, prefix_length)) continue;
      char *folded = dup_text(name);
      if (folded == NULL) continue; /* OOM only */
      for (char *p = folded; *p != '\0'; p++) *p = fold_byte(*p);
      bool duplicate = false;
      for (size_t j = 0; j < count; j++)
        if (strcmp(entries[j].folded, folded) == 0) duplicate = true;
      if (duplicate) {
        free(folded);
        continue;
      }
      if (count == capacity) {
        size_t next = capacity == 0 ? 8 : capacity * 2;
        match_entry *grown = (match_entry *)realloc(entries, next * sizeof *grown);
        if (grown == NULL) {
          free(folded);
          continue; /* OOM only */
        }
        entries = grown;
        capacity = next;
      }
      entries[count].folded = folded;
      entries[count].name = name;
      count++;
    }
    if (count == 0) {
      free(entries);
      return;
    }
    qa_js_sort(entries, count, sizeof *entries, match_compare);
    field->completion_text = qa_console_field_text(field);
    field->completion_tail =
        dup_range(field->characters + token_end, field->length - token_end);
    field->completion_matches = (char **)calloc(count, sizeof(char *));
    field->completion_selected = dup_text("");
    if (field->completion_text == NULL || field->completion_tail == NULL ||
        field->completion_matches == NULL || field->completion_selected == NULL) {
      for (size_t i = 0; i < count; i++) free(entries[i].folded);
      free(entries);
      completion_clear(field);
      return; /* OOM only */
    }
    for (size_t i = 0; i < count; i++) {
      field->completion_matches[i] = dup_text(entries[i].name);
      free(entries[i].folded);
    }
    free(entries);
    field->completion_match_count = count;
    field->completion_cursor = field->cursor;
    field->completion_index = reverse ? 0 : -1;
    field->has_completion = true;
  }
  int64_t total = (int64_t)field->completion_match_count;
  int64_t index = (field->completion_index + (reverse ? -1 : 1) + total) % total;
  const char *selected = field->completion_matches[index];
  size_t text_length = 1 + strlen(selected) + strlen(field->completion_tail);
  int64_t text_points = 1 + (int64_t)cp_count(selected, strlen(selected)) +
                        (int64_t)cp_count(field->completion_tail, strlen(field->completion_tail));
  if (text_points > field->maximum_length) {
    if (fresh) completion_clear(field); /* TS keeps the fresh cycle unassigned. */
    return;
  }
  char *text = (char *)malloc(text_length + 1);
  char *selected_copy = dup_text(selected);
  if (text == NULL || selected_copy == NULL) {
    free(text);
    free(selected_copy);
    return; /* OOM only */
  }
  sprintf(text, "/%s%s", selected, field->completion_tail);
  /* setText clears the cycle in TS; steal it across the call. */
  char *tail = field->completion_tail;
  char **matches = field->completion_matches;
  size_t match_count = field->completion_match_count;
  field->completion_tail = NULL;
  field->completion_matches = NULL;
  field->completion_match_count = 0;
  qa_console_field_set_text(field, text);
  field->cursor = 1 + (int64_t)cp_count(selected_copy, strlen(selected_copy));
  keep_visible(field);
  field->completion_text = text;
  field->completion_tail = tail;
  field->completion_matches = matches;
  field->completion_match_count = match_count;
  field->completion_cursor = field->cursor;
  field->completion_index = index;
  free(field->completion_selected);
  field->completion_selected = selected_copy;
  field->has_completion = true;
}

bool qa_complete_command(const char *text, const char *const *names, size_t name_count,
                         qa_completion_result *out) {
  memset(out, 0, sizeof *out);
  size_t length = strlen(text);
  size_t token_start = 0, token_end = 0;
  parse_line(text, length, &token_start, &token_end);
  size_t prefix_length = token_end - token_start;
  if (prefix_length == 0) {
    out->text = dup_text(text);
    return out->text != NULL;
  }
  const char **matches = NULL;
  size_t count = 0, capacity = 0;
  for (size_t i = 0; i < name_count; i++) {
    const char *name = names[i];
    if (!folded_starts_with(name, text + token_start, prefix_length)) continue;
    bool duplicate = false;
    for (size_t j = 0; j < count; j++)
      if (strcmp(matches[j], name) == 0) duplicate = true;
    if (duplicate) continue;
    if (count == capacity) {
      size_t next = capacity == 0 ? 8 : capacity * 2;
      const char **grown = (const char **)realloc(matches, next * sizeof *grown);
      if (grown == NULL) {
        free(matches);
        return false;
      }
      matches = grown;
      capacity = next;
    }
    matches[count++] = name;
  }
  out->matches = matches;
  out->match_count = count;
  if (count == 0) {
    out->text = dup_text(text);
    if (out->text == NULL) {
      qa_completion_result_destroy(out);
      return false;
    }
    return true;
  }
  const char *first = matches[0];
  size_t common = strlen(first);
  for (size_t i = 0; i < count; i++) {
    size_t at = 0;
    while (at < common && fold_byte(first[at]) == fold_byte(matches[i][at])) at++;
    common = at;
  }
  /* TS compares whole characters: never end inside a UTF-8 sequence. */
  while (common > 0 && common < strlen(first) && ((unsigned char)first[common] & 0xc0) == 0x80) common--;
  size_t tail_length = length - token_end;
  const char *suffix = tail_length > 0 ? text + token_end : (count == 1 ? " " : "");
  size_t suffix_length = tail_length > 0 ? tail_length : strlen(suffix);
  out->text = (char *)malloc(token_start + common + suffix_length + 1);
  if (out->text == NULL) {
    qa_completion_result_destroy(out);
    return false;
  }
  memcpy(out->text, text, token_start);
  memcpy(out->text + token_start, first, common);
  memcpy(out->text + token_start + common, suffix, suffix_length);
  out->text[token_start + common + suffix_length] = '\0';
  return true;
}

void qa_completion_result_destroy(qa_completion_result *result) {
  free(result->text);
  free(result->matches);
  memset(result, 0, sizeof *result);
}

static void history_free_value(const void *value, size_t index, void *ctx) {
  (void)index;
  (void)ctx;
  free(*(char *const *)value);
}

static void history_push(qa_console_history *history, const char *line) {
  char *copy = dup_text(line);
  if (copy == NULL) return; /* OOM only */
  const void *oldest = NULL;
  if (history->entries.count == history->entries.capacity)
    oldest = qa_bounded_queue_peek_oldest(&history->entries);
  char *evicted = oldest == NULL ? NULL : *(char *const *)oldest;
  qa_bounded_queue_push(&history->entries, &copy);
  free(evicted);
}

bool qa_console_history_init(qa_console_history *history, int64_t capacity) {
  memset(history, 0, sizeof *history);
  history->capacity = capacity;
  history->draft = dup_text("");
  if (history->draft == NULL) return false;
  if (!qa_bounded_queue_init(&history->entries,
                              capacity > 0 ? (size_t)capacity : 0, sizeof(char *))) {
    free(history->draft);
    history->draft = NULL;
    return false;
  }
  return true;
}

void qa_console_history_destroy(qa_console_history *history) {
  qa_bounded_queue_for_each(&history->entries, history_free_value, NULL);
  qa_bounded_queue_destroy(&history->entries);
  free(history->draft);
  memset(history, 0, sizeof *history);
}

size_t qa_console_history_line_count(const qa_console_history *history) {
  return history->entries.count;
}

const char **qa_console_history_lines(const qa_console_history *history, size_t *out_count) {
  size_t count = 0;
  char **values = (char **)qa_bounded_queue_to_array(&history->entries, &count);
  if (out_count != NULL) *out_count = count;
  return (const char **)values;
}

void qa_console_history_replace(qa_console_history *history, const char *const *lines,
                                size_t count) {
  qa_bounded_queue_for_each(&history->entries, history_free_value, NULL);
  qa_bounded_queue_clear(&history->entries);
  for (size_t i = 0; i < count; i++)
    if (lines[i][0] != '\0') history_push(history, lines[i]);
  history->position = (int64_t)history->entries.count;
}

void qa_console_history_add(qa_console_history *history, const char *line) {
  if (line[0] == '\0') return;
  history_push(history, line);
  history->position = (int64_t)history->entries.count;
  free(history->draft);
  history->draft = dup_text("");
}

const char *qa_console_history_previous(qa_console_history *history, const char *current) {
  if (history->position == (int64_t)history->entries.count) {
    char *draft = dup_text(current);
    if (draft != NULL) {
      free(history->draft);
      history->draft = draft;
    }
  }
  history->position = history->position - 1 > 0 ? history->position - 1 : 0;
  const void *value = qa_bounded_queue_at(&history->entries, (size_t)history->position);
  return value == NULL ? current : *(char *const *)value;
}

const char *qa_console_history_next(qa_console_history *history) {
  int64_t count = (int64_t)history->entries.count;
  history->position = history->position + 1 < count ? history->position + 1 : count;
  const void *value = qa_bounded_queue_at(&history->entries, (size_t)history->position);
  return value == NULL ? history->draft : *(char *const *)value;
}
