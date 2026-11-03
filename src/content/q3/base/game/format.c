/* Port of src/content/q3/base/game/format.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/format.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
enum { BIG_BUFFER_BYTES = 32000, LADJUST = 0x04, ZEROPAD = 0x80 };
qa_q3_game_format_arg qa_q3_game_format_number(double v) { qa_q3_game_format_arg a = {QA_Q3_FMT_NUMBER, v, NULL}; return a; }
qa_q3_game_format_arg qa_q3_game_format_string(const char *v) { qa_q3_game_format_arg a = {QA_Q3_FMT_STRING, 0, v}; return a; }
qa_q3_game_format_arg qa_q3_game_format_null(void) { qa_q3_game_format_arg a = {QA_Q3_FMT_NULL, 0, NULL}; return a; }
typedef struct { char *buf; size_t len, cap; } out_t;
static void reserve(out_t *o, size_t bytes) {
  if (o->len + bytes >= BIG_BUFFER_BYTES) qa_range_error("game format exceeds the 32000-byte Com_sprintf buffer");
  if (o->len + bytes + 1 > o->cap) { size_t cap = o->cap ? o->cap * 2 : 256; while (cap < o->len + bytes + 1) cap *= 2; o->buf = realloc(o->buf, cap); if (!o->buf) qa_range_error("out of memory"); o->cap = cap; }
}
static void append_byte(out_t *o, int byte) { reserve(o, 1); o->buf[o->len++] = (char)(byte & 255); }
static void append_bytes(out_t *o, const char *v, size_t n) { reserve(o, n); memcpy(o->buf + o->len, v, n); o->len += n; }
static void append_rep(out_t *o, int byte, double count) {
  if (count < 0 || count != trunc(count) || count > 9007199254740991.0) qa_range_error("game format padding count is unsafe");
  reserve(o, (size_t)count); for (double i = 0; i < count; i++) o->buf[o->len++] = (char)(byte & 255);
}
static size_t byte_length(const char *v, size_t limit) { size_t end = strlen(v) < limit ? strlen(v) : limit; return end; }
static qa_q3_game_format_arg argument_at(const qa_q3_game_format_arg *args, size_t n, size_t index, char spec) {
  if (index >= n) qa_range_error("missing argument %zu for %%%c", index, spec);
  return args[index];
}
static double integer_argument(qa_q3_game_format_arg v, size_t index, char spec) {
  if (v.kind != QA_Q3_FMT_NUMBER) qa_range_error("argument %zu for %%%c must be a number", index, spec);
  if (v.number != trunc(v.number) || v.number != qa_int32(v.number)) qa_range_error("argument %zu for %%%c must be a signed 32-bit integer", index, spec);
  return v.number;
}
static double float_argument(qa_q3_game_format_arg v, size_t index) {
  if (v.kind != QA_Q3_FMT_NUMBER) qa_range_error("argument %zu for %%f must be a number", index);
  float stored = (float)v.number;
  if (!isfinite(stored) || fabs(stored) > 2147483647) qa_range_error("argument %zu for %%f is outside the source's safe int-cast range", index);
  return stored;
}
static int reversed_bytes(double value, int *bytes) {
  double remaining = value < 0 ? qa_int32(-value) : value; int n = 0;
  do { bytes[n++] = 48 + qa_js_to_int32(fmod(remaining, 10)); remaining = trunc(remaining / 10); } while (remaining != 0);
  if (value < 0) bytes[n++] = 45;
  return n;
}
static void add_int(out_t *o, double value, double width, int flags) {
  int bytes[16]; int n = reversed_bytes(value, bytes);
  if (!(flags & LADJUST)) append_rep(o, (flags & ZEROPAD) ? 48 : 32, width > n ? width - n : 0);
  for (int i = n - 1; i >= 0; i--) append_byte(o, bytes[i]);
  if (flags & LADJUST) { double rem = qa_int32(width - n); if (rem < 0) qa_range_error("left-adjusted integer width would enter the source's negative padding loop"); append_rep(o, (flags & ZEROPAD) ? 48 : 32, rem); }
}
static void add_float(out_t *o, double value, double width, double precision) {
  double remaining = value < 0 ? (float)(-value) : value; double integer = trunc(remaining);
  int bytes[16]; int n = 0; double r = integer;
  do { bytes[n++] = 48 + qa_js_to_int32(fmod(r, 10)); r = trunc(r / 10); } while (r != 0);
  if (value < 0) bytes[n++] = 45;
  append_rep(o, 32, width > n ? width - n : 0);
  for (int i = n - 1; i >= 0; i--) append_byte(o, bytes[i]);
  double digits = precision < 0 ? 6 : precision;
  if (digits > 32) qa_range_error("float precision would overflow AddFloat's 32-byte digit buffer");
  if (digits == 0) return;
  append_byte(o, 46);
  for (int i = 0; i < digits; i++) { remaining = (float)(remaining - trunc(remaining)); remaining = (float)(remaining * 10); append_byte(o, 48 + qa_js_to_int32(fmod(trunc(remaining), 10))); }
}
static void add_string(out_t *o, const char *value, int is_null, double width, double precision) {
  const char *text = is_null ? "(null)" : value; double eff = is_null ? -1 : precision;
  size_t length = byte_length(text, eff < 0 ? strlen(text) : (size_t)eff);
  append_bytes(o, text, length); double padding = qa_int32(width - (double)length);
  if (padding > 0) append_rep(o, 32, padding);
}
static int format_byte(const char *format, size_t index, size_t len) {
  if (index >= len) return -1;
  unsigned char b = (unsigned char)format[index]; if (b == 0) return -1; return b;
}
char *qa_q3_game_format(const char *format, const qa_q3_game_format_arg *args, size_t arg_count, double max_bytes) {
  if (max_bytes != trunc(max_bytes) || max_bytes < 1 || max_bytes > 9007199254740991.0)
    qa_range_error("game format destination capacity must be a positive safe integer");
  out_t o = {NULL, 0, 0}; size_t flen = strlen(format), cursor = 0, arg_index = 0;
  while (1) {
    int literal = format_byte(format, cursor, flen); if (literal < 0) break;
    if (literal != 37) { append_byte(&o, literal); cursor++; continue; }
    cursor++; int flags = 0; double width = 0, precision = -1;
    while (1) {
      int sb = format_byte(format, cursor, flen); if (sb < 0) qa_range_error("unterminated game format specifier"); cursor++;
      if (sb == 45) { flags |= LADJUST; continue; }
      if (sb == 46) { double parsed = 0; while (1) { int d = format_byte(format, cursor, flen); if (d < 48 || d > 57) break; parsed = qa_int32(10 * parsed + d - 48); cursor++; } precision = parsed < 0 ? -1 : parsed; continue; }
      if (sb == 48) { flags |= ZEROPAD; continue; }
      if (sb >= 49 && sb <= 57) { double parsed = 0; int digit = sb; while (digit >= 48 && digit <= 57) { parsed = qa_int32(10 * parsed + digit - 48); int next = format_byte(format, cursor, flen); if (next < 0) qa_range_error("unterminated game format specifier"); cursor++; digit = next; } width = parsed; cursor--; continue; }
      if (sb == 37) { append_byte(&o, 37); break; }
      qa_q3_game_format_arg argument = argument_at(args, arg_count, arg_index, (char)sb);
      if (sb == 100 || sb == 105) add_int(&o, integer_argument(argument, arg_index, (char)sb), width, flags);
      else if (sb == 102) add_float(&o, float_argument(argument, arg_index), width, precision);
      else if (sb == 115) { if (argument.kind == QA_Q3_FMT_NUMBER) qa_range_error("argument %zu for %%s must be a string or null", arg_index); add_string(&o, argument.string, argument.kind == QA_Q3_FMT_NULL, width, precision); }
      else append_byte(&o, qa_js_to_int32(integer_argument(argument, arg_index, (char)sb)));
      arg_index++; break;
    }
  }
  if (!o.buf) { o.buf = malloc(1); if (!o.buf) return NULL; }
  size_t visible = 0; while (visible < o.len && o.buf[visible]) visible++;
  size_t keep = visible < (size_t)(max_bytes - 1) ? visible : (size_t)(max_bytes - 1);
  char *result = malloc(keep + 1); if (!result) { free(o.buf); return NULL; }
  memcpy(result, o.buf, keep); result[keep] = 0; free(o.buf); return result;
}
