#include "client_private.h"
#include "save_fields.h"
#include "qa/network_q3_save.h"

#define Q3_CLIENT_CHECKPOINT_MAX (16U * 1024U * 1024U)
#define Q3_CLIENT_CHECKPOINT_TAG UINT32_C(0x314c4351)

static bool continuation_valid(const qa_q3_client_peer *p, qa_error *error)
{
    int64_t pending = (int64_t)p->reliable.sequence - p->reliable.acknowledged;
    if ((!p->demo && (!p->channel || qa_q3_channel_role(p->channel) != QA_Q3_CLIENT ||
        pending < 0 || pending > QA_Q3_RELIABLE + 1)) ||
        (p->demo && p->channel) || p->reliable.sequence < 0 || p->reliable.acknowledged < 0 ||
        p->server_message_sequence < 0 || p->server_command_sequence < 0 ||
        p->last_executed_server_command < 0 || p->latest_snapshot < 0 ||
        (p->has_snapshot && p->latest_snapshot > p->server_message_sequence) ||
        p->disconnect_packets > 3 || (!p->disconnect_started && p->disconnect_packets) ||
        (p->disconnect_packets == 3 && !p->disconnected) ||
        (p->demo && (p->disconnect_started || p->disconnect_packets || p->transmit_size)) ||
        p->transmit_size > sizeof(p->transmit_packet) ||
        (p->disconnect_started && (p->reliable.sequence <= 0 ||
            strcmp(qa_q3_reliable_lookup(&p->reliable, p->reliable.sequence), "disconnect")))) goto invalid;
    if (p->transmit_size && !qa_q3_channel_transmit_matches(p->channel,
        (qa_bytes){p->transmit_packet, p->transmit_size}, error)) return false;
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i) {
        const qa_q3_snapshot *s = &p->history[i].value;
        if (p->packets[i].command_number > p->command_number) goto invalid;
        if (!s->valid) continue;
        if (!p->has_snapshot || s->message_number < 0 ||
            ((uint32_t)s->message_number & (QA_Q3_PACKET_BACKUP - 1)) != i ||
            s->message_number > p->latest_snapshot || s->parse_entities_number > p->parse_entities_number ||
            s->entity_count > p->parse_entities_number - s->parse_entities_number) goto invalid;
    }
    if (p->has_snapshot) {
        const qa_q3_snapshot *s = &p->history[(uint32_t)p->latest_snapshot & (QA_Q3_PACKET_BACKUP - 1)].value;
        if (!s->valid || s->message_number != p->latest_snapshot) goto invalid;
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Q3 client continuation ownership or history");
    return false;
}

bool qa_q3_client_peer_checkpoint(const qa_q3_client_peer *p, qa_buffer *out, qa_error *error)
{
    if (!p || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q3 client checkpoint owner"); return false;
    }
    if (!continuation_valid(p, error)) return false;
    qa_buffer bytes = {malloc(Q3_CLIENT_CHECKPOINT_MAX), 0};
    if (!bytes.data) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q3 client checkpoint"); return false;
    }
    qa_net_writer w;
    qa_net_writer_init(&w, bytes.data, Q3_CLIENT_CHECKPOINT_MAX, error);
    bool ok = qa_net_write_u32(&w, Q3_CLIENT_CHECKPOINT_TAG) && qa_net_write_u32(&w, 2) &&
        qa_net_write_u32(&w, p->product) && qa_net_write_u8(&w, p->demo) &&
        q3_save_address(&w, &p->remote) && qa_net_write_i32(&w, p->challenge) &&
        (p->demo || qa_q3_channel_checkpoint(p->channel, &w)) &&
        qa_net_write_i32(&w, p->reliable.sequence) && qa_net_write_i32(&w, p->reliable.acknowledged);
    for (size_t i = 0; ok && i < QA_Q3_RELIABLE; ++i)
        ok = q3_save_text(&w, p->reliable.text[i], QA_Q3_COMMAND_CHARS) &&
            q3_save_text(&w, p->server_commands[i], QA_Q3_COMMAND_CHARS);
    ok = ok && qa_net_write_i32(&w, p->server_message_sequence) &&
        qa_net_write_i32(&w, p->server_command_sequence) && qa_net_write_i32(&w, p->last_executed_server_command) &&
        qa_net_write_i32(&w, p->server_id) && qa_net_write_i32(&w, p->last_packet_sent_time) &&
        qa_net_write_i32(&w, p->receive_time) && qa_net_write_u8(&w, p->demo_waiting) &&
        qa_net_write_u8(&w, p->disconnected) && qa_net_write_u8(&w, p->disconnect_started) &&
        qa_net_write_u8(&w, p->disconnect_packets) && qa_net_write_u16(&w, p->transmit_size) &&
        qa_net_write_data(&w, p->transmit_packet, p->transmit_size) && q3_save_gamestate(&w, &p->gamestate) &&
        qa_net_write_i32(&w, p->latest_snapshot) && qa_net_write_u8(&w, p->has_snapshot) &&
        qa_net_write_u64(&w, p->parse_entities_number) && qa_net_write_u64(&w, p->command_number);
    for (size_t i = 0; ok && i < QA_Q3_PACKET_BACKUP; ++i) ok = q3_save_snapshot(&w, &p->history[i]);
    for (size_t i = 0; ok && i < 64; ++i) ok = q3_save_usercmd(&w, &p->commands[i]);
    for (size_t i = 0; ok && i < QA_Q3_PACKET_BACKUP; ++i)
        ok = qa_net_write_u64(&w, p->packets[i].command_number) &&
            qa_net_write_i32(&w, p->packets[i].server_time) && qa_net_write_i32(&w, p->packets[i].real_time);
    ok = ok && q3_save_text(&w, p->big_configstring, sizeof(p->big_configstring));
    if (!ok) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&w); *out = bytes; return true;
}

bool qa_q3_client_peer_restore(qa_bytes bytes, qa_q3_identity identity,
    const qa_q3_client_hooks *hooks, qa_q3_client_peer **out, qa_error *error)
{
    if (!out || !bytes.data || !bytes.size || bytes.size > Q3_CLIENT_CHECKPOINT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 client checkpoint record"); return false;
    }
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, error);
    uint32_t tag = qa_net_read_u32(&r), version = qa_net_read_u32(&r);
    qa_q3_product product = (qa_q3_product)qa_net_read_u32(&r);
    bool demo = q3_save_bool(&r);
    qa_net_address remote = {0};
    if (tag != Q3_CLIENT_CHECKPOINT_TAG || version != 2 ||
        (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA))
        return qa_net_reader_fail(&r, "Unsupported Q3 client checkpoint schema");
    if (!q3_restore_address(&r, &remote)) return false;
    int32_t challenge = qa_net_read_i32(&r);
    if (r.failed) return false;
    qa_q3_client_peer *p = NULL;
    bool created = demo ? qa_q3_client_peer_create_demo(identity, product, hooks, &p, error) :
        qa_q3_client_peer_create(identity, product, &remote, challenge, 0, hooks, &p, error);
    if (!created) return false;
    p->remote = remote; p->challenge = challenge;
    if (!demo) {
        qa_q3_channel *channel = NULL;
        if (!qa_q3_channel_restore(&r, &channel)) goto failure;
        qa_q3_channel_destroy(p->channel); p->channel = channel;
    }
    p->reliable.sequence = qa_net_read_i32(&r); p->reliable.acknowledged = qa_net_read_i32(&r);
    for (size_t i = 0; i < QA_Q3_RELIABLE; ++i)
        if (!qa_net_read_string(&r, p->reliable.text[i], QA_Q3_COMMAND_CHARS) ||
            !qa_net_read_string(&r, p->server_commands[i], QA_Q3_COMMAND_CHARS)) goto failure;
    p->server_message_sequence = qa_net_read_i32(&r); p->server_command_sequence = qa_net_read_i32(&r);
    p->last_executed_server_command = qa_net_read_i32(&r); p->server_id = qa_net_read_i32(&r);
    p->last_packet_sent_time = qa_net_read_i32(&r); p->receive_time = qa_net_read_i32(&r);
    p->demo_waiting = q3_save_bool(&r); p->disconnected = q3_save_bool(&r);
    p->disconnect_started = q3_save_bool(&r); p->disconnect_packets = qa_net_read_u8(&r);
    p->transmit_size = qa_net_read_u16(&r);
    if (p->transmit_size > sizeof(p->transmit_packet)) {
        qa_net_reader_fail(&r, "Saved Q3 client datagram exceeds its actual transmit buffer"); goto failure;
    }
    if (!qa_net_read_data(&r, p->transmit_packet, p->transmit_size)) goto failure;
    if (!q3_restore_gamestate(&r, &p->gamestate)) goto failure;
    p->latest_snapshot = qa_net_read_i32(&r); p->has_snapshot = q3_save_bool(&r);
    p->parse_entities_number = qa_net_read_u64(&r); p->command_number = qa_net_read_u64(&r);
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i)
        if (!q3_restore_snapshot(&r, &p->history[i], product)) goto failure;
    for (size_t i = 0; i < 64; ++i) if (!q3_restore_usercmd(&r, &p->commands[i])) goto failure;
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i) {
        p->packets[i].command_number = qa_net_read_u64(&r);
        p->packets[i].server_time = qa_net_read_i32(&r); p->packets[i].real_time = qa_net_read_i32(&r);
    }
    if (!qa_net_read_string(&r, p->big_configstring, sizeof(p->big_configstring)) ||
        !qa_net_reader_finish(&r) || !continuation_valid(p, error)) goto failure;
    *out = p; return true;
failure:
    qa_q3_client_peer_destroy(p); return false;
}
