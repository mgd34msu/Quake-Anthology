/* Port of src/network/q1/codecs/wide.ts. `name` is borrowed.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_CODECS_WIDE_H
#define QA_NETWORK_Q1_CODECS_WIDE_H
#include "qa/network/q1/codecs/codec.h"
#define QA_WIDE_MAX_MSGLEN 64000
#define QA_WIDE_MAX_DATAGRAM 64000
#define QA_WIDE_MAX_PRECACHE 65536
void qa_make_wide_codec(qa_message_reader *reader, double protocol, const char *name,
                        double default_flags, bool is_rmq, qa_protocol_codec *out);
#endif
