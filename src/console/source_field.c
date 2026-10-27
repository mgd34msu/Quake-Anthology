/* Port of src/console/source-field.ts. GPL-2.0-or-later. */
#include "qa/console/source_field.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"

/* KeyCode values from src/input/key-codes.ts (input module pending). */
enum {
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

static void check_index(int64_t value) {
  if (value < 0 || value >= 256) qa_range_error("Undefined native field buffer index");
}
static void check_integer(int64_t value) {
  if (value < -2147483648LL || value > 2147483647LL)
    qa_range_error("Field input requires int32");
}

void qa_edit_field_init(qa_edit_field *field) { memset(field, 0, sizeof *field); }
void qa_edit_field_destroy(qa_edit_field *field) { memset(field, 0, sizeof *field); }

static size_t field_length(const qa_edit_field *field) {
  for (size_t i = 0; i < sizeof field->buffer; i++)
    if (field->buffer[i] == 0) return i;
  qa_range_error("Undefined native unterminated field buffer");
}

char *qa_edit_field_text(const qa_edit_field *field) {
  size_t length = field_length(field);
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  memcpy(out, field->buffer, length);
  out[length] = '\0';
  return out;
}

int qa_edit_field_read_byte(const qa_edit_field *field, int64_t offset) {
  check_index(offset);
  return field->buffer[offset];
}

void qa_edit_field_write_byte(qa_edit_field *field, int64_t offset, int64_t byte) {
  check_index(offset);
  if (byte < 0 || byte > 255) qa_range_error("Field characters require source bytes");
  field->buffer[offset] = (uint8_t)byte;
}

void qa_edit_field_clear(qa_edit_field *field) {
  memset(field->buffer, 0, sizeof field->buffer);
  field->cursor = 0;
  field->scroll = 0;
}

void qa_edit_field_set_text(qa_edit_field *field, const char *value) {
  char *text = qa_source_command_text(value);
  if (text == NULL) return; /* OOM only; TS has no counterpart. */
  size_t length = strlen(text);
  if (length >= sizeof field->buffer) {
    free(text);
    qa_range_error("Field text exceeds 255 bytes");
  }
  memcpy(field->buffer, text, length);
  field->buffer[length] = 0;
  free(text);
}

void qa_edit_field_copy_from(qa_edit_field *field, const qa_edit_field *source) {
  memcpy(field->buffer, source->buffer, sizeof field->buffer);
  field->cursor = source->cursor;
  field->scroll = source->scroll;
  field->width_in_chars = source->width_in_chars;
}

static void field_move(qa_edit_field *field, int64_t destination, int64_t start, int64_t end) {
  check_index(destination);
  check_index(start);
  if (end < start || end > 256 || destination + end - start > 256)
    qa_range_error("Undefined native field memmove range");
  memmove(field->buffer + destination, field->buffer + start, (size_t)(end - start));
}

static void field_paste(qa_edit_field *field, const qa_field_controls *controls) {
  if (controls->clipboard.kind == QA_FIELD_CLIPBOARD_NATIVE_UNIX_UNAVAILABLE) return;
  if (field->paste_depth >= 32)
    qa_range_error("Recursive field paste exceeded 32 clipboard reads");
  const uint8_t *bytes = NULL;
  size_t length = 0;
  if (!controls->clipboard.read(controls->clipboard.ctx, &bytes, &length)) return;
  if (field->paste_depth == 0) field->paste_work = 0;
  field->paste_depth++;
  for (size_t i = 0; i < length; i++) {
    uint8_t byte = bytes[i];
    if (byte == 0) break;
    if (++field->paste_work > 65536) {
      field->paste_depth--;
      qa_range_error("Field paste exceeded 65536 byte operations");
    }
    qa_edit_field_char_event(field, byte < 128 ? byte : (int)byte - 256, controls);
  }
  field->paste_depth--;
}

void qa_edit_field_key_down(qa_edit_field *field, int64_t key,
                            const qa_field_controls *controls) {
  check_integer(key);
  if ((key == KEY_INSERT || key == KEY_KEYPAD_INSERT) &&
      controls->is_down(controls->ctx, KEY_SHIFT)) {
    field_paste(field, controls);
    return;
  }
  int64_t length = (int64_t)field_length(field);
  if (key == KEY_DELETE) {
    if (field->cursor < length) field_move(field, field->cursor, field->cursor + 1, length + 1);
    return;
  }
  if (key == KEY_RIGHT) {
    if (field->cursor < length) field->cursor++;
    if (field->cursor >= field->scroll + field->width_in_chars && field->cursor <= length)
      field->scroll++;
    return;
  }
  if (key == KEY_LEFT) {
    if (field->cursor > 0) field->cursor--;
    if (field->cursor < field->scroll) field->scroll--;
    return;
  }
  if (key == KEY_HOME || ((key == 65 || key == 97) && controls->is_down(controls->ctx, KEY_CONTROL))) {
    field->cursor = 0;
    return;
  }
  if (key == KEY_END || ((key == 69 || key == 101) && controls->is_down(controls->ctx, KEY_CONTROL))) {
    field->cursor = length;
    return;
  }
  if (key == KEY_INSERT)
    controls->set_overstrike(controls->ctx, !controls->get_overstrike(controls->ctx));
}

void qa_edit_field_char_event(qa_edit_field *field, int64_t character,
                              const qa_field_controls *controls) {
  check_integer(character);
  if (character == 22) {
    field_paste(field, controls);
    return;
  }
  if (character == 3) {
    qa_edit_field_clear(field);
    return;
  }
  int64_t length = (int64_t)field_length(field);
  if (character == 8) {
    if (field->cursor > 0) {
      field_move(field, field->cursor - 1, field->cursor, length + 1);
      field->cursor--;
      if (field->cursor < field->scroll) field->scroll--;
    }
    return;
  }
  if (character == 1) {
    field->cursor = 0;
    field->scroll = 0;
    return;
  }
  if (character == 5) {
    field->cursor = length;
    field->scroll = field->cursor - field->width_in_chars;
    return;
  }
  if (character < 32) return;
  if (character > 255) qa_range_error("Field characters require source bytes");
  if (controls->get_overstrike(controls->ctx)) {
    if (field->cursor == 255) return;
    check_index(field->cursor);
    field->buffer[field->cursor] = (uint8_t)character;
    field->cursor++;
  } else {
    if (length == 255) return;
    field_move(field, field->cursor + 1, field->cursor, length + 1);
    field->buffer[field->cursor] = (uint8_t)character;
    field->cursor++;
  }
  if (field->cursor >= field->width_in_chars) field->scroll++;
  if (field->cursor == length + 1) {
    check_index(field->cursor);
    field->buffer[field->cursor] = 0;
  }
}
