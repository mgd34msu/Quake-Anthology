#include "qa/network_q1_peer_save.h"
#include "../q3/save_fields.h"
#include <stdlib.h>

static bool fail(qa_error *error, qa_status status, const char *message)
{
    qa_error_set(error, status, 0, "%s", message);
    return false;
}

static bool admission_valid(const qa_q1_peer_save_admission *admission, qa_error *error)
{
    if (!admission || !admission->transport || !qa_q1_profile_valid(admission->protocol, error) ||
        !qa_net_address_equal(&admission->remote, &admission->remote, true))
        return fail(error, QA_ERROR_ARGUMENT, "Q1 peer requires an admitted source dialect and transport endpoint");
    const qa_net_address *local = qa_net_transport_address(admission->transport);
    if (!local || !qa_net_address_equal(local, local, true) || local->kind != admission->remote.kind ||
        !admission->message_bytes)
        return fail(error, QA_ERROR_ARGUMENT, "Q1 peer transport or message admission differs");
    size_t wire_bytes;
    if (qa_q1_is_qw(admission->protocol)) {
        qa_q1_channel_side side = admission->channel.qw.side;
        if (admission->message_bytes > 65525 ||
            (side != QA_Q1_CHANNEL_CLIENT && side != QA_Q1_CHANNEL_SERVER))
            return fail(error, QA_ERROR_ARGUMENT, "Invalid admitted QuakeWorld channel policy");
        wire_bytes = admission->message_bytes + (side == QA_Q1_CHANNEL_CLIENT ? 10u : 8u);
    } else {
        size_t fragment = admission->channel.nq_fragment_bytes;
        if (!fragment || fragment > 65527 || fragment > admission->message_bytes ||
            admission->message_bytes > SIZE_MAX / 2)
            return fail(error, QA_ERROR_ARGUMENT, "Invalid admitted NetQuake channel policy");
        wire_bytes = (admission->message_bytes < 65527 ? admission->message_bytes : 65527) + 8;
    }
    return wire_bytes <= qa_net_transport_limit(admission->transport) ||
        fail(error, QA_ERROR_ARGUMENT, "Admitted Q1 transport cannot carry the native channel datagrams");
}

static bool peer_matches(const qa_q1_peer *peer, const qa_q1_peer_save_admission *admission)
{
    if (!peer || peer->transport != admission->transport ||
        !qa_net_address_equal(&peer->remote, &admission->remote, true)) return false;
    size_t message = 0;
    if (qa_q1_is_qw(admission->protocol)) {
        qa_q1_channel_side side; uint16_t qport;
        return peer->kind == QA_Q1_PEER_QUAKEWORLD &&
            qa_qw_channel_save_policy(peer->channel.qw, &message, &side, &qport) &&
            message == admission->message_bytes && side == admission->channel.qw.side &&
            qport == admission->channel.qw.qport;
    }
    size_t fragment = 0;
    return peer->kind == QA_Q1_PEER_NETQUAKE &&
        qa_nq_channel_save_policy(peer->channel.nq, &message, &fragment) &&
        message == admission->message_bytes && fragment == admission->channel.nq_fragment_bytes;
}

bool qa_q1_peer_checkpoint(const qa_q1_peer *peer, const qa_q1_peer_save_admission *admission,
    qa_buffer *out, qa_error *error)
{
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "Missing Q1 peer continuation output");
    if (!admission_valid(admission, error)) return false;
    if (!peer_matches(peer, admission))
        return fail(error, QA_ERROR_ARGUMENT, "Actual Q1 peer differs from its enclosing source admission");
    qa_buffer channel = {0};
    bool qw = qa_q1_is_qw(admission->protocol);
    if (!(qw ? qa_qw_channel_checkpoint(peer->channel.qw, &channel, error) :
        qa_nq_channel_checkpoint(peer->channel.nq, &channel, error))) return false;
    if (channel.size > SIZE_MAX - 384) {
        qa_buffer_free(&channel);
        return fail(error, QA_ERROR_MEMORY, "Q1 peer continuation extent exceeds memory");
    }
    size_t capacity = channel.size + 384;
    uint8_t *data = malloc(capacity);
    if (!data) { qa_buffer_free(&channel); return fail(error, QA_ERROR_MEMORY, "Encoding Q1 peer continuation"); }
    qa_net_writer writer; qa_net_writer_init(&writer, data, capacity, error);
    bool ok = qa_net_write_u32(&writer, UINT32_C(0x50514151)) &&
        qa_net_write_u32(&writer, admission->protocol.kind) &&
        qa_net_write_u32(&writer, admission->protocol.revision) && qa_net_write_u32(&writer, admission->protocol.flags) &&
        q3_save_address(&writer, qa_net_transport_address(peer->transport)) && q3_save_address(&writer, &peer->remote) &&
        qa_net_write_u64(&writer, admission->message_bytes);
    if (ok && qw) ok = qa_net_write_u32(&writer, admission->channel.qw.side) &&
        qa_net_write_u16(&writer, admission->channel.qw.qport);
    else if (ok) ok = qa_net_write_u64(&writer, admission->channel.nq_fragment_bytes);
    if (ok) ok = qa_net_write_u8(&writer, peer->reply_send_failed) && qa_net_write_u64(&writer, channel.size) &&
        qa_net_write_data(&writer, channel.data, channel.size);
    qa_buffer_free(&channel);
    if (!ok || writer.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&writer)};
    return true;
}

bool qa_q1_peer_restore_checkpoint(qa_bytes bytes, const qa_q1_peer_save_admission *admission,
    qa_q1_peer *out, qa_error *error)
{
    if (!out || out->transport || out->channel.nq || (bytes.size && !bytes.data))
        return fail(error, QA_ERROR_ARGUMENT, "Q1 peer restore requires an empty candidate output");
    if (!admission_valid(admission, error)) return false;
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    if (qa_net_read_u32(&reader) != UINT32_C(0x50514151) ||
        qa_net_read_u32(&reader) != (uint32_t)admission->protocol.kind ||
        qa_net_read_u32(&reader) != admission->protocol.revision || qa_net_read_u32(&reader) != admission->protocol.flags)
        return fail(error, QA_ERROR_FORMAT, "Q1 peer continuation source dialect differs");
    qa_net_address local = {0}, remote = {0};
    if (!q3_restore_address(&reader, &local) || !q3_restore_address(&reader, &remote) ||
        !qa_net_address_equal(&local, qa_net_transport_address(admission->transport), true) ||
        !qa_net_address_equal(&remote, &admission->remote, true) ||
        qa_net_read_u64(&reader) != admission->message_bytes)
        return fail(error, QA_ERROR_FORMAT, "Q1 peer continuation transport/remote endpoint differs");
    bool qw = qa_q1_is_qw(admission->protocol);
    if (qw) {
        if (qa_net_read_u32(&reader) != (uint32_t)admission->channel.qw.side ||
            qa_net_read_u16(&reader) != admission->channel.qw.qport)
            return fail(error, QA_ERROR_FORMAT, "Q1 peer continuation QW side/qport differs");
    } else if (qa_net_read_u64(&reader) != admission->channel.nq_fragment_bytes)
        return fail(error, QA_ERROR_FORMAT, "Q1 peer continuation NQ fragment policy differs");
    uint8_t send_failed = qa_net_read_u8(&reader); uint64_t size = qa_net_read_u64(&reader);
    qa_bytes channel = {0};
    if (reader.failed || send_failed > 1 || size > SIZE_MAX ||
        !qa_net_read_bytes(&reader, (size_t)size, &channel) || !qa_net_reader_finish(&reader))
        return fail(error, QA_ERROR_FORMAT, "Invalid Q1 peer nested continuation extent");
    qa_q1_peer peer = {.transport = admission->transport, .remote = remote,
        .kind = qw ? QA_Q1_PEER_QUAKEWORLD : QA_Q1_PEER_NETQUAKE, .reply_send_failed = send_failed != 0};
    if (!(qw ? qa_qw_channel_restore_checkpoint(channel, admission->channel.qw.side,
            admission->channel.qw.qport, admission->message_bytes, &peer.channel.qw, error) :
        qa_nq_channel_restore_checkpoint(channel, admission->message_bytes, admission->channel.nq_fragment_bytes,
            &peer.channel.nq, error))) return false;
    *out = peer;
    return true;
}
