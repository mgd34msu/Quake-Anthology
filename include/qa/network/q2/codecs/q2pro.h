/*
 * Port of src/network/q2/codecs/q2pro.ts. Like r1q2, the C context
 * owns one codec storage reconfigured by createQ2ProCodec.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_Q2PRO_H
#define QA_NETWORK_Q2_CODECS_Q2PRO_H

#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/codecs/q2pro_fields.h"
#include "qa/network/q2/message.h"

/* TS createQ2ProContext(net_message) result. */
typedef struct qa_q2pro_context {
  qa_q2_sizebuf *net_message;
  qa_q2pro_features features;
  qa_q2_protocol_codec codec; /* TS Q2PRO_CODEC / createQ2ProCodec */
  double minor_version;
  int32_t pending_opcode_extrabits;
  int32_t pending_frame_extraflags;
} qa_q2pro_context;

void qa_q2pro_context_init(qa_q2pro_context *ctx, qa_q2_sizebuf *net_message);
/* TS noteQ2ProFrameOpcodeExtrabits. */
void qa_q2pro_note_frame_opcode_extrabits(qa_q2pro_context *ctx, int32_t extrabits);
/* TS createQ2ProCodec(minorVersion). */
qa_q2_protocol_codec *qa_q2pro_create_codec(qa_q2pro_context *ctx, double minor_version);

#endif
