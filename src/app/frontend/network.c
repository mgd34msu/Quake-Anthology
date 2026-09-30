#include "internal.h"
#include "qa/application_network.h"
#include "qa/downloads.h"
#include "qa/server_browser.h"
#include "qa/server_admin.h"
#include "qa/launch_identity.h"
#include "qa/network_q3_runtime.h"
#include "qa/archive.h"
#include "qa/bsp.h"
#include <inttypes.h>
#include <stdio.h>

#define NETWORK_OWNER UINT64_C(0x71616e6574770001)
typedef struct frontend_q3_peer {
    qa_frontend_network *network;
    qa_net_client_id client;
    qa_net_seat_id seat;
    qa_q3_server_world world;
    qa_q3_server_rate rate;
    qa_q3_product product;
    uint64_t sequence;
    int64_t connected_ms;
    uint32_t slot;
    uint16_t qport;
    bool occupied, retiring;
    char reason[256];
} frontend_q3_peer;
typedef struct frontend_q3_pending {
    qa_net_address address;
    size_t size;
    uint8_t bytes[QA_Q3_MESSAGE_BYTES];
} frontend_q3_pending;
struct qa_frontend_network {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_server_browser *browser;
    qa_server_admin *admin;
    qa_downloads *downloads;
    qa_fs_root *preferences, *content;
    uint64_t nonce;
    uint32_t rotation_random;
    qa_q3_server_admission *q3_admission;
    frontend_q3_peer q3_peers[64];
    frontend_q3_pending q3_pending[32];
    size_t q3_pending_count;
    uint64_t q3_generation;
    int32_t q3_server_id, q3_checksum_feed;
    uint8_t q3_server_bit;
    qa_sha256_digest composition;
    const qa_q3_accepted_connect *q3_reconnect;
    unsigned busy;
    bool registered;
};
static const char *const names[] = {"serverlist", "serverquery", "serverfavorite", "servermaster",
    "addip", "removeip", "heartbeat", "maprotation", "nextmap", "download", "downloadstatus", "downloadcancel", "downloadsuspend"};
static bool send_address(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *error)
{ qa_frontend_network *n = context; return qa_network_send_address(n->runtime, to, bytes, error); }
static bool local_address(void *context, const qa_net_address *address)
{
    (void)context;
    /* Private address classes do not prove interface locality. The transport
     * interface/netmask producer must explicitly qualify physical LAN peers. */
    return address->kind == QA_NET_LOOPBACK ||
        (address->kind == QA_NET_IPV4 && address->host.ipv4[0] == 127);
}
static bool admit(void *context, const qa_net_connect *request, qa_error *error)
{
    qa_frontend_network *n = context; qa_buffer identity = {0};
    if (!qa_launch_identity_encode(qa_application_launch(n->frontend->application),
        qa_session_actors(qa_application_session(n->frontend->application)), &identity, error)) return false;
    qa_sha256_digest digest; qa_sha256((qa_bytes){identity.data, identity.size}, &digest); qa_buffer_free(&identity);
    if (!qa_sha256_equal(&digest, &request->composition))
        return frontend_fail(error, QA_ERROR_FORMAT, "remote launch identity differs from the complete selected composition");
    if (n->q3_admission && request->protocol.kind == QA_NET_Q3_68) {
        qa_actor_id actor; uint32_t slot; qa_q3_product product;
        if (!qa_application_player_actor(n->frontend->application, 0, &actor))
            return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 source admission needs a configured canonical player template");
        return qa_application_network_q3_source(n->frontend->application, actor, &slot, &product, error);
    }
    return frontend_fail(error, QA_ERROR_UNSUPPORTED, "remote signon/full-state producer is not bound to this frontend");
}
static bool controlled(void *context, qa_net_client_id client, qa_net_seat_id seat,
    qa_actor_id actor, qa_movement_kind movement, qa_bytes arsenal, qa_error *error)
{
    qa_frontend_network *n = context;
    return qa_application_network_controlled(n->frontend->application, client, seat, actor, movement, arsenal, error);
}
static bool remote_command(void *context, const qa_network_command *command, qa_error *error)
{ return qa_application_network_command(((qa_frontend_network *)context)->frontend->application, command, error); }
static bool reconnect(void *context, const qa_net_client *client, const qa_net_address *address, qa_bytes proof, qa_error *error)
{
    qa_frontend_network *n = context;
    const qa_q3_accepted_connect *source = n->q3_reconnect;
    if (!source || source->slot >= 64 || proof.size != 6 ||
        !n->q3_peers[source->slot].occupied || !qa_net_client_id_equal(n->q3_peers[source->slot].client, client->id) ||
        !qa_net_address_equal(&source->address, address, true) ||
        qa_load_u32le(proof.data) != (uint32_t)source->challenge || qa_load_u16le(proof.data + 4) != source->qport)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Reconnect lacks current original challenge admission");
    return true;
}
static void disconnected(void *context, qa_net_client_id id, const char *reason)
{
    qa_frontend_network *n = context; (void)reason;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(n->runtime), id);
    qa_error error = {0};
    if (client && !qa_application_network_detach(n->frontend->application, client, &error))
        frontend_print(n->frontend, error.message);
    for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied && qa_net_client_id_equal(n->q3_peers[i].client, id)) {
        if (n->q3_admission && client) qa_q3_server_admission_disconnect(n->q3_admission, &client->endpoint);
        n->q3_peers[i] = (frontend_q3_peer){0};
    }
}
static bool connectionless(void *context, qa_network_runtime *runtime,
    const qa_net_datagram *packet, qa_error *error)
{
    qa_frontend_network *n = context; (void)runtime;
    bool recognized;
    if (!qa_server_browser_receive(n->browser, packet, &recognized, error)) return false;
    if (recognized) return true;
    qa_admin_result result;
    if (!qa_server_admin_receive(n->admin, packet, &result, error)) return false;
    if (result != QA_ADMIN_IGNORED || !n->q3_admission || qa_server_admin_rejects(n->admin, &packet->from)) return true;
    if (packet->payload.size < 4 || qa_load_u32le(packet->payload.data) != UINT32_MAX) return true;
    if (packet->payload.size > QA_Q3_MESSAGE_BYTES || n->q3_pending_count == 32) return true;
    frontend_q3_pending *pending = &n->q3_pending[n->q3_pending_count++];
    pending->address = packet->from; pending->size = packet->payload.size;
    memcpy(pending->bytes, packet->payload.data, pending->size); return true;
}
static const char *password(void *context, bool limited)
{
    qa_frontend_network *n = context;
    const qa_cvar_view *v = qa_cvars_find(qa_application_cvars(n->frontend->application),
        limited ? "rcon_limited_password" : "rcon_password");
    return v ? v->value : "";
}
typedef struct captured_output { qa_admin_write_fn write; void *context; qa_error error; } captured_output;
static void captured_print(void *context, const qa_command_context *source, const char *text)
{
    captured_output *out = context; (void)source;
    if (!out->error.code) (void)out->write(out->context, text, &out->error);
}
static bool admin_execute(void *context, const qa_net_address *from, const char *text, bool limited,
    qa_admin_write_fn write, void *output, qa_error *error)
{
    qa_frontend_network *n = context; (void)from; (void)limited;
    qa_command_context command = {.origin = QA_COMMAND_REMOTE, .direct = true, .console_text = true,
        .dialect = qa_cvars_dialect(qa_application_cvars(n->frontend->application))};
    if (!qa_application_capture_command_context(n->frontend->application, &command, &command, error)) return false;
    captured_output capture = {.write = write, .context = output};
    bool ok = qa_console_execute_capture(qa_application_console(n->frontend->application), &command,
        text, captured_print, &capture, error);
    if (capture.error.code) { if (error) *error = capture.error; return false; }
    return ok;
}
static bool travel(void *context, const char *map, qa_error *error)
{
    qa_frontend_network *n = context;
    return qa_application_queue_travel(n->frontend->application,
        &(qa_application_travel_request){.expression = map, .carry_players = true}, error);
}
static uint32_t player_count(void *context)
{
    qa_frontend_network *n = context; uint32_t cursor = 0, count = 0; const qa_net_client *client;
    while (qa_net_connections_next(qa_network_connections(n->runtime), &cursor, &client))
        if (client->phase == QA_NET_ACTIVE) count += (uint32_t)client->seat_count;
    return count;
}
static uint32_t random_rotation(void *context)
{
    qa_frontend_network *n = context;
    n->rotation_random = n->rotation_random * UINT32_C(1664525) + UINT32_C(1013904223);
    return n->rotation_random;
}
static bool q3_actor(frontend_q3_peer *peer, qa_actor_id *actor, qa_error *error)
{
    return qa_application_remote_player_actor(peer->network->frontend->application, peer->client, peer->seat, actor) ||
        frontend_fail(error, QA_ERROR_NOT_FOUND, "Q3 peer no longer owns a canonical player");
}
static qa_q3_server_world q3_world(void *context)
{ return ((frontend_q3_peer *)context)->world; }
static bool q3_signon(void *context, qa_error *error)
{
    frontend_q3_peer *peer = context; qa_frontend_network *n = peer->network; qa_actor_id actor;
    qa_q3_gamestate *state = malloc(sizeof(*state));
    if (!state) return frontend_fail(error, QA_ERROR_MEMORY, "allocating original Q3 signon observation");
    bool ok = q3_actor(peer, &actor, error) && qa_application_network_q3_signon(n->frontend->application, actor,
        n->q3_server_id, n->q3_checksum_feed, state, &peer->world, error) &&
        qa_network_q3_gamestate(n->runtime, peer->client, state, &peer->rate, error);
    free(state); return ok;
}
static bool q3_snapshot(frontend_q3_peer *peer, qa_error *error)
{
    qa_actor_id actor; qa_frontend_network *n = peer->network;
    qa_application_network_q3_frame frame;
    return q3_actor(peer, &actor, error) && qa_application_network_q3_snapshot(n->frontend->application,
        actor, 0, 0, n->q3_server_bit, &frame, error) &&
        qa_network_q3_snapshot(n->runtime, peer->client, &frame.snapshot, &peer->rate, NULL, 0, error);
}
static bool q3_rejected_snapshot(void *context, qa_error *error)
{ return q3_snapshot(context, error); }
static bool q3_drop(void *context, const char *reason, qa_error *error)
{
    frontend_q3_peer *peer = context; (void)error;
    peer->retiring = true; snprintf(peer->reason, sizeof(peer->reason), "%s", reason); return true;
}
static bool q3_input(void *context, const qa_q3_usercmd *source, qa_error *error)
{
    frontend_q3_peer *peer = context; qa_frontend_network *n = peer->network;
    if (peer->retiring) return true;
    if (peer->sequence == UINT64_MAX) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 command sequence exhausted");
    qa_actor_id actor;
    if (!q3_actor(peer, &actor, error)) return false;
    qa_network_command command = {.client = peer->client, .seat = peer->seat, .actor = actor,
        .epoch = qa_network_epoch(n->runtime, peer->client), .movement = {.kind = QA_MOVEMENT_Q3,
        .sequence = peer->sequence + 1, .server_time_ms = source->serverTime,
        .forward_move = source->forwardmove, .side_move = source->rightmove, .up_move = source->upmove,
        .buttons = (uint32_t)source->buttons, .weapon = source->weapon}};
    memcpy(command.movement.angle_words, source->angles, sizeof(source->angles));
    if (!qa_network_accept(n->runtime, &command, error)) return false;
    ++peer->sequence; return true;
}
static bool q3_enter(void *context, const qa_q3_usercmd *command, qa_error *error)
{
    frontend_q3_peer *peer = context; (void)command;
    if (peer->retiring) return true;
    /* ClientBegin belongs to the canonical roster. The source peer remembers
     * this first command and applies later commands with original time gating. */
    return qa_application_remote_player_begin(peer->network->frontend->application, peer->client, peer->seat, error);
}
static bool q3_client_command(void *context, const qa_q3_command *command, bool allowed, qa_error *error)
{
    frontend_q3_peer *peer = context; qa_frontend_network *n = peer->network;
    if (peer->retiring) return true;
    qa_q3_tokens tokens;
    if (!qa_q3_tokenize(command->text, &tokens, error)) return false;
    const char *name = qa_q3_token(&tokens, 0);
    if (!strcmp(name, "disconnect")) return q3_drop(peer, "disconnected", error);
    if (!strcmp(name, "cp")) {
        qa_q3_pure_result result;
        /* Admission rejects pure hosting until actual package metadata exists. */
        return qa_network_q3_pure(n->runtime, peer->client, &(qa_q3_pure_server){0}, &tokens, &result, error);
    }
    if (!strcmp(name, "vdr") || !strcmp(name, "stopdl") || !strcmp(name, "nextdl")) return true;
    if (!strcmp(name, "donedl")) {
        qa_q3_server_state state;
        return qa_network_q3_state(n->runtime, peer->client, &state, error) &&
            (state.phase == QA_Q3_ACTIVE || q3_signon(peer, error));
    }
    if (!strcmp(name, "download")) {
        qa_q3_download denied = {.file_size = -1};
        snprintf(denied.error, sizeof(denied.error), "Native package download admission is unavailable");
        qa_q3_snapshot snapshot = {.player = {.product = peer->product}};
        return qa_network_q3_snapshot(n->runtime, peer->client, &snapshot, &peer->rate, &denied, 1, error);
    }
    qa_actor_id actor;
    if (!q3_actor(peer, &actor, error)) return false;
    if (!strcmp(name, "userinfo"))
        return qa_application_network_q3_userinfo(n->frontend->application, actor, qa_q3_token(&tokens, 1), error);
    return !allowed || qa_application_actor_command(n->frontend->application, actor, command->text, error);
}
static bool q3_admit(void *context, const qa_q3_accepted_connect *request, char rejection[1024], qa_error *error)
{
    qa_frontend_network *n = context;
    if (request->slot >= 64) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 admission selected an invalid source slot");
    frontend_q3_peer *peer = &n->q3_peers[request->slot];
    bool retained = peer->occupied;
    qa_net_client_id retained_id = peer->client;
    if (retained) {
        uint8_t proof[6]; qa_store_u32le(proof, (uint32_t)request->challenge); qa_store_u16le(proof + 4, request->qport);
        n->q3_reconnect = request;
        bool ok = qa_network_reconnect(n->runtime, retained_id, &request->address, (qa_bytes){proof, sizeof(proof)}, n->frontend->time_ns, error);
        n->q3_reconnect = NULL;
        if (!ok) return false;
        peer->retiring = true;
        if (!qa_application_remote_player_detach(n->frontend->application, retained_id, peer->seat, error) ||
            !qa_network_q3_reconnect_channel(n->runtime, retained_id, request->challenge, request->qport, error)) {
            (void)qa_network_detach(n->runtime, retained_id, "reconnect source reset failed", NULL); return false;
        }
    }
    *peer = (frontend_q3_peer){.network = n, .slot = request->slot, .qport = request->qport,
        .seat = {NETWORK_OWNER, 64u + request->slot}, .connected_ms = (int64_t)(n->frontend->time_ns / UINT64_C(1000000)),
        .rate = {.bytes_per_second = 3000, .snapshot_ms = 50}};
    qa_actor_id template;
    uint32_t slot;
    if (!qa_application_player_actor(n->frontend->application, 0, &template) ||
        !qa_application_network_q3_source(n->frontend->application, template, &slot, &peer->product, error)) return false;
    qa_net_seat_binding seat = {peer->seat, 0};
    qa_net_connect connect = {.attachment = QA_NET_REMOTE, .endpoint = request->address,
        .protocol = {QA_NET_Q3_68, 0, 0}, .seats = &seat, .seat_count = 1, .composition = n->composition};
    qa_q3_server_hooks hooks = {.context = peer, .world = q3_world, .command = q3_client_command,
        .enter_world = q3_enter, .think = q3_input, .resend_gamestate = q3_signon,
        .pure_rejected_snapshot = q3_rejected_snapshot, .drop = q3_drop};
    if (retained) peer->client = retained_id;
    else if (!qa_network_attach_q3_server(n->runtime, &connect, peer->product, request->challenge,
        request->qport, &hooks, n->frontend->time_ns, &peer->client, error)) return false;
    peer->occupied = true;
    char name[1024], team[1024], skin[1024]; qa_actor_id actor;
    bool ok = qa_q3_info_value(request->userinfo, "name", name, sizeof(name), error) &&
        qa_q3_info_value(request->userinfo, "team", team, sizeof(team), error) &&
        qa_q3_info_value(request->userinfo, "model", skin, sizeof(skin), error);
    qa_application_remote_player_request player = {.client = peer->client, .seat = peer->seat,
        .application_seat = peer->seat.index, .source_slot = request->slot, .userinfo = request->userinfo,
        .name = name, .team = team, .skin = skin, .defer_source_begin = true};
    if (ok) ok = qa_application_remote_player_attach(n->frontend->application, &player, &actor, error);
    if (ok) ok = qa_application_network_q3_world(n->frontend->application, actor, n->q3_server_id,
        n->q3_server_id, n->q3_checksum_feed, &peer->world, error);
    if (ok && peer->world.pure) ok = frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 pure package-reference producer is unavailable");
    if (ok && retained) ok = qa_network_restart(n->runtime, peer->client, &n->composition, error);
    if (!ok) {
        qa_error rejected = error ? *error : (qa_error){0};
        (void)qa_network_detach(n->runtime, peer->client, "source admission rejected", NULL);
        snprintf(rejection, 1024, "%s", rejected.message);
        if (qa_application_get_state(n->frontend->application) == QA_APPLICATION_FAULTED) return false;
        if (error) *error = (qa_error){0}; return true;
    }
    char value[64];
    if (!qa_q3_info_value(request->userinfo, "rate", value, sizeof(value), error)) return false;
    if (*value) { long rate = strtol(value, NULL, 10); peer->rate.bytes_per_second = rate < 1000 ? 1000 : rate > 90000 ? 90000 : (uint32_t)rate; }
    peer->rate.lan = qa_q3_is_lan(&request->address); return true;
}
static bool q3_query(void *context, const qa_net_address *address, const qa_q3_connectionless *packet, qa_error *error)
{
    qa_frontend_network *n = context;
    const char *command = qa_q3_token(&packet->tokens, 0);
    if (strcmp(command, "getinfo") && strcmp(command, "getstatus")) return true;
    bool status = !strcmp(command, "getstatus");
    qa_actor_id actor; qa_application_network_q3_status_player players[64]; size_t count;
    if (!qa_application_player_actor(n->frontend->application, 0, &actor) ||
        !qa_application_network_q3_status(n->frontend->application, actor, players, &count, error)) return false;
    qa_cvars *cvars = qa_application_network_q3_cvars(n->frontend->application, actor);
    if (!cvars) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 query source cvars are unavailable");
    const qa_cvar_view *mode = qa_cvars_find(cvars, "g_gametype"), *single = qa_cvars_find(cvars, "ui_singlePlayerActive");
    if ((mode && mode->integer == 2) || (!status && single && single->integer)) return true;
    qa_buffer fields = {0}; char info[8192] = {0};
    if (status && !qa_cvars_info(cvars, QA_CVAR_SERVERINFO, 8192, &fields, error)) return false;
    if (fields.data) { memcpy(info, fields.data, fields.size + 1); qa_buffer_free(&fields); }
    qa_application_map_view map;
    if (!qa_application_map_read(n->frontend->application, &map)) return true;
    char clients[32]; snprintf(clients, sizeof(clients), "%zu", count);
    const qa_cvar_view *hostname = qa_cvars_find(cvars, "sv_hostname");
    if (!qa_q3_info_set(info, sizeof(info), "challenge", qa_q3_token(&packet->tokens, 1), error) ||
        !qa_q3_info_set(info, sizeof(info), "protocol", "68", error) ||
        !qa_q3_info_set(info, sizeof(info), "hostname", hostname ? hostname->value : "Quake Anthology", error) ||
        !qa_q3_info_set(info, sizeof(info), "mapname", map.name, error) ||
        !qa_q3_info_set(info, sizeof(info), "clients", clients, error)) return false;
    static const char *const keys[] = {"sv_maxclients", "gametype", "pure", "minPing", "maxPing", "game"};
    static const char *const sources[] = {"sv_maxclients", "g_gametype", "sv_pure", "sv_minPing", "sv_maxPing", "fs_game"};
    for (size_t i = 0; !status && i < sizeof(keys) / sizeof(*keys); ++i) {
        const qa_cvar_view *value = qa_cvars_find(cvars, sources[i]);
        if (!qa_q3_info_set(info, sizeof(info), keys[i], value ? value->value : "0", error)) return false;
    }
    char line[QA_Q3_MESSAGE_BYTES];
    int length = snprintf(line, sizeof(line), "%s\n%s\n", status ? "statusResponse" : "infoResponse", info);
    if (length < 0 || (size_t)length >= sizeof(line)) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 query response exceeds source capacity");
    size_t used = (size_t)length;
    for (size_t i = 0; status && i < count; ++i) {
        char name[1024], row[1152];
        if (!qa_q3_info_value(players[i].userinfo, "name", name, sizeof(name), error)) return false;
        for (char *p = name; *p; ++p) if (*p == '"' || *p == '\r' || *p == '\n') *p = ' ';
        length = snprintf(row, sizeof(row), "%d %d \"%s\"\n", players[i].score, players[i].ping, name);
        if (length < 0 || (size_t)length >= sizeof(row)) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 status row exceeds source capacity");
        if ((size_t)length >= sizeof(line) - used) break;
        memcpy(line + used, row, (size_t)length); used += (size_t)length; line[used] = 0;
    }
    qa_buffer wire = {0};
    bool ok = qa_q3_connectionless_encode(line, &wire, error) && send_address(n, address, (qa_bytes){wire.data, wire.size}, error);
    qa_buffer_free(&wire); return ok;
}
static bool q3_slots(qa_frontend_network *n, qa_q3_admission_slot slots[64], size_t *count, qa_error *error)
{
    qa_actor_id actor; bool occupied[64];
    if (!qa_application_player_actor(n->frontend->application, 0, &actor) ||
        !qa_application_network_q3_slots(n->frontend->application, actor, occupied, error)) return false;
    qa_cvars *cvars = qa_application_network_q3_cvars(n->frontend->application, actor);
    const qa_cvar_view *maximum = qa_cvars_find(cvars, "sv_maxclients");
    *count = maximum && maximum->integer > 0 ? (size_t)maximum->integer : 8;
    if (*count > 64) *count = 64;
    for (size_t i = 0; i < *count; ++i) {
        frontend_q3_peer *peer = &n->q3_peers[i];
        slots[i] = (qa_q3_admission_slot){.slot = (uint32_t)i, .phase = occupied[i] ? QA_Q3_ACTIVE : QA_Q3_FREE};
        if (!peer->occupied) continue;
        qa_q3_server_state state;
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(n->runtime), peer->client);
        if (!client || !qa_network_q3_state(n->runtime, peer->client, &state, error)) return false;
        slots[i].phase = state.phase; slots[i].address = client->endpoint;
        slots[i].qport = peer->qport; slots[i].last_connect_time = peer->connected_ms;
    }
    return true;
}
static bool q3_drain(qa_frontend_network *n, qa_error *error)
{
    for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied && n->q3_peers[i].retiring)
        if (!qa_network_detach(n->runtime, n->q3_peers[i].client, n->q3_peers[i].reason, error)) return false;
    size_t pending = n->q3_pending_count; n->q3_pending_count = 0;
    for (size_t i = 0; i < pending; ++i) {
        qa_q3_admission_slot slots[64]; size_t count;
        if (!q3_slots(n, slots, &count, error)) return false;
        frontend_q3_pending *packet = &n->q3_pending[i]; qa_error local = {0};
        qa_actor_id template; qa_cvars *cvars = NULL;
        if (qa_application_player_actor(n->frontend->application, 0, &template))
            cvars = qa_application_network_q3_cvars(n->frontend->application, template);
        const qa_cvar_view *private_clients = qa_cvars_find(cvars, "sv_privateClients"),
            *private_password = qa_cvars_find(cvars, "sv_privatePassword"), *reconnect = qa_cvars_find(cvars, "sv_reconnectlimit"),
            *minimum = qa_cvars_find(cvars, "sv_minPing"), *maximum = qa_cvars_find(cvars, "sv_maxPing");
        qa_q3_admission_options options = {.private_clients = private_clients && private_clients->integer > 0 ? (uint32_t)private_clients->integer : 0,
            .private_password = private_password ? private_password->value : "",
            .reconnect_limit_seconds = reconnect ? reconnect->integer : 3,
            .minimum_ping = minimum ? minimum->integer : 0, .maximum_ping = maximum ? maximum->integer : 0};
        if (!qa_q3_server_admission_receive(n->q3_admission, &options, slots, count, &packet->address,
            (qa_bytes){packet->bytes, packet->size}, (int64_t)(n->frontend->time_ns / UINT64_C(1000000)), &local)) {
            if (qa_application_get_state(n->frontend->application) == QA_APPLICATION_FAULTED) { if (error) *error = local; return false; }
            frontend_print(n->frontend, local.message);
        }
    }
    return true;
}
static bool q3_prepare(qa_frontend_network *n, qa_error *error)
{
    if (!n->q3_admission) return true;
    uint64_t generation = qa_application_configuration_generation(n->frontend->application);
    bool changed = generation != n->q3_generation;
    if (changed) {
        if (n->q3_server_id == INT32_MAX) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 server identity exhausted");
        ++n->q3_server_id; n->q3_server_bit ^= 4u;
    }
    qa_buffer identity = {0};
    if (!qa_launch_identity_encode(qa_application_launch(n->frontend->application),
        qa_session_actors(qa_application_session(n->frontend->application)), &identity, error)) return false;
    qa_sha256((qa_bytes){identity.data, identity.size}, &n->composition); qa_buffer_free(&identity);
    n->q3_generation = generation;
    for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied && !n->q3_peers[i].retiring) {
        frontend_q3_peer *peer = &n->q3_peers[i]; qa_actor_id actor;
        if (!q3_actor(peer, &actor, error) || !qa_application_network_q3_world(n->frontend->application, actor,
            n->q3_server_id, n->q3_server_id, n->q3_checksum_feed, &peer->world, error)) return false;
        if (changed) {
            peer->sequence = 0;
            if (!qa_network_restart(n->runtime, peer->client, &n->composition, error)) return false;
        }
    }
    return true;
}
static bool save_favorites(qa_frontend_network *n, qa_error *error)
{
    qa_buffer bytes = {0}; bool created;
    if (!qa_server_browser_save(n->browser, &bytes, error)) return false;
    bool ok = qa_fs_root_publish(n->preferences, "network/favorites.bin", (qa_bytes){bytes.data, bytes.size},
        ++n->nonce, false, true, &created, error);
    qa_buffer_free(&bytes); return ok;
}
static bool download_permit(void *context, const qa_download_request *request, const char *url, qa_error *error)
{
    (void)context;
    if (!request->exact_identity || !url || request->maximum_bytes > UINT64_C(2147483648))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "download requires explicit URL, exact content digest and bounded size");
    qa_archive_kind kind = qa_archive_kind_for_path(request->path);
    const char *extension = strrchr(request->path, '.');
    return kind != QA_ARCHIVE_AUTO || (extension && !strcmp(extension, ".bsp")) ||
        frontend_fail(error, QA_ERROR_UNSUPPORTED, "download inspection supports installed packages and maps");
}
static bool download_inspect(void *context, const char *path, qa_fs_stage *stage, uint64_t size, qa_error *error)
{
    (void)context; qa_fs_stage_mapping *mapping = NULL;
    if (!qa_fs_stage_map(stage, &mapping, error)) return false;
    qa_bytes bytes = qa_fs_stage_mapping_bytes(mapping); bool ok = bytes.size == size;
    qa_archive_kind kind = qa_archive_kind_for_path(path);
    if (ok && kind != QA_ARCHIVE_AUTO) {
        qa_archive *archive = NULL; ok = qa_archive_open_memory(bytes, kind, &archive, error);
        if (ok) {
            for (size_t i = 0; ok && i < qa_archive_count(archive); ++i) {
                const qa_archive_entry *entry = qa_archive_entry_at(archive, i);
                /* Package execution remains subject to catalog/module admission;
                 * this boundary admits only valid contained resource names. */
                ok = entry && entry->path && *entry->path;
                if (!ok) frontend_fail(error, QA_ERROR_FORMAT, "package contains an invalid resource member");
            }
        }
        qa_archive_close(archive);
    } else if (ok) {
        qa_bsp_view map; ok = qa_bsp_open(bytes, &map, error) && qa_bsp_validate(&map, error);
    }
    qa_fs_stage_unmap(mapping);
    return ok || (error && error->code ? false : frontend_fail(error, QA_ERROR_FORMAT, "staged inspection size changed"));
}
static bool download_remount(void *context, const char *path, const qa_sha256_digest *digest, qa_error *error)
{
    qa_frontend_network *n = context; (void)path; (void)digest;
    qa_application *application = n->frontend->application;
    if (!qa_application_rediscover(application, n->frontend->options.application.discover_mods, error)) return false;
    const qa_launch_snapshot *snapshot = qa_application_launch(application);
    if (!snapshot) return true;
    qa_launch_draft *current = NULL, *rebased = NULL;
    bool ok = qa_launch_snapshot_draft_copy(snapshot, &current, error) &&
        qa_launch_draft_rebase(current, qa_application_catalog(application), &rebased, error) &&
        qa_application_apply(application, rebased, error);
    qa_launch_draft_destroy(rebased); qa_launch_draft_destroy(current); return ok;
}
static bool downloads_ready(qa_frontend_network *n, qa_error *error)
{
    if (n->downloads) return true;
    if (!n->content && !qa_fs_root_open(n->frontend->options.application.content_root, &n->content, error)) return false;
    qa_download_options options = {.jobs = 4, .maximum_pending_bytes = UINT64_C(4294967296),
        .hooks = {.context = n, .permit = download_permit, .inspect = download_inspect, .remount = download_remount}};
    return qa_downloads_create(frontend_tools_http(n->frontend), n->content, &options, &n->downloads, error);
}
static bool unsigned_text(const char *text, uint64_t *out, qa_error *error)
{
    uint64_t value = 0;
    if (!text || !*text) return frontend_fail(error, QA_ERROR_ARGUMENT, "expected an unsigned decimal integer");
    for (; *text; ++text) {
        unsigned digit = (unsigned)(*text - '0');
        if (digit > 9 || value > (UINT64_MAX - digit) / 10)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "unsigned decimal integer is out of range");
        value = value * 10 + digit;
    }
    *out = value; return true;
}
static void emit(const qa_command_invocation *command, const char *text)
{ qa_console_emit(command->console, &command->context, text); }
static bool command(void *context, const qa_command_invocation *call, qa_error *error)
{
    qa_frontend_network *n = context; const char *name = call->argv[0];
    if (!strcmp(name, "serverlist")) {
        uint32_t indices[2048]; size_t count;
        qa_browser_filter filter = {.text = call->argc > 1 ? call->argv[1] : NULL, .sort = QA_BROWSER_PING};
        if (!qa_server_browser_list(n->browser, &filter, indices, 2048, &count, error)) return false;
        for (size_t i = 0; i < count && i < 2048; ++i) {
            qa_server_entry entry; char address[256], line[1536];
            if (!qa_server_browser_at(n->browser, indices[i], &entry) || !qa_net_address_format(&entry.address, address, sizeof(address), error)) return false;
            snprintf(line, sizeof(line), "%s %u/%u %" PRIu64 "ms %s %s\n", address, entry.players, entry.maximum_players,
                entry.ping_ns / UINT64_C(1000000), entry.name, entry.map); emit(call, line);
        }
        return true;
    }
    if (!strcmp(name, "serverquery") || !strcmp(name, "serverfavorite") || !strcmp(name, "servermaster")) {
        if (call->argc < 2 || call->argc > 3) return frontend_fail(error, QA_ERROR_ARGUMENT, "usage: serverquery/serverfavorite/servermaster address [protocol]");
        qa_net_protocol_id protocol = n->frontend->options.network_protocol;
        if (call->argc == 3 && !frontend_protocol(call->argv[2], &protocol, error)) return false;
        if (!strcmp(name, "servermaster") && (strstr(call->argv[1], "http://") == call->argv[1] || strstr(call->argv[1], "https://") == call->argv[1]))
            return qa_server_browser_master_http(n->browser, call->argv[1], protocol, n->frontend->time_ns, error);
        qa_net_address address;
        if (!qa_net_address_resolve(call->argv[1], n->frontend->options.network_port, 0, &address, error)) return false;
        if (!strcmp(name, "serverfavorite")) return qa_server_browser_add(n->browser, &address, protocol, QA_SERVER_FAVORITE, error) && save_favorites(n, error);
        if (!strcmp(name, "servermaster")) return qa_server_browser_master_udp(n->browser, &address, protocol,
            n->frontend->time_ns, UINT64_C(5000000000), error);
        return qa_server_browser_query(n->browser, &address, protocol, false, n->frontend->time_ns, UINT64_C(5000000000), error);
    }
    if (!strcmp(name, "addip") || !strcmp(name, "removeip"))
        return call->argc == 2 ? qa_server_admin_filter(n->admin, call->argv[1], !strcmp(name, "removeip"), error) :
            frontend_fail(error, QA_ERROR_ARGUMENT, "usage: addip/removeip address-mask");
    if (!strcmp(name, "heartbeat")) return qa_server_admin_tick(n->admin, n->frontend->time_ns, true, error);
    if (!strcmp(name, "maprotation")) return qa_server_admin_rotation(n->admin, call->argv + 1, call->argc - 1, false, error);
    if (!strcmp(name, "nextmap")) {
        qa_application_map_view map; bool rotated;
        return qa_server_admin_next_map(n->admin,
            qa_application_map_read(n->frontend->application, &map) ? map.name : NULL, &rotated, error);
    }
    if (!strcmp(name, "download")) {
        if (call->argc < 5 || call->argc > 6) return frontend_fail(error, QA_ERROR_ARGUMENT, "usage: download path url sha256 bytes [resume-nonce]");
        qa_download_request request = {.path = call->argv[1], .exact_identity = true};
        if (!qa_sha256_parse(call->argv[3], &request.digest, error) || !unsigned_text(call->argv[4], &request.expected_bytes, error)) return false;
        request.maximum_bytes = request.expected_bytes; request.stage_nonce = ++n->nonce;
        if (call->argc == 6) { request.resume = true; if (!unsigned_text(call->argv[5], &request.stage_nonce, error)) return false; }
        qa_download_id id;
        if (!downloads_ready(n, error) || !qa_downloads_begin(n->downloads, &request, call->argv[2], &id, error)) return false;
        char line[128]; snprintf(line, sizeof(line), "download %" PRIu64 " stage %" PRIu64 "\n", id, request.stage_nonce); emit(call, line); return true;
    }
    uint64_t id;
    if (call->argc != 2 || !unsigned_text(call->argv[1], &id, error)) return frontend_fail(error, QA_ERROR_ARGUMENT, "download operation requires a job ID");
    qa_download_view view;
    if (!qa_downloads_view(n->downloads, id, &view)) return frontend_fail(error, QA_ERROR_NOT_FOUND, "download job is absent");
    if (!strcmp(name, "downloadcancel")) { qa_downloads_cancel(n->downloads, id); return true; }
    if (!strcmp(name, "downloadsuspend")) { qa_downloads_suspend(n->downloads, id); return true; }
    char line[768]; snprintf(line, sizeof(line), "%" PRIu64 " %s %" PRIu64 "/%" PRIu64 " state%u published%u mounted%u stage%" PRIu64 " %s\n",
        id, view.path, view.received, view.limit, (unsigned)view.state, view.published, view.mounted, view.stage_nonce, view.failure.message);
    emit(call, line); return true;
}
bool frontend_network_create(qa_frontend *f, qa_error *error)
{
    if (f->network) return true;
    if (f->options.network_connect || (f->options.network_host && f->options.network_protocol.kind != QA_NET_Q3_68))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "selected connection requires its complete original/unified signon and prediction producer");
    qa_frontend_network *n = calloc(1, sizeof(*n));
    if (!n) return frontend_fail(error, QA_ERROR_MEMORY, "allocating network frontend owner");
    n->frontend = f; n->nonce = SDL_GetPerformanceCounter();
    n->rotation_random = (uint32_t)n->nonce ^ (uint32_t)(n->nonce >> 32); f->network = n;
    qa_net_udp_options udp = {.bind = {.kind = QA_NET_IPV4}, .limits = {65507, 256}, .broadcast = true};
    if (f->options.network_host) {
        if (!qa_net_address_parse(f->options.network_host, f->options.network_port, false, &udp.bind, error)) goto failed;
        if (udp.bind.kind != QA_NET_IPV4 && udp.bind.kind != QA_NET_IPV6)
            { frontend_fail(error, QA_ERROR_UNSUPPORTED, "hosting transport requires the admitted UDP address family"); goto failed; }
        udp.ipv6_only = udp.bind.kind == QA_NET_IPV6; udp.broadcast = udp.bind.kind == QA_NET_IPV4;
        qa_q3_admission_hooks hooks = {.context = n, .random = random_rotation, .send = send_address,
            .admit = q3_admit, .query = q3_query};
        if (!qa_q3_server_admission_create(&hooks, &n->q3_admission, error)) goto failed;
        n->q3_server_id = 1; n->q3_checksum_feed = (int32_t)random_rotation(n);
        if (!q3_prepare(n, error)) goto failed;
        qa_actor_id actor; qa_q3_server_world world;
        qa_q3_gamestate *signon = malloc(sizeof(*signon));
        if (!signon) { frontend_fail(error, QA_ERROR_MEMORY, "preparing Q3 host signon"); goto failed; }
        bool ok = qa_application_player_actor(f->application, 0, &actor) &&
            qa_application_network_q3_signon(f->application, actor, n->q3_server_id,
                n->q3_checksum_feed, signon, &world, error);
        free(signon); if (!ok) goto failed;
    }
    qa_net_transport *transport = NULL;
    qa_network_options options = {.owner = NETWORK_OWNER, .clients = 64, .packets_per_pump = 256,
        .timeout_ns = UINT64_C(30000000000), .hooks = {.context = n, .admit = admit, .controlled = controlled,
        .command = remote_command, .disconnected = disconnected, .connectionless = connectionless}};
    options.hooks.reconnect = reconnect;
    if (!qa_net_udp_open(&udp, &transport, error)) goto failed;
    if (!qa_network_create(transport, &options, &n->runtime, error)) { qa_net_transport_close(transport); goto failed; }
    qa_browser_hooks browser = {.context = n, .send = send_address, .local = local_address};
    qa_admin_options admin = {.dialect = qa_cvars_dialect(qa_application_cvars(f->application)),
        .filters = 1024, .rate_entries = 1024, .burst = 10, .rate_interval_ns = UINT64_C(1000000000),
        .heartbeat_interval_ns = UINT64_C(300000000000), .deny_matches = true,
        .hooks = {.context = n, .password = password, .execute = admin_execute, .send = send_address,
            .travel = travel, .players = player_count, .random = random_rotation}};
    if (!qa_server_browser_create(frontend_tools_http(f), 2048, &browser, &n->browser, error) ||
        !qa_server_admin_create(&admin, &n->admin, error) || !qa_fs_root_open(f->options.application.user_root, &n->preferences, error)) goto failed;
    qa_fs_file *favorites = NULL; qa_fs_identity identity; qa_buffer bytes = {0}; qa_error local = {0};
    if (qa_fs_root_file_open(n->preferences, "network/favorites.bin", &favorites, &identity, &local)) {
        bool ok = qa_fs_file_read_snapshot(favorites, &identity, &bytes, error) &&
            qa_server_browser_restore(n->browser, (qa_bytes){bytes.data, bytes.size}, error);
        qa_fs_file_close(favorites); qa_buffer_free(&bytes); if (!ok) goto failed;
    } else if (local.code != QA_ERROR_NOT_FOUND) { if (error) *error = local; goto failed; }
    qa_cvars *cvars = qa_application_cvars(f->application);
    if (!qa_cvars_register(cvars, "rcon_password", "", 0, NETWORK_OWNER, "Remote administrator password", error) ||
        !qa_cvars_register(cvars, "rcon_limited_password", "", 0, NETWORK_OWNER, "Limited remote administrator password", error)) goto failed;
    n->registered = true;
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!qa_console_register_owned(qa_application_console(f->application), names[i], "Shared network service", 0,
            NETWORK_OWNER, true, command, n, error)) goto failed;
    return true;
failed:
    (void)frontend_network_destroy(f, NULL); return false;
}
bool frontend_network_world_change_ready(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n = f->network;
    return !n || (!n->busy && qa_network_callbacks_idle(n->runtime)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "network callbacks must return before world publication");
}
bool frontend_network_destroy(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n = f->network; if (!n) return true;
    if (!frontend_network_world_change_ready(f, error) || !qa_http_callbacks_idle(frontend_tools_http(f)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "network/HTTP callbacks must return before teardown");
    if (n->registered && !qa_console_remove_owner(qa_application_console(f->application), NETWORK_OWNER, error)) return false;
    qa_cvars_remove_owner(qa_application_cvars(f->application), NETWORK_OWNER);
    qa_downloads_destroy(n->downloads); qa_server_browser_destroy(n->browser); qa_server_admin_destroy(n->admin);
    qa_network_destroy(n->runtime); qa_q3_server_admission_destroy(n->q3_admission);
    qa_fs_root_close(n->preferences); qa_fs_root_close(n->content);
    free(n); f->network = NULL; return true;
}
bool frontend_network_pump(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n = f->network; if (!n) return true;
    if (n->downloads && (!qa_downloads_pump(n->downloads, error) || !frontend_tools_sync(f, error))) return false;
    if (!q3_prepare(n, error)) return false;
    ++n->busy;
    qa_server_browser_expire(n->browser, f->time_ns);
    bool ok = qa_network_pump(n->runtime, f->time_ns, error) && qa_server_admin_tick(n->admin, f->time_ns, false, error);
    --n->busy;
    return ok && (!n->q3_admission || q3_drain(n, error));
}
bool frontend_network_command(qa_frontend *f, uint32_t seat, qa_actor_id actor,
    const qa_movement_command *movement, qa_error *error)
{
    qa_actor_id current;
    if (!qa_application_player_actor(f->application, seat, &current) || !qa_actor_id_equal(actor, current))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "local network command actor no longer owns its roster seat");
    return qa_application_control_move(f->application, actor, movement, error);
}
bool frontend_network_publish(qa_frontend *f, qa_error *error)
{
    if (!f->network) return true;
    qa_frontend_network *n = f->network;
    if (n->q3_admission) {
        if (!q3_prepare(n, error)) return false;
        ++n->busy; bool ok = true;
        for (size_t i = 0; ok && i < 64; ++i) {
            frontend_q3_peer *peer = &n->q3_peers[i];
            if (!peer->occupied || peer->retiring) continue;
            qa_q3_server_state state;
            ok = qa_network_q3_state(n->runtime, peer->client, &state, error);
            if (ok && state.phase != QA_Q3_CONNECTED && peer->world.time >= state.next_snapshot_time)
                ok = q3_snapshot(peer, error);
        }
        --n->busy;
        return ok && q3_drain(n, error);
    }
    uint32_t cursor = 0; const qa_net_client *client;
    if (qa_net_connections_next(qa_network_connections(f->network->runtime), &cursor, &client))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "connected source snapshot/event publisher is unavailable");
    return true;
}

typedef struct server_lease {
    qa_frontend *frontend;
    qa_q3_host_server_services original;
    void *lifetime;
    void (*release)(void *);
    qa_actor_owner owner;
} server_lease;
static void release_server(void *context)
{ server_lease *lease = context; if (lease->release) lease->release(lease->lifetime); free(lease); }
#define FORWARD(name, parameters, arguments) static bool server_##name parameters { \
    server_lease *lease = context; return lease->original.name arguments; }
FORWARD(configstring, (void *context, uint32_t slot, const char **text, qa_error *error), (lease->original.context, slot, text, error))
FORWARD(set_configstring, (void *context, uint32_t slot, const char *text, qa_error *error), (lease->original.context, slot, text, error))
FORWARD(userinfo, (void *context, uint32_t slot, const char **text, qa_error *error), (lease->original.context, slot, text, error))
FORWARD(set_userinfo, (void *context, uint32_t slot, const char *text, qa_error *error), (lease->original.context, slot, text, error))
FORWARD(user_command, (void *context, uint32_t slot, qa_q3_usercmd *command, qa_error *error), (lease->original.context, slot, command, error))
FORWARD(allocate_bot, (void *context, int32_t *slot, qa_error *error), (lease->original.context, slot, error))
FORWARD(free_bot, (void *context, int32_t slot, qa_error *error), (lease->original.context, slot, error))
FORWARD(bot_snapshot_entity, (void *context, int32_t slot, int32_t sequence, int32_t *entity, qa_error *error), (lease->original.context, slot, sequence, entity, error))
FORWARD(bot_console_message, (void *context, int32_t slot, const char **text, qa_error *error), (lease->original.context, slot, text, error))
FORWARD(bot_user_command, (void *context, int32_t slot, const qa_q3_usercmd *command, qa_error *error), (lease->original.context, slot, command, error))
FORWARD(admit_actor, (void *context, qa_actor_id actor, qa_error *error), (lease->original.context, actor, error))
FORWARD(player_velocity, (void *context, qa_actor_id actor, qa_vec3 velocity, qa_error *error), (lease->original.context, actor, velocity, error))
#undef FORWARD
static qa_actor_id server_world_actor(void *context)
{ server_lease *lease = context; return lease->original.world_actor(lease->original.context); }
static bool server_send_command(void *context, int32_t slot, const char *text, qa_error *error)
{
    server_lease *lease = context;
    qa_frontend_network *n = lease->frontend->network;
    for (size_t i = 0; n && i < 64; ++i) {
        frontend_q3_peer *peer = &n->q3_peers[i]; qa_actor_id actor;
        if (!peer->occupied || peer->retiring || (slot >= 0 && peer->slot != (uint32_t)slot)) continue;
        if (!q3_actor(peer, &actor, error)) return false;
        const qa_actor_record *record = qa_actors_get(qa_session_actors(lease->frontend->application), actor);
        if (record && record->owner == lease->owner && !qa_network_q3_command(n->runtime, peer->client, text, error)) return false;
    }
    return !lease->original.send_command || lease->original.send_command(lease->original.context, slot, text, error);
}
static bool server_drop_client(void *context, uint32_t slot, const char *reason, qa_error *error)
{
    server_lease *lease = context;
    qa_frontend_network *n = lease->frontend->network;
    if (n && slot < 64 && n->q3_peers[slot].occupied) {
        qa_actor_id actor;
        if (!q3_actor(&n->q3_peers[slot], &actor, error)) return false;
        const qa_actor_record *record = qa_actors_get(qa_session_actors(lease->frontend->application), actor);
        if (record && record->owner == lease->owner && !q3_drop(&n->q3_peers[slot], reason, error)) return false;
    }
    return !lease->original.drop_client || lease->original.drop_client(lease->original.context, slot, reason, error);
}
bool frontend_network_source_services(qa_frontend *f, qa_q3_host_options *host, qa_error *error)
{
    server_lease *lease = malloc(sizeof(*lease));
    if (!lease) return frontend_fail(error, QA_ERROR_MEMORY, "retaining original Q3 server service delegate");
    *lease = (server_lease){f, host->server, host->frontend_lifetime, host->release_frontend, host->owner};
    host->frontend_lifetime = lease; host->release_frontend = release_server;
    host->server.context = lease;
#define BIND(name) if (lease->original.name) host->server.name = server_##name
    BIND(configstring); BIND(set_configstring); BIND(userinfo); BIND(set_userinfo); BIND(user_command);
    BIND(allocate_bot); BIND(free_bot); BIND(bot_snapshot_entity); BIND(bot_console_message); BIND(bot_user_command);
    BIND(admit_actor); BIND(player_velocity); BIND(world_actor);
#undef BIND
    host->server.send_command = server_send_command; host->server.drop_client = server_drop_client;
    return true;
}
