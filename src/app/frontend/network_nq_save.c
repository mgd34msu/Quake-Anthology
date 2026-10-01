#include "network_nq_private.h"
#include "qa/application_network.h"
#include "qa/launch_identity.h"
#include "qa/network_save.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool address_fields(qa_source_save_io *io, qa_net_address *v)
{
    uint32_t kind = v->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_NET_IPX || !qa_source_save_u16(io, &v->port)) return false;
    v->kind = (qa_net_address_kind)kind;
    switch (v->kind) {
    case QA_NET_IPV4: return qa_source_save_bytes(io, v->host.ipv4, 4);
    case QA_NET_IPV6: return qa_source_save_bytes(io, v->host.ipv6.bytes, 16) && qa_source_save_u32(io, &v->host.ipv6.scope);
    case QA_NET_LOOPBACK: return qa_source_save_bytes(io, v->host.loopback, sizeof(v->host.loopback)) &&
        memchr(v->host.loopback, 0, sizeof(v->host.loopback));
    case QA_NET_IPX: return qa_source_save_u32(io, &v->host.ipx.network) && qa_source_save_bytes(io, v->host.ipx.node, 6);
    }
    return false;
}
static bool short_fields(qa_source_save_io *io, int16_t *value)
{
    uint16_t bits = (uint16_t)*value;
    if (!qa_source_save_u16(io, &bits)) return false;
    *value = bits <= INT16_MAX ? (int16_t)bits : (int16_t)((int32_t)bits - 65536);
    return true;
}
static bool command_fields(qa_source_save_io *io, qa_q1_command *v)
{
    if (!qa_source_save_f32(io, &v->time) || !isfinite(v->time)) return false;
    for (size_t i = 0; i < 3; ++i) if (!qa_source_save_f32(io, &v->angles[i]) || !isfinite(v->angles[i])) return false;
    return short_fields(io, &v->forward) && short_fields(io, &v->side) && short_fields(io, &v->up) &&
        qa_source_save_u8(io, &v->buttons) && qa_source_save_u8(io, &v->impulse);
}
static bool entity_fields(qa_source_save_io *io, qa_q1_entity *v)
{
    if (!qa_source_save_u32(io, &v->number) || !qa_source_save_u32(io, &v->model) || !qa_source_save_u32(io, &v->frame) ||
        !qa_source_save_u32(io, &v->colormap) || !qa_source_save_u32(io, &v->skin) || !qa_source_save_u32(io, &v->effects)) return false;
    for (size_t i = 0; i < 3; ++i) if (!qa_source_save_f32(io, &v->origin[i]) || !isfinite(v->origin[i]) ||
        !qa_source_save_f32(io, &v->angles[i]) || !isfinite(v->angles[i])) return false;
    return qa_source_save_u8(io, &v->alpha) && qa_source_save_u8(io, &v->scale) && qa_source_save_bool(io, &v->step) &&
        qa_source_save_f32(io, &v->lerp_finish) && qa_source_save_u32(io, &v->qw_flags);
}
static bool fields(qa_source_save_io *io, frontend_nq_host *host)
{
    uint32_t magic = UINT32_C(0x484e4151), version = 3;
    if (!qa_source_save_u32(io, &magic) || magic != UINT32_C(0x484e4151) || !qa_source_save_u32(io, &version) || version != 3 ||
        !frontend_save_provider(io, host->frontend->application, &host->owner) || !host->owner ||
        !qa_source_save_bytes(io, host->composition.bytes, sizeof(host->composition.bytes)) ||
        !qa_source_save_u64(io, &host->generation) || !qa_source_save_u64(io, &host->submillisecond_ns) ||
        !qa_source_save_bool(io, &host->previous_pause) ||
        !qa_source_save_u64(io, &host->published_source_time_ns) ||
        !qa_source_save_u64(io, &host->next_admission_order)) return false;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *p = host->peers + i;
        bool bound = p->host != NULL;
        if ((io->direction == QA_SOURCE_SAVE_WRITE && bound && p->host != host) ||
            !qa_source_save_bool(io, &bound) || !qa_source_save_bool(io, &p->occupied) || !qa_source_save_bool(io, &p->retiring) ||
            !qa_source_save_bool(io, &p->command_present) || !qa_source_save_u64(io, &p->client.owner) ||
            !qa_source_save_u64(io, &p->client.generation) || !qa_source_save_u32(io, &p->client.slot) ||
            !qa_source_save_u64(io, &p->seat.owner) || !qa_source_save_u32(io, &p->seat.index) ||
            !qa_source_save_u32(io, &p->source_slot) || !qa_source_save_u64(io, &p->entered_ns) ||
            !qa_source_save_u64(io, &p->input_sequence) || !qa_source_save_u64(io, &p->tick_sequence) ||
            !qa_source_save_u64(io, &p->admission_order) || !qa_source_save_u8(io, &p->ping_count) ||
            !qa_source_save_u8(io, &p->impulse) || !command_fields(io, &p->latest) ||
            !qa_source_save_bytes(io, p->reason, sizeof(p->reason)) || !memchr(p->reason, 0, sizeof(p->reason))) return false;
        for (size_t j = 0; j < NQ_PINGS; ++j)
            if (!qa_source_save_f64(io, p->pings + j)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) p->host = bound ? host : NULL;
        size_t maximum = UINT16_MAX;
        if (io->direction == QA_SOURCE_SAVE_READ && maximum > (io->input.size - io->offset) / 59)
            maximum = (io->input.size - io->offset) / 59;
        if (!qa_source_save_count(io, &p->baseline_count, maximum)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ && p->baseline_count) {
            p->baselines = calloc(p->baseline_count, sizeof(*p->baselines));
            if (!p->baselines) return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring actual NetQuake peer baselines");
        }
        if (p->baseline_count && !p->baselines) return false;
        for (size_t j = 0; j < p->baseline_count; ++j) {
            qa_q1_entity copy = p->baselines[j];
            if (!entity_fields(io, &copy)) return false;
            if (io->direction == QA_SOURCE_SAVE_READ) p->baselines[j] = copy;
        }
    }
    if (!qa_source_save_count(io, &host->pending_count, NQ_PENDING)) return false;
    for (size_t i = 0; i < NQ_PENDING; ++i) {
        nq_pending_control *p = host->pending + i;
        if (!address_fields(io, &p->address) || !qa_source_save_u64(io, &p->received_ns) ||
            !qa_source_save_count(io, &p->size, sizeof(p->bytes)) || !qa_source_save_bytes(io, p->bytes, p->size)) return false;
    }
    for (size_t i = 0; i < 256; ++i) {
        nq_status_cache *v = host->board + i;
        if (!frontend_save_text(io, &v->name) || !qa_source_save_i32(io, &v->frags) || !qa_source_save_f32(io, &v->source_frags) ||
            !qa_source_save_u8(io, &v->colors) || !qa_source_save_bool(io, &v->present)) return false;
    }
    for (size_t i = 0; i < 256; ++i)
        if (!frontend_save_text(io, host->published_names + i)) return false;
    for (size_t i = 0; i < 64; ++i) if (!frontend_save_text(io, host->styles + i)) return false;
    return true;
}
static bool state_valid(const frontend_nq_host *host, bool complete_clock, qa_error *error)
{
    if (!host || !host->frontend || !host->frontend->application || !host->runtime || host->busy ||
        !host->frontend->options.network_host || host->frontend->options.network_connect ||
        host->frontend->options.network_protocol.kind != QA_NET_NQ15 || host->frontend->options.network_protocol.flags ||
        host->frontend->options.network_protocol.revision || !host->owner ||
        host->generation != qa_application_configuration_generation(host->frontend->application) ||
        host->submillisecond_ns >= UINT64_C(1000000) || host->pending_count > NQ_PENDING || !host->next_admission_order)
        return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake host lacks its actual source generation and idle policy");
    qa_actor_id actor; qa_actor_owner owner; uint32_t slot; qa_net_protocol_id protocol;
    if (!qa_application_player_actor(host->frontend->application, 0, &actor) ||
        !qa_application_network_q1_source(host->frontend->application, actor, &owner, &slot, &protocol, error) ||
        owner != host->owner || protocol.kind != QA_NET_NQ15 || protocol.flags || protocol.revision)
        return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake host changes its selected classic source owner");
    uint32_t client_slots, entity_slots; const char *models[255]; size_t model_count; uint32_t player_model = 0;
    if (!qa_application_network_q1_extents(host->frontend->application, actor, &client_slots, &entity_slots, error) ||
        !qa_application_network_q1_precache(host->frontend->application, actor, true, models, &model_count, error)) return false;
    for (size_t i = 0; i < model_count; ++i) if (!strcmp(models[i], "progs/player.mdl")) player_model = (uint32_t)i + 1;
    qa_buffer identity = {0}; qa_sha256_digest digest;
    if (!qa_launch_identity_encode(qa_application_launch(host->frontend->application),
        qa_session_actors(qa_application_session(host->frontend->application)), &identity, error)) return false;
    qa_sha256((qa_bytes){identity.data, identity.size}, &digest); qa_buffer_free(&identity);
    if (!qa_sha256_equal(&digest, &host->composition))
        return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake host changes its complete launch composition");
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        const nq_frontend_peer *p = host->peers + i;
        if ((p->host && p->host != host) || (p->client.owner && p->client.owner != QA_NETWORK_COMMAND_OWNER) ||
            (p->seat.owner && p->seat.owner != QA_NETWORK_COMMAND_OWNER) || (p->baseline_count && !p->baselines) ||
            p->baseline_count > UINT16_MAX || !memchr(p->reason, 0, sizeof(p->reason)) ||
            p->ping_count > NQ_PINGS ||
            (!p->occupied && (p->retiring || p->command_present || p->baseline_count || p->ping_count || p->admission_order)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake physical peer has invalid owned storage");
        double maximum_ping = ((double)UINT64_MAX / 1e9 + (double)FLT_MAX) * 1000;
        for (size_t j = 0; j < NQ_PINGS; ++j)
            if (!isfinite(p->pings[j]) || p->pings[j] < 0 || p->pings[j] > maximum_ping ||
                (j >= p->ping_count && p->pings[j] != 0))
                return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake ping history differs from its received timestamp domain");
        if (!p->occupied) continue;
        if (p->host != host || p->client.owner != QA_NETWORK_COMMAND_OWNER || !p->client.generation || p->client.slot >= NQ_CLIENTS ||
            p->seat.owner != QA_NETWORK_COMMAND_OWNER || !p->source_slot || p->source_slot > client_slots ||
            p->seat.index != 128u + p->source_slot || p->baseline_count < client_slots || !player_model ||
            !p->admission_order || p->admission_order >= host->next_admission_order ||
            p->ping_count != (p->input_sequence < NQ_PINGS ? p->input_sequence : NQ_PINGS) ||
            (p->command_present && !p->input_sequence) ||
            (!p->command_present && p->impulse) || (complete_clock && p->entered_ns > host->frontend->time_ns))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake peer changes its actual native seat or input identity");
        for (size_t j = 0; j < i; ++j) if (host->peers[j].occupied &&
            (qa_net_client_id_equal(p->client, host->peers[j].client) || p->source_slot == host->peers[j].source_slot ||
             p->admission_order == host->peers[j].admission_order))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake peers alias one source client or connection");
        for (size_t j = 0; j < p->baseline_count; ++j) {
            const qa_q1_entity *e = p->baselines + j;
            uint8_t bytes[64]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
            qa_nq_message message = {.op = QA_NQ_BASELINE, .data.entity = *e};
            if (!e->number || e->number > UINT16_MAX || e->number >= entity_slots || e->model > model_count ||
                (j && e->number <= p->baselines[j - 1].number) || e->effects || e->step ||
                (j < client_slots && (e->number != j + 1 || e->model != player_model || e->colormap != e->number || e->frame || e->skin)) ||
                (j >= client_slots && (!e->model || e->colormap)) ||
                !qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15},
                    (qa_nq_options){.standard_quake = true}, &message, NULL, 0))
                return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake baseline lacks its original source wire admission");
        }
    }
    for (size_t i = 0; i < NQ_PENDING; ++i) {
        const nq_pending_control *p = host->pending + i;
        if (p->size > sizeof(p->bytes) || (i < host->pending_count && !p->size) ||
            (p->size && (p->size < 5 || p->bytes[0] != 128 || p->bytes[1] != 0 ||
                p->bytes[4] < QA_NQ_CONNECT_REQUEST || p->bytes[4] > QA_NQ_RULE_INFO_REQUEST || !p->address.port ||
                (p->address.kind != QA_NET_IPV4 && p->address.kind != QA_NET_IPV6))) ||
            (complete_clock && p->received_ns > host->frontend->time_ns))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake query queue differs from its actual receive producer");
    }
    for (size_t i = 0; i < 256; ++i) {
        const nq_status_cache *v = host->board + i;
        if (host->published_names[i] && strlen(host->published_names[i]) > NQ_MESSAGE - 3)
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake published name exceeds its real source service extent");
        if (!isfinite(v->source_frags) || (v->present && (!i || !v->name || (v->colors & 15) > 13 || (v->colors >> 4) > 13)) ||
            (!v->present && (v->name || v->frags || v->source_frags != 0 || v->colors)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake source status cache has invalid native ownership");
        double wrapped = fmod(trunc((double)v->source_frags), 4294967296.0);
        if (wrapped < 0) wrapped += 4294967296.0;
        uint32_t bits = (uint32_t)wrapped;
        int32_t frags = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
        if (v->frags != frags) return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake cache score differs from its actual source value");
    }
    return true;
}
bool frontend_nq_qualified(const frontend_nq_host *host, bool complete_clock, qa_error *error)
{
    if (!state_valid(host, complete_clock, error) || !qa_network_callbacks_idle(host->runtime)) return false;
    size_t expected = 0;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        const nq_frontend_peer *p = host->peers + i; if (!p->occupied) continue;
        ++expected;
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(host->runtime), p->client);
        const qa_q1_peer *native = qa_network_nq_server_view(host->runtime, p->client);
        qa_network_nq_server_state state; qa_network_nq_server_policy policy, declared; qa_network_nq_server_hooks hooks;
        if (!client || !native || native->kind != QA_Q1_PEER_NETQUAKE || !native->channel.nq || !native->transport ||
            !qa_net_address_equal(&native->remote, &client->endpoint, true) ||
            qa_net_transport_address(native->transport) != qa_network_local_address(host->runtime) || client->connected_ns != p->entered_ns ||
            !frontend_nq_source_hooks((frontend_nq_host *)host, client, &declared, &hooks, error) ||
            !qa_network_nq_server_policy_read(host->runtime, p->client, &policy, error) ||
            policy.message_bytes != declared.message_bytes || policy.fragment_bytes != declared.fragment_bytes ||
            policy.queued_bytes != declared.queued_bytes || !qa_network_nq_server_state_read(host->runtime, p->client, &state, error) ||
            !state.started || !state.stage || state.input_sequence != p->input_sequence || state.retiring != p->retiring)
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake host/native channel/source policy inventories differ");
        size_t cursor = 0; qa_application_network_player row;
        while (qa_application_network_player_next(host->frontend->application, &cursor, &row))
            if (qa_net_client_id_equal(row.client, p->client) && row.seat.owner == p->seat.owner && row.seat.index == p->seat.index &&
                !p->retiring && row.source_begin_pending != (state.stage < 3))
                return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake source begin differs from native signon stage");
    }
    uint32_t cursor = 0; const qa_net_client *client; size_t count = 0;
    while (qa_net_connections_next(qa_network_connections(host->runtime), &cursor, &client)) ++count;
    if (count != expected) return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake runtime has an undeclared frontend connection");
    size_t roster = 0, rows = 0; qa_application_network_player row;
    while (qa_application_network_player_next(host->frontend->application, &roster, &row)) {
        bool found = false;
        for (size_t i = 0; i < NQ_CLIENTS; ++i) if (host->peers[i].occupied && qa_net_client_id_equal(row.client, host->peers[i].client) &&
            row.seat.owner == host->peers[i].seat.owner && row.seat.index == host->peers[i].seat.index) found = true;
        if (!found) return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake application roster has an undeclared frontend peer");
        ++rows;
    }
    return rows == expected || frontend_fail(error, QA_ERROR_FORMAT, "NetQuake frontend and application roster counts differ");
}
bool frontend_nq_checkpoint(const frontend_nq_host *host, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !frontend_nq_qualified(host, true, error)) return false;
    frontend_nq_host *copy = malloc(sizeof(*copy));
    if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "Capturing real NetQuake frontend continuation");
    *copy = *host;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) if (copy->peers[i].host) copy->peers[i].host = copy;
    qa_source_save_io io = {0}; qa_buffer bytes = {0};
    bool ok = qa_source_save_writer(&io, qa_application_session(host->frontend->application), error) && fields(&io, copy) &&
        qa_source_save_finish(&io, &bytes);
    qa_source_save_dispose(&io); free(copy);
    if (!ok) { qa_buffer_free(&bytes); return false; }
    *out = bytes; return true;
}
bool frontend_nq_restore(qa_frontend *frontend, qa_network_runtime *runtime, qa_bytes bytes,
    frontend_nq_host **out, qa_error *error)
{
    if (!frontend || !frontend->application || !runtime || !out || *out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake continuation requires isolated candidate ownership");
    frontend_nq_host *host = calloc(1, sizeof(*host));
    if (!host) return frontend_fail(error, QA_ERROR_MEMORY, "Restoring real NetQuake frontend continuation");
    host->frontend = frontend; host->runtime = runtime; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) && fields(&io, host) &&
        qa_source_save_finish(&io, NULL) && state_valid(host, false, error);
    qa_source_save_dispose(&io);
    if (!ok) { frontend_nq_destroy(host); return false; }
    *out = host; return true;
}
void frontend_nq_rebind(frontend_nq_host *host, qa_frontend *frontend, qa_network_runtime *runtime)
{ if (host) { host->frontend = frontend; host->runtime = runtime; } }
