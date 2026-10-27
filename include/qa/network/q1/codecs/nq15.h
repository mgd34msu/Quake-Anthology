/* Port of src/network/q1/codecs/nq15.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_CODECS_NQ15_H
#define QA_NETWORK_Q1_CODECS_NQ15_H
#include "qa/network/q1/codecs/codec.h"
#define QA_NQ15_MAX_MSGLEN 8000
#define QA_NQ15_MAX_DATAGRAM 1024
#define QA_NQ15_MAX_PRECACHE 256
void qa_create_nq15_codec(qa_message_reader *reader, qa_protocol_codec *out);
#endif
