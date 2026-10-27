/* Port of src/network/q1/codecs/qw28.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_CODECS_QW28_H
#define QA_NETWORK_Q1_CODECS_QW28_H
#include "qa/network/q1/codecs/codec.h"
#define QA_QW28_MAX_ENTITY_NUMBER 512
#define QA_QW28_MAX_PRECACHE 256
void qa_qw_no_netquake_op(double protocol, const char *op);
void qa_qw_write_angle(qa_size_buf *sb, double f);
void qa_qw_write_angle16(qa_size_buf *sb, double f);
double qa_qw_read_angle16(qa_message_reader *reader);
void qa_qw_write_delta_usercmd(qa_size_buf *buf, const qa_qw_usercmd *from, const qa_qw_usercmd *cmd);
void qa_qw_read_delta_usercmd(qa_message_reader *reader, const qa_qw_usercmd *from, qa_qw_usercmd *move);
void qa_create_qw28_codec(qa_message_reader *reader, qa_qw_protocol_codec *out);
#endif
