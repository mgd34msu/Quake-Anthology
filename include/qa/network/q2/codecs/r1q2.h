/*
 * Port of src/network/q2/codecs/r1q2.ts. TS createR1Q2Codec(minor)
 * mints a codec sharing the context closure; the C context owns one
 * codec storage reconfigured per minor version (same observable state
 * sharing). GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_R1Q2_H
#define QA_NETWORK_Q2_CODECS_R1Q2_H

#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/codecs/vanilla.h"
#include "qa/network/q2/message.h"

/* TS createR1Context(net_message) result. */
typedef struct qa_q2_r1_context {
  qa_q2_sizebuf *net_message;
  qa_q2_vanilla_context vanilla;
  qa_q2_protocol_codec codec; /* TS R1Q2_CODEC / createR1Q2Codec result */
  double minor_version;
  int32_t pending_frame_extrabits;
  int32_t pending_frame_extraflags;
} qa_q2_r1_context;

void qa_q2_r1_context_init(qa_q2_r1_context *ctx, qa_q2_sizebuf *net_message);
/* TS setR1Q2FrameExtrabits. */
void qa_q2_r1_set_frame_extrabits(qa_q2_r1_context *ctx, int32_t extrabits);
/* TS createR1Q2Codec(minorVersion). */
qa_q2_protocol_codec *qa_q2_r1_create_codec(qa_q2_r1_context *ctx, double minor_version);

#endif
