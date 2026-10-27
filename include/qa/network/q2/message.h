/*
 * Port of src/network/q2/message.ts: SizeBuf and MSG_* primitives.
 * C accommodations:
 *  - SizeBuf.data/view become data + data_length; float access checks
 *    the same span the TS DataView would (RangeError past it).
 *  - The observer closure becomes fn + ctx.
 *  - MAXSIZE/cursize/readcount are int64 (TS numbers); SZ_Init takes
 *    both the byte span and the TS `length` (maxsize) argument.
 *  - Strings are NUL-terminated bytes; TS charCode & 0xff per UTF-16
 *    unit equals the byte value for Latin-1 input. MSG_ReadString
 *    writes at most out_capacity-1 chars plus NUL and returns the
 *    number of chars the TS string would hold (capped at 2047).
 *  - Integer write arguments are double (TS number) and narrowed with
 *    the same & / >> ToInt32 steps as TS.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_MESSAGE_H
#define QA_NETWORK_Q2_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/state.h"

typedef struct qa_q2_sizebuf qa_q2_sizebuf;
typedef void (*qa_q2_sizebuf_observer_fn)(void *ctx, qa_q2_sizebuf *buf,
                                          int64_t offset, int64_t length);

struct qa_q2_sizebuf {
  bool allow_overflow; /* TS allowoverflow */
  bool overflowed;
  qa_q2_sizebuf_observer_fn observer;
  void *observer_ctx;
  uint8_t *data;
  size_t data_length; /* TS data.byteLength / DataView span */
  int64_t maxsize;
  int64_t cursize;
  int64_t readcount;
  bool owns_data; /* createMessage allocation */
};

void qa_q2_sizebuf_init_empty(qa_q2_sizebuf *buf);
/* TS SZ_Init(buf, data, length). */
void qa_q2_sz_init(qa_q2_sizebuf *buf, uint8_t *data, size_t data_length,
                   int64_t length);
/* TS SZ_Clear / SZ_GetSpace / SZ_Write / SZ_Print. */
void qa_q2_sz_clear(qa_q2_sizebuf *buf);
int64_t qa_q2_sz_get_space(qa_q2_sizebuf *buf, int64_t length);
void qa_q2_sz_write(qa_q2_sizebuf *buf, const uint8_t *data, size_t data_length,
                    int64_t length);
void qa_q2_sz_print(qa_q2_sizebuf *buf, const char *text);

/* TS stringToBytes (exported): copies bytes, returns count. */
size_t qa_q2_string_to_bytes(const char *text, uint8_t *out, size_t out_capacity);

void qa_q2_msg_write_char(qa_q2_sizebuf *sb, double c);
void qa_q2_msg_write_byte(qa_q2_sizebuf *sb, double c);
void qa_q2_msg_write_short(qa_q2_sizebuf *sb, double c);
void qa_q2_msg_write_long(qa_q2_sizebuf *sb, double c);
void qa_q2_msg_write_float(qa_q2_sizebuf *sb, double f);
void qa_q2_msg_write_long64(qa_q2_sizebuf *sb, int64_t c);
/* NULL text is TS null (writes a single NUL byte). */
void qa_q2_msg_write_string(qa_q2_sizebuf *sb, const char *s);
void qa_q2_msg_write_coord(qa_q2_sizebuf *sb, double f);
void qa_q2_msg_write_pos(qa_q2_sizebuf *sb, const qa_q2_wire_vec3 pos);
void qa_q2_msg_write_angle(qa_q2_sizebuf *sb, double f);
void qa_q2_msg_write_angle16(qa_q2_sizebuf *sb, double f);
void qa_q2_msg_write_delta_usercmd(qa_q2_sizebuf *buf,
                                   const qa_q2_wire_usercmd *from,
                                   const qa_q2_wire_usercmd *cmd);
/* NULL dir is TS null. */
void qa_q2_msg_write_dir(qa_q2_sizebuf *sb, const qa_q2_wire_vec3 dir_or_null);
void qa_q2_msg_read_dir(qa_q2_sizebuf *sb, qa_q2_wire_vec3 dir);
void qa_q2_msg_write_delta_entity(const qa_q2_wire_entity *from,
                                  const qa_q2_wire_entity *to,
                                  qa_q2_sizebuf *msg, bool force,
                                  bool newentity);
void qa_q2_msg_begin_reading(qa_q2_sizebuf *msg);
int32_t qa_q2_msg_read_char(qa_q2_sizebuf *msg);
int32_t qa_q2_msg_read_byte(qa_q2_sizebuf *msg);
int32_t qa_q2_msg_read_short(qa_q2_sizebuf *msg);
int32_t qa_q2_msg_read_word(qa_q2_sizebuf *msg);
int32_t qa_q2_msg_read_long(qa_q2_sizebuf *msg);
double qa_q2_msg_read_float(qa_q2_sizebuf *msg);
int64_t qa_q2_msg_read_long64(qa_q2_sizebuf *msg);
size_t qa_q2_msg_read_string(qa_q2_sizebuf *msg, char *out, size_t out_capacity);
size_t qa_q2_msg_read_string_line(qa_q2_sizebuf *msg, char *out,
                                  size_t out_capacity);
double qa_q2_msg_read_coord(qa_q2_sizebuf *msg);
void qa_q2_msg_read_pos(qa_q2_sizebuf *msg, qa_q2_wire_vec3 pos);
double qa_q2_msg_read_angle(qa_q2_sizebuf *msg);
double qa_q2_msg_read_angle16(qa_q2_sizebuf *msg);
void qa_q2_msg_read_delta_usercmd(qa_q2_sizebuf *msg,
                                  const qa_q2_wire_usercmd *from,
                                  qa_q2_wire_usercmd *move);
void qa_q2_msg_read_data(qa_q2_sizebuf *msg, uint8_t *data, size_t data_length,
                         int64_t len);

/* TS createMessage(capacity=32768). False only on OOM. */
bool qa_q2_message_create(qa_q2_sizebuf *out, int64_t capacity);
void qa_q2_message_destroy(qa_q2_sizebuf *message);
/* TS loadMessage / messageBytes / checkMessageRead. messageBytes
 * returns a malloc'd copy (caller frees); NULL only on OOM. */
void qa_q2_message_load(qa_q2_sizebuf *message, uint8_t *bytes, size_t length);
uint8_t *qa_q2_message_bytes(const qa_q2_sizebuf *message, size_t *out_length);
void qa_q2_message_check_read(const qa_q2_sizebuf *message);

#endif
