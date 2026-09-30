#ifndef QA_NETWORK_Q1_PEER_SAVE_H
#define QA_NETWORK_Q1_PEER_SAVE_H
#include "qa/network_q1_save.h"

typedef struct qa_q1_peer_save_admission {
    qa_net_protocol_id protocol;
    qa_net_transport *transport;
    qa_net_address remote;
    size_t message_bytes;
    union {
        size_t nq_fragment_bytes;
        struct { qa_q1_channel_side side; uint16_t qport; } qw;
    } channel;
} qa_q1_peer_save_admission;

/* Pure observations of the actual native constructor policy. */
bool qa_nq_channel_save_policy(const qa_nq_channel *, size_t *message_bytes, size_t *fragment_bytes);
bool qa_qw_channel_save_policy(const qa_qw_channel *, size_t *message_bytes,
    qa_q1_channel_side *, uint16_t *qport);

/* Admission comes from the enclosing source connection, including its exact
 * dialect, retained transport endpoint and current remote endpoint. Restore
 * creates an owned native channel in an empty peer output, borrowing only the
 * admitted transport. The caller destroys that channel with its existing NQ
 * or QW destructor. No transport callback runs. The enclosing consumer must
 * separately qualify the live remote protocol cut before publication. */
bool qa_q1_peer_checkpoint(const qa_q1_peer *, const qa_q1_peer_save_admission *, qa_buffer *, qa_error *);
bool qa_q1_peer_restore_checkpoint(qa_bytes, const qa_q1_peer_save_admission *, qa_q1_peer *, qa_error *);
#endif
