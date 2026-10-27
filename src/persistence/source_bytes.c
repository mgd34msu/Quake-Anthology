/* Port of src/persistence/source-bytes.ts. GPL-2.0-or-later. */
#include "qa/persistence/source_bytes.h"

#include <stdlib.h>
#include <string.h>

#include "qa/persistence/value.h"

char *qa_byte_text(const uint8_t *bytes, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  if (length > 0) memcpy(out, bytes, length);
  out[length] = '\0';
  return out;
}
uint8_t *qa_text_bytes(const char *text, size_t length) {
  uint8_t *bytes = length > 0 ? (uint8_t *)malloc(length) : NULL;
  if (length > 0 && bytes == NULL) return NULL;
  for (size_t i = 0; i < length; i++) {
    unsigned byte = (unsigned char)text[i];
    if (byte > 255) qa_save_format_error("text", "source save requires byte characters");
    bytes[i] = (uint8_t)byte;
  }
  return bytes;
}
char *qa_read_cstring(qa_binary_reader *reader) {
  size_t capacity = 32;
  size_t length = 0;
  uint8_t *bytes = (uint8_t *)malloc(capacity);
  if (bytes == NULL) return NULL;
  for (;;) {
    uint8_t byte = qa_binary_reader_u8(reader);
    if (byte == 0) break;
    if (length == capacity) {
      capacity *= 2;
      uint8_t *grown = (uint8_t *)realloc(bytes, capacity);
      if (grown == NULL) { free(bytes); return NULL; }
      bytes = grown;
    }
    bytes[length++] = byte;
  }
  char *out = qa_byte_text(bytes, length);
  free(bytes);
  return out;
}
void qa_write_cstring(qa_binary_writer *writer, const char *value, size_t length) {
  for (size_t i = 0; i < length; i++)
    if (value[i] == '\0')
      qa_save_format_error("text", "source string contains an interior NUL");
  uint8_t *bytes = qa_text_bytes(value, length);
  if (length > 0 && bytes == NULL) return;
  qa_binary_writer_bytes(writer, bytes, length);
  free(bytes);
  qa_binary_writer_u8(writer, 0);
}
char *qa_read_fixed(qa_binary_reader *reader, int64_t width) {
  uint8_t *bytes = qa_binary_reader_bytes(reader, width);
  size_t total = width > 0 ? (size_t)width : 0;
  size_t end = total;
  if (bytes != NULL) {
    for (size_t i = 0; i < total; i++)
      if (bytes[i] == 0) { end = i; break; }
  }
  char *out = qa_byte_text(bytes, end);
  free(bytes);
  return out;
}
void qa_write_fixed(qa_binary_writer *writer, const char *value, size_t length, int64_t width) {
  bool interior = false;
  for (size_t i = 0; i < length; i++) if (value[i] == '\0') interior = true;
  if (interior || (int64_t)length >= width)
    qa_save_format_error("text", "source string must fit %lld bytes", (long long)(width - 1));
  size_t total = width > 0 ? (size_t)width : 0;
  uint8_t *bytes = total > 0 ? (uint8_t *)calloc(total, 1) : NULL;
  if (total > 0 && bytes == NULL) return;
  uint8_t *text = qa_text_bytes(value, length);
  if (length > 0 && text == NULL) { free(bytes); return; }
  if (length > 0) memcpy(bytes, text, length);
  free(text);
  qa_binary_writer_bytes(writer, bytes, total);
  free(bytes);
}
