#include "internal.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct unified_peer {
    qa_unified_channel *channel;
    qa_unified_token token;
    qa_network_unified_hooks hooks;
    qa_network_runtime *runtime;
    qa_net_client_id id;
} unified_peer;
typedef struct resolved_command { qa_unified_controlled_actor controlled; } resolved_command;
static bool controlled(void *context, uint32_t slot, uint32_t generation,
                         qa_unified_controlled_actor *out, qa_error *error) {
    resolved_command *resolved = context;
    if (resolved->controlled.actor.slot != slot || resolved->controlled.actor.generation != generation)
        return qa_network_fail(error, "Unified actor resolution changed");
    *out = resolved->controlled; return true;
}
static bool integral(double value, double minimum, double maximum) {
    return isfinite(value) && value >= minimum && value <= maximum && floor(value) == value;
}
static bool vector(qa_unified_vec3 from, qa_vec3 *to) {
    if (!isfinite(from.x) || !isfinite(from.y) || !isfinite(from.z) ||
        fabs(from.x) > 3.402823466e38 || fabs(from.y) > 3.402823466e38 || fabs(from.z) > 3.402823466e38) return false;
    *to = (qa_vec3){(float)from.x, (float)from.y, (float)from.z}; return true;
}
static bool movement(const qa_unified_command *source, qa_movement_command *out, qa_error *error) {
    qa_movement_command command = {.kind = source->movement.kind, .sequence = source->sequence};
    double duration = 0, buttons = 0, impulse = 0, forward = 0, side = 0, up = 0;
    switch (source->movement.kind) {
    case QA_MOVEMENT_NETQUAKE: {
        const qa_unified_movement *m = &source->movement;
        command.acknowledged_server_seconds = m->data.nq.acknowledged_seconds;
        if (!vector(m->data.nq.angles, &command.angles)) goto invalid;
        forward = m->data.nq.forward; side = m->data.nq.side; up = m->data.nq.up;
        buttons = m->data.nq.buttons; impulse = m->data.nq.impulse; break;
    }
    case QA_MOVEMENT_QUAKEWORLD: {
        const qa_unified_movement *m = &source->movement;
        duration = m->data.qw.milliseconds;
        if (!vector(m->data.qw.angles, &command.angles)) goto invalid;
        forward = m->data.qw.forward; side = m->data.qw.side; up = m->data.qw.up;
        buttons = m->data.qw.buttons; impulse = m->data.qw.impulse; break;
    }
    case QA_MOVEMENT_Q2_CLASSIC: {
        const qa_unified_movement *m = &source->movement;
        duration = m->data.q2.milliseconds;
        for (size_t i = 0; i < 3; ++i) {
            if (!integral(m->data.q2.angle_shorts[i], INT16_MIN, INT16_MAX)) goto invalid;
            command.angle_words[i] = (int32_t)m->data.q2.angle_shorts[i];
        }
        if (!integral(m->data.q2.light_level, 0, UINT8_MAX)) goto invalid;
        command.light_level = (uint8_t)m->data.q2.light_level;
        forward = m->data.q2.forward; side = m->data.q2.side; up = m->data.q2.up;
        buttons = m->data.q2.buttons; impulse = m->data.q2.impulse; break;
    }
    case QA_MOVEMENT_Q2_RERELEASE: {
        const qa_unified_movement *m = &source->movement;
        duration = m->data.q2r.milliseconds;
        if (!vector(m->data.q2r.angles, &command.angles) ||
            !integral(m->data.q2r.server_frame, INT32_MIN, INT32_MAX)) goto invalid;
        command.server_frame = (int32_t)m->data.q2r.server_frame;
        forward = m->data.q2r.forward; side = m->data.q2r.side; buttons = m->data.q2r.buttons; break;
    }
    case QA_MOVEMENT_Q3: {
        const qa_unified_movement *m = &source->movement;
        if (!integral(m->data.q3.server_time_ms, INT32_MIN, INT32_MAX) ||
            !integral(m->data.q3.weapon, 0, UINT8_MAX)) goto invalid;
        command.server_time_ms = (int32_t)m->data.q3.server_time_ms;
        command.weapon = (uint8_t)m->data.q3.weapon;
        for (size_t i = 0; i < 3; ++i) {
            if (!integral(m->data.q3.angle_words[i], INT32_MIN, INT32_MAX)) goto invalid;
            command.angle_words[i] = (int32_t)m->data.q3.angle_words[i];
        }
        forward = m->data.q3.forward; side = m->data.q3.right; up = m->data.q3.up;
        buttons = m->data.q3.buttons; break;
    }
    default: goto invalid;
    }
    if (!integral(duration, 0, UINT32_MAX) || !integral(buttons, 0, UINT32_MAX) ||
        !integral(impulse, 0, UINT8_MAX) || !isfinite(forward) || !isfinite(side) || !isfinite(up) ||
        fabs(forward) > 3.402823466e38 || fabs(side) > 3.402823466e38 || fabs(up) > 3.402823466e38) goto invalid;
    command.milliseconds = (uint32_t)duration; command.buttons = (uint32_t)buttons;
    command.impulse = (uint8_t)impulse;
    command.forward_move = (float)forward; command.side_move = (float)side; command.up_move = (float)up;
    *out = command; return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Unified movement exceeds native command representation"); return false;
}
static bool delivery(void *context, const qa_unified_delivery *message, qa_error *error) {
    unified_peer *peer = context;
    if (message->payload.size >= 4 && !memcmp(message->payload.data, "QTCM", 4)) {
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(peer->runtime), peer->id);
        /* Reliable packets from the retired epoch can still finish delivery
         * while travel signon is pending. They cannot reach gameplay. */
        if (!client || client->phase != QA_NET_ACTIVE) return true;
        if (message->payload.size < 14) return qa_network_fail(error, "Truncated unified command");
        uint32_t slot = qa_load_u32le(message->payload.data + 6);
        uint32_t generation = qa_load_u32le(message->payload.data + 10);
        qa_net_seat_id seat; resolved_command resolved = {0};
        if (!peer->hooks.resolve(peer->hooks.context, peer->id, slot, generation, &seat, &resolved.controlled, error)) return false;
        qa_unified_command_receiver receiver = {
            .source = {.kind = QA_UNIFIED_SOURCE_REMOTE, .client = peer->id, .seat = seat},
            .actor_registry = resolved.controlled.actor.registry, .context = &resolved, .controlled = controlled};
        qa_unified_command decoded;
        if (!qa_unified_command_decode(message->payload, &receiver, &decoded, error)) return false;
        qa_network_command command = {.client = peer->id, .seat = seat, .actor = decoded.actor,
            .epoch = qa_network_epoch(peer->runtime, peer->id), .has_arsenal = decoded.has_arsenal, .arsenal = decoded.arsenal};
        if (!movement(&decoded, &command.movement, error)) return false;
        return qa_network_accept(peer->runtime, &command, error);
    }
    return peer->hooks.delivery(peer->hooks.context, peer->runtime, peer->id, message, error);
}
static bool receive(void *context, qa_network_runtime *runtime, qa_net_client_id id,
                      const qa_net_datagram *packet, qa_error *error) {
    unified_peer *peer = context; peer->runtime = runtime; peer->id = id;
    qa_unified_packet decoded; qa_error ignored = {0};
    if (!qa_unified_packet_decode(packet->payload, &decoded, &ignored) ||
        memcmp(decoded.token.bytes, peer->token.bytes, sizeof(peer->token.bytes))) return true;
    uint32_t previous = qa_unified_channel_received(peer->channel);
    uint32_t acknowledged = qa_unified_channel_acknowledged(peer->channel);
    if (!qa_unified_channel_receive(peer->channel, packet->payload, packet->received_ns, delivery, peer, error)) return false;
    if (qa_unified_channel_received(peer->channel) != previous ||
        qa_unified_channel_acknowledged(peer->channel) != acknowledged)
        return qa_network_received(runtime, id, packet->received_ns, error);
    return true;
}
static bool send(void *context, qa_bytes bytes, qa_error *error) {
    unified_peer *peer = context;
    return qa_network_send(peer->runtime, peer->id, bytes, error);
}
static bool flush(void *context, qa_network_runtime *runtime, qa_net_client_id id, uint64_t now, qa_error *error) {
    unified_peer *peer = context; peer->runtime = runtime; peer->id = id;
    size_t sent;
    return qa_unified_channel_flush(peer->channel, now, send, peer, &sent, error);
}
static qa_unified_vec3 angles(qa_vec3 v) { return (qa_unified_vec3){v.x, v.y, v.z}; }
static bool command(void *context, const qa_network_command *source, qa_error *error) {
    unified_peer *peer = context;
    const qa_movement_command *c = &source->movement;
    qa_unified_command value = {.actor = source->actor, .sequence = c->sequence,
        .has_arsenal = source->has_arsenal, .arsenal = source->arsenal};
    value.movement.kind = c->kind;
    switch (c->kind) {
    case QA_MOVEMENT_NETQUAKE:
        value.movement.data.nq.acknowledged_seconds = c->acknowledged_server_seconds;
        value.movement.data.nq.angles = angles(c->angles);
        value.movement.data.nq.forward = c->forward_move; value.movement.data.nq.side = c->side_move;
        value.movement.data.nq.up = c->up_move; value.movement.data.nq.buttons = c->buttons;
        value.movement.data.nq.impulse = c->impulse; break;
    case QA_MOVEMENT_QUAKEWORLD:
        value.movement.data.qw.milliseconds = c->milliseconds; value.movement.data.qw.angles = angles(c->angles);
        value.movement.data.qw.forward = c->forward_move; value.movement.data.qw.side = c->side_move;
        value.movement.data.qw.up = c->up_move; value.movement.data.qw.buttons = c->buttons;
        value.movement.data.qw.impulse = c->impulse; break;
    case QA_MOVEMENT_Q2_CLASSIC:
        value.movement.data.q2.milliseconds = c->milliseconds;
        for (size_t i = 0; i < 3; ++i) value.movement.data.q2.angle_shorts[i] = c->angle_words[i];
        value.movement.data.q2.forward = c->forward_move; value.movement.data.q2.side = c->side_move;
        value.movement.data.q2.up = c->up_move; value.movement.data.q2.buttons = c->buttons;
        value.movement.data.q2.impulse = c->impulse; value.movement.data.q2.light_level = c->light_level; break;
    case QA_MOVEMENT_Q2_RERELEASE:
        value.movement.data.q2r.milliseconds = c->milliseconds; value.movement.data.q2r.angles = angles(c->angles);
        value.movement.data.q2r.forward = c->forward_move; value.movement.data.q2r.side = c->side_move;
        value.movement.data.q2r.buttons = c->buttons; value.movement.data.q2r.server_frame = c->server_frame; break;
    case QA_MOVEMENT_Q3:
        value.movement.data.q3.server_time_ms = c->server_time_ms;
        for (size_t i = 0; i < 3; ++i) value.movement.data.q3.angle_words[i] = c->angle_words[i];
        value.movement.data.q3.forward = c->forward_move; value.movement.data.q3.right = c->side_move;
        value.movement.data.q3.up = c->up_move; value.movement.data.q3.buttons = c->buttons;
        value.movement.data.q3.weapon = c->weapon; break;
    default: return qa_network_fail(error, "Unknown command movement provider");
    }
    qa_buffer encoded = {0};
    if (!qa_unified_command_encode(&value, &encoded, error)) return false;
    /* Commands must not overwrite one another in the single frame slot. */
    bool ok = qa_unified_channel_reliable(peer->channel, (qa_bytes){encoded.data, encoded.size}, NULL, error);
    qa_buffer_free(&encoded); return ok;
}
static bool restart(void *context, uint64_t epoch, const qa_sha256_digest *digest, qa_error *error) {
    unified_peer *peer = context;
    return peer->hooks.restart(peer->hooks.context, epoch, digest, peer->channel, error);
}
static bool rebind(void *context, const qa_net_address *endpoint, qa_error *error) {
    (void)context; (void)endpoint; (void)error; return true;
}
static void close_peer(void *context) {
    unified_peer *peer = context; qa_unified_channel_destroy(peer->channel); free(peer);
}
static const qa_network_peer_ops unified_ops = {.receive=receive,.flush=flush,.command=command,
    .restart=restart,.rebind=rebind,.close=close_peer};
bool qa_network_attach_unified(qa_network_runtime *runtime, const qa_net_connect *request,
                                qa_unified_token token, const qa_unified_limits *limits,
                                const qa_network_unified_hooks *hooks, uint64_t now,
                                qa_net_client_id *out, qa_error *error) {
    if (!request || request->protocol.kind != QA_NET_UNIFIED_1 || !hooks ||
        !hooks->resolve || !hooks->delivery || !hooks->restart)
        return qa_network_fail(error, "Missing unified identity/state owner");
    unified_peer *peer = calloc(1, sizeof(*peer));
    if (!peer) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating unified peer"); return false; }
    peer->token = token; peer->hooks = *hooks;
    if (!qa_unified_channel_create(token, limits, &peer->channel, error)) { free(peer); return false; }
    if (!qa_network_attach(runtime, request, &unified_ops, peer, now, out, error)) { close_peer(peer); return false; }
    peer->runtime = runtime; peer->id = *out; return true;
}
static unified_peer *unified_get(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error) {
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer) return NULL;
    if (peer->ops.receive != receive) { qa_network_fail(error, "Connection is not a unified peer"); return NULL; }
    return peer->state;
}
bool qa_network_unified_reliable(qa_network_runtime *runtime, qa_net_client_id id, qa_bytes bytes,
                                  uint32_t *sequence, qa_error *error) {
    unified_peer *peer = unified_get(runtime, id, error);
    return peer && qa_unified_channel_reliable(peer->channel, bytes, sequence, error);
}
bool qa_network_unified_frame(qa_network_runtime *runtime, qa_net_client_id id, qa_bytes bytes,
                               uint32_t required, qa_error *error) {
    unified_peer *peer = unified_get(runtime, id, error);
    return peer && qa_unified_channel_frame(peer->channel, bytes, required, error);
}
