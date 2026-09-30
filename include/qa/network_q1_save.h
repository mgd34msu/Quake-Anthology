#ifndef QA_NETWORK_Q1_SAVE_H
#define QA_NETWORK_Q1_SAVE_H
#include "qa/network_q1_channel.h"

/* Actual native channel continuation. Policy and endpoint role come from the
 * enclosing candidate connection admission. Borrowed wire/delivery scratch
 * is excluded. Constructors/decode do not send or replay packets. */
bool qa_nq_channel_checkpoint(const qa_nq_channel *, qa_buffer *, qa_error *);
bool qa_nq_channel_restore_checkpoint(qa_bytes, size_t message_bytes, size_t fragment_bytes,
    qa_nq_channel **, qa_error *);
bool qa_qw_channel_checkpoint(const qa_qw_channel *, qa_buffer *, qa_error *);
bool qa_qw_channel_restore_checkpoint(qa_bytes, qa_q1_channel_side, uint16_t qport,
    size_t message_bytes, qa_qw_channel **, qa_error *);
#endif
