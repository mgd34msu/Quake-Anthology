#include "server_private.h"
#include "save_fields.h"
#include "qa/network_q3_save.h"

#define Q3_SERVER_CHECKPOINT_MAX (16U * 1024U * 1024U)
#define Q3_SERVER_CHECKPOINT_TAG UINT32_C(0x31565351)

static bool continuation_valid(const qa_q3_server_peer *p, qa_error *error)
{
    int64_t pending = (int64_t)p->reliable.sequence - p->reliable.acknowledged;
    if (!p->channel || qa_q3_channel_role(p->channel) != QA_Q3_SERVER ||
        p->state.phase < QA_Q3_ZOMBIE || p->state.phase > QA_Q3_ACTIVE ||
        p->reliable.sequence < 0 || p->reliable.acknowledged < 0 || pending < 0 || pending > QA_Q3_RELIABLE + 1 ||
        p->state.reliable_sent < 0 || p->state.reliable_sent > p->reliable.sequence ||
        p->state.last_client_command < 0 || p->state.message_acknowledge < 0 || p->queue_count > 64) goto invalid;
    unsigned count = 0;
    const queued_message *last = NULL;
    for (const queued_message *q = p->queue_first; q; q = q->next) {
        if (++count > p->queue_count || q->size > QA_Q3_MESSAGE_BYTES ||
            !memchr(q->key_command, 0, sizeof(q->key_command))) goto invalid;
        last = q;
    }
    if (count != p->queue_count || last != p->queue_last) goto invalid;
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i) {
        const qa_q3_snapshot *s = &p->history[i].value;
        if (!s->valid) continue;
        if (s->message_number < 0 || ((uint32_t)s->message_number & (QA_Q3_PACKET_BACKUP - 1)) != i ||
            s->parse_entities_number > p->entity_number ||
            s->entity_count > p->entity_number - s->parse_entities_number) goto invalid;
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Q3 server continuation ownership or history"); return false;
}

bool qa_q3_server_peer_checkpoint(const qa_q3_server_peer *p, qa_buffer *out, qa_error *error)
{
    if (!p || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q3 server checkpoint owner"); return false;
    }
    if (!continuation_valid(p, error)) return false;
    qa_buffer bytes = {malloc(Q3_SERVER_CHECKPOINT_MAX), 0};
    if (!bytes.data) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q3 server checkpoint"); return false;
    }
    qa_net_writer w;
    qa_net_writer_init(&w, bytes.data, Q3_SERVER_CHECKPOINT_MAX, error);
    const qa_q3_server_state *s = &p->state;
    bool ok = qa_net_write_u32(&w, Q3_SERVER_CHECKPOINT_TAG) &&
        qa_net_write_u32(&w, p->product) && q3_save_address(&w, &p->remote) &&
        qa_net_write_i32(&w, p->challenge) && qa_q3_channel_checkpoint(p->channel, &w) &&
        qa_net_write_u32(&w, s->phase) && qa_net_write_i32(&w, s->last_client_command) &&
        qa_net_write_i32(&w, s->message_acknowledge) && qa_net_write_i32(&w, s->delta_message) &&
        qa_net_write_i32(&w, s->gamestate_message_number) && qa_net_write_i32(&w, s->reliable_sent) &&
        qa_net_write_u64(&w, (uint64_t)s->next_snapshot_time) && qa_net_write_u64(&w, (uint64_t)s->next_reliable_time) &&
        qa_net_write_u8(&w, s->pure_authentic) && qa_net_write_u8(&w, s->got_pure_command) &&
        qa_net_write_u8(&w, s->rate_delayed) && q3_save_usercmd(&w, &s->last_usercmd) &&
        qa_net_write_i32(&w, p->reliable.sequence) && qa_net_write_i32(&w, p->reliable.acknowledged);
    for (size_t i = 0; ok && i < QA_Q3_RELIABLE; ++i) ok = q3_save_text(&w, p->reliable.text[i], QA_Q3_COMMAND_CHARS);
    ok = ok && q3_save_text(&w, p->last_command, sizeof(p->last_command)) &&
        q3_save_gamestate(&w, &p->gamestate) && qa_net_write_u64(&w, p->entity_number);
    for (size_t i = 0; ok && i < QA_Q3_PACKET_BACKUP; ++i) ok = q3_save_snapshot(&w, &p->history[i]);
    ok = ok && qa_net_write_u32(&w, p->queue_count);
    for (const queued_message *q = p->queue_first; ok && q; q = q->next)
        ok = q3_save_text(&w, q->key_command, sizeof(q->key_command)) &&
            qa_net_write_u32(&w, (uint32_t)q->size) && qa_net_write_data(&w, q->data, q->size);
    if (!ok) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&w); *out = bytes; return true;
}

static int64_t read_time(qa_net_reader *r)
{
    uint64_t bits = qa_net_read_u64(r); int64_t value;
    memcpy(&value, &bits, sizeof(value)); return value;
}

bool qa_q3_server_peer_restore(qa_bytes bytes, qa_q3_identity identity,
    const qa_q3_server_hooks *hooks, qa_q3_server_peer **out, qa_error *error)
{
    if (!out || !bytes.data || !bytes.size || bytes.size > Q3_SERVER_CHECKPOINT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 server checkpoint record"); return false;
    }
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, error);
    uint32_t tag = qa_net_read_u32(&r);
    qa_q3_product product = (qa_q3_product)qa_net_read_u32(&r);
    if (tag != Q3_SERVER_CHECKPOINT_TAG ||
        (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA))
        return qa_net_reader_fail(&r, "Unsupported Q3 server checkpoint schema");
    qa_net_address remote = {0};
    if (!q3_restore_address(&r, &remote)) return false;
    int32_t challenge = qa_net_read_i32(&r);
    qa_q3_server_peer *p = NULL;
    if (r.failed || !qa_q3_server_peer_create(identity, product, &remote, challenge, 0, hooks, &p, error)) return false;
    qa_q3_channel *channel = NULL;
    if (!qa_q3_channel_restore(&r, &channel)) goto failure;
    qa_q3_channel_destroy(p->channel); p->channel = channel;
    qa_q3_server_state *s = &p->state;
    s->phase = (qa_q3_connection_phase)qa_net_read_u32(&r);
    s->last_client_command = qa_net_read_i32(&r); s->message_acknowledge = qa_net_read_i32(&r);
    s->delta_message = qa_net_read_i32(&r); s->gamestate_message_number = qa_net_read_i32(&r);
    s->reliable_sent = qa_net_read_i32(&r); s->next_snapshot_time = read_time(&r); s->next_reliable_time = read_time(&r);
    s->pure_authentic = q3_save_bool(&r); s->got_pure_command = q3_save_bool(&r); s->rate_delayed = q3_save_bool(&r);
    if (!q3_restore_usercmd(&r, &s->last_usercmd)) goto failure;
    p->reliable.sequence = qa_net_read_i32(&r); p->reliable.acknowledged = qa_net_read_i32(&r);
    for (size_t i = 0; i < QA_Q3_RELIABLE; ++i)
        if (!qa_net_read_string(&r, p->reliable.text[i], QA_Q3_COMMAND_CHARS)) goto failure;
    if (!qa_net_read_string(&r, p->last_command, sizeof(p->last_command)) ||
        !q3_restore_gamestate(&r, &p->gamestate)) goto failure;
    p->entity_number = qa_net_read_u64(&r);
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i)
        if (!q3_restore_snapshot(&r, &p->history[i], product)) goto failure;
    unsigned count = qa_net_read_u32(&r);
    if (count > 64) { qa_net_reader_fail(&r, "Invalid Q3 server continuation queue extent"); goto failure; }
    for (unsigned i = 0; !r.failed && i < count; ++i) {
        queued_message *q = calloc(1, sizeof(*q));
        if (!q) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring Q3 server message queue"); goto failure; }
        if (p->queue_last) p->queue_last->next = q; else p->queue_first = q;
        p->queue_last = q; ++p->queue_count;
        if (!qa_net_read_string(&r, q->key_command, sizeof(q->key_command))) goto failure;
        q->size = qa_net_read_u32(&r);
        if (q->size > QA_Q3_MESSAGE_BYTES) {
            qa_net_reader_fail(&r, "Invalid Q3 queued continuation message extent"); goto failure;
        }
        if (!qa_net_read_data(&r, q->data, q->size)) goto failure;
    }
    if (!qa_net_reader_finish(&r) || !continuation_valid(p, error)) goto failure;
    *out = p; return true;
failure:
    qa_q3_server_peer_destroy(p); return false;
}
