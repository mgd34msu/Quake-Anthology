/* Port of src/network/q3/message.ts. GPL-2.0-or-later. */
#include "qa/network/q3/message.h"
#include "qa/core/binary/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/huffman.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void qa_q3_source_message_state_init(qa_q3_source_message_state *s, qa_q3_message_print_fn print, void *ctx) {
  memset(s, 0, sizeof *s); s->print = print; s->print_ctx = ctx;
  s->has_oldsize = s->has_newsize = s->has_overflows = true;
}
static bool sms_add(int64_t value, bool has, int64_t inc, int64_t *out) {
  if (!has) return false;
  if ((inc > 0 && value > 0x7fffffff - inc) || (inc < 0 && value < -((int64_t)0x80000000) - inc)) return false;
  *out = value + inc; return true;
}
void qa_q3_source_message_state_add_oldsize(qa_q3_source_message_state *s, int64_t bits) {
  int64_t o; s->has_oldsize = sms_add(s->oldsize, s->has_oldsize, bits, &o); if (s->has_oldsize) s->oldsize = o;
}
void qa_q3_source_message_state_add_newsize(qa_q3_source_message_state *s, int64_t bytes) {
  int64_t o; s->has_newsize = sms_add(s->newsize, s->has_newsize, bytes, &o); if (s->has_newsize) s->newsize = o;
}
void qa_q3_source_message_state_check_overflow(qa_q3_source_message_state *s, int64_t value, int64_t bits) {
  if (bits == 32) return;
  if (bits < 0 || bits == 31) { s->has_overflows = false; return; }
  if (value > ((int64_t)1 << bits) - 1 || value < 0) {
    int64_t o; s->has_overflows = sms_add(s->overflows, s->has_overflows, 1, &o); if (s->has_overflows) s->overflows = o;
  }
}
void qa_q3_source_message_state_print(const qa_q3_source_message_state *s, const char *text) {
  if (s && s->print) s->print(s->print_ctx, text);
}
static int64_t width_of(int64_t bits, qa_q3_message_mode mode) {
  if (bits == 0 || bits < -31 || bits > 32) qa_range_error("Invalid message bit width");
  int64_t width = bits < 0 ? -bits : bits;
  if (mode == QA_Q3_MESSAGE_OOB && width != 8 && width != 16 && width != 32)
    qa_range_error("OOB messages require 8, 16 or 32 bits");
  return width;
}
static int64_t angle_integer(double value, double scale) {
  float scaled = (float)((float)((float)value * (float)scale) / 360.0f);
  /* TS: fround(fround(fround(value)*scale)/360); scale exact in float. */
  scaled = (float)((double)(float)((float)value * (float)scale) / 360.0);
  if (!isfinite(scaled) || scaled < -2147483648.0f || scaled >= 2147483648.0f)
    qa_range_error("Undefined native message angle float-to-int conversion");
  return (int64_t)trunc((double)scaled);
}
struct qa_q3_message_writer {
  uint8_t *buffer; int64_t capacity; int64_t position; int64_t size;
  qa_q3_message_mode mode; bool overflowed; qa_q3_source_message_state *source_state;
};
bool qa_q3_message_writer_init(qa_q3_message_writer **out, qa_q3_message_mode mode, int64_t capacity, qa_q3_source_message_state *source_state) {
  if (capacity < 0 || capacity > QA_Q3_MAX_MESSAGE_LENGTH) qa_range_error("Invalid message capacity");
  qa_q3_message_writer *w = calloc(1, sizeof *w);
  if (!w) return false;
  w->buffer = calloc((size_t)(capacity ? capacity : 1), 1);
  if (!w->buffer) { free(w); return false; }
  w->capacity = capacity; w->mode = mode; w->source_state = source_state;
  *out = w; return true;
}
bool qa_q3_message_writer_init_default(qa_q3_message_writer **out) {
  return qa_q3_message_writer_init(out, QA_Q3_MESSAGE_BITSTREAM, QA_Q3_MAX_MESSAGE_LENGTH, NULL);
}
void qa_q3_message_writer_destroy(qa_q3_message_writer *w) { if (!w) return; free(w->buffer); free(w); }
int64_t qa_q3_message_writer_bit_position(const qa_q3_message_writer *w) { return w->position; }
qa_q3_message_mode qa_q3_message_writer_get_mode(const qa_q3_message_writer *w) { return w->mode; }
int64_t qa_q3_message_writer_byte_length(const qa_q3_message_writer *w) { return w->size; }
int64_t qa_q3_message_writer_capacity(const qa_q3_message_writer *w) { return w->capacity; }
bool qa_q3_message_writer_overflowed(const qa_q3_message_writer *w) { return w->overflowed; }
qa_q3_source_message_state *qa_q3_message_writer_source_state(const qa_q3_message_writer *w) { return w->source_state; }
bool qa_q3_message_writer_to_bytes(const qa_q3_message_writer *w, uint8_t **out, size_t *len) {
  *out = malloc((size_t)(w->size ? w->size : 1));
  if (!*out) return false;
  memcpy(*out, w->buffer, (size_t)w->size); *len = (size_t)w->size; return true;
}
void qa_q3_message_writer_clear(qa_q3_message_writer *w) { w->position = 0; w->size = 0; w->overflowed = false; }
void qa_q3_message_writer_bitstream(qa_q3_message_writer *w) { w->mode = QA_Q3_MESSAGE_BITSTREAM; }
bool qa_q3_message_writer_copy(const qa_q3_message_writer *w, qa_q3_message_writer **out) {
  if (!qa_q3_message_writer_init(out, w->mode, w->capacity, w->source_state)) return false;
  memcpy((*out)->buffer, w->buffer, (size_t)w->size);
  (*out)->position = w->position; (*out)->size = w->size; (*out)->overflowed = w->overflowed;
  return true;
}
typedef struct { uint8_t bits[4096]; size_t count; } enc_t;
static void enc_put(void *ctx, int bit) { enc_t *e = ctx; if (e->count < sizeof e->bits) e->bits[e->count++] = (uint8_t)bit; }
void qa_q3_message_writer_write_bits(qa_q3_message_writer *w, int64_t value, int64_t bits) {
  if (w->source_state) qa_q3_source_message_state_add_oldsize(w->source_state, bits);
  if (w->capacity - w->size < 4) { w->overflowed = true; return; }
  int64_t width = width_of(bits, QA_Q3_MESSAGE_BITSTREAM);
  if (w->source_state) qa_q3_source_message_state_check_overflow(w->source_state, value, bits);
  if (w->mode == QA_Q3_MESSAGE_OOB) {
    width_of(bits, w->mode);
    uint32_t v = (uint32_t)value;
    if (width == 8) w->buffer[w->size] = (uint8_t)v;
    else if (width == 16) { w->buffer[w->size] = (uint8_t)v; w->buffer[w->size+1] = (uint8_t)(v >> 8); }
    else for (int i = 0; i < 4; i++) w->buffer[w->size+i] = (uint8_t)(v >> (8*i));
    w->size += width >> 3;
    w->position += width == 32 ? 8 : width; /* original OOB long quirk */
    return;
  }
  uint32_t remaining = (uint32_t)value;
  enc_t enc; enc.count = 0;
  for (int64_t i = 0; i < (width & 7); i++) { enc_put(&enc, (int)(remaining & 1u)); remaining >>= 1; }
  const qa_q3_huffman_codec *codec = qa_q3_create_message_huffman();
  for (int64_t i = width & 7; i < width; i += 8) {
    codec->encode_symbol(codec->ctx, (int32_t)(remaining & 255u), enc_put, &enc);
    remaining >>= 8;
  }
  int64_t size = ((w->position + (int64_t)enc.count) >> 3) + 1;
  if (size > w->capacity) { w->overflowed = true; return; }
  for (size_t i = 0; i < enc.count; i++) {
    int64_t offset = w->position >> 3;
    if ((w->position & 7) == 0) w->buffer[offset] = 0;
    w->buffer[offset] = (uint8_t)(w->buffer[offset] | (uint8_t)(enc.bits[i] << (w->position & 7)));
    w->position++;
  }
  w->size = size;
}
void qa_q3_message_writer_write_bits_double(qa_q3_message_writer *w, double value, int64_t bits) {
  if (!isfinite(value) || trunc(value) != value) {
    /* TS checks after oldsize/reserve; reserve-side overflow returns first. */
    if (w->source_state) qa_q3_source_message_state_add_oldsize(w->source_state, bits);
    if (w->capacity - w->size < 4) { w->overflowed = true; return; }
    qa_range_error("Message integer must be integral");
  }
  qa_q3_message_writer_write_bits(w, (int64_t)value, bits);
}
void qa_q3_message_writer_write_byte(qa_q3_message_writer *w, int64_t v) { qa_q3_message_writer_write_bits(w, v, 8); }
void qa_q3_message_writer_write_char(qa_q3_message_writer *w, int64_t v) { qa_q3_message_writer_write_bits(w, v, 8); }
void qa_q3_message_writer_write_short(qa_q3_message_writer *w, int64_t v) { qa_q3_message_writer_write_bits(w, v, 16); }
void qa_q3_message_writer_write_long(qa_q3_message_writer *w, int64_t v) { qa_q3_message_writer_write_bits(w, v, 32); }
void qa_q3_message_writer_write_float(qa_q3_message_writer *w, double v) { qa_q3_message_writer_write_long(w, (int32_t)qa_float32_to_bits(v)); }
void qa_q3_message_writer_write_data(qa_q3_message_writer *w, const uint8_t *d, size_t n) { for (size_t i = 0; i < n; i++) qa_q3_message_writer_write_byte(w, d[i]); }
static void write_text(qa_q3_message_writer *w, const char *value, int64_t limit) {
  if (!value) { qa_q3_message_writer_write_byte(w, 0); return; }
  size_t len = strlen(value); /* C string stops at NUL like TS indexOf("\0") slice */
  if ((int64_t)len < limit) {
    for (size_t i = 0; i < len; i++) {
      unsigned char c = (unsigned char)value[i];
      qa_q3_message_writer_write_byte(w, c > 127 ? 46 : c);
    }
  } else if (w->source_state)
    qa_q3_source_message_state_print(w->source_state, limit == 1024 ? "MSG_WriteString: MAX_STRING_CHARS" : "MSG_WriteString: BIG_INFO_STRING");
  qa_q3_message_writer_write_byte(w, 0);
}
void qa_q3_message_writer_write_string(qa_q3_message_writer *w, const char *v) { write_text(w, v, 1024); }
void qa_q3_message_writer_write_big_string(qa_q3_message_writer *w, const char *v) { write_text(w, v, 8192); }
void qa_q3_message_writer_write_angle(qa_q3_message_writer *w, double v) { qa_q3_message_writer_write_byte(w, angle_integer(v, 256) & 255); }
void qa_q3_message_writer_write_angle16(qa_q3_message_writer *w, double v) { qa_q3_message_writer_write_short(w, angle_integer(v, 65536) & 65535); }
void qa_q3_message_writer_write_delta(qa_q3_message_writer *w, int64_t p, int64_t v, int64_t bits) {
  qa_q3_message_writer_write_bits(w, p == v ? 0 : 1, 1);
  if (p != v) qa_q3_message_writer_write_bits(w, v, bits);
}
void qa_q3_message_writer_write_delta_key(qa_q3_message_writer *w, int64_t key, int64_t p, int64_t v, int64_t bits) {
  qa_q3_message_writer_write_bits(w, p == v ? 0 : 1, 1);
  if (p != v) qa_q3_message_writer_write_bits(w, (int32_t)((uint32_t)v ^ (uint32_t)key), bits);
}
void qa_q3_message_writer_write_delta_float(qa_q3_message_writer *w, double p, double v) {
  float of = (float)p, nf = (float)v;
  qa_q3_message_writer_write_bits(w, of == nf ? 0 : 1, 1);
  if (of != nf) qa_q3_message_writer_write_bits(w, (int32_t)qa_float32_to_bits(nf), 32);
}
void qa_q3_message_writer_write_delta_key_float(qa_q3_message_writer *w, int64_t key, double p, double v) {
  float of = (float)p, nf = (float)v;
  qa_q3_message_writer_write_bits(w, of == nf ? 0 : 1, 1);
  if (of != nf) qa_q3_message_writer_write_bits(w, (int32_t)(qa_float32_to_bits(nf) ^ (uint32_t)key), 32);
}

struct qa_q3_message_reader {
  const uint8_t *data; uint8_t *owned; size_t len;
  int64_t position; int64_t count; qa_q3_message_mode mode; char *source;
};
bool qa_q3_message_reader_init(qa_q3_message_reader **out, const uint8_t *data, size_t len, qa_q3_message_mode mode, const char *source) {
  const char *src = source ? source : "<message>";
  if (len > QA_Q3_MAX_MESSAGE_LENGTH) qa_binary_error(src, 0, "message exceeds maximum length");
  qa_q3_message_reader *r = calloc(1, sizeof *r);
  if (!r) return false;
  r->source = malloc(strlen(src) + 1);
  if (!r->source) { free(r); return false; }
  strcpy(r->source, src);
  r->data = data; r->len = len; r->mode = mode;
  *out = r; return true;
}
void qa_q3_message_reader_destroy(qa_q3_message_reader *r) { if (!r) return; free(r->owned); free(r->source); free(r); }
int64_t qa_q3_message_reader_bit_position(const qa_q3_message_reader *r) { return r->position; }
int64_t qa_q3_message_reader_read_count(const qa_q3_message_reader *r) { return r->count; }
qa_q3_message_mode qa_q3_message_reader_get_mode(const qa_q3_message_reader *r) { return r->mode; }
const char *qa_q3_message_reader_source(const qa_q3_message_reader *r) { return r->source; }
const uint8_t *qa_q3_message_reader_data(const qa_q3_message_reader *r, size_t *len) { *len = r->len; return r->data; }
void qa_q3_message_reader_bitstream(qa_q3_message_reader *r) { r->mode = QA_Q3_MESSAGE_BITSTREAM; }
void qa_q3_message_reader_begin_reading(qa_q3_message_reader *r) { r->position = 0; r->count = 0; r->mode = QA_Q3_MESSAGE_BITSTREAM; }
void qa_q3_message_reader_begin_reading_oob(qa_q3_message_reader *r) { r->position = 0; r->count = 0; r->mode = QA_Q3_MESSAGE_OOB; }
bool qa_q3_message_reader_copy(const qa_q3_message_reader *r, qa_q3_message_reader **out) {
  uint8_t *copy = malloc(r->len ? r->len : 1);
  if (!copy) return false;
  memcpy(copy, r->data, r->len);
  if (!qa_q3_message_reader_init(out, copy, r->len, r->mode, r->source)) { free(copy); return false; }
  (*out)->owned = copy; (*out)->position = r->position; (*out)->count = r->count;
  return true;
}
static int reader_get_bit(void *ctx) {
  qa_q3_message_reader *r = ctx;
  if (r->position >= (int64_t)r->len * 8) qa_binary_error(r->source, r->position >> 3, "truncated message bits");
  int bit = (r->data[r->position >> 3] >> (r->position & 7)) & 1;
  r->position++; return bit;
}
int64_t qa_q3_message_reader_read_bits(qa_q3_message_reader *r, int64_t bits) {
  int64_t width = (bits == 0 && r->mode == QA_Q3_MESSAGE_BITSTREAM) ? 0 : width_of(bits, r->mode);
  int64_t value = 0; int64_t sign_width = width;
  if (r->mode == QA_Q3_MESSAGE_OOB) {
    if (r->count + (width >> 3) > (int64_t)r->len) qa_binary_error(r->source, r->count, "truncated OOB integer");
    const uint8_t *p = r->data + r->count;
    if (width == 8) value = p[0];
    else if (width == 16) value = p[0] | (p[1] << 8);
    else value = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    r->count += width >> 3; r->position += width;
  } else {
    int64_t low = width & 7;
    for (int64_t i = 0; i < low; i++) value |= (int64_t)reader_get_bit(r) << i;
    const qa_q3_huffman_codec *codec = qa_q3_create_message_huffman();
    for (int64_t i = low; i < width; i += 8)
      value |= (int64_t)codec->decode_symbol(codec->ctx, reader_get_bit, r) << i;
    r->count = (r->position >> 3) + 1;
    sign_width -= low;
  }
  if (bits < 0 && sign_width > 0 && (value & ((int64_t)1 << (sign_width - 1))) != 0)
    value |= -1LL ^ (((int64_t)1 << sign_width) - 1);
  return (int32_t)value; /* TS numbers here are int32-valued via bitwise ops */
}
static int64_t read_scalar(qa_q3_message_reader *r, int64_t bits) {
  int64_t v = qa_q3_message_reader_read_bits(r, bits);
  return r->count > (int64_t)r->len ? -1 : v;
}
int64_t qa_q3_message_reader_read_byte(qa_q3_message_reader *r) { int64_t v = read_scalar(r, 8); return v == -1 ? -1 : (v & 255); }
int64_t qa_q3_message_reader_read_char(qa_q3_message_reader *r) { return (int8_t)read_scalar(r, 8); }
int64_t qa_q3_message_reader_read_short(qa_q3_message_reader *r) { return (int16_t)read_scalar(r, 16); }
int64_t qa_q3_message_reader_read_long(qa_q3_message_reader *r) { return read_scalar(r, 32); }
double qa_q3_message_reader_read_float(qa_q3_message_reader *r) {
  int64_t v = qa_q3_message_reader_read_bits(r, 32);
  if (r->count > (int64_t)r->len) return -1;
  return qa_bits_to_float32((uint32_t)v);
}
bool qa_q3_message_reader_read_data(qa_q3_message_reader *r, int64_t length, uint8_t **out) {
  if (length < 0 || length > QA_Q3_MAX_MESSAGE_LENGTH) qa_range_error("Invalid message data length");
  *out = malloc((size_t)(length ? length : 1));
  if (!*out) return false;
  for (int64_t i = 0; i < length; i++) (*out)[i] = (uint8_t)qa_q3_message_reader_read_byte(r);
  return true;
}
static bool read_text(qa_q3_message_reader *r, int64_t limit, bool newline, bool ascii, char **out) {
  char *buf = malloc((size_t)limit);
  if (!buf) return false;
  size_t n = 0;
  for (int64_t i = 0; i < limit - 1; i++) {
    int64_t c = qa_q3_message_reader_read_byte(r);
    if (c == -1 || c == 0 || (newline && c == 10)) break;
    buf[n++] = (char)(c == 37 || (ascii && c > 127) ? 46 : c);
  }
  buf[n] = 0; *out = buf; return true;
}
bool qa_q3_message_reader_read_string(qa_q3_message_reader *r, char **out) { return read_text(r, 1024, false, true, out); }
bool qa_q3_message_reader_read_big_string(qa_q3_message_reader *r, char **out) { return read_text(r, 8192, false, false, out); }
bool qa_q3_message_reader_read_string_line(qa_q3_message_reader *r, char **out) { return read_text(r, 1024, true, false, out); }
double qa_q3_message_reader_read_angle16(qa_q3_message_reader *r) { return qa_q3_message_reader_read_short(r) * (360.0 / 65536.0); }
int64_t qa_q3_message_reader_read_delta(qa_q3_message_reader *r, int64_t p, int64_t bits) {
  return qa_q3_message_reader_read_bits(r, 1) == 0 ? p : qa_q3_message_reader_read_bits(r, bits);
}
int64_t qa_q3_message_reader_read_delta_key(qa_q3_message_reader *r, int64_t key, int64_t p, int64_t bits) {
  if (qa_q3_message_reader_read_bits(r, 1) == 0) return p;
  int64_t v = qa_q3_message_reader_read_bits(r, bits);
  if (bits < 0 || bits >= 32) qa_range_error("Keyed delta mask index is outside kbitmask");
  return (int32_t)((uint32_t)v ^ ((uint32_t)key & (0xffffffffu >> (31 - bits))));
}
double qa_q3_message_reader_read_delta_float(qa_q3_message_reader *r, double p) {
  return qa_q3_message_reader_read_bits(r, 1) == 0 ? (float)p : qa_bits_to_float32((uint32_t)qa_q3_message_reader_read_bits(r, 32));
}
double qa_q3_message_reader_read_delta_key_float(qa_q3_message_reader *r, int64_t key, double p) {
  return qa_q3_message_reader_read_bits(r, 1) == 0 ? (float)p
    : qa_bits_to_float32((uint32_t)qa_q3_message_reader_read_bits(r, 32) ^ (uint32_t)key);
}
static const int COMMAND_WIDTHS[8] = {16,16,16,8,8,8,16,8};
static void cmd_fields(const qa_q3_wire_user_command *c, int32_t f[8]) {
  f[0]=c->angles[0]; f[1]=c->angles[1]; f[2]=c->angles[2]; f[3]=c->forwardmove;
  f[4]=c->rightmove; f[5]=c->upmove; f[6]=c->buttons; f[7]=c->weapon;
}
void qa_q3_write_delta_user_command(qa_q3_message_writer *w, const qa_q3_wire_user_command *from, const qa_q3_wire_user_command *to, bool has_key, int64_t key) {
  int32_t diff = (int32_t)((uint32_t)to->server_time - (uint32_t)from->server_time);
  qa_q3_message_writer_write_bits(w, diff < 256 ? 1 : 0, 1);
  qa_q3_message_writer_write_bits(w, diff < 256 ? diff : to->server_time, diff < 256 ? 8 : 32);
  int32_t prev[8], next[8]; cmd_fields(from, prev); cmd_fields(to, next);
  if (has_key) {
    bool changed = false;
    for (int i = 0; i < 8; i++) if (prev[i] != next[i]) changed = true;
    qa_q3_message_writer_write_bits(w, changed ? 1 : 0, 1);
    if (!changed) { if (w->source_state) qa_q3_source_message_state_add_oldsize(w->source_state, 7); return; }
  }
  for (int i = 0; i < 8; i++) {
    if (!has_key) qa_q3_message_writer_write_delta(w, prev[i], next[i], COMMAND_WIDTHS[i]);
    else qa_q3_message_writer_write_delta_key(w, (int32_t)((uint32_t)key ^ (uint32_t)to->server_time), prev[i], next[i], COMMAND_WIDTHS[i]);
  }
}
void qa_q3_read_delta_user_command(qa_q3_message_reader *r, const qa_q3_wire_user_command *from, bool has_key, int64_t key, qa_q3_wire_user_command *out) {
  int32_t server_time = qa_q3_message_reader_read_bits(r, 1)
    ? (int32_t)((uint32_t)from->server_time + (uint32_t)qa_q3_message_reader_read_bits(r, 8))
    : (int32_t)qa_q3_message_reader_read_bits(r, 32);
  *out = *from; out->server_time = server_time;
  if (has_key && qa_q3_message_reader_read_bits(r, 1) == 0) return;
  int32_t k = (int32_t)((uint32_t)key ^ (uint32_t)server_time);
  #define D(p, wd) (!has_key ? qa_q3_message_reader_read_delta(r, (p), (wd)) : qa_q3_message_reader_read_delta_key(r, k, (p), (wd)))
  out->angles[0] = (int32_t)D(from->angles[0], 16);
  out->angles[1] = (int32_t)D(from->angles[1], 16);
  out->angles[2] = (int32_t)D(from->angles[2], 16);
  out->forwardmove = (int8_t)D(from->forwardmove, 8);
  out->rightmove = (int8_t)D(from->rightmove, 8);
  out->upmove = (int8_t)D(from->upmove, 8);
  out->buttons = (int32_t)D(from->buttons, 16);
  out->weapon = (int32_t)(D(from->weapon, 8) & 255);
  #undef D
}
