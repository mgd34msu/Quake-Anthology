#include "q1_client_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { Q1_CLIENT_PARTS = 6 };
static qa_q1_peer_save_admission admission(const q1_runtime_client *c, const qa_net_client *client)
{
    qa_q1_peer_save_admission a = {.protocol = c->protocol, .transport = c->runtime->transport,
        .remote = client->endpoint, .message_bytes = c->policy.message_bytes};
    if (c->qw) { a.channel.qw.side = QA_Q1_CHANNEL_CLIENT; a.channel.qw.qport = c->policy.qport; }
    else a.channel.nq_fragment_bytes = c->policy.fragment_bytes;
    return a;
}
static bool valid(const q1_runtime_client *c, const qa_net_client *client, qa_error *e)
{
    bool qw = c->qw != NULL;
    if (!q1_client_retirement_valid(&c->retirement, c->retiring, qw,
            c->policy.message_bytes + 10, e)) return false;
    const qa_buffer *packet = &c->retirement.packet;
    if (packet->size) {
        if (qw) {
            if (packet->size < 10 || qa_load_u16le(packet->data + 8) != c->policy.qport ||
                (qa_load_u32le(packet->data) & INT32_MAX) + 1 !=
                    qa_qw_channel_get_stats(c->native.channel.qw).outgoing_sequence)
                return qa_network_fail(e, "QW CLIENT retirement packet differs from its actual channel cut");
        } else {
            uint32_t header = packet->size >= 8 ?
                (uint32_t)packet->data[0] << 24 | (uint32_t)packet->data[1] << 16 |
                (uint32_t)packet->data[2] << 8 | packet->data[3] : 0;
            uint32_t sequence = packet->size >= 8 ?
                (uint32_t)packet->data[4] << 24 | (uint32_t)packet->data[5] << 16 |
                (uint32_t)packet->data[6] << 8 | packet->data[7] : 0;
            if (packet->size != 9 || header != (QA_NQ_FLAG_UNRELIABLE | 9u) ||
                packet->data[8] != QA_Q1_CLC_DISCONNECT ||
                sequence + 1u != qa_nq_channel_unreliable_sequence(c->native.channel.nq))
                return qa_network_fail(e, "NQ CLIENT retirement packet differs from its actual channel cut");
        }
    }
    if (!client || c->busy || client->seat_count != 1 ||
        c->admitted_protocol.kind != client->protocol.kind || c->admitted_protocol.revision != client->protocol.revision ||
        c->admitted_protocol.flags != client->protocol.flags || !qa_q1_profile_valid(c->protocol, e) ||
        qa_q1_is_qw(c->protocol) != qw ||
        (c->held ? !qa_q1_profile_valid(c->before_protocol, e) || qa_q1_is_qw(c->before_protocol) != qw :
            c->before_protocol.kind || c->before_protocol.revision || c->before_protocol.flags) || c->signon.stage > 4 ||
        c->command_count > c->policy.pending_commands || c->queued_bytes > c->policy.queued_bytes ||
        c->record_count > c->policy.service_limit || c->cursor ||
        (c->held ? !c->before_decoder.size : c->record_count || c->cursor || c->payload.size || c->before_decoder.size) ||
        c->payload.size > c->policy.message_bytes || (!qw && (c->has_delta || c->waiting_skins)) ||
        (c->waiting_skins && (c->active || client->phase != QA_NET_PRIMED)) ||
        (qw && (c->sequence > INT32_MAX || c->acknowledged > INT32_MAX || c->last_frame > INT32_MAX)) ||
        (!c->retiring && (c->active != (client->phase == QA_NET_ACTIVE))) ||
        (!qw && !c->retiring && ((c->signon.stage == 4) != c->active ||
            client->phase != (c->signon.stage < 2 ? QA_NET_CONNECTED : c->signon.stage < 4 ? QA_NET_PRIMED : QA_NET_ACTIVE))))
        return qa_network_fail(e, "Q1 CLIENT continuation differs from its actual source phase or receiver");
    for (size_t i = 0; i < c->command_count; ++i) {
        const q1_client_move *m = &c->commands[i];
        for (size_t j = 0; j < 3; ++j)
            if (!isfinite(qw ? m->command.qw.angles[j] : m->command.nq.angles[j]))
                return qa_network_fail(e, "Q1 CLIENT queued command has a nonfinite source angle");
        if (!qw && !isfinite(m->command.nq.time)) return qa_network_fail(e, "NQ CLIENT queued source time is nonfinite");
    }
    return true;
}
static bool write_move(qa_net_writer *w, bool qw, const q1_client_move *m)
{
    if (!qw && !qa_net_write_f32(w, m->command.nq.time)) return false;
    const float *angles = qw ? m->command.qw.angles : m->command.nq.angles;
    for (size_t i = 0; i < 3; ++i) if (!qa_net_write_f32(w, angles[i])) return false;
    int16_t forward = qw ? m->command.qw.forward : m->command.nq.forward;
    int16_t side = qw ? m->command.qw.side : m->command.nq.side;
    int16_t up = qw ? m->command.qw.up : m->command.nq.up;
    return qa_net_write_i16(w, forward) && qa_net_write_i16(w, side) && qa_net_write_i16(w, up) &&
        (!qw || qa_net_write_u8(w, m->command.qw.msec)) &&
        qa_net_write_u8(w, qw ? m->command.qw.buttons : m->command.nq.buttons) &&
        qa_net_write_u8(w, qw ? m->command.qw.impulse : m->command.nq.impulse);
}
static bool read_move(qa_net_reader *r, bool qw, q1_client_move *m)
{
    *m = (q1_client_move){0};
    if (!qw) m->command.nq.time = qa_net_read_f32(r);
    float *angles = qw ? m->command.qw.angles : m->command.nq.angles;
    for (size_t i = 0; i < 3; ++i) {
        angles[i] = qa_net_read_f32(r);
        if (!isfinite(angles[i])) return qa_net_reader_fail(r, "Nonfinite Q1 CLIENT source command");
    }
    int16_t forward = qa_net_read_i16(r), side = qa_net_read_i16(r), up = qa_net_read_i16(r);
    if (qw) {
        m->command.qw.forward = forward; m->command.qw.side = side; m->command.qw.up = up;
        m->command.qw.msec = qa_net_read_u8(r); m->command.qw.buttons = qa_net_read_u8(r); m->command.qw.impulse = qa_net_read_u8(r);
    } else {
        m->command.nq.forward = forward; m->command.nq.side = side; m->command.nq.up = up;
        m->command.nq.buttons = qa_net_read_u8(r); m->command.nq.impulse = qa_net_read_u8(r);
        if (!isfinite(m->command.nq.time)) return qa_net_reader_fail(r, "Nonfinite NQ CLIENT source time");
    }
    return !r->failed;
}
static bool write_policy(qa_net_writer *w, const qa_network_q1_client_policy *p)
{
    return qa_net_write_u64(w,p->message_bytes) && qa_net_write_u64(w,p->fragment_bytes) &&
        qa_net_write_u64(w,p->queued_bytes) && qa_net_write_u64(w,p->service_limit) &&
        qa_net_write_u64(w,p->pending_commands) && qa_net_write_u16(w,p->qport) &&
        qa_net_write_u32(w,p->bytes_per_second) && qa_net_write_u8(w,p->nq_options.standard_quake) &&
        qa_net_write_u8(w,p->nq_options.private_rerelease);
}
static bool read_policy(qa_net_reader *r, const qa_network_q1_client_policy *p)
{
    if (qa_net_read_u64(r) != p->message_bytes || qa_net_read_u64(r) != p->fragment_bytes ||
        qa_net_read_u64(r) != p->queued_bytes || qa_net_read_u64(r) != p->service_limit ||
        qa_net_read_u64(r) != p->pending_commands || qa_net_read_u16(r) != p->qport ||
        qa_net_read_u32(r) != p->bytes_per_second || qa_net_read_u8(r) != p->nq_options.standard_quake ||
        qa_net_read_u8(r) != p->nq_options.private_rerelease)
        return qa_net_reader_fail(r, "Q1 CLIENT candidate source policy differs from its continuation");
    return !r->failed;
}
static bool blob_read(qa_net_reader *r, qa_bytes *bytes)
{
    uint64_t size = qa_net_read_u64(r);
    return !r->failed && size <= SIZE_MAX && qa_net_read_bytes(r, (size_t)size, bytes);
}
static bool write_protocol(qa_net_writer *w, qa_net_protocol_id p)
{ return qa_net_write_u32(w,p.kind) && qa_net_write_u32(w,p.revision) && qa_net_write_u32(w,p.flags); }
static qa_net_protocol_id read_protocol(qa_net_reader *r)
{
    qa_net_protocol_id p;
    p.kind=(qa_net_protocol)qa_net_read_u32(r); p.revision=qa_net_read_u32(r); p.flags=qa_net_read_u32(r);
    return p;
}
bool qa_network_q1_client_checkpoint_peer(const qa_network_peer *peer, qa_buffer *out, qa_error *e)
{
    if (!qa_network_q1_client_peer(peer) || !out) return qa_network_fail(e, "Missing Q1 CLIENT checkpoint owner/output");
    q1_runtime_client *c = peer->state;
    if (c->demo) return qa_network_fail(e, "Native demo playback has a live file cursor, not a CLIENT checkpoint");
    const qa_net_client *client = qa_net_connections_get(c->runtime->connections, c->id);
    if (!qa_network_callbacks_idle(c->runtime) || !valid(c, client, e)) return false;
    qa_buffer parts[Q1_CLIENT_PARTS] = {0};
    qa_q1_peer_save_admission a = admission(c, client); bool qw = c->qw != NULL;
    bool ok = qa_q1_peer_checkpoint(&c->native, &a, &parts[0], e) &&
        (qw ? qa_qw_decoder_checkpoint(c->qw,c->protocol,&parts[1],e) :
            qa_nq_decoder_checkpoint(c->nq,c->protocol,c->policy.nq_options,&parts[1],e)) &&
        (qw ? qa_qw_precache_checkpoint(c->precache,&parts[2],e) : qa_nq_signon_checkpoint(&c->signon,&parts[2],e));
    if (ok && qw) ok = qa_qw_history_checkpoint(&c->history,&parts[3],e);
    parts[4] = c->before_decoder; parts[5] = c->payload;
    size_t capacity = 256, queued = 0, queue_count = 0;
    for (q1_client_pending *p = c->first; ok && p; p = p->next) {
        if (!p->bytes.size || p->bytes.size > c->policy.message_bytes || p->bytes.size > c->policy.queued_bytes - queued ||
            capacity > SIZE_MAX - 8 || p->bytes.size > SIZE_MAX - capacity - 8) ok = qa_network_fail(e, "Invalid Q1 CLIENT retained reliable FIFO");
        else { queued += p->bytes.size; capacity += 8 + p->bytes.size; ++queue_count; }
    }
    if (ok && queued != c->queued_bytes) ok = qa_network_fail(e, "Q1 CLIENT FIFO extent differs from its owner");
    for (size_t i=0;ok && i<Q1_CLIENT_PARTS;++i) {
        if (capacity > SIZE_MAX - 8 || parts[i].size > SIZE_MAX - capacity - 8) ok = qa_network_fail(e, "Q1 CLIENT continuation extent overflows");
        else capacity += 8 + parts[i].size;
    }
    if (ok && c->command_count > (SIZE_MAX - capacity) / 24) ok = qa_network_fail(e, "Q1 CLIENT command extent overflows");
    if (ok) capacity += c->command_count * 24;
    if (ok) ok = q1_client_retirement_extent(&c->retirement, &capacity, e);
    qa_buffer bytes = {0}; qa_net_writer w;
    if (ok) {
        bytes.data = malloc(capacity);
        if (!bytes.data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding Q1 CLIENT continuation"); ok = false; }
        else qa_net_writer_init(&w,bytes.data,capacity,e);
    }
    if (ok) ok = qa_net_write_u32(&w,UINT32_C(0x4c314151)) &&
        write_policy(&w,&c->policy) && write_protocol(&w,c->protocol) && write_protocol(&w,c->before_protocol) &&
        qa_net_write_u8(&w,c->started) && qa_net_write_u8(&w,c->held) &&
        qa_net_write_u8(&w,c->active) && qa_net_write_u8(&w,c->retiring) && qa_net_write_u8(&w,c->has_delta) &&
        qa_net_write_u8(&w,c->waiting_skins) &&
        qa_net_write_u64(&w,c->received_ns) && qa_net_write_u32(&w,c->sequence) &&
        qa_net_write_u32(&w,c->acknowledged) && qa_net_write_u32(&w,c->moves) && qa_net_write_u32(&w,c->last_frame) &&
        qa_net_write_u64(&w,c->cursor) && qa_net_write_u64(&w,c->record_count) &&
        qa_net_write_u64(&w,queue_count) && qa_net_write_u64(&w,c->command_count);
    for (size_t i=0;ok && i<Q1_CLIENT_PARTS;++i) ok = qa_net_write_u64(&w,parts[i].size) && qa_net_write_data(&w,parts[i].data,parts[i].size);
    for (q1_client_pending *p=c->first;ok && p;p=p->next) ok = qa_net_write_u64(&w,p->bytes.size) && qa_net_write_data(&w,p->bytes.data,p->bytes.size);
    for (size_t i=0;ok && i<c->command_count;++i) ok = write_move(&w,qw,&c->commands[i]);
    if (ok) ok = q1_client_retirement_write(&c->retirement, &w);
    for (size_t i=0;i<4;++i) qa_buffer_free(&parts[i]);
    if (!ok) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&w); *out = bytes; return true;
}
bool qa_network_q1_client_restore_peer(qa_network_runtime *runtime, const qa_net_client *client, qa_bytes bytes,
    const qa_network_q1_client_policy *policy, const qa_network_q1_client_hooks *hooks,
    qa_network_peer *out, qa_error *e)
{
    if (!runtime || !client || !policy || !hooks || !out || out->state || !bytes.data)
        return qa_network_fail(e, "Q1 CLIENT restore requires actual empty candidate source admission");
    qa_net_reader r; qa_net_reader_init(&r,bytes,e);
    if (qa_net_read_u32(&r) != UINT32_C(0x4c314151) || !read_policy(&r,policy))
        return qa_net_reader_fail(&r,"Invalid Q1 CLIENT continuation schema or policy");
    qa_net_protocol_id protocol=read_protocol(&r), before=read_protocol(&r);
    uint8_t started=qa_net_read_u8(&r), held=qa_net_read_u8(&r), active=qa_net_read_u8(&r), retiring=qa_net_read_u8(&r), delta=qa_net_read_u8(&r);
    uint8_t waiting=qa_net_read_u8(&r);
    uint64_t received=qa_net_read_u64(&r); uint32_t sequence=qa_net_read_u32(&r), acknowledged=qa_net_read_u32(&r);
    uint32_t moves=qa_net_read_u32(&r), last=qa_net_read_u32(&r);
    uint64_t cursor=qa_net_read_u64(&r), count=qa_net_read_u64(&r), queued=qa_net_read_u64(&r), commands=qa_net_read_u64(&r);
    bool qw=qa_q1_is_qw(client->protocol);
    if (r.failed || !qa_q1_profile_valid(protocol,e) || qa_q1_is_qw(protocol)!=qw ||
        (held ? !qa_q1_profile_valid(before,e) || qa_q1_is_qw(before)!=qw : before.kind || before.revision || before.flags) ||
        started>1 || held>1 || active>1 || retiring>1 || delta>1 || waiting>1 ||
        cursor || count>policy->service_limit || queued>policy->queued_bytes || commands>policy->pending_commands ||
        received>runtime->now_ns || (held && received>client->received_ns))
        return qa_net_reader_fail(&r,"Invalid Q1 CLIENT retained source cursor or command extent");
    qa_bytes parts[Q1_CLIENT_PARTS];
    for (size_t i=0;i<Q1_CLIENT_PARTS;++i) if (!blob_read(&r,&parts[i])) return false;
    if ((!held && (parts[4].size || parts[5].size || cursor || count)) ||
        (held && (!parts[4].size || parts[5].size>policy->message_bytes)) || (!qw && parts[3].size))
        return qa_net_reader_fail(&r,"Q1 CLIENT retained batch ownership differs");
    qa_net_connect request={client->attachment,client->endpoint,client->protocol,client->seats,client->seat_count,client->composition};
    q1_runtime_client *c=NULL;
    if (!q1_client_create(runtime,&request,policy,hooks,&c,e)) return false;
    c->id=client->id; c->protocol=protocol;
    qa_q1_peer native={0}; qa_q1_peer_save_admission a=admission(c,client);
    bool ok=qa_q1_peer_restore_checkpoint(parts[0],&a,&native,e);
    if (ok) {
        if (qw) qa_qw_channel_destroy(c->native.channel.qw); else qa_nq_channel_destroy(c->native.channel.nq);
        c->native=native;
        if (qw) { qa_qw_decoder_destroy(c->qw); c->qw=NULL; }
        else { qa_nq_decoder_destroy(c->nq); c->nq=NULL; }
        qa_bytes decoder=held?parts[4]:parts[1];
        c->protocol=held?before:protocol;
        ok=qw?qa_qw_decoder_restore_checkpoint(decoder,c->protocol,&c->qw,e):
            qa_nq_decoder_restore_checkpoint(decoder,c->protocol,c->policy.nq_options,&c->nq,e);
    }
    if (ok && held) {
        ok=q1_client_decode_batch(c,parts[5],sequence,acknowledged,received,e) && c->record_count==count &&
            c->protocol.kind==protocol.kind && c->protocol.revision==protocol.revision && c->protocol.flags==protocol.flags;
        qa_buffer actual={0};
        if (ok) ok=(qw?qa_qw_decoder_checkpoint(c->qw,c->protocol,&actual,e):
            qa_nq_decoder_checkpoint(c->nq,c->protocol,c->policy.nq_options,&actual,e)) &&
            actual.size==parts[1].size && !memcmp(actual.data,parts[1].data,actual.size);
        qa_buffer_free(&actual);
        if (!ok && (!e || !e->code)) qa_network_fail(e,"Q1 CLIENT retained batch and decoder cut differ");
    }
    if (ok && qw) {
        qa_qw_precache_destroy(c->precache); c->precache=NULL;
        ok=qa_qw_precache_restore_checkpoint(parts[2],held?before:protocol,&c->precache,e) &&
            qa_qw_history_restore_checkpoint(parts[3],&c->history,e);
    } else if (ok) {
        qa_nq_signon signon={0}; ok=qa_nq_signon_restore_checkpoint(parts[2],&c->policy.nq_identity,&signon,e);
        if (ok) c->signon=signon;
    }
    for (uint64_t i=0;ok && i<queued;++i) {
        qa_bytes message; ok=blob_read(&r,&message) && message.size && q1_client_queue(c,message,e);
    }
    for (size_t i=0;ok && i<(size_t)commands;++i) ok=read_move(&r,qw,&c->commands[i]);
    if (ok) {
        c->command_count=(size_t)commands; c->cursor=(size_t)cursor;
        c->started=started!=0; c->active=active!=0; c->retiring=retiring!=0; c->has_delta=delta!=0;
        c->waiting_skins=waiting!=0;
        c->moves=moves; c->last_frame=last; c->sequence=sequence; c->acknowledged=acknowledged; c->received_ns=received;
        ok=q1_client_retirement_read(&c->retirement, &r, c->retiring, qw,
            c->policy.message_bytes + 10, e) && qa_net_reader_finish(&r) && valid(c,client,e);
    }
    if (!ok) { q1_client_close(c); return false; }
    *out=(qa_network_peer){.id=client->id,.ops=qa_network_q1_client_ops,.state=c}; return true;
}
