/* Port of src/network/q2/codecs/clc_batch_move.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/clc_batch_move.h"

#include <stdarg.h>
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>

noreturn void qa_q2_batch_move_error(const char *format, ...) {
  va_list args;
  char text[1536];
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("ClcBatchMoveError", 0, 0, "%s", text);
}

void qa_q2_bit_reader_init(qa_q2_bit_reader *reader, qa_q2_sizebuf *msg) {
  reader->buf = 0;
  reader->left = 0;
  reader->msg = msg;
}

static void bit_fill(qa_q2_bit_reader *reader, int32_t bits) {
  while (reader->left < bits) {
    int32_t byte = qa_q2_msg_read_byte(reader->msg);
    if (byte < 0) {
      qa_q2_batch_move_error(
          "clc batch move: truncated message (bitreader ran past end of buffer)");
    }
    reader->buf |= byte << reader->left;
    reader->left += 8;
  }
}

int32_t qa_q2_bit_reader_read_unsigned(qa_q2_bit_reader *reader, int32_t bits) {
  bit_fill(reader, bits);
  int32_t value = reader->buf & ((1 << bits) - 1);
  reader->buf = (int32_t)((uint32_t)reader->buf >> bits);
  reader->left -= bits;
  return value;
}

int32_t qa_q2_bit_reader_read_signed(qa_q2_bit_reader *reader, int32_t bits) {
  int32_t value = qa_q2_bit_reader_read_unsigned(reader, bits);
  int32_t sign_bit = 1 << (bits - 1);
  return (value ^ sign_bit) - sign_bit;
}

int32_t qa_q2_read_batch_move_angle_component(qa_q2_bit_reader *br,
                                              int32_t prev_angle) {
  int32_t delta_flag = qa_q2_bit_reader_read_unsigned(br, 1);
  if (delta_flag != 0) {
    int32_t delta = qa_q2_bit_reader_read_signed(br, 8);
    return prev_angle + delta;
  }
  return qa_q2_bit_reader_read_signed(br, 16);
}

void qa_q2_batch_move_seed_from_prev(const qa_q2_wire_usercmd *prev_or_null,
                                     qa_q2_wire_usercmd *out) {
  qa_q2_wire_usercmd_init(out);
  if (prev_or_null != NULL) {
    out->angles[0] = prev_or_null->angles[0];
    out->angles[1] = prev_or_null->angles[1];
    out->angles[2] = prev_or_null->angles[2];
    out->forwardmove = prev_or_null->forwardmove;
    out->sidemove = prev_or_null->sidemove;
    out->upmove = prev_or_null->upmove;
    out->buttons = prev_or_null->buttons;
    out->impulse = prev_or_null->impulse;
    out->msec = prev_or_null->msec;
    out->lightlevel = prev_or_null->lightlevel;
  }
}

typedef struct frames_held {
  qa_q2_clc_batch_move_frame *frames;
  size_t filled;
} frames_held;
static void frames_release(void *arg) {
  frames_held *held = (frames_held *)arg;
  for (size_t k = 0; k < held->filled; k++) free(held->frames[k].cmds);
  free(held->frames);
}

bool qa_q2_read_batch_move_frames(qa_q2_bit_reader *br, int32_t num_dups,
                                  qa_q2_batch_decode_cmd_fn decode_cmd, void *ctx,
                                  qa_q2_clc_batch_move_frame **out_frames,
                                  size_t *out_count) {
  size_t count = num_dups >= 0 ? (size_t)num_dups + 1 : 0;
  frames_held held = {NULL, 0};
  held.frames = (qa_q2_clc_batch_move_frame *)calloc(count > 0 ? count : 1, sizeof *held.frames);
  if (held.frames == NULL) return false;
  /* A truncated message raises mid-frame; the frames read so far are released. */
  qa_cleanup node;
  qa_cleanup_push(&node, frames_release, &held);
  qa_q2_wire_usercmd prev_storage;
  const qa_q2_wire_usercmd *prev = NULL;
  for (size_t i = 0; i < count; i++) {
    int32_t num_cmds = qa_q2_bit_reader_read_unsigned(br, 5);
    qa_q2_wire_usercmd *cmds = (qa_q2_wire_usercmd *)calloc(
        num_cmds > 0 ? (size_t)num_cmds : 1, sizeof *cmds);
    if (cmds == NULL) {
      qa_cleanup_pop(&node);
      frames_release(&held);
      return false;
    }
    held.frames[i].cmds = cmds;
    held.frames[i].cmd_count = num_cmds > 0 ? (size_t)num_cmds : 0;
    held.filled = i + 1;
    for (int32_t j = 0; j < num_cmds; j++) {
      decode_cmd(ctx, br, prev, &cmds[j]);
      prev_storage = cmds[j];
      prev = &prev_storage;
    }
  }
  qa_cleanup_pop(&node);
  *out_frames = held.frames;
  *out_count = count;
  return true;
}

void qa_q2_bit_writer_init(qa_q2_bit_writer *writer, qa_q2_sizebuf *msg) {
  writer->buf = 0;
  writer->left = 0;
  writer->msg = msg;
}

void qa_q2_bit_writer_write_unsigned(qa_q2_bit_writer *writer, int32_t value,
                                     int32_t bits) {
  writer->buf |= (value & ((1 << bits) - 1)) << writer->left;
  writer->left += bits;
  while (writer->left >= 8) {
    qa_q2_msg_write_byte(writer->msg, (double)(writer->buf & 0xff));
    writer->buf = (int32_t)((uint32_t)writer->buf >> 8);
    writer->left -= 8;
  }
}

void qa_q2_bit_writer_write_signed(qa_q2_bit_writer *writer, int32_t value,
                                   int32_t bits) {
  qa_q2_bit_writer_write_unsigned(writer, value & ((1 << bits) - 1), bits);
}

void qa_q2_bit_writer_flush(qa_q2_bit_writer *writer) {
  if (writer->left > 0) {
    qa_q2_msg_write_byte(writer->msg, (double)(writer->buf & 0xff));
    writer->buf = 0;
    writer->left = 0;
  }
}

void qa_q2_write_batch_move_frames(qa_q2_bit_writer *bw,
                                   const qa_q2_clc_batch_move_frame *frames,
                                   size_t frame_count,
                                   qa_q2_batch_encode_cmd_fn encode_cmd,
                                   void *ctx) {
  qa_q2_wire_usercmd prev_storage;
  const qa_q2_wire_usercmd *prev = NULL;
  for (size_t i = 0; i < frame_count; i++) {
    qa_q2_bit_writer_write_unsigned(bw, (int32_t)frames[i].cmd_count, 5);
    for (size_t j = 0; j < frames[i].cmd_count; j++) {
      encode_cmd(ctx, bw, &frames[i].cmds[j], prev);
      prev_storage = frames[i].cmds[j];
      prev = &prev_storage;
    }
  }
  qa_q2_bit_writer_flush(bw);
}
