/*
 * Port of src/console/source-field.ts (EditField): Quake III
 * cl_keys.c-style 256-byte source edit field. Key codes are the
 * numeric KeyCode values of src/input/key-codes.ts (that module is
 * not yet ported; the values used here are named in source_field.c).
 * RangeErrors abort via qa_range_error with the TS messages.
 * Ownership: the field owns only its fixed buffer; init/destroy do
 * not allocate. qa_edit_field_text returns a malloc'd copy.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_SOURCE_FIELD_H
#define QA_CONSOLE_SOURCE_FIELD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** TS FieldClipboard. read returns false for the TS null; the byte
 * range it reports is borrowed for the duration of the paste. */
typedef enum qa_field_clipboard_kind {
  QA_FIELD_CLIPBOARD_NATIVE_UNIX_UNAVAILABLE, /* "native-unix-unavailable" */
  QA_FIELD_CLIPBOARD_AVAILABLE                /* "available" */
} qa_field_clipboard_kind;
typedef struct qa_field_clipboard {
  qa_field_clipboard_kind kind;
  void *ctx;
  bool (*read)(void *ctx, const uint8_t **out_bytes, size_t *out_length);
} qa_field_clipboard;

/** TS FieldControls. All callbacks share ctx. */
typedef struct qa_field_controls {
  void *ctx;
  bool (*is_down)(void *ctx, int64_t key);
  bool (*get_overstrike)(void *ctx);
  void (*set_overstrike)(void *ctx, bool value);
  qa_field_clipboard clipboard;
} qa_field_controls;

typedef struct qa_edit_field {
  int64_t cursor;
  int64_t scroll;
  int64_t width_in_chars;
  uint8_t buffer[256];
  int64_t paste_depth;
  int64_t paste_work;
} qa_edit_field;

void qa_edit_field_init(qa_edit_field *field);
void qa_edit_field_destroy(qa_edit_field *field);

/* Malloc'd text up to the first NUL byte; caller frees. Aborts when
 * the buffer is unterminated, as the TS getter throws. NULL on OOM. */
char *qa_edit_field_text(const qa_edit_field *field);
int qa_edit_field_read_byte(const qa_edit_field *field, int64_t offset);
void qa_edit_field_write_byte(qa_edit_field *field, int64_t offset, int64_t byte);
void qa_edit_field_clear(qa_edit_field *field);
/* Copies sourceCommandText(value) in; aborts at 256+ bytes. */
void qa_edit_field_set_text(qa_edit_field *field, const char *value);
void qa_edit_field_copy_from(qa_edit_field *field, const qa_edit_field *source);
void qa_edit_field_key_down(qa_edit_field *field, int64_t key,
                            const qa_field_controls *controls);
void qa_edit_field_char_event(qa_edit_field *field, int64_t character,
                              const qa_field_controls *controls);

#endif
