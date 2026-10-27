/* Port of src/network/q1/message.ts. PacketError aborts via
 * qa_q1_packet_error with "<message> at byte <offset>". Numeric writes
 * reproduce TS DataView truncation (ToIntegerOrInfinity + modulo) by
 * taking double. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_MESSAGE_H
#define QA_NETWORK_Q1_MESSAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>
noreturn void qa_q1_packet_error(const char *message, long long offset);
typedef struct qa_size_buf {
  uint8_t *data; long long maxsize; bool allow_overflow;
  long long cursize; bool overflowed;
} qa_size_buf;
int qa_size_buf_init(qa_size_buf *sb, long long maxsize, bool allow_overflow);
void qa_size_buf_destroy(qa_size_buf *sb);
long long qa_size_buf_reserve(qa_size_buf *sb, long long length);
void qa_size_buf_clear(qa_size_buf *sb);
uint8_t *qa_size_buf_bytes(const qa_size_buf *sb, size_t *out_len);
void qa_sz_write(qa_size_buf *sb, const uint8_t *data, long long data_len, long long length);
void qa_msg_write_byte(qa_size_buf *sb, double n);
void qa_msg_write_short(qa_size_buf *sb, double n);
void qa_msg_write_long(qa_size_buf *sb, double n);
void qa_msg_write_float(qa_size_buf *sb, double n);
void qa_msg_write_string(qa_size_buf *sb, const char *s); /* NULL = TS null */
void qa_msg_write_coord(qa_size_buf *sb, double n);
void qa_msg_write_angle(qa_size_buf *sb, double n);
void qa_msg_write_coord_flags(qa_size_buf *sb, double n, int64_t flags);
void qa_msg_write_angle_flags(qa_size_buf *sb, double n, int64_t flags);
void qa_msg_write_angle16(qa_size_buf *sb, double value, int64_t flags);

typedef struct qa_message_reader {
  const uint8_t *data; size_t length; long long offset; bool badread;
} qa_message_reader;
void qa_message_reader_init(qa_message_reader *r, const uint8_t *data, size_t length);
long long qa_message_reader_remaining(const qa_message_reader *r);
double qa_message_reader_byte(qa_message_reader *r);
double qa_message_reader_char(qa_message_reader *r);
double qa_message_reader_short(qa_message_reader *r);
double qa_message_reader_long(qa_message_reader *r);
double qa_message_reader_float(qa_message_reader *r);
/* Malloc'd string (bytes & 255, like TS fromCharCode for byte input). */
char *qa_message_reader_string(qa_message_reader *r, long long limit, bool quakeworld, size_t *out_len);
double qa_message_reader_coord(qa_message_reader *r);
double qa_message_reader_angle(qa_message_reader *r);
double qa_message_reader_coord_flags(qa_message_reader *r, int64_t flags);
double qa_message_reader_angle_flags(qa_message_reader *r, int64_t flags);
uint8_t *qa_message_reader_bytes(qa_message_reader *r, long long length, size_t *out_len);
void qa_message_reader_finish(const qa_message_reader *r);
double qa_read_move_angle16(qa_message_reader *r, int64_t flags);
#endif
