#include "session_internal.h"
#include "qa/network_q2_session_save.h"
#include "qa/network_q2_wire_save.h"
#include "channel_internal.h"
#include "frames_internal.h"

static bool invalid(qa_source_save_io *io, const char *message)
{ qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message); io->failed = true; return false; }
static bool text(qa_source_save_io *io, char **value)
{
    bool present = *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    size_t length = io->direction == QA_SOURCE_SAVE_READ ? 0 : strlen(*value);
    if (!qa_source_save_count(io, &length, SIZE_MAX - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset) return invalid(io, "Q2 retained text exceeds its document");
        *value = malloc(length + 1); if (!*value) return invalid(io, "Cannot restore retained Q2 text");
        (*value)[length] = 0;
    }
    return qa_source_save_bytes(io, *value, length) &&
        (!memchr(*value, 0, length) || invalid(io, "Q2 retained text contains a terminator"));
}
static bool buffer(qa_source_save_io *io, qa_buffer *value, size_t maximum)
{
    if (!qa_source_save_count(io, &value->size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && value->size) {
        if (value->size > io->input.size - io->offset) return invalid(io, "Q2 retained bytes exceed their document");
        value->data = malloc(value->size); if (!value->data) return invalid(io, "Cannot restore retained Q2 bytes");
    }
    return (!value->size || value->data) && qa_source_save_bytes(io, value->data, value->size);
}
static bool game_state(qa_source_save_io *io, q2_game_state *value)
{
    if (!qa_q2_save_serverdata(io, &value->view.data) ||
        !qa_source_save_count(io, &value->view.config_count, UINT16_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && value->view.config_count) {
        value->configs = calloc(value->view.config_count, sizeof(*value->configs));
        if (!value->configs) return invalid(io, "Cannot restore Q2 signon configuration");
        value->view.configs = value->configs;
    }
    for (size_t i = 0; i < value->view.config_count; ++i) {
        if (!qa_source_save_u16(io, &value->configs[i].index) || !text(io, (char **)&value->configs[i].value)) return false;
        if (!value->configs[i].value || (i && value->configs[i].index <= value->configs[i - 1].index))
            return invalid(io, "Q2 signon configuration order differs");
    }
    if (!qa_source_save_count(io, &value->view.baselines.count, UINT16_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && value->view.baselines.count) {
        value->baselines = calloc(value->view.baselines.count, sizeof(*value->baselines));
        if (!value->baselines) return invalid(io, "Cannot restore Q2 signon baselines");
        value->view.baselines.data = value->baselines;
    }
    uint32_t previous = 0;
    for (size_t i = 0; i < value->view.baselines.count; ++i) {
        if (!qa_q2_save_entity(io, value->baselines + i)) return false;
        if (value->baselines[i].number <= previous || value->baselines[i].number > UINT16_MAX)
            return invalid(io, "Q2 retained signon baseline order differs");
        previous = value->baselines[i].number;
    }
    return true;
}
static bool channel_equal(const qa_q2_channel_options *a, const qa_q2_channel_options *b)
{
    return a->protocol.kind == b->protocol.kind && a->protocol.revision == b->protocol.revision &&
        a->protocol.flags == b->protocol.flags && a->server == b->server && a->new_channel == b->new_channel &&
        a->compress == b->compress && a->qport == b->qport && a->payload_bytes == b->payload_bytes &&
        a->message_bytes == b->message_bytes && a->datagram_bytes == b->datagram_bytes && a->sequence_recording == b->sequence_recording;
}
static bool download(qa_source_save_io *io, q2_server *server, const qa_network_q2_checkpoint_refs *refs)
{
    bool present = server->download != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return !server->download_view && !server->download_offset && !server->download_opening.path;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!refs || (reading ? !refs->view_decode || !refs->resource_decode : !refs->view_encode || !refs->resource_encode))
        return invalid(io, "Q2 download lacks its immutable candidate content graph");
    uint64_t view = 0, pool = 0, resource = 0;
    if (!reading && (!server->download_view || server->download_opening.resource_id != qa_resource_id(server->download) ||
        !qa_vfs_acquisition_retained(server->download_view, &server->download_opening, io->error) ||
        !refs->view_encode(refs->context, server->download_view, &view, io->error) ||
        !refs->resource_encode(refs->context, server->download, &pool, &resource, io->error))) return false;
    if (!qa_source_save_u64(io, &view) || !qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &resource) ||
        !view || !pool || !resource || !qa_source_save_u64(io, &server->download_opening.mount) ||
        !text(io, &server->download_opening.path) || !text(io, &server->download_opening.lookup_path) ||
        !text(io, &server->download_opening.link_source) || !text(io, &server->download_opening.link_target) ||
        !qa_source_save_count(io, &server->download_offset, INT32_MAX)) return false;
    if (reading) {
        const qa_resource *decoded = NULL;
        if (!refs->view_decode(refs->context, view, &server->download_view, io->error) || !server->download_view ||
            !refs->resource_decode(refs->context, pool, resource, &decoded, io->error) || !decoded) return false;
        server->download = (qa_resource *)decoded; qa_resource_retain(server->download);
        server->download_opening.resource_id = qa_resource_id(decoded);
    }
    qa_bytes bytes = qa_resource_bytes(server->download);
    return bytes.size <= INT32_MAX && server->download_offset <= bytes.size &&
        qa_vfs_acquisition_retained(server->download_view, &server->download_opening, io->error);
}
static bool server_fields(qa_source_save_io *io, q2_session *session, const qa_network_q2_checkpoint_refs *refs)
{
    q2_server *server = &session->state.server;
    qa_network_q2_server_policy policy = server->policy;
    if (!qa_q2_save_channel_options(io, &policy.channel) || !qa_source_save_u32(io, &policy.max_clients) ||
        !qa_source_save_count(io, &policy.history_capacity, SIZE_MAX / sizeof(qa_q2_wire_frame)) ||
        !qa_source_save_i32(io, &policy.server_count) || !qa_source_save_u64(io, &policy.source_interval_ns)) return false;
    if (!channel_equal(&policy.channel, &server->policy.channel) || policy.max_clients != server->policy.max_clients ||
        policy.history_capacity != server->policy.history_capacity || policy.source_interval_ns != server->policy.source_interval_ns ||
        !policy.source_interval_ns || policy.source_interval_ns > UINT64_C(1000000000) ||
        !policy.channel.server || !policy.max_clients || !policy.history_capacity)
        return invalid(io, "Q2 host candidate policy differs from its actual source");
    if (io->direction == QA_SOURCE_SAVE_READ) server->policy.server_count = policy.server_count;
    for (size_t i = 0; i < 14; ++i)
        if (!qa_source_save_i32(io, server->settings.values + i)) return false;
    if (!qa_source_save_u32(io, &server->settings.fps) || !qa_source_save_u32(io, &server->settings.frame_divisor) ||
        !qa_source_save_u64(io, &server->settings.source_interval_ns) ||
        !qa_source_save_bytes(io, server->userinfo, sizeof(server->userinfo)) ||
        !qa_source_save_bool(io, &server->has_source_frame) || !qa_source_save_u64(io, &server->last_source_frame) ||
        !qa_source_save_i32(io, &server->wire_frame)) return false;
    uint32_t source_fps = (uint32_t)(UINT64_C(1000000000) / policy.source_interval_ns), source_divisor = source_fps / 10;
    if (!source_divisor) source_divisor = 1;
    if (server->settings.source_interval_ns != policy.source_interval_ns || !server->settings.frame_divisor ||
        source_divisor % server->settings.frame_divisor ||
        server->settings.fps != source_fps / server->settings.frame_divisor ||
        !memchr(server->userinfo, 0, sizeof(server->userinfo)) || server->wire_frame < 1 ||
        (session->codec.protocol.kind != QA_NET_Q2PRO_36 && server->settings.frame_divisor != 1))
        return invalid(io, "Q2 retained connection settings or Source frame pacing differs");
    for (size_t i = 0; i < 14; ++i)
        if (i != 4 && (server->settings.values[i] < INT16_MIN || server->settings.values[i] > INT16_MAX))
            return invalid(io, "Q2 retained setting leaves its actual client command extent");
    if (!qa_source_save_bool(io, &server->signon_started) || !game_state(io, &server->signon) ||
        !qa_q2_save_history(io, &server->frames)) return false;
    if (server->frames->capacity != server->policy.history_capacity ||
        (server->signon_started && server->signon.view.data.servercount != server->policy.server_count))
        return invalid(io, "Q2 host history or physical signon generation differs");
    for (size_t i = 0; i < QA_NETWORK_MAX_SEATS; ++i)
        if (!qa_q2_save_usercmd(io, &server->replay[i].previous) || !qa_source_save_i32(io, &server->replay[i].last_frame) ||
            !qa_source_save_u64(io, &server->source_sequence[i])) return false;
    if (!buffer(io, &server->datagram, session->channel->capacity) ||
        !qa_source_save_u32(io, &server->dropped) || !qa_source_save_u8(io, &server->reading_seat) ||
        server->reading_seat >= session->seats || !download(io, server, refs)) return false;
    return true;
}
static bool records(qa_source_save_io *io, q2_records *batch)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = batch->count, capacity = batch->capacity, cursor = batch->cursor;
    if (!qa_source_save_count(io, &capacity, SIZE_MAX / sizeof(*batch->records)) ||
        capacity > SIZE_MAX / sizeof(*batch->owned) || !qa_source_save_count(io, &count, capacity) ||
        !qa_source_save_count(io, &cursor, count) || !qa_source_save_u64(io, &batch->received_ns)) return false;
    if ((!count && capacity) || (count && (capacity < 32 || (capacity & (capacity - 1)) ||
        (capacity > 32 && count <= capacity / 2))) ||
        (reading && count > (io->input.size - io->offset) / 14))
        return invalid(io, "Q2 retained record allocation differs from its actual growth and document");
    if (reading && capacity) {
        batch->records = calloc(capacity, sizeof(*batch->records)); batch->owned = calloc(capacity, sizeof(*batch->owned));
        if (!batch->records || !batch->owned) return invalid(io, "Cannot restore Q2 retained Source record allocation");
        batch->capacity = capacity;
    }
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            qa_q2_server_record record = {0}; bool ok = qa_q2_save_record(io, &record) && q2_record_retain(batch, &record, io->error);
            qa_q2_saved_record_free(&record); if (!ok) return false;
        } else if (!qa_q2_save_record(io, batch->records + i)) return false;
    }
    if (reading) batch->cursor = cursor;
    return true;
}
static bool client_fields(qa_source_save_io *io, q2_session *session)
{
    q2_client *client = &session->state.client;
    qa_q2_channel_options channel = client->policy.channel; size_t pending = client->policy.pending_commands;
    if (!qa_q2_save_channel_options(io, &channel) || !qa_source_save_count(io, &pending, SIZE_MAX / sizeof(q2_command_group))) return false;
    if (!channel_equal(&channel, &client->policy.channel) || channel.server || pending != client->policy.pending_commands || !pending)
        return invalid(io, "Q2 client candidate policy differs from its source");
    if (!qa_q2_save_messages(io, &client->policy.messages, &client->messages) || !records(io, &client->batch) ||
        !game_state(io, &client->preparing) || !qa_q2_save_serverdata(io, &client->server_data)) return false;
    for (size_t i = 0; i < QA_NETWORK_MAX_SEATS; ++i)
        if (!qa_q2_save_usercmd(io, &client->oldest[i]) || !qa_q2_save_usercmd(io, &client->previous[i])) return false;
    if (!qa_source_save_count(io, &client->command_capacity, pending) ||
        !qa_source_save_count(io, &client->command_count, client->command_capacity)) return false;
    if (client->command_capacity && client->command_capacity != pending) return invalid(io, "Q2 command allocation differs from its admitted extent");
    if (io->direction == QA_SOURCE_SAVE_READ && client->command_capacity > (io->input.size - io->offset) / (26 * QA_NETWORK_MAX_SEATS))
        return invalid(io, "Q2 retained command allocation exceeds its document");
    if (io->direction == QA_SOURCE_SAVE_READ && client->command_capacity) {
        client->commands = calloc(client->command_capacity, sizeof(*client->commands));
        if (!client->commands) return invalid(io, "Cannot restore Q2 command allocation");
    }
    if (!qa_source_save_u64(io, &client->command_number)) return false;
    for (size_t i = 0; i < client->command_capacity; ++i) {
        if (!qa_source_save_u64(io, &client->commands[i].number)) return false;
        if (i < client->command_count && (!client->commands[i].number ||
            client->commands[i].number > client->command_number ||
            (i && client->commands[i - 1].number >= client->commands[i].number)))
            return invalid(io, "Q2 pending command lost its actual logical number");
        for (size_t seat = 0; seat < QA_NETWORK_MAX_SEATS; ++seat)
            if (!qa_q2_save_usercmd(io, &client->commands[i].commands[seat])) return false;
    }
    bool ok = qa_source_save_u64(io, &client->loading_generation) && qa_source_save_u32(io, &client->acknowledged) &&
        qa_source_save_count(io, &client->command_offset, SIZE_MAX) && qa_source_save_i32(io, &client->last_frame) &&
        qa_source_save_bool(io, &client->has_server_data) && qa_source_save_bool(io, &client->preparing_game_state) &&
        qa_source_save_bool(io, &client->selecting_server_data) && qa_source_save_bool(io, &client->receive_held) &&
        qa_source_save_bool(io, &client->acknowledgement_held) && qa_source_save_bool(io, &client->preparation_held);
    if (!ok) return false;
    if ((client->selecting_server_data && client->preparing_game_state) ||
        ((client->selecting_server_data || client->preparing_game_state) && !client->has_server_data) ||
        (client->preparation_held && !client->selecting_server_data && !client->preparing_game_state) ||
        ((client->batch.count || client->acknowledgement_held) && !client->receive_held) ||
        (client->preparing_game_state && client->preparing.view.data.servercount != client->server_data.servercount) ||
        (session->active && (!client->has_server_data || client->selecting_server_data || client->preparing_game_state)))
        return invalid(io, "Q2 retained Source delivery and preparation phase disagree");
    if (client->command_offset) {
        if (client->batch.cursor >= client->batch.count ||
            client->batch.records[client->batch.cursor].event.kind != QA_Q2_SVC_COMMAND ||
            client->command_offset > strlen(client->batch.records[client->batch.cursor].event.data.print.text))
            return invalid(io, "Q2 retained stufftext cursor has no actual command record");
    }
    qa_q2_codec *decoder = qa_q2_messages_codec(client->messages);
    if (decoder->protocol.kind != session->codec.protocol.kind || decoder->protocol.revision != session->codec.protocol.revision ||
        decoder->protocol.flags != session->codec.protocol.flags || decoder->wire_flags != session->codec.wire_flags ||
        decoder->frame_extra != session->codec.frame_extra || decoder->demo26 != session->codec.demo26 ||
        decoder->frame_player_pending != session->codec.frame_player_pending || decoder->split_players != session->codec.split_players ||
        memcmp(decoder->kex_nonzero_solid, session->codec.kex_nonzero_solid, sizeof(decoder->kex_nonzero_solid)))
        return invalid(io, "Q2 outbound and received codec Source profiles disagree");
    return true;
}
static bool fields(qa_source_save_io *io, q2_session *session, const qa_net_client *client,
    const qa_network_q2_checkpoint_refs *refs)
{
    uint32_t tag = UINT32_C(0x32534e51), version = 3, slot = session->id.slot;
    uint64_t generation = session->id.generation; bool server = session->server; size_t seats = session->seats;
    if (!qa_source_save_u32(io, &tag) || !qa_source_save_u32(io, &version) || !qa_source_save_bool(io, &server) ||
        !qa_source_save_u32(io, &slot) || !qa_source_save_u64(io, &generation) || !qa_source_save_count(io, &seats, QA_NETWORK_MAX_SEATS)) return false;
    if (tag != UINT32_C(0x32534e51) || version != 3 || server != session->server || !seats ||
        slot != client->id.slot || generation != client->id.generation || seats != client->seat_count)
        return invalid(io, "Saved Q2 session does not belong to its actual candidate connection");
    session->seats = seats;
    if (!qa_source_save_bool(io, &session->active) || !qa_source_save_bool(io, &session->retiring) ||
        !qa_q2_save_codec(io, &session->codec) || !qa_q2_save_channel(io, &session->channel)) return false;
    qa_q2_channel_options *options = &session->channel->options;
    if (options->server != server || options->protocol.kind != client->protocol.kind ||
        options->protocol.revision != client->protocol.revision || options->protocol.flags != client->protocol.flags ||
        session->codec.protocol.kind != client->protocol.kind ||
        (session->active && client->phase != QA_NET_ACTIVE) ||
        (!session->active && !session->retiring && client->phase != QA_NET_CONNECTED))
        return invalid(io, "Saved Q2 channel, Source identity and signon phase disagree");
    return server ? server_fields(io, session, refs) : client_fields(io, session);
}
bool qa_network_q2_checkpoint_peer(const qa_network_peer *peer, const qa_network_q2_checkpoint_refs *refs,
    bool *server, qa_buffer *out, qa_error *error)
{
    if (!qa_network_q2_peer(peer) || !server || !out || out->data || out->size) return q2_fail(error, QA_ERROR_ARGUMENT, "Missing idle Q2 continuation owner");
    q2_session *session = peer->state;
    const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
    if (!client || !qa_network_callbacks_idle(session->runtime) || session->busy || session->seats != peer->seat_count)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 continuation requires its exact idle enclosing owner");
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, NULL, error)) return false;
    bool ok = fields(&io, session, client, refs) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); if (ok) *server = session->server; return ok;
}
bool qa_network_q2_restore_peer(qa_network_runtime *runtime, const qa_net_client *client, bool server,
    qa_bytes bytes, const qa_network_q2_checkpoint_refs *refs, qa_network_peer *peer, qa_error *error)
{
    if (!runtime || !client || !refs || !peer || peer->occupied || peer->state || !qa_network_callbacks_idle(runtime) ||
        (server ? !refs->source_server : !refs->source_client)) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 restore requires actual isolated candidate consumers");
    q2_session *session = calloc(1, sizeof(*session));
    if (!session) return q2_fail(error, QA_ERROR_MEMORY, "Restoring Q2 native session owner");
    session->runtime = runtime; session->id = client->id; session->server = server; session->seats = client->seat_count;
    bool ok = server ? refs->source_server(refs->context, client, &session->state.server.policy, &session->state.server.hooks, error) :
        refs->source_client(refs->context, client, &session->state.client.policy, &session->state.client.hooks, error);
    if (ok && !(server ? q2_server_hooks_valid(&session->state.server.hooks) : q2_client_hooks_valid(&session->state.client.hooks)))
        ok = q2_fail(error, QA_ERROR_ARGUMENT, "Q2 candidate Source callback inventory is incomplete");
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, session, client, refs) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { qa_network_q2_peer_ops.close(session); return false; }
    *peer = (qa_network_peer){.id = client->id, .ops = qa_network_q2_peer_ops, .state = session}; return true;
}
