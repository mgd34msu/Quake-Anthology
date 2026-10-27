/* Port of src/network/q3/message.ts (msg.c). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_MESSAGE_H
#define QA_NETWORK_Q3_MESSAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_Q3_MAX_MESSAGE_LENGTH 16384
typedef enum qa_q3_message_mode { QA_Q3_MESSAGE_BITSTREAM, QA_Q3_MESSAGE_OOB } qa_q3_message_mode;
typedef void (*qa_q3_message_print_fn)(void *ctx, const char *text);
/* TS SourceMessageState: null counters -> has_* false. */
typedef struct qa_q3_source_message_state {
  qa_q3_message_print_fn print; void *print_ctx;
  int64_t oldsize, newsize, overflows;
  bool has_oldsize, has_newsize, has_overflows;
} qa_q3_source_message_state;
void qa_q3_source_message_state_init(qa_q3_source_message_state *s, qa_q3_message_print_fn print, void *ctx);
void qa_q3_source_message_state_add_oldsize(qa_q3_source_message_state *s, int64_t bits);
void qa_q3_source_message_state_add_newsize(qa_q3_source_message_state *s, int64_t bytes);
void qa_q3_source_message_state_check_overflow(qa_q3_source_message_state *s, int64_t value, int64_t bits);
void qa_q3_source_message_state_print(const qa_q3_source_message_state *s, const char *text);

typedef struct qa_q3_message_writer qa_q3_message_writer;
bool qa_q3_message_writer_init(qa_q3_message_writer **out, qa_q3_message_mode mode, int64_t capacity, qa_q3_source_message_state *source_state);
bool qa_q3_message_writer_init_default(qa_q3_message_writer **out);
void qa_q3_message_writer_destroy(qa_q3_message_writer *w);
int64_t qa_q3_message_writer_bit_position(const qa_q3_message_writer *w);
qa_q3_message_mode qa_q3_message_writer_get_mode(const qa_q3_message_writer *w);
int64_t qa_q3_message_writer_byte_length(const qa_q3_message_writer *w);
int64_t qa_q3_message_writer_capacity(const qa_q3_message_writer *w);
bool qa_q3_message_writer_overflowed(const qa_q3_message_writer *w);
qa_q3_source_message_state *qa_q3_message_writer_source_state(const qa_q3_message_writer *w);
bool qa_q3_message_writer_to_bytes(const qa_q3_message_writer *w, uint8_t **out, size_t *len);
void qa_q3_message_writer_clear(qa_q3_message_writer *w);
void qa_q3_message_writer_bitstream(qa_q3_message_writer *w);
bool qa_q3_message_writer_copy(const qa_q3_message_writer *w, qa_q3_message_writer **out);
void qa_q3_message_writer_write_bits(qa_q3_message_writer *w, int64_t value, int64_t bits);
void qa_q3_message_writer_write_bits_double(qa_q3_message_writer *w, double value, int64_t bits); /* integral check */
void qa_q3_message_writer_write_byte(qa_q3_message_writer *w, int64_t value);
void qa_q3_message_writer_write_char(qa_q3_message_writer *w, int64_t value);
void qa_q3_message_writer_write_short(qa_q3_message_writer *w, int64_t value);
void qa_q3_message_writer_write_long(qa_q3_message_writer *w, int64_t value);
void qa_q3_message_writer_write_float(qa_q3_message_writer *w, double value);
void qa_q3_message_writer_write_data(qa_q3_message_writer *w, const uint8_t *data, size_t len);
void qa_q3_message_writer_write_string(qa_q3_message_writer *w, const char *value); /* NULL -> single 0 */
void qa_q3_message_writer_write_big_string(qa_q3_message_writer *w, const char *value);
void qa_q3_message_writer_write_angle(qa_q3_message_writer *w, double value);
void qa_q3_message_writer_write_angle16(qa_q3_message_writer *w, double value);
void qa_q3_message_writer_write_delta(qa_q3_message_writer *w, int64_t previous, int64_t value, int64_t bits);
void qa_q3_message_writer_write_delta_key(qa_q3_message_writer *w, int64_t key, int64_t previous, int64_t value, int64_t bits);
void qa_q3_message_writer_write_delta_float(qa_q3_message_writer *w, double previous, double value);
void qa_q3_message_writer_write_delta_key_float(qa_q3_message_writer *w, int64_t key, double previous, double value);

typedef struct qa_q3_message_reader qa_q3_message_reader;
/* Borrows data (caller keeps alive); source copied. NULL source -> "<message>". */
bool qa_q3_message_reader_init(qa_q3_message_reader **out, const uint8_t *data, size_t len, qa_q3_message_mode mode, const char *source);
void qa_q3_message_reader_destroy(qa_q3_message_reader *r);
int64_t qa_q3_message_reader_bit_position(const qa_q3_message_reader *r);
int64_t qa_q3_message_reader_read_count(const qa_q3_message_reader *r);
qa_q3_message_mode qa_q3_message_reader_get_mode(const qa_q3_message_reader *r);
const char *qa_q3_message_reader_source(const qa_q3_message_reader *r);
const uint8_t *qa_q3_message_reader_data(const qa_q3_message_reader *r, size_t *len);
void qa_q3_message_reader_bitstream(qa_q3_message_reader *r);
void qa_q3_message_reader_begin_reading(qa_q3_message_reader *r);
void qa_q3_message_reader_begin_reading_oob(qa_q3_message_reader *r);
bool qa_q3_message_reader_copy(const qa_q3_message_reader *r, qa_q3_message_reader **out); /* copy owns data */
int64_t qa_q3_message_reader_read_bits(qa_q3_message_reader *r, int64_t bits);
int64_t qa_q3_message_reader_read_byte(qa_q3_message_reader *r);
int64_t qa_q3_message_reader_read_char(qa_q3_message_reader *r);
int64_t qa_q3_message_reader_read_short(qa_q3_message_reader *r);
int64_t qa_q3_message_reader_read_long(qa_q3_message_reader *r);
double qa_q3_message_reader_read_float(qa_q3_message_reader *r);
bool qa_q3_message_reader_read_data(qa_q3_message_reader *r, int64_t length, uint8_t **out);
bool qa_q3_message_reader_read_string(qa_q3_message_reader *r, char **out);
bool qa_q3_message_reader_read_big_string(qa_q3_message_reader *r, char **out);
bool qa_q3_message_reader_read_string_line(qa_q3_message_reader *r, char **out);
double qa_q3_message_reader_read_angle16(qa_q3_message_reader *r);
int64_t qa_q3_message_reader_read_delta(qa_q3_message_reader *r, int64_t previous, int64_t bits);
int64_t qa_q3_message_reader_read_delta_key(qa_q3_message_reader *r, int64_t key, int64_t previous, int64_t bits);
double qa_q3_message_reader_read_delta_float(qa_q3_message_reader *r, double previous);
double qa_q3_message_reader_read_delta_key_float(qa_q3_message_reader *r, int64_t key, double previous);

/* TS WireUserCommand. */
typedef struct qa_q3_wire_user_command {
  int32_t server_time;
  int32_t angles[3];
  int32_t forwardmove, rightmove, upmove, buttons, weapon;
} qa_q3_wire_user_command;
/* has_key false selects MSG_WriteDeltaUsercmd. */
void qa_q3_write_delta_user_command(qa_q3_message_writer *w, const qa_q3_wire_user_command *from, const qa_q3_wire_user_command *to, bool has_key, int64_t key);
void qa_q3_read_delta_user_command(qa_q3_message_reader *r, const qa_q3_wire_user_command *from, bool has_key, int64_t key, qa_q3_wire_user_command *out);
#endif
