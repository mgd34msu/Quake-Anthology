/*
 * Port of src/llm/errors.ts. LlmSettingsError is thrown and caught all
 * over the TypeScript (settings stores its message, transports retry on
 * LlmHttpError status), so in C it is a returned error value, not an
 * abort: functions return false and fill qa_llm_error with the exact
 * message. http_status != 0 marks LlmHttpError. Plain Error throws (the
 * auth worker's startup validation) keep the contracts accommodation:
 * qa_llm_plain_error prints the identical message and aborts.
 * This header also carries the small shared helpers the llm module
 * needs (string buffer, time, randomness, SHA-256, base64url).
 * GPL-2.0-or-later.
 */
#ifndef QA_LLM_ERRORS_H
#define QA_LLM_ERRORS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

typedef struct qa_llm_error {
  char message[640];
  int http_status; /* != 0: LlmHttpError with this status */
} qa_llm_error;

void qa_llm_error_clear(qa_llm_error *error);
void qa_llm_error_set(qa_llm_error *error, const char *message);
void qa_llm_error_setf(qa_llm_error *error, const char *format, ...);
/* LlmHttpError(status): message built exactly as errors.ts builds it. */
void qa_llm_error_http(qa_llm_error *error, int status);
bool qa_llm_error_is_http(const qa_llm_error *error, int status);

/* Plain `throw new Error(msg)`: prints "Error: msg" and aborts. */
noreturn void qa_llm_plain_error(const char *message);
/* Allocation failure has no TS counterpart; aborts like qa_range_error. */
noreturn void qa_llm_oom(void);

/* Growable byte/string buffer; data is always NUL-terminated when set. */
typedef struct qa_llm_strbuf {
  char *data;
  size_t length;
  size_t capacity;
} qa_llm_strbuf;

void qa_llm_strbuf_init(qa_llm_strbuf *buf);
void qa_llm_strbuf_free(qa_llm_strbuf *buf);
void qa_llm_strbuf_append_n(qa_llm_strbuf *buf, const void *bytes, size_t length);
void qa_llm_strbuf_append(qa_llm_strbuf *buf, const char *text);
void qa_llm_strbuf_appendf(qa_llm_strbuf *buf, const char *format, ...);
void qa_llm_strbuf_append_char(qa_llm_strbuf *buf, char c);
/* Hands ownership of the (possibly empty) string to the caller. */
char *qa_llm_strbuf_steal(qa_llm_strbuf *buf);
const char *qa_llm_strbuf_text(const qa_llm_strbuf *buf);

typedef struct qa_llm_bytes {
  uint8_t *data;
  size_t length;
  size_t capacity;
} qa_llm_bytes;

void qa_llm_bytes_init(qa_llm_bytes *bytes);
void qa_llm_bytes_free(qa_llm_bytes *bytes);
void qa_llm_bytes_append(qa_llm_bytes *bytes, const void *data, size_t length);

char *qa_llm_strdup(const char *text);
char *qa_llm_strndup(const char *text, size_t length);
/* JS String.prototype.trim for ASCII whitespace (the TS data is ASCII
 * here; full Unicode White_Space trimming is the noted difference). */
char *qa_llm_trim_dup(const char *text);
bool qa_llm_trim_is_empty(const char *text);
bool qa_llm_starts_with(const char *text, const char *prefix);
bool qa_llm_ends_with(const char *text, const char *suffix);
/* Removes every trailing '/' (TS replace(/\/+$/, "")). */
char *qa_llm_strip_trailing_slashes(const char *text);
bool qa_llm_utf8_valid(const uint8_t *data, size_t length, size_t *valid_up_to);

/* Date.now() and a monotonic clock for deadlines, both milliseconds. */
int64_t qa_llm_now_ms(void);
int64_t qa_llm_monotonic_ms(void);

void qa_llm_random_bytes(uint8_t *out, size_t count);
/* crypto.randomUUID(): writes 36 chars plus NUL. */
void qa_llm_random_uuid(char out[37]);
/* createHash("sha256").update(text).digest("hex"). */
void qa_llm_sha256_hex(const void *data, size_t length, char out[65]);
char *qa_llm_sha256_base64url(const void *data, size_t length);
/* Buffer.toString("base64url") / Buffer.from(text, "base64url"). */
char *qa_llm_base64url_encode(const uint8_t *data, size_t length);
bool qa_llm_base64url_decode(const char *text, size_t length, qa_llm_bytes *out);

#endif
