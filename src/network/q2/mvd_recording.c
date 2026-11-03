/* Port of src/network/q2/mvd-recording.ts. GPL-2.0-or-later. */
#include "qa/network/q2/mvd_recording.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/constants.h"

/* MVD_MAGIC 0x3244564d little-endian ("MVD2"). */
static const uint8_t MAGIC_BYTES[4] = {0x4d, 0x56, 0x44, 0x32};
#define MVD_MAGIC_U32 0x3244564du

uint8_t *qa_q2_mvd_magic(size_t *out_length) {
  uint8_t *out = (uint8_t *)malloc(4);
  if (out == NULL) return NULL;
  memcpy(out, MAGIC_BYTES, 4);
  if (out_length != NULL) *out_length = 4;
  return out;
}

uint8_t *qa_q2_frame_mvd_message(const uint8_t *bytes, size_t length,
                                 size_t *out_length) {
  if (length < 1 || length > QA_Q2_MVD_MAX_MESSAGE) {
    qa_range_error("MVD message length outside 1..32768");
  }
  uint8_t *result = (uint8_t *)malloc(length + 2);
  if (result == NULL) return NULL;
  result[0] = (uint8_t)(length & 0xffu);
  result[1] = (uint8_t)((length >> 8) & 0xffu);
  memcpy(result + 2, bytes, length);
  if (out_length != NULL) *out_length = length + 2;
  return result;
}

void qa_q2_mvd_recording_init(qa_q2_mvd_recording *recording, qa_q2_mvd_write_fn write,
                              void *ctx) {
  recording->write = write;
  recording->write_ctx = ctx;
  recording->started = false;
  recording->closed = false;
}

void qa_q2_mvd_recording_append(qa_q2_mvd_recording *recording, const uint8_t *bytes,
                                size_t length) {
  if (recording->closed) qa_q2_throw_error("MVD recording is closed");
  size_t framed_len = 0;
  uint8_t *framed = qa_q2_frame_mvd_message(bytes, length, &framed_len);
  if (framed == NULL) qa_range_error("out of memory");
  if (!recording->started) {
    size_t magic_len = 0;
    uint8_t *magic = qa_q2_mvd_magic(&magic_len);
    if (magic == NULL) {
      free(framed);
      qa_range_error("out of memory");
    }
    recording->write(recording->write_ctx, magic, magic_len);
    free(magic);
    recording->started = true;
  }
  recording->write(recording->write_ctx, framed, framed_len);
  free(framed);
}

void qa_q2_mvd_recording_close(qa_q2_mvd_recording *recording) {
  if (recording->closed) return;
  if (!recording->started) {
    size_t magic_len = 0;
    uint8_t *magic = qa_q2_mvd_magic(&magic_len);
    if (magic == NULL) qa_range_error("out of memory");
    recording->write(recording->write_ctx, magic, magic_len);
    free(magic);
    recording->started = true;
  }
  uint8_t terminator[2] = {0, 0};
  recording->write(recording->write_ctx, terminator, 2);
  recording->closed = true;
}

void qa_q2_mvd_framer_init(qa_q2_mvd_message_framer *framer, bool magic, double limit) {
  if (limit != trunc(limit) || limit < 1 || limit > QA_Q2_MVD_MAX_MESSAGE) {
    qa_range_error("Invalid MVD/GTV frame limit");
  }
  framer->pending = NULL;
  framer->pending_length = 0;
  framer->magic_read = !magic;
  framer->ended = false;
  framer->limit = limit;
}

void qa_q2_mvd_framer_destroy(qa_q2_mvd_message_framer *framer) {
  free(framer->pending);
  framer->pending = NULL;
  framer->pending_length = 0;
}

bool qa_q2_mvd_framer_identified(const qa_q2_mvd_message_framer *framer) {
  return framer->magic_read;
}
bool qa_q2_mvd_framer_finished(const qa_q2_mvd_message_framer *framer) {
  return framer->ended;
}

uint8_t *qa_q2_mvd_framer_take_pending(qa_q2_mvd_message_framer *framer,
                                       size_t *out_length) {
  uint8_t *bytes = framer->pending;
  if (out_length != NULL) *out_length = framer->pending_length;
  framer->pending = NULL;
  framer->pending_length = 0;
  return bytes;
}

static uint16_t load_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint32_t)p[1] << 8));
}
static uint32_t load_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

void qa_q2_mvd_framer_push(qa_q2_mvd_message_framer *framer, const uint8_t *bytes,
                           size_t length, qa_q2_mvd_message_fn message, void *ctx) {
  if (framer->ended && length != 0) qa_q2_throw_error("Data after MVD terminator");
  size_t offset = 0;
  while (offset < length) {
    size_t need;
    if (!framer->magic_read) need = 4;
    else if (framer->pending_length < 2) need = 2;
    else need = (size_t)load_u16(framer->pending) + 2;
    size_t remaining_need = need > framer->pending_length ? need - framer->pending_length : 0;
    size_t count = remaining_need < length - offset ? remaining_need : length - offset;
    uint8_t *joined = (uint8_t *)malloc(framer->pending_length + count > 0
                                            ? framer->pending_length + count
                                            : 1);
    if (joined == NULL) qa_range_error("out of memory");
    if (framer->pending_length > 0) memcpy(joined, framer->pending, framer->pending_length);
    if (count > 0) memcpy(joined + framer->pending_length, bytes + offset, count);
    size_t joined_len = framer->pending_length + count;
    free(framer->pending);
    framer->pending = joined;
    framer->pending_length = joined_len;
    offset += count;
    if (joined_len < need) break;
    if (!framer->magic_read) {
      if (load_u32(framer->pending) != MVD_MAGIC_U32) {
        qa_q2_throw_error("Not an MVD/GTV stream");
      }
      framer->magic_read = true;
      free(framer->pending);
      framer->pending = NULL;
      framer->pending_length = 0;
      continue;
    }
    uint16_t mlen = load_u16(framer->pending);
    if ((double)mlen > framer->limit) qa_range_error("Oversize MVD/GTV message");
    if (mlen == 0) {
      framer->ended = true;
      free(framer->pending);
      framer->pending = NULL;
      framer->pending_length = 0;
      if (offset != length) qa_q2_throw_error("Data after MVD terminator");
      break;
    }
    if (joined_len < (size_t)mlen + 2) continue;
    uint8_t *payload = framer->pending;
    framer->pending = NULL;
    framer->pending_length = 0;
    qa_cleanup node;
    qa_cleanup_push(&node, free, payload); /* a raising consumer abandons the message */
    bool keep = message(ctx, payload + 2, (size_t)mlen);
    qa_cleanup_pop(&node);
    free(payload);
    if (!keep) {
      size_t rest = length - offset;
      uint8_t *tail = (uint8_t *)malloc(rest > 0 ? rest : 1);
      if (tail == NULL) qa_range_error("out of memory");
      if (rest > 0) memcpy(tail, bytes + offset, rest);
      framer->pending = tail;
      framer->pending_length = rest;
      return;
    }
  }
}

void qa_q2_mvd_framer_finish(qa_q2_mvd_message_framer *framer,
                             bool require_terminator) {
  if (!framer->magic_read || framer->pending_length != 0 ||
      (require_terminator && !framer->ended)) {
    qa_q2_throw_error("Truncated MVD/GTV stream");
  }
}

typedef struct collect_ctx {
  qa_q2_mvd_byte_block *blocks;
  size_t count;
  size_t capacity;
  bool oom;
} collect_ctx;

static bool collect_message(void *vctx, const uint8_t *bytes, size_t length) {
  collect_ctx *c = (collect_ctx *)vctx;
  if (c->count == c->capacity) {
    size_t next = c->capacity > 0 ? c->capacity * 2 : 16;
    qa_q2_mvd_byte_block *grown =
        (qa_q2_mvd_byte_block *)realloc(c->blocks, next * sizeof *c->blocks);
    if (grown == NULL) {
      c->oom = true;
      return false;
    }
    c->blocks = grown;
    c->capacity = next;
  }
  uint8_t *copy = (uint8_t *)malloc(length > 0 ? length : 1);
  if (copy == NULL) {
    c->oom = true;
    return false;
  }
  if (length > 0) memcpy(copy, bytes, length);
  c->blocks[c->count].bytes = copy;
  c->blocks[c->count].length = length;
  c->count++;
  return true;
}

void qa_q2_mvd_byte_blocks_free(qa_q2_mvd_byte_block *blocks, size_t count) {
  if (blocks != NULL) {
    for (size_t i = 0; i < count; i++) free(blocks[i].bytes);
    free(blocks);
  }
}

typedef struct recording_held {
  qa_q2_mvd_message_framer *framer;
  collect_ctx *collected;
} recording_held;
static void recording_release(void *arg) {
  recording_held *held = (recording_held *)arg;
  qa_q2_mvd_framer_destroy(held->framer);
  qa_q2_mvd_byte_blocks_free(held->collected->blocks, held->collected->count);
}

bool qa_q2_read_mvd_recording(const uint8_t *bytes, size_t length,
                              qa_q2_mvd_byte_block **out, size_t *out_count) {
  qa_q2_mvd_message_framer framer;
  qa_q2_mvd_framer_init(&framer, true, QA_Q2_MVD_MAX_MESSAGE);
  collect_ctx c = {NULL, 0, 0, false};
  /* A truncated or malformed recording raises; what was collected is released. */
  recording_held held = {&framer, &c};
  qa_cleanup node;
  qa_cleanup_push(&node, recording_release, &held);
  qa_q2_mvd_framer_push(&framer, bytes, length, collect_message, &c);
  if (!c.oom) qa_q2_mvd_framer_finish(&framer, true);
  qa_cleanup_pop(&node);
  qa_q2_mvd_framer_destroy(&framer);
  if (c.oom) {
    qa_q2_mvd_byte_blocks_free(c.blocks, c.count);
    return false;
  }
  *out = c.blocks;
  *out_count = c.count;
  return true;
}
