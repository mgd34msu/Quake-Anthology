#include "internal.h"
#include "qa/network_q3_runtime.h"
#include <stdlib.h>

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
static bool local_command(void *context, const qa_network_command *command, qa_error *error)
{
    (void)context; (void)command;
    return qa_network_fail(error, "A Q3 server peer cannot submit client movement");
}
static bool restart(void *context, uint64_t epoch, const qa_sha256_digest *composition, qa_error *error)
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
static const qa_network_peer_ops ops = {receive, flush, local_command, restart, rebind, close_peer};
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
bool qa_network_q3_snapshot(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q3_snapshot *snapshot, const qa_q3_server_rate *rate, const qa_q3_download *downloads,
    size_t count, qa_error *error)
{
    q3_runtime_peer *p = get(runtime, id, error);
    return p && qa_q3_server_peer_snapshot_downloads(p->source, snapshot, rate, downloads, count, error);
}
bool qa_network_q3_command(qa_network_runtime *runtime, qa_net_client_id id, const char *text, qa_error *error)
{ q3_runtime_peer *p = get(runtime, id, error); return p && qa_q3_server_peer_command(p->source, text, error); }
bool qa_network_q3_configstring(qa_network_runtime *runtime, qa_net_client_id id,
    unsigned index, const char *text, qa_error *error)
{ q3_runtime_peer *p = get(runtime, id, error); return p && qa_q3_server_peer_configstring(p->source, index, text, error); }
bool qa_network_q3_pure(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q3_pure_server *pure, const qa_q3_tokens *tokens, qa_q3_pure_result *result, qa_error *error)
{ q3_runtime_peer *p = get(runtime, id, error); return p && qa_q3_server_peer_pure(p->source, pure, tokens, result, error); }
bool qa_network_q3_state(qa_network_runtime *runtime, qa_net_client_id id, qa_q3_server_state *out, qa_error *error)
{
    q3_runtime_peer *p = get(runtime, id, error);
    if (!p || !out) return qa_network_fail(error, "Missing Q3 state snapshot output");
    *out = *qa_q3_server_peer_state(p->source); return true;
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
