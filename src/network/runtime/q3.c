#include "internal.h"
#include "qa/network_q3_runtime.h"
#include "qa/network_save.h"
#include "../q3/client_private.h"
#include "../q3/server_private.h"
#include <stdlib.h>
#include <string.h>

typedef struct q3_runtime_client {
    qa_q3_client_peer *source;
    qa_network_q3_client_policy policy;
} q3_runtime_client;
static bool client_settings(q3_runtime_client *p, const qa_net_client *client, int32_t time,
    qa_q3_client_readiness *ready, qa_q3_client_send *send, qa_error *error)
{
    if (!client || !p->policy.settings) return qa_network_fail(error, "Q3 client lost its admitted send policy");
    *ready = (qa_q3_client_readiness){.real_time = time,
        .local = client->endpoint.kind == QA_NET_LOOPBACK, .lan = qa_q3_is_lan(&client->endpoint)};
    *send = (qa_q3_client_send){.real_time = time};
    if (!p->policy.settings(p->policy.context, client, ready, send, error)) return false;
    return (ready->real_time == time && send->real_time == time &&
        ready->local == (client->endpoint.kind == QA_NET_LOOPBACK) && ready->lan == qa_q3_is_lan(&client->endpoint)) ||
        qa_network_fail(error, "Q3 source send policy changed its actual transport clock or endpoint");
}
static bool client_receive(void *context, qa_network_runtime *runtime, qa_net_client_id id,
    const qa_net_datagram *packet, qa_error *error)
{
    q3_runtime_client *p = context; qa_q3_receive_kind kind;
    if (p->source->disconnected || p->source->disconnect_started || p->source->demo) return true;
    if (!qa_q3_client_peer_receive(p->source, packet->payload,
        (int32_t)((packet->received_ns / UINT64_C(1000000)) & INT32_MAX), &kind, error)) return false;
    if (kind == QA_Q3_PACKET_STALE) return true;
    if (!qa_network_received(runtime, id, packet->received_ns, error)) return false;
    /* Deferred source construction owns media readiness, independently of
     * having parsed bytes into the native gamestate/snapshot storage. */
    if (p->source->hooks.defer_source) return true;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(runtime), id);
    if (client->phase == QA_NET_CONNECTED && qa_q3_client_peer_gamestate(p->source)->string_bytes > 1 &&
        !qa_network_phase(runtime, id, QA_NET_PRIMED, error)) return false;
    return client->phase != QA_NET_PRIMED || !qa_q3_client_peer_snapshot(p->source) ||
        qa_network_phase(runtime, id, QA_NET_ACTIVE, error);
}
static bool client_flush(void *context, qa_network_runtime *runtime, qa_net_client_id id,
    uint64_t now, qa_error *error)
{
    q3_runtime_client *p = context;
    if (p->source->disconnected || p->source->disconnect_started || p->source->demo) return true;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(runtime), id);
    int32_t time = (int32_t)((now / UINT64_C(1000000)) & INT32_MAX);
    qa_q3_client_readiness ready; qa_q3_client_send send;
    return client_settings(p, client, time, &ready, &send, error) &&
        (!qa_q3_client_peer_ready(p->source, &ready) || qa_q3_client_peer_send(p->source, &send, error));
}
static bool client_command(void *context, const qa_usercmd *command, qa_error *error)
{
    q3_runtime_client *p = context;
    if (command->kind != QA_RULESET_Q3 || command->has_arsenal ||
        command->forward_move < -127 || command->forward_move > 127 ||
        command->side_move < -127 || command->side_move > 127 ||
        command->up_move < -127 || command->up_move > 127)
        return qa_network_fail(error, "Q3 source command requires its original movement and weapon fields");
    const qa_usercmd *move = command;
    qa_q3_usercmd value = {.serverTime = move->server_time_ms, .buttons = (int32_t)move->buttons,
        .weapon = move->weapon, .forwardmove = (int8_t)move->forward_move,
        .rightmove = (int8_t)move->side_move, .upmove = (int8_t)move->up_move};
    memcpy(value.angles, move->angle_words, sizeof(value.angles));
    return qa_q3_client_peer_usercmd(p->source, &value, error);
}
static bool client_restart(void *context, uint64_t epoch, const uint64_t *composition, qa_error *error)
{
    (void)context; (void)epoch; (void)composition;
    return qa_network_fail(error, "Q3 client travel must arrive through the original server gamestate");
}
static bool client_rebind(void *context, const qa_net_address *address, qa_error *error)
{
    (void)context; (void)address;
    return qa_network_fail(error, "Q3 client endpoint changes require a fresh challenge admission");
}
static void client_close(void *context)
{ q3_runtime_client *p = context; qa_q3_client_peer_destroy(p->source); free(p); }
static bool client_receive_pending(const void *context)
{ const q3_runtime_client *p = context; return qa_q3_client_peer_receive_pending(p->source); }
static const qa_network_peer_ops client_ops = {
    client_receive, client_flush, client_command, client_restart, client_rebind, client_close, client_receive_pending
};
static q3_runtime_client *client_get(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer) return NULL;
    if (peer->ops.receive != client_receive) { qa_network_fail(error, "Connection is not a Q3 client peer"); return NULL; }
    return peer->state;
}
static bool attach_client(qa_network_runtime *runtime, const qa_net_connect *request,
    qa_q3_product product, int32_t challenge, uint16_t qport, const qa_q3_client_hooks *hooks,
    const qa_network_q3_client_policy *policy, bool demo, uint64_t now,
    qa_net_client_id *out, qa_error *error)
{
    if (!request || request->protocol.kind != QA_NET_Q3_68 || request->seat_count != 1 ||
        !hooks || !policy || !policy->settings || !out ||
        (demo && (request->attachment != QA_NET_LOCAL_SEAT || request->endpoint.kind != QA_NET_LOOPBACK)))
        return qa_network_fail(error, "Q3 client attachment requires its original single-seat admission");
    q3_runtime_client *p = calloc(1, sizeof(*p));
    if (!p) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q3 runtime client"); return false; }
    p->policy = *policy;
    qa_net_client_id id;
    if (!qa_network_attach(runtime, request, &client_ops, p, now, &id, error)) { free(p); return false; }
    qa_q3_identity identity = {id, true, request->seats[0].seat};
    bool okay = demo ? qa_q3_client_peer_create_demo(identity, product, hooks, &p->source, error) :
        qa_q3_client_peer_create(identity, product, &request->endpoint, challenge, qport, hooks, &p->source, error);
    if (!okay) {
        (void)qa_network_detach(runtime, id, "Q3 client allocation failed", NULL); return false;
    }
    *out = id; return true;
}
bool qa_network_attach_q3_client(qa_network_runtime *runtime, const qa_net_connect *request,
    qa_q3_product product, int32_t challenge, uint16_t qport, const qa_q3_client_hooks *hooks,
    const qa_network_q3_client_policy *policy, uint64_t now, qa_net_client_id *out, qa_error *error)
{ return attach_client(runtime, request, product, challenge, qport, hooks, policy, false, now, out, error); }
bool qa_network_attach_q3_demo(qa_network_runtime *runtime, const qa_net_connect *request,
    qa_q3_product product, const qa_q3_client_hooks *hooks,
    const qa_network_q3_client_policy *policy, uint64_t now, qa_net_client_id *out, qa_error *error)
{ return attach_client(runtime, request, product, 0, 0, hooks, policy, true, now, out, error); }
bool qa_network_q3_client_demo_sequence(qa_network_runtime *runtime, qa_net_client_id id,
    int32_t sequence, qa_error *error)
{
    if (!runtime || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Q3 demo sequence requires an idle runtime");
    q3_runtime_client *p = client_get(runtime, id, error);
    return p && qa_q3_client_peer_demo_sequence(p->source, sequence, error);
}
bool qa_network_q3_client_demo_message(qa_network_runtime *runtime, qa_net_client_id id,
    int32_t sequence, qa_bytes message, int32_t now, qa_error *error)
{
    if (!runtime || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Q3 demo message requires an idle runtime");
    q3_runtime_client *p = client_get(runtime, id, error);
    if (!p) return false;
    if (!p->source->demo)
        return qa_network_fail(error, "Q3 demo message requires its actual channel-free peer");
    return qa_q3_client_peer_message(p->source, sequence, message, now, error);
}
bool qa_network_q3_client_record_seed(qa_network_runtime *runtime, qa_net_client_id id,
    qa_q3_writer *writer, int32_t *sequence, qa_error *error)
{
    if (!runtime || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Q3 recording seed requires an idle runtime");
    q3_runtime_client *p = client_get(runtime, id, error);
    return p && qa_q3_client_peer_record_seed(p->source, writer, sequence, error);
}
const qa_q3_client_peer *qa_network_q3_client_view(qa_network_runtime *runtime, qa_net_client_id id)
{ q3_runtime_client *p = client_get(runtime, id, NULL); return p ? p->source : NULL; }
bool qa_network_q3_client_live(qa_network_runtime *runtime, qa_net_client_id id)
{
    q3_runtime_client *p = client_get(runtime, id, NULL);
    return p && !p->source->disconnected && !p->source->disconnect_started;
}
bool qa_network_q3_client_receive_pending(qa_network_runtime *runtime, qa_net_client_id id)
{ q3_runtime_client *p = client_get(runtime, id, NULL); return p && qa_q3_client_peer_receive_pending(p->source); }
bool qa_network_q3_client_continue(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    if (!runtime || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Q3 held source continuation requires an idle runtime");
    q3_runtime_client *p = client_get(runtime, id, error);
    if (!p) return false;
    if (qa_q3_client_peer_receive_pending(p->source) &&
        p->source->receive_cursor.phase == QA_Q3_SERVER_CURSOR_GAMESTATE) {
        const qa_net_client *client = qa_net_connections_get(runtime->connections, id);
        qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
        if (!client || !peer ||
            !qa_net_connections_restart(runtime->connections, id, &client->composition, error)) return false;
        /* Server-selected loading resets prediction admission without changing
         * the actual transport epoch, channel or reliable command ownership. */
        qa_network_history_clear(peer);
    }
    return qa_q3_client_peer_continue(p->source, error);
}
bool qa_network_q3_client_init_read(qa_network_runtime *runtime, qa_net_client_id id,
    qa_network_q3_client_init *out, qa_error *error)
{
    q3_runtime_client *p = client_get(runtime, id, error);
    if (!p) return false;
    if (!out || p->source->disconnected || p->source->disconnect_started ||
        qa_q3_client_peer_receive_pending(p->source) ||
        p->source->gamestate.string_bytes <= 1)
        return qa_network_fail(error, "Q3 Init requires the actual live decoded gamestate");
    *out = (qa_network_q3_client_init){p->source->server_message_sequence,
        p->source->last_executed_server_command, p->source->gamestate.client_number};
    return true;
}
bool qa_network_q3_client_init_current(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_network_q3_client_init *retained)
{
    qa_network_q3_client_init actual;
    return retained && qa_network_q3_client_init_read(runtime, id, &actual, NULL) &&
        actual.server_message == retained->server_message &&
        actual.last_executed_server_command == retained->last_executed_server_command &&
        actual.client_number == retained->client_number;
}
bool qa_network_q3_client_acknowledged_usercmd(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q3_snapshot *snapshot, bool *has_sequence, uint64_t *sequence,
    bool *history_unavailable, qa_error *error)
{
    q3_runtime_client *p = client_get(runtime, id, error);
    if (!p) return false;
    if (!snapshot || !has_sequence || !sequence || !history_unavailable ||
        p->source->disconnected || p->source->disconnect_started ||
        qa_q3_client_peer_snapshot_at(p->source, snapshot->message_number) != snapshot)
        return qa_network_fail(error, "Prediction acknowledgement needs its actual live retained snapshot");
    return qa_network_q3_client_acknowledged_command_time(runtime,id,snapshot->player.commandTime,
        has_sequence,sequence,history_unavailable,error);
}
bool qa_network_q3_client_acknowledged_command_time(qa_network_runtime *runtime, qa_net_client_id id,
    int32_t command_time, bool *has_sequence, uint64_t *sequence, bool *history_unavailable, qa_error *error)
{
    q3_runtime_client *p=client_get(runtime,id,error);
    if(!p) return false;
    if(!has_sequence || !sequence || !history_unavailable ||
        p->source->disconnected || p->source->disconnect_started)
        return qa_network_fail(error,"Command-time acknowledgement needs its actual live native ring");
    uint64_t latest = qa_q3_client_peer_usercmd_number(p->source);
    bool matched = latest == 0; uint64_t acknowledged = 0;
    uint64_t count = latest < 64 ? latest : 64;
    for (uint64_t offset = 0; offset < count; ++offset) {
        uint64_t number = latest - offset;
        const qa_q3_usercmd *command = qa_q3_client_peer_usercmd_at(p->source, number);
        if (command && command->serverTime == command_time) {
            matched = true; acknowledged = number; break;
        }
    }
    *has_sequence = matched; *sequence = acknowledged; *history_unavailable = !matched;
    return true;
}
bool qa_network_q3_client_command(qa_network_runtime *runtime, qa_net_client_id id, const char *text, qa_error *error)
{ q3_runtime_client *p = client_get(runtime, id, error); return p && qa_q3_client_peer_command(p->source, text, error); }
bool qa_network_q3_client_usercmd(qa_network_runtime *runtime, qa_net_client_id id, const qa_q3_usercmd *command, qa_error *error)
{ q3_runtime_client *p = client_get(runtime, id, error); return p && qa_q3_client_peer_usercmd(p->source, command, error); }
bool qa_network_q3_client_execute(qa_network_runtime *runtime, qa_net_client_id id, int32_t sequence, qa_error *error)
{ q3_runtime_client *p = client_get(runtime, id, error); return p && qa_q3_client_peer_execute(p->source, sequence, false, error); }
bool qa_network_q3_client_send(qa_network_runtime *runtime, qa_net_client_id id, int32_t now, qa_error *error)
{
    q3_runtime_client *p = client_get(runtime, id, error); qa_q3_client_readiness ready; qa_q3_client_send send;
    return p && client_settings(p, qa_net_connections_get(qa_network_connections(runtime), id), now, &ready, &send, error) &&
        qa_q3_client_peer_send(p->source, &send, error);
}
bool qa_network_q3_client_disconnect(qa_network_runtime *runtime, qa_net_client_id id, int32_t now, qa_error *error)
{
    q3_runtime_client *p = client_get(runtime, id, error); qa_q3_client_readiness ready; qa_q3_client_send send;
    if (p && p->source->disconnected) return true;
    return p && client_settings(p, qa_net_connections_get(qa_network_connections(runtime), id), now, &ready, &send, error) &&
        qa_q3_client_peer_disconnect(p->source, &send, error);
}
bool qa_network_q3_client_retire(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    q3_runtime_client *p = client_get(runtime, id, error);
    if (!p) return false;
    p->source->disconnected = true; return true;
}

typedef struct q3_runtime_peer {
    qa_network_runtime *runtime;
    qa_net_client_id id;
    qa_q3_server_peer *source;
    qa_q3_server_hooks hooks;
    qa_q3_product product;
    bool defer_signon;
} q3_runtime_peer;

static qa_q3_server_world world(void *context)
{ q3_runtime_peer *p = context; return p->hooks.world(p->hooks.context); }
static bool source_command(void *context, const qa_q3_command *command, bool allowed, qa_error *error)
{ q3_runtime_peer *p = context; return p->hooks.command(p->hooks.context, command, allowed, error); }
static bool enter(void *context, const qa_q3_usercmd *command, qa_error *error)
{
    q3_runtime_peer *p = context;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(p->runtime), p->id);
    if (!client || (client->phase != QA_NET_ACTIVE &&
        !qa_network_phase(p->runtime, p->id, QA_NET_ACTIVE, error))) return false;
    return p->hooks.enter_world(p->hooks.context, command, error);
}
static bool think(void *context, const qa_q3_usercmd *command, qa_error *error)
{ q3_runtime_peer *p = context; return p->hooks.think(p->hooks.context, command, error); }
static bool signon(void *context, qa_error *error)
{ q3_runtime_peer *p = context; return p->hooks.resend_gamestate(p->hooks.context, error); }
static bool rejected(void *context, qa_error *error)
{ q3_runtime_peer *p = context; return p->hooks.pure_rejected_snapshot(p->hooks.context, error); }
static bool drop(void *context, const char *reason, qa_error *error)
{ q3_runtime_peer *p = context; return p->hooks.drop(p->hooks.context, reason, error); }
static bool send(void *context, const qa_net_address *address, qa_bytes bytes, qa_error *error)
{
    q3_runtime_peer *p = context;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(p->runtime), p->id);
    if (!client || !qa_net_address_equal(address, &client->endpoint, true))
        return qa_network_fail(error, "Q3 send endpoint differs from the shared connection");
    return qa_network_send(p->runtime, p->id, bytes, error);
}
static bool receive(void *context, qa_network_runtime *runtime, qa_net_client_id id,
    const qa_net_datagram *packet, qa_error *error)
{
    q3_runtime_peer *p = context; qa_q3_receive_kind kind;
    if (!qa_q3_server_peer_receive(p->source, packet->payload, &kind, error)) return false;
    if (kind == QA_Q3_PACKET_STALE) return true;
    if (!qa_network_received(runtime, id, packet->received_ns, error)) return false;
    return kind != QA_Q3_PACKET_MESSAGE || qa_q3_server_peer_state(p->source)->phase != QA_Q3_CONNECTED || signon(p, error);
}
static bool flush(void *context, qa_network_runtime *runtime, qa_net_client_id id,
    uint64_t now_ns, qa_error *error)
{
    q3_runtime_peer *p = context; bool sent;
    (void)runtime; (void)id; (void)now_ns;
    return qa_q3_server_peer_fragment(p->source, &sent, error);
}
static bool local_command(void *context, const qa_usercmd *command, qa_error *error)
{
    (void)context; (void)command;
    return qa_network_fail(error, "A Q3 server peer cannot submit client movement");
}
static bool restart(void *context, uint64_t epoch, const uint64_t *composition, qa_error *error)
{
    q3_runtime_peer *p = context; (void)epoch; (void)composition;
    qa_q3_server_state *state = qa_q3_server_peer_state(p->source);
    state->phase = QA_Q3_CONNECTED; state->delta_message = -1;
    state->pure_authentic = false; state->got_pure_command = false;
    if (p->defer_signon) { p->defer_signon = false; return true; }
    /* Original reliable history and channel queues survive travel. The new
     * server_id fences old commands; signon installs fresh baselines/deltas. */
    return signon(p, error);
}
static bool rebind(void *context, const qa_net_address *address, qa_error *error)
{ return qa_q3_server_peer_rebind(((q3_runtime_peer *)context)->source, address, error); }
static void close_peer(void *context)
{
    q3_runtime_peer *p = context;
    qa_q3_server_peer_destroy(p->source); free(p);
}
static const qa_network_peer_ops ops = {.receive=receive,.flush=flush,.command=local_command,
    .restart=restart,.rebind=rebind,.close=close_peer};
static q3_runtime_peer *get(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer) return NULL;
    if (peer->ops.receive != receive) { qa_network_fail(error, "Connection is not a Q3 server peer"); return NULL; }
    return peer->state;
}
bool qa_network_attach_q3_server(qa_network_runtime *runtime, const qa_net_connect *request,
    qa_q3_product product, int32_t challenge, uint16_t qport, const qa_q3_server_hooks *hooks,
    uint64_t now_ns, qa_net_client_id *out, qa_error *error)
{
    if (!request || request->protocol.kind != QA_NET_Q3_68 || request->seat_count != 1 || !hooks || !out ||
        !hooks->world || !hooks->command || !hooks->enter_world || !hooks->think || !hooks->resend_gamestate ||
        !hooks->pure_rejected_snapshot || !hooks->drop)
        return qa_network_fail(error, "Q3 server attachment requires a complete original single-seat owner");
    q3_runtime_peer *p = calloc(1, sizeof(*p));
    if (!p) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q3 runtime peer"); return false; }
    p->runtime = runtime; p->hooks = *hooks; p->product = product;
    if (!qa_network_attach(runtime, request, &ops, p, now_ns, &p->id, error)) { free(p); return false; }
    qa_q3_server_hooks source = {.context = p, .world = world, .command = source_command,
        .enter_world = enter, .think = think, .resend_gamestate = signon,
        .pure_rejected_snapshot = rejected, .drop = drop, .send = send};
    qa_net_client_id id = p->id;
    if (!qa_q3_server_peer_create((qa_q3_identity){id, true, request->seats[0].seat}, product,
        &request->endpoint, challenge, qport, &source, &p->source, error)) {
        (void)qa_network_detach(runtime, id, "Q3 channel allocation failed", NULL); return false;
    }
    *out = id; return true;
}
bool qa_network_q3_gamestate(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q3_gamestate *state, const qa_q3_server_rate *rate, qa_error *error)
{
    q3_runtime_peer *p = get(runtime, id, error);
    if (!p || !qa_q3_server_peer_gamestate(p->source, state, rate, error)) return false;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(runtime), id);
    return client && (client->phase != QA_NET_CONNECTED || qa_network_phase(runtime, id, QA_NET_PRIMED, error));
}
bool qa_network_q3_seed_baselines(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q3_gamestate *state, qa_error *error)
{
    if (!runtime || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Q3 initial source baselines require an idle runtime");
    q3_runtime_peer *p = get(runtime, id, error);
    const qa_net_client *client = p ? qa_net_connections_get(runtime->connections, id) : NULL;
    if (!client || client->phase != QA_NET_CONNECTED)
        return qa_network_fail(error, "Q3 initial source baselines lack their connected shared client");
    return qa_q3_server_peer_seed_baselines(p->source, state, error);
}
bool qa_network_q3_snapshot(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q3_snapshot *snapshot, const qa_q3_server_rate *rate, const qa_q3_download *downloads,
    size_t count, qa_error *error)
{
    q3_runtime_peer *p = get(runtime, id, error);
    return p && qa_q3_server_peer_snapshot_downloads(p->source, snapshot, rate, downloads, count, error);
}
bool qa_network_q3_snapshot_write(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q3_snapshot *snapshot, const qa_q3_server_rate *rate,
    qa_q3_server_download_write_fn write_downloads, void *context, qa_error *error)
{
    q3_runtime_peer *p = get(runtime, id, error);
    return p && qa_q3_server_peer_snapshot_write(p->source, snapshot, rate, write_downloads, context, error);
}
bool qa_network_q3_command(qa_network_runtime *runtime, qa_net_client_id id, const char *text, qa_error *error)
{ q3_runtime_peer *p = get(runtime, id, error); return p && qa_q3_server_peer_command(p->source, text, error); }
bool qa_network_q3_configstring(qa_network_runtime *runtime, qa_net_client_id id,
    unsigned index, const char *text, qa_error *error)
{ q3_runtime_peer *p = get(runtime, id, error); return p && qa_q3_server_peer_configstring(p->source, index, text, error); }
bool qa_network_q3_pure(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q3_pure_server *pure, const qa_q3_tokens *tokens, qa_q3_pure_result *result, qa_error *error)
{ q3_runtime_peer *p = get(runtime, id, error); return p && qa_q3_server_peer_pure(p->source, pure, tokens, result, error); }
bool qa_network_q3_reset_pure(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{ q3_runtime_peer *p = get(runtime, id, error); return p && qa_q3_server_peer_reset_pure(p->source, error); }
bool qa_network_q3_state(qa_network_runtime *runtime, qa_net_client_id id, qa_q3_server_state *out, qa_error *error)
{
    q3_runtime_peer *p = get(runtime, id, error);
    if (!p || !out) return qa_network_fail(error, "Missing Q3 state snapshot output");
    *out = *qa_q3_server_peer_state(p->source); return true;
}
const qa_q3_server_peer *qa_network_q3_server_view(qa_network_runtime *runtime, qa_net_client_id id)
{ q3_runtime_peer *p = get(runtime, id, NULL); return p ? p->source : NULL; }
bool qa_network_q3_round_activate(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    if (!runtime || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Q3 source round activation requires its idle runtime");
    q3_runtime_peer *p = get(runtime, id, error);
    const qa_net_client *client = p ? qa_net_connections_get(runtime->connections, id) : NULL;
    qa_q3_server_state *state = p ? qa_q3_server_peer_state(p->source) : NULL;
    if (!p || !client || p->defer_signon || state->phase < QA_Q3_CONNECTED || state->phase > QA_Q3_ACTIVE)
        return qa_network_fail(error, "Q3 source round activation lacks a retained connected peer");
    runtime->callback = true;
    qa_q3_server_world current = p->hooks.world(p->hooks.context);
    runtime->callback = false;
    if (!current.generation || current.server_id <= 0 || current.restarted_server_id <= 0 ||
        current.restarted_server_id > current.server_id)
        return qa_network_fail(error, "Q3 source round activation lacks its current source world");
    if (client->phase == QA_NET_CONNECTED && !qa_network_phase(runtime, id, QA_NET_PRIMED, error)) return false;
    if (client->phase == QA_NET_PRIMED && !qa_network_phase(runtime, id, QA_NET_ACTIVE, error)) return false;
    state->phase = QA_Q3_ACTIVE; state->delta_message = -1;
    state->next_snapshot_time = current.time;
    return true;
}
bool qa_network_q3_disconnect(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q3_server_rate *rate, uint8_t flags, const char *reason, qa_error *error)
{
    if (!runtime || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Q3 native disconnect requires its idle runtime");
    q3_runtime_peer *p = get(runtime, id, error);
    if (!p) return false;
    runtime->callback = true;
    bool ok = qa_q3_server_peer_disconnect(p->source, rate, flags, reason, error);
    runtime->callback = false;
    return ok;
}
bool qa_network_q3_reconnect_channel(qa_network_runtime *runtime, qa_net_client_id id,
    int32_t challenge, uint16_t qport, qa_error *error)
{
    if (!qa_network_callbacks_idle(runtime)) return qa_network_fail(error, "Q3 reconnect requires the runtime safe point");
    q3_runtime_peer *p = get(runtime, id, error);
    const qa_net_client *client = p ? qa_net_connections_get(qa_network_connections(runtime), id) : NULL;
    if (!p || !client) return false;
    qa_q3_server_peer *next = NULL;
    qa_q3_server_hooks source = {.context = p, .world = world, .command = source_command,
        .enter_world = enter, .think = think, .resend_gamestate = signon,
        .pure_rejected_snapshot = rejected, .drop = drop, .send = send};
    /* Product comes from the current original player state, retained by the
     * adapter at initial admission; no source provider is chosen here. */
    qa_q3_product product = p->product;
    if (!qa_q3_server_peer_create((qa_q3_identity){id, true, client->seats[0].seat}, product,
        &client->endpoint, challenge, qport, &source, &next, error)) return false;
    qa_q3_server_peer_destroy(p->source); p->source = next; p->defer_signon = true; return true;
}

bool qa_network_q3_checkpoint_peer(const qa_network_peer *peer, uint32_t *kind,
    qa_buffer *out, qa_error *error)
{
    if (peer->ops.receive == client_receive) {
        *kind = QA_NETWORK_SOURCE_Q3_CLIENT;
        return qa_q3_client_peer_checkpoint(((q3_runtime_client *)peer->state)->source, out, error);
    }
    if (peer->ops.receive == receive) {
        q3_runtime_peer *p = peer->state; qa_buffer source = {0};
        *kind = QA_NETWORK_SOURCE_Q3_SERVER;
        if (!qa_q3_server_peer_checkpoint(p->source, &source, error)) return false;
        qa_buffer bytes = {malloc(source.size + 1), source.size + 1};
        if (!bytes.data) {
            qa_buffer_free(&source); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q3 server adapter continuation"); return false;
        }
        bytes.data[0] = p->defer_signon;
        memcpy(bytes.data + 1, source.data, source.size); qa_buffer_free(&source); *out = bytes; return true;
    }
    return qa_network_fail(error, "Installed network dialect has no concrete continuation codec");
}

bool qa_network_q3_restore_peer(qa_network_runtime *runtime, const qa_net_client *client,
    uint32_t kind, qa_bytes bytes, const qa_network_checkpoint_refs *refs,
    qa_network_peer *peer, qa_error *error)
{
    if (!refs || !refs->source || client->protocol.kind != QA_NET_Q3_68 || client->seat_count != 1 ||
        (kind != QA_NETWORK_SOURCE_Q3_CLIENT && kind != QA_NETWORK_SOURCE_Q3_SERVER))
        return qa_network_fail(error, "Saved network peer lacks its qualified source consumer");
    qa_q3_client_hooks client_hooks = {0}; qa_q3_server_hooks server_hooks = {0};
    if (!refs->source(refs->context, client, (qa_network_source_kind)kind, &client_hooks, &server_hooks, error)) return false;
    qa_q3_identity identity = {client->id, true, client->seats[0].seat};
    if (kind == QA_NETWORK_SOURCE_Q3_CLIENT) {
        q3_runtime_client *p = calloc(1, sizeof(*p));
        if (!p) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring Q3 runtime client"); return false; }
        if (!refs->client_q3_policy) {
            free(p); return qa_network_fail(error, "Restored Q3 client lacks its actual source send policy");
        }
        if (!refs->client_q3_policy(refs->context, client, &p->policy, error)) { free(p); return false; }
        if (!p->policy.settings) { free(p); return qa_network_fail(error, "Restored Q3 client policy has no actual producer"); }
        if (!qa_q3_client_peer_restore(bytes, identity, &client_hooks, &p->source, error)) { free(p); return false; }
        if (p->source->demo ?
            (client->attachment != QA_NET_LOCAL_SEAT || client->endpoint.kind != QA_NET_LOOPBACK) :
            !qa_net_address_equal(&p->source->remote, &client->endpoint, true)) {
            client_close(p); return qa_network_fail(error, "Restored Q3 client endpoint or live mode differs");
        }
        peer->ops = client_ops; peer->state = p; return true;
    }
    if (!bytes.size || bytes.data[0] > 1 || !server_hooks.world || !server_hooks.command ||
        !server_hooks.enter_world || !server_hooks.think || !server_hooks.resend_gamestate ||
        !server_hooks.pure_rejected_snapshot || !server_hooks.drop)
        return qa_network_fail(error, "Saved Q3 server adapter lacks complete source bindings");
    q3_runtime_peer *p = calloc(1, sizeof(*p));
    if (!p) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring Q3 runtime server"); return false; }
    p->runtime = runtime; p->id = client->id; p->hooks = server_hooks; p->defer_signon = bytes.data[0] != 0;
    qa_q3_server_hooks source = {.context = p, .world = world, .command = source_command,
        .enter_world = enter, .think = think, .resend_gamestate = signon,
        .pure_rejected_snapshot = rejected, .drop = drop, .send = send};
    if (!qa_q3_server_peer_restore((qa_bytes){bytes.data + 1, bytes.size - 1}, identity, &source, &p->source, error)) {
        free(p); return false;
    }
    if (!qa_net_address_equal(&p->source->remote, &client->endpoint, true)) {
        close_peer(p); return qa_network_fail(error, "Restored Q3 server endpoint differs");
    }
    p->product = p->source->product; peer->ops = ops; peer->state = p; return true;
}
