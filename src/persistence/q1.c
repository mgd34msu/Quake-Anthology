/* Port of src/persistence/q1.ts. GPL-2.0-or-later. */
#include "qa/persistence/q1.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/source_bytes.h"
#include "qa/persistence/value.h"
#include "qa/foundation/js.h"

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  if (length > 0) memcpy(out, text, length);
  out[length] = '\0';
  return out;
}

/* --- QuakeSaveScanner (byte text: one char per byte, so TS string
 * offsets equal byte offsets) --- */
typedef struct scanner {
  const char *text;
  size_t length;
  size_t offset;
} scanner;

static void scan_fail(const scanner *scan, const char *format, const char *arg) {
  char path[64];
  (void)snprintf(path, sizeof path, "q1:%zu", scan->offset);
  if (arg != NULL) qa_save_format_error(path, format, arg);
  qa_save_format_error(path, "%s", format);
}

static void scan_skip(scanner *scan) {
  for (;;) {
    while (scan->offset < scan->length && (unsigned char)scan->text[scan->offset] <= 32)
      scan->offset++;
    if (!(scan->offset + 1 < scan->length && scan->text[scan->offset] == '/' &&
          scan->text[scan->offset + 1] == '/'))
      return;
    size_t end = scan->offset;
    while (end < scan->length && scan->text[end] != '\n') end++;
    scan->offset = end >= scan->length ? scan->length : end + 1;
  }
}

/* Malloc'd token (NUL-terminated); NULL only on OOM. Aborts on the
 * TS truncated / unterminated errors. */
static char *scan_token(scanner *scan, size_t *out_length) {
  scan_skip(scan);
  if (scan->offset >= scan->length) scan_fail(scan, "truncated save", NULL);
  char first = scan->text[scan->offset++];
  if (first == '{' || first == '}') {
    *out_length = 1;
    return dup_range(&first, 1);
  }
  if (first == '"') {
    size_t end = scan->offset;
    while (end < scan->length && scan->text[end] != '"') end++;
    if (end >= scan->length) scan_fail(scan, "unterminated quoted field", NULL);
    char *token = dup_range(scan->text + scan->offset, end - scan->offset);
    *out_length = end - scan->offset;
    scan->offset = end + 1;
    return token;
  }
  size_t start = scan->offset - 1;
  while (scan->offset < scan->length && (unsigned char)scan->text[scan->offset] > 32 &&
         scan->text[scan->offset] != '{' && scan->text[scan->offset] != '}')
    scan->offset++;
  *out_length = scan->offset - start;
  return dup_range(scan->text + start, scan->offset - start);
}

/* JS Number(text) over a byte string. Returns false for NaN results. */
static bool js_space(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\v' || byte == '\f' ||
         byte == '\r' || byte == 0xa0;
}
static bool js_number(const char *text, size_t length, double *out) {
  char *copy = dup_range(text, length);
  if (copy == NULL) return false;
  *out = qa_js_string_to_number(copy);
  free(copy);
  return true;
}

/* False only on OOM (token allocation). */
static bool scan_number(scanner *scan, double *out) {
  size_t length = 0;
  char *text = scan_token(scan, &length);
  if (text == NULL) return false;
  double value = 0;
  bool parsed = js_number(text, length, &value);
  if (!parsed || !isfinite(value)) scan_fail(scan, "invalid numeric field %s", text);
  free(text);
  *out = value;
  return true;
}

static bool record_push(qa_q1_text_record *record, size_t *capacity, char *key, char *value) {
  if (record->pair_count == *capacity) {
    size_t grown = *capacity == 0 ? 8 : *capacity * 2;
    qa_q1_text_pair *pairs =
        (qa_q1_text_pair *)realloc(record->pairs, grown * sizeof *pairs);
    if (pairs == NULL) return false;
    record->pairs = pairs;
    *capacity = grown;
  }
  record->pairs[record->pair_count].key = key;
  record->pairs[record->pair_count].value = value;
  record->pair_count++;
  return true;
}

static void record_clear(qa_q1_text_record *record) {
  for (size_t i = 0; i < record->pair_count; i++) {
    free(record->pairs[i].key);
    free(record->pairs[i].value);
  }
  free(record->pairs);
  record->pairs = NULL;
  record->pair_count = 0;
}

/* False only on OOM; the partial record is cleared on failure. */
static bool scan_record(scanner *scan, qa_q1_text_record *out) {
  size_t length = 0;
  char *open = scan_token(scan, &length);
  if (open == NULL) return false;
  bool is_open = strcmp(open, "{") == 0;
  free(open);
  if (!is_open) scan_fail(scan, "expected opening brace", NULL);
  size_t capacity = 0;
  for (;;) {
    char *key = scan_token(scan, &length);
    if (key == NULL) { record_clear(out); return false; }
    if (strcmp(key, "}") == 0) { free(key); return true; }
    char *value = scan_token(scan, &length);
    if (value == NULL) { free(key); record_clear(out); return false; }
    if (strcmp(value, "}") == 0) {
      free(key);
      free(value);
      scan_fail(scan, "field has no value", NULL);
    }
    if (!record_push(out, &capacity, key, value)) {
      free(key);
      free(value);
      record_clear(out);
      return false;
    }
  }
}

void qa_q1_save_data_free(qa_q1_save_data *save) {
  if (save == NULL) return;
  free(save->game_directories);
  free(save->comment);
  free(save->spawn_parameters);
  free(save->map);
  for (size_t i = 0; i < save->light_style_count; i++) free(save->light_styles[i]);
  free(save->light_styles);
  for (size_t i = 0; i < save->global_count; i++) {
    free(save->globals[i].key);
    free(save->globals[i].value);
  }
  free(save->globals);
  for (size_t i = 0; i < save->entity_count; i++) record_clear(&save->entities[i]);
  free(save->entities);
  free(save->extension_text);
  memset(save, 0, sizeof *save);
}

bool qa_decode_q1_save(const uint8_t *bytes, size_t length, qa_q1_save_data *out) {
  memset(out, 0, sizeof *out);
  char *text = qa_byte_text(bytes, length);
  if (text == NULL) return false;
  scanner scan = {text, length, 0};
  double version = 0;
  size_t token_length = 0;
  if (!scan_number(&scan, &version)) goto oom;
  if (version == 5) {
    out->format_version = 5;
  } else if (version == 6) {
    out->format_version = 6;
    out->game_directories = scan_token(&scan, &token_length);
    if (out->game_directories == NULL) goto oom;
  } else {
    char shown[64];
    (void)snprintf(shown, sizeof shown, "%s", QA_JS_NUMBER(version));
    qa_save_format_error("q1", "unsupported save version %s", shown);
  }
  out->comment = scan_token(&scan, &token_length);
  if (out->comment == NULL) goto oom;
  out->spawn_parameters = (double *)calloc(16, sizeof *out->spawn_parameters);
  if (out->spawn_parameters == NULL) goto oom;
  out->spawn_parameter_count = 16;
  for (size_t i = 0; i < 16; i++)
    if (!scan_number(&scan, &out->spawn_parameters[i])) goto oom;
  double skill = 0;
  if (!scan_number(&scan, &skill)) goto oom;
  out->skill = trunc(skill + 0.1);
  out->map = scan_token(&scan, &token_length);
  if (out->map == NULL) goto oom;
  if (!scan_number(&scan, &out->time)) goto oom;
  out->light_styles = (char **)calloc(64, sizeof *out->light_styles);
  if (out->light_styles == NULL) goto oom;
  out->light_style_count = 64;
  for (size_t i = 0; i < 64; i++) {
    out->light_styles[i] = scan_token(&scan, &token_length);
    if (out->light_styles[i] == NULL) goto oom;
  }
  {
    qa_q1_text_record globals = {NULL, 0};
    if (!scan_record(&scan, &globals)) goto oom;
    out->globals = globals.pairs;
    out->global_count = globals.pair_count;
  }
  {
    size_t capacity = 0;
    for (;;) {
      while (scan.offset < scan.length && (unsigned char)text[scan.offset] <= 32)
        scan.offset++;
      size_t extension_start = scan.offset;
      scan_skip(&scan);
      if (!(scan.offset < scan.length && text[scan.offset] == '{')) {
        scan.offset = extension_start;
        break;
      }
      if (out->entity_count == capacity) {
        size_t grown = capacity == 0 ? 8 : capacity * 2;
        qa_q1_text_record *entities =
            (qa_q1_text_record *)realloc(out->entities, grown * sizeof *entities);
        if (entities == NULL) goto oom;
        out->entities = entities;
        capacity = grown;
      }
      qa_q1_text_record record = {NULL, 0};
      if (!scan_record(&scan, &record)) goto oom;
      out->entities[out->entity_count++] = record;
    }
  }
  out->extension_length = scan.length - scan.offset;
  out->extension_text = dup_range(text + scan.offset, out->extension_length);
  if (out->extension_text == NULL) goto oom;
  free(text);
  return true;
oom:
  free(text);
  qa_q1_save_data_free(out);
  return false;
}

/* --- encode --- */
typedef struct text_buffer {
  char *data;
  size_t length;
  size_t capacity;
  bool first_line;
} text_buffer;

static bool buffer_add(text_buffer *buffer, const char *text, size_t length) {
  if (buffer->length + length + 1 > buffer->capacity) {
    size_t cap = buffer->capacity == 0 ? 512 : buffer->capacity;
    while (cap < buffer->length + length + 1) cap *= 2;
    char *data = (char *)realloc(buffer->data, cap);
    if (data == NULL) return false;
    buffer->data = data;
    buffer->capacity = cap;
  }
  if (length > 0) memcpy(buffer->data + buffer->length, text, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return true;
}

static bool buffer_line(text_buffer *buffer, const char *text) {
  if (!buffer->first_line && !buffer_add(buffer, "\n", 1)) return false;
  buffer->first_line = false;
  return buffer_add(buffer, text, strlen(text));
}

/* TS token(): rejects empty and /[\s\0"]/ (interior NUL is not
 * observable in a C string). NULL is treated as the empty string. */
static const char *encode_token(const char *value) {
  const char *text = value == NULL ? "" : value;
  if (text[0] == '\0') qa_save_format_error("q1", "invalid save header token");
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++)
    if (js_space(*p) || *p == '"')
      qa_save_format_error("q1", "invalid save header token");
  return text;
}

static bool buffer_decimal(text_buffer *buffer, double value) {
  if (!isfinite(value)) qa_save_format_error("q1", "non-finite header number");
  char shown[QA_JS_TO_FIXED_CAPACITY];
  qa_js_to_fixed(value, 6, shown, sizeof shown); /* value.toFixed(6): exact ties round up */
  return buffer_line(buffer, shown);
}

static bool buffer_record(text_buffer *buffer, const qa_q1_text_pair *pairs, size_t count) {
  if (!buffer_line(buffer, "{")) return false;
  for (size_t i = 0; i < count; i++) {
    const char *key = pairs[i].key == NULL ? "" : pairs[i].key;
    const char *value = pairs[i].value == NULL ? "" : pairs[i].value;
    if (strchr(key, '"') != NULL || strchr(value, '"') != NULL)
      qa_save_format_error("q1", "source QuakeC save strings cannot contain quotes or NUL");
    if (!buffer->first_line && !buffer_add(buffer, "\n", 1)) return false;
    buffer->first_line = false;
    if (!buffer_add(buffer, "\"", 1) || !buffer_add(buffer, key, strlen(key)) ||
        !buffer_add(buffer, "\" \"", 3) || !buffer_add(buffer, value, strlen(value)) ||
        !buffer_add(buffer, "\"", 1))
      return false;
  }
  return buffer_line(buffer, "}");
}

uint8_t *qa_encode_q1_save(const qa_q1_save_data *save, size_t *out_length) {
  *out_length = 0;
  if (save->spawn_parameter_count != 16 || save->light_style_count != 64)
    qa_save_format_error("q1", "save requires 16 spawn parameters and 64 light styles");
  text_buffer buffer = {NULL, 0, 0, true};
  char shown[512];
  (void)snprintf(shown, sizeof shown, "%d", save->format_version);
  if (!buffer_line(&buffer, shown)) goto oom;
  if (save->format_version == 6 && !buffer_line(&buffer, encode_token(save->game_directories)))
    goto oom;
  if (!buffer_line(&buffer, encode_token(save->comment))) goto oom;
  for (size_t i = 0; i < save->spawn_parameter_count; i++)
    if (!buffer_decimal(&buffer, save->spawn_parameters[i])) goto oom;
  /* String(Math.trunc(skill)); no finite check in TS. */
  if (isnan(save->skill)) (void)snprintf(shown, sizeof shown, "NaN");
  else if (isinf(save->skill))
    (void)snprintf(shown, sizeof shown, "%s", save->skill < 0 ? "-Infinity" : "Infinity");
  else (void)snprintf(shown, sizeof shown, "%s", QA_JS_NUMBER(trunc(save->skill))); /* String(): -0 is "0" */
  if (!buffer_line(&buffer, shown)) goto oom;
  if (!buffer_line(&buffer, encode_token(save->map))) goto oom;
  if (!buffer_decimal(&buffer, save->time)) goto oom;
  for (size_t i = 0; i < save->light_style_count; i++) {
    const char *style = save->light_styles[i];
    if (!buffer_line(&buffer, encode_token(style == NULL || style[0] == '\0' ? "m" : style)))
      goto oom;
  }
  if (!buffer_record(&buffer, save->globals, save->global_count)) goto oom;
  for (size_t i = 0; i < save->entity_count; i++)
    if (!buffer_record(&buffer, save->entities[i].pairs, save->entities[i].pair_count))
      goto oom;
  if (!buffer_add(&buffer, "\n", 1)) goto oom;
  if (save->extension_text != NULL &&
      !buffer_add(&buffer, save->extension_text, save->extension_length))
    goto oom;
  {
    uint8_t *bytes = qa_text_bytes(buffer.data == NULL ? "" : buffer.data, buffer.length);
    if (bytes == NULL && buffer.length > 0) goto oom;
    *out_length = buffer.length;
    free(buffer.data);
    return bytes;
  }
oom:
  free(buffer.data);
  return NULL;
}

