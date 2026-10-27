/* Port of src/network/q2/codecs/vanilla.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q2_CODECS_VANILLA_H
#define QA_NETWORK_Q2_CODECS_VANILLA_H

#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/message.h"

/* TS createVanillaContext(net_message) result. */
typedef struct qa_q2_vanilla_context {
  qa_q2_sizebuf *net_message;
  qa_q2_protocol_codec codec; /* TS VANILLA_CODEC */
  int32_t bitcounts[32];
} qa_q2_vanilla_context;

void qa_q2_vanilla_context_init(qa_q2_vanilla_context *ctx, qa_q2_sizebuf *net_message);

#endif
