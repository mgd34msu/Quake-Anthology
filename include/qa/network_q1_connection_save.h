#ifndef QA_NETWORK_Q1_CONNECTION_SAVE_H
#define QA_NETWORK_Q1_CONNECTION_SAVE_H
#include "qa/network_q1_channel.h"

/* Actual connection negotiation owners at the enclosing caller's idle cut.
 * QW identity and challenge capacity/callback binding are admitted by the
 * source candidate. Constructors/decode do not send, issue challenges or call
 * random. Outputs must be empty and own all returned native allocations. */
bool qa_nq_connect_checkpoint(const qa_nq_connect_client *, qa_buffer *, qa_error *);
bool qa_nq_connect_restore_checkpoint(qa_bytes, qa_nq_connect_client **, qa_error *);
bool qa_qw_connect_checkpoint(const qa_qw_connect_client *, qa_buffer *, qa_error *);
bool qa_qw_connect_restore_checkpoint(qa_bytes, uint16_t qport, const char *userinfo,
    qa_qw_connect_client **, qa_error *);
bool qa_qw_challenges_checkpoint(const qa_qw_challenges *, qa_buffer *, qa_error *);
bool qa_qw_challenges_restore_checkpoint(qa_bytes, size_t capacity, qa_qw_random_fn,
    void *candidate_context, qa_qw_challenges **, qa_error *);
#endif
