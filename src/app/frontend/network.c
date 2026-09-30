#include "internal.h"
#include "qa/application_network.h"
#include "qa/downloads.h"
#include "qa/server_browser.h"
#include "qa/server_admin.h"
#include "qa/launch_identity.h"
#include "qa/network_q3_runtime.h"
#include "qa/network_save.h"
#include "qa/network_services_save.h"
#include "qa/network_downloads_save.h"
#include "save_private.h"
#include "../../network/service_save_fields.h"
#include "qa/archive.h"
#include "qa/bsp.h"
#include <inttypes.h>
#include <stdio.h>

#define NETWORK_OWNER QA_NETWORK_COMMAND_OWNER
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
typedef struct frontend_network_server_lease {
    qa_frontend *frontend;
    qa_q3_host_server_services original;
    void *lifetime;
    void (*release)(void *);
    qa_actor_owner owner;
    struct frontend_network_server_lease *next;
} server_lease;
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
    qa_q3_client_admission q3_client_admission;
    qa_net_client_id q3_client;
    qa_actor_owner q3_cgame_owner;
    qa_q3_product q3_client_product;
    qa_q3_client_clock q3_client_clock;
    qa_application_network_q3_projection q3_projection;
    uint8_t q3_projection_epoch;
    uint64_t q3_client_generation;
    int32_t q3_client_time, q3_weapon;
    float q3_sensitivity;
    bool q3_client_requested, q3_client_attach, q3_client_attached;
    bool q3_client_gamestate, q3_client_active, q3_client_retiring, q3_command_present, q3_angles_ready;
    char q3_client_reason[256];
    unsigned busy;
    bool registered;
    bool detached_transport;
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
    if (n->q3_client_requested && request->protocol.kind == QA_NET_Q3_68) {
        qa_actor_id actor; qa_actor_owner owner; qa_q3_product product;
        return qa_application_player_actor(n->frontend->application, 0, &actor) &&
            qa_application_network_q3_client_source(n->frontend->application, actor, &owner, &product, error);
    }
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
    if (n->q3_client_attached && qa_net_client_id_equal(n->q3_client, id)) {
        n->q3_client_attached = false; n->q3_client_active = false; n->q3_client_gamestate = false;
        n->q3_client_retiring = true;
        snprintf(n->q3_client_reason, sizeof(n->q3_client_reason), "%s", reason); return;
    }
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
    if (n->q3_client_requested) {
        qa_q3_connectionless source; qa_q3_admission_result result;
        if (!qa_q3_client_admission_receive(&n->q3_client_admission, &packet->from, packet->payload,
            (int64_t)(packet->received_ns / UINT64_C(1000000)), &result, &source, error)) return false;
        if (result == QA_Q3_ADMISSION_CONNECTED) { n->q3_client_attach = true; return true; }
        if (result == QA_Q3_ADMISSION_HANDLED || result == QA_Q3_ADMISSION_IGNORED) return true;
        if (!strcmp(qa_q3_token(&source.tokens, 0), "print") &&
            qa_net_address_equal(&packet->from, &n->q3_client_admission.address, false)) {
            frontend_print(n->frontend, source.line + 5); return true;
        }
    }
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
bool frontend_network_remote(const qa_frontend *f)
{ return f && f->options.network_connect && f->options.network_protocol.kind == QA_NET_Q3_68; }
bool frontend_network_client_ready(const qa_frontend *f)
{ return f && f->network && f->network->q3_client_active && !f->network->q3_client_retiring; }
uint32_t frontend_network_client_time(const qa_frontend *f)
{ return f && f->network ? (uint32_t)f->network->q3_client_time : 0; }
static const qa_q3_client_peer *remote_view(qa_frontend *f)
{
    qa_frontend_network *n = f->network;
    return n && n->q3_client_attached ? qa_network_q3_client_view(n->runtime, n->q3_client) : NULL;
}
static uint64_t client_generation(void *context)
{ qa_frontend_network *n = context; return qa_application_configuration_generation(n->frontend->application); }
static bool client_clear(void *context, qa_error *error)
{
    qa_frontend_network *n = context;
    n->q3_client_gamestate = false; n->q3_client_active = false;
    n->q3_angles_ready = false;
    qa_q3_clock_clear(&n->q3_client_clock);
    return qa_application_network_q3_client_unproject(n->frontend->application, &n->q3_projection, error) &&
        qa_application_network_q3_client_clear(n->frontend->application, n->q3_cgame_owner, 0, error);
}
static bool client_system_info(void *context, const char *info, qa_error *error)
{
    (void)context; char value[1024];
    if (!qa_q3_info_value(info, "sv_pure", value, sizeof(value), error)) return false;
    if (strtol(value, NULL, 10) != 0)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 remote pure admission requires live package-reference validation");
    return true;
}
static bool client_gamestate(void *context, const qa_q3_gamestate *state, qa_error *error)
{
    qa_frontend_network *n = context; char map[1024]; qa_application_map_view local;
    if (!qa_q3_info_value(qa_q3_configstring(state, 0), "mapname", map, sizeof(map), error)) return false;
    if (state->client_number < 0 || state->client_number >= 64 ||
        !qa_application_map_read(n->frontend->application, &local) || !*map || strcmp(map, local.name))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 remote signon requires the current selected local map; server travel admission remains unbound");
    n->q3_client_gamestate = true; return true;
}
static bool client_snapshot(void *context, const qa_q3_snapshot *snapshot, int32_t ping, qa_error *error)
{
    qa_frontend_network *n = context; (void)ping; (void)error;
    qa_q3_clock_publish(&n->q3_client_clock, snapshot); return true;
}
static bool client_download_size(void *context, int32_t size, int32_t *effective, qa_error *error)
{ (void)context; (void)error; *effective = size; return true; }
static bool client_download(void *context, const qa_q3_download *download, qa_error *error)
{
    (void)context; (void)download;
    return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Unsolicited Q3 package transfer has no admitted download identity");
}
static bool client_source_command(void *context, int32_t sequence, const qa_q3_tokens *tokens, qa_error *error)
{
    qa_frontend_network *n = context; (void)sequence;
    if (!qa_application_network_q3_client_command(n->frontend->application, n->q3_cgame_owner, 0, tokens, error)) return false;
    n->q3_command_present = true; return true;
}
static bool client_map_restart(void *context, qa_error *error)
{
    qa_frontend_network *n = context; (void)error;
    /* The source can execute this command during DrawActiveFrame. Keep the
     * current source/audio identities until safe snapshot-epoch publication. */
    qa_q3_clock_clear(&n->q3_client_clock); n->q3_client_active = false;
    const qa_q3_client_peer *peer = remote_view(n->frontend);
    const qa_q3_snapshot *snapshot = peer ? qa_q3_client_peer_snapshot(peer) : NULL;
    if (snapshot) qa_q3_clock_publish(&n->q3_client_clock, snapshot);
    return true;
}
static bool client_disconnect(void *context, const char *reason, qa_error *error)
{
    qa_frontend_network *n = context; (void)error; n->q3_client_retiring = true;
    snprintf(n->q3_client_reason, sizeof(n->q3_client_reason), "%s", reason); return true;
}
static bool client_level_shot(void *context, qa_error *error)
{ (void)context; return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Remote Q3 server cannot request a local level shot"); }
static bool client_local_server(void *context)
{ (void)context; return false; }
static bool client_project(qa_frontend_network *n, qa_error *error)
{
    if (!n->q3_client_active) return true;
    const qa_q3_client_peer *peer = remote_view(n->frontend);
    const qa_q3_snapshot *latest = peer ? qa_q3_client_peer_snapshot(peer) : NULL;
    if (!latest) return true;
    const qa_q3_snapshot *current = NULL, *next = NULL;
    for (int32_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i) {
        int64_t number = (int64_t)latest->message_number - i;
        if (number < 0) break;
        const qa_q3_snapshot *snapshot = qa_q3_client_peer_snapshot_at(peer, (int32_t)number);
        if (!snapshot || (snapshot->flags & 2)) continue;
        if (snapshot->server_time <= n->q3_client_time) {
            if (!current || snapshot->server_time > current->server_time) current = snapshot;
        } else if (!next || snapshot->server_time < next->server_time) next = snapshot;
    }
    if (!current) { current = next; next = NULL; }
    if (!current) return true;
    uint8_t epoch = current->flags & 4;
    if (n->q3_projection.owner && n->q3_projection_epoch != epoch &&
        !qa_application_network_q3_client_unproject(n->frontend->application, &n->q3_projection, error)) return false;
    /* A future server-count change must not reuse the current epoch's IDs. */
    if (next && ((next->flags ^ current->flags) & 4)) next = NULL;
    if (!qa_application_network_q3_client_project(n->frontend->application,
        n->q3_cgame_owner, &n->q3_projection, current, next, error)) return false;
    n->q3_projection_epoch = epoch; return true;
}
static bool client_drain(qa_frontend_network *n, qa_error *error)
{
    if (!n->q3_client_requested) return true;
    if (n->q3_client_retiring) {
        if (n->q3_client_attached && !qa_network_detach(n->runtime, n->q3_client, n->q3_client_reason, error)) return false;
        if (!qa_application_network_q3_client_unproject(n->frontend->application, &n->q3_projection, error)) return false;
        n->q3_client_admission.phase = QA_Q3_DISCONNECTED;
        frontend_print(n->frontend, n->q3_client_reason); qa_application_request_stop(n->frontend->application); return true;
    }
    if (n->q3_client_generation != client_generation(n))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Remote Q3 launch changed and requires fresh connection admission");
    if (n->q3_client_attach) {
        n->q3_client_attach = false;
        qa_net_seat_binding seat = {{NETWORK_OWNER, 0}, 0};
        qa_net_connect request = {.attachment = QA_NET_REMOTE, .endpoint = n->q3_client_admission.address,
            .protocol = {QA_NET_Q3_68, 0, 0}, .seats = &seat, .seat_count = 1, .composition = n->composition};
        qa_q3_client_hooks hooks = {.context = n, .generation = client_generation,
            .clear_active = client_clear, .gamestate = client_gamestate, .system_info = client_system_info,
            .snapshot = client_snapshot, .download_size = client_download_size, .download = client_download,
            .command = client_source_command, .map_restart = client_map_restart, .disconnect = client_disconnect,
            .level_shot = client_level_shot, .local_server_running = client_local_server, .send = send_address};
        if (!qa_network_attach_q3_client(n->runtime, &request, n->q3_client_product,
            n->q3_client_admission.challenge, n->q3_client_admission.qport, &hooks,
            n->frontend->time_ns, &n->q3_client, error)) return false;
        n->q3_client_attached = true;
    }
    if (!n->q3_client_attached) {
        qa_actor_id actor;
        if (!qa_application_player_actor(n->frontend->application, 0, &actor))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 client lost its local roster seat");
        qa_cvars *cvars = qa_application_network_q3_cvars(n->frontend->application, actor);
        qa_buffer info = {0};
        if (!cvars || !qa_cvars_info(cvars, QA_CVAR_USERINFO, 1024, &info, error)) return false;
        bool ok = qa_q3_client_admission_resend(&n->q3_client_admission,
            (int64_t)(n->frontend->time_ns / UINT64_C(1000000)), (const char *)info.data, send_address, n, error);
        qa_buffer_free(&info); return ok;
    }
    qa_q3_clock_options options = {.timescale = 1};
    const qa_cvar_view *nudge = qa_cvars_find(qa_application_cvars(n->frontend->application), "cl_timeNudge");
    if (nudge) options.time_nudge = nudge->integer;
    return qa_q3_clock_advance(&n->q3_client_clock,
        (int32_t)((n->frontend->time_ns / UINT64_C(1000000)) & INT32_MAX), &options,
        &n->q3_client_active, &n->q3_client_time, error) && client_project(n, error);
}
static const qa_q3_gamestate *service_gamestate(void *context)
{
    qa_frontend *f = context; const qa_q3_client_peer *p = remote_view(f);
    return p && f->network->q3_client_gamestate ? qa_q3_client_peer_gamestate(p) : NULL;
}
static bool service_current_snapshot(void *context, int32_t *number, int32_t *time, qa_error *error)
{
    qa_frontend *f = context; const qa_q3_client_peer *p = remote_view(f); (void)error;
    const qa_q3_snapshot *snapshot = p ? qa_q3_client_peer_snapshot(p) : NULL;
    *number = snapshot ? snapshot->message_number : 0; *time = snapshot ? snapshot->server_time : 0; return true;
}
static bool service_snapshot(void *context, int32_t number, const qa_q3_snapshot **out, int32_t *ping, qa_error *error)
{
    qa_frontend *f = context; const qa_q3_client_peer *p = remote_view(f);
    const qa_q3_snapshot *latest = p ? qa_q3_client_peer_snapshot(p) : NULL;
    if (latest && number > latest->message_number) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 snapshot request is in the future");
    *out = p ? qa_q3_client_peer_snapshot_at(p, number) : NULL;
    *ping = *out ? (*out)->player.ping : 0; return true;
}
static bool service_server_command(void *context, int32_t sequence, bool *present, qa_error *error)
{
    qa_frontend *f = context; qa_frontend_network *n = f->network;
    *present = false;
    if (!n || !n->q3_client_attached) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 command requested without an admitted client");
    n->q3_command_present = false;
    if (!qa_network_q3_client_execute(n->runtime, n->q3_client, sequence, error)) return false;
    *present = n->q3_command_present; return true;
}
static int32_t service_current_command(void *context)
{
    const qa_q3_client_peer *p = remote_view(context);
    return p ? (int32_t)qa_q3_client_peer_usercmd_number(p) : 0;
}
static bool service_user_command(void *context, int32_t number, qa_q3_usercmd *out, bool *present, qa_error *error)
{
    const qa_q3_client_peer *p = remote_view(context); *present = false;
    if (!p || number < 0) return true;
    if ((uint64_t)number > qa_q3_client_peer_usercmd_number(p))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 usercmd requested in the future");
    const qa_q3_usercmd *command = qa_q3_client_peer_usercmd_at(p, (uint64_t)number);
    if (command) { *out = *command; *present = true; } return true;
}
static bool service_command_values(void *context, int32_t weapon, float sensitivity, qa_error *error)
{
    qa_frontend *f = context;
    if (!f->network || weapon < 0 || weapon > UINT8_MAX || !isfinite(sensitivity))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid remote Q3 cgame input values");
    f->network->q3_weapon = weapon; f->network->q3_sensitivity = sensitivity; return true;
}
static bool service_source_actor(void *context, uint32_t source, qa_actor_id *out, bool *present, qa_error *error)
{
    qa_frontend *f = context;
    if (!f->network) { *out = (qa_actor_id){0}; *present = false; return true; }
    return qa_application_network_q3_client_actor(f->application, &f->network->q3_projection,
        source, out, present, error);
}
bool frontend_network_client_actor(const qa_frontend *f, qa_actor_id actor)
{
    if (!f || !f->network || !f->network->q3_client_requested || !actor.registry ||
        !qa_actors_get(qa_session_actors(qa_application_session(f->application)), actor)) return false;
    const qa_application_network_q3_projection *projection = &f->network->q3_projection;
    for (uint32_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i)
        if (qa_actor_id_equal(projection->actors[i], actor)) return true;
    return false;
}
bool frontend_network_client_services(qa_frontend *f, qa_actor_owner owner, qa_qvm_role role,
    uint32_t seat, qa_q3_host_options *host, qa_error *error)
{
    (void)owner;
    if (!frontend_network_remote(f) || role != QA_QVM_CGAME) return true;
    if (seat != 0 || f->options.seats != 1)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 remote connection owns one local client seat");
    host->client = (qa_q3_host_client_services){f, service_gamestate, service_current_snapshot,
        service_snapshot, service_server_command, service_current_command, service_user_command,
        service_command_values, service_source_actor};
    return true;
}
bool frontend_network_client_command(qa_frontend *f, const char *text, qa_error *error)
{
    qa_frontend_network *n = f->network;
    return n && n->q3_client_attached ? qa_network_q3_client_command(n->runtime, n->q3_client, text, error) :
        frontend_fail(error, QA_ERROR_ARGUMENT, "Remote client command requires original Q3 admission");
}
bool frontend_network_client_input(qa_frontend *f, qa_input_command_builder *builder,
    qa_input_command_frame *frame, qa_error *error)
{
    if (!frontend_network_remote(f)) return true;
    qa_frontend_network *n = f->network;
    if (!n) return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 input owner is unavailable");
    const qa_q3_client_peer *peer = remote_view(f);
    const qa_q3_snapshot *snapshot = peer ? qa_q3_client_peer_snapshot(peer) : NULL;
    if (snapshot && !n->q3_angles_ready) {
        qa_vec3 angles = qa_v3(snapshot->player.viewangles[0] - (float)(uint16_t)snapshot->player.deltaAngles[0] * (360.0f / 65536.0f),
            snapshot->player.viewangles[1] - (float)(uint16_t)snapshot->player.deltaAngles[1] * (360.0f / 65536.0f),
            snapshot->player.viewangles[2] - (float)(uint16_t)snapshot->player.deltaAngles[2] * (360.0f / 65536.0f));
        if (!qa_input_command_angles(builder, angles, error)) return false;
        n->q3_angles_ready = true;
    }
    frame->server_time_ms = n->q3_client_time; frame->weapon = (uint8_t)n->q3_weapon;
    frame->sensitivity = n->q3_sensitivity; return true;
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
static bool download_inspect_bytes(const char *path, qa_bytes bytes, qa_error *error)
{
    bool ok = true;
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
    return ok;
}
static bool download_inspect(void *context, const char *path, qa_fs_stage *stage, uint64_t size, qa_error *error)
{
    (void)context; qa_fs_stage_mapping *mapping = NULL;
    if (!qa_fs_stage_map(stage, &mapping, error)) return false;
    qa_bytes bytes = qa_fs_stage_mapping_bytes(mapping);
    bool ok = bytes.size == size && download_inspect_bytes(path, bytes, error);
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
static qa_download_options saved_download_options(qa_frontend_network *n)
{
    return (qa_download_options){.jobs = 4, .maximum_pending_bytes = UINT64_C(4294967296),
        .hooks = {.context = n, .permit = download_permit, .inspect = download_inspect, .remount = download_remount}};
}
static bool downloads_ready(qa_frontend_network *n, qa_error *error)
{
    if (n->downloads) return true;
    if (!n->content && !qa_fs_root_open(n->frontend->options.application.content_root, &n->content, error)) return false;
    qa_download_options options = saved_download_options(n);
    return qa_downloads_create(frontend_tools_http(n->frontend), n->content, &options, &n->downloads, error);
}
static bool download_saved_resource(void *context, const qa_download_request *request,
    const qa_download_view *view, bool staged, qa_error *error)
{
    qa_frontend_network *n = context;
    qa_archive_kind archive = qa_archive_kind_for_path(request->path);
    const char *extension = strrchr(request->path, '.');
    if (!n->content || !request->exact_identity || request->maximum_bytes > UINT64_C(2147483648) ||
        (archive == QA_ARCHIVE_AUTO && (!extension || strcmp(extension, ".bsp"))) ||
        staged != (view->state == QA_DOWNLOAD_RECEIVING))
        return frontend_fail(error, QA_ERROR_FORMAT, "saved download differs from the actual frontend admission policy");
    qa_fs_entry_kind kind = QA_FS_MISSING; qa_fs_identity identity;
    if (!qa_fs_root_status(n->content, request->path, &kind, &identity, error)) return false;
    if (!view->published)
        return kind == QA_FS_MISSING || frontend_fail(error, QA_ERROR_FORMAT, "unpublished download target is already installed");
    if (kind != QA_FS_REGULAR || qa_fs_identity_size(&identity) != view->received)
        return frontend_fail(error, QA_ERROR_FORMAT, "installed download target differs from its retained size");
    qa_fs_file *file = NULL; qa_buffer content = {0};
    bool ok = qa_fs_root_file_open(n->content, request->path, &file, &identity, error) &&
        qa_fs_identity_size(&identity) == view->received &&
        qa_fs_file_read_snapshot(file, &identity, &content, error);
    qa_sha256_digest digest;
    if (ok) {
        qa_sha256((qa_bytes){content.data, content.size}, &digest);
        ok = content.size == view->received && qa_sha256_equal(&digest, &view->digest) &&
            qa_sha256_equal(&digest, &request->digest) && download_inspect_bytes(request->path, (qa_bytes){content.data, content.size}, error);
    }
    bool unchanged = false;
    if (ok) ok = qa_fs_file_path_unchanged(file, &identity, &unchanged, error) && unchanged;
    qa_buffer_free(&content); qa_fs_file_close(file);
    return ok || (error && error->code ? false : frontend_fail(error, QA_ERROR_FORMAT, "installed download content identity changed"));
}
static bool download_saved_stage(void *context, const qa_download_request *request,
    const qa_download_view *view, qa_bytes prefix, qa_fs_stage **out, uint64_t *nonce, qa_error *error)
{
    qa_frontend_network *n = context;
    if (!out || *out || !nonce || *nonce || n->nonce == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "candidate download stage allocation is exhausted");
    uint64_t fresh = n->nonce + 1;
    if (fresh == view->stage_nonce) {
        if (fresh == UINT64_MAX) return frontend_fail(error, QA_ERROR_ARGUMENT, "candidate download stage namespace is exhausted");
        ++fresh;
    }
    qa_fs_stage *stage = NULL; uint64_t initial = 0; size_t written = 0;
    if (!qa_fs_stage_open(n->content, request->path, fresh, false, &stage, &initial, error)) return false;
    bool ok = !initial && qa_fs_stage_write(stage, 0, prefix, &written, error) && written == prefix.size;
    if (!ok) { qa_fs_stage_close(stage, false); return false; }
    n->nonce = fresh; *nonce = fresh; *out = stage; return true;
}
static qa_download_checkpoint_refs saved_download_refs(qa_frontend_network *n)
{ return (qa_download_checkpoint_refs){n, download_saved_resource, download_saved_stage}; }
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
    if ((f->options.network_connect && f->options.network_protocol.kind != QA_NET_Q3_68) ||
        (f->options.network_host && f->options.network_protocol.kind != QA_NET_Q3_68))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "selected connection requires its complete original/unified signon and prediction producer");
    qa_frontend_network *n = calloc(1, sizeof(*n));
    if (!n) return frontend_fail(error, QA_ERROR_MEMORY, "allocating network frontend owner");
    n->frontend = f; n->nonce = SDL_GetPerformanceCounter();
    n->rotation_random = (uint32_t)n->nonce ^ (uint32_t)(n->nonce >> 32); f->network = n;
    n->q3_client_requested = frontend_network_remote(f); n->q3_sensitivity = 1;
    qa_net_udp_options udp = {.bind = {.kind = QA_NET_IPV4}, .limits = {65507, 256}, .broadcast = true};
    if (n->q3_client_requested) {
        qa_actor_id actor; qa_net_address address; qa_buffer identity = {0};
        if (f->options.dedicated || f->options.seats != 1) {
            frontend_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 remote client requires one presentation seat"); goto failed;
        }
        if (!qa_application_player_actor(f->application, 0, &actor) ||
            !qa_application_network_q3_client_source(f->application, actor, &n->q3_cgame_owner, &n->q3_client_product, error) ||
            !qa_net_address_resolve(f->options.network_connect, f->options.network_port, 0, &address, error)) goto failed;
        if (address.kind != QA_NET_IPV4 && address.kind != QA_NET_IPV6) {
            frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 remote client requires the selected UDP address family"); goto failed;
        }
        udp.bind.kind = address.kind; udp.ipv6_only = address.kind == QA_NET_IPV6;
        udp.broadcast = address.kind == QA_NET_IPV4;
        qa_q3_client_admission_begin(&n->q3_client_admission, &address, (uint16_t)n->rotation_random);
        n->q3_client_generation = qa_application_configuration_generation(f->application);
        if (!qa_launch_identity_encode(qa_application_launch(f->application),
            qa_session_actors(qa_application_session(f->application)), &identity, error)) goto failed;
        qa_sha256((qa_bytes){identity.data, identity.size}, &n->composition); qa_buffer_free(&identity);
    }
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
    if (!client_drain(n, error)) goto failed;
    return true;
failed:
    (void)frontend_network_destroy(f, NULL); return false;
}

static qa_network_options saved_network_options(qa_frontend_network *n)
{
    return (qa_network_options){.owner = NETWORK_OWNER, .clients = 64, .packets_per_pump = 256,
        .timeout_ns = UINT64_C(30000000000), .hooks = {.context = n, .admit = admit, .controlled = controlled,
        .command = remote_command, .disconnected = disconnected, .connectionless = connectionless, .reconnect = reconnect}};
}
static qa_browser_hooks saved_browser_hooks(qa_frontend_network *n)
{ return (qa_browser_hooks){.context = n, .send = send_address, .local = local_address}; }
static qa_admin_options saved_admin_options(qa_frontend_network *n)
{
    return (qa_admin_options){.dialect = qa_cvars_dialect(qa_application_cvars(n->frontend->application)),
        .filters = 1024, .rate_entries = 1024, .burst = 10, .rate_interval_ns = UINT64_C(1000000000),
        .heartbeat_interval_ns = UINT64_C(300000000000), .deny_matches = true,
        .hooks = {.context = n, .password = password, .execute = admin_execute, .send = send_address,
            .travel = travel, .players = player_count, .random = random_rotation}};
}
static bool detached_send(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *error)
{
    (void)context; (void)to; (void)bytes;
    return frontend_fail(error, QA_ERROR_ARGUMENT, "detached network candidate has no published transport");
}
static bool detached_receive(void *context, uint64_t now, qa_net_datagram *packet, qa_error *error)
{ (void)context; (void)now; (void)error; *packet = (qa_net_datagram){.kind = QA_NET_POLL_EMPTY}; return true; }
static void detached_close(void *context) { (void)context; }
static bool detached_ready(const void *context) { (void)context; return false; }
static bool detached_transport(const qa_net_address *address, qa_net_transport **out, qa_error *error)
{
    const qa_net_transport_ops ops = {detached_send, detached_receive, detached_close, detached_ready};
    return qa_net_transport_create(address, (qa_net_limits){65507, 256}, &ops, NULL, out, error);
}
static bool network_header(qa_source_save_io *io, bool *installed)
{
    uint32_t magic = UINT32_C(0x464e4151), version = 2;
    return qa_source_save_u32(io, &magic) && magic == UINT32_C(0x464e4151) &&
        qa_source_save_u32(io, &version) && version == 2 && qa_source_save_bool(io, installed);
}
bool frontend_network_prepare_restored(qa_frontend *f, qa_bytes bytes, qa_error *error)
{
    if (!f || !f->application || f->network || !frontend_tools_http(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "network restore preparation requires detached application and HTTP owners");
    qa_source_save_io io = {0}; bool installed = false;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && network_header(&io, &installed);
    qa_source_save_dispose(&io); if (!ok) return false;
    if (!installed) return true;
    if (f->options.network_host || (f->options.network_connect && !frontend_network_remote(f)))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "restored frontend hosting or dialect needs its complete actual service consumer");
    qa_frontend_network *n = calloc(1, sizeof(*n));
    if (!n) return frontend_fail(error, QA_ERROR_MEMORY, "allocating detached network consumer");
    n->frontend = f; n->detached_transport = true; n->q3_client_requested = frontend_network_remote(f);
    n->q3_sensitivity = 1; f->network = n;
    qa_net_address inert = {.kind = QA_NET_LOOPBACK}; memcpy(inert.host.loopback, "qa-save-candidate", 18);
    qa_net_transport *transport = NULL; qa_network_options options = saved_network_options(n);
    if (!detached_transport(&inert, &transport, error)) goto failed;
    if (!qa_network_create(transport, &options, &n->runtime, error)) { qa_net_transport_close(transport); goto failed; }
    qa_browser_hooks browser = saved_browser_hooks(n); qa_admin_options admin = saved_admin_options(n);
    if (!qa_server_browser_create(frontend_tools_http(f), 2048, &browser, &n->browser, error) ||
        !qa_server_admin_create(&admin, &n->admin, error) || !qa_fs_root_open(f->options.application.user_root, &n->preferences, error)) goto failed;
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
static bool network_clock_fields(qa_source_save_io *io, qa_q3_client_clock *v)
{
    return qa_source_save_i32(io, &v->time) && qa_source_save_i32(io, &v->delta) && qa_source_save_i32(io, &v->old_time) &&
        qa_source_save_i32(io, &v->old_frame_server_time) && qa_source_save_i32(io, &v->snapshot_time) &&
        qa_source_save_u8(io, &v->snapshot_flags) && qa_source_save_bool(io, &v->pending) && qa_source_save_bool(io, &v->extrapolated) &&
        qa_source_save_bool(io, &v->active) && qa_source_save_bool(io, &v->has_snapshot) &&
        qa_source_save_i32(io, &v->demo_base_time) && qa_source_save_i32(io, &v->demo_frames) && qa_source_save_i32(io, &v->demo_start);
}
static bool network_address_fields(qa_source_save_io *io, qa_net_address *v)
{
    uint32_t kind = v->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_NET_IPX || !qa_source_save_u16(io, &v->port)) return false;
    v->kind = (qa_net_address_kind)kind;
    switch (v->kind) {
    case QA_NET_IPV4: return qa_source_save_bytes(io, v->host.ipv4, 4);
    case QA_NET_IPV6: return qa_source_save_bytes(io, v->host.ipv6.bytes, 16) && qa_source_save_u32(io, &v->host.ipv6.scope);
    case QA_NET_IPX: return qa_source_save_u32(io, &v->host.ipx.network) && qa_source_save_bytes(io, v->host.ipx.node, 6);
    case QA_NET_LOOPBACK:
        return qa_source_save_bytes(io, v->host.loopback, sizeof(v->host.loopback)) && service_address_valid(v);
    }
    return false;
}
static bool network_frontend_fields(qa_source_save_io *io, qa_frontend_network *n)
{
    uint32_t phase = n->q3_client_admission.phase, product = n->q3_client_product;
    if (!qa_source_save_u64(io, &n->nonce) || !qa_source_save_u32(io, &n->rotation_random) ||
        !qa_source_save_bytes(io, n->composition.bytes, sizeof(n->composition.bytes)) ||
        !qa_source_save_bool(io, &n->q3_client_requested) || !qa_source_save_u32(io, &phase) || phase > QA_Q3_ADMITTED ||
        !network_address_fields(io, &n->q3_client_admission.address) || !qa_source_save_u16(io, &n->q3_client_admission.qport) ||
        !qa_source_save_i32(io, &n->q3_client_admission.challenge) || !qa_source_save_i64(io, &n->q3_client_admission.connect_time) ||
        !qa_source_save_i64(io, &n->q3_client_admission.last_packet_time) || !qa_source_save_u32(io, &n->q3_client_admission.connect_packets) ||
        !qa_source_save_u64(io, &n->q3_client.generation) || !qa_source_save_u32(io, &n->q3_client.slot) ||
        !frontend_save_provider(io, n->frontend->application, &n->q3_cgame_owner) || !qa_source_save_u32(io, &product) || product > QA_Q3_TEAM_ARENA ||
        !network_clock_fields(io, &n->q3_client_clock) || !qa_source_save_u64(io, &n->q3_client_generation) ||
        !qa_source_save_i32(io, &n->q3_client_time) || !qa_source_save_i32(io, &n->q3_weapon) ||
        !qa_source_save_f32(io, &n->q3_sensitivity) || !isfinite(n->q3_sensitivity) ||
        !qa_source_save_bool(io, &n->q3_client_attach) || !qa_source_save_bool(io, &n->q3_client_attached) ||
        !qa_source_save_bool(io, &n->q3_client_gamestate) || !qa_source_save_bool(io, &n->q3_client_active) ||
        !qa_source_save_bool(io, &n->q3_client_retiring) || !qa_source_save_bool(io, &n->q3_command_present) ||
        !qa_source_save_bool(io, &n->q3_angles_ready) ||
        !qa_source_save_bytes(io, n->q3_client_reason, sizeof(n->q3_client_reason)) ||
        !memchr(n->q3_client_reason, 0, sizeof(n->q3_client_reason)) ||
        !frontend_save_provider(io, n->frontend->application, &n->q3_projection.owner) ||
        !qa_source_save_string(io, &n->q3_projection.definition) || !qa_source_save_u8(io, &n->q3_projection_epoch)) return false;
    n->q3_client_admission.phase = (qa_q3_admission_phase)phase; n->q3_client_product = (qa_q3_product)product;
    n->q3_client.owner = n->q3_client.generation ? NETWORK_OWNER : 0;
    for (size_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i) if (!qa_source_save_actor(io, &n->q3_projection.actors[i])) return false;
    return true;
}
static bool network_metadata_valid(qa_frontend_network *n, qa_error *error)
{
    qa_application *app = n->frontend->application;
    if (n->q3_admission || n->q3_pending_count || n->q3_reconnect || (n->downloads && !n->content) ||
        n->q3_client_requested != frontend_network_remote(n->frontend) || !n->registered || n->q3_projection_epoch > 4 ||
        (n->q3_projection_epoch != 0 && n->q3_projection_epoch != 4))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "installed frontend network service lacks a complete continuation consumer");
    for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "original hosting frontend continuation is not admitted");
    if (n->q3_client_requested) {
        qa_actor_id actor; qa_actor_owner owner; qa_q3_product product;
        if (n->frontend->options.dedicated || n->frontend->options.seats != 1 ||
            !qa_application_player_actor(app, 0, &actor) ||
            !qa_application_network_q3_client_source(app, actor, &owner, &product, error) ||
            owner != n->q3_cgame_owner || product != n->q3_client_product ||
            n->q3_client_generation != qa_application_configuration_generation(app) ||
            (n->q3_client_admission.address.kind != QA_NET_IPV4 && n->q3_client_admission.address.kind != QA_NET_IPV6) ||
            !n->q3_client_admission.address.port || n->q3_client.slot >= 64 ||
            (n->q3_client_attached && (!n->q3_client.generation || n->q3_client_admission.phase != QA_Q3_ADMITTED)) ||
            (n->q3_client_active && (!n->q3_client_attached || !n->q3_client_gamestate || !n->q3_client_clock.active)))
            return frontend_fail(error, QA_ERROR_FORMAT, "remote Q3 continuation differs from its selected source and seat");
    } else if (n->q3_client_attach || n->q3_client_attached || n->q3_client_gamestate || n->q3_client_active || n->q3_projection.owner)
        return frontend_fail(error, QA_ERROR_FORMAT, "uninstalled remote source carries live client state");
    if (n->q3_projection.owner) {
        const char *definition = qa_strings_cstr(qa_session_strings(qa_application_session(app)), n->q3_projection.definition);
        if (n->q3_projection.owner != n->q3_cgame_owner || !definition || strcmp(definition, "qa.network.q3.remote-entity"))
            return frontend_fail(error, QA_ERROR_FORMAT, "remote entity projection has a foreign source definition");
    } else if (n->q3_projection.definition) return frontend_fail(error, QA_ERROR_FORMAT, "absent remote projection has a definition");
    qa_actor_registry *actors = qa_session_actors(qa_application_session(app));
    for (size_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i) {
        qa_actor_id id = n->q3_projection.actors[i]; if (!id.registry) continue;
        const qa_actor_record *record = qa_actors_get(actors, id);
        if (!n->q3_projection.owner || !record || record->owner != n->q3_projection.owner ||
            record->definition != n->q3_projection.definition || record->has_source)
            return frontend_fail(error, QA_ERROR_FORMAT, "remote entity projection does not own its saved canonical actor");
        for (size_t j = 0; j < i; ++j) if (qa_actor_id_equal(id, n->q3_projection.actors[j]))
            return frontend_fail(error, QA_ERROR_FORMAT, "remote source entity numbers alias one canonical actor");
    }
    return true;
}
static bool network_save_actor(void *context, qa_actor_id actor, qa_saved_actor_id *out, qa_error *error)
{
    qa_frontend_network *n = context;
    return qa_actors_save_reference(qa_session_actors(qa_application_session(n->frontend->application)), actor, out, error);
}
static bool network_restore_actor(void *context, qa_saved_actor_id actor, qa_actor_id *out, qa_error *error)
{
    qa_frontend_network *n = context;
    return qa_actors_reference_saved(qa_session_actors(qa_application_session(n->frontend->application)), actor, true, out, error);
}
static bool network_restore_source(void *context, const qa_net_client *client, qa_network_source_kind kind,
    qa_q3_client_hooks *hooks, qa_q3_server_hooks *server, qa_error *error)
{
    qa_frontend_network *n = context; (void)server;
    if (kind != QA_NETWORK_SOURCE_Q3_CLIENT || !n->q3_client_requested || !n->q3_client_attached ||
        !qa_net_client_id_equal(n->q3_client, client->id) || client->attachment != QA_NET_REMOTE ||
        client->seat_count != 1 || client->seats[0].seat.owner != NETWORK_OWNER || client->seats[0].seat.index ||
        client->seats[0].remote_index || !qa_net_address_equal(&client->endpoint, &n->q3_client_admission.address, true) ||
        !qa_sha256_equal(&client->composition, &n->composition))
        return frontend_fail(error, QA_ERROR_FORMAT, "restored native source is not the declared remote Q3 endpoint/seat");
    *hooks = (qa_q3_client_hooks){.context = n, .generation = client_generation,
        .clear_active = client_clear, .gamestate = client_gamestate, .system_info = client_system_info,
        .snapshot = client_snapshot, .download_size = client_download_size, .download = client_download,
        .command = client_source_command, .map_restart = client_map_restart, .disconnect = client_disconnect,
        .level_shot = client_level_shot, .local_server_running = client_local_server, .send = send_address};
    return true;
}
static qa_network_checkpoint_refs network_saved_refs(qa_frontend_network *n)
{ return (qa_network_checkpoint_refs){n, network_restore_source, network_save_actor, network_restore_actor}; }
static bool network_blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t size = bytes->size;
    if (!qa_source_save_count(io, &size, (size_t)512 * 1048576)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)bytes->data, size);
    if (io->offset > io->input.size || size > io->input.size - io->offset)
        return frontend_fail(io->error, QA_ERROR_FORMAT, "truncated frontend network continuation");
    *bytes = (qa_bytes){io->input.data + io->offset, size}; io->offset += size; return true;
}
static bool network_runtime_valid(qa_frontend_network *n, bool complete_world, qa_error *error)
{
    if (!network_metadata_valid(n, error)) return false;
    uint32_t cursor = 0; const qa_net_client *client = NULL; size_t count = 0;
    while (qa_net_connections_next(qa_network_connections(n->runtime), &cursor, &client)) {
        ++count;
        const qa_q3_client_peer *peer = qa_network_q3_client_view(n->runtime, client->id);
        if (!n->q3_client_attached || !qa_net_client_id_equal(client->id, n->q3_client) || !peer ||
            qa_q3_client_peer_product(peer) != n->q3_client_product ||
            !qa_net_address_equal(&client->endpoint, &n->q3_client_admission.address, true) ||
            !qa_sha256_equal(&client->composition, &n->composition) ||
            (client->phase >= QA_NET_PRIMED && !n->q3_client_gamestate))
            return frontend_fail(error, QA_ERROR_FORMAT, "frontend client state differs from its actual native connection owner");
        if (n->q3_client_gamestate) {
            char map[1024], pure[1024]; qa_application_map_view local;
            const qa_q3_gamestate *state = qa_q3_client_peer_gamestate(peer);
            if (!qa_q3_info_value(qa_q3_configstring(state, 0), "mapname", map, sizeof(map), error) ||
                !qa_q3_info_value(qa_q3_configstring(state, 1), "sv_pure", pure, sizeof(pure), error) ||
                !*map || (complete_world && (!qa_application_map_read(n->frontend->application, &local) || strcmp(local.name, map))) ||
                state->client_number < 0 || state->client_number >= 64 || strtol(pure, NULL, 10) != 0)
                return frontend_fail(error, QA_ERROR_FORMAT, "saved native gamestate differs from the admitted selected map/pure contract");
        }
    }
    return count == (n->q3_client_attached ? 1u : 0u) ||
        frontend_fail(error, QA_ERROR_FORMAT, "frontend and runtime installed connection inventories differ");
}
bool frontend_network_checkpoint(qa_frontend *f, qa_buffer *connections, qa_buffer *prediction, qa_error *error)
{
    if (!f || !f->application || !connections || !prediction || connections == prediction ||
        !frontend_network_world_change_ready(f, error)) return false;
    qa_frontend_network *n = f->network; bool installed = n != NULL;
    if (n && !network_runtime_valid(n, true, error)) return false;
    qa_source_save_io io = {0}, history = {0}; qa_buffer runtime = {0}, browser = {0}, admin = {0}, commands = {0}, jobs = {0};
    bool ok = qa_source_save_writer(&io, qa_application_session(f->application), error) && network_header(&io, &installed) &&
        qa_source_save_writer(&history, qa_application_session(f->application), error) && network_header(&history, &installed);
    if (ok && n) {
        qa_frontend_network *copy = malloc(sizeof(*copy));
        if (!copy) ok = frontend_fail(error, QA_ERROR_MEMORY, "capturing frontend network fields");
        else {
            *copy = *n; qa_net_address address = *qa_network_local_address(n->runtime);
            ok = network_address_fields(&io, &address) && network_frontend_fields(&io, copy);
            free(copy);
        }
        qa_network_checkpoint_refs refs = network_saved_refs(n);
        ok = ok && qa_network_connections_checkpoint(n->runtime, &runtime, error) &&
            qa_server_browser_checkpoint(n->browser, &browser, error) && qa_server_admin_checkpoint(n->admin, &admin, error) &&
            qa_network_prediction_checkpoint(n->runtime, &refs, &commands, error);
        qa_bytes bytes = {runtime.data, runtime.size}; if (ok) ok = network_blob(&io, &bytes);
        bytes = (qa_bytes){browser.data, browser.size}; if (ok) ok = network_blob(&io, &bytes);
        bytes = (qa_bytes){admin.data, admin.size}; if (ok) ok = network_blob(&io, &bytes);
        bool content = n->content != NULL, downloads = n->downloads != NULL;
        if (ok) ok = qa_source_save_bool(&io, &content) && qa_source_save_bool(&io, &downloads);
        if (ok && downloads) {
            qa_download_checkpoint_refs resources = saved_download_refs(n);
            ok = qa_downloads_resources_ready(n->downloads, &resources, error) && qa_downloads_checkpoint(n->downloads, &jobs, error);
            bytes = (qa_bytes){jobs.data, jobs.size}; if (ok) ok = network_blob(&io, &bytes);
        }
        bytes = (qa_bytes){commands.data, commands.size}; if (ok) ok = network_blob(&history, &bytes);
    }
    qa_buffer complete = {0}, predicted = {0};
    if (ok) ok = qa_source_save_finish(&io, &complete) && qa_source_save_finish(&history, &predicted);
    qa_source_save_dispose(&io); qa_source_save_dispose(&history);
    qa_buffer_free(&runtime); qa_buffer_free(&browser); qa_buffer_free(&admin); qa_buffer_free(&commands); qa_buffer_free(&jobs);
    if (!ok) { qa_buffer_free(&complete); qa_buffer_free(&predicted); return false; }
    *connections = complete; *prediction = predicted; return true;
}
bool frontend_network_restore_connections(qa_frontend *f, qa_bytes bytes, qa_error *error)
{
    if (!f || !f->application || (f->network && (!f->network->detached_transport || f->network->busy)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "network continuation requires a detached candidate");
    qa_source_save_io io = {0}; bool installed = false;
    bool ok = qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) &&
        network_header(&io, &installed) && installed == (f->network != NULL);
    if (!ok) { qa_source_save_dispose(&io); return frontend_fail(error, QA_ERROR_FORMAT, "network continuation installed inventory differs from prepared candidate"); }
    if (!installed) {
        if (ok) ok = qa_source_save_finish(&io, NULL);
        qa_source_save_dispose(&io); return ok;
    }
    qa_frontend_network *n = f->network;
    uint32_t previous_cursor = 0; const qa_net_client *previous_client = NULL;
    if (qa_net_connections_next(qa_network_connections(n->runtime), &previous_cursor, &previous_client) || n->q3_projection.owner || n->downloads || n->content) {
        qa_source_save_dispose(&io);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "network continuation may replace only an empty prepared candidate");
    }
    qa_frontend_network *state = calloc(1, sizeof(*state));
    if (!state) { qa_source_save_dispose(&io); return frontend_fail(error, QA_ERROR_MEMORY, "decoding network candidate fields"); }
    state->frontend = f; state->registered = n->registered;
    qa_net_address local = {0}; qa_bytes runtime = {0}, browser = {0}, admin = {0}, jobs = {0};
    bool content = false, downloads = false;
    ok = ok && network_address_fields(&io, &local) && service_address_valid(&local) &&
        network_frontend_fields(&io, state) && network_metadata_valid(state, error) &&
        network_blob(&io, &runtime) && network_blob(&io, &browser) && network_blob(&io, &admin) &&
        qa_source_save_bool(&io, &content) && qa_source_save_bool(&io, &downloads) && (!downloads || content) &&
        (!downloads || network_blob(&io, &jobs)) && qa_source_save_finish(&io, NULL);
    if (ok) {
        qa_network_runtime *previous = n->runtime; qa_server_browser *old_browser = n->browser; qa_server_admin *old_admin = n->admin;
        qa_fs_root *preferences = n->preferences;
        *n = *state; n->detached_transport = true; n->runtime = previous; n->browser = old_browser; n->admin = old_admin; n->preferences = preferences;
        qa_net_transport *transport = NULL; qa_network_runtime *restored = NULL;
        qa_network_options options = saved_network_options(n); qa_network_checkpoint_refs refs = network_saved_refs(n);
        ok = detached_transport(&local, &transport, error);
        if (ok) ok = qa_network_connections_restore(runtime, transport, &options, &refs, &restored, error);
        if (!ok) qa_net_transport_close(transport);
        else { n->runtime = restored; qa_network_destroy(previous); }
        qa_browser_hooks browser_hooks = saved_browser_hooks(n); qa_admin_options admin_options = saved_admin_options(n);
        qa_server_browser *restored_browser = NULL; qa_server_admin *restored_admin = NULL;
        if (ok) ok = qa_server_browser_restore_checkpoint(browser, frontend_tools_http(f), 2048, &browser_hooks, &restored_browser, error) &&
            qa_server_admin_restore_checkpoint(admin, &admin_options, &restored_admin, error);
        if (ok) {
            n->browser = restored_browser; n->admin = restored_admin;
            qa_server_browser_destroy(old_browser); qa_server_admin_destroy(old_admin);
        } else { qa_server_browser_destroy(restored_browser); qa_server_admin_destroy(restored_admin); }
        if (ok && content) ok = qa_fs_root_open(f->options.application.content_root, &n->content, error);
        if (ok && downloads) {
            qa_download_options download_options = saved_download_options(n);
            qa_download_checkpoint_refs resources = saved_download_refs(n);
            ok = qa_downloads_restore_checkpoint(jobs, frontend_tools_http(f), n->content, &download_options, &resources, &n->downloads, error);
        }
        if (ok) ok = network_runtime_valid(n, false, error);
    }
    free(state); qa_source_save_dispose(&io); return ok;
}
bool frontend_network_restore_prediction(qa_frontend *f, qa_bytes bytes, qa_error *error)
{
    if (!f || !f->application || (f->network && !f->network->detached_transport))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "prediction continuation requires detached candidate connections");
    qa_source_save_io io = {0}; bool installed = false; qa_bytes history = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) &&
        network_header(&io, &installed) && installed == (f->network != NULL);
    if (ok && installed) ok = network_blob(&io, &history);
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (ok && installed) {
        qa_network_checkpoint_refs refs = network_saved_refs(f->network);
        ok = network_runtime_valid(f->network, false, error) && qa_network_prediction_restore(f->network->runtime, &refs, history, error);
    }
    qa_source_save_dispose(&io); return ok;
}
bool frontend_network_rebind_ready(const qa_frontend *candidate, const qa_frontend *published, qa_error *error)
{
    if (!candidate || !published || candidate == published || candidate->stepping || published->stepping ||
        (candidate->network != NULL) != (published->network != NULL))
        return frontend_fail(error, QA_ERROR_FORMAT, "network candidate and published service inventories differ");
    const qa_frontend *frontends[2] = {candidate, published};
    for (size_t i = 0; i < 2; ++i) {
        const server_lease *slow = frontends[i]->network_server_leases, *fast = slow;
        for (const server_lease *lease = slow; lease; lease = lease->next) {
            if (lease->frontend != frontends[i])
                return frontend_fail(error, QA_ERROR_FORMAT, "Q3 server wrapper borrows another frontend lifetime");
            slow = slow ? slow->next : NULL;
            fast = fast && fast->next ? fast->next->next : NULL;
            if (fast && fast == slow)
                return frontend_fail(error, QA_ERROR_FORMAT, "Q3 server wrapper lifetime list contains a cycle");
        }
    }
    if (!candidate->network) return true;
    qa_frontend_network *next = candidate->network, *active = published->network;
    if (!next->detached_transport || active->detached_transport || next->frontend != candidate || active->frontend != published ||
        next->busy || active->busy || !qa_network_callbacks_idle(next->runtime) || !qa_network_callbacks_idle(active->runtime) ||
        !qa_http_callbacks_idle(frontend_tools_http((qa_frontend *)candidate)) || !qa_http_callbacks_idle(frontend_tools_http((qa_frontend *)published)) ||
        !qa_net_address_equal(qa_network_local_address(next->runtime), qa_network_local_address(active->runtime), true) ||
        next->q3_client_requested != active->q3_client_requested ||
        (next->q3_client_requested && !qa_net_address_equal(&next->q3_client_admission.address, &active->q3_client_admission.address, true)) ||
        !network_runtime_valid(next, true, error) || !network_runtime_valid(active, true, error))
        return frontend_fail(error, QA_ERROR_FORMAT, "network publication lacks idle qualified endpoint and candidate source ownership");
    if ((next->content || active->content) && strcmp(candidate->options.application.content_root, published->options.application.content_root))
        return frontend_fail(error, QA_ERROR_FORMAT, "download candidate uses another filesystem namespace");
    if (next->downloads) {
        qa_download_checkpoint_refs candidate_resources = saved_download_refs(next);
        if (!qa_downloads_resources_ready(next->downloads, &candidate_resources, error)) return false;
    }
    if (active->downloads) {
        qa_download_checkpoint_refs active_resources = saved_download_refs(active);
        if (!qa_downloads_resources_ready(active->downloads, &active_resources, error)) return false;
    }
    /* A live original peer has no checkpoint barrier. Refuse to rewind its
     * wire state after the captured cut; remote coordination is external. */
    uint32_t cursor = 0; const qa_net_client *client = NULL;
    bool active_connection = qa_net_connections_next(qa_network_connections(active->runtime), &cursor, &client);
    cursor = 0;
    if (!active_connection && !qa_net_connections_next(qa_network_connections(next->runtime), &cursor, &client)) return true;
    qa_buffer current = {0}, restored = {0};
    bool ok = qa_network_connections_checkpoint(active->runtime, &current, error) &&
        qa_network_connections_checkpoint(next->runtime, &restored, error);
    if (ok && (current.size != restored.size || memcmp(current.data, restored.data, current.size)))
        ok = frontend_fail(error, QA_ERROR_UNSUPPORTED, "live original peer advanced beyond the saved protocol continuation cut");
    qa_buffer_free(&current); qa_buffer_free(&restored); return ok;
}
void frontend_network_rebind(qa_frontend *owned, qa_frontend *destination)
{
    if (owned->network) owned->network->frontend = destination;
    for (server_lease *lease = owned->network_server_leases; lease; lease = lease->next) lease->frontend = destination;
}
void frontend_network_transport_exchange(qa_frontend *active, qa_frontend *candidate)
{
    if (!active->network) return;
    qa_network_transport_exchange(active->network->runtime, candidate->network->runtime);
    active->network->detached_transport = true; candidate->network->detached_transport = false;
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
    if (!qa_application_network_q3_client_unproject(f->application, &n->q3_projection, error)) return false;
    if (n->registered && !qa_console_remove_owner(qa_application_console(f->application), NETWORK_OWNER, error)) return false;
    if (n->q3_client_attached && !n->q3_client_retiring && !n->detached_transport) {
        qa_error local = {0};
        if (!qa_network_q3_client_disconnect(n->runtime, n->q3_client,
            (int32_t)((f->time_ns / UINT64_C(1000000)) & INT32_MAX), &local)) frontend_print(f, local.message);
    }
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
    return ok && (!n->q3_admission || q3_drain(n, error)) && client_drain(n, error);
}
bool frontend_network_command(qa_frontend *f, uint32_t seat, qa_actor_id actor,
    const qa_movement_command *movement, qa_error *error)
{
    qa_actor_id current;
    if (!qa_application_player_actor(f->application, seat, &current) || !qa_actor_id_equal(actor, current))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "local network command actor no longer owns its roster seat");
    if (frontend_network_remote(f)) {
        qa_frontend_network *n = f->network;
        if (!n || !n->q3_client_attached || !n->q3_client_gamestate || n->q3_client_retiring) return true;
        if (seat != 0 || movement->kind != QA_MOVEMENT_Q3 ||
            !isfinite(movement->forward_move) || fabsf(movement->forward_move) > 127 ||
            !isfinite(movement->side_move) || fabsf(movement->side_move) > 127 ||
            !isfinite(movement->up_move) || fabsf(movement->up_move) > 127)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 command differs from its admitted source seat or byte range");
        const qa_q3_client_peer *peer = remote_view(f);
        if (!peer || qa_q3_client_peer_usercmd_number(peer) >= INT32_MAX)
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 client command history exhausted signed source ordinals");
        qa_q3_usercmd command = {.serverTime = n->q3_client_time, .buttons = (int32_t)movement->buttons,
            .weapon = (uint8_t)n->q3_weapon, .forwardmove = (int8_t)movement->forward_move,
            .rightmove = (int8_t)movement->side_move, .upmove = (int8_t)movement->up_move};
        memcpy(command.angles, movement->angle_words, sizeof(command.angles));
        return qa_network_q3_client_usercmd(n->runtime, n->q3_client, &command, error);
    }
    return qa_application_control_move(f->application, actor, movement, error);
}
bool frontend_network_publish(qa_frontend *f, qa_error *error)
{
    if (!f->network) return true;
    qa_frontend_network *n = f->network;
    if (n->q3_client_requested) return client_drain(n, error);
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

static void release_server(void *context)
{
    server_lease *lease = context;
    server_lease **link = &lease->frontend->network_server_leases;
    while (*link && *link != lease) link = &(*link)->next;
    if (*link) *link = lease->next;
    if (lease->release) lease->release(lease->lifetime);
    free(lease);
}
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
        const qa_actor_record *record = qa_actors_get(qa_session_actors(qa_application_session(lease->frontend->application)), actor);
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
        const qa_actor_record *record = qa_actors_get(qa_session_actors(qa_application_session(lease->frontend->application)), actor);
        if (record && record->owner == lease->owner && !q3_drop(&n->q3_peers[slot], reason, error)) return false;
    }
    return !lease->original.drop_client || lease->original.drop_client(lease->original.context, slot, reason, error);
}
bool frontend_network_source_services(qa_frontend *f, qa_q3_host_options *host, qa_error *error)
{
    server_lease *lease = malloc(sizeof(*lease));
    if (!lease) return frontend_fail(error, QA_ERROR_MEMORY, "retaining original Q3 server service delegate");
    *lease = (server_lease){.frontend = f, .original = host->server,
        .lifetime = host->frontend_lifetime, .release = host->release_frontend, .owner = host->owner,
        .next = f->network_server_leases};
    f->network_server_leases = lease;
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
