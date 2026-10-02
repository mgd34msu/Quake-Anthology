#include "internal.h"
#include <stdlib.h>
#include <string.h>

bool qa_network_fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
qa_network_peer *qa_network_peer_get(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error) {
    if (!runtime || !qa_net_connections_get(runtime->connections, id)) {
        qa_network_fail(error, "Stale network connection"); return NULL;
    }
    qa_network_peer *peer = &runtime->peers[id.slot];
    if (!peer->occupied || !qa_net_client_id_equal(peer->id, id)) {
        qa_network_fail(error, "Network peer is not attached"); return NULL;
    }
    return peer;
}
bool qa_network_admission(void *context, const qa_net_connect *request, qa_error *error) {
    qa_network_runtime *runtime = context;
    runtime->callback = true;
    bool ok = runtime->options.hooks.admit(runtime->options.hooks.context, request, error);
    runtime->callback = false;
    return ok;
}
bool qa_network_create(qa_net_transport *transport, const qa_network_options *options,
                        qa_network_runtime **out, qa_error *error) {
    if (!transport || !options || !out || !options->owner || !options->clients ||
        !options->packets_per_pump || !options->hooks.admit || !options->hooks.controlled ||
        !options->hooks.command || SIZE_MAX / options->clients < sizeof(qa_network_peer))
        return qa_network_fail(error, "Invalid network runtime options");
    qa_network_runtime *runtime = calloc(1, sizeof(*runtime));
    if (!runtime) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating network runtime"); return false; }
    runtime->options = *options;
    runtime->peers = calloc(options->clients, sizeof(*runtime->peers));
    if (!runtime->peers || !qa_net_connections_create(options->owner, options->clients,
        qa_network_admission, runtime, &runtime->connections, error)) {
        if (!runtime->peers) qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating network peers");
        free(runtime->peers); free(runtime); return false;
    }
    runtime->transport = transport; *out = runtime; return true;
}
static void retire(qa_network_runtime *runtime, qa_network_peer *peer, const char *reason) {
    qa_net_client_id id = peer->id;
    qa_network_history_clear(peer);
    runtime->callback = true;
    peer->ops.close(peer->state);
    if (runtime->options.hooks.disconnected)
        runtime->options.hooks.disconnected(runtime->options.hooks.context, id, reason);
    runtime->callback = false;
    free(peer->seats); memset(peer, 0, sizeof(*peer));
    qa_net_connections_remove(runtime->connections, id, NULL);
}
void qa_network_destroy(qa_network_runtime *runtime) {
    if (!runtime || runtime->callback || runtime->pumping) return;
    for (uint32_t i = 0; i < runtime->options.clients; ++i)
        if (runtime->peers[i].occupied) retire(runtime, &runtime->peers[i], "runtime closed");
    qa_net_connections_destroy(runtime->connections);
    qa_net_transport_close(runtime->transport);
    free(runtime->peers); free(runtime);
}
const qa_net_connections *qa_network_connections(const qa_network_runtime *runtime) {
    return runtime ? runtime->connections : NULL;
}
bool qa_network_udp_policy_read(const qa_network_runtime *runtime, qa_net_udp_policy *out,
    bool *present, qa_error *error)
{
    if (!runtime) return qa_network_fail(error, "Missing native socket policy owner");
    return qa_net_udp_policy_read(runtime->transport, out, present, error);
}
bool qa_network_callbacks_idle(const qa_network_runtime *runtime) {
    return !runtime || (!runtime->callback && !runtime->pumping);
}
bool qa_network_send_address(qa_network_runtime *runtime, const qa_net_address *address,
                              qa_bytes bytes, qa_error *error) {
    if (!runtime || !address) return qa_network_fail(error, "Missing connectionless transport address");
    return qa_net_transport_send(runtime->transport, address, bytes, error);
}
bool qa_network_attach(qa_network_runtime *runtime, const qa_net_connect *request,
                        const qa_network_peer_ops *ops, void *state, uint64_t now,
                        qa_net_client_id *out, qa_error *error) {
    if (!runtime || runtime->callback || runtime->pumping || !request || !ops || !out ||
        !ops->receive || !ops->flush || !ops->command || !ops->restart || !ops->rebind ||
        !ops->close || !request->seat_count ||
        request->seat_count > qa_network_protocol_seat_capacity(request->protocol))
        return qa_network_fail(error, "Invalid network peer attachment");
    bool unified=request->protocol.kind==QA_NET_UNIFIED_1;
    if(unified && !qa_unified_session_attachment(runtime,ops,state,request))
        return qa_network_fail(error,"Unified attachment lacks its actual token channel and Source owner");
    qa_network_seat *seats = calloc(request->seat_count, sizeof(*seats));
    for (uint32_t i = 0; i < runtime->options.clients; ++i) {
        const qa_net_client *client = runtime->peers[i].occupied ?
            qa_net_connections_get(runtime->connections, runtime->peers[i].id) : NULL;
        if(client && unified && qa_unified_session_token_conflict(state,&runtime->peers[i])) {
            free(seats); return qa_network_fail(error,"Unified token already belongs to an actual peer");
        }
        if (client && qa_net_address_equal(&client->endpoint, &request->endpoint, true) &&
            !(unified && qa_unified_session_peer(&runtime->peers[i]))) {
            free(seats); return qa_network_fail(error, "Transport endpoint already belongs to a connection");
        }
    }
    if (!seats) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating network seat histories"); return false; }
    qa_net_client_id id;
    if (!qa_net_connections_add(runtime->connections, request, now, &id, error)) { free(seats); return false; }
    for (size_t i = 0; i < request->seat_count; ++i) seats[i].id = request->seats[i].seat;
    runtime->peers[id.slot] = (qa_network_peer){.id = id, .ops = *ops, .state = state,
        .seats = seats, .seat_count = request->seat_count, .epoch = 1, .occupied = true};
    *out = id; return true;
}
bool qa_network_detach(qa_network_runtime *runtime, qa_net_client_id id, const char *reason, qa_error *error) {
    if (!runtime || runtime->callback || runtime->pumping)
        return qa_network_fail(error, "Cannot detach network peer during callback");
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer) return false;
    retire(runtime, peer, reason ? reason : "disconnected"); return true;
}
bool qa_network_discard_incomplete(qa_network_runtime *runtime,qa_net_client_id id,qa_error *error)
{
    if(!runtime || runtime->callback || runtime->pumping ||
        !qa_net_connections_get(runtime->connections,id) || id.slot>=runtime->options.clients ||
        runtime->peers[id.slot].occupied || runtime->peers[id.slot].state || runtime->peers[id.slot].seats)
        return qa_network_fail(error,"Only an actual incomplete cold connection can be discarded");
    if(!qa_net_connections_remove(runtime->connections,id,error)) return false;
    runtime->peers[id.slot]=(qa_network_peer){0}; return true;
}
bool qa_network_connection_incomplete(const qa_network_runtime *runtime,qa_net_client_id id)
{
    return runtime && id.slot<runtime->options.clients && qa_net_connections_get(runtime->connections,id) &&
        !runtime->peers[id.slot].occupied && !runtime->peers[id.slot].state && !runtime->peers[id.slot].seats;
}
bool qa_network_send(qa_network_runtime *runtime, qa_net_client_id id, qa_bytes bytes, qa_error *error) {
    const qa_net_client *client = runtime ? qa_net_connections_get(runtime->connections, id) : NULL;
    if (!client) return qa_network_fail(error, "Cannot send to stale connection");
    return qa_net_transport_send(runtime->transport, &client->endpoint, bytes, error);
}
bool qa_network_received(qa_network_runtime *runtime, qa_net_client_id id, uint64_t now, qa_error *error) {
    return runtime && qa_net_connections_received(runtime->connections, id, now, error);
}
bool qa_network_phase(qa_network_runtime *runtime, qa_net_client_id id, qa_net_phase phase, qa_error *error) {
    return runtime && qa_net_connections_phase(runtime->connections, id, phase, error);
}
uint64_t qa_network_epoch(const qa_network_runtime *runtime, qa_net_client_id id) {
    return runtime && qa_net_connections_get(runtime->connections, id) ? runtime->peers[id.slot].epoch : 0;
}
static bool receive_pending(const qa_network_runtime *runtime) {
    for (uint32_t i = 0; i < runtime->options.clients; ++i) {
        const qa_network_peer *peer = &runtime->peers[i];
        if (peer->occupied && peer->ops.receive_pending && peer->ops.receive_pending(peer->state)) return true;
    }
    return false;
}
static bool retirement_pending(const qa_network_peer *peer)
{
    return qa_network_nq_retirement_pending(peer) || qa_network_qw_retirement_pending(peer) ||
        qa_network_q2_retirement_pending(peer);
}
bool qa_network_pump(qa_network_runtime *runtime, uint64_t now, qa_error *error) {
    if (!runtime || runtime->pumping || runtime->callback || now < runtime->now_ns)
        return qa_network_fail(error, "Invalid or recursive network pump");
    runtime->pumping = true; runtime->now_ns = now;
    bool ok = true;
    for (uint32_t n = 0; n < runtime->options.packets_per_pump; ++n) {
        if (receive_pending(runtime)) break;
        qa_net_datagram packet;
        if (!qa_net_transport_receive(runtime->transport, now, &packet, error)) { ok = false; break; }
        if (packet.kind == QA_NET_POLL_EMPTY) break;
        if (packet.kind != QA_NET_POLL_PACKET) continue;
        qa_network_peer *target = NULL;
        bool connectionless = packet.payload.size >= 4 && qa_load_u32le(packet.payload.data) == UINT32_MAX;
        for (uint32_t i = 0; !connectionless && i < runtime->options.clients; ++i) {
            qa_network_peer *peer = &runtime->peers[i];
            const qa_net_client *client = peer->occupied ? qa_net_connections_get(runtime->connections, peer->id) : NULL;
            if (client && (client->protocol.kind==QA_NET_UNIFIED_1 ? qa_unified_session_peer_matches(peer,&packet) :
                qa_network_qw_peer(peer) ? qa_network_qw_peer_matches(peer, &packet) :
                qa_network_q2_peer(peer) ? qa_network_q2_peer_matches(peer,&packet) :
                qa_net_address_equal(&client->endpoint, &packet.from, true))) { target = peer; break; }
        }
        runtime->callback = true;
        if (target) {
            if (!target->ops.receive(target->state, runtime, target->id, &packet, error)) {
                runtime->callback = false;
                if(!retirement_pending(target)) retire(runtime, target, "receive failed");
                ok = false; break;
            }
        } else if (runtime->options.hooks.connectionless &&
            !runtime->options.hooks.connectionless(runtime->options.hooks.context, runtime, &packet, error)) ok = false;
        runtime->callback = false;
        if (!ok) break;
    }
    bool held = receive_pending(runtime);
    for (uint32_t i = 0; ok && !held && i < runtime->options.clients; ++i) {
        qa_network_peer *peer = &runtime->peers[i];
        const qa_net_client *client = peer->occupied ? qa_net_connections_get(runtime->connections, peer->id) : NULL;
        if (!client) continue;
        if (!qa_unified_session_peer(peer) && !retirement_pending(peer) && runtime->options.timeout_ns &&
            qa_net_client_expired(client, now, runtime->options.timeout_ns)) {
            retire(runtime, peer, "connection timed out"); continue;
        }
        runtime->callback = true;
        bool sent = peer->ops.flush(peer->state, runtime, peer->id, now, error);
        runtime->callback = false;
        if (!sent) {
            if(!retirement_pending(peer)) retire(runtime, peer, "send failed");
            ok = false;
        }
    }
    runtime->pumping = false; return ok;
}
bool qa_network_restart(qa_network_runtime *runtime, qa_net_client_id id,
                         const qa_sha256_digest *composition, qa_error *error) {
    if (!runtime || runtime->callback || runtime->pumping || !composition)
        return qa_network_fail(error, "Invalid network travel boundary");
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer) return false;
    if (peer->epoch == UINT64_MAX) return qa_network_fail(error, "Network epoch exhausted");
    if (!qa_net_connections_restart(runtime->connections, id, composition, error)) return false;
    ++peer->epoch; qa_network_history_clear(peer);
    runtime->callback = true;
    bool ok = peer->ops.restart(peer->state, peer->epoch, composition, error);
    runtime->callback = false;
    if (!ok) retire(runtime, peer, "travel signon failed");
    return ok;
}
bool qa_network_reconnect(qa_network_runtime *runtime, qa_net_client_id id,
                           const qa_net_address *endpoint, qa_bytes proof, uint64_t now, qa_error *error) {
    if (!runtime || runtime->callback || runtime->pumping || !endpoint || !runtime->options.hooks.reconnect)
        return qa_network_fail(error, "Missing authenticated reconnect contract");
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer) return false;
    const qa_net_client *client = qa_net_connections_get(runtime->connections, id);
    for (uint32_t i = 0; i < runtime->options.clients; ++i) {
        const qa_network_peer *other = &runtime->peers[i];
        if (!other->occupied || qa_net_client_id_equal(other->id, id)) continue;
        const qa_net_client *bound = qa_net_connections_get(runtime->connections, other->id);
        if (bound && qa_net_address_equal(&bound->endpoint, endpoint, true))
            return qa_network_fail(error, "Transport endpoint already belongs to a connection");
    }
    runtime->callback = true;
    bool ok = runtime->options.hooks.reconnect(runtime->options.hooks.context, client, endpoint, proof, error);
    runtime->callback = false;
    if (!ok) return false;
    /* Table validation precedes the peer mutation; failed peer mutation closes
     * the connection rather than retaining disagreeing endpoint owners. */
    if (!qa_net_connections_rebind(runtime->connections, id, endpoint, error)) return false;
    runtime->callback = true; ok = peer->ops.rebind(peer->state, endpoint, error); runtime->callback = false;
    if (!ok) { retire(runtime, peer, "reconnect failed"); return false; }
    return qa_net_connections_received(runtime->connections, id, now, error);
}
