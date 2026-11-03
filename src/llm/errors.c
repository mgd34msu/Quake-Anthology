#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/errors.h. GPL-2.0-or-later. */
#include "qa/llm/errors.h"
#include "qa/foundation/js.h"

#include <ctype.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void qa_llm_error_clear(qa_llm_error *error) {
  if (error == NULL) return;
  error->message[0] = '\0';
  error->http_status = 0;
}

void qa_llm_error_set(qa_llm_error *error, const char *message) {
  if (error == NULL) return;
  snprintf(error->message, sizeof error->message, "%s", message != NULL ? message : "");
  error->http_status = 0;
}

void qa_llm_error_setf(qa_llm_error *error, const char *format, ...) {
  if (error == NULL) return;
  va_list args;
  va_start(args, format);
  vsnprintf(error->message, sizeof error->message, format, args);
  va_end(args);
  error->http_status = 0;
}

void qa_llm_error_http(qa_llm_error *error, int status) {
  if (error == NULL) return;
  const char *advice = status == 401 || status == 403 ? "Check the selected provider's credential or sign in again."
      : status == 429 ? "The service limit was reached. Try again later."
      : status == 400 || status == 404 ? "Check the model and service settings."
      : "Try again later.";
  snprintf(error->message, sizeof error->message, "LLM service returned HTTP %d. %s", status, advice);
  error->http_status = status;
}

bool qa_llm_error_is_http(const qa_llm_error *error, int status) {
  return error != NULL && error->http_status == status;
}

noreturn void qa_llm_plain_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message != NULL ? message : "");
  abort();
}

noreturn void qa_llm_oom(void) {
  fputs("Error: out of memory\n", stderr);
  abort();
}

static void *xmalloc(size_t size) {
  void *ptr = malloc(size == 0 ? 1 : size);
  if (ptr == NULL) qa_llm_oom();
  return ptr;
}

static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size == 0 ? 1 : size);
  if (next == NULL) qa_llm_oom();
  return next;
}

void qa_llm_strbuf_init(qa_llm_strbuf *buf) { buf->data = NULL; buf->length = 0; buf->capacity = 0; }
void qa_llm_strbuf_free(qa_llm_strbuf *buf) { free(buf->data); qa_llm_strbuf_init(buf); }

void qa_llm_strbuf_append_n(qa_llm_strbuf *buf, const void *bytes, size_t length) {
  if (buf->length + length + 1 > buf->capacity) {
    size_t capacity = buf->capacity == 0 ? 64 : buf->capacity;
    while (capacity < buf->length + length + 1) capacity *= 2;
    buf->data = xrealloc(buf->data, capacity);
    buf->capacity = capacity;
  }
  if (length > 0) memcpy(buf->data + buf->length, bytes, length);
  buf->length += length;
  buf->data[buf->length] = '\0';
}

void qa_llm_strbuf_append(qa_llm_strbuf *buf, const char *text) {
  if (text != NULL) qa_llm_strbuf_append_n(buf, text, strlen(text));
}

void qa_llm_strbuf_append_char(qa_llm_strbuf *buf, char c) { qa_llm_strbuf_append_n(buf, &c, 1); }

void qa_llm_strbuf_appendf(qa_llm_strbuf *buf, const char *format, ...) {
  char scratch[1024];
  va_list args;
  va_start(args, format);
  int written = vsnprintf(scratch, sizeof scratch, format, args);
  va_end(args);
  if (written < 0) return;
  if ((size_t)written < sizeof scratch) {
    qa_llm_strbuf_append_n(buf, scratch, (size_t)written);
    return;
  }
  char *big = xmalloc((size_t)written + 1);
  va_start(args, format);
  vsnprintf(big, (size_t)written + 1, format, args);
  va_end(args);
  qa_llm_strbuf_append_n(buf, big, (size_t)written);
  free(big);
}

char *qa_llm_strbuf_steal(qa_llm_strbuf *buf) {
  if (buf->data == NULL) return qa_llm_strdup("");
  char *data = buf->data;
  qa_llm_strbuf_init(buf);
  return data;
}

const char *qa_llm_strbuf_text(const qa_llm_strbuf *buf) { return buf->data != NULL ? buf->data : ""; }

void qa_llm_bytes_init(qa_llm_bytes *bytes) { bytes->data = NULL; bytes->length = 0; bytes->capacity = 0; }
void qa_llm_bytes_free(qa_llm_bytes *bytes) { free(bytes->data); qa_llm_bytes_init(bytes); }

void qa_llm_bytes_append(qa_llm_bytes *bytes, const void *data, size_t length) {
  if (bytes->length + length > bytes->capacity) {
    size_t capacity = bytes->capacity == 0 ? 64 : bytes->capacity;
    while (capacity < bytes->length + length) capacity *= 2;
    bytes->data = xrealloc(bytes->data, capacity);
    bytes->capacity = capacity;
  }
  if (length > 0) memcpy(bytes->data + bytes->length, data, length);
  bytes->length += length;
}

char *qa_llm_strdup(const char *text) {
  if (text == NULL) text = "";
  size_t length = strlen(text);
  char *copy = xmalloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}

char *qa_llm_strndup(const char *text, size_t length) {
  char *copy = xmalloc(length + 1);
  if (length > 0) memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

/* String.prototype.trim: strips every JS WhiteSpace and LineTerminator code
 * point (qa_js_white_space_length) from both ends. */
char *qa_llm_trim_dup(const char *text) {
  if (text == NULL) return qa_llm_strdup("");
  size_t begin = 0, length;
  while ((length = qa_js_white_space_length(text + begin)) != 0) begin += length;
  size_t end = begin, at = begin;
  while (text[at] != '\0') {
    length = qa_js_white_space_length(text + at);
    if (length != 0) { at += length; continue; }
    at++;
    end = at;
  }
  return qa_llm_strndup(text + begin, end - begin);
}

bool qa_llm_utf8_incomplete_tail(const uint8_t *tail, size_t length) {
  if (length == 0) return false;
  uint8_t lead = tail[0];
  size_t need = lead >= 0xc2 && lead <= 0xdf ? 2 : lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
  if (need == 0 || length >= need) return false;
  for (size_t i = 1; i < length; i++) {
    uint8_t lower = 0x80, upper = 0xbf;
    if (i == 1) {
      if (lead == 0xe0) lower = 0xa0;
      else if (lead == 0xed) upper = 0x9f;
      else if (lead == 0xf0) lower = 0x90;
      else if (lead == 0xf4) upper = 0x8f;
    }
    if (tail[i] < lower || tail[i] > upper) return false;
  }
  return true;
}

size_t qa_llm_utf16_length(const char *text) {
  size_t units = 0;
  for (const uint8_t *p = (const uint8_t *)text; *p != '\0'; p++) {
    if ((*p & 0xc0) == 0x80) continue;
    units += *p >= 0xf0 ? 2 : 1;
  }
  return units;
}

/* Buffer.toString("utf8") / TextDecoder (not fatal): each maximal invalid
 * subpart becomes U+FFFD, as the WHATWG UTF-8 decoder does. */
void qa_llm_utf8_decode_lossy(const uint8_t *data, size_t length, qa_llm_bytes *out) {
  static const uint8_t replacement[3] = {0xef, 0xbf, 0xbd};
  size_t i = 0;
  while (i < length) {
    uint8_t lead = data[i];
    if (lead < 0x80) { qa_llm_bytes_append(out, &lead, 1); i++; continue; }
    size_t need = lead >= 0xc2 && lead <= 0xdf ? 1 : lead >= 0xe0 && lead <= 0xef ? 2 : lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
    if (need == 0) { qa_llm_bytes_append(out, replacement, 3); i++; continue; }
    size_t seen = 0;
    while (seen < need && i + 1 + seen < length) {
      uint8_t lower = 0x80, upper = 0xbf, byte = data[i + 1 + seen];
      if (seen == 0) {
        if (lead == 0xe0) lower = 0xa0;
        else if (lead == 0xed) upper = 0x9f;
        else if (lead == 0xf0) lower = 0x90;
        else if (lead == 0xf4) upper = 0x8f;
      }
      if (byte < lower || byte > upper) break;
      seen++;
    }
    if (seen == need) qa_llm_bytes_append(out, data + i, need + 1);
    else qa_llm_bytes_append(out, replacement, 3);
    i += 1 + seen;
  }
}

bool qa_llm_trim_is_empty(const char *text) {
  char *trimmed = qa_llm_trim_dup(text);
  bool empty = trimmed[0] == '\0';
  free(trimmed);
  return empty;
}

bool qa_llm_starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

bool qa_llm_ends_with(const char *text, const char *suffix) {
  size_t text_len = strlen(text), suffix_len = strlen(suffix);
  return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

char *qa_llm_strip_trailing_slashes(const char *text) {
  size_t end = strlen(text);
  while (end > 0 && text[end - 1] == '/') end--;
  return qa_llm_strndup(text, end);
}

bool qa_llm_utf8_valid(const uint8_t *data, size_t length, size_t *valid_up_to) {
  size_t i = 0;
  while (i < length) {
    uint8_t c = data[i];
    if (c < 0x80) { i++; continue; }
    size_t extra;
    uint32_t value, minimum;
    if ((c & 0xe0) == 0xc0) { extra = 1; value = c & 0x1f; minimum = 0x80; }
    else if ((c & 0xf0) == 0xe0) { extra = 2; value = c & 0x0f; minimum = 0x800; }
    else if ((c & 0xf8) == 0xf0) { extra = 3; value = c & 0x07; minimum = 0x10000; }
    else { if (valid_up_to != NULL) *valid_up_to = i; return false; }
    if (i + extra >= length) { if (valid_up_to != NULL) *valid_up_to = i; return false; }
    for (size_t k = 1; k <= extra; k++) {
      if ((data[i + k] & 0xc0) != 0x80) { if (valid_up_to != NULL) *valid_up_to = i; return false; }
      value = (value << 6) | (uint32_t)(data[i + k] & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
      if (valid_up_to != NULL) *valid_up_to = i;
      return false;
    }
    i += extra + 1;
  }
  if (valid_up_to != NULL) *valid_up_to = length;
  return true;
}

int64_t qa_llm_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t qa_llm_monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void qa_llm_random_bytes(uint8_t *out, size_t count) {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    size_t done = 0;
    while (done < count) {
      ssize_t got = read(fd, out + done, count - done);
      if (got <= 0) break;
      done += (size_t)got;
    }
    close(fd);
    if (done == count) return;
  }
  /* /dev/urandom unavailable: node:crypto would throw; abort loudly. */
  qa_llm_plain_error("Random source unavailable.");
}

void qa_llm_random_uuid(char out[37]) {
  uint8_t bytes[16];
  qa_llm_random_bytes(bytes, sizeof bytes);
  bytes[6] = (uint8_t)((bytes[6] & 0x0f) | 0x40);
  bytes[8] = (uint8_t)((bytes[8] & 0x3f) | 0x80);
  snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
           bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

void qa_llm_sha256_hex(const void *data, size_t length, char out[65]) {
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256((const uint8_t *)data, length, digest);
  static const char hexdigits[] = "0123456789abcdef";
  for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    out[i * 2] = hexdigits[digest[i] >> 4];
    out[i * 2 + 1] = hexdigits[digest[i] & 0x0f];
  }
  out[64] = '\0';
}

static const char b64url_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

char *qa_llm_base64url_encode(const uint8_t *data, size_t length) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  for (size_t i = 0; i < length; i += 3) {
    size_t remain = length - i;
    uint32_t triple = (uint32_t)data[i] << 16;
    if (remain > 1) triple |= (uint32_t)data[i + 1] << 8;
    if (remain > 2) triple |= data[i + 2];
    qa_llm_strbuf_append_char(&out, b64url_alphabet[(triple >> 18) & 63]);
    qa_llm_strbuf_append_char(&out, b64url_alphabet[(triple >> 12) & 63]);
    if (remain > 1) qa_llm_strbuf_append_char(&out, b64url_alphabet[(triple >> 6) & 63]);
    if (remain > 2) qa_llm_strbuf_append_char(&out, b64url_alphabet[triple & 63]);
  }
  return qa_llm_strbuf_steal(&out);
}

char *qa_llm_sha256_base64url(const void *data, size_t length) {
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256((const uint8_t *)data, length, digest);
  return qa_llm_base64url_encode(digest, sizeof digest);
}

static int b64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-' || c == '+') return 62;
  if (c == '_' || c == '/') return 63;
  return -1;
}

bool qa_llm_base64url_decode(const char *text, size_t length, qa_llm_bytes *out) {
  uint32_t accumulator = 0;
  int bits = 0;
  for (size_t i = 0; i < length; i++) {
    if (text[i] == '=') break;
    int value = b64_value(text[i]);
    if (value < 0) return false;
    accumulator = (accumulator << 6) | (uint32_t)value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      uint8_t byte = (uint8_t)(accumulator >> bits);
      qa_llm_bytes_append(out, &byte, 1);
    }
  }
  return true;
}
