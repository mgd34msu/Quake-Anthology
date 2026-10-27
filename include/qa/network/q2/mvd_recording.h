/*
 * Port of src/network/q2/mvd-recording.ts. Callbacks are fn + ctx;
 * byte results are malloc'd (caller frees; NULL only on OOM apart
 * from the documented RangeError aborts). GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_MVD_RECORDING_H
#define QA_NETWORK_Q2_MVD_RECORDING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QA_Q2_MVD_MAX_MESSAGE 0x8000

/* TS mvdMagic() / frameMvdMessage(bytes). */
uint8_t *qa_q2_mvd_magic(size_t *out_length);
uint8_t *qa_q2_frame_mvd_message(const uint8_t *bytes, size_t length,
                                 size_t *out_length);

typedef void (*qa_q2_mvd_write_fn)(void *ctx, const uint8_t *bytes, size_t length);

/* TS MvdRecording. */
typedef struct qa_q2_mvd_recording {
  qa_q2_mvd_write_fn write;
  void *write_ctx;
  bool started;
  bool closed;
} qa_q2_mvd_recording;

void qa_q2_mvd_recording_init(qa_q2_mvd_recording *recording, qa_q2_mvd_write_fn write,
                              void *ctx);
void qa_q2_mvd_recording_append(qa_q2_mvd_recording *recording, const uint8_t *bytes,
                                size_t length);
void qa_q2_mvd_recording_close(qa_q2_mvd_recording *recording);

/* Return false to stop (TS callback returning false). */
typedef bool (*qa_q2_mvd_message_fn)(void *ctx, const uint8_t *bytes, size_t length);

/* TS MvdMessageFramer (pending is malloc'd). */
typedef struct qa_q2_mvd_message_framer {
  uint8_t *pending;
  size_t pending_length;
  bool magic_read;
  bool ended;
  double limit;
} qa_q2_mvd_message_framer;

/* TS constructor(magic=true, limit=MVD_MAX_MESSAGE). */
void qa_q2_mvd_framer_init(qa_q2_mvd_message_framer *framer, bool magic, double limit);
void qa_q2_mvd_framer_destroy(qa_q2_mvd_message_framer *framer);
bool qa_q2_mvd_framer_identified(const qa_q2_mvd_message_framer *framer);
bool qa_q2_mvd_framer_finished(const qa_q2_mvd_message_framer *framer);
/* TS takePending(): hands ownership to the caller. */
uint8_t *qa_q2_mvd_framer_take_pending(qa_q2_mvd_message_framer *framer,
                                       size_t *out_length);
void qa_q2_mvd_framer_push(qa_q2_mvd_message_framer *framer, const uint8_t *bytes,
                           size_t length, qa_q2_mvd_message_fn message, void *ctx);
/* TS finish(requireTerminator=true). */
void qa_q2_mvd_framer_finish(qa_q2_mvd_message_framer *framer,
                             bool require_terminator);

/* One message from readMvdRecording (each bytes block owned). */
typedef struct qa_q2_mvd_byte_block {
  uint8_t *bytes;
  size_t length;
} qa_q2_mvd_byte_block;

/* TS readMvdRecording generator, collected. False only on OOM. */
bool qa_q2_read_mvd_recording(const uint8_t *bytes, size_t length,
                              qa_q2_mvd_byte_block **out, size_t *out_count);
void qa_q2_mvd_byte_blocks_free(qa_q2_mvd_byte_block *blocks, size_t count);

#endif
