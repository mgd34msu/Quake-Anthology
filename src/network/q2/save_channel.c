#include "channel_internal.h"
#include "qa/network_q2_wire_save.h"
#include <stdlib.h>
#include <string.h>

static bool invalid(qa_source_save_io *io, const char *message)
{ qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message); io->failed = true; return false; }
bool qa_q2_save_channel(qa_source_save_io *io, qa_q2_channel **owner)
{
    if (!io || !owner || (io->direction == QA_SOURCE_SAVE_READ ? *owner != NULL : *owner == NULL)) return false;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_q2_channel_options options = reading ? (qa_q2_channel_options){0} : (*owner)->options;
    if (!qa_q2_save_channel_options(io, &options)) return false;
    qa_q2_channel *channel = reading ? NULL : *owner;
    if (reading && !qa_q2_channel_create(&options, &channel, io->error)) { io->failed = true; return false; }
    bool ok = true;
#define U32(f) if (ok) ok = qa_source_save_u32(io, &channel->f)
#define U64(f) if (ok) ok = qa_source_save_u64(io, &channel->f)
#define BOOL(f) if (ok) ok = qa_source_save_bool(io, &channel->f)
#define EXTENT(f,n) if (ok) ok = qa_source_save_count(io, &channel->f, (n))
    size_t capacity = channel->capacity, payload = channel->payload_bytes, packet = channel->packet_bytes;
    if (ok) ok = qa_source_save_count(io, &capacity, 65527) && qa_source_save_count(io, &payload, 65527) &&
        qa_source_save_count(io, &packet, 65535);
    if (ok && (capacity != channel->capacity || payload != channel->payload_bytes || packet != channel->packet_bytes))
        ok = invalid(io, "Saved Q2 channel allocation policy differs");
    U32(incoming); U32(outgoing); U32(incoming_ack); U32(last_reliable); U32(receive_sequence);
    BOOL(incoming_reliable); BOOL(incoming_reliable_ack); BOOL(reliable_bit); BOOL(ack_pending);
    EXTENT(queued_size, channel->capacity); EXTENT(reliable_size, channel->capacity);
    size_t transfer_capacity = options.protocol.kind == QA_NET_Q2KEX_2023 ? 65527 : 32768;
    EXTENT(sending_size, transfer_capacity); EXTENT(sending_offset, channel->sending_size); EXTENT(receive_size, transfer_capacity);
    BOOL(sending_reliable); BOOL(id_recording); U64(sent_ns); U64(received_ns);
    bool expected_id = options.sequence_recording == QA_Q2_SEQUENCE_ID ||
        (options.sequence_recording == QA_Q2_SEQUENCE_DEFAULT && options.protocol.kind == QA_NET_Q2_34);
    if (ok && channel->id_recording != expected_id) ok = invalid(io, "Saved Q2 channel changes its sequence owner");
    if (ok) ok = qa_source_save_bytes(io, channel->queued, channel->queued_size) &&
        qa_source_save_bytes(io, channel->reliable, channel->reliable_size) &&
        qa_source_save_bytes(io, channel->sending, channel->sending_size) &&
        qa_source_save_bytes(io, channel->receiving, channel->receive_size);
    if (reading) {
        if (!ok) qa_q2_channel_destroy(channel);
        else {
            if (options.protocol.kind != QA_NET_Q2KEX_2023) {
                if (channel->reliable_size) channel->receipt.inflight = ++channel->receipt.queued;
                if (channel->queued_size) channel->queued_serial = ++channel->receipt.queued;
            }
            *owner = channel;
        }
    }
    return ok;
}
