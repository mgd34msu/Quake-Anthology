#include "internal.h"
#include "qa/application_network.h"
#include "qa/application_network_qw.h"
#include "qa/downloads.h"
#include "qa/server_browser.h"
#include "qa/server_admin.h"
#include "qa/launch_identity.h"
#include "qa/network_q3_runtime.h"
#include "qa/network_q3_download.h"
#include "qa/network_save.h"
#include "qa/network_services_save.h"
#include "qa/network_downloads_save.h"
#include "save_private.h"
#include "network_nq_private.h"
#include "network_qw_private.h"
#include "network_q3_restart.h"
#include "network_q3_packages.h"
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
    qa_q3_download_window *download;
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
    uint64_t preparation_nonce;
    uint32_t rotation_random;
    qa_q3_server_admission *q3_admission;
    frontend_nq_host *nq_host;
    frontend_qw_host *qw_host;
    frontend_q3_packages *q3_packages;
    frontend_q3_peer q3_peers[64];
    frontend_q3_pending q3_pending[32];
    size_t q3_pending_count;
    uint64_t q3_generation;
    int32_t q3_server_id, q3_restarted_server_id, q3_checksum_feed;
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
    uint64_t q3_client_generation, q3_client_epoch;
    int32_t q3_client_time, q3_weapon;
    float q3_sensitivity;
    bool q3_client_requested, q3_client_attach, q3_client_attached;
    bool q3_client_gamestate, q3_client_active, q3_client_retiring, q3_command_present, q3_angles_ready, q3_client_entered;
    char q3_client_reason[256];
    char q3_client_userinfo[1024];
    unsigned busy;
    bool registered;
    bool detached_transport;
    qa_network_q3_round *round;
};
struct qa_network_q3_round {
    qa_frontend_network *network;
    qa_frontend *frontend;
    qa_actor_owner source_owner;
    uint64_t generation;
    int32_t server_id, restarted_server_id, checksum_feed;
    uint8_t snapshot_bit;
    size_t count;
    qa_network_q3_round_client clients[64];
    char *userinfo[64];
    qa_buffer native[64];
    uint64_t sequence[64], epoch[64];
    bool queued[64], resolved[64];
    bool begun, bound, finished;
};
static bool round_publish(qa_network_q3_round *, qa_error *);
static bool network_blob(qa_source_save_io *, qa_bytes *);
static bool client_disconnect(void *, const char *, qa_error *);
static const char *const names[] = {"serverlist", "serverquery", "serverfavorite", "servermaster", "setmaster",
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
        qa_actor_owner owner; qa_q3_product product;
        return qa_application_network_q3_owner(n->frontend->application, &owner, &product, error);
    }
    if (n->nq_host && request->protocol.kind == QA_NET_NQ15) {
        qa_actor_id actor; qa_actor_owner owner; uint32_t slot; qa_net_protocol_id protocol;
        return qa_application_player_actor(n->frontend->application, 0, &actor) &&
            qa_application_network_q1_source(n->frontend->application, actor, &owner, &slot, &protocol, error) &&
            ((protocol.kind == QA_NET_NQ15 && !protocol.flags && !protocol.revision) ||
             frontend_fail(error, QA_ERROR_UNSUPPORTED, "NetQuake admission changes its actual classic source dialect"));
    }
    if (n->qw_host && request->protocol.kind == QA_NET_QW28) {
        qa_application_network_qw_world world;
        return !request->protocol.flags && !request->protocol.revision &&
            qa_application_network_qw_world_read(n->frontend->application, &world, error);
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
static bool remote_q3_command(void *context, const qa_network_q3_source_command *command, qa_error *error)
{ return qa_application_network_q3_command(((qa_frontend_network *)context)->frontend->application, command, error); }
static bool remote_qw_commands(void *context, const qa_network_command_group *group, qa_error *error)
{ return qa_application_network_qw_commands(((qa_frontend_network *)context)->frontend->application, group, error); }
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
    if (client && !n->detached_transport && !qa_application_network_detach(n->frontend->application, client, &error))
        frontend_print(n->frontend, error.message);
    frontend_nq_disconnected(n->nq_host, id);
    frontend_qw_disconnected(n->qw_host, id);
    for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied && qa_net_client_id_equal(n->q3_peers[i].client, id)) {
        if (n->q3_admission && client && !n->detached_transport) qa_q3_server_admission_disconnect(n->q3_admission, &client->endpoint);
        qa_q3_download_window_destroy(n->q3_peers[i].download);
        n->q3_peers[i] = (frontend_q3_peer){0};
    }
}
static bool connectionless(void *context, qa_network_runtime *runtime,
    const qa_net_datagram *packet, qa_error *error)
{
    qa_frontend_network *n = context; (void)runtime;
    if (n->nq_host && !qa_server_admin_rejects(n->admin, &packet->from)) {
        bool recognized;
        if (!frontend_nq_receive(n->nq_host, packet, &recognized, error)) return false;
        if (recognized) return true;
    }
    if (n->q3_client_requested) {
        qa_q3_connectionless source; qa_q3_admission_result result;
        if (!qa_q3_client_admission_receive(&n->q3_client_admission, &packet->from, packet->payload,
            (int64_t)(packet->received_ns / UINT64_C(1000000)), &result, &source, error)) return false;
        if (result == QA_Q3_ADMISSION_CONNECTED) { n->q3_client_attach = true; return true; }
        if (result == QA_Q3_ADMISSION_HANDLED || result == QA_Q3_ADMISSION_IGNORED) return true;
        if (!strcmp(qa_q3_token(&source.tokens, 0), "disconnect") && n->q3_client_attached &&
            qa_net_address_equal(&packet->from, &n->q3_client_admission.address, true)) {
            const qa_net_client *client = qa_net_connections_get(qa_network_connections(n->runtime), n->q3_client);
            if (client && packet->received_ns >= client->received_ns &&
                packet->received_ns - client->received_ns >= UINT64_C(3000000000))
                return client_disconnect(n, "Server disconnected", error);
            return true;
        }
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
    if (result != QA_ADMIN_IGNORED || qa_server_admin_rejects(n->admin, &packet->from)) return true;
    if (n->qw_host) {
        if (!frontend_qw_receive(n->qw_host, packet, &recognized, error)) return false;
        if (recognized) return true;
    }
    if (!n->q3_admission) return true;
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
{
    frontend_q3_peer *peer = context; qa_q3_server_world world = peer->world;
    world.downloading = qa_q3_download_window_active(peer->download); return world;
}
static bool q3_download_resolve(void *context, const char *name, qa_bytes *bytes,
    const qa_sha256_digest **digest, qa_error *error)
{
    qa_frontend_network *n = context; qa_error local = {0};
    *bytes = (qa_bytes){0}; *digest = NULL;
    if (frontend_q3_packages_download(n->q3_packages, name, bytes, digest, &local)) return true;
    if (local.code == QA_ERROR_NOT_FOUND) return true;
    if (error) *error = local; return false;
}
static bool q3_rate(frontend_q3_peer *peer, qa_q3_server_rate *out, bool *download_enabled, qa_error *error)
{
    qa_frontend_network *n = peer->network; qa_actor_owner owner; qa_q3_product product; qa_actor_id actor;
    const char *userinfo = NULL;
    if (!out || !qa_application_network_q3_owner(n->frontend->application, &owner, &product, error) ||
        !q3_actor(peer, &actor, error) || !qa_application_network_q3_userinfo_read(n->frontend->application, actor, &userinfo, error)) return false;
    qa_cvars *cvars = qa_application_network_q3_host_cvars(n->frontend->application, owner, error);
    if (!cvars) return false;
    const qa_cvar_view *fps = qa_cvars_find(cvars, "sv_fps"), *maximum = qa_cvars_find(cvars, "sv_maxRate"),
        *enabled = qa_cvars_find(cvars, "sv_allowDownload");
    if (!fps || !maximum || !enabled || !isfinite(fps->number) || !isfinite(maximum->number) || !isfinite(enabled->number))
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 source rate/download policy lacks its actual finite cvar producer");
    char rate_text[1024], snaps_text[1024], ip[1024];
    if (!qa_q3_info_value(userinfo, "rate", rate_text, sizeof(rate_text), error) ||
        !qa_q3_info_value(userinfo, "snaps", snaps_text, sizeof(snaps_text), error) ||
        !qa_q3_info_value(userinfo, "ip", ip, sizeof(ip), error)) return false;
    char *end = NULL; long requested = strtol(rate_text, &end, 10);
    uint32_t rate = end == rate_text ? 3000 : requested < 1000 ? 1000 : requested > 90000 ? 90000 : (uint32_t)requested;
    double frequency = fps->number < 1 ? 1 : fps->number;
    requested = strtol(snaps_text, &end, 10);
    if (end != snaps_text) {
        double selected = requested < 1 ? 1 : requested;
        if (selected < frequency) frequency = selected;
    }
    *out = (qa_q3_server_rate){.bytes_per_second = rate,
        .maximum_rate = maximum->number,
        .snapshot_ms = (uint32_t)(1000 / frequency), .local = !strcmp(ip, "localhost")};
    if (download_enabled) *download_enabled = enabled->number != 0;
    return true;
}
static bool q3_signon(void *context, qa_error *error)
{
    frontend_q3_peer *peer = context; qa_frontend_network *n = peer->network; qa_actor_id actor;
    qa_q3_gamestate *state = malloc(sizeof(*state));
    if (!state) return frontend_fail(error, QA_ERROR_MEMORY, "allocating original Q3 signon observation");
    qa_application_network_q3_package_view packages;
    bool ok = frontend_q3_packages_prepare(n->q3_packages, false, error) &&
        frontend_q3_packages_view(n->q3_packages, &packages, error) &&
        q3_actor(peer, &actor, error) && qa_application_network_q3_signon(n->frontend->application, actor,
        n->q3_server_id, n->q3_checksum_feed, &packages, state, &peer->world, error);
    if (ok) {
        ok = q3_rate(peer, &peer->rate, NULL, error);
        peer->world.restarted_server_id = n->q3_restarted_server_id;
        peer->world.downloading = qa_q3_download_window_active(peer->download);
        if (ok) ok = qa_network_q3_gamestate(n->runtime, peer->client, state, &peer->rate, error);
    }
    free(state); return ok;
}
typedef struct q3_download_packet {
    frontend_q3_peer *peer;
    qa_q3_download_offer offer;
    bool enabled;
} q3_download_packet;
static bool q3_write_downloads(void *context, qa_q3_writer *writer, qa_error *error)
{
    q3_download_packet *packet = context; frontend_q3_peer *peer = packet->peer;
    if (!*qa_q3_download_window_name(peer->download)) return true;
    bool ok = qa_q3_download_window_write(peer->download, packet->enabled, peer->world.pure,
        peer->world.time, &peer->rate, writer, &packet->offer, error);
    peer->world.downloading = qa_q3_download_window_active(peer->download); return ok;
}
static bool q3_snapshot(frontend_q3_peer *peer, qa_error *error)
{
    qa_actor_id actor; qa_frontend_network *n = peer->network;
    bool enabled;
    if (!q3_rate(peer, &peer->rate, &enabled, error)) return false;
    const qa_q3_server_peer *native = qa_network_q3_server_view(n->runtime, peer->client);
    if (!native) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Q3 snapshot has no actual native packet owner");
    if (!qa_q3_server_peer_snapshot_ready(native)) {
        qa_q3_snapshot unused = {.player = {.product = peer->product}};
        return qa_network_q3_snapshot(n->runtime, peer->client, &unused, &peer->rate, NULL, 0, error);
    }
    qa_application_network_q3_frame frame;
    if (!q3_actor(peer, &actor, error) || !qa_application_network_q3_snapshot(n->frontend->application,
        actor, 0, 0, n->q3_server_bit, &frame, error)) return false;
    q3_download_packet packet = {.peer = peer, .enabled = enabled};
    peer->world.downloading = qa_q3_download_window_active(peer->download);
    bool ok = qa_network_q3_snapshot_write(n->runtime, peer->client, &frame.snapshot, &peer->rate,
        q3_write_downloads, &packet, error);
    if (ok && packet.offer.owner) ok = qa_q3_download_window_commit(peer->download, &packet.offer, error);
    peer->world.downloading = qa_q3_download_window_active(peer->download);
    return ok;
}
static bool q3_rejected_snapshot(void *context, qa_error *error)
{ return q3_snapshot(context, error); }
static bool q3_drop(void *context, const char *reason, qa_error *error)
{
    frontend_q3_peer *peer = context; (void)error;
    qa_q3_download_window_close(peer->download); peer->world.downloading = false;
    peer->retiring = true; snprintf(peer->reason, sizeof(peer->reason), "%s", reason); return true;
}
static bool q3_input(void *context, const qa_q3_usercmd *source, qa_error *error)
{
    frontend_q3_peer *peer = context; qa_frontend_network *n = peer->network;
    if (peer->retiring) return true;
    if (peer->sequence == UINT64_MAX) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 command sequence exhausted");
    qa_actor_id actor;
    if (!q3_actor(peer, &actor, error)) return false;
    qa_application_control_view control;
    if (!qa_application_control_read(n->frontend->application, actor, &control))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 input has no actual selected movement owner");
    qa_network_q3_source_command command = {.client = peer->client, .seat = peer->seat, .actor = actor,
        .epoch = qa_network_epoch(n->runtime, peer->client), .sequence = peer->sequence + 1,
        .movement = control.state.kind, .command = *source};
    if (!qa_network_accept_q3_source_command(n->runtime, &command, error)) return false;
    ++peer->sequence; return true;
}
static bool q3_enter(void *context, const qa_q3_usercmd *command, qa_error *error)
{
    frontend_q3_peer *peer = context;
    if (peer->retiring) return true;
    return qa_application_network_q3_enter(peer->network->frontend->application,
        peer->client, peer->seat, command, error);
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
        qa_q3_pure_result result; qa_q3_pure_server pure;
        return frontend_q3_packages_pure(n->q3_packages, n->q3_restarted_server_id, &pure, error) &&
            qa_network_q3_pure(n->runtime, peer->client, &pure, &tokens, &result, error);
    }
    if (!strcmp(name, "vdr")) return qa_network_q3_reset_pure(n->runtime, peer->client, error);
    if (!strcmp(name, "stopdl")) {
        qa_q3_download_window_close(peer->download); peer->world.downloading = false; return true;
    }
    if (!strcmp(name, "nextdl")) {
        long requested = strtol(qa_q3_token(&tokens, 1), NULL, 10);
        int32_t block = requested < INT32_MIN ? INT32_MIN : requested > INT32_MAX ? INT32_MAX : (int32_t)requested;
        bool broken;
        if (!qa_q3_download_window_acknowledge(peer->download, block, peer->world.time, &broken, error)) return false;
        peer->world.downloading = qa_q3_download_window_active(peer->download);
        return !broken || q3_drop(peer, "broken download", error);
    }
    if (!strcmp(name, "donedl")) {
        qa_q3_server_state state;
        return qa_network_q3_state(n->runtime, peer->client, &state, error) &&
            (state.phase == QA_Q3_ACTIVE || q3_signon(peer, error));
    }
    if (!strcmp(name, "download")) {
        char requested[64]; const char *text = qa_q3_token(&tokens, 1);
        size_t length = strlen(text); if (length >= sizeof(requested)) length = sizeof(requested) - 1;
        memcpy(requested, text, length); requested[length] = 0;
        qa_error path = {0};
        if (*requested && !qa_q3_download_name(requested, &path)) return q3_drop(peer, "Invalid download path", error);
        bool ok = qa_q3_download_window_begin(peer->download, requested, error);
        peer->world.downloading = qa_q3_download_window_active(peer->download); return ok;
    }
    qa_actor_id actor;
    if (!q3_actor(peer, &actor, error)) return false;
    if (!strcmp(name, "userinfo"))
        return qa_application_network_q3_userinfo(n->frontend->application, actor, qa_q3_token(&tokens, 1), error);
    if (!allowed) return true;
    qa_actor_owner owner; qa_q3_product product;
    if (!qa_application_network_q3_owner(n->frontend->application, &owner, &product, error)) return false;
    const char *argv[1024]; char args[sizeof(tokens.text)]; size_t used = 0;
    for (size_t i = 0; i < tokens.count; ++i) {
        argv[i] = qa_q3_token(&tokens, i);
        if (!i) continue;
        size_t length = strlen(argv[i]);
        if (length + (i > 1) >= sizeof(args) - used)
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 source command exceeds its actual token arguments");
        if (i > 1) args[used++] = ' ';
        memcpy(args + used, argv[i], length); used += length;
    }
    args[used] = 0;
    qa_command_invocation invocation = {.console = qa_application_console(n->frontend->application),
        .context = {.dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_REMOTE, .owner = owner,
            .actor = actor}, .argc = tokens.count, .argv = argv, .args_text = args, .raw = command->text};
    (void)qa_application_player_seat(n->frontend->application, actor, &invocation.context.seat);
    return qa_application_source_command(n->frontend->application, &invocation, error);
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
    qa_q3_download_window_destroy(peer->download);
    *peer = (frontend_q3_peer){.network = n, .slot = request->slot, .qport = request->qport,
        .seat = {NETWORK_OWNER, 64u + request->slot}, .connected_ms = (int64_t)(n->frontend->time_ns / UINT64_C(1000000)),
        .rate = {.bytes_per_second = 3000, .snapshot_ms = 50}};
    qa_actor_owner source_owner;
    if (!qa_application_network_q3_owner(n->frontend->application, &source_owner, &peer->product, error)) {
        if (retained) (void)qa_network_detach(n->runtime, retained_id, "reconnect source owner unavailable", NULL);
        return false;
    }
    qa_q3_download_source download_source = {.context = n, .resolve = q3_download_resolve};
    if (!qa_q3_download_window_create(&download_source, &peer->download, error)) {
        if (retained) (void)qa_network_detach(n->runtime, retained_id, "reconnect download owner unavailable", NULL);
        return false;
    }
    qa_net_seat_binding seat = {peer->seat, 0};
    qa_net_connect connect = {.attachment = QA_NET_REMOTE, .endpoint = request->address,
        .protocol = {QA_NET_Q3_68, 0, 0}, .seats = &seat, .seat_count = 1, .composition = n->composition};
    qa_q3_server_hooks hooks = {.context = peer, .world = q3_world, .command = q3_client_command,
        .enter_world = q3_enter, .think = q3_input, .resend_gamestate = q3_signon,
        .pure_rejected_snapshot = q3_rejected_snapshot, .drop = q3_drop};
    if (retained) peer->client = retained_id;
    else if (!qa_network_attach_q3_server(n->runtime, &connect, peer->product, request->challenge,
        request->qport, &hooks, n->frontend->time_ns, &peer->client, error)) {
        qa_q3_download_window_destroy(peer->download); peer->download = NULL; return false;
    }
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
        n->q3_restarted_server_id, n->q3_checksum_feed, &peer->world, error);
    if (ok) ok = frontend_q3_packages_prepare(n->q3_packages, false, error);
    if (ok && retained) ok = qa_network_restart(n->runtime, peer->client, &n->composition, error);
    if (ok) {
        qa_q3_gamestate *baselines = calloc(1, sizeof(*baselines));
        if (!baselines) ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining accepted Q3 source baselines");
        else {
            ok = qa_application_network_q3_host_baselines(n->frontend->application,
                source_owner, baselines, error) &&
                qa_network_q3_seed_baselines(n->runtime, peer->client, baselines, error);
            free(baselines);
        }
    }
    if (ok) ok = q3_rate(peer, &peer->rate, NULL, error);
    if (!ok) {
        qa_error rejected = error ? *error : (qa_error){0};
        (void)qa_network_detach(n->runtime, peer->client, "source admission rejected", NULL);
        snprintf(rejection, 1024, "%s", rejected.message);
        if (qa_application_get_state(n->frontend->application) == QA_APPLICATION_FAULTED) return false;
        if (error) *error = (qa_error){0}; return true;
    }
    return true;
}
static bool q3_query(void *context, const qa_net_address *address, const qa_q3_connectionless *packet, qa_error *error)
{
    qa_frontend_network *n = context;
    const char *command = qa_q3_token(&packet->tokens, 0);
    if (strcmp(command, "getinfo") && strcmp(command, "getstatus")) return true;
    bool status = !strcmp(command, "getstatus");
    qa_actor_owner owner; qa_q3_product product;
    qa_application_network_q3_status_player players[64]; size_t count;
    if (!qa_application_network_q3_owner(n->frontend->application, &owner, &product, error) ||
        !qa_application_network_q3_host_status(n->frontend->application, owner, players, &count, error)) return false;
    qa_cvars *cvars = qa_application_network_q3_host_cvars(n->frontend->application, owner, error);
    if (!cvars) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 query source cvars are unavailable");
    const qa_cvar_view *mode = qa_cvars_find(cvars, "g_gametype"), *single = qa_cvars_find(cvars, "ui_singlePlayerActive");
    if ((mode && mode->integer == 2) || (!status && single && single->integer)) return true;
    qa_buffer fields = {0}; char info[8192] = {0};
    if (status && !qa_cvars_info(cvars, QA_CVAR_SERVERINFO, 8192, &fields, error)) return false;
    if (fields.data) { memcpy(info, fields.data, fields.size + 1); qa_buffer_free(&fields); }
    qa_application_map_view map;
    if (!qa_application_map_read(n->frontend->application, &map)) return true;
    uint32_t capacity;
    if (!qa_application_network_q3_host_capacity(n->frontend->application, owner, &capacity, error)) return false;
    const qa_cvar_view *private_clients = qa_cvars_find(cvars, "sv_privateClients");
    uint32_t private_count = private_clients && private_clients->integer > 0 ? (uint32_t)private_clients->integer : 0;
    if (private_count > capacity) private_count = capacity;
    size_t public_count = 0;
    for (size_t i = 0; i < count; ++i) if (players[i].slot >= private_count) ++public_count;
    char clients[32]; snprintf(clients, sizeof(clients), "%zu", status ? count : public_count);
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
        char maximum[32]; snprintf(maximum, sizeof(maximum), "%u", capacity - private_count);
        if (!qa_q3_info_set(info, sizeof(info), keys[i], i ? value ? value->value : "0" : maximum, error)) return false;
    }
    char line[QA_Q3_MESSAGE_BYTES];
    int length = snprintf(line, sizeof(line), "%s\n%s\n", status ? "statusResponse" : "infoResponse", info);
    if (length < 0 || (size_t)length >= sizeof(line)) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 query response exceeds source capacity");
    size_t used = (size_t)length;
    for (size_t i = 0; status && i < count; ++i) {
        char row[1152];
        for (char *p = players[i].name; *p; ++p) if (*p == '"' || *p == '\r' || *p == '\n') *p = ' ';
        length = snprintf(row, sizeof(row), "%d %d \"%s\"\n", players[i].score, players[i].ping, players[i].name);
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
    qa_actor_owner owner; qa_q3_product product; bool occupied[64];
    if (!qa_application_network_q3_owner(n->frontend->application, &owner, &product, error) ||
        !qa_application_network_q3_host_slots(n->frontend->application, owner, occupied, error)) return false;
    uint32_t capacity;
    if (!qa_application_network_q3_host_capacity(n->frontend->application, owner, &capacity, error)) return false;
    *count = capacity;
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
        qa_actor_owner owner; qa_q3_product product;
        if (!qa_application_network_q3_owner(n->frontend->application, &owner, &product, error)) return false;
        qa_cvars *cvars = qa_application_network_q3_host_cvars(n->frontend->application, owner, error);
        if (!cvars) return false;
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
    if (n->round) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source round owns the current network world");
    uint64_t generation = qa_application_configuration_generation(n->frontend->application);
    bool changed = generation != n->q3_generation;
    qa_actor_owner source_owner; qa_q3_product source_product;
    if (!qa_application_network_q3_owner(n->frontend->application, &source_owner, &source_product, error)) return false;
    if (!n->q3_packages || changed) {
        frontend_q3_packages *packages = NULL;
        if (!frontend_q3_packages_create(n->frontend->application, source_owner, (uint32_t)n->q3_checksum_feed, &packages, error)) return false;
        if (changed) for (size_t i = 0; i < 64; ++i) {
            qa_q3_download_window_close(n->q3_peers[i].download); n->q3_peers[i].world.downloading = false;
        }
        frontend_q3_packages_destroy(n->q3_packages); n->q3_packages = packages;
    }
    if (!frontend_q3_packages_prepare(n->q3_packages, false, error)) return false;
    if (changed) {
        if (n->q3_server_id == INT32_MAX) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 server identity exhausted");
        ++n->q3_server_id; n->q3_server_bit ^= 4u;
        n->q3_restarted_server_id = n->q3_server_id;
    }
    qa_buffer identity = {0};
    if (!qa_launch_identity_encode(qa_application_launch(n->frontend->application),
        qa_session_actors(qa_application_session(n->frontend->application)), &identity, error)) return false;
    qa_sha256((qa_bytes){identity.data, identity.size}, &n->composition); qa_buffer_free(&identity);
    n->q3_generation = generation;
    qa_application_network_q3_package_view packages; qa_q3_server_world prepared;
    if (!frontend_q3_packages_view(n->q3_packages, &packages, error) ||
        !qa_application_network_q3_round_prepare(n->frontend->application, source_owner, n->q3_server_id,
            n->q3_restarted_server_id, n->q3_checksum_feed, &packages, &prepared, error)) return false;
    for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied && !n->q3_peers[i].retiring) {
        frontend_q3_peer *peer = &n->q3_peers[i]; qa_actor_id actor;
        if (!q3_actor(peer, &actor, error) || !qa_application_network_q3_world(n->frontend->application, actor,
            n->q3_server_id, n->q3_restarted_server_id, n->q3_checksum_feed, &peer->world, error)) return false;
        peer->world.downloading = qa_q3_download_window_active(peer->download);
        if (changed) {
            peer->sequence = 0;
            if (!qa_network_restart(n->runtime, peer->client, &n->composition, error)) return false;
        }
    }
    return true;
}
qa_save_authority frontend_network_save_authority(const qa_frontend *f)
{
    const qa_frontend_network *n = f ? f->network : NULL;
    if (!n) return QA_SAVE_OFFLINE;
    if (n->q3_client_requested) return QA_SAVE_REMOTE;
    return n->q3_admission || n->nq_host || n->qw_host ? QA_SAVE_SERVER : QA_SAVE_OFFLINE;
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
static bool remote_client_settings(void *context, const qa_net_client *client,
    qa_q3_client_readiness *ready, qa_q3_client_send *send, qa_error *error)
{
    qa_frontend_network *n = context; qa_application_q3_client_context role;
    if (!n || !n->q3_client_requested || !n->q3_client_attached || n->q3_client_retiring ||
        !client || !qa_net_client_id_equal(client->id, n->q3_client) || client->seat_count != 1 ||
        client->seats[0].seat.owner != NETWORK_OWNER || client->seats[0].seat.index ||
        client->seats[0].remote_index || !qa_net_address_equal(&client->endpoint, &n->q3_client_admission.address, true) ||
        !qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner, 0, &role, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 send policy lost its actual receiver and admitted transport seat");
    const qa_cvar_view *maximum = qa_cvars_find(role.cvars, "cl_maxpackets"),
        *duplicate = qa_cvars_find(role.cvars, "cl_packetdup");
    if (!maximum || !duplicate)
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 client send policy lacks its installed receiver cvars");
    ready->maximum_packets = maximum->integer < 15 ? 15 : maximum->integer > 125 ? 125 : (unsigned)maximum->integer;
    ready->active = n->q3_client_active;
    ready->primed = n->q3_client_gamestate;
    send->packet_dup = duplicate->integer < 0 ? 0 : duplicate->integer > 5 ? 5 : (unsigned)duplicate->integer;
    send->no_delta = false;
    return true;
}
bool frontend_network_q3_client_context_read(qa_frontend *f, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_context *out, qa_error *error)
{
    qa_frontend_network *n = f ? f->network : NULL;
    const qa_q3_client_peer *peer = n ? remote_view(f) : NULL;
    if (!n || !n->q3_client_requested || n->q3_client_retiring || seat || receiver != n->q3_cgame_owner || !peer ||
        !qa_application_q3_remote_context_read(f->application, receiver, seat, out, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 context lost its actual connection and CGAME role");
    if (n->q3_client_gamestate) {
        const qa_q3_gamestate *state = qa_q3_client_peer_gamestate(peer);
        if (!state || state->client_number < 0 || state->client_number >= 64)
            return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 context has no admitted physical client ordinal");
        out->source_client = (uint32_t)state->client_number;
    }
    out->source_milliseconds = n->q3_client_time;
    return true;
}
bool frontend_network_q3_client_context_current(qa_frontend *f,
    const qa_application_q3_client_context *context)
{
    qa_frontend_network *n = f ? f->network : NULL;
    const qa_q3_client_peer *peer = n ? remote_view(f) : NULL;
    if (!n || !context || !n->q3_client_requested || n->q3_client_retiring || !peer || context->seat ||
        context->receiver != n->q3_cgame_owner || !qa_application_q3_remote_context_current(f->application, context)) return false;
    const qa_q3_gamestate *state = n->q3_client_gamestate ? qa_q3_client_peer_gamestate(peer) : NULL;
    return context->source_client == (state && state->client_number >= 0 ? (uint32_t)state->client_number : UINT32_MAX);
}
static uint64_t client_generation(void *context)
{ qa_frontend_network *n = context; return n->q3_client_epoch; }
static bool client_clear(void *context, qa_error *error)
{
    qa_frontend_network *n = context;
    n->q3_client_gamestate = false; n->q3_client_active = false;
    n->q3_angles_ready = false; n->q3_client_entered = false;
    qa_q3_clock_clear(&n->q3_client_clock);
    return qa_application_network_q3_client_unproject(n->frontend->application, &n->q3_projection, error) &&
        qa_application_network_q3_client_clear(n->frontend->application, n->q3_cgame_owner, 0, error);
}
static bool client_system_info(void *context, const char *info, qa_error *error)
{
    qa_frontend_network *n = context; char value[1024];
    if (!qa_q3_info_value(info, "sv_pure", value, sizeof(value), error)) return false;
    if (strtol(value, NULL, 10) != 0)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 remote pure admission requires live package-reference validation");
    qa_application_q3_client_context role;
    return frontend_network_q3_client_context_read(n->frontend, n->q3_cgame_owner, 0, &role, error) &&
        frontend_source_system_info(n->frontend, &role, info, error);
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
    qa_frontend_network *n = context;
    if (!n->q3_client_attached || !qa_network_q3_client_retire(n->runtime, n->q3_client, error)) return false;
    n->q3_client_retiring = true;
    snprintf(n->q3_client_reason, sizeof(n->q3_client_reason), "%s", reason); return true;
}
bool frontend_network_q3_client_effect(qa_frontend *f,
    const qa_application_q3_client_context *context, qa_application_q3_client_effect effect,
    const char *text, qa_error *error)
{
    qa_frontend_network *n = f ? f->network : NULL;
    if (!n || !context || !n->q3_client_requested || !n->q3_client_attached ||
        context->receiver != n->q3_cgame_owner || context->seat != 0 ||
        context->session != qa_application_session(f->application) ||
        !frontend_network_q3_client_context_current(f, context))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 client effect lost its actual CGAME receiver and connection");
    if (effect == QA_APPLICATION_Q3_MAP_RESTART) return client_map_restart(n, error);
    if (effect == QA_APPLICATION_Q3_DISCONNECT) return client_disconnect(n, text ? text : "disconnected", error);
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 effect belongs to another concrete frontend service");
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
    if (n->q3_client_generation != qa_application_configuration_generation(n->frontend->application))
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
        qa_network_q3_client_policy policy = {n, remote_client_settings};
        if (!qa_network_attach_q3_client(n->runtime, &request, n->q3_client_product,
            n->q3_client_admission.challenge, n->q3_client_admission.qport, &hooks,
            &policy, n->frontend->time_ns, &n->q3_client, error)) return false;
        n->q3_client_attached = true;
    }
    if (!n->q3_client_attached) {
        qa_application_q3_client_context role;
        qa_buffer info = {0};
        if (!qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner, 0, &role, error) ||
            !qa_cvars_info(role.cvars, QA_CVAR_USERINFO, 1024, &info, error)) return false;
        bool ok = qa_q3_client_admission_resend(&n->q3_client_admission,
            (int64_t)(n->frontend->time_ns / UINT64_C(1000000)), (const char *)info.data, send_address, n, error);
        qa_buffer_free(&info); return ok;
    }
    qa_application_q3_client_context role; qa_buffer info = {0};
    if (!qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner, 0, &role, error) ||
        !qa_cvars_info(role.cvars, QA_CVAR_USERINFO, sizeof(n->q3_client_userinfo), &info, error)) return false;
    bool updated = true;
    if (strcmp(n->q3_client_userinfo, (const char *)info.data)) {
        char command[sizeof(n->q3_client_userinfo) + 12];
        snprintf(command, sizeof(command), "userinfo \"%s\"", (const char *)info.data);
        updated = qa_network_q3_client_command(n->runtime, n->q3_client, command, error);
        if (updated) memcpy(n->q3_client_userinfo, info.data, info.size + 1);
    }
    qa_buffer_free(&info); if (!updated) return false;
    if (n->q3_client_gamestate && !n->q3_client_entered) {
        qa_q3_usercmd initial = {0};
        if (!qa_network_q3_client_usercmd(n->runtime, n->q3_client, &initial, error)) return false;
        n->q3_client_entered = true;
    }
    qa_q3_clock_options options = {.timescale = 1};
    const qa_cvar_view *nudge = qa_cvars_find(role.cvars, "cl_timeNudge");
    const qa_cvar_view *scale = qa_cvars_find(role.cvars, "timescale");
    if (scale) options.timescale = scale->number;
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
    if (!frontend_network_remote(f) || role != QA_QVM_CGAME) return true;
    if (seat != 0 || f->options.seats != 1)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 remote connection owns one local client seat");
    static const struct { const char *name, *value; uint32_t flags; } defaults[] = {
        {"cl_allowDownload", "0", QA_CVAR_ARCHIVE},
        {"cl_timeNudge", "0", QA_CVAR_TEMPORARY},
        {"rate", "25000", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"cl_maxpackets", "30", QA_CVAR_ARCHIVE},
        {"cl_packetdup", "1", QA_CVAR_ARCHIVE},
        {"snaps", "20", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"name", "Player", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"color1", "4", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"color2", "5", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"sex", "male", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"cl_anonymous", "0", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"cg_predictItems", "1", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"teamtask", "0", QA_CVAR_USERINFO},
        {"password", "", QA_CVAR_USERINFO},
        {"handicap", "100", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"cl_maxPing", "800", QA_CVAR_ARCHIVE},
        {"cl_serverStatusResendTime", "750", 0},
        {"sv_master1", "master.quake3arena.com", 0}
    };
    if (!host->cvars) return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 role lacks its actual client registry");
    for (size_t i = 0; i < sizeof(defaults) / sizeof(*defaults); ++i)
        if (!qa_cvars_register(host->cvars, defaults[i].name, defaults[i].value,
            defaults[i].flags, owner, "Original Q3 client policy", error)) return false;
    host->client = (qa_q3_host_client_services){f, service_gamestate, service_current_snapshot,
        service_snapshot, service_server_command, service_current_command, service_user_command,
        service_command_values, service_source_actor};
    host->client_time_cvars = host->cvars;
    host->client_time_owner = owner;
    return true;
}
bool frontend_network_client_command_seat(qa_frontend *f, uint32_t seat, const char *text, qa_error *error)
{
    qa_frontend_network *n = f ? f->network : NULL; qa_application_q3_client_context role;
    if (!n || seat || !text || !frontend_network_q3_client_context_read(f, n->q3_cgame_owner, seat, &role, error) ||
        !frontend_network_q3_client_context_current(f, &role))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote client command lost its actual Q3 receiver and connection seat");
    return qa_network_q3_client_command(n->runtime, n->q3_client, text, error);
}
bool frontend_network_client_command(qa_frontend *f, const char *text, qa_error *error)
{ return frontend_network_client_command_seat(f, 0, text, error); }
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
    if (!n->preparation_nonce) n->preparation_nonce = n->nonce;
    if (!out || *out || !nonce || *nonce || n->preparation_nonce == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "candidate download stage allocation is exhausted");
    uint64_t fresh = n->preparation_nonce + 1;
    if (fresh == view->stage_nonce) {
        if (fresh == UINT64_MAX) return frontend_fail(error, QA_ERROR_ARGUMENT, "candidate download stage namespace is exhausted");
        ++fresh;
    }
    qa_fs_stage *stage = NULL; uint64_t initial = 0; size_t written = 0;
    if (!qa_fs_stage_open(n->content, request->path, fresh, false, &stage, &initial, error)) return false;
    bool ok = !initial && qa_fs_stage_write(stage, 0, prefix, &written, error) && written == prefix.size;
    if (!ok) { qa_fs_stage_close(stage, false); return false; }
    n->preparation_nonce = fresh; *nonce = fresh; *out = stage; return true;
}
static bool download_saved_artifact(void *context, const qa_download_request *request,
    const qa_download_view *view, qa_fs_stage **out, qa_error *error)
{
    qa_frontend_network *n = context; uint64_t size = 0;
    return out && !*out && qa_fs_stage_open_readonly(n->content, request->path, view->stage_nonce, out, &size, error);
}
static qa_download_checkpoint_refs saved_download_refs(qa_frontend_network *n)
{ return (qa_download_checkpoint_refs){.context = n, .resource = download_saved_resource,
    .stage = download_saved_stage, .artifact = download_saved_artifact}; }
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
    if (!strcmp(name, "setmaster")) {
        if (call->argc > 33) return frontend_fail(error, QA_ERROR_ARGUMENT, "setmaster accepts at most 32 admitted master addresses");
        qa_net_address masters[32];
        uint16_t port = n->frontend->options.network_protocol.kind == QA_NET_QW28 ? 27000 : 27950;
        for (size_t i = 1; i < call->argc; ++i)
            if (!qa_net_address_parse(call->argv[i], port, false, masters + i - 1, error)) return false;
        return qa_server_admin_masters(n->admin, masters, (size_t)call->argc - 1, error) &&
            qa_server_admin_tick(n->admin, n->frontend->time_ns, true, error);
    }
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
        (f->options.network_host && f->options.network_protocol.kind != QA_NET_Q3_68 &&
         ((f->options.network_protocol.kind != QA_NET_NQ15 && f->options.network_protocol.kind != QA_NET_QW28) ||
          f->options.network_protocol.flags || f->options.network_protocol.revision)))
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
        n->q3_client_generation = qa_application_configuration_generation(f->application); n->q3_client_epoch = 1;
        if (!qa_launch_identity_encode(qa_application_launch(f->application),
            qa_session_actors(qa_application_session(f->application)), &identity, error)) goto failed;
        qa_sha256((qa_bytes){identity.data, identity.size}, &n->composition); qa_buffer_free(&identity);
    }
    if (f->options.network_host) {
        if (!qa_net_address_parse(f->options.network_host, f->options.network_port, false, &udp.bind, error)) goto failed;
        if (udp.bind.kind != QA_NET_IPV4 && udp.bind.kind != QA_NET_IPV6)
            { frontend_fail(error, QA_ERROR_UNSUPPORTED, "hosting transport requires the admitted UDP address family"); goto failed; }
        udp.ipv6_only = udp.bind.kind == QA_NET_IPV6; udp.broadcast = udp.bind.kind == QA_NET_IPV4;
        if (f->options.network_protocol.kind == QA_NET_Q3_68) {
            qa_q3_admission_hooks hooks = {.context = n, .random = random_rotation, .send = send_address,
                .admit = q3_admit, .query = q3_query};
            if (!qa_q3_server_admission_create(&hooks, &n->q3_admission, error)) goto failed;
            n->q3_server_id = n->q3_restarted_server_id = 1; n->q3_checksum_feed = (int32_t)random_rotation(n);
            if (!q3_prepare(n, error)) goto failed;
            qa_actor_owner owner; qa_q3_product product; qa_q3_server_world world;
            qa_application_network_q3_package_view packages;
            if (!qa_application_network_q3_owner(f->application, &owner, &product, error) ||
                !frontend_q3_packages_view(n->q3_packages, &packages, error) ||
                !qa_application_network_q3_round_prepare(f->application, owner, n->q3_server_id,
                    n->q3_restarted_server_id, n->q3_checksum_feed, &packages, &world, error)) goto failed;
        } else {
            qa_buffer identity = {0};
            if (!qa_launch_identity_encode(qa_application_launch(f->application),
                qa_session_actors(qa_application_session(f->application)), &identity, error)) goto failed;
            qa_sha256((qa_bytes){identity.data, identity.size}, &n->composition); qa_buffer_free(&identity);
        }
    }
    qa_net_transport *transport = NULL;
    qa_network_options options = {.owner = NETWORK_OWNER, .clients = 64, .packets_per_pump = 256,
        .timeout_ns = n->q3_client_requested ? UINT64_C(120000000000) :
            f->options.network_host && (f->options.network_protocol.kind == QA_NET_NQ15 || f->options.network_protocol.kind == QA_NET_QW28) ?
            UINT64_C(65000000000) : UINT64_C(30000000000),
        .hooks = {.context = n, .admit = admit, .controlled = controlled,
        .command = remote_command, .disconnected = disconnected, .connectionless = connectionless,
        .q3_source_command = remote_q3_command, .commands = remote_qw_commands}};
    options.hooks.reconnect = reconnect;
    if (!qa_net_udp_open(&udp, &transport, error)) goto failed;
    if (!qa_network_create(transport, &options, &n->runtime, error)) { qa_net_transport_close(transport); goto failed; }
    if (f->options.network_host && f->options.network_protocol.kind == QA_NET_NQ15 &&
        !frontend_nq_create(f, n->runtime, &n->composition, &n->nq_host, error)) goto failed;
    qa_browser_hooks browser = {.context = n, .send = send_address, .local = local_address};
    qa_admin_options admin = {.dialect = qa_cvars_dialect(qa_application_cvars(f->application)),
        .filters = 1024, .rate_entries = 1024, .burst = 10, .rate_interval_ns = UINT64_C(1000000000),
        .heartbeat_interval_ns = UINT64_C(300000000000), .deny_matches = true,
        .public_server = f->options.network_host && f->options.network_protocol.kind == QA_NET_QW28,
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
    if (f->options.network_host && f->options.network_protocol.kind == QA_NET_QW28 &&
        !frontend_qw_create(f, n->runtime, n->admin, &n->composition, &n->qw_host, error)) goto failed;
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
        .timeout_ns = n->q3_client_requested ? UINT64_C(120000000000) :
            n->frontend->options.network_host && (n->frontend->options.network_protocol.kind == QA_NET_NQ15 || n->frontend->options.network_protocol.kind == QA_NET_QW28) ?
            UINT64_C(65000000000) : UINT64_C(30000000000), .hooks = {.context = n, .admit = admit, .controlled = controlled,
        .command = remote_command, .disconnected = disconnected, .connectionless = connectionless,
        .reconnect = reconnect, .q3_source_command = remote_q3_command, .commands = remote_qw_commands}};
}
static qa_browser_hooks saved_browser_hooks(qa_frontend_network *n)
{ return (qa_browser_hooks){.context = n, .send = send_address, .local = local_address}; }
static qa_q3_admission_hooks saved_admission_hooks(qa_frontend_network *n)
{ return (qa_q3_admission_hooks){.context = n, .random = random_rotation, .send = send_address, .admit = q3_admit, .query = q3_query}; }
static qa_admin_options saved_admin_options(qa_frontend_network *n)
{
    return (qa_admin_options){.dialect = qa_cvars_dialect(qa_application_cvars(n->frontend->application)),
        .filters = 1024, .rate_entries = 1024, .burst = 10, .rate_interval_ns = UINT64_C(1000000000),
        .heartbeat_interval_ns = UINT64_C(300000000000), .deny_matches = true,
        .public_server = n->frontend->options.network_host && n->frontend->options.network_protocol.kind == QA_NET_QW28,
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
    uint32_t magic = UINT32_C(0x464e4151), version = 9;
    return qa_source_save_u32(io, &magic) && magic == UINT32_C(0x464e4151) &&
        qa_source_save_u32(io, &version) && version == 9 && qa_source_save_bool(io, installed);
}
bool frontend_network_prepare_restored(qa_frontend *f, qa_bytes bytes, qa_error *error)
{
    if (!f || !f->application || f->network || !frontend_tools_http(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "network restore preparation requires detached application and HTTP owners");
    qa_source_save_io io = {0}; bool installed = false;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && network_header(&io, &installed);
    qa_source_save_dispose(&io); if (!ok) return false;
    if (!installed) return true;
    bool nq_host = f->options.network_protocol.kind == QA_NET_NQ15 &&
        !f->options.network_protocol.flags && !f->options.network_protocol.revision;
    bool qw_host = f->options.network_protocol.kind == QA_NET_QW28 &&
        !f->options.network_protocol.flags && !f->options.network_protocol.revision;
    if ((f->options.network_host && ((f->options.network_protocol.kind != QA_NET_Q3_68 && !nq_host && !qw_host) || f->options.network_connect)) ||
        (f->options.network_connect && !frontend_network_remote(f)))
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
        !qa_source_save_u64(io, &n->q3_client_epoch) ||
        !qa_source_save_i32(io, &n->q3_client_time) || !qa_source_save_i32(io, &n->q3_weapon) ||
        !qa_source_save_f32(io, &n->q3_sensitivity) || !isfinite(n->q3_sensitivity) ||
        !qa_source_save_bool(io, &n->q3_client_attach) || !qa_source_save_bool(io, &n->q3_client_attached) ||
        !qa_source_save_bool(io, &n->q3_client_gamestate) || !qa_source_save_bool(io, &n->q3_client_active) ||
        !qa_source_save_bool(io, &n->q3_client_retiring) || !qa_source_save_bool(io, &n->q3_command_present) ||
        !qa_source_save_bool(io, &n->q3_angles_ready) || !qa_source_save_bool(io, &n->q3_client_entered) ||
        !qa_source_save_bytes(io, n->q3_client_reason, sizeof(n->q3_client_reason)) ||
        !memchr(n->q3_client_reason, 0, sizeof(n->q3_client_reason)) ||
        !qa_source_save_bytes(io, n->q3_client_userinfo, sizeof(n->q3_client_userinfo)) ||
        !memchr(n->q3_client_userinfo, 0, sizeof(n->q3_client_userinfo)) ||
        !frontend_save_provider(io, n->frontend->application, &n->q3_projection.owner) ||
        !qa_source_save_string(io, &n->q3_projection.definition) || !qa_source_save_u8(io, &n->q3_projection_epoch)) return false;
    n->q3_client_admission.phase = (qa_q3_admission_phase)phase; n->q3_client_product = (qa_q3_product)product;
    n->q3_client.owner = n->q3_client.generation ? NETWORK_OWNER : 0;
    for (size_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i) if (!qa_source_save_actor(io, &n->q3_projection.actors[i])) return false;
    return true;
}
static bool network_host_fields(qa_source_save_io *io, qa_frontend_network *n, bool *installed,
    qa_bytes *package_cut, qa_bytes download_cuts[64])
{
    if (!qa_source_save_bool(io, installed)) return false;
    if (!*installed) return true;
    if (!qa_source_save_u64(io, &n->q3_generation) || !qa_source_save_i32(io, &n->q3_server_id) ||
        !qa_source_save_i32(io, &n->q3_restarted_server_id) ||
        !qa_source_save_i32(io, &n->q3_checksum_feed) || !qa_source_save_u8(io, &n->q3_server_bit) ||
        !qa_source_save_count(io, &n->q3_pending_count, 32)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!package_cut || !network_blob(io, package_cut)) return false;
    } else {
        qa_buffer buffer = {0};
        bool ok = frontend_q3_packages_checkpoint(n->q3_packages, &buffer, io->error);
        qa_bytes bytes = {buffer.data, buffer.size};
        if (ok) ok = network_blob(io, &bytes);
        qa_buffer_free(&buffer); if (!ok) return false;
    }
    for (size_t i = 0; i < 64; ++i) {
        frontend_q3_peer *p = &n->q3_peers[i];
        uint32_t product = p->product;
        if (!qa_source_save_bool(io, &p->occupied)) return false;
        if (!p->occupied) continue; /* Ordinary disconnect clears the entire row. */
        if (!qa_source_save_u64(io, &p->client.generation) || !qa_source_save_u32(io, &p->client.slot) ||
            !qa_source_save_u64(io, &p->seat.owner) || !qa_source_save_u32(io, &p->seat.index) ||
            !qa_source_save_u64(io, &p->world.generation) || !qa_source_save_i32(io, &p->world.server_id) ||
            !qa_source_save_i32(io, &p->world.restarted_server_id) || !qa_source_save_i32(io, &p->world.checksum_feed) ||
            !qa_source_save_i32(io, &p->world.time) || !qa_source_save_bool(io, &p->world.pure) ||
            !qa_source_save_bool(io, &p->world.client_running) || !qa_source_save_bool(io, &p->world.flood_protect) ||
            !qa_source_save_bool(io, &p->world.downloading) || !qa_source_save_u32(io, &p->rate.bytes_per_second) ||
            !qa_source_save_f64(io, &p->rate.maximum_rate) || !qa_source_save_u32(io, &p->rate.snapshot_ms) ||
            !qa_source_save_bool(io, &p->rate.local) || !qa_source_save_bool(io, &p->rate.lan) ||
            !qa_source_save_bool(io, &p->rate.force_lan) || !qa_source_save_u32(io, &product) || product > QA_Q3_TEAM_ARENA ||
            !qa_source_save_u64(io, &p->sequence) || !qa_source_save_i64(io, &p->connected_ms) ||
            !qa_source_save_u32(io, &p->slot) || !qa_source_save_u16(io, &p->qport) ||
            !qa_source_save_bool(io, &p->retiring) || !qa_source_save_bytes(io, p->reason, sizeof(p->reason)) ||
            !memchr(p->reason, 0, sizeof(p->reason))) return false;
        p->client.owner = NETWORK_OWNER; p->product = (qa_q3_product)product;
        if (io->direction == QA_SOURCE_SAVE_READ) p->network = n;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (!download_cuts || !network_blob(io, download_cuts + i)) return false;
        } else {
            qa_buffer buffer = {0}; bool ok = qa_q3_download_window_checkpoint(p->download, &buffer, io->error);
            qa_bytes bytes = {buffer.data, buffer.size}; if (ok) ok = network_blob(io, &bytes);
            qa_buffer_free(&buffer); if (!ok) return false;
        }
    }
    /* Draining retains physical packet rows. Keep their last extent and bytes,
     * although only the active prefix is scheduled for the next drain. */
    for (size_t i = 0; i < 32; ++i) {
        frontend_q3_pending *p = &n->q3_pending[i];
        if (!network_address_fields(io, &p->address) ||
            !qa_source_save_count(io, &p->size, QA_Q3_MESSAGE_BYTES) ||
            !qa_source_save_bytes(io, p->bytes, p->size)) return false;
        if (p->size && (p->size < 4 || !p->address.port ||
            (p->address.kind != QA_NET_IPV4 && p->address.kind != QA_NET_IPV6) ||
            qa_load_u32le(p->bytes) != UINT32_MAX)) return false;
        if (i < n->q3_pending_count && !p->size) return false;
    }
    return true;
}
static bool network_host_player(qa_frontend_network *n, const frontend_q3_peer *p,
    qa_application_network_player *out, qa_error *error)
{
    size_t cursor = 0; qa_application_network_player row; bool found = false;
    while (qa_application_network_player_next(n->frontend->application, &cursor, &row)) {
        if (!qa_net_client_id_equal(row.client, p->client) || row.seat.owner != p->seat.owner || row.seat.index != p->seat.index) continue;
        if (found || row.application_seat != p->seat.index || row.client_slot != p->slot ||
            (!p->retiring && (row.retiring || row.deferred)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 frontend peer differs from its actual remote roster row");
        *out = row; found = true;
    }
    if (!found) return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 pending peer has no retained application roster owner");
    if (!out->retiring) {
        uint32_t slot; qa_q3_product product;
        if (!qa_application_network_q3_source(n->frontend->application, out->actor, &slot, &product, error) ||
            slot != p->slot || product != p->product)
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 restored source projection differs from its declared peer slot/product");
    }
    return true;
}
static bool network_metadata_check(qa_frontend_network *n, bool hosting, bool finishing_round, qa_error *error)
{
    qa_application *app = n->frontend->application;
    bool nq = n->nq_host != NULL, qw = n->qw_host != NULL;
    if ((n->round && !finishing_round) || n->q3_reconnect || (n->downloads && !n->content) ||
        ((unsigned)hosting + (unsigned)nq + (unsigned)qw > 1) ||
        (hosting || nq || qw) != (n->frontend->options.network_host != NULL) ||
        n->q3_client_requested != frontend_network_remote(n->frontend) || !n->registered || n->q3_projection_epoch > 4 ||
        (n->q3_projection_epoch != 0 && n->q3_projection_epoch != 4))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "installed frontend network service lacks a complete continuation consumer");
    if (nq && (n->q3_client_requested || n->frontend->options.network_protocol.kind != QA_NET_NQ15 ||
        n->frontend->options.network_protocol.flags || n->frontend->options.network_protocol.revision || n->q3_projection.owner))
        return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake hosting continuation has another installed dialect consumer");
    if (qw && (n->q3_client_requested || n->frontend->options.network_protocol.kind != QA_NET_QW28 ||
        n->frontend->options.network_protocol.flags || n->frontend->options.network_protocol.revision || n->q3_projection.owner))
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld hosting continuation has another installed dialect consumer");
    if (hosting) {
        if (n->q3_client_requested || n->frontend->options.network_protocol.kind != QA_NET_Q3_68 ||
            !n->q3_generation || n->q3_server_id <= 0 || n->q3_restarted_server_id <= 0 ||
            n->q3_restarted_server_id > n->q3_server_id || (n->q3_server_bit != 0 && n->q3_server_bit != 4))
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 hosting continuation has a foreign source generation/dialect");
        qa_actor_owner source_owner; qa_q3_product source_product;
        if (!qa_application_network_q3_owner(app, &source_owner, &source_product, error)) return false;
        qa_q3_pure_server package_policy;
        if (!frontend_q3_packages_check(n->q3_packages, error) ||
            !frontend_q3_packages_pure(n->q3_packages, n->q3_restarted_server_id, &package_policy, error)) return false;
        for (size_t i = 0; i < 64; ++i) {
            frontend_q3_peer *p = &n->q3_peers[i]; if (!p->occupied) continue;
            qa_application_network_player row;
            if (p->slot != i || !p->client.generation || p->client.owner != NETWORK_OWNER || p->client.slot >= 64 ||
                p->seat.owner != NETWORK_OWNER || p->seat.index != 64u + i || p->product != source_product ||
                p->connected_ms < 0 || p->world.server_id <= 0 || p->world.restarted_server_id <= 0 ||
                !p->world.generation || p->world.pure != package_policy.enabled || p->world.client_running || !p->download ||
                p->world.downloading != qa_q3_download_window_active(p->download) ||
                p->world.checksum_feed != n->q3_checksum_feed || p->rate.lan || p->rate.force_lan ||
                !isfinite(p->rate.maximum_rate) ||
                p->rate.bytes_per_second < 1000 || p->rate.bytes_per_second > 90000 || p->rate.snapshot_ms > 1000 ||
                (!p->retiring && (p->world.generation != n->q3_generation || p->world.server_id != n->q3_server_id ||
                    p->world.restarted_server_id != n->q3_restarted_server_id)) || !network_host_player(n, p, &row, error))
                return frontend_fail(error, QA_ERROR_FORMAT, "Q3 hosting peer lacks its retained source identity/rate/roster");
            for (size_t j = 0; j < i; ++j) if (n->q3_peers[j].occupied && qa_net_client_id_equal(n->q3_peers[j].client, p->client))
                return frontend_fail(error, QA_ERROR_FORMAT, "Q3 source slots alias one runtime connection");
        }
    } else if (n->q3_packages || n->q3_pending_count || n->q3_generation || n->q3_server_id || n->q3_restarted_server_id || n->q3_checksum_feed || n->q3_server_bit)
        return frontend_fail(error, QA_ERROR_FORMAT, "absent hosting owner retains source state");
    if (n->q3_client_requested) {
        qa_actor_id actor; qa_actor_owner owner; qa_q3_product product;
        if (n->frontend->options.dedicated || n->frontend->options.seats != 1 ||
            !qa_application_player_actor(app, 0, &actor) ||
            !qa_application_network_q3_client_source(app, actor, &owner, &product, error) ||
            owner != n->q3_cgame_owner || product != n->q3_client_product ||
            n->q3_client_generation != qa_application_configuration_generation(app) || n->q3_client_epoch != 1 ||
            (n->q3_client_admission.address.kind != QA_NET_IPV4 && n->q3_client_admission.address.kind != QA_NET_IPV6) ||
            !n->q3_client_admission.address.port || n->q3_client.slot >= 64 ||
            (n->q3_client_attached && (!n->q3_client.generation || n->q3_client_admission.phase != QA_Q3_ADMITTED)) ||
            (n->q3_client_entered && !n->q3_client_gamestate && !n->q3_client_retiring) ||
            (n->q3_client_active && (!n->q3_client_attached || !n->q3_client_gamestate || !n->q3_client_clock.active)))
            return frontend_fail(error, QA_ERROR_FORMAT, "remote Q3 continuation differs from its selected source and seat");
    } else if (n->q3_client_attach || n->q3_client_attached || n->q3_client_gamestate || n->q3_client_active || n->q3_projection.owner ||
        n->q3_client_epoch || n->q3_client_entered || *n->q3_client_userinfo)
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
static bool network_metadata_valid(qa_frontend_network *n, bool hosting, qa_error *error)
{ return network_metadata_check(n, hosting, false, error); }
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
    qa_frontend_network *n = context;
    if (kind == QA_NETWORK_SOURCE_Q3_SERVER) {
        frontend_q3_peer *p = NULL;
        for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied && qa_net_client_id_equal(n->q3_peers[i].client, client->id)) p = &n->q3_peers[i];
        qa_application_network_player row;
        if (!n->q3_admission || !p || client->attachment != QA_NET_REMOTE || client->protocol.kind != QA_NET_Q3_68 ||
            client->protocol.revision || client->protocol.flags || client->seat_count != 1 ||
            client->seats[0].seat.owner != p->seat.owner || client->seats[0].seat.index != p->seat.index ||
            client->seats[0].remote_index || !qa_sha256_equal(&client->composition, &n->composition) ||
            !network_host_player(n, p, &row, error))
            return frontend_fail(error, QA_ERROR_FORMAT, "restored Q3 server source is not the declared hosted roster seat");
        *server = (qa_q3_server_hooks){.context = p, .world = q3_world, .command = q3_client_command,
            .enter_world = q3_enter, .think = q3_input, .resend_gamestate = q3_signon,
            .pure_rejected_snapshot = q3_rejected_snapshot, .drop = q3_drop};
        return true;
    }
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
static bool network_restore_nq_source(void *context, const qa_net_client *client,
    qa_network_nq_server_policy *policy, qa_network_nq_server_hooks *hooks, qa_error *error)
{
    qa_frontend_network *n = context;
    return frontend_nq_source_hooks(n->nq_host, client, policy, hooks, error);
}
static bool network_restore_qw_source(void *context, const qa_net_client *client,
    qa_network_qw_server_policy *policy, qa_network_qw_server_hooks *hooks,
    qa_qw_download_admission *downloads, qa_error *error)
{
    qa_frontend_network *n = context;
    return frontend_qw_source_hooks(n->qw_host, client, policy, hooks, downloads, error);
}
static bool network_restore_client_policy(void *context, const qa_net_client *client,
    qa_network_q3_client_policy *out, qa_error *error)
{
    qa_frontend_network *n = context;
    if (!n->q3_client_requested || !n->q3_client_attached || !qa_net_client_id_equal(n->q3_client, client->id) ||
        client->attachment != QA_NET_REMOTE || client->protocol.kind != QA_NET_Q3_68 ||
        client->seat_count != 1 || client->seats[0].seat.owner != NETWORK_OWNER || client->seats[0].seat.index ||
        client->seats[0].remote_index || !qa_net_address_equal(&client->endpoint, &n->q3_client_admission.address, true))
        return frontend_fail(error, QA_ERROR_FORMAT, "Saved Q3 send policy has another actual client connection");
    *out = (qa_network_q3_client_policy){n, remote_client_settings}; return true;
}
static qa_network_checkpoint_refs network_saved_refs(qa_frontend_network *n)
{ return (qa_network_checkpoint_refs){.context = n, .source = network_restore_source,
    .save_actor = network_save_actor, .restore_actor = network_restore_actor,
    .source_nq = network_restore_nq_source, .source_qw = network_restore_qw_source,
    .client_q3_policy = network_restore_client_policy}; }
static bool network_nq_commands_valid(qa_frontend_network *n, qa_error *error)
{
    if (!n->nq_host) return true;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        const nq_frontend_peer *p = n->nq_host->peers + i;
        if (!p->occupied) continue;
        bool present; uint64_t sequence; qa_network_nq_server_state state;
        if (!qa_network_accepted_sequence(n->runtime, p->client, p->seat, &present, &sequence, error) ||
            !qa_network_nq_server_state_read(n->runtime, p->client, &state, error) ||
            (present && (!sequence || sequence != p->tick_sequence || state.stage != 4)) ||
            (!present && (sequence || (p->tick_sequence && qa_network_epoch(n->runtime, p->client) == 1))))
            return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake source tick differs from its actual accepted command owner");
    }
    return true;
}
static bool network_q3_commands_valid(qa_frontend_network *n, qa_error *error)
{
    if (!n->q3_admission) return true;
    for (size_t i = 0; i < 64; ++i) {
        const frontend_q3_peer *peer = n->q3_peers + i;
        if (!peer->occupied) continue;
        bool present; uint64_t sequence;
        if (!qa_network_accepted_sequence(n->runtime, peer->client, peer->seat, &present, &sequence, error) ||
            (present && (!sequence || sequence != peer->sequence)) ||
            (!present && (sequence || peer->sequence)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 source command ordinal differs from its actual accepted command owner");
    }
    return true;
}
static bool network_blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t size = bytes->size;
    if (!qa_source_save_count(io, &size, (size_t)512 * 1048576)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)bytes->data, size);
    if (io->offset > io->input.size || size > io->input.size - io->offset)
        return frontend_fail(io->error, QA_ERROR_FORMAT, "truncated frontend network continuation");
    *bytes = (qa_bytes){io->input.data + io->offset, size}; io->offset += size; return true;
}
static bool network_runtime_check(qa_frontend_network *n, bool complete_world, bool finishing_round, qa_error *error)
{
    if (!network_metadata_check(n, n->q3_admission != NULL, finishing_round, error)) return false;
    if (n->nq_host) return frontend_nq_qualified(n->nq_host, complete_world, error) &&
        (!complete_world || network_nq_commands_valid(n, error));
    if (n->qw_host) return frontend_qw_qualified(n->qw_host, complete_world, error);
    uint32_t cursor = 0; const qa_net_client *client = NULL; size_t count = 0;
    while (qa_net_connections_next(qa_network_connections(n->runtime), &cursor, &client)) {
        ++count;
        if (n->q3_admission) {
            frontend_q3_peer *p = NULL;
            for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied && qa_net_client_id_equal(n->q3_peers[i].client, client->id)) p = &n->q3_peers[i];
            const qa_q3_server_peer *native = qa_network_q3_server_view(n->runtime, client->id);
            const qa_q3_identity *identity = native ? qa_q3_server_peer_identity(native) : NULL;
            qa_q3_server_state state; qa_application_network_player row;
            if (!p || !native || !identity || !qa_net_client_id_equal(identity->client, client->id) || !identity->has_seat ||
                identity->seat.owner != p->seat.owner || identity->seat.index != p->seat.index ||
                qa_q3_server_peer_product(native) != p->product || qa_q3_server_peer_qport(native) != p->qport ||
                client->attachment != QA_NET_REMOTE || client->protocol.kind != QA_NET_Q3_68 ||
                client->protocol.revision || client->protocol.flags || client->seat_count != 1 || client->seats[0].remote_index ||
                client->seats[0].seat.owner != p->seat.owner || client->seats[0].seat.index != p->seat.index ||
                !qa_sha256_equal(&client->composition, &n->composition) ||
                !qa_network_q3_state(n->runtime, client->id, &state, error) || !network_host_player(n, p, &row, error) ||
                (!p->retiring && ((state.phase == QA_Q3_ACTIVE && row.source_begin_pending) ||
                    state.phase == QA_Q3_ZOMBIE || state.phase == QA_Q3_FREE)))
                return frontend_fail(error, QA_ERROR_FORMAT, "hosted frontend/runtime/native/source roster inventories differ");
            if (state.phase >= QA_Q3_PRIMED && state.gamestate_message_number >= 0 && !p->retiring) {
                const qa_q3_gamestate *gamestate = qa_q3_server_peer_gamestate_view(native);
                char map[1024], pure[1024]; qa_application_map_view local;
                if (gamestate->client_number != (int32_t)p->slot || gamestate->checksum_feed != n->q3_checksum_feed ||
                    !qa_q3_info_value(qa_q3_configstring(gamestate, 0), "mapname", map, sizeof(map), error) ||
                    !qa_q3_info_value(qa_q3_configstring(gamestate, 1), "sv_pure", pure, sizeof(pure), error) ||
                    !*map || (strtol(pure, NULL, 10) != 0) != p->world.pure ||
                    (complete_world && (!qa_application_map_read(n->frontend->application, &local) || strcmp(local.name, map))))
                    return frontend_fail(error, QA_ERROR_FORMAT, "hosted native gamestate differs from its retained source map/client number");
            }
            continue;
        }
        const qa_q3_client_peer *peer = qa_network_q3_client_view(n->runtime, client->id);
        if (!n->q3_client_attached || !qa_net_client_id_equal(client->id, n->q3_client) || !peer ||
            qa_q3_client_peer_product(peer) != n->q3_client_product ||
            !qa_net_address_equal(&client->endpoint, &n->q3_client_admission.address, true) ||
            !qa_sha256_equal(&client->composition, &n->composition) ||
            (client->phase >= QA_NET_PRIMED && !n->q3_client_gamestate) ||
            (n->q3_client_entered && !qa_q3_client_peer_usercmd_number(peer)))
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
    size_t expected = n->q3_client_attached ? 1u : 0u;
    if (n->q3_admission) {
        expected = 0;
        for (size_t i = 0; i < 64; ++i) expected += n->q3_peers[i].occupied ? 1u : 0u;
        size_t roster_cursor = 0, rows = 0; qa_application_network_player row;
        while (qa_application_network_player_next(n->frontend->application, &roster_cursor, &row)) {
            bool found = false;
            for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied && qa_net_client_id_equal(n->q3_peers[i].client, row.client) &&
                n->q3_peers[i].seat.owner == row.seat.owner && n->q3_peers[i].seat.index == row.seat.index) found = true;
            if (!found) return frontend_fail(error, QA_ERROR_FORMAT, "remote application roster has no hosted native connection");
            ++rows;
        }
        if (rows != expected) return frontend_fail(error, QA_ERROR_FORMAT, "hosted application roster count differs from frontend peers");
    }
    if (count != expected)
        return frontend_fail(error, QA_ERROR_FORMAT, "frontend and runtime installed connection inventories differ");
    return !complete_world || network_q3_commands_valid(n, error);
}
static bool network_runtime_valid(qa_frontend_network *n, bool complete_world, qa_error *error)
{ return network_runtime_check(n, complete_world, false, error); }
bool frontend_network_checkpoint(qa_frontend *f, qa_buffer *connections, qa_buffer *prediction, qa_error *error)
{
    if (!f || !f->application || !connections || !prediction || connections == prediction ||
        !frontend_network_world_change_ready(f, error)) return false;
    qa_frontend_network *n = f->network; bool installed = n != NULL;
    if (n && !network_runtime_valid(n, true, error)) return false;
    qa_source_save_io io = {0}, history = {0}; qa_buffer runtime = {0}, browser = {0}, admin = {0}, commands = {0}, jobs = {0}, admission = {0}, nq_state = {0}, qw_state = {0};
    bool ok = qa_source_save_writer(&io, qa_application_session(f->application), error) && network_header(&io, &installed) &&
        qa_source_save_writer(&history, qa_application_session(f->application), error) && network_header(&history, &installed);
    if (ok && n) {
        qa_frontend_network *copy = malloc(sizeof(*copy));
        if (!copy) ok = frontend_fail(error, QA_ERROR_MEMORY, "capturing frontend network fields");
        else {
            *copy = *n; qa_net_address address = *qa_network_local_address(n->runtime);
            bool hosting = n->q3_admission != NULL;
            ok = network_address_fields(&io, &address) && network_frontend_fields(&io, copy) && network_host_fields(&io, copy, &hosting, NULL, NULL);
            if (ok && hosting) ok = qa_q3_server_admission_checkpoint(n->q3_admission, &admission, error);
            free(copy);
        }
        qa_bytes admission_bytes = {admission.data, admission.size};
        if (ok && n->q3_admission) ok = network_blob(&io, &admission_bytes);
        bool nq = n->nq_host != NULL;
        if (ok) ok = qa_source_save_bool(&io, &nq);
        if (ok && nq) {
            ok = frontend_nq_checkpoint(n->nq_host, &nq_state, error);
            qa_bytes bytes = {nq_state.data, nq_state.size};
            if (ok) ok = network_blob(&io, &bytes);
        }
        bool qw = n->qw_host != NULL;
        if (ok) ok = qa_source_save_bool(&io, &qw);
        if (ok && qw) {
            ok = frontend_qw_checkpoint(n->qw_host, &qw_state, error);
            qa_bytes bytes = {qw_state.data, qw_state.size};
            if (ok) ok = network_blob(&io, &bytes);
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
    qa_buffer_free(&runtime); qa_buffer_free(&browser); qa_buffer_free(&admin); qa_buffer_free(&commands); qa_buffer_free(&jobs); qa_buffer_free(&admission); qa_buffer_free(&nq_state); qa_buffer_free(&qw_state);
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
    if (qa_net_connections_next(qa_network_connections(n->runtime), &previous_cursor, &previous_client) || n->q3_projection.owner || n->downloads || n->content || n->q3_admission || n->nq_host || n->qw_host || n->q3_packages) {
        qa_source_save_dispose(&io);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "network continuation may replace only an empty prepared candidate");
    }
    qa_frontend_network *state = calloc(1, sizeof(*state));
    if (!state) { qa_source_save_dispose(&io); return frontend_fail(error, QA_ERROR_MEMORY, "decoding network candidate fields"); }
    state->frontend = f; state->registered = n->registered; bool transferred = false;
    qa_net_address local = {0}; qa_bytes runtime = {0}, browser = {0}, admin = {0}, jobs = {0}, admission = {0}, nq_state = {0}, qw_state = {0}, package_cut = {0};
    qa_bytes download_cuts[64] = {{0}};
    bool content = false, downloads = false, hosting = false, nq = false, qw = false;
    ok = ok && network_address_fields(&io, &local) && service_address_valid(&local) &&
        network_frontend_fields(&io, state) && network_host_fields(&io, state, &hosting, &package_cut, download_cuts) &&
        (!hosting || network_blob(&io, &admission)) &&
        qa_source_save_bool(&io, &nq) && (!nq || network_blob(&io, &nq_state)) &&
        qa_source_save_bool(&io, &qw) && (!qw || network_blob(&io, &qw_state)) &&
        network_blob(&io, &runtime) && network_blob(&io, &browser) && network_blob(&io, &admin) &&
        qa_source_save_bool(&io, &content) && qa_source_save_bool(&io, &downloads) && (!downloads || content) &&
        (!downloads || network_blob(&io, &jobs)) && qa_source_save_finish(&io, NULL);
    if (ok && (unsigned)hosting + (unsigned)nq + (unsigned)qw > 1)
        ok = frontend_fail(error, QA_ERROR_FORMAT, "Network continuation declares two native host owners");
    if (ok && nq) ok = frontend_nq_restore(f, n->runtime, nq_state, &state->nq_host, error);
    if (ok && qw) ok = frontend_qw_restore(f, n->runtime, n->admin, qw_state, &state->qw_host, error);
    if (ok && hosting) {
        qa_actor_owner owner; qa_q3_product product;
        ok = qa_application_network_q3_owner(f->application, &owner, &product, error) &&
            frontend_q3_packages_create(f->application, owner, (uint32_t)state->q3_checksum_feed, &state->q3_packages, error) &&
            frontend_q3_packages_restore_receipt(state->q3_packages, package_cut, error) &&
            frontend_q3_packages_prepare(state->q3_packages, true, error);
        qa_q3_download_source source = {.context = state, .resolve = q3_download_resolve};
        for (size_t i = 0; ok && i < 64; ++i) if (state->q3_peers[i].occupied)
            ok = qa_q3_download_window_restore(download_cuts[i], &source, &state->q3_peers[i].download, error);
    }
    if (ok) ok = network_metadata_valid(state, hosting, error);
    if (ok) {
        qa_network_runtime *previous = n->runtime; qa_server_browser *old_browser = n->browser; qa_server_admin *old_admin = n->admin;
        qa_fs_root *preferences = n->preferences;
        *n = *state; transferred = true; n->detached_transport = true; n->runtime = previous; n->browser = old_browser; n->admin = old_admin; n->preferences = preferences;
        for (size_t i = 0; i < 64; ++i) if (n->q3_peers[i].occupied) {
            n->q3_peers[i].network = n; qa_q3_download_window_rebind(n->q3_peers[i].download, n);
        }
        if (hosting) {
            qa_q3_admission_hooks admission_hooks = saved_admission_hooks(n);
            ok = qa_q3_server_admission_restore_checkpoint(admission, &admission_hooks, &n->q3_admission, error);
        }
        qa_net_transport *transport = NULL; qa_network_runtime *restored = NULL;
        qa_network_options options = saved_network_options(n); qa_network_checkpoint_refs refs = network_saved_refs(n);
        if (ok) ok = detached_transport(&local, &transport, error);
        if (ok) ok = qa_network_connections_restore(runtime, transport, &options, &refs, &restored, error);
        if (!ok) qa_net_transport_close(transport);
        else {
            n->runtime = restored; frontend_nq_rebind(n->nq_host, f, restored);
            frontend_qw_rebind(n->qw_host, f, restored, n->admin);
            qa_network_destroy(previous);
        }
        qa_browser_hooks browser_hooks = saved_browser_hooks(n); qa_admin_options admin_options = saved_admin_options(n);
        qa_server_browser *restored_browser = NULL; qa_server_admin *restored_admin = NULL;
        if (ok) ok = qa_server_browser_restore_checkpoint(browser, frontend_tools_http(f), 2048, &browser_hooks, &restored_browser, error) &&
            qa_server_admin_restore_checkpoint(admin, &admin_options, &restored_admin, error);
        if (ok) {
            n->browser = restored_browser; n->admin = restored_admin;
            frontend_qw_rebind(n->qw_host, f, n->runtime, restored_admin);
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
    if (state->nq_host && state->nq_host != n->nq_host) frontend_nq_destroy(state->nq_host);
    if (state->qw_host && state->qw_host != n->qw_host) frontend_qw_destroy(state->qw_host);
    if (state->q3_packages && state->q3_packages != n->q3_packages) frontend_q3_packages_destroy(state->q3_packages);
    for (size_t i = 0; !transferred && i < 64; ++i)
        qa_q3_download_window_destroy(state->q3_peers[i].download);
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
        ok = network_runtime_valid(f->network, false, error) && qa_network_prediction_restore(f->network->runtime, &refs, history, error) &&
            network_nq_commands_valid(f->network, error) && network_q3_commands_valid(f->network, error) &&
            (!f->network->qw_host || frontend_qw_qualified(f->network->qw_host, true, error));
    }
    qa_source_save_dispose(&io); return ok;
}
static bool network_host_cut(qa_frontend_network *n, qa_buffer *out, qa_error *error)
{
    if (n->nq_host) return frontend_nq_checkpoint(n->nq_host, out, error);
    if (n->qw_host) return frontend_qw_checkpoint(n->qw_host, out, error);
    qa_frontend_network *copy = malloc(sizeof(*copy));
    if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "qualifying retained hosting cut");
    *copy = *n;
    qa_source_save_io io = {0}; qa_buffer admission = {0}; bool hosting = n->q3_admission != NULL;
    bool ok = qa_source_save_writer(&io, NULL, error) && network_host_fields(&io, copy, &hosting, NULL, NULL);
    if (ok && hosting) {
        ok = qa_q3_server_admission_checkpoint(n->q3_admission, &admission, error);
        qa_bytes bytes = {admission.data, admission.size};
        if (ok) ok = network_blob(&io, &bytes);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&admission); free(copy); return ok;
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
        (next->q3_admission != NULL) != (active->q3_admission != NULL) ||
        (next->nq_host != NULL) != (active->nq_host != NULL) ||
        (next->qw_host != NULL) != (active->qw_host != NULL) ||
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
    if ((next->downloads != NULL) != (active->downloads != NULL) ||
        (next->downloads && !qa_downloads_handoff_ready(active->downloads, next->downloads, error))) return false;
    if (!qa_server_browser_http_handoff_ready(active->browser, next->browser, error)) return false;
    if (active->q3_admission || active->nq_host || active->qw_host) {
        qa_buffer current_host = {0}, restored_host = {0};
        bool ok = network_host_cut(active, &current_host, error) && network_host_cut(next, &restored_host, error);
        if (ok && (current_host.size != restored_host.size || memcmp(current_host.data, restored_host.data, current_host.size)))
            ok = frontend_fail(error, QA_ERROR_UNSUPPORTED, "live native host advanced beyond its saved admission/source continuation cut");
        qa_buffer_free(&current_host); qa_buffer_free(&restored_host);
        if (!ok) return false;
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
    qa_buffer_free(&current); qa_buffer_free(&restored);
    if (ok && (active->nq_host || active->q3_admission || active->qw_host)) {
        qa_network_checkpoint_refs active_refs = network_saved_refs(active), next_refs = network_saved_refs(next);
        ok = qa_network_prediction_checkpoint(active->runtime, &active_refs, &current, error) &&
            qa_network_prediction_checkpoint(next->runtime, &next_refs, &restored, error);
        if (ok && (current.size != restored.size || memcmp(current.data, restored.data, current.size)))
            ok = frontend_fail(error, QA_ERROR_UNSUPPORTED, "live original accepted command owner advanced beyond its saved cut");
        qa_buffer_free(&current); qa_buffer_free(&restored);
    }
    return ok;
}
void frontend_network_rebind(qa_frontend *owned, qa_frontend *destination)
{
    if (owned->network) {
        owned->network->frontend = destination;
        frontend_nq_rebind(owned->network->nq_host, destination, owned->network->runtime);
        frontend_qw_rebind(owned->network->qw_host, destination, owned->network->runtime, owned->network->admin);
        frontend_q3_packages_rebind(owned->network->q3_packages, destination->application);
    }
    for (server_lease *lease = owned->network_server_leases; lease; lease = lease->next) lease->frontend = destination;
}
void frontend_network_transport_exchange(qa_frontend *active, qa_frontend *candidate)
{
    if (!active->network) return;
    if (active->network->downloads) qa_downloads_handoff_publish(active->network->downloads, candidate->network->downloads);
    qa_network_transport_exchange(active->network->runtime, candidate->network->runtime);
    active->network->detached_transport = true; candidate->network->detached_transport = false;
}
bool frontend_network_fresh_ready(const qa_frontend *candidate, const qa_frontend *active,
    const qa_frontend *constructor, qa_error *error)
{
    if (!candidate || !active || !constructor || candidate == active || candidate == constructor || active == constructor ||
        candidate->stepping || active->stepping || constructor->stepping || candidate->options.network_host ||
        candidate->options.network_connect || constructor->options.network_host || constructor->options.network_connect ||
        frontend_network_save_authority(candidate) != QA_SAVE_OFFLINE ||
        frontend_network_save_authority(constructor) != QA_SAVE_OFFLINE ||
        !frontend_network_world_change_ready((qa_frontend *)active, error) ||
        !qa_http_callbacks_idle(frontend_tools_http((qa_frontend *)active)))
        return frontend_fail(error, QA_ERROR_FORMAT, "Fresh offline publication lost its retained constructor or idle displaced network owner");
    const qa_frontend *fresh[2] = {candidate, constructor};
    for (size_t i = 0; i < 2; ++i) if (fresh[i]->network) {
        const qa_frontend_network *n = fresh[i]->network; uint32_t cursor = 0; const qa_net_client *client = NULL;
        if (n->frontend != fresh[i] || n->round || n->busy || n->q3_client_requested || n->q3_admission || n->nq_host || n->qw_host ||
            qa_net_connections_next(qa_network_connections(n->runtime), &cursor, &client))
            return frontend_fail(error, QA_ERROR_FORMAT, "Fresh offline graph carries an admitted endpoint or source host");
    }
    if (active->network && active->network->frontend != active)
        return frontend_fail(error, QA_ERROR_FORMAT, "Displaced endpoints borrow another physical frontend owner");
    return frontend_network_rebind_ready(candidate, constructor, error);
}
void frontend_network_publish_fresh(qa_frontend *active, qa_frontend *candidate, qa_frontend *constructor)
{
    (void)active;
    frontend_network_transport_exchange(constructor, candidate);
}
bool frontend_network_world_change_ready(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n = f->network;
    return !n || (!n->round && !n->busy && frontend_nq_idle(n->nq_host) && frontend_qw_idle(n->qw_host) && qa_network_callbacks_idle(n->runtime)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "network callbacks must return before world publication");
}
bool frontend_network_close_client(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n = f ? f->network : NULL;
    if (!n || n->detached_transport || !n->q3_client_attached || n->q3_client_retiring) return true;
    if (!frontend_network_world_change_ready(f, error)) return false;
    return qa_network_q3_client_disconnect(n->runtime, n->q3_client,
        (int32_t)((f->time_ns / UINT64_C(1000000)) & INT32_MAX), error);
}
bool frontend_network_destroy(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n = f->network; if (!n) return true;
    if (!frontend_network_world_change_ready(f, error) || !qa_http_callbacks_idle(frontend_tools_http(f)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "network/HTTP callbacks must return before teardown");
    if (!frontend_network_close_client(f, error)) return false;
    if (!n->detached_transport && !qa_server_admin_shutdown(n->admin, error)) return false;
    if (!qa_application_network_q3_client_unproject(f->application, &n->q3_projection, error)) return false;
    if (n->registered && !qa_console_remove_owner(qa_application_console(f->application), NETWORK_OWNER, error)) return false;
    qa_cvars_remove_owner(qa_application_cvars(f->application), NETWORK_OWNER);
    qa_downloads_destroy(n->downloads); qa_server_browser_destroy(n->browser); qa_server_admin_destroy(n->admin);
    qa_network_destroy(n->runtime); qa_q3_server_admission_destroy(n->q3_admission);
    frontend_nq_destroy(n->nq_host);
    frontend_qw_destroy(n->qw_host);
    for (size_t i = 0; i < 64; ++i) qa_q3_download_window_destroy(n->q3_peers[i].download);
    frontend_q3_packages_destroy(n->q3_packages);
    qa_fs_root_close(n->preferences); qa_fs_root_close(n->content);
    free(n); f->network = NULL; return true;
}
bool frontend_network_pump(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n = f->network; if (!n) return true;
    if (n->downloads && (!qa_downloads_pump(n->downloads, error) || !frontend_tools_sync(f, error))) return false;
    if (!q3_prepare(n, error) || !frontend_nq_prepare(n->nq_host, error) || !frontend_qw_prepare(n->qw_host, error)) return false;
    ++n->busy;
    qa_server_browser_expire(n->browser, f->time_ns);
    bool ok = qa_network_pump(n->runtime, f->time_ns, error) && qa_server_admin_tick(n->admin, f->time_ns, false, error);
    --n->busy;
    return ok && (!n->q3_admission || q3_drain(n, error)) && client_drain(n, error) &&
        frontend_nq_pump(n->nq_host, error) && frontend_qw_pump(n->qw_host, error);
}
bool frontend_network_tick(qa_frontend *f, uint64_t elapsed_ns, bool retiring_map, qa_error *error)
{
    return !f || !f->network || frontend_nq_tick(f->network->nq_host, elapsed_ns, retiring_map, error);
}
bool frontend_network_command(qa_frontend *f, uint32_t seat, qa_actor_id actor,
    const qa_movement_command *movement, qa_error *error)
{
    qa_actor_id current;
    if (!qa_application_player_actor(f->application, seat, &current) || !qa_actor_id_equal(actor, current))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "local network command actor no longer owns its roster seat");
    if (frontend_network_remote(f)) {
        qa_frontend_network *n = f->network;
        if (!n || !n->q3_client_attached || !n->q3_client_active || !n->q3_client_gamestate || n->q3_client_retiring) return true;
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
    if (n->nq_host) return frontend_nq_publish(n->nq_host, error);
    if (n->qw_host) return frontend_qw_publish(n->qw_host, error);
    if (n->q3_client_requested) return client_drain(n, error);
    if (n->q3_admission) {
        if (n->round) return round_publish(n->round, error);
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

static bool round_idle(qa_network_q3_round *cut, qa_error *error)
{
    qa_frontend_network *n = cut ? cut->network : NULL;
    if (!n || n->round != cut || n->frontend != cut->frontend || cut->frontend->network != n ||
        cut->frontend->stepping || n->busy || !qa_network_callbacks_idle(n->runtime) || n->q3_reconnect ||
        qa_application_get_state(cut->frontend->application) != QA_APPLICATION_RUNNING ||
        !qa_session_safe(qa_application_session(cut->frontend->application)) ||
        cut->generation != qa_application_configuration_generation(cut->frontend->application) ||
        n->q3_generation != cut->generation || n->q3_checksum_feed != cut->checksum_feed ||
        n->q3_restarted_server_id != cut->restarted_server_id ||
        n->q3_server_id != cut->server_id + (cut->begun ? 1 : 0) ||
        n->q3_server_bit != (uint8_t)(cut->snapshot_bit ^ (cut->begun ? 4u : 0u)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source round no longer owns its exact idle network cut");
    qa_q3_server_world observed;
    qa_application_network_q3_package_view packages;
    return frontend_q3_packages_view(n->q3_packages, &packages, error) &&
        qa_application_network_q3_round_world(cut->frontend->application, cut->source_owner,
        n->q3_server_id, n->q3_restarted_server_id, n->q3_checksum_feed, &packages, &observed, error);
}
static bool round_peer(qa_network_q3_round *cut, size_t i, frontend_q3_peer **out, qa_error *error)
{
    const qa_network_q3_round_client *row = cut->clients + i;
    frontend_q3_peer *peer = cut->network->q3_peers + row->source_slot;
    if (!peer->occupied || !qa_net_client_id_equal(peer->client, row->client) ||
        peer->seat.owner != row->seat.owner || peer->seat.index != row->seat.index ||
        peer->sequence != cut->sequence[i] || qa_network_epoch(cut->network->runtime, row->client) != cut->epoch[i])
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 source round client changed its retained transport/command identity");
    bool present; uint64_t accepted;
    if (!qa_network_accepted_sequence(cut->network->runtime, row->client, row->seat, &present, &accepted, error)) return false;
    if ((present && accepted != cut->sequence[i]) || (!present && (accepted || cut->sequence[i])))
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 round client differs from its real accepted source command counter");
    *out = peer; return true;
}
size_t frontend_network_q3_round_clients(const qa_network_q3_round *cut,
    const qa_network_q3_round_client **out)
{
    if (out) *out = cut ? cut->clients : NULL;
    return cut ? cut->count : 0;
}
qa_actor_owner frontend_network_q3_round_source_owner(const qa_network_q3_round *cut)
{ return cut ? cut->source_owner : 0; }
uint8_t frontend_network_q3_round_snapshot_bit(const qa_network_q3_round *cut)
{ return cut ? (uint8_t)(cut->snapshot_bit ^ (cut->begun ? 4u : 0u)) : 0; }
void frontend_network_q3_round_dispose(qa_network_q3_round *cut)
{
    if (!cut) return;
    if (cut->network && cut->network->round == cut) cut->network->round = NULL;
    for (size_t i = 0; i < 64; ++i) { free(cut->userinfo[i]); qa_buffer_free(&cut->native[i]); }
    free(cut);
}
bool frontend_network_q3_round_prepare(qa_frontend *f, qa_network_q3_round **out, qa_error *error)
{
    if (!f || !out || *out || f->stepping || !frontend_network_world_change_ready(f, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source round requires its drained frontend and empty cut output");
    qa_frontend_network *n = f->network;
    if (!n) return !f->options.network_host && !f->options.network_connect ||
        frontend_fail(error, QA_ERROR_UNSUPPORTED, "Configured networking lacks its actual restart owner");
    if (n->q3_client_requested || n->nq_host || (f->options.network_host && !n->q3_admission))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 source round cannot substitute another installed network dialect");
    if (!n->q3_admission) return true;
    if (n->q3_pending_count || n->q3_server_id == INT32_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source round has pending admission or exhausted server identity");
    if (!network_runtime_valid(n, true, error)) return false;
    qa_network_q3_round *cut = calloc(1, sizeof(*cut));
    if (!cut) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Q3 source round network cut");
    cut->network = n; cut->frontend = f; cut->generation = n->q3_generation;
    cut->server_id = n->q3_server_id; cut->restarted_server_id = n->q3_restarted_server_id;
    cut->checksum_feed = n->q3_checksum_feed; cut->snapshot_bit = n->q3_server_bit;
    qa_q3_product product;
    bool ok = qa_application_network_q3_owner(f->application, &cut->source_owner, &product, error);
    qa_q3_server_world world;
    qa_application_network_q3_package_view packages;
    if (ok) ok = frontend_q3_packages_view(n->q3_packages, &packages, error) &&
        qa_application_network_q3_round_world(f->application, cut->source_owner,
        cut->server_id, cut->restarted_server_id, cut->checksum_feed, &packages, &world, error);
    for (size_t i = 0; ok && i < 64; ++i) {
        frontend_q3_peer *peer = n->q3_peers + i; if (!peer->occupied) continue;
        if (peer->retiring) { ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source round has a retiring native client"); break; }
        size_t at = cut->count++; qa_network_q3_round_client *row = cut->clients + at;
        qa_q3_server_state state; const char *userinfo;
        row->client = peer->client; row->seat = peer->seat; row->source_slot = (uint32_t)i;
        ok = q3_actor(peer, &row->previous_actor, error) &&
            qa_application_network_q3_userinfo_read(f->application, row->previous_actor, &userinfo, error) &&
            qa_network_q3_state(n->runtime, peer->client, &state, error);
        if (!ok) break;
        uint32_t physical_slot; qa_q3_product client_product;
        if (!qa_application_network_q3_source(f->application, row->previous_actor,
                &physical_slot, &client_product, error) || physical_slot != row->source_slot ||
            client_product != product || client_product != peer->product) {
            ok = frontend_fail(error, QA_ERROR_FORMAT, "Q3 round client differs from its retained physical source admission"); break;
        }
        size_t length = strlen(userinfo);
        if (length == SIZE_MAX || !(cut->userinfo[at] = malloc(length + 1))) {
            ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual Q3 source client userinfo"); break;
        }
        memcpy(cut->userinfo[at], userinfo, length + 1); row->userinfo = cut->userinfo[at];
        row->last_command = state.last_usercmd; cut->sequence[at] = peer->sequence;
        cut->epoch[at] = qa_network_epoch(n->runtime, peer->client);
        frontend_q3_peer *qualified;
        if (!round_peer(cut, at, &qualified, error)) { ok = false; break; }
        const qa_q3_server_peer *native = qa_network_q3_server_view(n->runtime, peer->client);
        ok = native && qa_q3_server_peer_checkpoint(native, &cut->native[at], error);
    }
    uint32_t cursor = 0; const qa_net_client *client;
    while (ok && qa_net_connections_next(qa_network_connections(n->runtime), &cursor, &client)) {
        bool found = false;
        for (size_t i = 0; i < cut->count; ++i) if (qa_net_client_id_equal(client->id, cut->clients[i].client)) found = true;
        if (!found) ok = frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 round has an unrepresented installed transport client");
    }
    if (!ok) {
        frontend_network_q3_round_dispose(cut);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Q3 source cut lacks its genuine GAME/transport admission");
        return false;
    }
    n->round = cut; *out = cut; return true;
}
static bool round_bind_world(qa_network_q3_round *cut, bool prepare, qa_error *error)
{
    qa_frontend_network *n = cut->network; qa_q3_server_world world;
    qa_application_network_q3_package_view packages;
    if (!frontend_q3_packages_prepare(n->q3_packages, false, error) ||
        !frontend_q3_packages_view(n->q3_packages, &packages, error)) return false;
    bool ok = prepare ? qa_application_network_q3_round_prepare(cut->frontend->application, cut->source_owner,
        n->q3_server_id, n->q3_restarted_server_id, n->q3_checksum_feed, &packages, &world, error) :
        qa_application_network_q3_round_world(cut->frontend->application, cut->source_owner,
        n->q3_server_id, n->q3_restarted_server_id, n->q3_checksum_feed, &packages, &world, error);
    if (!ok) return false;
    for (size_t i = 0; i < cut->count; ++i) {
        frontend_q3_peer *peer;
        if (!round_peer(cut, i, &peer, error)) return false;
        peer->world = world;
        peer->world.downloading = qa_q3_download_window_active(peer->download);
    }
    return true;
}
static bool round_publish(qa_network_q3_round *cut, qa_error *error)
{
    if (!round_idle(cut, error) || !round_bind_world(cut, false, error)) return false;
    for (size_t i = 0; i < cut->count; ++i) {
        frontend_q3_peer *peer; qa_q3_server_state state;
        if (!round_peer(cut, i, &peer, error)) return false;
        if (peer->retiring || (cut->begun && !cut->resolved[i])) continue;
        if (!qa_network_q3_state(cut->network->runtime, peer->client, &state, error)) return false;
        if (state.phase != QA_Q3_CONNECTED && peer->world.time >= state.next_snapshot_time && !q3_snapshot(peer, error)) return false;
    }
    return true;
}
static bool round_command_equal(const qa_q3_usercmd *a, const qa_q3_usercmd *b)
{
    return a->serverTime == b->serverTime && !memcmp(a->angles, b->angles, sizeof(a->angles)) &&
        a->forwardmove == b->forwardmove && a->rightmove == b->rightmove && a->upmove == b->upmove &&
        a->buttons == b->buttons && a->weapon == b->weapon;
}
bool frontend_network_q3_round_refresh(qa_network_q3_round *cut, qa_error *error)
{
    if (!cut) return true;
    if (!round_idle(cut, error)) return false;
    if (cut->begun) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round cannot replace an already mutated cut");
    qa_buffer refreshed[64] = {{0}}; bool ok = true;
    for (size_t i = 0; ok && i < cut->count; ++i) {
        frontend_q3_peer *peer; qa_actor_id actor; qa_q3_server_state state; const char *userinfo;
        ok = round_peer(cut, i, &peer, error) && !peer->retiring && q3_actor(peer, &actor, error) &&
            qa_actor_id_equal(actor, cut->clients[i].previous_actor) &&
            qa_application_network_q3_userinfo_read(cut->frontend->application, actor, &userinfo, error) &&
            !strcmp(userinfo, cut->clients[i].userinfo) && qa_network_q3_state(cut->network->runtime, peer->client, &state, error) &&
            round_command_equal(&state.last_usercmd, &cut->clients[i].last_command);
        const qa_q3_server_peer *native = ok ? qa_network_q3_server_view(cut->network->runtime, peer->client) : NULL;
        if (ok) ok = native && qa_q3_server_peer_checkpoint(native, refreshed + i, error);
    }
    if (ok) for (size_t i = 0; i < cut->count; ++i) {
        qa_buffer_free(cut->native + i); cut->native[i] = refreshed[i]; refreshed[i] = (qa_buffer){0};
    }
    for (size_t i = 0; i < 64; ++i) qa_buffer_free(refreshed + i);
    if (!ok && (!error || error->code == QA_OK))
        frontend_fail(error, QA_ERROR_FORMAT, "Q3 source admissions advanced during old frame delivery");
    return ok;
}
bool frontend_network_q3_round_begin(qa_network_q3_round *cut, void (*mark)(void *), void *mark_context, qa_error *error)
{
    if (!cut) return true;
    if (!round_idle(cut, error)) return false;
    if (!mark || cut->begun) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source round requires its first mutation marker");
    for (size_t i = 0; i < cut->count; ++i) {
        frontend_q3_peer *peer; qa_buffer current = {0}; qa_actor_id actor; const char *userinfo;
        if (!round_peer(cut, i, &peer, error)) return false;
        if (peer->retiring || !q3_actor(peer, &actor, error) ||
            !qa_actor_id_equal(actor, cut->clients[i].previous_actor) ||
            !qa_application_network_q3_userinfo_read(cut->frontend->application, actor, &userinfo, error)) return false;
        if (strcmp(userinfo, cut->clients[i].userinfo))
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 live userinfo advanced beyond its prepared source cut");
        const qa_q3_server_peer *native = qa_network_q3_server_view(cut->network->runtime, peer->client);
        bool ok = native && qa_q3_server_peer_checkpoint(native, &current, error);
        if (ok && (current.size != cut->native[i].size || memcmp(current.data, cut->native[i].data, current.size)))
            ok = frontend_fail(error, QA_ERROR_FORMAT, "Q3 live protocol advanced beyond its prepared round cut");
        qa_buffer_free(&current); if (!ok) return false;
    }
    mark(mark_context);
    ++cut->network->q3_server_id; cut->network->q3_server_bit ^= 4u;
    cut->begun = true;
    return round_bind_world(cut, true, error);
}
bool frontend_network_q3_round_bind(qa_network_q3_round *cut, qa_error *error)
{
    if (!cut) return true;
    if (!round_idle(cut, error) || !cut->begun || cut->bound)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source round bind is outside its reset scope");
    if (!round_bind_world(cut, true, error)) return false;
    cut->bound = true; return true;
}
static bool round_client(qa_network_q3_round *cut, qa_net_client_id client, size_t *at,
    frontend_q3_peer **peer, qa_error *error)
{
    if (!round_idle(cut, error) || !cut->bound || cut->finished)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round client is outside the actual bound source scope");
    for (size_t i = 0; i < cut->count; ++i) if (qa_net_client_id_equal(cut->clients[i].client, client)) {
        if (cut->resolved[i]) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round client already completed admission");
        *at = i; return round_peer(cut, i, peer, error);
    }
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round client has no retained transport admission");
}
bool frontend_network_q3_round_queue_client(qa_network_q3_round *cut, qa_net_client_id client, qa_error *error)
{
    size_t i; frontend_q3_peer *peer;
    if (!cut || !round_client(cut, client, &i, &peer, error)) return false;
    if (cut->queued[i] || peer->retiring) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round client cannot reconnect twice or after retirement");
    if (!qa_network_q3_command(cut->network->runtime, client, "map_restart\n", error)) return false;
    cut->queued[i] = true; return true;
}
bool frontend_network_q3_round_activate_client(qa_network_q3_round *cut, qa_net_client_id client, qa_error *error)
{
    size_t i; frontend_q3_peer *peer; qa_application_network_player row;
    if (!cut || !round_client(cut, client, &i, &peer, error)) return false;
    if (!cut->queued[i] || peer->retiring || !network_host_player(cut->network, peer, &row, error) || row.source_begin_pending)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source round has not completed actual canonical ClientBegin");
    const qa_actor_record *record = qa_actors_get(qa_session_actors(qa_application_session(cut->frontend->application)), row.actor);
    if (!record || qa_actor_id_equal(row.actor, cut->clients[i].previous_actor))
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 round has not replaced its actual retired source player generation");
    if (!round_bind_world(cut, false, error) || !qa_network_q3_round_activate(cut->network->runtime, client, error)) return false;
    cut->resolved[i] = true; return true;
}
bool frontend_network_q3_round_reject_client(qa_network_q3_round *cut, qa_net_client_id client,
    const char *reason, qa_error *error)
{
    size_t i; frontend_q3_peer *peer;
    if (!cut || !reason || !round_client(cut, client, &i, &peer, error)) return false;
    if (!qa_network_q3_disconnect(cut->network->runtime, client, &peer->rate, cut->network->q3_server_bit, reason, error)) return false;
    if (!q3_drop(peer, reason, error)) return false;
    cut->resolved[i] = true; return true;
}
bool frontend_network_q3_round_finish(qa_network_q3_round *cut, qa_error *error)
{
    if (!cut) return true;
    if (!round_idle(cut, error) || !cut->bound || cut->finished)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source round has no complete bound network scope");
    for (size_t i = 0; i < cut->count; ++i)
        if (!cut->resolved[i]) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round did not resolve every retained client");
    if (cut->network->q3_pending_count)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round acquired a new pending transport admission");
    for (size_t i = 0; i < cut->count; ++i) {
        frontend_q3_peer *peer; qa_q3_server_state state;
        if (!round_peer(cut, i, &peer, error)) return false;
        if (peer->retiring) continue;
        if (!qa_network_q3_state(cut->network->runtime, peer->client, &state, error)) return false;
        if (!cut->queued[i] || state.phase != QA_Q3_ACTIVE ||
            !round_command_equal(&state.last_usercmd, &cut->clients[i].last_command))
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 round lost its native active client or retained last command");
    }
    if (!q3_drain(cut->network, error)) return false;
    if (!network_runtime_check(cut->network, true, true, error)) return false;
    cut->finished = true; cut->network->round = NULL;
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
static bool server_round_peer(server_lease *lease, frontend_q3_peer *peer, bool *owned, qa_error *error)
{
    qa_frontend_network *n = lease->frontend->network;
    qa_network_q3_round *cut = n ? n->round : NULL;
    *owned = false;
    if (!cut || !cut->begun || cut->source_owner != lease->owner) return true;
    for (size_t i = 0; i < cut->count; ++i) if (cut->clients[i].source_slot == peer->slot) {
        frontend_q3_peer *actual;
        if (!round_peer(cut, i, &actual, error)) return false;
        if (actual != peer) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 source command changed its retained cut recipient");
        *owned = true; return true;
    }
    return frontend_fail(error, QA_ERROR_FORMAT, "Q3 source command names an unrepresented round recipient");
}
static bool server_send_command(void *context, int32_t slot, const char *text, qa_error *error)
{
    server_lease *lease = context;
    qa_frontend_network *n = lease->frontend->network;
    for (size_t i = 0; n && i < 64; ++i) {
        frontend_q3_peer *peer = &n->q3_peers[i]; qa_actor_id actor;
        if (!peer->occupied || peer->retiring || (slot >= 0 && peer->slot != (uint32_t)slot)) continue;
        bool owned;
        if (!server_round_peer(lease, peer, &owned, error)) return false;
        if (owned) {
            if (!qa_network_q3_command(n->runtime, peer->client, text, error)) return false;
            continue;
        }
        if (!q3_actor(peer, &actor, error)) return false;
        if (qa_application_network_q3_client_bound(lease->frontend->application, lease->owner, actor, peer->slot) &&
            !qa_network_q3_command(n->runtime, peer->client, text, error)) return false;
    }
    return !lease->original.send_command || lease->original.send_command(lease->original.context, slot, text, error);
}
static bool server_drop_client(void *context, uint32_t slot, const char *reason, qa_error *error)
{
    server_lease *lease = context;
    qa_frontend_network *n = lease->frontend->network;
    if (n && slot < 64 && n->q3_peers[slot].occupied) {
        qa_actor_id actor;
        bool owned;
        if (!server_round_peer(lease, &n->q3_peers[slot], &owned, error)) return false;
        if (!owned) {
            if (!q3_actor(&n->q3_peers[slot], &actor, error)) return false;
            owned = qa_application_network_q3_client_bound(lease->frontend->application, lease->owner, actor, slot);
        }
        if (owned && !q3_drop(&n->q3_peers[slot], reason, error)) return false;
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
