#ifndef QA_Q2_CHANNEL_INTERNAL_H
#define QA_Q2_CHANNEL_INTERNAL_H
#include "qa/network_q2.h"
struct qa_q2_channel {
    qa_q2_channel_options options;
    uint32_t incoming,outgoing,incoming_ack,last_reliable,receive_sequence;
    bool incoming_reliable,incoming_reliable_ack,reliable_bit,ack_pending;
    size_t capacity,payload_bytes,packet_bytes,queued_size,reliable_size,sending_size,sending_offset,receive_size;
    uint8_t *queued,*reliable,*sending,*receiving,*packet;
    bool sending_reliable,id_recording;
    uint64_t sent_ns,received_ns;
    qa_network_reliable_receipt receipt;
    uint64_t queued_serial,reliable_submitted;
};
#endif
