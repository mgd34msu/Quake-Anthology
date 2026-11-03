/* Port of src/core/binary/index.ts. GPL-2.0-or-later. */
#include "qa/core/binary/index.h"

#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/binary.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

noreturn void qa_binary_error(const char *source, int64_t offset,
                              const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("BinaryError", 0, offset, "%s:%lld: %s", source, (long long)offset, text);
}

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

int qa_binary_reader_init(qa_binary_reader *reader, uint8_t *data,
                          size_t length, const char *source) {
  reader->data = data;
  reader->length = length;
  reader->cursor = 0;
  reader->source = copy_text(source != NULL ? source : "<buffer>");
  return reader->source != NULL;
}

void qa_binary_reader_destroy(qa_binary_reader *reader) {
  free(reader->source);
  reader->source = NULL;
  reader->data = NULL;
  reader->length = 0;
  reader->cursor = 0;
}

size_t qa_binary_reader_offset(const qa_binary_reader *reader) {
  return reader->cursor;
}
size_t qa_binary_reader_length(const qa_binary_reader *reader) {
  return reader->length;
}
size_t qa_binary_reader_remaining(const qa_binary_reader *reader) {
  return reader->length - reader->cursor;
}

static void check_range(const qa_binary_reader *reader, int64_t offset,
                        int64_t length) {
  if (offset < 0 || length < 0 || offset > (int64_t)reader->length - length) {
    qa_binary_error(reader->source, offset,
                    "range of %lld bytes exceeds %zu-byte input",
                    (long long)length, reader->length);
  }
}

static bool safe_integer(double value) {
  return value == trunc(value) && fabs(value) <= 9007199254740991.0;
}

void qa_binary_reader_check_number(const qa_binary_reader *reader, double offset, double length) {
  if (!safe_integer(offset) || !safe_integer(length) || offset < 0 || length < 0 ||
      offset > (double)reader->length - length) {
    qa_binary_error(reader->source, safe_integer(offset) ? (int64_t)offset : 0,
                    "range of %s bytes exceeds %zu-byte input", QA_JS_NUMBER(length), reader->length);
  }
}

static size_t take_bytes(qa_binary_reader *reader, int64_t length) {
  size_t offset = reader->cursor;
  check_range(reader, (int64_t)offset, length);
  reader->cursor += (size_t)length;
  return offset;
}

void qa_binary_reader_seek(qa_binary_reader *reader, int64_t offset) {
  check_range(reader, offset, 0);
  reader->cursor = (size_t)offset;
}

void qa_binary_reader_skip(qa_binary_reader *reader, int64_t length) {
  (void)take_bytes(reader, length);
}

uint8_t qa_binary_reader_u8(qa_binary_reader *reader) {
  return reader->data[take_bytes(reader, 1)];
}

int8_t qa_binary_reader_i8(qa_binary_reader *reader) {
  return (int8_t)reader->data[take_bytes(reader, 1)];
}

uint16_t qa_binary_reader_u16(qa_binary_reader *reader) {
  size_t o = take_bytes(reader, 2);
  return (uint16_t)(reader->data[o] | (uint16_t)reader->data[o + 1] << 8);
}

int16_t qa_binary_reader_i16(qa_binary_reader *reader) {
  return (int16_t)qa_binary_reader_u16(reader);
}

uint32_t qa_binary_reader_u32(qa_binary_reader *reader) {
  size_t o = take_bytes(reader, 4);
  return (uint32_t)reader->data[o] | (uint32_t)reader->data[o + 1] << 8 |
         (uint32_t)reader->data[o + 2] << 16 |
         (uint32_t)reader->data[o + 3] << 24;
}

int32_t qa_binary_reader_i32(qa_binary_reader *reader) {
  return (int32_t)qa_binary_reader_u32(reader);
}

float qa_binary_reader_f32(qa_binary_reader *reader) {
  size_t o = take_bytes(reader, 4);
  uint32_t bits = (uint32_t)reader->data[o] |
                  (uint32_t)reader->data[o + 1] << 8 |
                  (uint32_t)reader->data[o + 2] << 16 |
                  (uint32_t)reader->data[o + 3] << 24;
  return qa_float32_from_bits(bits);
}

double qa_binary_reader_f64(qa_binary_reader *reader) {
  size_t o = take_bytes(reader, 8);
  uint64_t bits = 0;
  for (int i = 0; i < 8; i++) bits |= (uint64_t)reader->data[o + i] << (8 * i);
  return qa_float64_from_bits(bits);
}

float qa_binary_reader_finite_f32(qa_binary_reader *reader) {
  size_t offset = reader->cursor;
  float value = qa_binary_reader_f32(reader);
  if (!isfinite((double)value)) {
    qa_binary_error(reader->source, (int64_t)offset, "non-finite float");
  }
  return value;
}

uint8_t *qa_binary_reader_bytes(qa_binary_reader *reader, int64_t length) {
  size_t offset = take_bytes(reader, length);
  if (length == 0) return NULL;
  uint8_t *copy = (uint8_t *)malloc((size_t)length);
  if (copy == NULL) return NULL;
  memcpy(copy, reader->data + offset, (size_t)length);
  return copy;
}

/* Strict UTF-8 validation, matching the fatal TextDecoder's rejects
 * (overlong forms, surrogate halves, and values past U+10FFFF). */
static int valid_utf8(const uint8_t *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    uint8_t first = bytes[i];
    if (first < 0x80) {
      i++;
      continue;
    }
    uint32_t code;
    size_t extra;
    if (first >= 0xc2 && first <= 0xdf) {
      code = first & 0x1fu;
      extra = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      code = first & 0x0fu;
      extra = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      code = first & 0x07u;
      extra = 3;
    } else {
      return 0;
    }
    if (i + extra >= length) return 0;
    for (size_t j = 1; j <= extra; j++) {
      uint8_t next = bytes[i + j];
      if ((next & 0xc0u) != 0x80u) return 0;
      code = (code << 6) | (next & 0x3fu);
    }
    uint32_t minimum = extra == 1 ? 0x80u : extra == 2 ? 0x800u : 0x10000u;
    if (code < minimum || code > 0x10ffffu ||
        (code >= 0xd800u && code <= 0xdfffu)) {
      return 0;
    }
    i += extra + 1;
  }
  return 1;
}

static noreturn void throw_type_error(void) {
  /* The fatal decoder's TypeError; message per the Bun oracle runtime. */
  qa_raise("TypeError", 0, 0, "Invalid byte sequence");
}

char *qa_binary_reader_fixed_string(qa_binary_reader *reader, int64_t length) {
  uint8_t *bytes = qa_binary_reader_bytes(reader, length);
  size_t count = length > 0 ? (size_t)length : 0;
  size_t end = count;
  for (size_t i = 0; i < count; i++) {
    if (bytes[i] == 0) {
      end = i;
      break;
    }
  }
  if (!valid_utf8(bytes, end)) {
    free(bytes);
    throw_type_error();
  }
  char *out = (char *)malloc(end + 1);
  if (out != NULL) {
    if (end > 0) memcpy(out, bytes, end);
    out[end] = '\0';
  }
  free(bytes);
  return out;
}

uint8_t *qa_binary_reader_data_view(qa_binary_reader *reader, int64_t offset,
                                    int64_t length) {
  check_range(reader, offset, length);
  return reader->data + offset;
}

char *qa_binary_reader_fixed_byte_string(qa_binary_reader *reader,
                                         int64_t length) {
  uint8_t *bytes = qa_binary_reader_bytes(reader, length);
  size_t count = length > 0 ? (size_t)length : 0;
  size_t end = count;
  for (size_t i = 0; i < count; i++) {
    if (bytes[i] == 0) {
      end = i;
      break;
    }
  }
  char *out = (char *)malloc(end + 1);
  if (out != NULL) {
    if (end > 0) memcpy(out, bytes, end);
    out[end] = '\0';
  }
  free(bytes);
  return out;
}

int qa_binary_reader_section(const qa_binary_reader *reader, int64_t offset,
                             int64_t length, qa_binary_reader *out) {
  check_range(reader, offset, length);
  size_t name_length = strlen(reader->source) + 32;
  char *name = (char *)malloc(name_length);
  if (name == NULL) return 0;
  snprintf(name, name_length, "%s@%lld", reader->source, (long long)offset);
  out->data = reader->data + offset;
  out->length = (size_t)length;
  out->cursor = 0;
  out->source = name;
  return 1;
}

/* Minimal JSON.stringify for the expectMagic message. */
static size_t json_length(const char *text) {
  size_t total = 2;
  for (const unsigned char *p = (const unsigned char *)text; *p != 0; p++) {
    if (*p == '"' || *p == '\\') total += 2;
    else if (*p == '\b' || *p == '\f' || *p == '\n' || *p == '\r' || *p == '\t')
      total += 2;
    else if (*p < 0x20) total += 6;
    else total += 1;
  }
  return total;
}

static char *json_stringify(const char *text) {
  char *out = (char *)malloc(json_length(text) + 1);
  if (out == NULL) return NULL;
  char *w = out;
  *w++ = '"';
  for (const unsigned char *p = (const unsigned char *)text; *p != 0; p++) {
    switch (*p) {
      case '"': *w++ = '\\'; *w++ = '"'; break;
      case '\\': *w++ = '\\'; *w++ = '\\'; break;
      case '\b': *w++ = '\\'; *w++ = 'b'; break;
      case '\f': *w++ = '\\'; *w++ = 'f'; break;
      case '\n': *w++ = '\\'; *w++ = 'n'; break;
      case '\r': *w++ = '\\'; *w++ = 'r'; break;
      case '\t': *w++ = '\\'; *w++ = 't'; break;
      default:
        if (*p < 0x20) w += sprintf(w, "\\u%04x", *p);
        else *w++ = (char)*p;
        break;
    }
  }
  *w++ = '"';
  *w = '\0';
  return out;
}

void qa_binary_reader_expect_magic(qa_binary_reader *reader,
                                   const char *magic) {
  size_t offset = reader->cursor;
  char *actual = qa_binary_reader_fixed_string(reader, (int64_t)strlen(magic));
  if (actual == NULL || strcmp(actual, magic) != 0) {
    char *want = json_stringify(magic);
    char *got = json_stringify(actual != NULL ? actual : "");
    const char *want_text = want != NULL ? want : "\"\"";
    const char *got_text = got != NULL ? got : "\"\"";
    char text[1536];
    snprintf(text, sizeof text, "%s:%zu: expected %s, got %s",
             reader->source, offset, want_text, got_text);
    free(want);
    free(got);
    free(actual);
    qa_raise("BinaryError", 0, (long long)offset, "%s", text);
  }
  free(actual);
}

int qa_binary_reader_records(const qa_binary_reader *reader, int64_t offset,
                             int64_t length, int64_t stride,
                             qa_binary_reader *out) {
  if (stride <= 0 || length % stride != 0) {
    qa_binary_error(reader->source, offset,
                    "length %lld is not a multiple of record size %lld",
                    (long long)length, (long long)stride);
  }
  return qa_binary_reader_section(reader, offset, length, out);
}

int qa_binary_writer_init(qa_binary_writer *writer, int64_t capacity) {
  if (capacity < 0 || capacity > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Invalid binary capacity");
  }
  writer->capacity = (size_t)capacity;
  writer->cursor = 0;
  writer->data = capacity == 0 ? NULL : (uint8_t *)calloc((size_t)capacity, 1);
  return capacity == 0 || writer->data != NULL;
}

void qa_binary_writer_destroy(qa_binary_writer *writer) {
  free(writer->data);
  writer->data = NULL;
  writer->capacity = 0;
  writer->cursor = 0;
}

size_t qa_binary_writer_offset(const qa_binary_writer *writer) {
  return writer->cursor;
}

static size_t writer_take(qa_binary_writer *writer, int64_t length) {
  if (length < 0 || (size_t)length > writer->capacity - writer->cursor) {
    qa_range_error("Binary output exceeds %zu-byte capacity",
                   writer->capacity);
  }
  size_t offset = writer->cursor;
  writer->cursor += (size_t)length;
  return offset;
}

static void check_integer(int64_t value, int64_t minimum, int64_t maximum) {
  if (value < minimum || value > maximum) {
    qa_range_error("Integer %lld outside %lld..%lld", (long long)value,
                   (long long)minimum, (long long)maximum);
  }
}

void qa_binary_writer_u8(qa_binary_writer *writer, int64_t value) {
  check_integer(value, 0, 255);
  writer->data[writer_take(writer, 1)] = (uint8_t)value;
}
void qa_binary_writer_i8(qa_binary_writer *writer, int64_t value) {
  check_integer(value, -128, 127);
  writer->data[writer_take(writer, 1)] = (uint8_t)(int8_t)value;
}
void qa_binary_writer_u16(qa_binary_writer *writer, int64_t value) {
  check_integer(value, 0, 65535);
  size_t o = writer_take(writer, 2);
  writer->data[o] = (uint8_t)value;
  writer->data[o + 1] = (uint8_t)((uint64_t)value >> 8);
}
void qa_binary_writer_i16(qa_binary_writer *writer, int64_t value) {
  check_integer(value, -32768, 32767);
  size_t o = writer_take(writer, 2);
  uint16_t bits = (uint16_t)(int16_t)value;
  writer->data[o] = (uint8_t)bits;
  writer->data[o + 1] = (uint8_t)(bits >> 8);
}
void qa_binary_writer_u32(qa_binary_writer *writer, int64_t value) {
  check_integer(value, 0, 0xffffffffLL);
  size_t o = writer_take(writer, 4);
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) writer->data[o + i] = (uint8_t)(bits >> (8 * i));
}
void qa_binary_writer_i32(qa_binary_writer *writer, int64_t value) {
  check_integer(value, -0x80000000LL, 0x7fffffffLL);
  size_t o = writer_take(writer, 4);
  uint32_t bits = (uint32_t)(int32_t)value;
  for (int i = 0; i < 4; i++) writer->data[o + i] = (uint8_t)(bits >> (8 * i));
}
void qa_binary_writer_f32(qa_binary_writer *writer, double value) {
  size_t o = writer_take(writer, 4);
  uint32_t bits = qa_float32_bits(value);
  for (int i = 0; i < 4; i++) writer->data[o + i] = (uint8_t)(bits >> (8 * i));
}
void qa_binary_writer_f64(qa_binary_writer *writer, double value) {
  size_t o = writer_take(writer, 8);
  uint64_t bits = qa_float64_bits(value);
  for (int i = 0; i < 8; i++) {
    writer->data[o + i] = (uint8_t)(bits >> (8 * i));
  }
}
void qa_binary_writer_bytes(qa_binary_writer *writer, const uint8_t *value,
                            size_t length) {
  size_t o = writer_take(writer, (int64_t)length);
  if (length > 0) memcpy(writer->data + o, value, length);
}

uint8_t *qa_binary_writer_finish(const qa_binary_writer *writer,
                                 size_t *out_length) {
  *out_length = writer->cursor;
  if (writer->cursor == 0) return NULL;
  uint8_t *copy = (uint8_t *)malloc(writer->cursor);
  if (copy != NULL) memcpy(copy, writer->data, writer->cursor);
  return copy;
}
