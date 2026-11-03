/* Port of src/network/q1/message.ts. GPL-2.0-or-later. */
#include "qa/network/q1/message.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/constants.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
noreturn void qa_q1_packet_error(const char *message, long long offset) {
  qa_raise("PacketError", 0, offset, "%s at byte %lld", message, offset);
}
static int is_safe_ll(double v) { return isfinite(v) && v == trunc(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0; }
int qa_size_buf_init(qa_size_buf *sb, long long maxsize, bool allow_overflow) {
  memset(sb, 0, sizeof *sb); sb->maxsize = maxsize; sb->allow_overflow = allow_overflow;
  sb->data = calloc((size_t)(maxsize > 0 ? maxsize : 1), 1);
  return sb->data != NULL;
}
void qa_size_buf_destroy(qa_size_buf *sb) { free(sb->data); memset(sb, 0, sizeof *sb); }
long long qa_size_buf_reserve(qa_size_buf *sb, long long length) {
  if (length < 0 || length > sb->maxsize) qa_range_error("Invalid message write size");
  if (sb->cursize + length > sb->maxsize) {
    if (!sb->allow_overflow) qa_range_error("Quake message overflow");
    sb->cursize = 0; sb->overflowed = true;
  }
  long long offset = sb->cursize; sb->cursize += length; return offset;
}
void qa_size_buf_clear(qa_size_buf *sb) { sb->cursize = 0; sb->overflowed = false; }
uint8_t *qa_size_buf_bytes(const qa_size_buf *sb, size_t *out_len) {
  *out_len = (size_t)sb->cursize;
  uint8_t *p = malloc(*out_len ? *out_len : 1);
  if (*out_len) memcpy(p, sb->data, *out_len);
  return p;
}
void qa_sz_write(qa_size_buf *sb, const uint8_t *data, long long data_len, long long length) {
  if (length > data_len) qa_range_error("Message source too short");
  long long off = qa_size_buf_reserve(sb, length);
  memcpy(sb->data + off, data, (size_t)length);
}
/* JS ToIntegerOrInfinity then modulo 2^n, as DataView integer setters do. */
static uint32_t js_mod_u32(double n) {
  if (!isfinite(n)) return 0;
  double t = trunc(n);
  double m = fmod(t, 4294967296.0);
  if (m < 0) m += 4294967296.0;
  return (uint32_t)m;
}
void qa_msg_write_byte(qa_size_buf *sb, double n) {
  /* TS: n & 255 (ToInt32 first). */
  int32_t i = (int32_t)js_mod_u32(n);
  sb->data[qa_size_buf_reserve(sb, 1)] = (uint8_t)(i & 255);
}
void qa_msg_write_short(qa_size_buf *sb, double n) {
  uint16_t v = (uint16_t)js_mod_u32(n);
  long long o = qa_size_buf_reserve(sb, 2);
  sb->data[o] = (uint8_t)v; sb->data[o+1] = (uint8_t)(v >> 8);
}
void qa_msg_write_long(qa_size_buf *sb, double n) {
  uint32_t v = js_mod_u32(n);
  long long o = qa_size_buf_reserve(sb, 4);
  sb->data[o]=(uint8_t)v; sb->data[o+1]=(uint8_t)(v>>8); sb->data[o+2]=(uint8_t)(v>>16); sb->data[o+3]=(uint8_t)(v>>24);
}
void qa_msg_write_float(qa_size_buf *sb, double n) {
  float f = (float)n; uint32_t v; memcpy(&v, &f, 4);
  long long o = qa_size_buf_reserve(sb, 4);
  sb->data[o]=(uint8_t)v; sb->data[o+1]=(uint8_t)(v>>8); sb->data[o+2]=(uint8_t)(v>>16); sb->data[o+3]=(uint8_t)(v>>24);
}
void qa_msg_write_string(qa_size_buf *sb, const char *s) {
  if (s) for (const char *p = s; *p; p++) qa_msg_write_byte(sb, (double)(uint8_t)*p);
  qa_msg_write_byte(sb, 0);
}
void qa_msg_write_coord(qa_size_buf *sb, double n) { qa_msg_write_short(sb, trunc(n * 8)); }
void qa_msg_write_angle(qa_size_buf *sb, double n) { qa_msg_write_byte(sb, trunc(trunc(n) * 256 / 360)); }
void qa_msg_write_coord_flags(qa_size_buf *sb, double n, int64_t flags) {
  if (flags & QA_Q1_PRFL_FLOATCOORD) qa_msg_write_float(sb, n);
  else if (flags & QA_Q1_PRFL_INT32COORD) qa_msg_write_long(sb, qa_q1_rint(n * 16));
  else if (flags & QA_Q1_PRFL_24BITCOORD) { qa_msg_write_short(sb, n); qa_msg_write_byte(sb, fmod(trunc(n * 255), 255.0)); }
  else qa_msg_write_short(sb, qa_q1_rint(n * 8));
}
void qa_msg_write_angle_flags(qa_size_buf *sb, double n, int64_t flags) {
  if (flags & QA_Q1_PRFL_FLOATANGLE) qa_msg_write_float(sb, n);
  else if (flags & QA_Q1_PRFL_SHORTANGLE) qa_msg_write_short(sb, qa_q1_rint(n * 65536 / 360));
  else qa_msg_write_byte(sb, qa_q1_rint(n * 256 / 360));
}
void qa_msg_write_angle16(qa_size_buf *sb, double value, int64_t flags) {
  if (flags & QA_Q1_PRFL_FLOATANGLE) qa_msg_write_float(sb, value);
  else qa_msg_write_short(sb, qa_q1_rint(value * 65536 / 360));
}
void qa_message_reader_init(qa_message_reader *r, const uint8_t *data, size_t length) {
  r->data = data; r->length = length; r->offset = 0; r->badread = false;
}
long long qa_message_reader_remaining(const qa_message_reader *r) {
  long long v = (long long)r->length - r->offset; return v > 0 ? v : 0;
}
static long long take(qa_message_reader *r, long long n) {
  if (r->offset + n > (long long)r->length) { r->badread = true; return -1; }
  long long o = r->offset; r->offset += n; return o;
}
double qa_message_reader_byte(qa_message_reader *r) { long long i = take(r, 1); return i < 0 ? -1 : r->data[i]; }
double qa_message_reader_char(qa_message_reader *r) { long long i = take(r, 1); return i < 0 ? -1 : (int8_t)r->data[i]; }
double qa_message_reader_short(qa_message_reader *r) {
  long long i = take(r, 2); if (i < 0) return -1;
  return (int16_t)((uint16_t)r->data[i] | ((uint16_t)r->data[i+1] << 8));
}
double qa_message_reader_long(qa_message_reader *r) {
  long long i = take(r, 4); if (i < 0) return -1;
  return (int32_t)((uint32_t)r->data[i] | ((uint32_t)r->data[i+1]<<8) | ((uint32_t)r->data[i+2]<<16) | ((uint32_t)r->data[i+3]<<24));
}
double qa_message_reader_float(qa_message_reader *r) {
  long long i = take(r, 4); if (i < 0) return -1;
  uint32_t v = (uint32_t)r->data[i] | ((uint32_t)r->data[i+1]<<8) | ((uint32_t)r->data[i+2]<<16) | ((uint32_t)r->data[i+3]<<24);
  float f; memcpy(&f, &v, 4); return (double)f;
}
char *qa_message_reader_string(qa_message_reader *r, long long limit, bool quakeworld, size_t *out_len) {
  size_t cap = 32, len = 0; char *s = malloc(cap);
  while ((long long)len < limit) {
    double c = qa_message_reader_char(r);
    if (c == -1 || c == 0) break;
    if (quakeworld && c == 10) continue;
    if (len + 1 >= cap) { cap *= 2; s = realloc(s, cap); }
    s[len++] = (char)(qa_js_to_int32(c) & 255);
  }
  s[len] = '\0'; if (out_len) *out_len = len; return s;
}
double qa_message_reader_coord(qa_message_reader *r) { return qa_message_reader_short(r) / 8; }
double qa_message_reader_angle(qa_message_reader *r) { return qa_message_reader_char(r) * 360 / 256; }
double qa_message_reader_coord_flags(qa_message_reader *r, int64_t flags) {
  if (flags & QA_Q1_PRFL_FLOATCOORD) return qa_message_reader_float(r);
  if (flags & QA_Q1_PRFL_INT32COORD) return qa_message_reader_long(r) / 16;
  if (flags & QA_Q1_PRFL_24BITCOORD) {
    double whole = qa_message_reader_short(r);
    return whole + qa_message_reader_byte(r) / 255;
  }
  return qa_message_reader_coord(r);
}
double qa_message_reader_angle_flags(qa_message_reader *r, int64_t flags) {
  if (flags & QA_Q1_PRFL_FLOATANGLE) return qa_message_reader_float(r);
  if (flags & QA_Q1_PRFL_SHORTANGLE) return qa_message_reader_short(r) * 360 / 65536;
  return qa_message_reader_angle(r);
}
uint8_t *qa_message_reader_bytes(qa_message_reader *r, long long length, size_t *out_len) {
  if (!is_safe_ll((double)length) || length < 0) qa_q1_packet_error("Invalid byte count", r->offset);
  long long i = take(r, length);
  if (i < 0) qa_q1_packet_error("Truncated message", r->offset);
  *out_len = (size_t)length;
  uint8_t *p = malloc(*out_len ? *out_len : 1);
  if (*out_len) memcpy(p, r->data + i, *out_len);
  return p;
}
void qa_message_reader_finish(const qa_message_reader *r) {
  if (r->badread) qa_q1_packet_error("Truncated message", r->offset);
}
double qa_read_move_angle16(qa_message_reader *r, int64_t flags) {
  return (flags & QA_Q1_PRFL_FLOATANGLE) ? qa_message_reader_float(r) : qa_message_reader_short(r) * 360 / 65536;
}
