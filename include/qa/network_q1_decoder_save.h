#ifndef QA_NETWORK_Q1_DECODER_SAVE_H
#define QA_NETWORK_Q1_DECODER_SAVE_H
#include "qa/network_q1_nq.h"
#include "qa/network_q1_qw.h"

/* Portable boundary over the original native decoder records. The actual
 * source connection supplies exact dialect and NQ gameplay options. Output
 * decoders must be empty; every returned allocation belongs to the caller.
 * Packet-borrowed list/name scratch is excluded at the enclosing idle cut. */
bool qa_nq_decoder_checkpoint(const qa_nq_decoder *, qa_net_protocol_id, qa_nq_options,
    qa_buffer *, qa_error *);
bool qa_nq_decoder_restore_checkpoint(qa_bytes, qa_net_protocol_id, qa_nq_options,
    qa_nq_decoder **, qa_error *);
bool qa_qw_decoder_checkpoint(const qa_qw_decoder *, qa_net_protocol_id, qa_buffer *, qa_error *);
bool qa_qw_decoder_restore_checkpoint(qa_bytes, qa_net_protocol_id, qa_qw_decoder **, qa_error *);
#endif
