/*
 * Port of src/network/q2/codecs/clc_batch_move.ts. C accommodations:
 * BitReader/BitWriter are caller-allocated structs; the TS
 * ClcBatchMoveError throw becomes qa_q2_batch_move_error() (prints
 * "ClcBatchMoveError: ..." and aborts); decode/encode closures become
 * fn + ctx; read frames are malloc'd (free with the codec.h helper).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_CLC_BATCH_MOVE_H
#define QA_NETWORK_Q2_CODECS_CLC_BATCH_MOVE_H

#include <stdbool.h>
#include <stdnoreturn.h>

#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/state.h"

#define QA_Q2_MAX_CLC_BATCH_MOVE_FRAMES 4
#define QA_Q2_MAX_CLC_BATCH_MOVE_CMDS 32
/* TS CLC_Q2PRO_MOVE_NODELTA/BATCHED (same values as the ClcOpsT
 * entries; distinct C names avoid clashing with qa_q2_clc_op). */
#define QA_Q2_BATCH_MOVE_Q2PRO_NODELTA 10
#define QA_Q2_BATCH_MOVE_Q2PRO_BATCHED 11

typedef struct qa_q2_bit_writer qa_q2_bit_writer;

/* TS `throw new ClcBatchMoveError(message)`. */
noreturn void qa_q2_batch_move_error(const char *format, ...);

/* TS BitReader. */
typedef struct qa_q2_bit_reader {
  int32_t buf;
  int32_t left;
  qa_q2_sizebuf *msg;
} qa_q2_bit_reader;
void qa_q2_bit_reader_init(qa_q2_bit_reader *reader, qa_q2_sizebuf *msg);
int32_t qa_q2_bit_reader_read_unsigned(qa_q2_bit_reader *reader, int32_t bits);
int32_t qa_q2_bit_reader_read_signed(qa_q2_bit_reader *reader, int32_t bits);

/* TS readBatchMoveAngleComponent / seedFromPrev (prev NULL is TS null). */
int32_t qa_q2_read_batch_move_angle_component(qa_q2_bit_reader *br,
                                              int32_t prev_angle);
void qa_q2_batch_move_seed_from_prev(const qa_q2_wire_usercmd *prev_or_null,
                                     qa_q2_wire_usercmd *out);

typedef void (*qa_q2_batch_decode_cmd_fn)(void *ctx, qa_q2_bit_reader *br,
                                          const qa_q2_wire_usercmd *prev_or_null,
                                          qa_q2_wire_usercmd *out);
typedef void (*qa_q2_batch_encode_cmd_fn)(void *ctx, qa_q2_bit_writer *bw,
                                          const qa_q2_wire_usercmd *cmd,
                                          const qa_q2_wire_usercmd *prev_or_null);

/* TS readBatchMoveFrames. False only on OOM. */
bool qa_q2_read_batch_move_frames(qa_q2_bit_reader *br, int32_t num_dups,
                                  qa_q2_batch_decode_cmd_fn decode_cmd, void *ctx,
                                  qa_q2_clc_batch_move_frame **out_frames,
                                  size_t *out_count);

/* TS BitWriter. */
struct qa_q2_bit_writer {
  int32_t buf;
  int32_t left;
  qa_q2_sizebuf *msg;
};
void qa_q2_bit_writer_init(qa_q2_bit_writer *writer, qa_q2_sizebuf *msg);
void qa_q2_bit_writer_write_unsigned(qa_q2_bit_writer *writer, int32_t value,
                                     int32_t bits);
void qa_q2_bit_writer_write_signed(qa_q2_bit_writer *writer, int32_t value,
                                   int32_t bits);
void qa_q2_bit_writer_flush(qa_q2_bit_writer *writer);

/* TS writeBatchMoveFrames. */
void qa_q2_write_batch_move_frames(qa_q2_bit_writer *bw,
                                   const qa_q2_clc_batch_move_frame *frames,
                                   size_t frame_count,
                                   qa_q2_batch_encode_cmd_fn encode_cmd,
                                   void *ctx);

#endif
