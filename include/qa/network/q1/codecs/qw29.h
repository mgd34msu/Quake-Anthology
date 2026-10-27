/* Port of src/network/q1/codecs/qw29.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_CODECS_QW29_H
#define QA_NETWORK_Q1_CODECS_QW29_H
#include "qa/network/q1/codecs/codec.h"
#define QA_PROTOCOL_QW_WIDE 29
#define QA_QW29_DEFAULT_FLAGS (QA_Q1_PRFL_INT32COORD | QA_Q1_PRFL_SHORTANGLE)
#define QA_QW29_U_EXTEND (1 << 7)
#define QA_QW29_U_ENTITY2 (1 << 0)
#define QA_QW29_U_MODEL2 (1 << 1)
#define QA_QW29_U_FRAME2 (1 << 2)
#define QA_QW29_U_ALPHA (1 << 3)
#define QA_QW29_U_SCALE (1 << 4)
#define QA_QW29_MAX_ENTITY_NUMBER 65536
void qa_create_qw29_codec(qa_message_reader *reader, qa_qw_protocol_codec *out);
#endif
