#include "internal.h"
#include "qa/application_network.h"
#include "qa/local_lobby.h"
#include "qa/application_network_qw.h"
#include "qa/application_character_selection.h"
#include "qa/application_q3_factory.h"
#include "qa/downloads.h"
#include "qa/server_browser.h"
#include "qa/server_browser_favorites.h"
#include "qa/network_interfaces.h"
#include "qa/network_events.h"
#include "qa/network_kex_transport.h"
#include "qa/server_admin.h"
#include "qa/tokenizer.h"
#include "network_admin.h"
#include "qa/launch_identity.h"
#include "qa/network_q3_runtime.h"
#include "qa/network_q3_download.h"
#include "qa/network_q3_client_download.h"
#include "qa/network_q3_server_authorization.h"
#include "qa/network_q3_authorization.h"
#include "qa/network_q3_prediction_scene.h"
#include "network_config.h"
#include "network_recipient.h"
#include "network_local_groups.h"
#include "network_kex.h"
#include "network_declarations.h"
#include "remote_config.h"
#include "remote_input.h"
#include "remote_prediction.h"
#include "network_predictor.h"
#include "network_session.h"
#include "network_q3_video.h"
#include "network_restore.h"
#include "network_restore_publication.h"
#include "network_restore_attempt.h"
#include "network_initial_graph.h"
#include "network_restore_services.h"
#include "network_restore_prediction.h"
#include "network_menu.h"
#include "demo_dispatch.h"
#include "content_library_services.h"
#include "network_q2_input.h"
#include "network_q1_input.h"
#include "network_q2_client.h"
#include "network_q1_client.h"
#include "neutral_config.h"
#include "config_store.h"
#include "network_q2_host.h"
#include "network_q2_host_save.h"
#include "network_unified.h"
#include "network_unified_save.h"
#include "../application/unified_events.h"
#include "../application/unified_save_internal.h"
#include "network_unified_client.h"
#include "qc_messages.h"
#include "network_player_drop.h"
#include "remote_q2_client.h"
#include "qa/application_native_q3_client_modules.h"
#include "remote_q3_client.h"
#include "remote_q3_session.h"
#include "qa/application_native_q3_remote_content.h"
#include "qa/application_native_q3_remote_lifecycle.h"
#include "remote_snapshots.h"
#include "qa/network_save.h"
#include "qa/network_services_save.h"
#include "qa/network_downloads_save.h"
#include "save_private.h"
#include "network_nq_private.h"
#include "network_qw_private.h"
#include "network_q3_restart.h"
#include "network_q3_packages.h"
#include "network_content_q3.h"
#include "network_prediction.h"
#include "network_presentation.h"
#include "network_browser.h"
#include "../../presentation/q3_native/remote_frame.h"
#include "../../network/service_save_fields.h"
#include "qa/archive.h"
#include "qa/console_cvars_prepare.h"
#include "qa/bsp.h"
#include <inttypes.h>
#include <stdio.h>

#define NETWORK_OWNER QA_NETWORK_COMMAND_OWNER
static uint64_t next_input_serial;
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
typedef struct frontend_q3_attempt {
    struct frontend_q3_attempt *next;
    bool disconnect;
    char server[1024];
} frontend_q3_attempt;
typedef struct frontend_network_server_lease {
    qa_frontend *frontend;
    qa_q3_host_server_services original;
    void *lifetime;
    void (*release)(void *);
    qa_actor_owner owner;
    struct frontend_network_server_lease *next;
} server_lease;
typedef struct frontend_q3_client {
    qa_frontend_network *network;
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_net_seat_id seat;
    uint32_t physical;
    qa_q3_client_authorization *q3_client_authorization;
    frontend_remote_config_view q3_authorization_configuration;
    frontend_key_profile_view q3_authorization_profile;
    qa_q3_client_admission q3_client_admission;
    qa_net_client_id q3_client, q3_client_previous;
    qa_actor_owner q3_cgame_owner;
    uint32_t q3_client_launch_seat;
    qa_q3_product q3_client_product;
    qa_q3_client_clock q3_client_clock;
    qa_cvar_handle cl_maxpackets, cl_packetdup, cl_timeNudge, cg_smoothClients;
    frontend_q3_content *q3_client_content;
    qa_q3_client_downloads *q3_client_downloads;
    qa_q3_prediction_scene *q3_prediction_scene;
    frontend_remote_input *q3_input;
    frontend_network_predictor *q3_predictor;
    frontend_remote_q3 *q3_session;
    frontend_remote_q3_initial *q3_initial;
    frontend_remote_q3_modules *q3_initial_modules;
    qa_application_q3_remote_source q3_session_source;
    qa_buffer q3_predictor_pending;
    qa_buffer q3_download_pending;
    qa_buffer q3_connections_prefix;
    bool q3_native_restore;
    bool q3_initial_restore_complete;
    uint64_t q3_predictor_zero_sequence;
    bool q3_predictor_zero_pending;
    uint64_t q3_scene_frame;
    bool q3_scene_frame_valid;
    qa_application_network_q3_projection q3_projection;
    uint8_t q3_projection_epoch;
    uint64_t q3_client_generation, q3_client_epoch, q3_client_restart_generation, q3_client_previous_epoch;
    int32_t q3_client_time, q3_previous_presentation_time, q3_weapon;
    int32_t q3_initial_message, q3_initial_command, q3_reached_command_sequence;
    int32_t q3_reliable_receipt_sequence;
    bool q3_initial_tuple;
    bool q3_reliable_receipt;
    qa_q3_tokens q3_reached_command;
    float q3_sensitivity;
    bool q3_client_requested, q3_client_attach, q3_client_attached;
    bool q3_client_decoded, q3_client_initializing;
    bool q3_client_closed, q3_client_rebind;
    bool q3_client_gamestate, q3_client_active, q3_client_retiring, q3_command_present, q3_client_entered;
    char q3_client_reason[256];
    char q3_client_userinfo[1024];
    char q3_client_message[1024], q3_client_update_info[1024];
    int32_t q3_ui_client_number;
} frontend_q3_client;
static void client_cvars_bind(frontend_q3_client *client, const qa_cvars *cvars)
{
    client->cl_maxpackets = qa_cvars_resolve(cvars, "cl_maxpackets");
    client->cl_packetdup = qa_cvars_resolve(cvars, "cl_packetdup");
    client->cl_timeNudge = qa_cvars_resolve(cvars, "cl_timeNudge");
    client->cg_smoothClients = qa_cvars_resolve(cvars, "cg_smoothClients");
}
typedef struct frontend_local_client {
    qa_frontend_network *network;
    qa_network_runtime *runtime;
    frontend_network_unified *unified;
    frontend_network_unified_client_service *service;
    frontend_network_q1_client *q1;
    frontend_network_q2_client *q2;
    frontend_q3_client *q3;
    qa_net_address endpoint;
    qa_net_seat_id seat, client_seat;
    uint32_t physical, authored;
} frontend_local_client;
struct qa_frontend_network {
    qa_frontend *frontend;
    qa_net_loopback *loopback;
    qa_net_address loopback_server;
    frontend_local_client local_clients[QA_NETWORK_MAX_SEATS];
    qa_network_runtime *runtime;
    qa_server_browser *browser;
    qa_net_interfaces *interfaces;
    qa_kex_transport *kex_transport;
    frontend_kex_browser *kex_browser;
    frontend_q3_browser *q3_browser;
    qa_server_admin *admin;
    qa_application *operator_application;
    qa_downloads *downloads;
    qa_fs_root *preferences, *content;
    frontend_network_menu_preferences menu_preferences[4];
    frontend_network_menu_status browser_status;
    uint64_t browser_receipt, browser_expired[4];
    uint64_t input_serial;
    uint64_t nonce;
    uint64_t preparation_nonce;
    uint32_t rotation_random;
    qa_q3_server_admission *q3_admission;
    qa_q3_server_authorization *q3_authorization;
    frontend_nq_host *nq_host;
    frontend_qw_host *qw_host;
    frontend_network_q2_client *q2_client_owner;
    frontend_network_q1_client *q1_client_owner;
    frontend_network_q2_host *q2_host;
    frontend_network_unified *unified;
    frontend_network_unified_client_service *unified_client_service;
    uint64_t unified_map_revision;
    frontend_q3_packages *q3_packages;
    frontend_q3_peer q3_peers[64];
    frontend_q3_pending q3_pending[32];
    size_t q3_pending_count;
    uint64_t q3_generation;
    int32_t q3_server_id, q3_restarted_server_id, q3_checksum_feed;
    uint8_t q3_server_bit;
    uint64_t composition;
    const qa_q3_accepted_connect *q3_reconnect;
    frontend_q3_client q3_clients[QA_NETWORK_MAX_SEATS];
    qa_buffer menu_connections_prefix;
    char client_server[1024];
    frontend_q3_attempt *q3_attempts, *q3_attempt_tail;
    size_t q3_attempt_count;
    unsigned busy;
    bool registered;
    bool detached_transport;
    frontend_demo_record_source demo_q1_source;
    frontend_demo_playback_source demo_source;
    frontend_network_q1_client *demo_q1_owner;
    frontend_demo_sink demo_q1_sink, demo_q3_sink;
    qa_net_client_id demo_q3_record_client;
    bool demo_playback, demo_first_frame;
    frontend_demo_format demo_format;
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
static frontend_q3_client *q3_client_seat(const qa_frontend *f, uint64_t receiver, uint32_t seat)
{
    qa_frontend_network *n = f ? f->network : NULL;
    if (!n) return NULL;
    for (uint32_t i = 0; i < f->options.seats; ++i) {
        frontend_q3_client *client = n->q3_clients + i;
        if (client->q3_client_requested && client->q3_cgame_owner == receiver &&
            client->q3_client_launch_seat == seat) return client;
    }
    return NULL;
}
static frontend_q3_client *q3_client_receiver(const qa_frontend *f,
    const qa_application_q3_client_context *receiver)
{
    return receiver ? q3_client_seat(f, receiver->receiver, receiver->seat) : NULL;
}
static frontend_q3_client *q3_client_launch(const qa_frontend *f, uint32_t seat)
{
    qa_frontend_network *n = f ? f->network : NULL;
    if (!n) return NULL;
    for (uint32_t i = 0; i < f->options.seats; ++i)
        if (n->q3_clients[i].q3_client_requested && n->q3_clients[i].q3_client_launch_seat == seat)
            return n->q3_clients + i;
    return NULL;
}
static frontend_q3_client *q3_client_connection(const qa_frontend *f, qa_net_client_id connection)
{
    if (!f || !f->network) return NULL;
    for (uint32_t i = 0; i < f->options.seats; ++i)
        if (f->network->q3_clients[i].q3_client_requested &&
            qa_net_client_id_equal(f->network->q3_clients[i].q3_client, connection))
            return f->network->q3_clients + i;
    return NULL;
}
static bool client_player(const frontend_q3_client *n, qa_actor_id *out, qa_error *error)
{
    return qa_application_player_actor(n->frontend->application, n->q3_client_launch_seat, out) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 client lost its actual viewing seat");
}
static frontend_q3_client *q3_client_physical(const qa_frontend *f, uint32_t physical)
{
    return f && f->network && physical < f->options.seats ? f->network->q3_clients + physical : NULL;
}
static frontend_q3_client *q3_service_client(void *context)
{
    frontend_seat *seat = context;
    return q3_client_physical(seat->frontend, seat->id);
}
static const qa_q3_client_peer *q3_view(const frontend_q3_client *n)
{
    return n && n->q3_client_attached ? qa_network_q3_client_view(n->runtime, n->q3_client) : NULL;
}
static bool client_send_address(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *error)
{
    frontend_q3_client *n = context;
    return qa_network_send_address(n->runtime, to, bytes, error);
}
static bool round_publish(qa_network_q3_round *, qa_error *);
static bool demo_q3_accepted(void *,int32_t,qa_bytes,bool,qa_error *);
static bool client_disconnect(void *, const char *, qa_error *);
static bool client_authorization_prepare(frontend_q3_client *, qa_bytes, qa_error *);
static bool client_admission_send(void *, const qa_net_address *, qa_bytes, qa_error *);
static bool client_lan_address(const qa_net_address *);
static bool network_capture_ready(const qa_frontend *,qa_error *);
static bool network_address_fields(qa_source_save_io *,qa_net_address *);
static bool menu_preferences_fields(qa_source_save_io *,frontend_network_menu_preferences[4]);
static bool menu_preferences_load(qa_frontend_network *,qa_error *);
static uint32_t random_rotation(void *);
static bool menu_preferences_master(qa_frontend_network *,qa_net_protocol_id,const char *,qa_error *);
static bool menu_preferences_direct(qa_frontend_network *,qa_net_protocol_id,const char *,const qa_net_address *,qa_error *);
static bool client_native_media_read(void *, frontend_q3_content_native_receipt *,
    frontend_q3_content_role_receipt *, qa_error *);
static bool client_modules_media_read(void *, const application_native_q3_client_modules **,
    frontend_q3_content_role_receipt *, frontend_q3_content_role_receipt *, qa_error *);
static bool q2_raw_protocol(qa_net_protocol_id protocol)
{
    return protocol.kind==QA_NET_Q2_34 || protocol.kind==QA_NET_R1Q2_35 || protocol.kind==QA_NET_Q2PRO_36 ||
        protocol.kind==QA_NET_Q2REPRO_1038 || protocol.kind==QA_NET_Q2PRIVATE_4038;
}
static bool q2_host_protocol(qa_net_protocol_id protocol)
{
    return q2_raw_protocol(protocol) || protocol.kind==QA_NET_Q2KEX_2023;
}
static bool q2_timeout_sync(qa_frontend_network *n,qa_error *error)
{
    qa_frontend *f=n->frontend;
    if(!f->options.network_host || !q2_host_protocol(f->options.network_protocol)) return true;
    qa_application_startup_source source; bool present=false;
    if(!frontend_config_store_primary_server_read(f->config_store,&source,&present,error)) return false;
    if(!present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 host timeout requires its actual primary Source registry");
    qa_console_dialect dialect=qa_cvars_dialect(source.cvars);
    if(dialect!=QA_CONSOLE_Q2 && dialect!=QA_CONSOLE_Q2_RERELEASE) return true;
    const qa_cvar_view *timeout=qa_cvars_find(source.cvars,"timeout");
    if(!timeout)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 host timeout lacks its Source declaration");
    return qa_network_q2_server_timeout_policy(n->runtime,timeout->number,error);
}
static bool q2_host_current(void *context,const frontend_network_q2_host *host)
{
    const qa_frontend_network *n=context;
    return n && n->frontend && n->frontend->network==n && n->q2_host==host &&
        (!n->detached_transport || frontend_network_q2_host_importing(host) || frontend_network_q2_host_imported(host));
}
static bool local_clients_prepare(qa_frontend_network *, qa_error *);
static bool local_clients_tick(qa_frontend_network *, qa_error *);
static bool local_clients_destroy(qa_frontend_network *, qa_error *);
static bool q3_client_destroy(frontend_q3_client *, qa_error *);
static bool local_clients_idle(const qa_frontend_network *);
static bool nq_host_protocol(qa_net_protocol_id);
static bool q1_client_protocol(qa_net_protocol_id);
static bool q2_host_protocol(qa_net_protocol_id);
static bool q3_prepare(qa_frontend_network *, qa_error *);
static bool local_server_prepare(qa_frontend_network *, qa_error *);
static bool local_q3_prepare(frontend_local_client *, qa_actor_id, qa_error *);
static bool q3_client_receive(frontend_q3_client *, const qa_net_datagram *, bool *, qa_error *);
static bool client_drain(frontend_q3_client *, bool, qa_error *);
static bool q1_service_message(qa_frontend_network *, const qa_application_client_source *,
    const qa_nq_message *, qa_error *);
static bool q1_controlled(frontend_network_q1_client *, qa_net_client_id, qa_net_seat_id,
    qa_actor_id, qa_movement_kind, qa_bytes, qa_error *);
static bool q2_download_nonce(void *, uint64_t *, qa_error *);
static bool q2_download_stage(void *, qa_fs_root *, const char *, qa_fs_stage **,
    uint64_t *, qa_error *);
static bool q2_restore_stage(void *, qa_fs_root *, const char *, uint64_t, bool, qa_bytes,
    qa_fs_stage **, uint64_t *, qa_fs_identity *, qa_error *);
bool frontend_network_local_seat(const qa_frontend *f, const qa_net_address *endpoint,
    qa_net_seat_id *seat, uint32_t *authored)
{
    const qa_frontend_network *n = f ? f->network : NULL;
    if (!n) return false;
    for (uint32_t i = 0; i < n->frontend->options.seats; ++i) {
        const frontend_local_client *local = n->local_clients + i;
        if (!local->runtime || !qa_net_address_equal(endpoint, &local->endpoint, true)) continue;
        *seat = local->seat; *authored = local->authored; return true;
    }
    return false;
}
static bool local_server_seat(void *context, const qa_net_address *endpoint,
    qa_net_seat_id *seat, uint32_t *authored)
{
    const qa_frontend_network *n = context;
    return frontend_network_local_seat(n->frontend, endpoint, seat, authored);
}
static bool q2_local_groups_prepare(qa_frontend_network *n,qa_error *error)
{
    return local_clients_prepare(n, error);
}
static bool unified_current(void *context,const frontend_network_unified *owner)
{
    qa_frontend_network *n=context;
    return n && n->frontend && n->frontend->network==n && n->unified==owner &&
        (!n->detached_transport || frontend_network_unified_imported(owner));
}
static bool unified_client_current(void *context,const frontend_network_unified_client_service *service)
{
    const qa_frontend_network *n=context;
    return n && n->frontend && n->frontend->network==n &&
        n->unified_client_service==service && !n->detached_transport;
}
static bool unified_client_disconnected(void *context,const qa_application_client_source *source,
    const char *reason,qa_error *error)
{
    qa_frontend_network *n=context;
    if(!source || !reason || !unified_client_current(n,n->unified_client_service) ||
        !qa_application_client_current(n->frontend->application,source))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Unified disconnect lost its real CLIENT namespace");
    frontend_console_print(n->frontend,&source->context.command,reason); return true;
}
static bool unified_source_input(void *context,qa_net_client_id client,qa_net_seat_id seat,
    qa_actor_id actor,uint64_t epoch,const qa_unified_input *input,qa_error *error)
{
    qa_frontend_network *n=context;
    return n && n->runtime && n->unified && epoch==qa_network_epoch(n->runtime,client) &&
        application_unified_player_input(n->frontend->application,client,seat,actor,input,error);
}
static bool unified_tick(qa_frontend_network *n,qa_error *error)
{
    if(n->unified_client_service && !n->unified &&
        !frontend_network_unified_client_retired(n->unified_client_service)) {
        bool ready=false;
        if(!frontend_network_unified_client_advance(n->unified_client_service,&ready,error)) return false;
        if(!ready) return true;
        frontend_remote_unified_options client;
        if(!frontend_network_unified_client_options_read(n->unified_client_service,&client,error)) return false;
        frontend_network_unified_client_view physical;
        if(!frontend_network_unified_client_metadata_read(n->unified_client_service,&physical,error)) return false;
        frontend_network_unified_options options={.frontend=n->frontend,.runtime=n->runtime,
            .seat_owner=QA_NETWORK_COMMAND_OWNER,.remote=physical.remote,.context=n,.current=unified_current,
            .client=client,.client_service=n->unified_client_service};
        if(!frontend_network_unified_create(&options,&n->unified,error)) return false;
    }
    bool waiting=false;
    if(n->unified && !frontend_network_unified_tick(n->unified,n->frontend->wall_time_ns,&waiting,error)) return false;
    if(n->unified_client_service && frontend_network_unified_client_retired(n->unified_client_service)) {
        frontend_network_unified_client_view held;
        if(!frontend_network_unified_client_metadata_read(n->unified_client_service,&held,error)) return false;
        if(qa_net_connections_get(qa_network_connections(n->runtime),held.physical.source.client)) return true;
        if(!frontend_network_unified_destroy(&n->unified,error)) return false;
        qa_error issue={0};
        if(!frontend_network_unified_client_destroy(&n->unified_client_service,&issue) && issue.code!=QA_OK) {
            if(error) *error=issue;
            return false;
        }
        return true;
    }
    size_t executed=0;
    return !n->unified_client_service || !n->unified ||
        frontend_network_unified_client_drain(n->unified_client_service,1024,&executed,error);
}
static bool local_unified_current(void *context, const frontend_network_unified *owner)
{
    const frontend_local_client *local = context;
    return local->network->frontend->network == local->network && local->unified == owner;
}
static bool local_service_current(void *context, const frontend_network_unified_client_service *service)
{
    const frontend_local_client *local = context;
    return local->network->frontend->network == local->network && local->service == service;
}
static bool local_service_disconnected(void *context, const qa_application_client_source *source,
    const char *reason, qa_error *error)
{
    frontend_local_client *local = context;
    (void)error;
    frontend_console_print(local->network->frontend, &source->context.command, reason);
    return true;
}
static bool local_admit(void *context, const qa_net_connect *request, qa_error *error)
{
    frontend_local_client *local = context;
    bool recognized = false;
    if (local->q1) return frontend_network_q1_client_admit(local->q1, request, &recognized, error) && recognized;
    if (local->q2) return frontend_network_q2_client_admit(local->q2, request, &recognized, error) && recognized;
    if (local->q3) {
        qa_actor_id actor; qa_actor_owner owner; qa_q3_product product; uint32_t seat;
        return qa_application_player_actor(local->network->frontend->application, local->authored, &actor) &&
            qa_application_network_q3_client_source(local->network->frontend->application, actor,
                &owner, &product, &seat, error) && seat == local->authored;
    }
    return frontend_network_unified_admit(local->unified, request, &recognized, error) && recognized;
}
static bool local_connectionless(void *context, qa_network_runtime *runtime,
    const qa_net_datagram *packet, qa_error *error)
{
    frontend_local_client *local = context;
    bool recognized = false;
    (void)runtime;
    if (local->q1) return frontend_network_q1_client_receive(local->q1, packet, &recognized, error);
    if (local->q2) return frontend_network_q2_client_receive(local->q2, packet, &recognized, error);
    if (local->q3) return q3_client_receive(local->q3, packet, &recognized, error);
    return frontend_network_unified_receive(local->unified, packet, &recognized, error);
}
static void local_disconnected(void *context, qa_net_client_id id, const char *reason)
{
    frontend_local_client *local = context;
    (void)reason;
    frontend_network_q1_client_disconnected(local->q1, id);
    frontend_network_q2_client_disconnected(local->q2, id);
    if (local->q3 && local->q3->q3_client_attached && qa_net_client_id_equal(local->q3->q3_client, id)) {
        local->q3->q3_client_attached = false; local->q3->q3_client_active = false;
        local->q3->q3_client_gamestate = false; local->q3->q3_client_retiring = true;
        snprintf(local->q3->q3_client_reason, sizeof(local->q3->q3_client_reason), "%s", reason);
    }
}
static bool local_q1_current(void *context, const frontend_network_q1_client *owner)
{
    const frontend_local_client *local = context;
    return local->network->frontend->network == local->network && local->q1 == owner;
}
static bool local_q2_current(void *context, const frontend_network_q2_client *owner)
{
    const frontend_local_client *local = context;
    return local->network->frontend->network == local->network && local->q2 == owner;
}
static bool local_q1_downloads(void *context, bool *allowed, bool *recording,
    bool *playback, qa_error *error)
{
    frontend_local_client *local = context;
    frontend_remote_q1_source_view view;
    if (!frontend_network_q1_client_source_read(local->q1, &view, error)) return false;
    *allowed = true; *recording = false; *playback = false; return true;
}
static bool local_q1_service(void *context, const qa_application_client_source *source,
    qa_net_protocol_id protocol, const qa_nq_message *message, double seconds,
    uint64_t sequence, qa_error *error)
{
    frontend_local_client *local = context;
    (void)protocol; (void)seconds; (void)sequence;
    return q1_service_message(local->network, source, message, error);
}
static bool local_q1_controlled(void *context, qa_net_client_id client, qa_net_seat_id seat,
    qa_actor_id actor, qa_movement_kind movement, qa_bytes arsenal, qa_error *error)
{
    frontend_local_client *local = context;
    return q1_controlled(local->q1, client, seat, actor, movement, arsenal, error);
}
static bool local_download_nonce(void *context, uint64_t *nonce, qa_error *error)
{
    frontend_local_client *local = context;
    return q2_download_nonce(local->network, nonce, error);
}
static bool local_download_stage(void *context, qa_fs_root *root, const char *path,
    qa_fs_stage **out, uint64_t *nonce, qa_error *error)
{
    frontend_local_client *local = context;
    return q2_download_stage(local->network, root, path, out, nonce, error);
}
static bool local_restore_stage(void *context, qa_fs_root *root, const char *path,
    uint64_t logical_nonce, bool published, qa_bytes prefix, qa_fs_stage **out,
    uint64_t *nonce, qa_fs_identity *identity, qa_error *error)
{
    frontend_local_client *local = context;
    return q2_restore_stage(local->network, root, path, logical_nonce, published,
        prefix, out, nonce, identity, error);
}
static qa_network_q1_client_policy q1_policy(qa_net_protocol_id protocol)
{
    bool qw = qa_q1_is_qw(protocol);
    return (qa_network_q1_client_policy){.message_bytes = qw ? 1450u : 64000u, .fragment_bytes = 1024,
        .queued_bytes = 1024u * 1024u, .service_limit = qw ? 1450u : 64000u, .pending_commands = 64,
        .bytes_per_second = 2500, .nq_options = {.standard_quake = true},
        .nq_identity = {.name = "", .spawn_parameters = ""}};
}
static qa_product_id local_client_selection(qa_frontend *f, uint32_t authored, qa_actor_id actor)
{
    const qa_launch_snapshot *snapshot = qa_application_launch(f->application);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    static const qa_launch_role roles[] = {QA_ROLE_HUD, QA_ROLE_CHARACTER};
    for (size_t role = 0; role < sizeof(roles) / sizeof(roles[0]); ++role) {
        const qa_launch_binding *binding = NULL;
        for (size_t i = 0; i < choices->binding_count; ++i) {
            const qa_launch_binding *candidate = choices->bindings + i;
            if (candidate->role == roles[role] && candidate->scope.kind == QA_SCOPE_ACTOR &&
                qa_actor_id_equal(candidate->scope.actor, actor) && !*candidate->selector) {
                binding = candidate; break;
            }
        }
        if (!binding) binding = qa_launch_binding_for(choices,
            (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = authored}, roles[role], "");
        if (!binding) binding = qa_launch_binding_for(choices,
            (qa_launch_scope){.kind = QA_SCOPE_WORLD}, roles[role], "");
        const qa_launch_instance *selected = binding ? qa_launch_snapshot_find(snapshot, binding->instance) : NULL;
        if (selected) return selected->selection.product;
    }
    return QA_PRODUCT_NONE;
}
static bool local_clients_prepare(qa_frontend_network *n, qa_error *error)
{
    qa_frontend *f = n->frontend;
    qa_application_map_view map;
    if (!n->loopback || n->detached_transport || n->demo_playback || f->options.network_connect || f->options.dedicated ||
        qa_application_get_state(f->application) != QA_APPLICATION_RUNNING ||
        !qa_application_map_read(f->application, &map)) return true;
    if (!local_server_prepare(n, error)) return false;
    if (f->options.network_protocol.kind == QA_NET_UNIFIED_1 && !n->unified) {
        application_unified_source source;
        if (!application_unified_source_read(f->application, &source, error)) return false;
        n->composition = qa_application_configuration_generation(f->application);
        n->unified_map_revision = source.map_revision;
        frontend_network_unified_options server = {.frontend = f, .runtime = n->runtime, .server = true,
            .seat_owner = NETWORK_OWNER, .remote_seat_base = 256, .context = n,
            .current = unified_current, .local_seat = local_server_seat};
        if (!frontend_network_unified_create(&server, &n->unified, error)) return false;
    }
    for (uint32_t i = 0; i < f->options.seats; ++i) {
        frontend_local_client *local = n->local_clients + i;
        if (local->runtime) continue;
        uint32_t authored;
        qa_actor_id actor;
        if (!frontend_seat_launch_id_read(f, i, &authored) ||
            !qa_application_player_actor(f->application, authored, &actor)) continue;
        *local = (frontend_local_client){.network = n, .physical = i, .authored = authored,
            .seat = {NETWORK_OWNER, authored}, .client_seat = {NETWORK_OWNER + i + 1, authored}};
        qa_net_transport *transport = NULL;
        char name[48];
        snprintf(name, sizeof(name), "client-%u", i);
        if (!qa_net_loopback_bind(n->loopback, name, &transport, error)) return false;
        local->endpoint = *qa_net_transport_address(transport);
        qa_network_options options = {.owner = NETWORK_OWNER + i + 1, .clients = 1, .packets_per_pump = 256,
            .hooks = {.context = local, .admit = local_admit, .controlled = q1_client_protocol(f->options.network_protocol) ?
                local_q1_controlled : NULL, .connectionless = local_connectionless,
                .disconnected = local_disconnected}};
        if (!qa_network_create(transport, &options, &local->runtime, error)) {
            qa_net_transport_close(transport); return false;
        }
        qa_product_id selected = local_client_selection(f, authored, actor), profile;
        if (!frontend_config_store_client_profile(f->config_store, selected, &profile, error)) return false;
        if (q1_client_protocol(f->options.network_protocol)) {
            frontend_network_q1_client_options client = {.frontend = f, .runtime = local->runtime,
                .remote = n->loopback_server, .protocol = f->options.network_protocol,
                .physical_seat = i, .qport = (uint16_t)(n->rotation_random + i),
                .profile = profile, .selected = selected, .seat = local->client_seat,
                .policy = q1_policy(f->options.network_protocol), .context = local,
                .current = local_q1_current, .download_nonce = local_download_nonce,
                .downloads = local_q1_downloads, .service = local_q1_service};
            if (!frontend_config_store_neutral_pending_options(f->config_store, i, &client.configuration, error)) return false;
            if (!frontend_network_q1_client_create(&client, &local->q1, error)) {
                if (!local->q1) (void)frontend_config_store_neutral_options_cancel(f->config_store,
                    &client.configuration, NULL);
                return false;
            }
        } else if (q2_host_protocol(f->options.network_protocol)) {
            frontend_network_q2_client_options client = {.frontend = f, .runtime = local->runtime,
                .remote = n->loopback_server, .protocol = f->options.network_protocol,
                .physical_seat = i, .qport = (uint16_t)(n->rotation_random + i),
                .profile = profile, .selected = selected, .seat = local->client_seat,
                .context = local, .current = local_q2_current, .download_stage = local_download_stage,
                .restore_stage = local_restore_stage};
            if (!frontend_network_q2_client_create(&client, &local->q2, error)) return false;
        } else if (f->options.network_protocol.kind == QA_NET_Q3_68) {
            if (!local_q3_prepare(local, actor, error)) return false;
        } else if (f->options.network_protocol.kind == QA_NET_UNIFIED_1) {
            frontend_network_unified_client_options client = {.frontend = f, .runtime = local->runtime,
                .remote = n->loopback_server, .physical_seat = i, .seat = local->client_seat,
                .selected = selected, .profile = profile, .context = local,
                .current = local_service_current, .disconnected = local_service_disconnected};
            if (!frontend_config_store_neutral_pending_options(f->config_store, i, &client.configuration, error)) return false;
            if (!frontend_network_unified_client_create(&client, &local->service, error)) {
                if (!local->service) (void)frontend_config_store_neutral_options_cancel(f->config_store,
                    &client.configuration, NULL);
                return false;
            }
        }
    }
    return local_clients_tick(n, error);
}
static bool local_clients_tick(qa_frontend_network *n, qa_error *error)
{
    for (uint32_t i = 0; i < n->frontend->options.seats; ++i) {
        frontend_local_client *local = n->local_clients + i;
        if (!local->runtime) continue;
        if (local->q1 && !frontend_network_q1_client_tick(local->q1, n->frontend->wall_time_ns, error)) return false;
        if (local->q2 && !frontend_network_q2_client_tick(local->q2, n->frontend->wall_time_ns, error)) return false;
        if (local->q3 && !client_drain(local->q3, false, error)) return false;
        if (!local->service) {
            if (!qa_network_tick(local->runtime, n->frontend->wall_time_ns, error)) return false;
            continue;
        }
        if (!local->unified && !frontend_network_unified_client_retired(local->service)) {
            bool ready = false;
            if (!frontend_network_unified_client_advance(local->service, &ready, error)) return false;
            if (!ready) continue;
            frontend_remote_unified_options consumer;
            if (!frontend_network_unified_client_options_read(local->service, &consumer, error)) return false;
            frontend_network_unified_options client = {.frontend = n->frontend, .runtime = local->runtime,
                .seat_owner = local->client_seat.owner, .remote = n->loopback_server, .context = local,
                .current = local_unified_current, .client = consumer, .client_service = local->service};
            if (!frontend_network_unified_create(&client, &local->unified, error)) return false;
        }
        bool waiting = false;
        if (local->unified && !frontend_network_unified_tick(local->unified,
            n->frontend->wall_time_ns, &waiting, error)) return false;
        size_t executed = 0;
        if (local->unified && !frontend_network_unified_client_retired(local->service) &&
            !frontend_network_unified_client_drain(local->service, 1024, &executed, error)) return false;
        if (!qa_network_tick(local->runtime, n->frontend->wall_time_ns, error)) return false;
    }
    return true;
}
static bool local_clients_idle(const qa_frontend_network *n)
{
    for (uint32_t i = 0; i < n->frontend->options.seats; ++i) {
        const frontend_local_client *local = n->local_clients + i;
        if (!qa_network_callbacks_idle(local->runtime) || !frontend_network_unified_idle(local->unified) ||
            !frontend_network_unified_client_idle(local->service) || !frontend_network_q1_client_idle(local->q1) ||
            !frontend_network_q2_client_idle(local->q2)) return false;
    }
    return true;
}
static bool local_clients_destroy(qa_frontend_network *n, qa_error *error)
{
    for (uint32_t i = 0; i < n->frontend->options.seats; ++i) {
        frontend_local_client *local = n->local_clients + i;
        if (!frontend_network_unified_destroy(&local->unified, error) ||
            !frontend_network_unified_client_destroy(&local->service, error) ||
            !frontend_network_q1_client_destroy(&local->q1, error) ||
            !frontend_network_q2_client_destroy(&local->q2, error) ||
            (local->q3 && !q3_client_destroy(local->q3, error))) return false;
        qa_network_destroy(local->runtime); *local = (frontend_local_client){0};
    }
    return true;
}

static frontend_network_q1_client *q1_client_at(const qa_frontend_network *n, uint32_t physical)
{
    if (!n) return NULL;
    return frontend_network_q1_client_owns_input(n->q1_client_owner, physical) ?
        n->q1_client_owner : n->local_clients[physical].q1;
}
static frontend_network_q2_client *q2_client_at(const qa_frontend_network *n, uint32_t physical)
{
    if (!n) return NULL;
    return frontend_network_q2_client_owns_input(n->q2_client_owner, physical) ?
        n->q2_client_owner : n->local_clients[physical].q2;
}

static bool q2_client_current(void *context,const frontend_network_q2_client *client)
{
    const qa_frontend_network *n=context;
    return n && n->frontend && n->frontend->network==n && n->q2_client_owner==client &&
        (!n->detached_transport || frontend_network_q2_client_importing(client));
}
static bool nq_host_protocol(qa_net_protocol_id protocol)
{ return protocol.kind<=QA_NET_RMQ999 && qa_q1_profile_valid(protocol,NULL); }
static bool q1_client_protocol(qa_net_protocol_id protocol)
{
    return protocol.kind==QA_NET_NQ15 || protocol.kind==QA_NET_FITZ666 || protocol.kind==QA_NET_RMQ999 ||
        protocol.kind==QA_NET_QW28 || protocol.kind==QA_NET_QW29;
}
static bool remote_client_protocol(qa_net_protocol_id protocol)
{
    return q1_client_protocol(protocol) || q2_host_protocol(protocol) || protocol.kind==QA_NET_UNIFIED_1;
}
static bool client_target_selected(const qa_frontend_network *n)
{
    const qa_frontend *f=n?n->frontend:NULL;
    return f && f->options.network_connect && !f->options.dedicated && f->options.seats==1 &&
        (n->q3_clients[0].q3_client_requested || remote_client_protocol(f->options.network_protocol));
}
static bool q1_client_current(void *context,const frontend_network_q1_client *client)
{
    qa_frontend_network *n=context;
    return n && n->frontend && n->frontend->network==n && n->q1_client_owner==client &&
        !n->detached_transport;
}
static bool q1_downloads(void *context,bool *allowed,bool *recording,bool *playback,qa_error *error)
{
    qa_frontend_network *n=context; frontend_remote_q1_source_view view;
    if(!allowed || !recording || !playback || !q1_client_current(n,n->q1_client_owner) ||
        !frontend_network_q1_client_source_read(n->q1_client_owner,&view,error)) return false;
    *allowed=true; *recording=n->demo_q1_sink.append!=NULL;
    *playback=n->demo_playback; return true;
}
static bool q1_service(void *context,const qa_application_client_source *source,qa_net_protocol_id protocol,
    const qa_nq_message *message,double seconds,uint64_t sequence,qa_error *error)
{
    qa_frontend_network *n=context; (void)protocol; (void)seconds; (void)sequence;
    return q1_client_current(n,n->q1_client_owner) && q1_service_message(n,source,message,error);
}
static bool q1_service_message(qa_frontend_network *n,const qa_application_client_source *source,
    const qa_nq_message *message,qa_error *error)
{
    if(!source || !message || !qa_application_client_current(n->frontend->application,source)) return false;
    if(message->op==QA_NQ_PRINT) {
        frontend_console_print(n->frontend,&source->context.command,message->data.text); return true;
    }
    if(message->op!=QA_NQ_STUFFTEXT) return true;
    const char *pending=message->data.text;
    while(pending && *pending) {
        size_t length=strlen(pending),offset=qa_command_separator(pending,length,QA_CONSOLE_Q1);
        char *line=malloc(offset+1); qa_command_tokens tokens={0};
        if(!line) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining received Source command diagnostic");
        memcpy(line,pending,offset); line[offset]=0;
        bool ok=qa_command_tokenize(line,QA_CONSOLE_Q1,false,&tokens,error);
        const char *trimmed=line;
        while(*trimmed && (unsigned char)*trimmed<=32) ++trimmed;
        size_t trimmed_length=strlen(trimmed);
        while(trimmed_length && (unsigned char)trimmed[trimmed_length-1]<=32) --trimmed_length;
        bool reconnect=trimmed_length==9 && !memcmp(trimmed,"reconnect",9);
        bool bonus=ok && tokens.count && strlen(tokens.values[0])==2 &&
            (tokens.values[0][0]=='b' || tokens.values[0][0]=='B') &&
            (tokens.values[0][1]=='f' || tokens.values[0][1]=='F') && !tokens.values[0][2];
        if(ok && tokens.count && !bonus && !reconnect) {
            size_t size=offset+28; char *text=malloc(size);
            if(!text) ok=frontend_fail(error,QA_ERROR_MEMORY,"Formatting received Source command diagnostic");
            else { snprintf(text,size,"Unhandled server command: %s\n",line);
                frontend_console_print(n->frontend,&source->context.command,text); free(text); }
        }
        qa_command_tokens_free(&tokens); free(line);
        if(!ok) return false;
        pending+=offset<length?offset+1:offset;
    }
    return true;
}
static bool q2_download_nonce(void *context,uint64_t *out,qa_error *error)
{
    qa_frontend_network *n=context;
    if(!n || !out || !n->frontend || n->frontend->network!=n || n->detached_transport || n->nonce==UINT64_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 download stage namespace is unavailable");
    *out=++n->nonce; return true;
}
static bool q2_download_stage(void *context,qa_fs_root *root,const char *path,
    qa_fs_stage **out,uint64_t *nonce,qa_error *error)
{
    qa_frontend_network *n=context; uint64_t initial=0;
    if(!n || !n->frontend || n->frontend->network!=n || n->detached_transport ||
        !root || !path || !out || *out || !nonce || *nonce)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 download stage lacks its live Network namespace");
    if(!qa_fs_stage_open_unique_checked(root,path,&n->nonce,0,out,&initial,error)) return false;
    *nonce=n->nonce; return true;
}
static bool q2_restore_stage(void *context,qa_fs_root *root,const char *path,uint64_t logical_nonce,
    bool published,qa_bytes prefix,qa_fs_stage **out,uint64_t *nonce,qa_fs_identity *identity,qa_error *error)
{
    qa_frontend_network *n=context; uint64_t initial=0; size_t written=0;
    if(!n || !n->frontend || n->frontend->network!=n || !n->detached_transport ||
        !n->frontend->source_restoring || !root || !path || !logical_nonce ||
        !out || *out || !nonce || *nonce || !identity || (prefix.size && !prefix.data))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 download import lacks its isolated stage namespace");
    if(published) {
        if(!qa_fs_stage_open_published_checked(root,path,prefix,out,identity,error)) return false;
        *nonce=logical_nonce; return true;
    }
    if(!n->preparation_nonce) n->preparation_nonce=n->nonce;
    if(!qa_fs_stage_open_unique_checked(root,path,&n->preparation_nonce,logical_nonce,out,&initial,error) ||
        initial || !qa_fs_stage_write(*out,0,prefix,&written,error) || written!=prefix.size) return false;
    *nonce=n->preparation_nonce; return true;
}
bool frontend_network_client_configuration_primary(const qa_frontend *f,const qa_application_client_source *source)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(!n || !source || n->frontend!=f || n->busy) return false;
    uint32_t physical = source->context.physical_seat;
    const frontend_local_client *local = physical < f->options.seats ? n->local_clients + physical : NULL;
    bool local_source = local && (local->service || local->q1 || local->q2) && source->runtime == local->runtime;
    if (!local_source && ((!f->options.network_connect && !n->demo_playback) ||
        (source->runtime!=n->runtime && (!n->detached_transport || !f->source_restoring)))) return false;
    frontend_network_q2_client *q2 = physical < f->options.seats ? q2_client_at(n, physical) : NULL;
    if(q2) return frontend_network_q2_client_configuration_primary(q2,source);
    frontend_client_source_view held;
    frontend_network_q1_client *q1 = physical < f->options.seats ? q1_client_at(n, physical) : NULL;
    if (q1) {
        frontend_network_q1_client_view view;
        if (!frontend_network_q1_client_metadata_read(q1, &view, NULL)) return false;
        held = view.physical;
    } else if (local_source) {
        frontend_network_unified_client_view view;
        if (!frontend_network_unified_client_metadata_read(local->service, &view, NULL)) return false;
        held = view.physical;
    } else if(n->q1_client_owner) {
        frontend_network_q1_client_view view;
        if(!frontend_network_q1_client_metadata_read(n->q1_client_owner,&view,NULL)) return false;
        held=view.physical;
    } else if(n->unified_client_service) {
        frontend_network_unified_client_view view;
        if(!frontend_network_unified_client_metadata_read(n->unified_client_service,&view,NULL)) return false;
        held=view.physical;
    } else return false;
    return frontend_client_source_descriptor_equal(source->descriptor,held.source.descriptor) &&
        source->context.receiver==held.source.context.receiver &&
        source->context.entity_owner==held.source.context.entity_owner && source->context.lifetime==held.source.context.lifetime &&
        source->context.seat==held.source.context.seat &&
        source->context.physical_seat==held.source.context.physical_seat &&
        source->context.console==held.source.context.console && source->context.cvars==held.source.context.cvars &&
        source->configuration_generation==held.source.configuration_generation &&
        qa_net_client_id_equal(source->client,held.source.client) && source->connection_epoch==held.source.connection_epoch;
}
bool frontend_network_client_recipient_read(const qa_frontend *f,uint32_t physical,
    frontend_network_client_recipient *out,bool *present,qa_error *error)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(!f || !out || !present || physical>=f->options.seats)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT recipient requires its actual physical seat");
    *out=(frontend_network_client_recipient){0}; *present=false;
    if(!n) return true;
    frontend_network_q1_client *q1 = q1_client_at(n, physical);
    frontend_network_q2_client *q2 = q2_client_at(n, physical);
    if(q2) {
        if(!frontend_network_q2_client_owns_input(q2,physical) ||
            frontend_network_q2_client_retired(q2)) return true;
        if(!frontend_network_q2_client_configuration_read(q2,&out->source,&out->ready,error)) return false;
    } else {
        frontend_client_source_view held;
        if(q1) {
            if(frontend_network_q1_client_retired(q1)) return true;
            frontend_network_q1_client_view view;
            if(!frontend_network_q1_client_metadata_read(q1,&view,error)) return false;
            held=view.physical;
        } else if(n->unified_client_service || n->local_clients[physical].service) {
            frontend_network_unified_client_service *service = n->unified_client_service ?
                n->unified_client_service : n->local_clients[physical].service;
            if(frontend_network_unified_client_retired(service)) return true;
            frontend_network_unified_client_view view;
            if(!frontend_network_unified_client_metadata_read(service,&view,error)) return false;
            held=view.physical;
        } else return true;
        if(held.source.context.physical_seat!=physical) return true;
        if(!frontend_client_source_current(&held))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT recipient lost its retained physical namespace");
        out->source=held.source; out->ready=held.ready;
    }
    *present=true; return true;
}
bool frontend_network_client_retired_recipient_read(const qa_frontend *f,uint32_t physical,
    frontend_network_client_recipient *out,bool *present,qa_error *error)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(!f || !out || !present || physical>=f->options.seats || !f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Retired CLIENT recipient requires its actual cold physical seat");
    *out=(frontend_network_client_recipient){0}; *present=false;
    if(!n) return true;
    frontend_network_q1_client *q1 = q1_client_at(n, physical);
    frontend_network_q2_client *q2 = q2_client_at(n, physical);
    if(q2) {
        if(!frontend_network_q2_client_owns_input(q2,physical) ||
            !frontend_network_q2_client_retired(q2)) return true;
        if(!frontend_network_q2_client_retired_recipient_read(q2,&out->source,&out->ready,error)) return false;
    } else {
        frontend_client_source_view held;
        if(q1) {
            if(!frontend_network_q1_client_owns_input(q1,physical) ||
                !frontend_network_q1_client_retired(q1)) return true;
            frontend_network_q1_client_view view;
            if(!frontend_network_q1_client_metadata_read(q1,&view,error)) return false;
            held=view.physical;
        } else if(n->unified_client_service || n->local_clients[physical].service) {
            frontend_network_unified_client_service *service = n->unified_client_service ?
                n->unified_client_service : n->local_clients[physical].service;
            if(!frontend_network_unified_client_retired(service)) return true;
            frontend_network_unified_client_view view;
            if(!frontend_network_unified_client_metadata_read(service,&view,error)) return false;
            held=view.physical;
        } else return true;
        if(held.source.context.physical_seat!=physical) return true;
        if(!qa_application_client_retirement_current(f->application,&held.source))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Retired CLIENT recipient lost its retained physical namespace");
        out->source=held.source; out->ready=held.ready;
    }
    bool retained=false;
    if(!frontend_config_store_neutral_retired_recipient(f->config_store,&out->source,&retained,error)) return false;
    if(!retained) { *out=(frontend_network_client_recipient){0}; return true; }
    *present=true; return true;
}
bool frontend_network_client_recipient_current(const qa_frontend *f,uint32_t physical,
    const frontend_network_client_recipient *saved)
{
    frontend_network_client_recipient now; bool present;
    return saved && frontend_network_client_recipient_read(f,physical,&now,&present,NULL) && present &&
        saved->ready==now.ready && saved->source.descriptor==now.source.descriptor &&
        saved->source.context.lifetime==now.source.context.lifetime &&
        saved->source.context.session==now.source.context.session &&
        saved->source.context.receiver==now.source.context.receiver &&
        saved->source.context.entity_owner==now.source.context.entity_owner &&
        saved->source.context.entity_definition==now.source.context.entity_definition &&
        saved->source.context.seat==now.source.context.seat &&
        saved->source.context.physical_seat==now.source.context.physical_seat &&
        saved->source.context.console==now.source.context.console && saved->source.context.cvars==now.source.context.cvars &&
        saved->source.runtime==now.source.runtime && qa_net_client_id_equal(saved->source.client,now.source.client) &&
        saved->source.connection_epoch==now.source.connection_epoch &&
        saved->source.network_seat.owner==now.source.network_seat.owner &&
        saved->source.network_seat.index==now.source.network_seat.index &&
        saved->source.configuration_generation==now.source.configuration_generation &&
        saved->source.context.command.owner==now.source.context.command.owner &&
        saved->source.context.command.session==now.source.context.command.session &&
        saved->source.context.command.client==now.source.context.command.client &&
        saved->source.context.command.seat==now.source.context.command.seat &&
        saved->source.context.command.dialect==now.source.context.command.dialect &&
        saved->source.context.command.origin==now.source.context.command.origin &&
        saved->source.context.command.direct==now.source.context.command.direct &&
        saved->source.context.command.console_text==now.source.context.command.console_text &&
        saved->source.context.command.script==now.source.context.command.script &&
        qa_actor_id_equal(saved->source.context.command.actor,now.source.context.command.actor) &&
        saved->source.context.command.registry==now.source.context.command.registry &&
        saved->source.context.command.generation==now.source.context.command.generation;
}
bool frontend_network_client_retirement_current(const qa_frontend *f,
    const qa_application_client_source *source,const qa_console *console,
    const qa_command_context *command,qa_error *error)
{
    if(!f || !source || !console || !command)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement requires its actual physical namespace");
    frontend_network_q2_client *q2 = source->context.physical_seat < f->options.seats ?
        q2_client_at(f->network, source->context.physical_seat) : NULL;
    if(q2 && frontend_network_q2_client_configuration_primary(q2,source))
        return frontend_network_q2_client_retirement_current(q2,source,console,command,error);
    return frontend_client_sources_retirement_current(f,source,console,command,error);
}
bool frontend_network_client_configuration_advance(qa_frontend *f,qa_application_client_preparation *token,
    bool *complete,qa_error *error)
{
    const qa_application_client_source *source=qa_application_client_prepare_source(token);
    if(!complete || !source || !frontend_network_client_configuration_primary(f,source) ||
        qa_application_client_prepare_application(token)!=f->application ||
        !qa_application_client_prepare_associated(f->application,token) ||
        !qa_application_client_prepare_current(token))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Network configuration requires its actual returned CLIENT token");
    frontend_network_q2_client *q2 = q2_client_at(f->network, source->context.physical_seat);
    if(q2) return frontend_network_q2_client_configuration_advance(q2,token,complete,error);
    for(size_t i=0;i<frontend_client_source_count(f);++i) {
        frontend_client_source *owner=frontend_client_source_at(f,i); frontend_client_source_view view;
        if(!frontend_client_source_metadata_read(owner,&view,error)) return false;
        if(view.source.context.lifetime==source->context.lifetime && view.source.context.receiver==source->context.receiver)
            return frontend_client_source_advance(owner,complete,error);
    }
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Network CLIENT token has no retained physical programme owner");
}
static const char *const names[] = {"serverlist", "serverquery", "serverfavorite", "servermaster", "setmaster",
    "addip", "removeip", "listip", "writeip", "addlrconcmd", "dellrconcmd", "listlrconcmds",
    "heartbeat", "maprotation", "nextmap", "download", "downloadstatus", "downloadcancel", "downloadsuspend",
    "connect", "reconnect", "disconnect", "localservers", "globalservers", "ping", "serverstatus"};
static bool send_address(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *error)
{
    qa_frontend_network *n = context;
    return n->kex_transport?qa_kex_transport_send_connectionless(n->kex_transport,to,bytes,error):
        qa_network_send_address(n->runtime,to,bytes,error);
}
static int menu_family(qa_net_protocol_id);
static void browser_status_set(qa_frontend_network *n,const char *text)
{
    n->browser_status.receipt=++n->browser_receipt;
    snprintf(n->browser_status.text,sizeof(n->browser_status.text),"%s",text);
}
static void browser_changed(void *context,const qa_server_entry *entry,qa_browser_change change)
{
    qa_frontend_network *n=context;
    if(change==QA_BROWSER_STATUS_RECEIVED)
        browser_status_set(n,"Server updated");
    if(change==QA_BROWSER_QUERY_EXPIRED) {
        int family=menu_family(entry->protocol);
        if(family>=0)n->browser_expired[family]=++n->browser_receipt;
    }
}
static void browser_master_complete(void *context,const qa_error *failure,const qa_browser_master_result *result)
{
    qa_frontend_network *n=context;
    if (failure && failure->code!=QA_OK) {
        frontend_print(n->frontend,failure->message);
        frontend_print(n->frontend,"\n");
    }
    if(failure && failure->code!=QA_OK)browser_status_set(n,failure->message);
    else if(result->complete && (result->http || result->protocol.kind!=QA_NET_Q3_68)) {
        char status[96]; snprintf(status,sizeof(status),"Found %zu servers; querying status",result->found);
        browser_status_set(n,status);
    }
}
static bool local_address(void *context, const qa_net_address *address)
{
    const qa_frontend_network *n=context;
    qa_net_udp_policy policy; bool present=false;
    if(!address || !n || !n->runtime || !n->interfaces || n->detached_transport) return false;
    if(address->kind==QA_NET_LOOPBACK) return true;
    bool observed=n->kex_transport?qa_kex_transport_udp_policy(n->kex_transport,&policy,&present,NULL):
        qa_network_udp_policy_read(n->runtime,&policy,&present,NULL);
    return observed && present &&
        qa_net_interfaces_local(n->interfaces,&policy.bound,policy.ipv6_only,address);
}
static bool browser_owner_current(void *context, qa_error *error)
{
    const qa_frontend_network *n = context;
    return (n && n->frontend && n->frontend->network == n && n->runtime && n->browser && n->preferences) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 browser lost its actual Network lifetime");
}
static uint64_t browser_now(void *context)
{ return ((qa_frontend_network *)context)->frontend->wall_time_ns; }
static bool browser_resolve(void *context, const char *text, uint16_t port, qa_net_address *out, bool *present, qa_error *error)
{
    if (!out || !present || !browser_owner_current(context, error)) return false;
    *present = false;
    if (!text || !*text || strlen(text) > 255) return true;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (*p <= 32 || *p == 127) return true;
    qa_error ignored = {0}; *present = qa_net_address_resolve(text, port, 4, out, &ignored);
    return browser_owner_current(context, error);
}
static bool browser_cache_read(void *context, qa_buffer *out, bool *present, qa_error *error)
{
    qa_frontend_network *n = context;
    if (!out || out->data || out->size || !present || !browser_owner_current(n, error)) return false;
    *present = false; qa_fs_file *file = NULL; qa_fs_identity identity; qa_error local = {0};
    if (!qa_fs_root_file_open(n->preferences, "network/servers-cache-q3.bin", &file, &identity, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = local;
        return false;
    }
    bool ok = qa_fs_file_read_snapshot(file, &identity, out, error); qa_fs_file_close(file);
    if (ok) *present = true;
    return ok && browser_owner_current(n, error);
}
static bool browser_cache_write(void *context, qa_bytes bytes, qa_error *error)
{
    qa_frontend_network *n = context; bool created;
    if (!browser_owner_current(n, error) || n->nonce == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 cache publication lifetime or nonce exhausted");
    return qa_fs_root_publish(n->preferences, "network/servers-cache-q3.bin", bytes,
        ++n->nonce, false, true, &created, error) && browser_owner_current(n, error);
}
static bool browser_print(void *context, const char *text, qa_error *error)
{
    qa_frontend_network *n = context;
    if (!browser_owner_current(n, error)) return false;
    frontend_console_print(n->frontend, NULL, text);
    return browser_owner_current(n, error);
}
static frontend_q3_browser_options browser_options(qa_frontend_network *n)
{
    return (frontend_q3_browser_options){.browser = n->browser, .context = n, .current = browser_owner_current,
        .now_ns = browser_now, .resolve = browser_resolve, .cache_read = browser_cache_read,
        .cache_write = browser_cache_write, .print = browser_print};
}
static bool browser_binding_current(void *context, qa_error *error)
{
    frontend_network_browser_binding *binding = context;
    const qa_frontend *f = binding ? binding->frontend : NULL;
    const qa_frontend_network *n = f ? f->network : NULL;
    const frontend_q3_client *client = binding ? q3_client_seat(f, binding->ui.owner, binding->authored_seat) : NULL;
    if (!binding || !n || n != binding->network || f->application != binding->application ||
        !n->q3_browser || binding->access.browser != n->q3_browser || binding->access.cvars != binding->ui.cvars ||
        binding->epoch != (client ? client->q3_client_epoch : 0) || binding->ui.role != QA_QVM_UI ||
        binding->ui.session != qa_application_session(f->application) || !binding->ui.owner || !binding->ui.service_owner ||
        !binding->ui.console || !binding->ui.cvars || !binding->ui.frontend_lifetime ||
        !binding->qualified || !binding->qualified(binding->context, &binding->ui, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "UI browser lost its actual retained role namespace");
    return browser_owner_current((void *)n, error);
}
bool frontend_network_browser_services(qa_frontend *f, const qa_q3_host_client_context *ui,
    uint32_t seat, uint64_t epoch, void *context,
    bool (*qualified)(void *, const qa_q3_host_client_context *, qa_error *),
    frontend_network_browser_binding *binding, qa_q3_host_browser_services *out, qa_error *error)
{
    if (!f || !f->application || !f->network || !ui || !binding || !out || !qualified)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "UI browser requires its actual physical lease");
    *binding = (frontend_network_browser_binding){.frontend = f, .application = f->application,
        .network = f->network, .ui = *ui, .authored_seat = seat, .epoch = epoch, .context = context, .qualified = qualified};
    binding->access = (frontend_q3_browser_access){.browser = f->network->q3_browser, .cvars = ui->cvars,
        .context = binding, .current = browser_binding_current};
    return browser_binding_current(binding, error) && frontend_q3_browser_services(&binding->access, out, error);
}
bool frontend_network_ui_client_state(frontend_network_browser_binding *binding, const qa_q3_host *host,
    void *context, bool (*host_current)(void *, const qa_q3_host *, const qa_q3_host_client_context *, qa_error *),
    qa_q3_ui_client_state *out, qa_error *error)
{
    const frontend_q3_client *n = binding ? q3_client_seat(binding->frontend, binding->ui.owner, binding->authored_seat) : NULL;
    if (!out || !host || !host_current || !browser_binding_current(binding, error) ||
        !n || !n->q3_client_requested ||
        !host_current(context, host, &binding->ui, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "UI client state lost its exact physical entered host");
    qa_q3_ui_client_state state = {.phase = 1, .client_number = n->q3_ui_client_number};
    if (n->q3_client_requested && !n->q3_client_closed && !n->q3_client_retiring) {
        state.phase = n->q3_client_active ? 8 : n->q3_client_initializing ? 6 : n->q3_client_gamestate ? 7 :
            n->q3_client_admission.phase == QA_Q3_ADMITTED ? 5 :
            n->q3_client_admission.phase == QA_Q3_CHALLENGING ? 4 :
            n->q3_client_admission.phase == QA_Q3_CONNECTING ? 3 : 1;
    }
    uint32_t packets = n->q3_client_admission.connect_packets;
    memcpy(&state.connect_packet_count, &packets, sizeof(packets));
    memcpy(state.server_name, n->network->client_server, sizeof(state.server_name));
    memcpy(state.update_info, n->q3_client_update_info, sizeof(state.update_info));
    memcpy(state.message, n->q3_client_message, sizeof(state.message));
    if (!browser_binding_current(binding, error) || !host_current(context, host, &binding->ui, error)) return false;
    *out = state; return true;
}
static bool remote_player(qa_application *application, qa_actor_id *out, qa_error *error)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(application));
    return (choices && choices->seat_count == 1 &&
        qa_application_player_actor(application, choices->seats[0].id, out)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 client lost its actual single viewing seat");
}
static bool admit(void *context, const qa_net_connect *request, qa_error *error)
{
    qa_frontend_network *n = context;
    if(n->q1_client_owner) {
        bool recognized=false;
        if(!frontend_network_q1_client_admit(n->q1_client_owner,request,&recognized,error)) return false;
        if(recognized) return true;
    }
    if(n->unified) {
        bool recognized=false;
        if(!frontend_network_unified_admit(n->unified,request,&recognized,error)) return false;
        if(recognized) return true;
    }
    if (n->q2_client_owner) {
        bool recognized = false;
        if (!frontend_network_q2_client_admit(n->q2_client_owner, request, &recognized, error)) return false;
        if (recognized) return true;
    }
    if (n->q2_host) {
        bool recognized=false;
        bool ok=frontend_network_q2_host_importing(n->q2_host) ?
            frontend_network_q2_host_restore_admit(n->q2_host,request,&recognized,error) :
            frontend_network_q2_host_admit(n->q2_host,request,&recognized,error);
        if(!ok) return false;
        if(recognized) return true;
    }
    if (request->composition != n->composition)
        return frontend_fail(error, QA_ERROR_FORMAT, "remote launch generation differs from the selected composition");
    if (frontend_network_remote(n->frontend) && request->protocol.kind == QA_NET_Q3_68) {
        qa_actor_id actor; qa_actor_owner owner; qa_q3_product product; uint32_t seat;
        return remote_player(n->frontend->application, &actor, error) &&
            qa_application_network_q3_client_source(n->frontend->application, actor, &owner, &product, &seat, error) &&
            seat == n->q3_clients[0].q3_client_launch_seat;
    }
    if (n->q3_admission && request->protocol.kind == QA_NET_Q3_68) {
        qa_actor_owner owner; qa_q3_product product;
        return qa_application_network_q3_owner(n->frontend->application, &owner, &product, error);
    }
    if (n->nq_host && nq_host_protocol(request->protocol) &&
        request->protocol.kind == n->frontend->options.network_protocol.kind &&
        request->protocol.flags == n->frontend->options.network_protocol.flags &&
        request->protocol.revision == n->frontend->options.network_protocol.revision) {
        qa_application_network_q1_host source;
        return qa_application_network_q1_host_source(n->frontend->application, &source, error) &&
            ((source.protocol.kind == QA_NET_NQ15 && !source.protocol.flags && !source.protocol.revision) ||
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
    if(n->q1_client_owner) return q1_controlled(n->q1_client_owner, client, seat, actor, movement, arsenal, error);
    return qa_application_network_controlled(n->frontend->application, client, seat, actor, movement, arsenal, error);
}
static bool q1_controlled(frontend_network_q1_client *owner, qa_net_client_id client,
    qa_net_seat_id seat, qa_actor_id actor, qa_movement_kind movement, qa_bytes arsenal, qa_error *error)
{
    frontend_remote_q1_source_view source; frontend_remote_q1_player_view player; bool present=false;
    if(!frontend_network_q1_client_source_read(owner,&source,error) ||
        !qa_net_client_id_equal(source.physical.source.client,client) ||
        source.physical.source.network_seat.owner!=seat.owner || source.physical.source.network_seat.index!=seat.index ||
        arsenal.size || movement!=(qa_q1_is_qw(source.domain.protocol)?QA_MOVEMENT_QUAKEWORLD:QA_MOVEMENT_NETQUAKE) ||
        !frontend_remote_q1_player_read(source.receiver,&player,&present,error) || !present ||
        !qa_actor_id_equal(player.actor,actor) || !frontend_remote_q1_source_current(&source))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 input authority differs from its real received CLIENT player");
    return true;
}
static bool remote_command(void *context, const qa_network_command *command, qa_error *error)
{ return qa_application_network_command(((qa_frontend_network *)context)->frontend->application, command, error); }
static bool remote_q3_command(void *context, const qa_network_q3_source_command *command, qa_error *error)
{ return qa_application_network_q3_command(((qa_frontend_network *)context)->frontend->application, command, error); }
static bool remote_nq_command(void *context, const qa_network_nq_source_command *command, qa_error *error)
{ return qa_application_network_nq_command(((qa_frontend_network *)context)->frontend->application, command, error); }
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
    frontend_network_q2_client_disconnected(n->q2_client_owner, id);
    frontend_network_q1_client_disconnected(n->q1_client_owner,id);
    frontend_network_q2_host_disconnected(n->q2_host, id);
    if (n->q3_clients[0].q3_client_attached && qa_net_client_id_equal(n->q3_clients[0].q3_client, id)) {
        n->q3_clients[0].q3_client_attached = false; n->q3_clients[0].q3_client_active = false; n->q3_clients[0].q3_client_gamestate = false;
        n->q3_clients[0].q3_client_retiring = true;
        snprintf(n->q3_clients[0].q3_client_reason, sizeof(n->q3_clients[0].q3_client_reason), "%s", reason); return;
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
    if(n->kex_browser) {
        bool recognized=false;
        if(!frontend_kex_browser_receive(n->kex_browser,packet,&recognized,error)) return false;
        if(recognized) return true;
    }
    if(n->q1_client_owner) {
        bool recognized=false;
        if(!frontend_network_q1_client_receive(n->q1_client_owner,packet,&recognized,error)) return false;
        if(recognized) return true;
    }
    if(n->unified) {
        bool recognized=false;
        if(!frontend_network_unified_receive(n->unified,packet,&recognized,error)) return false;
        if(recognized) return true;
    }
    if (n->q2_client_owner) {
        bool recognized = false;
        if (!frontend_network_q2_client_receive(n->q2_client_owner, packet, &recognized, error)) return false;
        if (recognized) return true;
    }
    if(n->q2_host) {
        bool recognized=false;
        if(!frontend_network_q2_host_receive(n->q2_host,packet,&recognized,error)) return false;
        if(recognized) return true;
    }
    if (n->nq_host && !qa_server_admin_rejects(n->admin, &packet->from)) {
        bool recognized;
        if (!frontend_nq_receive(n->nq_host, packet, &recognized, error)) return false;
        if (recognized) return true;
    }
    if (frontend_network_remote(n->frontend)) {
        bool recognized = false;
        if (!q3_client_receive(n->q3_clients, packet, &recognized, error)) return false;
        if (recognized) return true;
    }
    bool recognized;
    if (!qa_server_browser_receive(n->browser,packet,&recognized,error)) return false;
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
static bool q3_client_receive(frontend_q3_client *n, const qa_net_datagram *packet,
    bool *recognized, qa_error *error)
{
    *recognized = false;
    qa_q3_connectionless source; qa_q3_admission_result result;
    if (!qa_q3_client_admission_receive(&n->q3_client_admission, &packet->from, packet->payload,
        (int64_t)(packet->received_ns / UINT64_C(1000000)), &result, &source, error)) return false;
    if (result == QA_Q3_ADMISSION_CONNECTED) { n->q3_client_attach = true; *recognized = true; return true; }
    if (result == QA_Q3_ADMISSION_HANDLED || result == QA_Q3_ADMISSION_IGNORED) { *recognized = true; return true; }
    if (!strcmp(qa_q3_token(&source.tokens, 0), "disconnect") && n->q3_client_attached &&
        qa_net_address_equal(&packet->from, &n->q3_client_admission.address, true)) {
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(n->runtime), n->q3_client);
        if (client && packet->received_ns >= client->received_ns &&
            packet->received_ns - client->received_ns >= UINT64_C(3000000000)) {
            *recognized = true; return client_disconnect(n, "Server disconnected", error);
        }
        *recognized = true; return true;
    }
    const char *command = qa_q3_token(&source.tokens, 0);
    bool print = (command[0] == 'p' || command[0] == 'P') &&
        (command[1] == 'r' || command[1] == 'R') && (command[2] == 'i' || command[2] == 'I') &&
        (command[3] == 'n' || command[3] == 'N') && (command[4] == 't' || command[4] == 'T') && !command[5];
    if (print &&
        qa_net_address_equal(&packet->from, &n->q3_client_admission.address, false)) {
        size_t used = 0;
        while (used < source.payload_size && used < sizeof(n->q3_client_message) - 1 && source.payload[used]) {
            uint8_t ch = source.payload[used];
            n->q3_client_message[used++] = (char)(ch == '%' || ch > 127 ? '.' : ch);
        }
        n->q3_client_message[used] = 0;
        frontend_print(n->frontend, n->q3_client_message); *recognized = true; return true;
    }

    return true;
}
static bool kex_connectionless(void *context,const qa_net_datagram *packet,bool *recognized,qa_error *error)
{
    qa_frontend_network *n=context;
    if(!n || !packet || !recognized || !n->runtime) return false;
    *recognized=false;
    if(n->kex_browser) {
        if(!frontend_kex_browser_receive(n->kex_browser,packet,recognized,error)) return false;
        if(*recognized) return true;
    }
    if(n->q2_client_owner) {
        if(!frontend_network_q2_client_receive(n->q2_client_owner,packet,recognized,error)) return false;
        if(*recognized) return true;
    }
    if(n->q2_host) {
        if(!frontend_network_q2_host_receive(n->q2_host,packet,recognized,error)) return false;
        if(*recognized) return true;
    }
    if(!qa_server_browser_receive(n->browser,packet,recognized,error)) return false;
    if(*recognized) return true;
    qa_admin_result result;
    if(!qa_server_admin_receive(n->admin,packet,&result,error)) return false;
    *recognized=result!=QA_ADMIN_IGNORED || qa_server_admin_rejects(n->admin,&packet->from);
    return true;
}
static const char *password(void *context, bool limited)
{
    qa_frontend_network *n = context;
    qa_application_startup_source source; bool present=false;
    if (!n->frontend->options.network_host ||
        !frontend_config_store_primary_server_read(n->frontend->config_store,&source,&present,NULL) || !present) return "";
    const qa_cvar_view *v = qa_cvars_find(source.cvars,
        limited ? "lrcon_password" : qa_cvars_dialect(source.cvars)==QA_CONSOLE_Q3?"rconPassword":"rcon_password");
    return v ? v->value : "";
}
static qa_cvars *admin_rate_registry(void *context)
{
    qa_frontend_network *n=context; qa_application_startup_source source; bool present=false;
    return n->frontend->options.network_host &&
        frontend_config_store_primary_server_read(n->frontend->config_store,&source,&present,NULL) && present?
        source.cvars:NULL;
}
static void admin_print(void *context,const char *text)
{
    qa_frontend_network *n=context; qa_application_startup_source source; bool present=false;
    if (frontend_config_store_primary_server_read(n->frontend->config_store,&source,&present,NULL) && present)
        qa_console_emit(source.console,&source.command,text);
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
    qa_application_startup_source source; bool present=false;
    if (!n->frontend->options.network_host ||
        !frontend_config_store_primary_server_read(n->frontend->config_store,&source,&present,error) || !present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Authenticated administration has no actual hosted Source console");
    qa_command_context command=source.command;
    command.direct=true; command.console_text=true; command.script="remote-console";
    captured_output capture = {.write = write, .context = output};
    bool ok = qa_console_execute_capture(source.console, &command,
        text, captured_print, &capture, error);
    if (capture.error.code) { if (error) *error = capture.error; return false; }
    return ok;
}
static bool travel(void *context, const char *map, qa_error *error)
{
    qa_frontend_network *n = context;
    return qa_application_queue_travel(n->operator_application?n->operator_application:n->frontend->application,
        &(qa_application_travel_request){.expression = map, .carry_players = true}, error);
}
static bool player_count(void *context,uint32_t *out,qa_error *error)
{
    qa_frontend_network *n=context; uint32_t cursor=0,count=0;
    qa_application_network_qw_source source;
    if (!qa_application_network_qw_source_read(n->frontend->application,&source,error)) return false;
    for (;;) {
        qa_application_network_qw_client client; bool present=false;
        if (!qa_application_network_qw_client_next(n->frontend->application,&cursor,&present,&client,error)) return false;
        if (!present) break;
        ++count;
    }
    *out=count; return true;
}
static uint32_t random_rotation(void *context)
{
    qa_frontend_network *n = context;
    n->rotation_random = n->rotation_random * UINT32_C(1664525) + UINT32_C(1013904223);
    return n->rotation_random;
}
static bool admin_options(qa_frontend_network *n,qa_admin_options *out,qa_error *error)
{
    qa_cvars *cvars=qa_application_cvars(n->frontend->application);
    qa_application_startup_source source; bool present=false;
    if (n->frontend->options.network_host) {
        if (!frontend_config_store_primary_server_read(n->frontend->config_store,&source,&present,error)) return false;
        if (!present) return frontend_fail(error,QA_ERROR_ARGUMENT,"Hosting administration has no actual primary Source registry");
        cvars=source.cvars;
    }
    qa_console_dialect dialect=qa_cvars_dialect(cvars);
    const frontend_engine_cvar_handles *refs=&n->frontend->engine_cvars;
    const qa_cvar_view *filter=qa_cvars_read(cvars,refs->filterban),
        *published=qa_cvars_read(cvars,refs->public_server),
        *dedicated=qa_cvars_read(cvars,refs->dedicated);
    bool q2=dialect==QA_CONSOLE_Q2 || dialect==QA_CONSOLE_Q2_RERELEASE;
    *out=(qa_admin_options){.dialect=dialect,.filters=1024,.rate_entries=1024,.burst=10,
        .rate_interval_ns=UINT64_C(1000000000),.heartbeat_interval_ns=UINT64_C(300000000000),
        .deny_matches=!filter || filter->integer!=0,
        .public_server=n->frontend->options.network_host && n->frontend->options.dedicated &&
            (q2?published && published->number!=0:dialect!=QA_CONSOLE_Q3 || (dedicated && dedicated->integer==2)),
        .hooks={.context=n,.password=password,.execute=admin_execute,.send=send_address,
            .travel=travel,.players=player_count,.random=random_rotation,
            .rate_registry=admin_rate_registry,.print=admin_print}};
    return true;
}
bool frontend_network_admin_send(qa_frontend *f,const qa_net_address *address,qa_bytes bytes,qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    if (!n || !n->runtime || n->busy || n->detached_transport || !qa_network_callbacks_idle(n->runtime))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Early Source packet requires its actual returned Network socket");
    return send_address(n,address,bytes,error);
}
bool frontend_network_admin_adopt(qa_frontend *f,qa_server_admin **source,uint32_t source_rotation_random,qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    if (!n || !source || !*source || !n->runtime || !n->admin || n->busy || n->detached_transport ||
        !qa_network_callbacks_idle(n->runtime))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source administration adoption requires its actual new Network transport");
    if(!qa_server_admin_adopt(n->admin,source,error)) return false;
    n->rotation_random=source_rotation_random;
    return true;
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
    const qa_fs_identity **identity, qa_error *error)
{
    qa_frontend_network *n = context; qa_error local = {0};
    *bytes = (qa_bytes){0}; *identity = NULL;
    if (frontend_q3_packages_download(n->q3_packages, name, bytes, identity, &local)) return true;
    if (local.code == QA_ERROR_NOT_FOUND) return true;
    if (error) *error = local;
    return false;
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
        double selected = requested < 1 ? 1 : (double)requested;
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
    qa_net_seat_id local_seat; uint32_t application_seat;
    bool local = frontend_network_local_seat(n->frontend, &request->address, &local_seat, &application_seat);
    if (request->slot >= 64) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 admission selected an invalid source slot");
    frontend_q3_peer *peer = &n->q3_peers[request->slot];
    bool retained = peer->occupied;
    qa_net_client_id retained_id = peer->client;
    if (retained) {
        uint8_t proof[6]; qa_store_u32le(proof, (uint32_t)request->challenge); qa_store_u16le(proof + 4, request->qport);
        n->q3_reconnect = request;
        bool ok = qa_network_reconnect(n->runtime, retained_id, &request->address, (qa_bytes){proof, sizeof(proof)}, n->frontend->wall_time_ns, error);
        n->q3_reconnect = NULL;
        if (!ok) return false;
        peer->retiring = true;
        const qa_net_client *connection = qa_net_connections_get(qa_network_connections(n->runtime), retained_id);
        if (!qa_application_network_detach(n->frontend->application, connection, error) ||
            !qa_network_q3_reconnect_channel(n->runtime, retained_id, request->challenge, request->qport, error)) {
            (void)qa_network_detach(n->runtime, retained_id, "reconnect source reset failed", NULL); return false;
        }
    }
    qa_q3_download_window_destroy(peer->download);
    *peer = (frontend_q3_peer){.network = n, .slot = request->slot, .qport = request->qport,
        .seat = local ? local_seat : (qa_net_seat_id){NETWORK_OWNER, 64u + request->slot},
        .connected_ms = (int64_t)(n->frontend->wall_time_ns / UINT64_C(1000000)),
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
    qa_net_connect connect = {.attachment = local ? QA_NET_LOCAL_SEAT : QA_NET_REMOTE, .endpoint = request->address,
        .protocol = {QA_NET_Q3_68, 0, 0}, .seats = &seat, .seat_count = 1, .composition = n->composition};
    qa_q3_server_hooks hooks = {.context = peer, .world = q3_world, .command = q3_client_command,
        .enter_world = q3_enter, .think = q3_input, .resend_gamestate = q3_signon,
        .pure_rejected_snapshot = q3_rejected_snapshot, .drop = q3_drop};
    if (retained) peer->client = retained_id;
    else if (!qa_network_attach_q3_server(n->runtime, &connect, peer->product, request->challenge,
        request->qport, &hooks, n->frontend->wall_time_ns, &peer->client, error)) {
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
    if (ok && local) {
        const qa_net_client *connection = qa_net_connections_get(qa_network_connections(n->runtime), peer->client);
        ok = qa_application_network_local_bind(n->frontend->application, connection, peer->seat, application_seat, error) &&
            qa_application_player_actor(n->frontend->application, application_seat, &actor);
        if (ok) ok = qa_application_network_q3_userinfo(n->frontend->application, actor, request->userinfo, error);
    } else if (ok) ok = qa_application_remote_player_attach(n->frontend->application, &player, &actor, error);
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
        if (error) *error = (qa_error){0};
        return true;
    }
    return true;
}
static bool q3_query_field(qa_frontend_network *n, char info[1024], const char *key,
    const char *value, qa_error *error)
{
    if (strchr(key, '\\') || strchr(value, '\\')) {
        frontend_print(n->frontend, "Can't use keys or values with a \\\n"); return true;
    }
    if (strchr(key, ';') || strchr(value, ';')) {
        frontend_print(n->frontend, "Can't use keys or values with a semicolon\n"); return true;
    }
    if (strchr(key, '"') || strchr(value, '"')) {
        frontend_print(n->frontend, "Can't use keys or values with a \"\n"); return true;
    }
    if (!qa_q3_info_set(info, 1024, key, "", error)) return false;
    if (!*value) return true;
    size_t pair_size = strlen(key) + strlen(value) + 2;
    if (pair_size >= 1024) pair_size = 1023;
    size_t extent = strlen(info) + pair_size;
    if (extent > 1024) { frontend_print(n->frontend, "Info string length exceeded\n"); return true; }
    return qa_q3_info_set(info, 1024, key, value, error);
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
    if ((mode && mode->number == 2) || (!status && single && single->number != 0)) return true;
    qa_buffer fields = {0}; char info[1024] = {0};
    if (status && !qa_cvars_info(cvars, QA_CVAR_SERVERINFO, 1024, &fields, error)) return false;
    if (fields.data) { memcpy(info, fields.data, fields.size + 1); qa_buffer_free(&fields); }
    uint32_t capacity;
    if (!qa_application_network_q3_host_capacity(n->frontend->application, owner, &capacity, error)) return false;
    const qa_cvar_view *private_clients = qa_cvars_find(cvars, "sv_privateClients");
    int32_t private_count = private_clients ? private_clients->integer : 0;
    size_t public_count = 0;
    for (size_t i = 0; i < count; ++i)
        if ((int64_t)players[i].slot >= private_count && players[i].slot < capacity) ++public_count;
    char clients[32]; snprintf(clients, sizeof(clients), "%zu", public_count);
    const qa_cvar_view *hostname = qa_cvars_find(cvars, "sv_hostname");
    const qa_cvar_view *map = qa_cvars_find(cvars, "mapname");
    if (!q3_query_field(n, info, "challenge", qa_q3_token(&packet->tokens, 1), error)) return false;
    if (!status && (!q3_query_field(n, info, "protocol", "68", error) ||
        !q3_query_field(n, info, "hostname", hostname ? hostname->value : "", error) ||
        !q3_query_field(n, info, "mapname", map ? map->value : "", error) ||
        !q3_query_field(n, info, "clients", clients, error))) return false;
    static const char *const keys[] = {"sv_maxclients", "gametype", "pure", "minPing", "maxPing", "game"};
    static const char *const sources[] = {"sv_maxclients", "g_gametype", "sv_pure", "sv_minPing", "sv_maxPing", "fs_game"};
    for (size_t i = 0; !status && i < sizeof(keys) / sizeof(*keys); ++i) {
        const qa_cvar_view *value = qa_cvars_find(cvars, sources[i]);
        char numeric[32];
        if (!i) snprintf(numeric, sizeof(numeric), "%" PRId64, (int64_t)capacity - private_count);
        else if (i < 5) snprintf(numeric, sizeof(numeric), "%d", value ? value->integer : 0);
        if ((i == 3 || i == 4) && (!value || !value->integer)) continue;
        if (!q3_query_field(n, info, keys[i], i < 5 ? numeric : value ? value->value : "", error)) return false;
    }
    qa_q3_product_policy policy;
    if (status && !qa_application_q3_product_policy_read(n->frontend->application, &policy))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 status lost its genuine retained demo policy");
    if (status && policy.filesystem_restricted) {
        char keywords[1024], marked[1030];
        if (!qa_q3_info_value(info, "sv_keywords", keywords, sizeof(keywords), error)) return false;
        snprintf(marked, sizeof(marked), "demo %s", keywords);
        if (!q3_query_field(n, info, "sv_keywords", marked, error)) return false;
    }
    char line[QA_Q3_MESSAGE_BYTES + 1056];
    int length = snprintf(line, sizeof(line), "%s\n%s%s", status ? "statusResponse" : "infoResponse", info, status ? "\n" : "");
    if (length < 0 || (size_t)length >= sizeof(line)) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 query response exceeds source capacity");
    size_t used = (size_t)length, player_bytes = 0;
    for (size_t i = 0; status && i < count; ++i) {
        char row[1152];
        length = snprintf(row, sizeof(row), "%d %d \"%s\"\n", players[i].score, players[i].ping, players[i].name);
        if (length < 0 || (size_t)length >= sizeof(row)) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 status row exceeds source capacity");
        if ((size_t)length >= QA_Q3_MESSAGE_BYTES - player_bytes) break;
        player_bytes += (size_t)length;
        memcpy(line + used, row, (size_t)length); used += (size_t)length; line[used] = 0;
    }
    qa_buffer wire = {0};
    bool ok = qa_q3_connectionless_encode(line, &wire, error) && send_address(n, address, (qa_bytes){wire.data, wire.size}, error);
    qa_buffer_free(&wire); return ok;
}
static qa_cvars *q3_authorization_cvars(qa_frontend_network *n, qa_error *error)
{
    qa_actor_owner owner; qa_q3_product product;
    if (!n || !n->frontend || !n->frontend->application || !n->q3_admission ||
        n->frontend->options.network_protocol.kind != QA_NET_Q3_68 || frontend_network_remote(n->frontend) ||
        n->q3_generation != qa_application_configuration_generation(n->frontend->application) ||
        !qa_application_network_q3_owner(n->frontend->application, &owner, &product, error)) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 authorization lost its actual primary source host"); return NULL;
    }
    return qa_application_network_q3_host_cvars(n->frontend->application, owner, error);
}
static bool q3_authorization_current(void *context, qa_error *error)
{ return q3_authorization_cvars(context, error) != NULL; }
static bool q3_authorization_policy(void *context, bool *enabled, const char **game,
    const char **strict, qa_error *error)
{
    qa_cvars *cvars = q3_authorization_cvars(context, error);
    if (!cvars) return false;
    const qa_cvar_view *type = qa_cvars_find(cvars, "g_gametype"),
        *single = qa_cvars_find(cvars, "ui_singlePlayerActive"), *directory = qa_cvars_find(cvars, "fs_game"),
        *auth = qa_cvars_find(cvars, "sv_strictAuth");
    if (!type || !single || !directory || !auth)
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 authorization source lacks its actual admission policy rows");
    *enabled = type->number != 2 && single->number == 0;
    *game = directory->value; *strict = auth->value; return true;
}
static bool q3_admission_enabled(void *context, bool *enabled, qa_error *error)
{
    const char *game, *strict;
    return q3_authorization_policy(context, enabled, &game, &strict, error);
}
static void q3_authorization_print(void *context, const char *text)
{ qa_frontend_network *n = context; frontend_print(n->frontend, text); }
static qa_q3_server_authorization_bindings q3_authorization_bindings(qa_frontend_network *n)
{
    return (qa_q3_server_authorization_bindings){.context = n, .current = q3_authorization_current,
        .policy = q3_authorization_policy, .send = send_address, .print = q3_authorization_print};
}
static bool q3_authorize(void *context, const qa_q3_challenge *challenge, qa_error *error)
{
    qa_frontend_network *n = context;
    return qa_q3_server_authorization_request(n->q3_authorization, challenge, error);
}
static bool q3_drop_bot(void *context, uint32_t slot, qa_error *error)
{
    qa_frontend_network *n = context;
    qa_actor_owner owner; qa_q3_product product;
    return qa_application_network_q3_owner(n->frontend->application, &owner, &product, error) &&
        qa_application_network_q3_drop_bot(n->frontend->application, owner, slot, error);
}
static bool q3_slots(qa_frontend_network *n, qa_q3_admission_slot slots[64], size_t *count, qa_error *error)
{
    qa_actor_owner owner; qa_q3_product product; qa_application_network_q3_host_slot physical[64];
    if (!qa_application_network_q3_owner(n->frontend->application, &owner, &product, error) ||
        !qa_application_network_q3_host_slots(n->frontend->application, owner, physical, error)) return false;
    uint32_t capacity;
    if (!qa_application_network_q3_host_capacity(n->frontend->application, owner, &capacity, error)) return false;
    *count = capacity;
    for (size_t i = 0; i < *count; ++i) {
        frontend_q3_peer *peer = &n->q3_peers[i];
        slots[i] = (qa_q3_admission_slot){.slot = (uint32_t)i,
            .phase = physical[i].occupied ? QA_Q3_ACTIVE : QA_Q3_FREE, .bot = physical[i].bot};
        if (!peer->occupied) continue;
        qa_q3_server_state state;
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(n->runtime), peer->client);
        if (!client || !qa_network_q3_state(n->runtime, peer->client, &state, error)) return false;
        slots[i].phase = state.phase; slots[i].address = client->endpoint; slots[i].has_address = true;
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
        qa_net_seat_id local_seat; uint32_t application_seat;
        bool local_player = frontend_network_local_seat(n->frontend, &packet->address,
            &local_seat, &application_seat);
        if (local_player) {
            qa_actor_id actor; uint32_t slot; qa_q3_product product;
            if (!qa_application_player_actor(n->frontend->application, application_seat, &actor) ||
                !qa_application_network_q3_source(n->frontend->application, actor, &slot, &product, error)) return false;
            slots[0] = slots[slot]; count = 1;
            if (!n->q3_peers[slot].occupied) slots[0].phase = QA_Q3_FREE;
        }
        qa_actor_owner owner; qa_q3_product product;
        if (!qa_application_network_q3_owner(n->frontend->application, &owner, &product, error)) return false;
        qa_cvars *cvars = qa_application_network_q3_host_cvars(n->frontend->application, owner, error);
        if (!cvars) return false;
        const qa_cvar_view *private_clients = qa_cvars_find(cvars, "sv_privateClients"),
            *private_password = qa_cvars_find(cvars, "sv_privatePassword"), *reconnect = qa_cvars_find(cvars, "sv_reconnectlimit"),
            *minimum = qa_cvars_find(cvars, "sv_minPing"), *maximum = qa_cvars_find(cvars, "sv_maxPing");
        qa_q3_product_policy policy;
        if (!qa_application_q3_product_policy_read(n->frontend->application, &policy))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 admission lost its retained resolved demo policy");
        qa_q3_admission_options options = {.private_clients = !local_player && private_clients && private_clients->integer > 0 ? (uint32_t)private_clients->integer : 0,
            .private_password = private_password ? private_password->value : "",
            .reconnect_limit_seconds = reconnect ? reconnect->integer : 3,
            .minimum_ping = minimum ? (float)minimum->number : 0, .maximum_ping = maximum ? (float)maximum->number : 0,
            .demo_restricted = policy.filesystem_restricted,
            .authorize_address = qa_q3_server_authorization_address(n->q3_authorization)};
        if (!qa_q3_server_admission_receive(n->q3_admission, &options, slots, count, &packet->address,
            (qa_bytes){packet->bytes, packet->size}, (int64_t)(n->frontend->wall_time_ns / UINT64_C(1000000)), &local)) {
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
    n->composition = generation;
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
static bool local_server_prepare(qa_frontend_network *n, qa_error *error)
{
    qa_frontend *f = n->frontend;
    qa_net_protocol_id protocol = f->options.network_protocol;
    if (protocol.kind == QA_NET_UNIFIED_1) return true;
    n->composition = qa_application_configuration_generation(f->application);
    if (nq_host_protocol(protocol) && !n->nq_host)
        return frontend_nq_create(f, n->runtime, &n->composition, &n->nq_host, error);
    if (protocol.kind == QA_NET_QW28 && !n->qw_host)
        return frontend_qw_create(f, n->runtime, n->admin, &n->composition, &n->qw_host, error);
    if (q2_host_protocol(protocol) && !n->q2_host) {
        frontend_network_q2_host_options host = {.frontend = f, .runtime = n->runtime, .admin = n->admin,
            .protocol = protocol, .composition = n->composition, .context = n,
            .current = q2_host_current, .random = random_rotation,
            .lobby = n->kex_transport ? qa_kex_transport_lobby(n->kex_transport) : NULL,
            .transport = n->kex_transport};
        return frontend_network_q2_host_create(&host, &n->q2_host, error);
    }
    if (protocol.kind == QA_NET_Q3_68 && !n->q3_admission) {
        qa_q3_admission_hooks hooks = {.context = n, .random = random_rotation, .send = send_address,
            .admit = q3_admit, .query = q3_query, .authorize = q3_authorize, .drop_bot = q3_drop_bot,
            .enabled = q3_admission_enabled, .print = q3_authorization_print};
        if (!qa_q3_server_admission_create(&hooks, &n->q3_admission, error)) return false;
        qa_q3_server_authorization_bindings authorization = q3_authorization_bindings(n);
        if (!qa_q3_server_authorization_create(&authorization, &n->q3_authorization, error)) return false;
        n->q3_server_id = n->q3_restarted_server_id = 1;
        n->q3_checksum_feed = (int32_t)random_rotation(n);
        return q3_prepare(n, error);
    }
    return true;
}
qa_save_authority frontend_network_save_authority(const qa_frontend *f)
{
    const qa_frontend_network *n = f ? f->network : NULL;
    if (!n) return QA_SAVE_OFFLINE;
    bool unified_server = frontend_network_unified_server(n->unified);
    if (frontend_network_remote(f) || frontend_network_client_only(f) || n->q1_client_owner ||
        n->q2_client_owner || n->unified_client_service || (n->unified && !unified_server)) return QA_SAVE_REMOTE;
    if (n->loopback && !f->options.network_host && !f->options.network_connect && !f->options.dedicated)
        return QA_SAVE_OFFLINE;
    return n->q3_admission || n->nq_host || n->qw_host || unified_server ||
        n->q2_host ? QA_SAVE_SERVER : QA_SAVE_OFFLINE;
}
bool frontend_network_q2_configs(qa_frontend *f,const qa_q2_config_entry **entries,
    size_t *count,qa_error *error)
{
    if (f && !f->network && !frontend_network_create(f,error)) return false;
    qa_frontend_network *n=f?f->network:NULL;
    if (!n || n->frontend!=f)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original Quake II save requires its actual Source configstrings");
    if (!q2_local_groups_prepare(n,error)) return false;
    if (!n->q2_host)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original Quake II save has no local Source host");
    return frontend_network_q2_host_configs(n->q2_host,entries,count,error);
}
bool frontend_network_remote(const qa_frontend *f)
{ return f && ((f->network&&f->network->demo_playback&&f->network->demo_format==FRONTEND_DEMO_Q3)||
    (f->options.network_connect && f->options.network_protocol.kind == QA_NET_Q3_68)); }
bool frontend_network_lobby_host_read(const qa_frontend *f, bool *ready,
    qa_net_address *endpoint, qa_lobby_wire *wire, qa_error *error)
{
    (void)error;
    *ready = false;
    const qa_frontend_network *n = f->network;
    if (!n || !n->runtime || !f->options.network_host || f->options.network_connect ||
        qa_application_startup_pending(f->application) ||
        qa_application_get_state(f->application) != QA_APPLICATION_RUNNING) return true;
    const qa_net_address *bound = qa_network_local_address(n->runtime);
    if (!bound || !bound->port) return true;
    *endpoint = *bound;
    if (endpoint->kind == QA_NET_IPV4 && !endpoint->host.ipv4[0] &&
        !endpoint->host.ipv4[1] && !endpoint->host.ipv4[2] && !endpoint->host.ipv4[3]) {
        endpoint->host.ipv4[0] = 127;
        endpoint->host.ipv4[3] = 1;
    } else if (endpoint->kind == QA_NET_IPV6) {
        bool wildcard = true;
        for (size_t i = 0; i < sizeof(endpoint->host.ipv6.bytes); ++i)
            wildcard &= endpoint->host.ipv6.bytes[i] == 0;
        if (wildcard) endpoint->host.ipv6.bytes[15] = 1;
    }
    *wire = (qa_lobby_wire){.protocol = f->options.network_protocol,
        .generation = n->composition};
    *ready = true;
    return true;
}
bool frontend_network_client_only(const qa_frontend *f)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(n&&n->frontend==f&&n->demo_playback)return true;
    return n && n->frontend==f && f->options.network_connect && !f->options.network_host &&
        (n->q3_clients[0].q3_client_requested || q1_client_protocol(f->options.network_protocol) ||
         q2_host_protocol(f->options.network_protocol) || f->options.network_protocol.kind==QA_NET_UNIFIED_1);
}
bool frontend_network_client_ready(const qa_frontend *f)
{
    const frontend_q3_client *n = q3_client_physical(f, 0);
    const qa_net_client *client = n ? qa_net_connections_get(qa_network_connections(n->runtime), n->q3_client) : NULL;
    return n && n->q3_client_requested && n->q3_client_attached && n->q3_client_active && !n->q3_client_retiring &&
        !n->q3_client_closed && client && client->phase == QA_NET_ACTIVE && qa_network_q3_client_live(n->runtime, n->q3_client);
}
uint32_t frontend_network_client_time(const qa_frontend *f)
{ return f && f->network ? (uint32_t)f->network->q3_clients[0].q3_client_time : 0; }
static const qa_q3_client_peer *remote_view(const qa_frontend *f)
{
    return q3_view(q3_client_physical(f, 0));
}
static bool remote_client_settings(void *context, const qa_net_client *client,
    qa_q3_client_readiness *ready, qa_q3_client_send *send, qa_error *error)
{
    frontend_q3_client *n = context; qa_application_q3_client_context role;
    if (!n || !n->q3_client_requested || !n->q3_client_attached || n->q3_client_retiring ||
        !client || !qa_net_client_id_equal(client->id, n->q3_client) || client->seat_count != 1 ||
        client->seats[0].seat.owner != n->seat.owner || client->seats[0].seat.index != n->seat.index ||
        client->seats[0].remote_index || !qa_net_address_equal(&client->endpoint, &n->q3_client_admission.address, true) ||
        !qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner, n->q3_client_launch_seat, &role, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 send policy lost its actual receiver and admitted transport seat");
    const qa_cvar_view *maximum = qa_cvars_read(role.cvars, n->cl_maxpackets),
        *duplicate = qa_cvars_read(role.cvars, n->cl_packetdup);
    if (!maximum || !duplicate)
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 client send policy lacks its installed receiver cvars");
    ready->maximum_packets = maximum->integer < 15 ? 15 : maximum->integer > 125 ? 125 : (unsigned)maximum->integer;
    ready->active = n->q3_client_active;
    ready->primed = n->q3_client_gamestate;
    ready->downloading = qa_q3_client_downloads_active(n->q3_client_downloads);
    ready->lan = client_lan_address(&client->endpoint);
    send->packet_dup = duplicate->integer < 0 ? 0 : duplicate->integer > 5 ? 5 : (unsigned)duplicate->integer;
    send->no_delta = false;
    return true;
}
bool frontend_network_q3_client_context_read(const qa_frontend *f, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_context *out, qa_error *error)
{
    frontend_q3_client *n = q3_client_seat(f, receiver, seat);
    const qa_q3_client_peer *peer = n ? q3_view(n) : NULL;
    if (!n || !n->q3_client_requested || n->q3_client_retiring || seat != n->q3_client_launch_seat || receiver != n->q3_cgame_owner || !peer ||
        !qa_application_q3_remote_context_read(f->application, receiver, seat, out, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 context lost its actual connection and CGAME role");
    if (n->q3_client_decoded) {
        const qa_q3_gamestate *state = qa_q3_client_peer_gamestate(peer);
        if (!state || state->client_number < 0 || state->client_number >= 64)
            return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 context has no admitted physical client ordinal");
        out->source_client = (uint32_t)state->client_number;
    }
    out->source_milliseconds = n->q3_client_time;
    return true;
}
bool frontend_network_q3_client_context_current(const qa_frontend *f,
    const qa_application_q3_client_context *context)
{
    frontend_q3_client *n = q3_client_receiver(f, context);
    const qa_q3_client_peer *peer = n ? q3_view(n) : NULL;
    if (!n || !context || !n->q3_client_requested || n->q3_client_retiring || !peer || context->seat != n->q3_client_launch_seat ||
        context->receiver != n->q3_cgame_owner || !qa_application_q3_remote_context_current(f->application, context)) return false;
    const qa_q3_gamestate *state = n->q3_client_decoded ? qa_q3_client_peer_gamestate(peer) : NULL;
    return context->source_client == (state && state->client_number >= 0 ? (uint32_t)state->client_number : UINT32_MAX);
}
static uint64_t client_generation(void *context)
{ frontend_q3_client *n = context; return n->q3_client_epoch; }
static bool client_connection_current(void *context, uint64_t epoch, qa_error *error)
{
    frontend_q3_client *n = context;
    const qa_net_client *client = n && n->runtime ? qa_net_connections_get(
        qa_network_connections(n->runtime), n->q3_client) : NULL;
    qa_application_q3_client_context receiver;
    return (n && n->q3_client_requested && n->q3_client_attached && !n->q3_client_retiring &&
        epoch == n->q3_client_epoch && qa_network_epoch(n->runtime, n->q3_client) == 1 &&
        qa_network_q3_client_live(n->runtime, n->q3_client) &&
        client && client->attachment == (n->network->demo_playback ? QA_NET_LOCAL_SEAT : QA_NET_REMOTE) &&
        (!n->network->demo_playback || qa_q3_client_peer_demo(qa_network_q3_client_view(n->runtime,n->q3_client))) &&
        client->protocol.kind == QA_NET_Q3_68 &&
        !client->protocol.flags && !client->protocol.revision && client->seat_count == 1 &&
        client->seats[0].seat.owner == n->seat.owner && client->seats[0].seat.index == n->seat.index &&
        !client->seats[0].remote_index &&
        qa_net_address_equal(&client->endpoint, &n->q3_client_admission.address, true) &&
        qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, &receiver, error)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 content lost its genuine connection and receiver lifetime");
}
static bool client_content_open(frontend_q3_client *n, const qa_q3_gamestate *state,
    qa_catalog *previous, frontend_q3_content **out, qa_error *error)
{
    qa_application_q3_remote_source source;
    if (!out || *out || !client_connection_current(n, n->q3_client_epoch, error) ||
        !qa_application_q3_remote_source_read(n->frontend->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, n->q3_client_epoch, &source, error)) return false;
    qa_catalog *seed = previous ? previous : qa_launch_instance_catalog(source.descriptor);
    const qa_product *base = seed ? qa_catalog_find(seed, "q3-baseq3") : NULL;
    char game[1024];
    if (!base || qa_catalog_generation(seed) == UINT64_MAX ||
        !qa_q3_info_value(qa_q3_configstring(state, 1), "fs_game", game, sizeof(game), error))
        return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 catalog selection lacks its actual base product and directory");
    qa_catalog *fresh = NULL; qa_product_id selected;
    if (!qa_catalog_discover_remote_q3(seed, base->id, game, qa_catalog_generation(seed) + 1,
        &fresh, &selected, error)) return false;
    frontend_q3_content_request request = {.application = n->frontend->application,
        .descriptor = source.descriptor, .catalog = fresh, .receiver = source.receiver,
        .configuration_generation = source.configuration_generation, .connection_epoch = source.connection_epoch,
        .gamestate = state, .connection = n, .connection_current = client_connection_current,
        .native_media_read = client_native_media_read, .modules_media_read = client_modules_media_read};
    bool ok = frontend_q3_content_create(&request, out, error);
    if (ok) {
        frontend_q3_content_view view;
        ok = frontend_q3_content_read(*out, &view, error) && view.selected == selected;
        if (!ok) { frontend_q3_content_destroy(*out); *out = NULL; }
    }
    qa_catalog_release(fresh); return ok;
}
static bool client_download_current(void *context, qa_error *error)
{
    frontend_q3_client *n = context; frontend_q3_content_view view;
    return n && n->q3_client_decoded && client_connection_current(n, n->q3_client_epoch, error) &&
        frontend_q3_content_read(n->q3_client_content, &view, error);
}
static bool client_download_permission(void *context, bool *allowed, qa_error *error)
{
    frontend_q3_client *n = context; qa_application_q3_client_context receiver;
    if (!allowed || !client_download_current(n, error) ||
        !qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, &receiver, error)) return false;
    const qa_cvar_view *value = qa_cvars_find(receiver.cvars, "cl_allowDownload");
    if (!value) return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 lacks its actual local download permission row");
    *allowed = value->integer != 0; return true;
}
static bool client_download_destination(void *context, const char *remote, qa_buffer *out, qa_error *error)
{
    frontend_q3_client *n = context; char *path = NULL;
    if (!out || out->data || !client_download_current(n, error) ||
        !frontend_q3_content_download_destination(n->q3_client_content, remote, &path, error)) return false;
    *out = (qa_buffer){(uint8_t *)path, strlen(path) + 1}; return true;
}
static bool client_download_reference(void *context, const char *remote, uint32_t checksum, qa_error *error)
{
    frontend_q3_client *n = context;
    return client_download_current(n, error) &&
        frontend_q3_content_download_reference(n->q3_client_content, remote, checksum, error);
}
static bool client_download_nonce(void *context, uint64_t *out, qa_error *error)
{
    frontend_q3_client *n = context;
    if (!out || !client_download_current(n, error) || n->network->nonce == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 native stage namespace is exhausted");
    *out = ++n->network->nonce; return true;
}
static bool client_download_reliable(void *context, const char *text, qa_error *error)
{
    frontend_q3_client *n = context;
    return client_download_current(n, error) && qa_network_q3_client_command(n->runtime, n->q3_client, text, error);
}
static bool client_download_send(void *context, qa_error *error)
{
    frontend_q3_client *n = context;
    return client_download_current(n, error) && qa_network_q3_client_send(n->runtime, n->q3_client,
        (int32_t)((n->frontend->wall_time_ns / UINT64_C(1000000)) & INT32_MAX), error);
}
static void client_download_progress(void *context, const char *name, int32_t count, int32_t size)
{
    frontend_q3_client *n = context;
    if (count == 0 || count == size) {
        char text[256]; snprintf(text, sizeof(text), "Downloading %s: %" PRId32 "/%" PRId32 " bytes\n", name, count, size);
        frontend_print(n->frontend, text);
    }
}
static void client_download_failure(void *context,const qa_error *failure)
{
    frontend_q3_client *n=context;
    char text[640]; snprintf(text,sizeof(text),"Q3 package operation retained for retry: %s\n",failure->message);
    qa_application_q3_client_context receiver;
    if(qa_application_q3_remote_context_read(n->frontend->application,n->q3_cgame_owner,
        n->q3_client_launch_seat,&receiver,NULL))
        qa_console_emit(receiver.console,&receiver.command_context,text);
}
static bool client_download_reload(void *context, qa_error *error)
{
    frontend_q3_client *n = context; frontend_q3_content_view view;
    if (!client_download_current(n, error) || !qa_network_callbacks_idle(n->runtime) ||
        !frontend_q3_content_read(n->q3_client_content, &view, error)) return false;
    frontend_q3_content *fresh = NULL;
    if (!client_content_open(n, view.gamestate, view.catalog, &fresh, error)) return false;
    frontend_q3_content_destroy(n->q3_client_content); n->q3_client_content = fresh;
    return true;
}
static bool client_download_stage(void *context, const char *path, uint64_t logical_nonce,
    qa_bytes prefix, qa_fs_stage **out, uint64_t *native_nonce, qa_error *error)
{
    frontend_q3_client *n = context; frontend_q3_content_view view;
    if (!out || *out || !native_nonce || *native_nonce || !logical_nonce ||
        !client_download_current(n, error) || !frontend_q3_content_read(n->q3_client_content, &view, error)) return false;
    qa_fs_root *root = qa_catalog_q3_download_root(view.catalog);
    if (!n->network->preparation_nonce) n->network->preparation_nonce = n->network->nonce;
    if (!root || n->network->preparation_nonce == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Restored remote Q3 stage lacks its native root and preparation namespace");
    qa_fs_stage *stage = NULL; uint64_t initial; size_t written;
    if (!qa_fs_stage_open_unique_checked(root,path,&n->network->preparation_nonce,logical_nonce,&stage,&initial,error)) {
        qa_fs_stage_close(stage,false); return false;
    }
    bool ok = !initial && qa_fs_stage_write(stage, 0, prefix, &written, error) && written == prefix.size;
    if (!ok) { qa_fs_stage_close(stage, false); return false; }
    *native_nonce = n->network->preparation_nonce; *out = stage; return true;
}
static bool client_download_bindings(frontend_q3_client *n, qa_q3_client_download_bindings *out, qa_error *error)
{
    frontend_q3_content_view view;
    if (!out || !client_download_current(n, error) || !frontend_q3_content_read(n->q3_client_content, &view, error)) return false;
    qa_fs_root *root = qa_catalog_q3_download_root(view.catalog);
    if (!root) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Remote Q3 lacks its configured native family download root");
    *out = (qa_q3_client_download_bindings){.context = n, .root = root,
        .current = client_download_current, .permission = client_download_permission,
        .destination = client_download_destination, .reference = client_download_reference,
        .nonce = client_download_nonce, .reliable = client_download_reliable, .send_packet = client_download_send,
        .reload_packages = client_download_reload, .progress = client_download_progress,
        .failure=client_download_failure,.prepare_stage = client_download_stage};
    return true;
}
static bool client_init_current(void *context, uint64_t epoch, int32_t message,
    int32_t executed, int32_t client, qa_error *error)
{
    frontend_q3_client *n = context;
    qa_network_q3_client_init retained = {message, executed, client};
    return client_connection_current(n, epoch, error) &&
        (qa_network_q3_client_init_current(n->runtime, n->q3_client, &retained) ||
         frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 Init lost its actual decoded connection tuple"));
}
static bool client_role_receipt_current(void *context,
    const frontend_q3_content_role_receipt *receipt, qa_error *error)
{
    frontend_q3_client *n = context;
    if (!receipt || receipt->media_view_count != 1 || !receipt->media_views ||
        !client_connection_current(n, receipt->connection_epoch, error)) return false;
    qa_application_q3_role_receipt actual = {.role = receipt->role, .receiver = receipt->receiver,
        .seat = receipt->seat, .service_owner = receipt->service_owner,
        .configuration_generation = receipt->configuration_generation,
        .connection_epoch = receipt->connection_epoch, .descriptor = receipt->descriptor,
        .artifact = receipt->artifact, .acquisition = receipt->acquisition, .artifact_view = receipt->artifact_view};
    return (qa_application_q3_role_receipt_current(n->frontend->application, &actual) &&
        frontend_source_role_media_current(n->frontend, receipt->receiver, receipt->role,
            receipt->seat, receipt->service_owner, receipt->media_views[0])) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 media receipt lost its initialized role and actual presentation view");
}
static bool client_role_receipt(frontend_q3_client *n, qa_qvm_role role,
    frontend_q3_content_role_receipt *out, qa_vfs **media, qa_error *error)
{
    qa_application_q3_role_receipt actual;
    if (!out || !media || !qa_application_q3_role_receipt_read(n->frontend->application,
        n->q3_cgame_owner, role, n->q3_client_launch_seat, &actual, error) ||
        !frontend_source_role_media_read(n->frontend, actual.receiver, actual.role,
            actual.seat, actual.service_owner, media))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 media completion lacks its actual initialized source view");
    *out = (frontend_q3_content_role_receipt){.role = actual.role, .receiver = actual.receiver,
        .seat = actual.seat, .service_owner = actual.service_owner,
        .configuration_generation = actual.configuration_generation,
        .connection_epoch = actual.connection_epoch, .descriptor = actual.descriptor,
        .artifact = actual.artifact, .acquisition = actual.acquisition, .artifact_view = actual.artifact_view,
        .media_views = media, .media_view_count = 1, .producer = n, .current = client_role_receipt_current};
    return true;
}
static bool client_native_media_read(void *context, frontend_q3_content_native_receipt *cgame,
    frontend_q3_content_role_receipt *ui, qa_error *error)
{
    frontend_q3_client *n = context;
    return n && n->q3_session && frontend_remote_q3_session_native_media_read(n->q3_session,cgame,ui,error);
}
static bool client_modules_media_read(void *context, const application_native_q3_client_modules **modules,
    frontend_q3_content_role_receipt *cgame, frontend_q3_content_role_receipt *ui, qa_error *error)
{
    frontend_q3_client *n = context;
    return n && n->q3_session && frontend_remote_q3_session_modules_media_read(n->q3_session,modules,cgame,ui,error);
}
static bool client_native_retire(void *context, const qa_application_q3_remote_source *previous, qa_error *error)
{
    frontend_q3_client *n=context;
    const qa_application_q3_remote_source *held=n?&n->q3_session_source:NULL;
    if(!n || !previous || !previous->receiver.native_source ||
        ((n->q3_session || n->q3_initial || n->q3_initial_modules) &&
         (!held->descriptor || held->descriptor->storage!=previous->descriptor->storage ||
          held->configuration_generation!=previous->configuration_generation ||
          held->connection_epoch!=previous->connection_epoch ||
          held->receiver.receiver!=previous->receiver.receiver || held->receiver.seat!=previous->receiver.seat ||
          held->receiver.service_owner!=previous->receiver.service_owner ||
          held->receiver.console!=previous->receiver.console || held->receiver.cvars!=previous->receiver.cvars)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native retirement lost its actual retained session namespace");
    if(!frontend_remote_q3_session_retire_initial(&n->q3_initial,&n->q3_initial_modules,error) ||
        !frontend_remote_q3_resources_destroy(&n->q3_session,error)) return false;
    n->q3_session_source=(qa_application_q3_remote_source){0};
    return true;
}
static bool client_native_prepared(void *context,
    const qa_application_native_q3_remote_content_admission *request, qa_error *error)
{
    frontend_q3_client *n=context; frontend_q3_content_view content;
    const qa_q3_client_peer *peer=n?q3_view(n):NULL;
    return request && n && peer && !n->q3_session && !n->q3_initial && !n->q3_initial_modules &&
        !n->q3_session_source.descriptor && n->q3_client_decoded &&
        request->connection_epoch==n->q3_client_epoch && request->previous.receiver.receiver==n->q3_cgame_owner &&
        request->previous.receiver.seat==n->q3_client_launch_seat &&
        client_connection_current(n,request->connection_epoch,error) &&
        frontend_q3_content_state(n->q3_client_content)==FRONTEND_Q3_CONTENT_PREPARED &&
        frontend_q3_content_read(n->q3_client_content,&content,error) && content.map &&
        content.catalog==request->catalog && content.selected==request->product && content.mounts==request->prepared_mounts &&
        content.gamestate && content.gamestate->client_number==qa_q3_client_peer_gamestate(peer)->client_number &&
        content.checksum_feed==(uint32_t)qa_q3_client_peer_gamestate(peer)->checksum_feed;
}
static bool client_clear(void *context, qa_error *error)
{
    frontend_q3_client *n = context; qa_application_q3_remote_source source, cleared;
    if (n->q3_client_restart_generation == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 source restart generation is exhausted");
    if (!qa_network_callbacks_idle(n->runtime) ||
        !qa_application_q3_remote_source_read(n->frontend->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, n->q3_client_rebind ? n->q3_client_previous_epoch : n->q3_client_epoch,
            &source, error) ||
        !qa_application_network_q3_client_unproject(n->frontend->application, &n->q3_projection, error)) return false;
    if(source.receiver.native_source) {
        qa_native_q3_remote_retirement retirement={source,n,client_native_retire};
        if(!qa_native_q3_remote_client_clear(n->frontend->application,&retirement,&cleared,error)) return false;
    } else if(!qa_application_q3_remote_clear(n->frontend->application,&source,&cleared,error)) return false;
    ++n->q3_client_restart_generation;
    qa_q3_prediction_scene_clear(n->q3_prediction_scene);
    frontend_remote_input_clear(n->q3_input);
    if(n->q3_predictor) frontend_remote_prediction_clear(frontend_network_predictor_read(n->q3_predictor));
    n->q3_predictor_zero_pending=false; n->q3_predictor_zero_sequence=0;
    n->q3_scene_frame = 0; n->q3_scene_frame_valid = false;
    qa_q3_client_downloads_destroy(n->q3_client_downloads); n->q3_client_downloads = NULL;
    frontend_q3_content_destroy(n->q3_client_content); n->q3_client_content = NULL;
    n->q3_client_decoded = false;
    n->q3_ui_client_number = 0;
    n->q3_client_gamestate = false; n->q3_client_active = false;
    n->q3_client_entered = false;
    qa_q3_clock_clear(&n->q3_client_clock);
    n->q3_client_time = n->q3_client_clock.time;
    n->q3_previous_presentation_time = 0;
    n->q3_initial_message = 0; n->q3_initial_command = 0; n->q3_initial_tuple = false;
    n->q3_reached_command = (qa_q3_tokens){0}; n->q3_reached_command_sequence = 0;
    n->q3_reliable_receipt = false; n->q3_reliable_receipt_sequence = 0;
    n->q3_command_present = false;
    return true;
}
static bool client_system_info(void *context, const char *info, qa_error *error)
{
    frontend_q3_client *n = context;
    qa_application_q3_client_context role;
    if (!frontend_network_q3_client_context_read(n->frontend, n->q3_cgame_owner, n->q3_client_launch_seat, &role, error)) return false;
    return role.native_source ? qa_application_network_q3_client_native_system_info(n->frontend->application,
        role.receiver, role.seat, info, error) : frontend_source_system_info(n->frontend, &role, info, error);
}
static bool client_native_gamestate(frontend_q3_client *n, const qa_q3_gamestate *state,
    const frontend_q3_content_view *content, const qa_application_q3_remote_source *previous, qa_error *error)
{
    qa_application_native_q3_remote_content_admission admission={.previous=*previous,
        .catalog=content->catalog,.product=content->selected,.prepared_mounts=content->mounts,
        .connection_epoch=n->q3_client_epoch,.producer=n,.prepared_current=client_native_prepared};
    qa_application_q3_remote_source published; qa_network_q3_client_init counters;
    if(!qa_application_native_q3_remote_content_admit(n->frontend->application,&admission,&published,error)) return false;
    client_cvars_bind(n, published.receiver.cvars);
    frontend_q3_content_publication publication={published.descriptor,published.receiver,
        published.configuration_generation,published.connection_epoch};
    if(!frontend_q3_content_publish(n->q3_client_content,&publication,error) ||
        !qa_network_q3_client_init_read(n->runtime,n->q3_client,&counters,error)) return false;
    n->q3_initial_message=counters.server_message;
    n->q3_initial_command=counters.last_executed_server_command;
    n->q3_initial_tuple=true; n->q3_client_initializing=true;
    qa_application_q3_remote_init init={.source=published,.server_message=counters.server_message,
        .last_executed_server_command=counters.last_executed_server_command,.client_number=counters.client_number,
        .connection=n,.current=client_init_current};
    frontend_network_client_domain domain;
    n->q3_session_source=published;
    qa_application_q3_client_context receiver;
    bool ok=qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner,
        n->q3_client_launch_seat, &receiver, error) &&
        frontend_network_client_domain_read(n->frontend,&receiver,&domain,error) &&
        domain.gamestate==state && frontend_remote_q3_session_create(n->frontend,&domain,&init,&n->q3_session,error);
    frontend_remote_q3_session_view completed;
    if(ok) ok=frontend_remote_q3_session_read(n->q3_session,&completed,error);
    frontend_q3_content_role_receipt cgame,ui;
    if(ok && completed.pure) {
        const application_native_q3_client_modules *modules=NULL;
        ok=client_modules_media_read(n,&modules,&cgame,&ui,error) &&
            frontend_q3_content_modules_media_ready(n->q3_client_content,modules,&cgame,&ui,error);
    } else if(ok) {
        frontend_q3_content_native_receipt native;
        ok=client_native_media_read(n,&native,&ui,error) &&
            frontend_q3_content_native_media_ready(n->q3_client_content,&native,&ui,error);
    }
    char command[QA_Q3_COMMAND_CHARS];
    if(ok) ok=frontend_q3_content_media_current(n->q3_client_content,error) &&
        frontend_q3_content_pure_command(n->q3_client_content,command,sizeof(command),error) &&
        qa_network_q3_client_command(n->runtime,n->q3_client,command,error) &&
        qa_network_phase(n->runtime,n->q3_client,QA_NET_PRIMED,error);
    if(ok) n->q3_client_gamestate=true;
    n->q3_client_initializing=false; return ok;
}
static bool client_gamestate(void *context, const qa_q3_gamestate *state, qa_error *error)
{
    frontend_q3_client *n = context;
    if (!state || state->client_number < 0 || state->client_number >= 64 ||
        !qa_network_callbacks_idle(n->runtime) || n->q3_client_content || n->q3_client_downloads)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 gamestate requires its cleared idle decoder boundary");
    n->q3_client_decoded = true;
    n->q3_ui_client_number = state->client_number;
    if (!client_content_open(n, state, NULL, &n->q3_client_content, error)) return false;
    frontend_q3_content_view content; qa_q3_client_download_bindings bindings; bool downloading;
    if (!frontend_q3_content_read(n->q3_client_content, &content, error) ||
        !client_download_bindings(n, &bindings, error) ||
        !qa_q3_client_downloads_create(&bindings, &n->q3_client_downloads, error) ||
        !qa_q3_client_downloads_begin(n->q3_client_downloads, content.referenced, content.referenced_count,
            content.loaded_checksums, content.loaded_count, &downloading, error)) return false;
    /* A completed download asks the actual server for a new gamestate after
     * native filesystem refresh. It does not initialize this old map cut. */
    if (downloading) return true;
    qa_application_q3_remote_source previous, published;
    qa_application_q3_remote_recipe recipe;
    if (!frontend_q3_content_prepare(n->q3_client_content, error) ||
        !frontend_q3_content_read(n->q3_client_content, &content, error) ||
        !qa_application_q3_remote_source_read(n->frontend->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, n->q3_client_epoch, &previous, error)) return false;
    if(previous.receiver.native_source) return client_native_gamestate(n,state,&content,&previous,error);
    if(!qa_application_q3_remote_recipe_read(n->frontend->application,&previous,state,&recipe,error)) return false;
    qa_application_q3_remote_replacement replacement = {.previous = previous,
        .catalog = content.catalog, .product = content.selected, .prepared_mounts = content.mounts,
        .cgame_path = recipe.cgame_path, .ui_path = recipe.replace_ui ? recipe.ui_path : NULL,
        .cgame_runtime = recipe.cgame_runtime,
        .connection_epoch = n->q3_client_epoch};
    n->q3_client_initializing = true;
    bool ok = qa_application_q3_remote_replace(n->frontend->application, &replacement, &published, error);
    if (ok) {
        client_cvars_bind(n, published.receiver.cvars);
        frontend_q3_content_publication publication = {.descriptor = published.descriptor,
            .receiver = published.receiver, .configuration_generation = published.configuration_generation,
            .connection_epoch = published.connection_epoch};
        ok = frontend_q3_content_publish(n->q3_client_content, &publication, error);
    }
    qa_network_q3_client_init counters;
    if (ok) ok = qa_network_q3_client_init_read(n->runtime, n->q3_client, &counters, error);
    if (ok) {
        qa_application_q3_remote_init init = {.source = published, .server_message = counters.server_message,
            .last_executed_server_command = counters.last_executed_server_command, .client_number = counters.client_number,
            .connection = n, .current = client_init_current};
        n->q3_initial_message = counters.server_message;
        n->q3_initial_command = counters.last_executed_server_command;
        n->q3_initial_tuple = true;
        ok = qa_application_q3_remote_initialize(n->frontend->application, &init, error);
    }
    if (!ok) { n->q3_client_initializing = false; return false; }
    qa_vfs *cgame_media = NULL, *ui_media = NULL;
    frontend_q3_content_role_receipt cgame, ui;
    char pure_command[QA_Q3_COMMAND_CHARS];
    ok = client_role_receipt(n, QA_QVM_CGAME, &cgame, &cgame_media, error) &&
        client_role_receipt(n, QA_QVM_UI, &ui, &ui_media, error) &&
        frontend_q3_content_media_ready(n->q3_client_content, &cgame, &ui, error) &&
        frontend_q3_content_pure_command(n->q3_client_content, pure_command, sizeof(pure_command), error) &&
        qa_network_q3_client_command(n->runtime, n->q3_client, pure_command, error) &&
        qa_network_phase(n->runtime, n->q3_client, QA_NET_PRIMED, error);
    if (ok) n->q3_client_gamestate = true;
    n->q3_client_initializing = false; return ok;
}
static bool client_snapshot(void *context, const qa_q3_snapshot *snapshot, int32_t ping, qa_error *error)
{
    frontend_q3_client *n = context; (void)ping; (void)error;
    qa_q3_clock_publish(&n->q3_client_clock, snapshot); return true;
}
static bool client_download_size(void *context, int32_t size, int32_t *effective, qa_error *error)
{
    frontend_q3_client *n = context;
    return qa_q3_client_downloads_size(n->q3_client_downloads, size, effective, error);
}
static bool client_download(void *context, const qa_q3_download *download, qa_error *error)
{
    frontend_q3_client *n = context;
    return qa_q3_client_downloads_receive(n->q3_client_downloads, download, error);
}
static bool client_source_command(void *context, int32_t sequence, const qa_q3_tokens *tokens, qa_error *error)
{
    frontend_q3_client *n = context;
    if (!qa_application_network_q3_client_command(n->frontend->application, n->q3_cgame_owner, n->q3_client_launch_seat, tokens, error)) return false;
    n->q3_reached_command = (qa_q3_tokens){.count = tokens->count};
    size_t used = 0;
    for (size_t i = 0; i < tokens->count; ++i) {
        const char *value = qa_q3_token(tokens, i); size_t size = strlen(value) + 1;
        n->q3_reached_command.offsets[i] = (uint16_t)used;
        memcpy(n->q3_reached_command.text + used, value, size); used += size;
    }
    n->q3_reached_command_sequence = sequence;
    n->q3_command_present = true; return true;
}
static bool client_map_restart(void *context, qa_error *error)
{
    frontend_q3_client *n = context;
    /* The source can execute this command during DrawActiveFrame. Keep the
     * current source/audio identities until safe snapshot-epoch publication. */
    if (!client_connection_current(n, n->q3_client_epoch, error)) return false;
    if (n->q3_client_restart_generation == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 source restart generation is exhausted");
    if (!qa_q3_prediction_scene_restart(n->q3_prediction_scene, error)) return false;
    frontend_remote_input_clear(n->q3_input);
    if(n->q3_predictor) frontend_remote_prediction_clear(frontend_network_predictor_read(n->q3_predictor));
    if(n->q3_predictor) {
        const qa_q3_client_peer *peer=q3_view(n);
        uint64_t number=peer?qa_q3_client_peer_usercmd_number(peer):0;
        const qa_q3_usercmd *zero=peer?qa_q3_client_peer_usercmd_at(peer,number):NULL;
        if(!zero || zero->serverTime || zero->buttons || zero->weapon || zero->forwardmove ||
            zero->rightmove || zero->upmove || zero->angles[0] || zero->angles[1] || zero->angles[2])
            return frontend_fail(error,QA_ERROR_FORMAT,"Map restart lost the actual cleared native command ring head");
        n->q3_predictor_zero_sequence=number; n->q3_predictor_zero_pending=number!=0;
    }
    ++n->q3_client_restart_generation; return true;
}
static bool client_disconnect(void *context, const char *reason, qa_error *error)
{
    frontend_q3_client *n = context;
    if (!n->q3_client_attached || !qa_network_q3_client_retire(n->runtime, n->q3_client, error)) return false;
    n->q3_client_retiring = true;
    snprintf(n->q3_client_reason, sizeof(n->q3_client_reason), "%s", reason); return true;
}
bool frontend_network_q3_client_effect(qa_frontend *f,
    const qa_application_q3_client_context *context, qa_application_q3_client_effect effect,
    const char *text, qa_error *error)
{
    frontend_q3_client *n = q3_client_receiver(f, context);
    if (!n || !context || !n->q3_client_requested || !n->q3_client_attached ||
        context->receiver != n->q3_cgame_owner || context->seat != n->q3_client_launch_seat ||
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
{ frontend_q3_client *n = context; return n->runtime != n->network->runtime; }
static bool client_close_attempt(frontend_q3_client *n, qa_error *error)
{
    if (n->q3_client_closed) return true;
    if (n->q3_client_attached && !n->q3_client_retiring &&
        !qa_network_q3_client_disconnect(n->runtime, n->q3_client,
            (int32_t)((n->frontend->wall_time_ns / UINT64_C(1000000)) & INT32_MAX), error)) return false;
    if (!client_clear(n, error)) return false;
    if (n->q3_client_attached && !qa_network_detach(n->runtime, n->q3_client, "disconnected", error)) return false;
    n->q3_client_attach = false; n->q3_client_admission.phase = QA_Q3_DISCONNECTED;
    n->q3_client_closed = true; n->q3_client_retiring = true; return true;
}
static bool client_binding_current(void *context, uint64_t old_epoch, uint64_t new_epoch, qa_error *error)
{
    frontend_q3_client *n = context;
    if (!n || !n->q3_client_rebind || n->network->busy || !qa_network_callbacks_idle(n->runtime) ||
        !old_epoch || old_epoch != n->q3_client_previous_epoch || new_epoch != n->q3_client_epoch ||
        new_epoch <= old_epoch || n->q3_client_attach || n->q3_client_closed || n->q3_client_decoded ||
        n->q3_client_generation != qa_application_configuration_generation(n->frontend->application) ||
        (n->q3_client_previous.generation &&
            (qa_net_client_id_equal(n->q3_client_previous, n->q3_client) ||
             qa_net_connections_get(qa_network_connections(n->runtime), n->q3_client_previous))))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote reconnect lost its actual old retirement and fresh attempt");
    return client_connection_current(n, new_epoch, error);
}
static bool client_bind_attempt(frontend_q3_client *n, qa_error *error)
{
    if (!n->q3_client_rebind) return true;
    qa_application_q3_remote_binding request = {.new_epoch = n->q3_client_epoch,
        .connection = n, .current = client_binding_current};
    qa_application_q3_remote_source rebound;
    if (!qa_application_q3_remote_source_read(n->frontend->application, n->q3_cgame_owner,
        n->q3_client_launch_seat, n->q3_client_previous_epoch, &request.previous, error)) return false;
    bool ok=request.previous.receiver.native_source ?
        qa_native_q3_remote_client_rebind(n->frontend->application,&request,&rebound,error) :
        qa_application_q3_remote_rebind(n->frontend->application,&request,&rebound,error);
    if(!ok) return false;
    client_cvars_bind(n, rebound.receiver.cvars);
    n->q3_client_rebind = false;
    return true;
}
static bool client_transport_family(qa_frontend_network *n,const qa_net_address *address,qa_error *error)
{
    const qa_net_address *local=qa_network_local_address(n->runtime);
    if ((address->kind!=QA_NET_IPV4 && address->kind!=QA_NET_IPV6) || !local)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote connection requires its actual UDP address family");
    if (address->kind==local->kind) return true;
    qa_net_udp_options udp={.bind={.kind=address->kind},.limits={65507,256},
        .broadcast=address->kind==QA_NET_IPV4,.ipv6_only=false};
    qa_net_transport *transport=NULL; qa_network_runtime *replacement=NULL;
    qa_network_options options={.owner=NETWORK_OWNER,.clients=64,.packets_per_pump=256,
        .timeout_ns=UINT64_C(120000000000),.hooks={.context=n,.admit=admit,.controlled=controlled,
        .command=remote_command,.disconnected=disconnected,.connectionless=connectionless,
        .reconnect=reconnect,.q3_source_command=remote_q3_command,.nq_source_command=remote_nq_command,
        .commands=remote_qw_commands}};
    if (!qa_net_udp_open(&udp,&transport,error)) return false;
    if (!qa_network_create(transport,&options,&replacement,error)) { qa_net_transport_close(transport); return false; }
    qa_network_transport_exchange(n->runtime,replacement); qa_network_destroy(replacement);
    return true;
}
static bool q1_client_construct(qa_frontend_network *n,const qa_net_address *remote,
    qa_net_protocol_id protocol,uint32_t physical,bool demo,qa_error *error)
{
    qa_frontend *f=n->frontend;
    if (f->network!=n || n->q1_client_owner ||
        !q1_client_protocol(protocol) || f->options.dedicated || physical>=f->options.seats ||
        n->busy || n->detached_transport || !qa_network_callbacks_idle(n->runtime))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 connection requires its returned selected CLIENT constructor");
    frontend_network_q1_client_options client={.frontend=f,.runtime=n->runtime,.remote=*remote,
        .protocol=protocol,.physical_seat=physical,.qport=(uint16_t)n->rotation_random,.demo_playback=demo,
        .context=n,.current=q1_client_current,.download_nonce=q2_download_nonce,.downloads=q1_downloads,.service=q1_service};
    client.policy=q1_policy(client.protocol);
    client.selected=frontend_product_current(f)->id;
    if (!frontend_config_store_client_profile(f->config_store,frontend_product_current(f)->id,&client.profile,error) ||
        !frontend_config_store_neutral_pending_options(f->config_store,physical,&client.configuration,error)) return false;
    if (frontend_network_q1_client_create(&client,&n->q1_client_owner,error)) return true;
    if (!n->q1_client_owner)
        (void)frontend_config_store_neutral_options_cancel(f->config_store,&client.configuration,NULL);
    return false;
}
static bool q1_client_create(qa_frontend_network *n,const qa_net_address *remote,qa_error *error)
{
    qa_frontend *f=n->frontend;
    if(!f->options.network_connect||f->options.seats!=1)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 connection requires its actual selected remote request");
    return q1_client_construct(n,remote,f->options.network_protocol,0,false,error);
}
static bool client_attempts_drain(qa_frontend_network *n, qa_error *error)
{
    if (n->q3_attempts && !n->q3_clients[0].q3_client_requested) {
        qa_frontend *f=n->frontend;
        if (f->stepping || frontend_config_store_client_preparation(f->config_store)) return true;
        if (!frontend_network_client_only(f) || !q1_client_protocol(f->options.network_protocol) ||
            f->options.dedicated || f->options.seats!=1 || n->busy || n->detached_transport ||
            f->capture || f->resource_inventory || f->source_restoring || f->preparing ||
            !qa_network_callbacks_idle(n->runtime) || !frontend_network_q1_client_idle(n->q1_client_owner))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Queued connection lost its returned Q1 CLIENT constructor");
        while (n->q3_attempts) {
            if (frontend_config_store_client_preparation(f->config_store)) return true;
            frontend_q3_attempt *request=n->q3_attempts;
            if (request->disconnect ? *request->server!=0 : *request->server==0)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 CLIENT connection changed its retained request");
            if (n->q1_client_owner) {
                frontend_network_q1_client_view view;
                if (!frontend_network_q1_client_metadata_read(n->q1_client_owner,&view,error) ||
                    view.physical.source.configuration_generation!=qa_application_configuration_generation(f->application) ||
                    (view.retired ? !qa_application_client_retirement_current(f->application,&view.physical.source) :
                        !frontend_client_source_current(&view.physical)))
                    return frontend_fail(error,QA_ERROR_ARGUMENT,"Queued connection lost its actual Q1 CLIENT namespace");
                if (f->archive_enabled && view.configured && !view.retired &&
                    !frontend_config_store_save(f->config_store,error)) return false;
                if (!frontend_network_q1_client_disconnect(n->q1_client_owner,"disconnected",error)) return false;
                if (!request->disconnect) {
                    qa_error retirement={0};
                    if (!frontend_network_q1_client_destroy(&n->q1_client_owner,&retirement)) {
                        if (retirement.code==QA_OK) return true;
                        if (error) *error=retirement;
                        return false;
                    }
                }
            }
            if (!request->disconnect) {
                qa_net_address address;
                if (!qa_net_address_resolve(request->server,f->options.network_port,0,&address,error) ||
                    !client_transport_family(n,&address,error) || !q1_client_create(n,&address,error)) return false;
                memcpy(n->client_server,request->server,sizeof(n->client_server));
            }
            n->q3_attempts=request->next; --n->q3_attempt_count;
            if (!n->q3_attempts) n->q3_attempt_tail=NULL;
            free(request);
        }
        return true;
    }
    if (n->q3_attempts && n->q3_clients[0].q3_client_generation != qa_application_configuration_generation(n->frontend->application))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Queued connection command lost its retained source launch lifetime");
    while (n->q3_attempts) {
        if (!n->q3_attempts->disconnect && n->q3_clients[0].q3_client_epoch == UINT64_MAX)
            return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 connection attempt generation is exhausted");
        if (!client_close_attempt(&n->q3_clients[0], error)) return false;
        frontend_q3_attempt request = *n->q3_attempts;
        frontend_q3_attempt *entered = n->q3_attempts;
        n->q3_attempts = entered->next; --n->q3_attempt_count;
        if (!n->q3_attempts) n->q3_attempt_tail = NULL;
        free(entered);
        if (request.disconnect) continue;
        memcpy(n->client_server, request.server, sizeof(n->client_server));
        memset(n->q3_clients[0].q3_client_message, 0, sizeof(n->q3_clients[0].q3_client_message)); n->q3_clients[0].q3_ui_client_number = 0;
        qa_net_address address;
        if (!qa_net_address_resolve(request.server, n->frontend->options.network_port, 0, &address, error)) return false;
        if (!client_transport_family(n,&address,error)) return false;
        if (!n->q3_clients[0].q3_client_rebind) n->q3_clients[0].q3_client_previous_epoch = n->q3_clients[0].q3_client_epoch;
        if (n->q3_clients[0].q3_client.generation) n->q3_clients[0].q3_client_previous = n->q3_clients[0].q3_client;
        ++n->q3_clients[0].q3_client_epoch; n->q3_clients[0].q3_client = (qa_net_client_id){0};
        n->q3_clients[0].q3_client_rebind = true; n->q3_clients[0].q3_client_closed = false; n->q3_clients[0].q3_client_retiring = false;
        n->q3_clients[0].q3_client_reason[0] = 0; n->q3_clients[0].q3_client_userinfo[0] = 0;
        uint16_t qport = n->q3_clients[0].q3_client_admission.qport;
        qa_q3_client_admission_begin(&n->q3_clients[0].q3_client_admission, &address, qport);
    }
    return true;
}
bool frontend_network_client_attempts_advance(qa_frontend *f,qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    if (!n || n->q3_clients[0].q3_client_requested || !n->q3_attempts) return true;
    if (!f || f->stepping || f->preparing || f->round || f->capture || f->resource_inventory || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT commands require their returned frontend phase");
    return n->frontend==f && client_attempts_drain(n,error);
}
static const qa_q3_snapshot *client_scene_snapshot(void *context, int32_t number)
{ return qa_q3_client_peer_presentation_snapshot_at(context, number); }
static bool client_project(frontend_q3_client *n, qa_error *error)
{
    if (!n->q3_client_active) return true;
    /* Pump and publish both drain this same physical frame. Snapshot seed
     * selection precedes prediction once; later teleport consumption must
     * not cause a second selection before packet-pose publication. */
    if (n->q3_scene_frame_valid && n->q3_scene_frame == n->frontend->frame_number) return true;
    const qa_q3_client_peer *peer = q3_view(n);
    const qa_q3_snapshot *latest = peer ? qa_q3_client_peer_snapshot(peer) : NULL;
    if (!latest) return true;
    if (!qa_q3_prediction_scene_process(n->q3_prediction_scene, latest->message_number,
        client_scene_snapshot, (void *)peer, n->q3_client_time, error)) return false;
    qa_q3_prediction_scene_view scene;
    if (!qa_q3_prediction_scene_read(n->q3_prediction_scene, &scene)) return true;
    const qa_q3_snapshot *current = scene.snapshot, *next = scene.next_snapshot;
    uint8_t epoch = current->flags & 4;
    if (n->q3_projection.owner && n->q3_projection_epoch != epoch &&
        !qa_application_network_q3_client_unproject(n->frontend->application, &n->q3_projection, error)) return false;
    /* A future server-count change must not reuse the current epoch's IDs. */
    if (scene.next_frame_teleport) next = NULL;
    if (!qa_application_network_q3_client_project(n->frontend->application,
        n->q3_cgame_owner, n->q3_client_launch_seat, &n->q3_projection,
        current, next, n->q3_prediction_scene, &scene, error)) return false;
    n->q3_projection_epoch = epoch;
    n->q3_scene_frame = n->frontend->frame_number; n->q3_scene_frame_valid = true; return true;
}
static bool client_initial_session(frontend_q3_client *n, qa_error *error)
{
    if(n->q3_session || n->q3_client_decoded) return true;
    if(n->q3_initial || n->q3_initial_modules) {
        frontend_remote_q3_initial_view initial; frontend_remote_q3_module_media ui;
        return frontend_remote_q3_initial_read(n->q3_initial,&initial,error) &&
            frontend_remote_q3_modules_media_read(n->q3_initial_modules,QA_QVM_UI,&ui,error) &&
            frontend_remote_q3_modules_media_current(&ui) && frontend_remote_q3_initial_current(&initial);
    }
    frontend_network_client_attempt attempt; bool present=false;
    qa_application_q3_client_context receiver;
    if (!qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner,
        n->q3_client_launch_seat, &receiver, error) ||
        !frontend_network_client_attempt_read(n->frontend,&receiver,&attempt,&present,error)) return false;
    if(!present || !attempt.source.receiver.native_source) return true;
    n->q3_session_source=attempt.source;
    return frontend_remote_q3_session_create_initial(n->frontend,&attempt,
        &n->q3_initial,&n->q3_initial_modules,error);
}
static bool client_drain(frontend_q3_client *n, bool presentation_frame, qa_error *error)
{
    if (!n->q3_client_requested) return true;
    if (!client_attempts_drain(n->network, error)) return false;
    if (n->q3_client_retiring) {
        if (!n->q3_client_closed) {
            if (!client_close_attempt(n, error)) return false;
            frontend_print(n->frontend, n->q3_client_reason);
        }
        return true;
    }
    if (n->q3_client_generation != qa_application_configuration_generation(n->frontend->application))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Remote Q3 launch changed and requires fresh connection admission");
    if (n->q3_client_attach) {
        qa_net_seat_binding seat = {n->seat, 0};
        qa_net_connect request = {.attachment = QA_NET_REMOTE, .endpoint = n->q3_client_admission.address,
            .protocol = {QA_NET_Q3_68, 0, 0}, .seats = &seat, .seat_count = 1, .composition = n->network->composition};
        qa_q3_client_hooks hooks = {.context = n, .generation = client_generation,
            .clear_active = client_clear, .gamestate = client_gamestate, .system_info = client_system_info,
            .snapshot = client_snapshot, .download_size = client_download_size, .download = client_download,
            .command = client_source_command, .map_restart = client_map_restart, .disconnect = client_disconnect,
            .level_shot = client_level_shot, .local_server_running = client_local_server, .send = client_send_address,
            .accepted_message=demo_q3_accepted,.defer_source = true};
        qa_network_q3_client_policy policy = {n, remote_client_settings};
        if (!qa_network_attach_q3_client(n->runtime, &request, n->q3_client_product,
            n->q3_client_admission.challenge, n->q3_client_admission.qport, &hooks,
            &policy, n->frontend->wall_time_ns, &n->q3_client, error)) return false;
        n->q3_client_attach = false; n->q3_client_attached = true;
    }
    if (!n->q3_client_attached) {
        if (qa_application_startup_pending(n->frontend->application)) return true;
        if (!client_authorization_prepare(n, (qa_bytes){0}, error) || !client_initial_session(n,error)) return false;
        qa_application_q3_client_context role;
        qa_buffer info = {0};
        if (!qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner, n->q3_client_launch_seat, &role, error) ||
            !qa_cvars_info(role.cvars, QA_CVAR_USERINFO, 1024, &info, error)) return false;
        bool ok = qa_q3_client_admission_resend(&n->q3_client_admission,
            (int64_t)(n->frontend->wall_time_ns / UINT64_C(1000000)), (const char *)info.data, client_admission_send, n, error);
        qa_buffer_free(&info); return ok;
    }
    if (!client_bind_attempt(n, error) || !client_initial_session(n,error)) return false;
    if (qa_network_q3_client_receive_pending(n->runtime, n->q3_client) &&
        !qa_network_q3_client_continue(n->runtime, n->q3_client, error)) return false;
    if(n->q3_client_downloads && !qa_q3_client_downloads_pump(n->q3_client_downloads,error)) return false;
    qa_application_q3_client_context role; qa_buffer info = {0};
    if (!qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner, n->q3_client_launch_seat, &role, error) ||
        !qa_cvars_info(role.cvars, QA_CVAR_USERINFO, sizeof(n->q3_client_userinfo), &info, error)) return false;
    bool updated = true;
    if (strcmp(n->q3_client_userinfo, (const char *)info.data)) {
        char command[sizeof(n->q3_client_userinfo) + 12];
        snprintf(command, sizeof(command), "userinfo \"%s\"", (const char *)info.data);
        updated = qa_network_q3_client_command(n->runtime, n->q3_client, command, error);
        if (updated) memcpy(n->q3_client_userinfo, info.data, info.size + 1);
    }
    qa_buffer_free(&info); if (!updated) return false;
    if (!n->q3_client_gamestate) return true;
    if (n->q3_client_gamestate && !n->q3_client_entered) {
        qa_q3_usercmd initial = {0};
        if (!qa_network_q3_client_usercmd(n->runtime, n->q3_client, &initial, error)) return false;
        if(n->q3_predictor) {
            const qa_q3_client_peer *peer=q3_view(n);
            uint64_t number=peer?qa_q3_client_peer_usercmd_number(peer):0;
            const qa_q3_usercmd *zero=peer?qa_q3_client_peer_usercmd_at(peer,number):NULL;
            if(number!=1 || !zero || zero->serverTime || zero->buttons || zero->weapon || zero->forwardmove ||
                zero->rightmove || zero->upmove || zero->angles[0] || zero->angles[1] || zero->angles[2])
                return frontend_fail(error,QA_ERROR_FORMAT,"Initial prediction lost the actual appended zero native command");
            n->q3_predictor_zero_sequence=number; n->q3_predictor_zero_pending=true;
        }
        n->q3_client_entered = true;
    }
    if (!presentation_frame) return true;
    if(n->network->demo_playback) return client_project(n,error);
    qa_q3_clock_options options = {.timescale = 1};
    const qa_cvar_view *nudge = qa_cvars_read(role.cvars, n->cl_timeNudge);
    const qa_cvar_view *scale = qa_cvars_read(role.cvars, n->frontend->engine_cvars.timescale);
    if (scale) options.timescale = scale->number;
    if (nudge) options.time_nudge = nudge->integer;
    if (!qa_q3_clock_advance(&n->q3_client_clock,
        (int32_t)((n->frontend->time_ns / UINT64_C(1000000)) & INT32_MAX), &options,
        &n->q3_client_active, &n->q3_client_time, error)) return false;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(n->runtime), n->q3_client);
    if (n->q3_client_active && (!n->q3_client_gamestate || !client ||
        (client->phase == QA_NET_PRIMED && !qa_network_phase(n->runtime, n->q3_client, QA_NET_ACTIVE, error)))) return false;
    return client_project(n, error);
}
static const qa_q3_gamestate *service_gamestate(void *context)
{
    frontend_q3_client *n = q3_service_client(context); const qa_q3_client_peer *p = q3_view(n);
    return p && n->q3_client_decoded &&
        (n->q3_client_gamestate || n->q3_client_initializing) ? qa_q3_client_peer_gamestate(p) : NULL;
}
static bool service_current_snapshot(void *context, int32_t *number, int32_t *time, qa_error *error)
{
    frontend_q3_client *n = q3_service_client(context); const qa_q3_client_peer *p = q3_view(n); (void)error;
    const qa_q3_snapshot *snapshot = p ? qa_q3_client_peer_snapshot(p) : NULL;
    *number = snapshot ? snapshot->message_number : 0; *time = snapshot ? snapshot->server_time : 0; return true;
}
static bool service_snapshot(void *context, int32_t number, const qa_q3_snapshot **out, int32_t *ping, qa_error *error)
{
    frontend_q3_client *n = q3_service_client(context); const qa_q3_client_peer *p = q3_view(n);
    const qa_q3_snapshot *latest = p ? qa_q3_client_peer_snapshot(p) : NULL;
    if (number > (latest ? latest->message_number : 0))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 snapshot request is in the future");
    *out = p ? qa_q3_client_peer_presentation_snapshot_at(p, number) : NULL;
    *ping = *out ? (*out)->player.ping : 0; return true;
}
static bool service_server_command(void *context, int32_t sequence, bool *present, qa_error *error)
{
    frontend_q3_client *n = q3_service_client(context);
    *present = false;
    if (!n || !n->q3_client_attached) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 command requested without an admitted client");
    n->q3_command_present = false;
    n->q3_reliable_receipt = false; n->q3_reliable_receipt_sequence = 0;
    if (!qa_network_q3_client_execute(n->runtime, n->q3_client, sequence, error)) return false;
    n->q3_reliable_receipt_sequence = sequence; n->q3_reliable_receipt = true;
    *present = n->q3_command_present; return true;
}
static int32_t service_current_command(void *context)
{
    const qa_q3_client_peer *p = q3_view(q3_service_client(context));
    uint32_t word = p ? (uint32_t)qa_q3_client_peer_usercmd_number(p) : 0;
    int32_t number; memcpy(&number,&word,sizeof(number)); return number;
}
static bool service_user_command(void *context, int32_t number, qa_q3_usercmd *out, bool *present, qa_error *error)
{
    const qa_q3_client_peer *p = q3_view(q3_service_client(context)); *present = false;
    if (!p) return true;
    if (number > service_current_command(context))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 usercmd requested in the future");
    const qa_q3_usercmd *command = qa_q3_client_peer_usercmd_signed_at(p, number);
    if (command) { *out = *command; *present = true; } return true;
}
static bool service_command_values(void *context, int32_t weapon, float sensitivity, qa_error *error)
{
    frontend_q3_client *n = q3_service_client(context);
    if (!n || weapon < 0 || weapon > UINT8_MAX || !isfinite(sensitivity))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid remote Q3 cgame input values");
    n->q3_weapon = weapon; n->q3_sensitivity = sensitivity; return true;
}
static bool service_source_actor(void *context, uint32_t source, qa_actor_id *out, bool *present, qa_error *error)
{
    frontend_q3_client *n = q3_service_client(context);
    if (!n) { *out = (qa_actor_id){0}; *present = false; return true; }
    return qa_application_network_q3_client_actor(n->frontend->application, &n->q3_projection,
        source, out, present, error);
}
bool frontend_network_client_actor(const qa_frontend *f, qa_actor_id actor)
{
    if (!f || !f->network || !actor.registry ||
        !qa_actors_get(qa_session_actors(qa_application_session(f->application)), actor)) return false;
    for (uint32_t physical = 0; physical < f->options.seats; ++physical) {
        const frontend_q3_client *n = f->network->q3_clients + physical;
        if (!n->q3_client_requested) continue;
        for (uint32_t i = 0; i < QA_Q3_ENTITY_NONE; ++i)
            if (qa_actor_id_equal(n->q3_projection.actors[i], actor)) return true;
    }
    return false;
}
static bool client_character_cvars(qa_application *application, qa_actor_owner owner,
    uint32_t seat, qa_cvars *cvars, qa_error *error)
{
    qa_application_character_declaration declaration; bool found;
    if (!qa_application_character_constructor_read(application, owner, seat, &declaration, &found, error)) return false;
    if (!found) return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 client lacks its actual selected CHARACTER declaration");
    const qa_native_q3_character_declaration *appearance = &declaration.appearance;
    const char *models[] = {appearance->model, *appearance->head_model ? appearance->head_model : appearance->model};
    const char *skins[] = {appearance->skin, appearance->head_skin};
    static const char *appearance_names[2][2] = {{"model", "team_model"}, {"headmodel", "team_headmodel"}};
    for (size_t i = 0; i < 2; ++i) {
        size_t model_size = strlen(models[i]), skin_size = strlen(skins[i]);
        if (skin_size > SIZE_MAX - 2 || model_size > SIZE_MAX - skin_size - 2)
            return frontend_fail(error, QA_ERROR_MEMORY, "Remote Q3 CHARACTER declaration is too large");
        char *value = malloc(model_size + skin_size + 2);
        if (!value) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining remote Q3 CHARACTER constructor value");
        memcpy(value, models[i], model_size); value[model_size] = '/';
        memcpy(value + model_size + 1, skins[i], skin_size + 1);
        bool ok = qa_cvars_register(cvars, appearance_names[i][0], value, QA_CVAR_ARCHIVE | QA_CVAR_USERINFO,
            owner, "Original Q3 selected CHARACTER declaration", error) &&
            qa_cvars_register(cvars, appearance_names[i][1], value, QA_CVAR_ARCHIVE | QA_CVAR_USERINFO,
                owner, "Original Q3 selected CHARACTER declaration", error);
        free(value); if (!ok) return false;
    }
    return true;
}
bool frontend_network_client_services(qa_frontend *f, qa_application *application,
    qa_actor_owner owner, qa_qvm_role role,
    uint32_t seat, qa_q3_host_options *host, qa_error *error)
{
    if (f->options.network_protocol.kind != QA_NET_Q3_68 || role == QA_QVM_GAME) return true;
    uint32_t ordinal;
    if (!qa_application_constructor_seat_ordinal(application, owner, seat, &ordinal, error))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 remote connection owns one local client seat");
    if (role != QA_QVM_CGAME && role != QA_QVM_UI)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote client imports require an actual CGAME or UI role");
    host->client = (qa_q3_host_client_services){.context=f->seats + ordinal,.gamestate=service_gamestate,
        .current_snapshot=service_current_snapshot,.snapshot=service_snapshot,.server_command=service_server_command,
        .current_command=service_current_command,.user_command=service_user_command,
        .command_values=service_command_values,.source_actor=service_source_actor};
    if (role == QA_QVM_UI) return true;
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
    if (!client_character_cvars(application, owner, seat, host->cvars, error)) return false;
    host->client_time_cvars = host->cvars;
    host->client_time_owner = owner;
    return true;
}
static bool client_configuration_view(const frontend_q3_client *n, frontend_remote_config_view *out, qa_error *error)
{
    const qa_frontend *f = n ? n->frontend : NULL;
    qa_application_startup_source source;
    if (!n || !out || !n->q3_client_requested || !qa_application_q3_client_configuration_read(f->application,
        n->q3_cgame_owner, QA_QVM_CGAME, n->q3_client_launch_seat, &source, error)) return false;
    frontend_remote_config *row = frontend_config_store_client(f->config_store, source.cvars);
    if (!frontend_remote_config_read(row, out) || !out->ready || !out->published ||
        out->scope.provider != source.scope.provider || out->scope.kind != source.scope.kind ||
        out->scope.seat != source.scope.seat || out->console != source.console || out->cvars != source.cvars ||
        !frontend_remote_config_current(row, out))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote CLIENT lost its completed physical configuration owner");
    return true;
}
bool frontend_network_client_configuration(const qa_frontend *f, uint32_t seat,
    frontend_remote_config_view *out, qa_error *error)
{
    frontend_q3_client *n = q3_client_launch(f, seat);
    qa_application_q3_client_context role;
    if (!n || seat != n->q3_client_launch_seat || !client_configuration_view(n, out, error) ||
        !qa_application_q3_remote_context_read(f->application, n->q3_cgame_owner, seat, &role, error) ||
        role.cvars != out->cvars || role.console != out->console ||
        !qa_application_q3_remote_context_current(f->application, &role))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT configuration lost its actual authored receiver seat");
    return true;
}
bool frontend_network_client_configuration_read(const qa_frontend *f, uint32_t seat,
    frontend_remote_config_view *out, bool *present, qa_error *error)
{
    if (!f || !out || !present)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Missing optional CLIENT configuration observation");
    memset(out, 0, sizeof(*out)); *present = false;
    frontend_q3_client *n = q3_client_launch(f, seat);
    if (!n || !n->q3_client_requested || !n->q3_cgame_owner || seat != n->q3_client_launch_seat ||
        n->q3_client_retiring || n->q3_client_closed) return true;
    qa_application_q3_client_context receiver; qa_error observed = {0};
    if (!qa_application_q3_remote_context_read(f->application, n->q3_cgame_owner, seat, &receiver, &observed)) {
        if (qa_application_startup_pending(f->application)) return true;
        if (error) *error = observed;
        return false;
    }
    if (qa_application_startup_pending(f->application)) {
        qa_application_startup_source source;
        if (!qa_application_q3_client_configuration_read(f->application, n->q3_cgame_owner, QA_QVM_CGAME, seat, &source, error)) return false;
        frontend_remote_config *row = frontend_config_store_client(f->config_store, source.cvars);
        frontend_remote_config_view view;
        if (!frontend_remote_config_read(row, &view) || !view.ready || !view.published) return true;
    }
    if (!frontend_network_client_configuration(f, seat, out, error)) return false;
    *present = true; return true;
}
bool frontend_network_client_attempt_read(const qa_frontend *f,
    const qa_application_q3_client_context *receiver, frontend_network_client_attempt *out, bool *present, qa_error *error)
{
    if (!f || !out || !present)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Missing initial CLIENT attempt observation");
    *out = (frontend_network_client_attempt){0}; *present = false;
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    if (!n || !n->q3_client_requested || n->network->detached_transport || n->q3_client_closed ||
        n->q3_client_retiring || n->q3_client_rebind || n->q3_client_decoded ||
        n->q3_client_initializing || n->q3_client_gamestate ||
        n->q3_client_admission.phase == QA_Q3_DISCONNECTED) return true;
    frontend_network_client_attempt actual = {0}; bool configured;
    if (!frontend_network_client_configuration_read(f, n->q3_client_launch_seat,
        &actual.configuration, &configured, error)) return false;
    if (!configured) return true;
    if (!n->q3_client_epoch || !qa_network_local_address(n->runtime) ||
        n->q3_client_generation != qa_application_configuration_generation(f->application) ||
        n->q3_client_content || n->q3_client_downloads ||
        !qa_application_q3_remote_source_read(f->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, n->q3_client_epoch, &actual.source, error) ||
        !actual.source.receiver.native_source || actual.source.receiver.initialized ||
        !actual.source.descriptor || actual.source.descriptor->selection.runtime != QA_PROGRAM_BUILTIN ||
        actual.source.descriptor->artifact ||
        (n->q3_client_attached && !client_connection_current(n, n->q3_client_epoch, error)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Initial CLIENT UI lost its genuine physical source and attempt");
    actual.connection = n->q3_client_attached ? n->q3_client : (qa_net_client_id){0};
    actual.endpoint = n->q3_client_admission.address;
    actual.epoch = n->q3_client_epoch; actual.restart_generation = n->q3_client_restart_generation;
    actual.phase = n->q3_client_admission.phase; actual.attached = n->q3_client_attached;
    *out = actual; *present = true; return true;
}
static bool client_attempt_owner_current(const qa_frontend *f,
    const frontend_network_client_attempt *attempt, const frontend_network_client_attempt *observed)
{
    const frontend_network_client_attempt actual = *observed;
    const qa_application_q3_client_context *a = &attempt->source.receiver, *b = &actual.source.receiver;
    return qa_application_q3_remote_source_current(f->application, &attempt->source) &&
        a->session == b->session && a->receiver == b->receiver && a->seat == b->seat &&
        a->source_owner == b->source_owner && qa_actor_id_equal(a->source_actor, b->source_actor) &&
        a->source_client == b->source_client && a->source_cvars == b->source_cvars &&
        a->service_owner == b->service_owner && a->frontend_lifetime == b->frontend_lifetime &&
        a->console == b->console && a->cvars == b->cvars &&
        a->client_time_cvars == b->client_time_cvars && a->client_time_owner == b->client_time_owner &&
        a->native_source == b->native_source && a->initialized == b->initialized &&
        a->source_milliseconds == b->source_milliseconds &&
        a->source_frame.provider == b->source_frame.provider && a->source_frame.kind == b->source_frame.kind &&
        a->source_frame.phase == b->source_frame.phase && a->source_frame.number == b->source_frame.number &&
        a->source_frame.start_ns == b->source_frame.start_ns && a->source_frame.elapsed_ns == b->source_frame.elapsed_ns &&
        a->source_frame.time_ns == b->source_frame.time_ns &&
        a->command_context.session == b->command_context.session && a->command_context.owner == b->command_context.owner &&
        a->command_context.client == b->command_context.client && a->command_context.seat == b->command_context.seat &&
        a->command_context.dialect == b->command_context.dialect && a->command_context.origin == b->command_context.origin &&
        a->command_context.direct == b->command_context.direct && a->command_context.console_text == b->command_context.console_text &&
        a->command_context.script == b->command_context.script && a->command_context.registry == b->command_context.registry &&
        a->command_context.generation == b->command_context.generation &&
        qa_actor_id_equal(a->command_context.actor, b->command_context.actor) &&
        a->receiver == attempt->configuration.scope.provider && a->seat == attempt->configuration.scope.seat &&
        a->console == attempt->configuration.console && a->cvars == attempt->configuration.cvars &&
        attempt->source.descriptor->storage == actual.source.descriptor->storage &&
        attempt->source.configuration_generation == actual.source.configuration_generation &&
        attempt->source.connection_epoch == actual.source.connection_epoch &&
        attempt->configuration.owner == actual.configuration.owner &&
        frontend_remote_config_current(attempt->configuration.owner, &attempt->configuration) &&
        attempt->epoch == actual.epoch && attempt->restart_generation == actual.restart_generation &&
        qa_net_address_equal(&attempt->endpoint, &actual.endpoint, true);
}
bool frontend_network_client_attempt_current(const qa_frontend *f,
    const frontend_network_client_attempt *attempt)
{
    frontend_network_client_attempt actual; bool present;
    return attempt && frontend_network_client_attempt_read(f, &attempt->source.receiver, &actual, &present, NULL) && present &&
        client_attempt_owner_current(f, attempt, &actual) &&
        attempt->attached == actual.attached && attempt->phase == actual.phase &&
        qa_net_client_id_equal(attempt->connection, actual.connection);
}
static bool initial_services_current(frontend_network_initial_services_binding *binding, qa_error *error)
{
    frontend_network_client_attempt actual; bool present;
    if (!binding || !binding->frontend || binding->frontend->application != binding->application ||
        binding->frontend->network != binding->network)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Initial UI DATA lost its retained frontend and Network owner");
    qa_frontend_network *n = binding->frontend->network;
    frontend_q3_client *client = q3_client_receiver(binding->frontend, &binding->attempt.source.receiver);
    bool read=binding->restore_candidate && n && n->detached_transport ?
        frontend_network_client_restore_attempt_read(binding->frontend,&actual,&present,NULL):
        frontend_network_client_attempt_read(binding->frontend,&binding->attempt.source.receiver,&actual,&present,NULL);
    if (read && present &&
        client_attempt_owner_current(binding->frontend, &binding->attempt, &actual)) return true;
    return (client && client->q3_client_requested && client->q3_client_epoch == binding->attempt.epoch &&
        client->q3_client_restart_generation == binding->attempt.restart_generation &&
        client->q3_cgame_owner == binding->attempt.source.receiver.receiver &&
        client->q3_client_launch_seat == binding->attempt.source.receiver.seat &&
        qa_net_address_equal(&client->q3_client_admission.address, &binding->attempt.endpoint, true) &&
        binding->entered && binding->entered(binding->context, &binding->attempt, error)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Initial UI DATA lost its actual CLIENT attempt and entered namespace");
}
static frontend_seat *initial_service_seat(const frontend_network_initial_services_binding *binding)
{
    frontend_q3_client *n = q3_client_receiver(binding->frontend, &binding->attempt.source.receiver);
    return n ? binding->frontend->seats + n->physical : NULL;
}
static const qa_q3_gamestate *initial_service_gamestate(void *context)
{
    frontend_network_initial_services_binding *binding = context;
    if (!initial_services_current(binding, NULL)) return NULL;
    return NULL;
}
static bool initial_service_configstring_absent(void *context, qa_error *error)
{ return initial_services_current(context, error); }
static bool initial_service_current_snapshot(void *context, int32_t *number, int32_t *time, qa_error *error)
{
    frontend_network_initial_services_binding *binding = context;
    return initial_services_current(binding, error) &&
        service_current_snapshot(initial_service_seat(binding), number, time, error);
}
static bool initial_service_snapshot(void *context, int32_t number,
    const qa_q3_snapshot **out, int32_t *ping, qa_error *error)
{
    frontend_network_initial_services_binding *binding = context;
    return initial_services_current(binding, error) &&
        service_snapshot(initial_service_seat(binding), number, out, ping, error);
}
static bool initial_service_server_command(void *context, int32_t sequence, bool *present, qa_error *error)
{
    frontend_network_initial_services_binding *binding = context;
    if (!initial_services_current(binding, error)) return false;
    *present = false;
    if (!q3_client_receiver(binding->frontend, &binding->attempt.source.receiver)->q3_client_attached) return true;
    if(binding->restore_candidate && binding->frontend->network->detached_transport)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI import cannot execute a live reliable command");
    return service_server_command(initial_service_seat(binding), sequence, present, error) &&
        initial_services_current(binding, error);
}
static int32_t initial_service_current_command(void *context)
{
    frontend_network_initial_services_binding *binding = context;
    return initial_services_current(binding, NULL) ? service_current_command(initial_service_seat(binding)) : 0;
}
static bool initial_service_user_command(void *context, int32_t number,
    qa_q3_usercmd *out, bool *present, qa_error *error)
{
    frontend_network_initial_services_binding *binding = context;
    return initial_services_current(binding, error) &&
        service_user_command(initial_service_seat(binding), number, out, present, error);
}
static bool initial_service_command_values(void *context, int32_t weapon, float sensitivity, qa_error *error)
{
    frontend_network_initial_services_binding *binding = context;
    if(binding && binding->restore_candidate && binding->frontend && binding->frontend->network &&
        binding->frontend->network->detached_transport)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI import cannot publish live input values");
    return initial_services_current(binding, error) &&
        service_command_values(initial_service_seat(binding), weapon, sensitivity, error);
}
static bool initial_service_source_actor(void *context, uint32_t number,
    qa_actor_id *out, bool *present, qa_error *error)
{
    frontend_network_initial_services_binding *binding = context;
    return initial_services_current(binding, error) &&
        service_source_actor(initial_service_seat(binding), number, out, present, error);
}
bool frontend_network_client_attempt_services(qa_frontend *f,
    const frontend_network_client_attempt *attempt, frontend_network_initial_services_binding *binding,
    void *lease_context,
    bool (*entered)(void *, const frontend_network_client_attempt *, qa_error *),
    qa_q3_host_client_services *out, qa_error *error)
{
    if (!binding || !out || !entered || !frontend_network_client_attempt_current(f, attempt))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Initial UI imports require their actual retained attempt receipt");
    *binding = (frontend_network_initial_services_binding){.frontend = f, .application = f->application,
        .network = f->network, .attempt = *attempt, .context = lease_context, .entered = entered};
    *out = (qa_q3_host_client_services){.context=binding,.gamestate=initial_service_gamestate,
        .current_snapshot=initial_service_current_snapshot,.snapshot=initial_service_snapshot,
        .server_command=initial_service_server_command,.current_command=initial_service_current_command,
        .user_command=initial_service_user_command,.command_values=initial_service_command_values,
        .source_actor=initial_service_source_actor,.configstring_absent=initial_service_configstring_absent};
    return true;
}
bool frontend_network_client_previous_configuration_read(const qa_frontend *f, uint32_t seat,
    frontend_remote_config_view *out, bool *present, qa_error *error)
{
    if (!f || !out || !present)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Missing published CLIENT configuration observation");
    memset(out, 0, sizeof(*out)); *present = false;
    frontend_q3_client *n = q3_client_launch(f, seat);
    if (!n || !n->q3_client_requested || !n->q3_cgame_owner || seat != n->q3_client_launch_seat ||
        n->q3_client_retiring || n->q3_client_closed) return true;
    qa_application_q3_client_context receiver;
    if (!qa_application_q3_remote_published_context_read(f->application, n->q3_cgame_owner, seat, &receiver, error))
        return false;
    frontend_remote_config *row = frontend_config_store_client(f->config_store, receiver.cvars);
    frontend_remote_config_view view;
    if (!frontend_remote_config_read(row, &view) || !view.ready || !view.published ||
        !view.receiver || !view.receiver->selection.instance ||
        view.scope.provider != receiver.receiver || view.scope.kind != QA_APPLICATION_CONSOLE_Q3_CGAME ||
        view.scope.seat != seat || view.physical_seat != n->physical ||
        view.console != receiver.console || view.cvars != receiver.cvars ||
        !frontend_remote_config_current(row, &view) ||
        !qa_application_q3_remote_published_context_current(f->application, &receiver))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Published CLIENT archive lost its actual physical row and receiver");
    const qa_launch_instance *descriptor = qa_launch_snapshot_find(qa_application_launch(f->application),
        view.receiver->selection.instance);
    if (!descriptor || descriptor->storage != view.receiver->storage || descriptor->content != view.receiver->content ||
        descriptor != view.receiver)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Published CLIENT archive lost its retained configuration descriptor");
    *out = view; *present = true; return true;
}
static bool client_authorization_current(void *context, qa_error *error)
{
    frontend_q3_client *n = context;
    qa_application_startup_source source; qa_application_q3_client_context role;
    frontend_remote_config_view configuration; frontend_key_profile_view profile;
    if (!n || !n->frontend || !n->q3_client_requested ||
        !qa_application_q3_client_configuration_read(n->frontend->application, n->q3_cgame_owner, QA_QVM_CGAME,
            n->q3_client_launch_seat, &source, error) ||
        !qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, &role, error)) return false;
    frontend_remote_config *row = frontend_config_store_client(n->frontend->config_store, source.cvars);
    const frontend_remote_config_view *retained = &n->q3_authorization_configuration;
    const frontend_key_profile_view *key = &n->q3_authorization_profile;
    if (!frontend_remote_config_read(row, &configuration) || !frontend_remote_config_current(row, &configuration) ||
        configuration.owner != retained->owner || configuration.receiver != retained->receiver ||
        configuration.scope.provider != retained->scope.provider || configuration.scope.kind != retained->scope.kind ||
        configuration.scope.seat != retained->scope.seat || configuration.console != retained->console ||
        configuration.cvars != retained->cvars || configuration.keys != retained->keys ||
        source.scope.provider != configuration.scope.provider || source.scope.kind != configuration.scope.kind ||
        source.scope.seat != configuration.scope.seat || source.console != configuration.console ||
        source.cvars != configuration.cvars || role.console != configuration.console || role.cvars != configuration.cvars ||
        !qa_application_q3_remote_context_current(n->frontend->application, &role) ||
        !frontend_key_profile_read(configuration.keys, &profile) || profile.profile != key->profile ||
        profile.state != key->state || profile.cvars != key->cvars || profile.identity != key->identity ||
        profile.product != key->product || profile.demo != key->demo || profile.cvars != configuration.cvars)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 authorization lost its actual CLIENT profile and registry");
    return true;
}
static bool client_authorization_profile(void *context, uint8_t key[33], bool *demo, qa_error *error)
{
    frontend_q3_client *n = context;
    frontend_remote_config_view configuration;
    return client_authorization_current(n, error) && client_configuration_view(n, &configuration, error) &&
        frontend_key_profile_authorization(configuration.keys, key, demo, error);
}
static void client_authorization_print(void *context, const char *text)
{ frontend_print(((frontend_q3_client *)context)->frontend, text); }
static qa_q3_client_authorization_bindings client_authorization_bindings(frontend_q3_client *n)
{
    return (qa_q3_client_authorization_bindings){.context = n, .cvars = n->q3_authorization_configuration.cvars,
        .current = client_authorization_current, .read_profile = client_authorization_profile,
        .print = client_authorization_print};
}
static bool client_authorization_prepare(frontend_q3_client *n, qa_bytes saved, qa_error *error)
{
    if (n->q3_client_authorization)
        return (!saved.size || frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 authorization import needs an empty actual owner")) &&
            client_authorization_current(n, error);
    qa_application_startup_source source;
    if (!qa_application_q3_client_configuration_read(n->frontend->application, n->q3_cgame_owner, QA_QVM_CGAME,
        n->q3_client_launch_seat, &source, error)) return false;
    frontend_remote_config *row = frontend_config_store_client(n->frontend->config_store, source.cvars);
    frontend_remote_config_view configuration; frontend_key_profile_view profile;
    if (!frontend_remote_config_read(row, &configuration) || !frontend_remote_config_current(row, &configuration) ||
        (!saved.size && (!configuration.ready || !configuration.published)) ||
        !frontend_key_profile_read(configuration.keys, &profile))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 authorization requires its actual bound CLIENT key profile");
    n->q3_authorization_configuration = configuration; n->q3_authorization_profile = profile;
    if (!client_authorization_current(n, error)) return false;
    qa_q3_client_authorization_bindings bindings = client_authorization_bindings(n);
    return saved.size ? qa_q3_client_authorization_restore(saved, &bindings, &n->q3_client_authorization, error) :
        qa_q3_client_authorization_create(&bindings, &n->q3_client_authorization, error);
}
typedef struct client_authorization_attempt {
    frontend_q3_client *network;
    qa_net_address address;
    uint64_t epoch;
} client_authorization_attempt;
static bool client_authorization_attempt_current(void *context, qa_error *error)
{
    const client_authorization_attempt *attempt = context;
    const frontend_q3_client *n = attempt->network;
    return (n && n->q3_client_requested && !n->network->detached_transport && !n->q3_client_retiring &&
        !n->q3_client_closed && !n->q3_client_attached && n->q3_client_epoch == attempt->epoch &&
        n->q3_client_admission.phase == QA_Q3_CONNECTING &&
        qa_net_address_equal(&n->q3_client_admission.address, &attempt->address, true) &&
        qa_network_local_address(n->runtime)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 authorization attempt lost its actual connection and UDP owner");
}
static bool client_authorization_send(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *error)
{
    client_authorization_attempt *attempt = context;
    return client_authorization_attempt_current(attempt, error) && client_send_address(attempt->network, to, bytes, error);
}
static bool client_lan_address(const qa_net_address *address)
{
    if (address->kind == QA_NET_LOOPBACK || address->kind == QA_NET_IPX) return true;
    if (address->kind != QA_NET_IPV4) return false;
    const uint8_t *host = address->host.ipv4;
    return host[0] == 127 || host[0] == 10 || (host[0] == 192 && host[1] == 168) ||
        (host[0] == 172 && host[1] >= 16 && host[1] <= 31);
}
static bool client_admission_send(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *error)
{
    frontend_q3_client *n = context;
    if (n->q3_client_admission.phase == QA_Q3_CONNECTING && !client_lan_address(to)) {
        client_authorization_attempt attempt = {n, *to, n->q3_client_epoch};
        if (!qa_q3_client_authorization_request(n->q3_client_authorization,
            client_authorization_attempt_current, client_authorization_send, &attempt, error)) return false;
    }
    return client_send_address(n, to, bytes, error);
}
static bool client_input_source_read(void *context, frontend_remote_input_source *out, bool *present, qa_error *error)
{
    frontend_q3_client *n = context; qa_frontend *f = n ? n->frontend : NULL;
    if (!out || !present) return frontend_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 source input observation");
    memset(out, 0, sizeof(*out)); *present = false;
    if (!n || !n->q3_client_attached || !n->q3_client_active || !n->q3_client_gamestate ||
        n->q3_client_retiring || n->q3_client_closed) return true;
    frontend_remote_config_view configuration;
    const qa_q3_client_peer *peer = q3_view(n);
    const qa_q3_snapshot *snapshot = peer ? qa_q3_client_peer_snapshot(peer) : NULL;
    if (!snapshot || !qa_network_q3_client_live(n->runtime, n->q3_client) ||
        !frontend_network_q3_client_context_read(f, n->q3_cgame_owner, n->q3_client_launch_seat, &out->receiver, error) ||
        !client_configuration_view(n, &configuration, error) || !configuration.q3_mouse || !configuration.q3_view ||
        configuration.cvars != out->receiver.cvars || configuration.console != out->receiver.console ||
        !frontend_q3_content_media_current(n->q3_client_content,error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 raw input lacks its actual receiver and private source settings");
    uint64_t number = qa_q3_client_peer_usercmd_number(peer);
    if (number == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 raw input exhausted its actual source append ordinal");
    out->connection = n->q3_client; out->epoch = n->q3_client_epoch;
    out->input_settings = configuration.q3_mouse;
    out->movement_settings = configuration.q3_view;
    out->input_tuning = configuration.q3_input_tuning;
    out->media_owner = n->q3_client_content;
    qa_vec3 delta = qa_v3((float)(uint16_t)snapshot->player.deltaAngles[0] * (360.0f / 65536.0f),
        (float)(uint16_t)snapshot->player.deltaAngles[1] * (360.0f / 65536.0f),
        (float)(uint16_t)snapshot->player.deltaAngles[2] * (360.0f / 65536.0f));
    out->frame = (qa_input_command_frame){.kind = QA_MOVEMENT_Q3, .sequence = (uint64_t)number + 1,
        .delta_angles = delta, .server_time_ms = n->q3_client_time, .weapon = (uint8_t)n->q3_weapon,
        .sensitivity = n->q3_sensitivity, .attack_allowed = true};
    out->initial_angles = qa_vec_sub(qa_v3(snapshot->player.viewangles[0], snapshot->player.viewangles[1],
        snapshot->player.viewangles[2]), delta);
    out->has_initial_angles = true; *present = true; return true;
}
static bool client_input_source_current(void *context, const frontend_remote_input_source *source)
{
    frontend_remote_input_source now; bool present = false; qa_error ignored = {0};
    frontend_q3_client *n = context; qa_frontend *f = n ? n->frontend : NULL;
    return source && client_input_source_read(context, &now, &present, &ignored) && present &&
        qa_net_client_id_equal(source->connection, now.connection) && source->epoch == now.epoch &&
        frontend_network_q3_client_context_current(f, &source->receiver) &&
        source->receiver.source_milliseconds == now.receiver.source_milliseconds &&
        source->input_settings == now.input_settings && source->movement_settings == now.movement_settings &&
        source->media_owner == now.media_owner &&
        source->frame.kind == now.frame.kind &&
        source->frame.sequence == now.frame.sequence && source->frame.server_time_ms == now.frame.server_time_ms &&
        source->frame.acknowledged_server_seconds == now.frame.acknowledged_server_seconds &&
        source->frame.server_frame == now.frame.server_frame && source->frame.light_level == now.frame.light_level &&
        source->frame.weapon == now.frame.weapon && source->frame.sensitivity == now.frame.sensitivity &&
        source->frame.attack_allowed == now.frame.attack_allowed &&
        source->frame.has_pitch_drift == now.frame.has_pitch_drift && source->frame.grounded == now.frame.grounded &&
        source->frame.drift_disabled == now.frame.drift_disabled && source->frame.ideal_pitch == now.frame.ideal_pitch &&
        source->frame.delta_angles.x == now.frame.delta_angles.x && source->frame.delta_angles.y == now.frame.delta_angles.y &&
        source->frame.delta_angles.z == now.frame.delta_angles.z && source->has_initial_angles == now.has_initial_angles &&
        source->initial_angles.x == now.initial_angles.x && source->initial_angles.y == now.initial_angles.y &&
        source->initial_angles.z == now.initial_angles.z;
}
static frontend_remote_input_options client_input_options(frontend_q3_client *n)
{ return (frontend_remote_input_options){n, client_input_source_read, client_input_source_current}; }
static bool local_q3_prepare(frontend_local_client *local, qa_actor_id actor, qa_error *error)
{
    qa_frontend_network *owner = local->network;
    frontend_q3_client *n = owner->q3_clients + local->physical;
    n->runtime = local->runtime; n->seat = local->client_seat; n->q3_client_requested = true;
    local->q3 = n;
    if (!qa_application_network_q3_client_source(n->frontend->application, actor, &n->q3_cgame_owner,
        &n->q3_client_product, &n->q3_client_launch_seat, error)) return false;
    qa_q3_client_admission_begin(&n->q3_client_admission, &owner->loopback_server,
        (uint16_t)(owner->rotation_random + local->physical));
    n->q3_client_generation = qa_application_configuration_generation(n->frontend->application);
    n->q3_client_epoch = 1;
    if (!qa_q3_prediction_scene_create(n->q3_client_product, &n->q3_prediction_scene, error)) return false;
    frontend_remote_input_options input = client_input_options(n);
    if (!frontend_remote_input_create(&input, &n->q3_input, error)) return false;
    qa_application_q3_client_context receiver;
    if (!qa_application_q3_remote_context_read(n->frontend->application, n->q3_cgame_owner,
        n->q3_client_launch_seat, &receiver, error)) return false;
    client_cvars_bind(n, receiver.cvars);
    if (receiver.native_source) {
        qa_application_control_prediction_configuration initial;
        if (!qa_application_control_prediction_read(n->frontend->application, actor, &initial, error) ||
            !frontend_network_predictor_create(n->frontend, &receiver, &initial, &n->q3_predictor, error)) return false;
    }
    return true;
}
bool frontend_network_prediction_input_read(const qa_frontend *f,
    const qa_application_q3_client_context *receiver, frontend_remote_input_source *out,
    bool *present, qa_error *error)
{
    frontend_q3_client *n=q3_client_receiver(f,receiver);
    if(!out || !present) return frontend_fail(error,QA_ERROR_ARGUMENT,"Missing prediction input observation");
    memset(out,0,sizeof(*out)); *present=false;
    if(!n || !n->q3_client_attached || !n->q3_client_active || !n->q3_client_gamestate ||
        n->q3_client_retiring || n->q3_client_closed) return true;
    const qa_q3_client_peer *peer=q3_view(n); frontend_remote_config_view configuration;
    uint64_t number=peer?qa_q3_client_peer_usercmd_number(peer):0;
    const qa_q3_usercmd *command=peer?qa_q3_client_peer_usercmd_at(peer,number):NULL;
    if(!command || !qa_network_q3_client_live(n->runtime,n->q3_client) ||
        !frontend_network_q3_client_context_read(f,n->q3_cgame_owner,n->q3_client_launch_seat,&out->receiver,error) ||
        !client_configuration_view(n,&configuration,error) || !configuration.q3_mouse || !configuration.q3_view ||
        configuration.cvars!=out->receiver.cvars || configuration.console!=out->receiver.console)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Prediction input lost its actual retained source command and settings");
    out->connection=n->q3_client; out->epoch=n->q3_client_epoch;
    out->input_settings=configuration.q3_mouse; out->movement_settings=configuration.q3_view;
    out->input_tuning=configuration.q3_input_tuning;
    out->media_owner=n->q3_client_content;
    out->frame=(qa_input_command_frame){.kind=QA_MOVEMENT_Q3,.sequence=number,
        .server_time_ms=n->q3_client_time,.weapon=command->weapon};
    *present=true; return true;
}
bool frontend_network_prediction_input_current(const qa_frontend *f, const frontend_remote_input_source *source)
{
    frontend_remote_input_source now; bool present=false; qa_error ignored={0};
    if(!source || !frontend_network_prediction_input_read(f,&source->receiver,&now,&present,&ignored) || !present ||
        !qa_net_client_id_equal(source->connection,now.connection) || source->epoch!=now.epoch ||
        !frontend_network_q3_client_context_current(f,&source->receiver) ||
        source->receiver.source_milliseconds!=now.receiver.source_milliseconds ||
        source->input_settings!=now.input_settings || source->movement_settings!=now.movement_settings ||
        source->media_owner!=now.media_owner) return false;
    const qa_input_command_frame *v=&source->frame;
    return v->kind==now.frame.kind && v->sequence==now.frame.sequence && v->server_time_ms==now.frame.server_time_ms &&
        v->weapon==now.frame.weapon && v->acknowledged_server_seconds == 0 && !v->server_frame && !v->light_level &&
        v->sensitivity == 0 && !v->attack_allowed && !v->has_pitch_drift && !v->grounded && !v->drift_disabled &&
        v->ideal_pitch == 0 && v->delta_angles.x == 0 && v->delta_angles.y == 0 && v->delta_angles.z == 0 &&
        !source->has_initial_angles && source->initial_angles.x == 0 && source->initial_angles.y == 0 && source->initial_angles.z == 0;
}
bool frontend_network_client_frame_time_read(const qa_frontend *f,
    const qa_source_frame_time_binding **out, bool *present, qa_error *error)
{
    if (!f || !out || !present)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid CLIENT time registry observation");
    *out = NULL; *present = false;
    const qa_frontend_network *n = f->network;
    if(n && n->unified_client_service) {
        if(frontend_network_unified_client_retired(n->unified_client_service)) return true;
        frontend_client_source_view physical;
        if(!frontend_network_unified_client_source_read(n->unified_client_service,&physical,error)) return false;
        if(!physical.ready) return true;
        if(!frontend_client_source_current(&physical))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Unified CLIENT timing lost its actual physical configuration");
        *out=physical.frame_time; *present=true; return true;
    }
    if(n && n->q1_client_owner) {
        if(frontend_network_q1_client_retired(n->q1_client_owner)) return true;
        frontend_network_q1_client_view held;
        if(!frontend_network_q1_client_metadata_read(n->q1_client_owner,&held,error)) return false;
        if(!held.physical.ready) return true;
        if(!frontend_client_source_current(&held.physical))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 CLIENT timing lost its actual physical configuration");
        *out=held.physical.frame_time; *present=true; return true;
    }
    if(n && n->q2_client_owner) {
        if(frontend_network_q2_client_retired(n->q2_client_owner)) return true;
        qa_application_client_source held; bool ready;
        if(!frontend_network_q2_client_configuration_read(n->q2_client_owner,&held,&ready,error)) return false;
        if(!ready) return true;
        *out=frontend_network_q2_client_frame_time(n->q2_client_owner); *present=true; return true;
    }
    if (!n || !n->q3_clients[0].q3_client_requested || !n->q3_clients[0].q3_cgame_owner) return true;
    qa_application_startup_source source;
    if (!qa_application_q3_client_configuration_read(f->application, n->q3_clients[0].q3_cgame_owner, QA_QVM_CGAME,
        n->q3_clients[0].q3_client_launch_seat, &source, error)) return false;
    frontend_remote_config *row = frontend_config_store_client(f->config_store, source.cvars);
    frontend_remote_config_view view;
    if (!frontend_remote_config_read(row, &view)) {
        if (qa_application_startup_pending(f->application)) return true;
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT timing has no retained physical configuration owner");
    }
    if ((!view.ready || !view.published) && qa_application_startup_pending(f->application)) return true;
    if (!view.ready || !view.published ||
        view.scope.provider != source.scope.provider || view.scope.kind != source.scope.kind ||
        view.scope.seat != source.scope.seat || view.console != source.console || view.cvars != source.cvars ||
        !frontend_remote_config_current(row, &view))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT timing lacks its completed physical configuration owner");
    *out = view.frame_time; *present = true; return true;
}
bool frontend_network_client_map_read(const qa_frontend *f, qa_application *application,
    qa_actor_owner owner, qa_qvm_role role, uint32_t seat, const qa_vfs *mounts,
    const qa_resource **out, bool *present, qa_error *error)
{
    if (!f || f->application != application || !out || !present || !mounts ||
        (role != QA_QVM_CGAME && role != QA_QVM_UI))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote map construction needs its actual application, role and content");
    *out = NULL; *present = false;
    if (f->options.network_protocol.kind != QA_NET_Q3_68) return true;
    qa_application_q3_client_preparation preparation;
    if (!qa_application_q3_preconstruction_source_read(application, owner, role, seat, &preparation, error) ||
        !preparation.receiver_descriptor || preparation.receiver_descriptor->content != mounts)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote map construction differs from its actual pending role descriptor");
    const frontend_q3_client *n = q3_client_seat(f, owner, seat);
    if (!n || !n->q3_client_content) return true;
    frontend_q3_content_view content;
    if (owner != n->q3_cgame_owner || seat != n->q3_client_launch_seat ||
        !frontend_q3_content_read(n->q3_client_content, &content, error)) return false;
    if (!content.map) return true;
    if (preparation.receiver_catalog != content.catalog ||
        preparation.receiver_descriptor->selection.product != content.selected)
        return frontend_fail(error, QA_ERROR_FORMAT, "Remote map construction changes its genuine prepared private catalog");
    *out = content.map; *present = true; return true;
}
bool frontend_network_client_command_seat(qa_frontend *f, uint32_t seat, const char *text, qa_error *error)
{
    frontend_q3_client *n = q3_client_physical(f, seat); qa_application_q3_client_context role;
    if (!n || !text || !frontend_network_q3_client_context_read(f, n->q3_cgame_owner, n->q3_client_launch_seat, &role, error) ||
        !frontend_network_q3_client_context_current(f, &role))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote client command lost its actual Q3 receiver and connection seat");
    return qa_network_q3_client_command(n->runtime, n->q3_client, text, error);
}
bool frontend_network_client_reliable(qa_frontend *f, const qa_command_context *origin,
    const char *text, qa_error *error)
{
    frontend_q3_client *n = origin ? q3_client_seat(f, origin->owner, origin->seat) : NULL;
    qa_application_q3_client_context role;
    qa_command_context actual;
    if (!n || !origin || !text || !client_connection_current(n, n->q3_client_epoch, error) ||
        !frontend_network_q3_client_context_read(f, n->q3_cgame_owner, n->q3_client_launch_seat, &role, error) ||
        !frontend_network_q3_client_context_current(f, &role) ||
        !qa_application_capture_command_context(f->application, &role.command_context, &actual, error) ||
        origin->session != actual.session || origin->owner != actual.owner || origin->seat != actual.seat ||
        origin->cvar_view != actual.cvar_view ||
        origin->dialect != actual.dialect || origin->origin != actual.origin ||
        origin->client != actual.client || !qa_actor_id_equal(origin->actor, actual.actor) ||
        origin->registry != actual.registry || origin->generation != actual.generation ||
        !qa_application_command_context_active(f->application, origin))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Reliable CLIENT command lost its actual receiver/input origin");
    return qa_network_q3_client_command(n->runtime, n->q3_client, text, error);
}
bool frontend_network_client_forward(qa_frontend *f, const qa_command_invocation *call, qa_error *error)
{
    frontend_q3_client *n = NULL;
    if (f && f->network && call) for (uint32_t i = 0; i < f->options.seats; ++i) {
        frontend_q3_client *client = f->network->q3_clients + i;
        if (!client->q3_client_requested) continue;
        qa_application_startup_source source;
        if (!qa_application_q3_client_configuration_read(f->application, client->q3_cgame_owner,
            QA_QVM_CGAME, client->q3_client_launch_seat, &source, error)) return false;
        if (source.console == call->console) { n = client; break; }
    }
    qa_application_q3_client_context role; qa_command_context actual;
    const char *text;bool explicit_command;
    if(!qa_console_forward_text(call,&text,&explicit_command,error))return false;
    if (!n ||
        !frontend_network_q3_client_context_read(f, n->q3_cgame_owner, n->q3_client_launch_seat, &role, error) ||
        !frontend_network_q3_client_context_current(f, &role) || call->console != role.console ||
        !qa_console_invocation_current(call->console,call) ||
        !qa_application_capture_command_context(f->application, &role.command_context, &actual, error) ||
        !qa_application_command_context_active(f->application, &call->context))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Console forwarding lost its actual CLIENT invocation");
    const qa_command_context *sender=&call->context;
    bool source=sender->session==actual.session && sender->owner==actual.owner &&
        sender->cvar_view==actual.cvar_view && sender->seat==actual.seat &&
        sender->dialect==actual.dialect && sender->origin==actual.origin && sender->client==actual.client &&
        qa_actor_id_equal(sender->actor,actual.actor) && sender->registry==actual.registry &&
        sender->generation==actual.generation;
    bool engine=!sender->owner && sender->session==actual.session &&
        sender->cvar_view==qa_cvars_view_identity(qa_application_cvars(f->application)) &&
        qa_console_invocation_delivered_view(call,qa_cvars_view_identity(role.cvars),role.receiver,role.service_owner);
    if (!source && !engine)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Console forwarding lacks its delivered CLIENT capability");
    const qa_q3_client_peer *peer = q3_view(n);
    bool demo = peer && qa_q3_client_peer_demo(peer);
    if (explicit_command) {
        if (!n->q3_client_active || n->q3_client_closed || n->q3_client_retiring || demo) {
            frontend_console_print(f, &call->context, "Not connected to a server.\n"); return true;
        }
        return call->argc == 1 || frontend_network_client_reliable(f, &actual, text, error);
    }
    if (call->argv[0][0] == '-') return true;
    if (demo || n->q3_client_admission.phase != QA_Q3_ADMITTED || !n->q3_client_attached ||
        n->q3_client_closed || n->q3_client_retiring || call->argv[0][0] == '+') {
        size_t length = strlen(call->argv[0]);
        if (length > SIZE_MAX - sizeof("Unknown command \"\"\n"))
            return frontend_fail(error, QA_ERROR_MEMORY, "Forwarding diagnostic exceeds native storage");
        char *message = malloc(length + sizeof("Unknown command \"\"\n"));
        if (!message) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining source forwarding diagnostic");
        snprintf(message, length + sizeof("Unknown command \"\"\n"), "Unknown command \"%s\"\n", call->argv[0]);
        frontend_console_print(f, &call->context, message); free(message); return true;
    }
    return frontend_network_client_reliable(f, &actual, text, error);
}
bool frontend_network_client_command(qa_frontend *f, const char *text, qa_error *error)
{ return frontend_network_client_command_seat(f, 0, text, error); }
static bool save_favorites(qa_frontend_network *n, qa_error *error)
{
    qa_buffer bytes = {0}; bool created;
    if (n->nonce == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Favorite publication namespace exhausted");
    if (!qa_server_browser_save(n->browser, &bytes, error)) return false;
    bool ok = qa_fs_root_publish(n->preferences, "network/favorites.bin", (qa_bytes){bytes.data, bytes.size},
        ++n->nonce, false, true, &created, error);
    qa_buffer_free(&bytes); return ok;
}
static bool save_filters(qa_frontend_network *n, qa_error *error)
{
    qa_buffer bytes={0}; bool created;
    if (n->nonce==UINT64_MAX) return frontend_fail(error,QA_ERROR_ARGUMENT,"Filter publication namespace exhausted");
    if (!qa_server_admin_save_filters(n->admin,&bytes,error)) return false;
    bool ok=qa_fs_root_publish(n->preferences,"network/filters.bin",(qa_bytes){bytes.data,bytes.size},
        ++n->nonce,false,true,&created,error);
    qa_buffer_free(&bytes); return ok;
}
static int menu_family(qa_net_protocol_id protocol)
{
    if(protocol.kind<=QA_NET_RMQ999) return 0;
    if(protocol.kind<=QA_NET_QW29) return 1;
    if(protocol.kind<=QA_NET_Q2PRIVATE_4038) return 2;
    return protocol.kind==QA_NET_Q3_68?3:-1;
}
static bool menu_direct_text_valid(const char *text)
{
    if(!text || !*text || strlen(text)>255) return false;
    for(const unsigned char *p=(const unsigned char *)text;*p;++p) {
        if(*p<=32 || *p==127) return false;
        if(p[1] && ((*p==0xc2 && p[1]==0xa0) ||
            (p[2] && ((*p==0xe1 && p[1]==0x9a && p[2]==0x80) ||
             (*p==0xe2 && p[1]==0x80 && ((p[2]>=0x80 && p[2]<=0x8a) ||
                p[2]==0xa8 || p[2]==0xa9 || p[2]==0xaf)) ||
             (*p==0xe2 && p[1]==0x81 && p[2]==0x9f) ||
             (*p==0xe3 && p[1]==0x80 && p[2]==0x80) ||
             (*p==0xef && p[1]==0xbb && p[2]==0xbf))))) return false;
    }
    return true;
}
static bool menu_preferences_fields(qa_source_save_io *io,frontend_network_menu_preferences rows[4])
{
    for(size_t family=0;family<4;++family) {
        frontend_network_menu_preferences *row=rows+family;
        if(!qa_source_save_bytes(io,row->master,sizeof(row->master)) ||
            !memchr(row->master,0,sizeof(row->master)) || !qa_source_save_u32(io,&row->direct_count) ||
            row->direct_count>16) return frontend_fail(io->error,QA_ERROR_FORMAT,"Invalid retained browser preferences");
        for(uint32_t i=0;i<row->direct_count;++i) {
            frontend_network_menu_direct *direct=row->direct+i;
            if(!qa_source_save_bytes(io,direct->remote,sizeof(direct->remote)) ||
                !memchr(direct->remote,0,sizeof(direct->remote)) || !menu_direct_text_valid(direct->remote) ||
                !network_address_fields(io,&direct->address) || !direct->address.port ||
                (direct->address.kind!=QA_NET_IPV4 && direct->address.kind!=QA_NET_IPV6))
                return frontend_fail(io->error,QA_ERROR_FORMAT,"Invalid original direct server receipt");
            for(uint32_t j=0;j<i;++j)
                if(qa_net_address_equal(&direct->address,&row->direct[j].address,true))
                    return frontend_fail(io->error,QA_ERROR_FORMAT,"Duplicate direct server receipt");
        }
    }
    return true;
}
static bool menu_preferences_publish(qa_frontend_network *n,frontend_network_menu_preferences rows[4],qa_error *error)
{
    if(n->nonce==UINT64_MAX) return frontend_fail(error,QA_ERROR_ARGUMENT,"Browser preference publication namespace exhausted");
    qa_source_save_io io={0}; qa_buffer bytes={0}; uint32_t magic=UINT32_C(0x504d4e51); bool created;
    bool ok=qa_source_save_writer(&io,NULL,error) && qa_source_save_u32(&io,&magic) &&
        menu_preferences_fields(&io,rows) && qa_source_save_finish(&io,&bytes);
    if(ok) ok=qa_fs_root_publish(n->preferences,"network/menu-preferences.bin",(qa_bytes){bytes.data,bytes.size},
        ++n->nonce,false,true,&created,error);
    if(ok) memcpy(n->menu_preferences,rows,sizeof(n->menu_preferences));
    qa_source_save_dispose(&io); qa_buffer_free(&bytes); return ok;
}
static bool menu_preferences_load(qa_frontend_network *n,qa_error *error)
{
    qa_fs_file *file=NULL; qa_fs_identity identity; qa_buffer bytes={0}; qa_error missing={0};
    if(!qa_fs_root_file_open(n->preferences,"network/menu-preferences.bin",&file,&identity,&missing)) {
        if(missing.code==QA_ERROR_NOT_FOUND) return true;
        if(error) *error=missing;
        return false;
    }
    bool ok=qa_fs_file_read_snapshot(file,&identity,&bytes,error); qa_fs_file_close(file);
    frontend_network_menu_preferences rows[4]={0}; qa_source_save_io io={0}; uint32_t magic=0;
    ok=ok && bytes.size<=65536 && qa_source_save_reader(&io,NULL,(qa_bytes){bytes.data,bytes.size},error) &&
        qa_source_save_u32(&io,&magic) && magic==UINT32_C(0x504d4e51) && menu_preferences_fields(&io,rows) && qa_source_save_finish(&io,NULL);
    if(ok) {
        memcpy(n->menu_preferences,rows,sizeof(rows));
        const qa_net_protocol kinds[4]={QA_NET_NQ15,QA_NET_QW28,QA_NET_Q2_34,QA_NET_Q3_68};
        for(size_t f=0;ok && f<4;++f) for(uint32_t i=0;ok && i<rows[f].direct_count;++i)
            ok=qa_server_browser_add(n->browser,&rows[f].direct[i].address,(qa_net_protocol_id){.kind=kinds[f]},QA_SERVER_DIRECT,error);
    }
    qa_source_save_dispose(&io); qa_buffer_free(&bytes);
    return ok || frontend_fail(error,QA_ERROR_FORMAT,"Invalid persisted browser preferences");
}
static bool menu_preferences_master(qa_frontend_network *n,qa_net_protocol_id protocol,const char *remote,qa_error *error)
{
    int family=menu_family(protocol);
    if(family<0 || !remote || strlen(remote)>2048)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Browser master address exceeds its actual preference extent");
    frontend_network_menu_preferences rows[4]; memcpy(rows,n->menu_preferences,sizeof(rows));
    memset(rows[family].master,0,sizeof(rows[family].master));
    memcpy(rows[family].master,remote,strlen(remote));
    return menu_preferences_publish(n,rows,error);
}
static bool menu_preferences_direct(qa_frontend_network *n,qa_net_protocol_id protocol,const char *remote,
    const qa_net_address *address,qa_error *error)
{
    int family=menu_family(protocol);
    if(family<0 || !remote || !*remote || strlen(remote)>255 || !address)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid original direct server address");
    frontend_network_menu_preferences rows[4]; memcpy(rows,n->menu_preferences,sizeof(rows));
    frontend_network_menu_preferences *row=rows+family;
    frontend_network_menu_direct previous[16]; memcpy(previous,row->direct,sizeof(previous)); uint32_t count=row->direct_count;
    memset(row->direct,0,sizeof(row->direct)); row->direct_count=1;
    row->direct[0].address=*address; memcpy(row->direct[0].remote,remote,strlen(remote));
    for(uint32_t i=0;i<count && row->direct_count<16;++i)
        if(!qa_net_address_equal(&previous[i].address,address,true)) row->direct[row->direct_count++]=previous[i];
    return menu_preferences_publish(n,rows,error) && qa_server_browser_add(n->browser,address,protocol,QA_SERVER_DIRECT,error);
}
static bool download_permit(void *context, const qa_download_request *request, const char *url, qa_error *error)
{
    (void)context;
    if (!request->exact_length || !url || request->maximum_bytes > UINT64_C(2147483648))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "download requires explicit URL and bounded exact file length");
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
static bool download_catalog_receipt(qa_frontend_network *n, const char *path, qa_error *error)
{
    char *native = NULL;
    if (!qa_fs_root_join(n->content, path, &native, error)) return false;
    const qa_catalog *catalog = qa_application_catalog(n->frontend->application);
    qa_archive_kind kind = qa_archive_kind_for_path(path);
    bool mounted = false;
    for (size_t i = 0; !mounted && i < qa_catalog_mount_count(catalog); ++i) {
        const qa_catalog_mount *mount = qa_catalog_mount_at(catalog, i);
        if (kind != QA_ARCHIVE_AUTO) {
            if (mount->format == kind && !strcmp(mount->path, native)) {
                mounted = mount->identity != NULL;
            }
            continue;
        }
        size_t length = strlen(mount->path);
        if (mount->format != QA_ARCHIVE_AUTO || strncmp(native, mount->path, length) ||
            native[length] != '/') continue;
        for (size_t p = 0; !mounted && p < qa_catalog_count(catalog); ++p) {
            size_t count = 0;
            const qa_catalog_map *maps = qa_catalog_maps(catalog, qa_catalog_at(catalog, p)->id, &count);
            for (size_t m = 0; !mounted && m < count; ++m)
                mounted = !maps[m].archived && maps[m].mount == mount->id &&
                    !strcmp(maps[m].path, native + length + 1);
        }
    }
    free(native);
    if (!mounted) return frontend_fail(error, QA_ERROR_NOT_FOUND,
        "published download was not admitted by the user content catalog");
    return true;
}
static bool download_remount(void *context, const char *path, qa_error *error)
{
    qa_frontend_network *n = context;
    qa_application *application = n->frontend->application;
    if (!qa_application_rediscover(application, n->frontend->options.application.discover_mods, error)) return false;
    if (!download_catalog_receipt(n, path, error)) return false;
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
    if (!n->content && !qa_fs_root_open(n->frontend->options.application.user_root, &n->content, error)) return false;
    qa_download_options options = saved_download_options(n);
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
static bool client_attempt_enqueue(qa_frontend_network *n, const char *server,
    bool disconnect, qa_error *error)
{
    if(!disconnect && (!server || !*server || strlen(server)>=sizeof(n->client_server)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connection command lacks its retained native server name");
    if(n->q3_attempt_count==SIZE_MAX)
        return frontend_fail(error,QA_ERROR_MEMORY,"Remote connection command queue is exhausted");
    frontend_q3_attempt *request=calloc(1,sizeof(*request));
    if(!request) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining genuine remote connection command");
    request->disconnect=disconnect;
    if(!disconnect) memcpy(request->server,server,strlen(server)+1);
    if(n->q3_attempt_tail) n->q3_attempt_tail->next=request;
    else n->q3_attempts=request;
    n->q3_attempt_tail=request; ++n->q3_attempt_count;
    return true;
}
static bool client_attempt_queue(qa_frontend_network *n, const qa_command_invocation *call, qa_error *error)
{
    uint32_t physical;
    bool q1=frontend_network_client_only(n->frontend) && q1_client_protocol(n->frontend->options.network_protocol);
    if ((!n->q3_clients[0].q3_client_requested && !n->q1_client_owner && !q1) || call->context.origin == QA_COMMAND_REMOTE ||
        call->context.origin == QA_COMMAND_SERVER ||
        !frontend_command_seat_read(n->frontend, &call->context, &physical) || physical != 0)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Connection commands need the actual local remote-client owner");
    bool disconnect = !strcmp(call->argv[0], "disconnect"), reconnecting = !strcmp(call->argv[0], "reconnect");
    if (call->argc != (disconnect || reconnecting ? 1u : 2u))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "usage: connect server, reconnect, or disconnect");
    if (!n->q3_clients[0].q3_client_requested && !n->q1_client_owner) {
        qa_frontend *f=n->frontend;
        qa_console *console=NULL; qa_cvars *cvars=NULL; qa_command_context recipient;
        uint64_t lifetime=0; qa_command_handler handler=NULL; void *user=NULL;
        if (!q1 || f->network!=n || n->busy || n->round || n->detached_transport ||
            f->capture || f->resource_inventory || f->source_restoring || f->preparing ||
            !qa_network_callbacks_idle(n->runtime) || !f->seats || physical>=f->options.seats ||
            !qa_console_invocation_current(call->console,call) ||
            !qa_input_seat_recipient_read(f->seats[physical].input,&console,&cvars,&recipient) ||
            console!=qa_application_console(f->application) || call->console!=console ||
            cvars!=qa_application_cvars(f->application) ||
            !qa_application_capture_command_context(f->application,&recipient,&recipient,error) ||
            call->context.session!=recipient.session || call->context.owner!=recipient.owner ||
            call->context.client!=recipient.client || call->context.seat!=recipient.seat ||
            call->context.registry!=recipient.registry || call->context.generation!=recipient.generation ||
            call->context.dialect!=recipient.dialect || !qa_actor_id_equal(call->context.actor,recipient.actor) ||
            !qa_console_registration_read(console,call->argv[0],0,&lifetime,&handler,&user) ||
            lifetime!=NETWORK_OWNER || !handler || user!=n)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Disconnected Q1 command lost its current local ENGINE recipient");
        if (disconnect) return true;
    }
    if (!n->q3_clients[0].q3_client_requested && n->q1_client_owner) {
        frontend_network_q1_client_view view;
        if (!qa_console_invocation_current(call->console,call) ||
            !frontend_network_q1_client_metadata_read(n->q1_client_owner,&view,error) ||
            call->console!=view.physical.source.context.console ||
            call->context.owner!=view.physical.source.context.command.owner ||
            call->context.client!=view.physical.source.context.command.client ||
            call->context.seat!=view.physical.source.context.command.seat ||
            call->context.registry!=view.physical.source.context.command.registry ||
            call->context.generation!=view.physical.source.context.command.generation ||
            call->context.dialect!=view.physical.source.context.command.dialect ||
            !qa_actor_id_equal(call->context.actor,view.physical.source.context.command.actor) ||
            !frontend_client_source_current(&view.physical))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Connection command lost its actual Q1 CLIENT command namespace");
    }
    const char *server = reconnecting ? n->client_server : disconnect ? "" : call->argv[1];
    if (reconnecting) for (const frontend_q3_attempt *queued = n->q3_attempts; queued; queued = queued->next)
        if (!queued->disconnect) server = queued->server;
    return client_attempt_enqueue(n,server,disconnect,error);
}
static bool browser_command_current(void *context, qa_error *error)
{
    qa_frontend_network *n = context; frontend_remote_config_view configuration;
    return browser_owner_current(n, error) && client_configuration_view(&n->q3_clients[0], &configuration, error);
}
static int32_t browser_integer(const char *text)
{
    char *end; long long value = strtoll(text, &end, 10);
    return end == text ? 0 : value > INT32_MAX ? INT32_MAX : value < INT32_MIN ? INT32_MIN : (int32_t)value;
}
static bool q3_browser_command(qa_frontend_network *n, const qa_command_invocation *call, qa_error *error)
{
    uint32_t physical; frontend_remote_config_view configuration;
    if (!n->q3_clients[0].q3_client_requested || call->context.origin == QA_COMMAND_REMOTE || call->context.origin == QA_COMMAND_SERVER ||
        !frontend_command_seat_read(n->frontend, &call->context, &physical) || physical != 0 ||
        !client_configuration_view(&n->q3_clients[0], &configuration, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 browser command requires the actual local CLIENT owner");
    frontend_q3_browser_access access = {.browser = n->q3_browser, .cvars = configuration.cvars,
        .context = n, .current = browser_command_current};
    const char *name = call->argv[0];
    if (!strcmp(name, "localservers")) {
        emit(call, "Scanning for servers on the local network...\n"); return frontend_q3_browser_scan(n->q3_browser, error);
    }
    if (!strcmp(name, "globalservers")) {
        if (call->argc < 3) { emit(call, "usage: globalservers <master# 0-1> <protocol> [keywords]\n"); return true; }
        const qa_cvar_view *master = qa_cvars_find(configuration.cvars, "sv_master1");
        const qa_cvar_view *restricted = qa_cvars_find(configuration.cvars, "fs_restrict");
        if (!master || !restricted) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 CLIENT browser policy registry is incomplete");
        size_t count = call->argc - 3; bool demo = restricted->number != 0;
        const char **keywords = calloc(count + 1, sizeof(*keywords));
        if (!keywords) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual browser command keywords");
        for (size_t i = 0; i < count; ++i) keywords[i] = call->argv[i + 3];
        if (demo) keywords[count++] = "demo";
        emit(call, "Requesting servers from the master...\n");
        bool ok = frontend_q3_browser_master(n->q3_browser, browser_integer(call->argv[1]) == 1 ? 1 : 2,
            master->value, browser_integer(call->argv[2]), keywords, count, error);
        free(keywords); return ok;
    }
    if (!strcmp(name, "ping")) {
        if (call->argc != 2) { emit(call, "usage: ping [server]\n"); return true; }
        return frontend_q3_browser_ping(&access, call->argv[1], error);
    }
    const char *server = call->argc == 2 ? call->argv[1] : n->q3_clients[0].q3_client_active ? n->client_server : NULL;
    if (!server) { emit(call, "Not connected to a server.\nUsage: serverstatus [server]\n"); return true; }
    return frontend_q3_browser_status_command(&access, server, error);
}
static bool operator_print(void *context,const char *text,qa_error *error)
{
    (void)error; emit(context,text); return true;
}
bool frontend_network_source_admin_dispatch(qa_frontend *f,qa_server_admin *admin,void *context,
    bool (*send)(void *,const qa_net_address *,qa_bytes,qa_error *),
    bool (*persist_filters)(void *,qa_error *),const qa_command_invocation *call,
    size_t skip,bool *handled,qa_error *error)
{
    if (!f || !admin || !send || !persist_filters || !call || !call->console || !handled ||
        call->context.origin==QA_COMMAND_REMOTE || skip>=call->argc ||
        !qa_console_invocation_current(call->console,call))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Operator dispatch requires its actual invocation and effect owners");
    const char *name=call->argv[skip]; size_t argc=call->argc-skip;
    const char *const *argv=call->argv+skip;
    const char *arguments=call->args_text?call->args_text:"";
    *handled=true;
    if (skip) {
        qa_tokenizer lexer; qa_token token; bool found;
        if (!qa_tokenizer_init(&lexer,(qa_bytes){(const uint8_t *)arguments,strlen(arguments)},error) ||
            !qa_tokenizer_next(&lexer,&token,&found,error) || !found) return false;
        arguments+=lexer.offset;
        while (*arguments==' ' || *arguments=='\t') ++arguments;
    }
    if (!strcmp(name, "addip") || !strcmp(name, "removeip"))
        return argc == 2 ? qa_server_admin_filter(admin, argv[1], !strcmp(name, "removeip"), error) && persist_filters(context,error) :
            frontend_fail(error, QA_ERROR_ARGUMENT, "usage: addip/removeip address-mask");
    if (!strcmp(name,"listip")) {
        qa_buffer filters={0};
        if(!qa_server_admin_filters_text(admin,false,call->context.dialect,true,&filters,error)) return false;
        emit(call,(const char *)filters.data); qa_buffer_free(&filters); return true;
    }
    if (!strcmp(name,"writeip")) {
        qa_cvars *registry=frontend_config_store_cvar_owner(f->config_store,call->console,&call->context,"filterban");
        const qa_cvar_view *filterban=qa_cvars_find(registry,"filterban");
        if (!filterban) return frontend_fail(error,QA_ERROR_ARGUMENT,"Filter write requires the actual Source filterban declaration");
        qa_buffer filters={0};
        if (!qa_server_admin_filters_text(admin,true,call->context.dialect,filterban->integer!=0,&filters,error)) return false;
        bool ok=frontend_config_store_write_source_text(f->config_store,call,"listip.cfg",
            (qa_bytes){filters.data,filters.size},error);
        qa_buffer_free(&filters); if (ok) emit(call,"Wrote listip.cfg\n"); return ok;
    }
    if (!strcmp(name,"addlrconcmd") || !strcmp(name,"dellrconcmd") || !strcmp(name,"listlrconcmds"))
        return qa_server_admin_limited_command(admin,name,arguments,
            operator_print,(void *)call,error);
    if (!strcmp(name, "setmaster")) {
        bool qw=call->context.dialect==QA_CONSOLE_QW;
        bool q2=call->context.dialect==QA_CONSOLE_Q2 || call->context.dialect==QA_CONSOLE_Q2_RERELEASE;
        if (!qw && !q2) return frontend_fail(error,QA_ERROR_UNSUPPORTED,"This Source uses master cvars instead of setmaster");
        if (q2 && !f->options.dedicated) { emit(call,"Only dedicated servers use masters.\n"); return true; }
        if (q2) {
            qa_application_startup_source source; bool present=true;
            if (!call->context.owner) {
                if (!frontend_config_store_primary_server_read(f->config_store,&source,&present,error)) return false;
            } else if (!frontend_config_store_server_invocation_read(f->config_store,call,&source,error)) return false;
            if (!present) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 masters require their actual Source public declaration");
            qa_cvars_edit_command edit={.kind=QA_CVARS_EDIT_SET,.name="public",.value="1"};
            if (!qa_console_cvar_apply(source.console,&source.command,&edit,error)) return false;
        }
        qa_net_address masters[8]; size_t count=0,maximum=qw?8:7;
        for (size_t i=1;i<argc && i<=maximum;++i) {
            if (qw && !strcmp(argv[i],"none")) break;
            qa_error local={0}; qa_net_address address;
            if (!qa_net_address_resolve(argv[i],qw?27000:27900,4,&address,&local)) {
                char message[768]; snprintf(message,sizeof(message),"Bad master address %.255s: %.400s\n",argv[i],local.message);
                emit(call,message); continue;
            }
            uint8_t ping[16]; qa_net_writer writer; qa_net_writer_init(&writer,ping,sizeof(ping),error);
            if (qw) { qa_net_write_u8(&writer,107); qa_net_write_u8(&writer,0); }
            else if (!qa_q2_oob_write(&writer,"ping")) return false;
            if (writer.failed || !send(context,&address,(qa_bytes){ping,qa_net_writer_size(&writer)},error)) return false;
            char formatted[256],message[320];
            if (!qa_net_address_format(&address,formatted,sizeof(formatted),error)) return false;
            snprintf(message,sizeof(message),"Master server at %s\nSending a ping.\n",formatted); emit(call,message);
            masters[count++]=address;
        }
        return qa_server_admin_source_masters(admin,call->context.dialect,masters,count,error) &&
            qa_server_admin_request_heartbeat(admin,error);
    }
    if (!strcmp(name, "heartbeat")) return qa_server_admin_request_heartbeat(admin,error);
    if (!strcmp(name, "maprotation")) return qa_server_admin_rotation(admin, argv + 1, argc - 1, false, error);
    if (!strcmp(name, "nextmap")) {
        qa_application_map_view map; bool rotated; qa_application *application=f->application;
        if (call->console!=qa_application_console(f->application) &&
            !frontend_config_store_server_invocation_application(f->config_store,call,&application,error)) return false;
        return qa_server_admin_next_map(admin,
            qa_application_map_read(application, &map) ? map.name : NULL, &rotated, error);
    }
    *handled=false; return true;
}
static bool operator_filters_save(void *context,qa_error *error)
{ return save_filters(context,error); }
static bool operator_command(qa_frontend_network *n,const qa_command_invocation *call,
    size_t skip,bool *handled,qa_error *error)
{
    qa_application *application=n->frontend->application;
    if (call->context.owner &&
        !frontend_config_store_server_invocation_application(n->frontend->config_store,call,&application,error)) return false;
    qa_application *previous=n->operator_application; n->operator_application=application;
    bool ok=frontend_network_source_admin_dispatch(n->frontend,n->admin,n,send_address,
        operator_filters_save,call,skip,handled,error);
    n->operator_application=previous; return ok;
}
static const char *const source_admin_names[]={"sv","addip","removeip","listip","writeip",
    "setmaster","heartbeat","maprotation","nextmap","addlrconcmd","dellrconcmd","listlrconcmds"};
static bool source_admin_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    qa_frontend *f=context; qa_application_startup_source source;
    if (!f || !call || !call->argc ||
        !frontend_config_store_server_invocation_read(f->config_store,call,&source,error) ||
        source.console!=call->console)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source operator command lost its actual hosted console");
    size_t skip=!strcmp(call->argv[0],"sv")?1:0;
    if (skip && call->argc<2) { emit(call,"Usage: sv <command>\n"); return true; }
    bool handled=false;
    if (!frontend_config_store_admin_pending(f->config_store) &&
        f->network && f->network->runtime && f->network->admin) {
        if (!operator_command(f->network,call,skip,&handled,error)) return false;
    } else if (!frontend_config_store_admin_dispatch(f->config_store,call,skip,&handled,error)) return false;
    return handled || qa_application_source_command(f->application,call,error);
}
static void source_admin_span(qa_console_dialect dialect,size_t *first,size_t *count)
{
    bool q2=dialect==QA_CONSOLE_Q2 || dialect==QA_CONSOLE_Q2_RERELEASE;
    *first=dialect==QA_CONSOLE_Q3?6:q2?0:1;
    *count=dialect==QA_CONSOLE_Q1?0:dialect==QA_CONSOLE_Q3?1:q2?12:8;
}
bool frontend_network_source_admin_binding(qa_frontend *f,const qa_console *console,const qa_command_context *command,
    const char *name,qa_command_handler *handler,void **user)
{
    if (!f || !console || !command || !command->owner || !command->cvar_view || !name || !handler || !user) return false;
    size_t first,count; source_admin_span(command->dialect,&first,&count);
    for (size_t i=0;i<count;++i) if (!strcmp(name,source_admin_names[first+i])) {
        *handler=source_admin_command; *user=f; return true;
    }
    return false;
}
static bool source_admin_custody(qa_frontend *f,qa_console *console,const qa_command_context *command,uint64_t owner,
    size_t *registered,bool install,qa_error *error)
{
    if (!f || !console || !command || command->owner!=owner || !command->cvar_view || !owner || !registered)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source administration handlers require their physical lifetime");
    size_t first,count; source_admin_span(command->dialect,&first,&count);
    *registered=0;
    for (size_t i=0;i<count;++i) {
        const char *name=source_admin_names[first+i];
        qa_command_handler expected=NULL,actual=NULL; void *binding=NULL,*user=NULL; uint64_t lifetime=0;
        if (!frontend_network_source_admin_binding(f,console,command,name,&expected,&binding))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source administration declaration disappeared");
        if (qa_console_registration_read_context(console,command,name,owner,&lifetime,&actual,&user)) {
            if (lifetime!=owner || actual!=expected || user!=binding)
                return frontend_fail(error,QA_ERROR_FORMAT,"Source administration callback has another physical owner");
        } else if (install) {
            if (!qa_console_register_context(console,command,name,"Operate the actual Source network service",
                owner,owner,true,expected,binding,error)) return false;
        } else continue;
        ++*registered;
    }
    return true;
}
bool frontend_network_source_admin_bind(qa_frontend *f,qa_console *console,const qa_command_context *command,uint64_t owner,
    size_t *registered,qa_error *error)
{ return source_admin_custody(f,console,command,owner,registered,true,error); }
bool frontend_network_source_admin_adopt(qa_frontend *f,qa_console *console,const qa_command_context *command,uint64_t owner,
    size_t *registered,qa_error *error)
{ return source_admin_custody(f,console,command,owner,registered,false,error); }
void frontend_network_source_admin_unbind(qa_console *console,const qa_command_context *command,uint64_t owner,size_t registered)
{
    if (!console || !command || command->owner!=owner || !command->cvar_view || !registered) return;
    size_t first,count; source_admin_span(command->dialect,&first,&count);
    if (registered>count) return;
    for (size_t i=0;i<count;++i) {
        uint64_t lifetime=0; qa_command_handler handler=NULL; void *user=NULL;
        if (qa_console_registration_read_context(console,command,source_admin_names[first+i],owner,&lifetime,&handler,&user) &&
            lifetime==owner && handler==source_admin_command)
            qa_console_unregister_context(console,command,source_admin_names[first+i],owner);
    }
}
static bool command(void *context, const qa_command_invocation *call, qa_error *error)
{
    qa_frontend_network *n = context; const char *name = call->argv[0];
    if (!strcmp(name, "connect") || !strcmp(name, "reconnect") || !strcmp(name, "disconnect"))
        return client_attempt_queue(n, call, error);
    if (!strcmp(name, "localservers") || !strcmp(name, "globalservers") || !strcmp(name, "ping") || !strcmp(name, "serverstatus"))
        return q3_browser_command(n, call, error);
    if (!strcmp(name, "serverlist")) {
        uint32_t indices[2048]; size_t count;
        qa_browser_filter filter = {.text = call->argc > 1 ? call->argv[1] : NULL, .sort = QA_BROWSER_PING};
        if (!qa_server_browser_list(n->browser, &filter, indices, 2048, &count, error)) return false;
        for (size_t i = 0; i < count && i < 2048; ++i) {
            qa_server_entry entry; char address[256], line[sizeof(entry.name)+sizeof(entry.map)+sizeof(address)+96];
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
            return qa_server_browser_master_http(n->browser, call->argv[1], protocol, n->frontend->wall_time_ns, error);
        qa_net_address address;
        if (!qa_net_address_resolve(call->argv[1], n->frontend->options.network_port, 0, &address, error)) return false;
        if (!strcmp(name, "serverfavorite")) return qa_server_browser_add(n->browser, &address, protocol, QA_SERVER_FAVORITE, error) && save_favorites(n, error);
        if (!strcmp(name, "servermaster")) return qa_server_browser_master_udp(n->browser, &address, protocol,
            n->frontend->wall_time_ns, UINT64_C(5000000000), error);
        return qa_server_browser_query(n->browser, &address, protocol, false, n->frontend->wall_time_ns, UINT64_C(5000000000), error);
    }
    bool handled=false;
    if (!operator_command(n,call,0,&handled,error)) return false;
    if (handled) return true;
    if (!strcmp(name, "download")) {
        if (call->argc < 4 || call->argc > 5) return frontend_fail(error, QA_ERROR_ARGUMENT, "usage: download path url bytes [resume-nonce]");
        qa_download_request request = {.path = call->argv[1], .exact_length = true};
        if (!unsigned_text(call->argv[3], &request.expected_bytes, error)) return false;
        request.maximum_bytes = request.expected_bytes; request.stage_nonce = ++n->nonce;
        if (call->argc == 5) { request.resume = true; if (!unsigned_text(call->argv[4], &request.stage_nonce, error)) return false; }
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
bool frontend_network_declarations(qa_cvars *cvars,qa_error *error)
{
    return qa_cvars_register(cvars,"rcon_password","",0,NETWORK_OWNER,
        "Remote administrator password",error) &&
        qa_cvars_register(cvars,"rcon_limited_password","",0,NETWORK_OWNER,
            "Limited remote administrator password",error);
}
static bool detached_transport(const qa_net_address *,qa_net_transport **,qa_error *);
static bool network_create(qa_frontend *f,const qa_frontend *active,qa_error *error)
{
    if (!f->options.network_host && !f->options.network_connect && !f->options.dedicated)
        f->options.network_protocol = (qa_net_protocol_id){.kind = QA_NET_UNIFIED_1};
    if (f->network) return q2_local_groups_prepare(f->network,error);
    if ((f->options.network_connect && f->options.network_protocol.kind != QA_NET_Q3_68 &&
         !q2_host_protocol(f->options.network_protocol) && !q1_client_protocol(f->options.network_protocol) &&
         f->options.network_protocol.kind!=QA_NET_UNIFIED_1) ||
        (f->options.network_host && f->options.network_protocol.kind != QA_NET_Q3_68 &&
         !q2_host_protocol(f->options.network_protocol) && f->options.network_protocol.kind!=QA_NET_UNIFIED_1 &&
         (!nq_host_protocol(f->options.network_protocol) &&
          (f->options.network_protocol.kind != QA_NET_QW28 ||
           f->options.network_protocol.flags || f->options.network_protocol.revision))))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "selected connection requires its complete original/unified signon and prediction producer");
    qa_frontend_network *n = calloc(1, sizeof(*n));
    if (!n) return frontend_fail(error, QA_ERROR_MEMORY, "allocating network frontend owner");
    n->input_serial=++next_input_serial;
    n->frontend = f; n->detached_transport=active!=NULL; n->nonce = qa_platform_time_ns();
    for (uint32_t i = 0; i < f->options.seats; ++i)
        n->q3_clients[i] = (frontend_q3_client){.network = n, .frontend = f, .physical = i,
            .seat = {NETWORK_OWNER, i}, .q3_sensitivity = 1};
    n->rotation_random = (uint32_t)n->nonce ^ (uint32_t)(n->nonce >> 32); f->network = n;
    n->q3_clients[0].q3_client_requested = frontend_network_remote(f); n->q3_clients[0].q3_sensitivity = 1;
    if (client_target_selected(n)) {
        if (strlen(f->options.network_connect)>=sizeof(n->client_server)) {
            frontend_fail(error,QA_ERROR_ARGUMENT,"Remote server name exceeds its actual constructor storage"); goto failed;
        }
        memcpy(n->client_server,f->options.network_connect,strlen(f->options.network_connect)+1);
    }
    qa_net_udp_options udp = {.bind = {.kind = QA_NET_IPV4}, .limits = {65507, 256}, .broadcast = true};
    qa_net_address q2_remote={0};
    if(f->options.network_connect && (q2_host_protocol(f->options.network_protocol) || q1_client_protocol(f->options.network_protocol) ||
        f->options.network_protocol.kind==QA_NET_UNIFIED_1)) {
        if(f->options.dedicated || f->options.seats!=1 ||
            !qa_net_address_resolve(f->options.network_connect,f->options.network_port,0,&q2_remote,error)) goto failed;
        if(q2_remote.kind!=QA_NET_IPV4 && q2_remote.kind!=QA_NET_IPV6) {
            frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 CLIENT requires its real native UDP endpoint"); goto failed;
        }
        udp.bind.kind=q2_remote.kind; udp.ipv6_only=false; udp.broadcast=q2_remote.kind==QA_NET_IPV4;
    }
    if (n->q3_clients[0].q3_client_requested) {
        qa_actor_id actor; qa_net_address address;
        if (f->options.dedicated || f->options.seats != 1) {
            frontend_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 remote client requires one presentation seat"); goto failed;
        }
        if (!remote_player(f->application, &actor, error) ||
            !qa_application_network_q3_client_source(f->application, actor, &n->q3_clients[0].q3_cgame_owner, &n->q3_clients[0].q3_client_product, &n->q3_clients[0].q3_client_launch_seat, error) ||
            !qa_net_address_resolve(f->options.network_connect, f->options.network_port, 0, &address, error)) goto failed;
        if (address.kind != QA_NET_IPV4 && address.kind != QA_NET_IPV6) {
            frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q3 remote client requires the selected UDP address family"); goto failed;
        }
        udp.bind.kind = address.kind; udp.ipv6_only = false;
        udp.broadcast = address.kind == QA_NET_IPV4;
        qa_q3_client_admission_begin(&n->q3_clients[0].q3_client_admission, &address, (uint16_t)n->rotation_random);
        n->q3_clients[0].q3_client_generation = qa_application_configuration_generation(f->application); n->q3_clients[0].q3_client_epoch = 1;
        if (!qa_q3_prediction_scene_create(n->q3_clients[0].q3_client_product, &n->q3_clients[0].q3_prediction_scene, error)) goto failed;
        frontend_remote_input_options input_options = client_input_options(&n->q3_clients[0]);
        if (!frontend_remote_input_create(&input_options, &n->q3_clients[0].q3_input, error)) goto failed;
        qa_application_q3_client_context receiver;
        if (!qa_application_q3_remote_context_read(f->application,n->q3_clients[0].q3_cgame_owner,n->q3_clients[0].q3_client_launch_seat,&receiver,error)) goto failed;
        client_cvars_bind(&n->q3_clients[0], receiver.cvars);
        if (receiver.native_source) {
            qa_application_control_prediction_configuration initial;
            if (!qa_application_control_prediction_read(f->application,actor,&initial,error) ||
                !frontend_network_predictor_create(f,&receiver,&initial,&n->q3_clients[0].q3_predictor,error)) goto failed;
        }
        n->composition = n->q3_clients[0].q3_client_generation;
    }
    if (f->options.network_host) {
        if (!qa_net_address_parse(f->options.network_host, f->options.network_port, false, &udp.bind, error)) goto failed;
        if (udp.bind.kind != QA_NET_IPV4 && udp.bind.kind != QA_NET_IPV6)
            { frontend_fail(error, QA_ERROR_UNSUPPORTED, "hosting transport requires the admitted UDP address family"); goto failed; }
        udp.ipv6_only = udp.bind.kind == QA_NET_IPV6; udp.broadcast = udp.bind.kind == QA_NET_IPV4;
        if (f->options.network_protocol.kind == QA_NET_Q3_68) {
            qa_q3_admission_hooks hooks = {.context = n, .random = random_rotation, .send = send_address,
                .admit = q3_admit, .query = q3_query, .authorize = q3_authorize, .drop_bot = q3_drop_bot,
                .enabled = q3_admission_enabled, .print = q3_authorization_print};
            if (!qa_q3_server_admission_create(&hooks, &n->q3_admission, error)) goto failed;
            qa_q3_server_authorization_bindings authorization = q3_authorization_bindings(n);
            if (!qa_q3_server_authorization_create(&authorization, &n->q3_authorization, error)) goto failed;
            n->q3_server_id = n->q3_restarted_server_id = 1; n->q3_checksum_feed = (int32_t)random_rotation(n);
            if (!q3_prepare(n, error)) goto failed;
            qa_actor_owner owner; qa_q3_product product; qa_q3_server_world world;
            qa_application_network_q3_package_view packages;
            if (!qa_application_network_q3_owner(f->application, &owner, &product, error) ||
                !frontend_q3_packages_view(n->q3_packages, &packages, error) ||
                !qa_application_network_q3_round_prepare(f->application, owner, n->q3_server_id,
                    n->q3_restarted_server_id, n->q3_checksum_feed, &packages, &world, error)) goto failed;
        } else {
            n->composition = qa_application_configuration_generation(f->application);
        }
    }
    qa_net_transport *transport = NULL;
    uint32_t clients=q2_host_protocol(f->options.network_protocol)?256:64,source_clients=0;
    if(f->options.network_host && q2_host_protocol(f->options.network_protocol)) {
        qa_application_network_q2_host source;
        if(!qa_application_network_q2_host_source(f->application,f->options.network_protocol,&source,error)) goto failed;
        if(!source.client_slots || source.client_slots>256) goto failed;
        source_clients=source.client_slots; clients=256;
    }
    if(f->options.network_host && f->options.network_protocol.kind==QA_NET_UNIFIED_1) {
        application_unified_source source;
        if(!application_unified_source_read(f->application,&source,error) ||
            !source.max_clients || source.max_clients>256) goto failed;
        clients=source.max_clients+8;
        n->unified_map_revision=source.map_revision;
    }
    qa_network_options options = {.owner = NETWORK_OWNER, .clients = clients, .packets_per_pump = 256,
        .timeout_ns = f->options.network_connect ? UINT64_C(120000000000) :
            f->options.network_host && (nq_host_protocol(f->options.network_protocol) || f->options.network_protocol.kind == QA_NET_QW28) ?
            UINT64_C(65000000000) : UINT64_C(30000000000),
        .hooks = {.context = n, .admit = admit, .controlled = controlled,
        .command = remote_command, .disconnected = disconnected, .connectionless = connectionless,
        .q3_source_command = remote_q3_command, .commands = remote_qw_commands}};
    options.hooks.reconnect = reconnect;
    options.hooks.nq_source_command = remote_nq_command;
    options.hooks.unified_input=unified_source_input;
    if (active && active->network) {
        if (!detached_transport(qa_network_local_address(active->network->runtime),&transport,error)) goto failed;
    } else if (!qa_net_udp_open(&udp,&transport,error)) goto failed;
    if((f->options.network_host || f->options.network_connect) && f->options.network_protocol.kind==QA_NET_Q2KEX_2023) {
        if(f->options.network_host && source_clients>UINT8_MAX) {
            qa_net_transport_close(transport);
            frontend_fail(error,QA_ERROR_ARGUMENT,"KEX lobby cannot represent actual Source maxClients"); goto failed;
        }
        qa_kex_lan_options lobby={.host=f->options.network_host!=NULL,.max_players=(uint8_t)source_clients,
            .local_players=f->options.network_host?0:1,.name="Quake II",.server=q2_remote};
        qa_kex_transport_hooks hooks={n,kex_connectionless}; qa_net_transport *wrapped=NULL;
        if(!qa_kex_transport_open(transport,&lobby,&hooks,&wrapped,&n->kex_transport,error)) {
            qa_net_transport_close(transport); goto failed;
        }
        transport=wrapped;
    }
    if (!f->options.network_connect && !f->options.dedicated) {
        qa_net_transport *local = NULL, *host = NULL;
        qa_net_limits loopback_limits = udp.limits;
        if (f->options.network_protocol.kind == QA_NET_UNIFIED_1)
            loopback_limits.datagram_bytes = qa_unified_limits_default().datagram_bytes;
        if (!qa_net_loopback_create(loopback_limits, &n->loopback, error) ||
            !qa_net_loopback_bind(n->loopback, "server", &local, error)) {
            qa_net_transport_close(transport); goto failed;
        }
        n->loopback_server = *qa_net_transport_address(local);
        if (!qa_net_host_transport_create(transport, local, &host, error)) {
            qa_net_transport_close(local); qa_net_transport_close(transport); goto failed;
        }
        transport = host;
    }
    if (!qa_network_create(transport, &options, &n->runtime, error)) { qa_net_transport_close(transport); goto failed; }
    if (n->q3_clients[0].q3_client_requested) n->q3_clients[0].runtime = n->runtime;
    if (!qa_net_interfaces_capture(&n->interfaces,error)) goto failed;
    if (f->options.network_host && nq_host_protocol(f->options.network_protocol) &&
        !frontend_nq_create(f, n->runtime, &n->composition, &n->nq_host, error)) goto failed;
    qa_browser_hooks browser = {.context = n, .send = send_address, .local = local_address,
        .changed=browser_changed,.master_complete=browser_master_complete};
    qa_admin_options admin;
    if (!admin_options(n,&admin,error)) goto failed;
    if (!qa_server_browser_create(frontend_tools_http(f), 16384, &browser, &n->browser, error) ||
        !qa_server_admin_create(&admin, &n->admin, error) || !qa_fs_root_open(f->options.application.user_root, &n->preferences, error)) goto failed;
    qa_fs_file *favorites = NULL; qa_fs_identity identity; qa_buffer bytes = {0}; qa_error local = {0};
    if (qa_fs_root_file_open(n->preferences, "network/favorites.bin", &favorites, &identity, &local)) {
        bool ok = qa_fs_file_read_snapshot(favorites, &identity, &bytes, error) &&
            qa_server_browser_restore(n->browser, (qa_bytes){bytes.data, bytes.size}, error);
        qa_fs_file_close(favorites); qa_buffer_free(&bytes); if (!ok) goto failed;
    } else if (local.code != QA_ERROR_NOT_FOUND) { if (error) *error = local; goto failed; }
    favorites=NULL; local=(qa_error){0};
    if (qa_fs_root_file_open(n->preferences,"network/filters.bin",&favorites,&identity,&local)) {
        bool ok=qa_fs_file_read_snapshot(favorites,&identity,&bytes,error) &&
            qa_server_admin_restore_filters(n->admin,(qa_bytes){bytes.data,bytes.size},error);
        qa_fs_file_close(favorites); qa_buffer_free(&bytes); if (!ok) goto failed;
    } else if (local.code!=QA_ERROR_NOT_FOUND) { if (error) *error=local; goto failed; }
    if(!menu_preferences_load(n,error)) goto failed;
    frontend_q3_browser_options ui_browser = browser_options(n);
    if (!frontend_q3_browser_create(&ui_browser, &n->q3_browser, error) ||
        !frontend_q3_browser_load_cache(n->q3_browser, error)) goto failed;
    qa_cvars *cvars = qa_application_cvars(f->application);
    const qa_cvar_view *password_view=qa_cvars_find(cvars,"rcon_password");
    const qa_cvar_view *limited_view=qa_cvars_find(cvars,"rcon_limited_password");
    if(!password_view || !limited_view || password_view->owner!=NETWORK_OWNER || limited_view->owner!=NETWORK_OWNER) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Network fields were not declared before the actual ENGINE edit"); goto failed;
    }
    n->registered = true;
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!qa_console_register_owned(qa_application_console(f->application), names[i], "Shared network service", 0,
            NETWORK_OWNER, true, command, n, error)) goto failed;
    if (f->options.network_host && f->options.network_protocol.kind == QA_NET_QW28 &&
        !frontend_qw_create(f, n->runtime, n->admin, &n->composition, &n->qw_host, error)) goto failed;
    if(f->options.network_host && q2_host_protocol(f->options.network_protocol)) {
        frontend_network_q2_host_options host={.frontend=f,.runtime=n->runtime,.admin=n->admin,
            .protocol=f->options.network_protocol,.composition=n->composition,.context=n,
            .current=q2_host_current,.random=random_rotation,
            .lobby=n->kex_transport?qa_kex_transport_lobby(n->kex_transport):NULL,.transport=n->kex_transport};
        if(!frontend_network_q2_host_create(&host,&n->q2_host,error)) goto failed;
        if(!q2_timeout_sync(n,error)) goto failed;
    }
    if(f->options.network_host && f->options.network_protocol.kind==QA_NET_UNIFIED_1) {
        frontend_network_unified_options unified={.frontend=f,.runtime=n->runtime,.server=true,
            .seat_owner=QA_NETWORK_COMMAND_OWNER,.remote_seat_base=256,.context=n,.current=unified_current,
            .local_seat=local_server_seat};
        if(!frontend_network_unified_create(&unified,&n->unified,error)) goto failed;
    }
    if(!q2_local_groups_prepare(n,error)) goto failed;
    if(f->options.network_connect && q2_host_protocol(f->options.network_protocol)) {
        frontend_network_q2_client_options client={.frontend=f,.runtime=n->runtime,.remote=q2_remote,
            .protocol=f->options.network_protocol,.qport=(uint16_t)n->rotation_random,.physical_seat=0,
            .selected=frontend_product_current(f)->id,
            .context=n,.current=q2_client_current,.download_stage=q2_download_stage,.restore_stage=q2_restore_stage,
            .lobby=n->kex_transport?qa_kex_transport_lobby(n->kex_transport):NULL};
        if(!frontend_config_store_client_profile(f->config_store, client.selected, &client.profile, error) ||
            !frontend_network_q2_client_create(&client,&n->q2_client_owner,error)) goto failed;
    }
    if(f->options.network_connect && q1_client_protocol(f->options.network_protocol) &&
        !q1_client_create(n,&q2_remote,error)) goto failed;
    if(f->options.network_connect && f->options.network_protocol.kind==QA_NET_UNIFIED_1) {
        const qa_product *selected=frontend_product_current(f);
        if(!selected) {
            frontend_fail(error,QA_ERROR_ARGUMENT,
                "Unified connection requires --game PRODUCT selecting an installed local client profile");
            goto failed;
        }
        frontend_network_unified_client_options client_options={.frontend=f,.runtime=n->runtime,.remote=q2_remote,
            .physical_seat=0,.seat={QA_NETWORK_COMMAND_OWNER,0},.selected=selected->id,
            .context=n,.current=unified_client_current,
            .disconnected=unified_client_disconnected};
        if(!frontend_config_store_client_profile(f->config_store,selected->id,&client_options.profile,error) ||
            !frontend_config_store_neutral_pending_options(f->config_store,0,&client_options.configuration,error)) goto failed;
        if(!frontend_network_unified_client_create(&client_options,&n->unified_client_service,error)) {
            if(!n->unified_client_service)
                (void)frontend_config_store_neutral_options_cancel(f->config_store,&client_options.configuration,NULL);
            goto failed;
        }
        if(!unified_tick(n,error)) goto failed;
    }
    if (!client_drain(&n->q3_clients[0], false, error)) goto failed;
    /* Detached candidates acquire the live socket at publication. The ordinary
     * network pump then adopts pending Source administration on that socket. */
    if (!n->detached_transport && !frontend_config_store_admin_adopt(f->config_store,error)) goto failed;
    return true;
failed:
    (void)frontend_network_destroy(f, NULL); return false;
}

bool frontend_network_create(qa_frontend *f,qa_error *error)
{ return network_create(f,NULL,error); }
bool frontend_network_create_detached(qa_frontend *f,const qa_frontend *active,qa_error *error)
{
    if (!active || !active->network)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Network rebuild needs its retained native socket owner");
    return network_create(f,active,error);
}
bool frontend_network_rebuild_ready(const qa_frontend *candidate,const qa_frontend *active,qa_error *error)
{
    const qa_frontend_network *next=candidate?candidate->network:NULL;
    const qa_frontend_network *previous=active?active->network:NULL;
    if (!next || !previous || next->frontend!=candidate || previous->frontend!=active ||
        !next->detached_transport || previous->detached_transport || next->busy || previous->busy ||
        !qa_network_callbacks_idle(next->runtime) || !qa_network_callbacks_idle(previous->runtime) ||
        !qa_net_address_equal(qa_network_local_address(next->runtime),qa_network_local_address(previous->runtime),true))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Network rebuild lost its actual idle socket handoff");
    return qa_network_source_publication_ready(next->runtime,error) &&
        frontend_network_q2_client_publication_ready(next->q2_client_owner,error) &&
        frontend_network_q1_client_publication_ready(next->q1_client_owner,error) &&
        frontend_network_unified_client_publication_ready(next->unified_client_service,error) &&
        frontend_network_q2_host_publication_ready(next->q2_host,error);
}

static bool detached_send(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *error)
{
    (void)context; (void)to; (void)bytes;
    return frontend_fail(error, QA_ERROR_ARGUMENT, "detached network candidate has no published transport");
}
static bool detached_collect(void *context, uint64_t now, qa_net_transport_event *event, qa_error *error)
{ (void)context; (void)now; (void)error; *event = (qa_net_transport_event){0}; return true; }
static void detached_close(void *context) { (void)context; }
static bool detached_ready(const void *context) { (void)context; return false; }
static bool detached_transport(const qa_net_address *address, qa_net_transport **out, qa_error *error)
{
    const qa_net_transport_ops ops = {.send=detached_send, .collect=detached_collect,
        .close=detached_close, .ready=detached_ready};
    return qa_net_transport_create(address, (qa_net_limits){65507, 256}, &ops, NULL, out, error);
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
static void client_attempts_dispose(qa_frontend_network *n)
{
    while (n->q3_attempts) {
        frontend_q3_attempt *next = n->q3_attempts->next; free(n->q3_attempts); n->q3_attempts = next;
    }
    n->q3_attempt_tail = NULL; n->q3_attempt_count = 0;
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
    bool nq = n->nq_host != NULL, qw = n->qw_host != NULL,
        q2=n->q2_host!=NULL,
        unified=n->unified!=NULL && n->frontend->options.network_host!=NULL;
    if ((n->round && !finishing_round) || n->q3_reconnect || n->q3_clients[0].q3_client_initializing || (n->downloads && !n->content) ||
        ((unsigned)hosting + (unsigned)nq + (unsigned)qw + (unsigned)unified + (unsigned)q2 > 1) ||
        (!(hosting || nq || qw || unified || q2) && n->frontend->options.network_host) ||
        ((hosting || nq || qw || unified || q2) && !n->frontend->options.network_host && !n->loopback) ||
        n->q3_clients[0].q3_client_requested != (frontend_network_remote(n->frontend) || n->local_clients[0].q3 != NULL) || !n->registered || n->q3_clients[0].q3_projection_epoch > 4 ||
        (n->q3_clients[0].q3_projection_epoch != 0 && n->q3_clients[0].q3_projection_epoch != 4))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "installed frontend network service lacks a complete continuation consumer");
    if(unified && (n->q3_clients[0].q3_client_requested || n->frontend->options.network_protocol.kind!=QA_NET_UNIFIED_1 ||
        n->frontend->options.network_protocol.flags || n->frontend->options.network_protocol.revision || n->q3_clients[0].q3_projection.owner))
        return frontend_fail(error,QA_ERROR_FORMAT,"Unified hosting continuation has another installed Source consumer");
    if (nq && (n->q3_clients[0].q3_client_requested || !nq_host_protocol(n->frontend->options.network_protocol) || n->q3_clients[0].q3_projection.owner))
        return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake hosting continuation has another installed dialect consumer");
    if (qw && (n->q3_clients[0].q3_client_requested || n->frontend->options.network_protocol.kind != QA_NET_QW28 ||
        n->frontend->options.network_protocol.flags || n->frontend->options.network_protocol.revision || n->q3_clients[0].q3_projection.owner))
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld hosting continuation has another installed dialect consumer");
    if (hosting) {
        if (frontend_network_remote(n->frontend) || n->frontend->options.network_protocol.kind != QA_NET_Q3_68 ||
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
            const qa_net_client *connection = qa_net_connections_get(qa_network_connections(n->runtime), p->client);
            bool local = connection && connection->attachment == QA_NET_LOCAL_SEAT;
            qa_application_network_player row;
            if (p->slot != i || !p->client.generation || p->client.owner != NETWORK_OWNER || p->client.slot >= 64 ||
                p->seat.owner != NETWORK_OWNER || (!local && p->seat.index != 64u + i) || p->product != source_product ||
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
    if (n->q3_attempt_count && !n->q3_clients[0].q3_client_requested) {
        if (!n->frontend->options.network_connect || !q1_client_protocol(n->frontend->options.network_protocol) ||
            n->frontend->options.dedicated || n->frontend->options.seats!=1)
            return frontend_fail(error,QA_ERROR_FORMAT,"Queued connection has no selected Q1 CLIENT constructor");
        for (const frontend_q3_attempt *request=n->q3_attempts;request;request=request->next)
            if (request->disconnect ? *request->server!=0 : *request->server==0)
                return frontend_fail(error,QA_ERROR_FORMAT,"Saved Q1 connection changed its canonical queued request");
    }
    if (frontend_network_remote(n->frontend) ? !*n->client_server : *n->client_server && !client_target_selected(n))
        return frontend_fail(error,QA_ERROR_FORMAT,"Remote target differs from its selected native CLIENT constructor");
    for (uint32_t physical = 0; physical < n->frontend->options.seats; ++physical) {
        frontend_q3_client *client = n->q3_clients + physical;
        if (client->q3_client_requested) {
            qa_actor_id actor; qa_actor_owner owner; qa_q3_product product; uint32_t seat;
            qa_application_q3_remote_source retained_source;
            if (n->frontend->options.dedicated ||
                !client_player(client, &actor, error) ||
                !qa_application_network_q3_client_source(app, actor, &owner, &product, &seat, error) ||
                owner != client->q3_cgame_owner || product != client->q3_client_product || seat != client->q3_client_launch_seat ||
                client->q3_client_generation != qa_application_configuration_generation(app) || !client->q3_client_epoch ||
                !client->q3_prediction_scene || !client->q3_input ||
                (!client->q3_scene_frame_valid && client->q3_scene_frame) ||
                (client->q3_scene_frame_valid && ((!client->q3_client_active && !client->q3_client_retiring) ||
                    client->q3_scene_frame > n->frontend->frame_number)) ||
                (client->q3_client_admission.address.kind != QA_NET_LOOPBACK &&
                 ((client->q3_client_admission.address.kind != QA_NET_IPV4 && client->q3_client_admission.address.kind != QA_NET_IPV6) ||
                  !client->q3_client_admission.address.port)) || client->q3_client.slot >= 64 ||
                (client->q3_client_attached && (!client->q3_client.generation || client->q3_client_admission.phase != QA_Q3_ADMITTED)) ||
                (client->q3_client_decoded && (!client->q3_client_attached || !client->q3_client_restart_generation)) ||
                (client->q3_client_gamestate && !client->q3_client_decoded) ||
                (client->q3_client_gamestate && !client->q3_initial_tuple) ||
                (client->q3_initial_tuple && (!client->q3_client_decoded || client->q3_initial_message < 0)) ||
                (!client->q3_initial_tuple && (client->q3_initial_message || client->q3_initial_command)) ||
                (!client->q3_reliable_receipt && client->q3_reliable_receipt_sequence) ||
                (client->q3_client_rebind && (!client->q3_client_previous_epoch || client->q3_client_previous_epoch >= client->q3_client_epoch || client->q3_client_decoded)) ||
                (client->q3_client_closed && (!client->q3_client_retiring || client->q3_client_attached ||
                    client->q3_client_admission.phase != QA_Q3_DISCONNECTED || client->q3_client_decoded)) ||
                client->q3_client_previous.slot >= 64 ||
                (client->q3_client_previous_epoch && client->q3_client_previous_epoch >= client->q3_client_epoch) ||
                (client->q3_client_previous.generation && (!client->q3_client_previous_epoch ||
                    (client->q3_client_attached && qa_net_client_id_equal(client->q3_client_previous, client->q3_client)))) ||
                (client->q3_client_entered && !client->q3_client_gamestate && !client->q3_client_retiring) ||
                (client->q3_client_active && (!client->q3_client_attached || !client->q3_client_gamestate || !client->q3_client_clock.active)))
                return frontend_fail(error, QA_ERROR_FORMAT, "remote Q3 continuation differs from its selected source and seat");
            if (!qa_application_q3_remote_source_read(app, client->q3_cgame_owner, client->q3_client_launch_seat,
                client->q3_client_rebind ? client->q3_client_previous_epoch : client->q3_client_epoch, &retained_source, error))
                return false;
            if (!frontend_network_predictor_bound(client->q3_predictor,n->frontend) ||
                (client->q3_predictor && client->q3_predictor_pending.data) ||
                ((client->q3_predictor || client->q3_predictor_pending.size) != retained_source.receiver.native_source))
                return frontend_fail(error,QA_ERROR_FORMAT,"Selected private predictor differs from its actual compiled CLIENT owner");
        } else if (client->q3_client_attach || client->q3_client_attached || client->q3_client_gamestate || client->q3_client_active || client->q3_projection.owner ||
            client->q3_client_epoch || client->q3_client_restart_generation || client->q3_client_decoded ||
            client->q3_client_previous.generation || client->q3_client_previous_epoch || client->q3_client_rebind ||
            client->q3_client_closed || *client->q3_client_message || client->q3_ui_client_number ||
            client->q3_scene_frame_valid || client->q3_scene_frame || client->q3_previous_presentation_time ||
            client->q3_initial_tuple || client->q3_initial_message || client->q3_initial_command ||
            client->q3_reliable_receipt || client->q3_reliable_receipt_sequence ||
            client->q3_reached_command_sequence || client->q3_reached_command.count || client->q3_command_present ||
            client->q3_client_content || client->q3_client_downloads || client->q3_prediction_scene || client->q3_input || client->q3_predictor ||
            client->q3_predictor_pending.data || client->q3_predictor_pending.size || client->q3_client_authorization ||
            client->q3_client_entered || client->q3_client_launch_seat || *client->q3_client_userinfo)
            return frontend_fail(error, QA_ERROR_FORMAT, "uninstalled remote source carries live client state");
        if (client->q3_projection.owner) {
            const char *definition = qa_strings_cstr(qa_session_strings(qa_application_session(app)), client->q3_projection.definition);
            if (client->q3_projection.owner != client->q3_cgame_owner || !definition || strcmp(definition, "qa.network.q3.remote-entity"))
                return frontend_fail(error, QA_ERROR_FORMAT, "remote entity projection has a foreign source definition");
        } else if (client->q3_projection.definition) return frontend_fail(error, QA_ERROR_FORMAT, "absent remote projection has a definition");
        const qa_actor_registry *actors = qa_session_actors(qa_application_session(app));
        for (size_t i = 0; i < QA_Q3_ENTITY_NONE; ++i) {
            qa_actor_id id = client->q3_projection.actors[i]; if (!id.registry) continue;
            if (i == QA_Q3_ENTITY_WORLD)
                return frontend_fail(error, QA_ERROR_FORMAT, "Remote WORLD identity cannot own a projected actor");
            const qa_actor_record *record = qa_actors_get(actors, id);
            if (!client->q3_projection.owner || !record || record->owner != client->q3_projection.owner ||
                record->definition != client->q3_projection.definition || record->has_source)
                return frontend_fail(error, QA_ERROR_FORMAT, "remote entity projection does not own its saved canonical actor");
            for (size_t j = 0; j < i; ++j) if (qa_actor_id_equal(id, client->q3_projection.actors[j]))
                return frontend_fail(error, QA_ERROR_FORMAT, "remote source entity numbers alias one canonical actor");
        }
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
static bool client_runtime_check(frontend_q3_client *receiver, const qa_net_client *client,
    bool complete_world, qa_error *error)
{
    qa_frontend_network *n = receiver->network;
    const qa_q3_client_peer *peer = qa_network_q3_client_view(receiver->runtime, client->id);
    if (!receiver->q3_client_attached || !qa_net_client_id_equal(client->id, receiver->q3_client) || !peer ||
        qa_q3_client_peer_product(peer) != receiver->q3_client_product ||
        !qa_net_address_equal(&client->endpoint, &receiver->q3_client_admission.address, true) ||
        client->composition != n->composition ||
        (client->phase >= QA_NET_PRIMED && !receiver->q3_client_gamestate) ||
        (receiver->q3_client_entered && !qa_q3_client_peer_usercmd_number(peer)))
        return frontend_fail(error, QA_ERROR_FORMAT, "frontend client state differs from its actual native connection owner");
    if (receiver->q3_client_decoded) {
        frontend_q3_content_view content;
        const qa_q3_gamestate *state = qa_q3_client_peer_gamestate(peer);
        bool staging=receiver->q3_native_restore && n->detached_transport && !complete_world;
        bool pending=frontend_q3_content_native_restore_pending(receiver->q3_client_content);
        bool read=staging && pending ? frontend_q3_content_native_restore_read(receiver->q3_client_content,&content,error) :
            frontend_q3_content_read(receiver->q3_client_content,&content,error);
        if (!read || (pending && !staging) ||
            !content.gamestate || state->client_number < 0 || state->client_number >= 64 ||
            state->client_number != content.gamestate->client_number ||
            state->checksum_feed != content.gamestate->checksum_feed ||
            (receiver->q3_client_gamestate && !staging && (frontend_q3_content_state(receiver->q3_client_content) != FRONTEND_Q3_CONTENT_MEDIA_READY ||
                !content.map || !frontend_q3_content_media_current(receiver->q3_client_content, error))) ||
            (receiver->q3_client_gamestate && qa_q3_client_downloads_active(receiver->q3_client_downloads)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 decoder differs from its actual private content and media cut");
    } else if (receiver->q3_client_gamestate || receiver->q3_client_content || receiver->q3_client_downloads)
        return frontend_fail(error, QA_ERROR_FORMAT, "Undecoded remote Q3 retains private content or receiver state");
    return true;
}
static bool network_runtime_check(qa_frontend_network *n, bool complete_world, bool finishing_round, qa_error *error)
{
    if (!network_metadata_check(n, n->q3_admission != NULL, finishing_round, error)) return false;
    if ((n->q3_authorization != NULL) != (n->q3_admission != NULL) ||
        !qa_q3_server_authorization_idle(n->q3_authorization))
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 hosting lost its actual idle authorization owner");
    for (uint32_t physical = 0; physical < n->frontend->options.seats; ++physical) {
        frontend_q3_client *client = n->q3_clients + physical;
        if (client->q3_client_decoded) {
            const qa_q3_client_peer *peer = qa_network_q3_client_view(client->runtime, client->q3_client);
            const qa_q3_gamestate *state = peer ? qa_q3_client_peer_gamestate(peer) : NULL;
            if (!state || client->q3_ui_client_number != state->client_number)
                return frontend_fail(error, QA_ERROR_FORMAT, "UI client number differs from its actual decoded connection");
        } else if (client->q3_ui_client_number)
            return frontend_fail(error, QA_ERROR_FORMAT, "Cold UI client number differs from its constructor continuation");
        if (client->q3_client_previous.generation &&
            qa_net_connections_get(qa_network_connections(client->runtime), client->q3_client_previous))
            return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 attempt retains its retired physical connection");
        if (!qa_q3_client_authorization_idle(client->q3_client_authorization) ||
            (client->q3_client_authorization && !client_authorization_current(client, error)) ||
            (!client->q3_client_authorization && (client->q3_client_attached || client->q3_client_admission.connect_packets)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Remote Q3 lost its actual client-static authorization continuation");
        if (client->q3_command_present || client->q3_reliable_receipt) {
            qa_network_q3_client_init counters;
            if (!qa_network_q3_client_init_read(client->runtime, client->q3_client, &counters, error) ||
                (client->q3_command_present && counters.last_executed_server_command != client->q3_reached_command_sequence))
                return frontend_fail(error, QA_ERROR_FORMAT, "Reached Q3 arguments differ from their actual native cursor");
            if (client->q3_reliable_receipt) {
                const qa_q3_client_peer *peer = qa_network_q3_client_view(client->runtime, client->q3_client);
                int32_t sequence = client->q3_reliable_receipt_sequence;
                if (!peer || sequence > qa_q3_client_peer_server_command_sequence(peer) ||
                    (client->q3_command_present && sequence != client->q3_reached_command_sequence) ||
                    (counters.last_executed_server_command != sequence &&
                     (client->q3_command_present || !qa_q3_client_peer_demo(peer) ||
                      (int64_t)sequence > (int64_t)qa_q3_client_peer_server_command_sequence(peer) - 64)))
                    return frontend_fail(error, QA_ERROR_FORMAT, "Q3 execution receipt differs from its actual native result");
            }
        }
    }
    for (uint32_t physical = 0; physical < n->frontend->options.seats; ++physical) {
        frontend_q3_client *receiver = n->local_clients[physical].q3;
        if (!receiver) continue;
        uint32_t local_cursor = 0; const qa_net_client *client; size_t local_count = 0;
        while (qa_net_connections_next(qa_network_connections(receiver->runtime), &local_cursor, &client)) {
            ++local_count;
            if (!client_runtime_check(receiver, client, complete_world, error)) return false;
        }
        if (local_count != (receiver->q3_client_attached ? 1u : 0u))
            return frontend_fail(error, QA_ERROR_FORMAT, "frontend and runtime installed connection inventories differ");
    }
    if (n->nq_host) return frontend_nq_qualified(n->nq_host, complete_world, error);
    if (n->qw_host) return frontend_qw_qualified(n->qw_host, complete_world, error);
    if(n->unified) return frontend_network_unified_qualified(n->unified,n->runtime,complete_world,error);
    if(n->q1_client_owner) return frontend_network_q1_client_qualified(n->q1_client_owner,n->runtime,complete_world,error);
    if(n->q2_client_owner) return frontend_network_q2_client_qualified(n->q2_client_owner,n->runtime,complete_world,error);
    if(n->q2_host) return frontend_network_q2_host_qualified(n->q2_host,n->runtime,complete_world,error);
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
                (client->attachment != QA_NET_REMOTE && client->attachment != QA_NET_LOCAL_SEAT) || client->protocol.kind != QA_NET_Q3_68 ||
                client->protocol.revision || client->protocol.flags || client->seat_count != 1 || client->seats[0].remote_index ||
                client->seats[0].seat.owner != p->seat.owner || client->seats[0].seat.index != p->seat.index ||
                client->composition != n->composition ||
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
        if (!client_runtime_check(n->q3_clients, client, complete_world, error)) return false;
    }
    size_t expected = n->q3_clients[0].q3_client_attached ? 1u : 0u;
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
    if(complete_world && (n->q3_clients[0].q3_native_restore || n->q3_clients[0].q3_download_pending.data || n->q3_clients[0].q3_predictor_pending.data))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Staged private prediction has not qualified its restored native service and snapshots");
    return !complete_world || network_q3_commands_valid(n, error);
}
static bool network_runtime_valid(qa_frontend_network *n, bool complete_world, qa_error *error)
{ return network_runtime_check(n, complete_world, false, error); }
void frontend_network_transport_exchange(qa_frontend *active, qa_frontend *candidate)
{
    if (!active->network) return;
    if (active->network->downloads) qa_downloads_handoff_publish(active->network->downloads, candidate->network->downloads);
    if (active->network->q3_clients[0].q3_client_downloads) qa_q3_client_downloads_handoff_publish(
        active->network->q3_clients[0].q3_client_downloads, candidate->network->q3_clients[0].q3_client_downloads);
    if(active->network->kex_browser)
        (void)frontend_kex_browser_handoff(active->network->kex_browser,candidate->network->kex_browser,NULL);
    if(active->network->kex_transport) {
        (void)qa_kex_transport_handoff(active->network->kex_transport,candidate->network->kex_transport,NULL);
        qa_network_transport_publish_retained(active->network->runtime,candidate->network->runtime);
    } else {
        qa_network_transport_exchange(active->network->runtime, candidate->network->runtime);
        qa_net_loopback *loopback = active->network->loopback;
        qa_net_address server = active->network->loopback_server;
        active->network->loopback = candidate->network->loopback;
        active->network->loopback_server = candidate->network->loopback_server;
        candidate->network->loopback = loopback;
        candidate->network->loopback_server = server;
    }
    active->network->detached_transport = true; candidate->network->detached_transport = false;
    frontend_network_q2_client_publish(candidate->network->q2_client_owner);
    frontend_network_q1_client_publish(candidate->network->q1_client_owner);
    frontend_network_unified_client_publish(candidate->network->unified_client_service);
    frontend_network_q2_host_publish_import(candidate->network->q2_host);
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
        if (n->frontend != fresh[i] || n->round || n->busy || n->q3_clients[0].q3_client_requested || n->q3_admission || n->nq_host || n->qw_host ||
            n->unified || n->q1_client_owner || n->q2_client_owner ||
            n->q2_host)
            return frontend_fail(error, QA_ERROR_FORMAT, "Fresh offline graph carries an admitted endpoint or source host");
        while(qa_net_connections_next(qa_network_connections(n->runtime),&cursor,&client))
            if(client->attachment!=QA_NET_LOCAL_SEAT || !n->q2_host)
                return frontend_fail(error,QA_ERROR_FORMAT,"Fresh offline graph carries a nonlocal transport recipient");
        if(n->q2_host && !frontend_network_q2_host_qualified(n->q2_host,n->runtime,true,error)) return false;
    }
    if (active->network && active->network->frontend != active)
        return frontend_fail(error, QA_ERROR_FORMAT, "Displaced endpoints borrow another physical frontend owner");
    if ((candidate->network != NULL) != (constructor->network != NULL))
        return frontend_fail(error, QA_ERROR_FORMAT, "Fresh offline publication lost its normal network constructor");
    return !candidate->network || frontend_network_rebuild_ready(candidate, constructor, error);
}
void frontend_network_publish_fresh(qa_frontend *active, qa_frontend *candidate, qa_frontend *constructor)
{
    (void)active;
    frontend_network_transport_exchange(constructor, candidate);
}
bool frontend_network_world_change_ready(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n = f->network;
    return !n || (!n->round && !n->busy && qa_q3_server_authorization_idle(n->q3_authorization) &&
        qa_q3_client_authorization_idle(n->q3_clients[0].q3_client_authorization) &&
        frontend_remote_q3_initial_idle(n->q3_clients[0].q3_initial) && frontend_remote_q3_modules_idle(n->q3_clients[0].q3_initial_modules) &&
        frontend_remote_q3_idle(f) && frontend_network_q1_client_idle(n->q1_client_owner) &&
        frontend_network_q2_client_idle(n->q2_client_owner) && frontend_remote_q2_idle(f) &&
        (frontend_network_q2_host_idle(n->q2_host) ||
            (n->detached_transport && frontend_network_q2_host_import_retirement_idle(n->q2_host))) &&
        frontend_network_unified_idle(n->unified) &&
        frontend_network_unified_client_idle(n->unified_client_service) && local_clients_idle(n) &&
        (!n->kex_transport || qa_kex_transport_idle(n->kex_transport)) &&
        (!n->kex_browser || frontend_kex_browser_idle(n->kex_browser)) &&
        frontend_nq_idle(n->nq_host) && frontend_qw_idle(n->qw_host) && qa_network_callbacks_idle(n->runtime)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "network callbacks must return before world publication");
}
bool frontend_network_local_groups_retire(qa_frontend *f,qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    if(!f) return frontend_fail(error,QA_ERROR_ARGUMENT,"Local group retirement requires its physical frontend");
    if (!n) return true;
    if (n->loopback && !f->options.network_host) {
        if (!frontend_network_unified_destroy(&n->unified, error) ||
            !local_clients_destroy(n, error)) return false;
    }
    return true;
}
bool frontend_network_component_drop(void *context,qa_actor_owner component,
    qa_actor_id actor,const char *reason,qa_error *error)
{
    qa_frontend *f=context; qa_frontend_network *n=f?f->network:NULL;
    if(!f || !component || !actor.registry || !reason ||
        (n && (n->busy || n->detached_transport || !qa_network_callbacks_idle(n->runtime))))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Component drop requires its returned real transport owner");
    const qa_net_client *destination=NULL;
    uint32_t cursor=0; const qa_net_client *client;
    while(n && qa_net_connections_next(qa_network_connections(n->runtime),&cursor,&client)) {
        for(size_t i=0;i<client->seat_count;++i) {
            qa_actor_id actual;
            if(!qa_application_remote_player_actor(f->application,client->id,client->seats[i].seat,&actual) ||
                !qa_actor_id_equal(actual,actor)) continue;
            if(destination && !qa_net_client_id_equal(destination->id,client->id))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Component player belongs to multiple real remote connections");
            destination=client;
        }
    }
    if(!destination) {
        for(uint32_t i=0;i<f->options.seats;++i) {
            uint32_t authored; qa_actor_id actual;
            if(frontend_seat_launch_id_read(f,i,&authored) && qa_application_player_actor(f->application,authored,&actual) &&
                qa_actor_id_equal(actual,actor)) return true;
        }
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Component player has no retained local or remote transport binding");
    }
    switch(destination->protocol.kind) {
    case QA_NET_UNIFIED_1:
        return n->unified && frontend_network_unified_close(n->unified,destination->id,reason,error);
    case QA_NET_NQ15: case QA_NET_FITZ666: case QA_NET_RMQ999:
        return qa_network_nq_server_drop(n->runtime,destination->id,reason,error);
    case QA_NET_QW28: case QA_NET_QW29:
        return qa_network_qw_server_drop(n->runtime,destination->id,reason,error);
    case QA_NET_Q2_34: case QA_NET_R1Q2_35: case QA_NET_Q2PRO_36: case QA_NET_Q2REPRO_1038:
    case QA_NET_Q2PRIVATE_4038: case QA_NET_Q2KEX_2023: case QA_NET_Q2KEX_DEMO_2022:
        return qa_network_q2_server_drop(n->runtime,destination->id,reason,f->wall_time_ns,error);
    case QA_NET_Q3_68:
        for(size_t i=0;i<64;++i) {
            frontend_q3_peer *peer=n->q3_peers+i;
            if(!peer->occupied || !qa_net_client_id_equal(peer->client,destination->id)) continue;
            if(peer->retiring) return true;
            if(!qa_network_q3_disconnect(n->runtime,peer->client,&peer->rate,n->q3_server_bit,reason,error)) return false;
            peer->retiring=true; snprintf(peer->reason,sizeof(peer->reason),"%s",reason); return true;
        }
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Component drop lost its actual Q3 source peer");
    default:
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Component transport needs its genuine protocol disconnect producer");
    }
}
static bool network_capture_ready(const qa_frontend *f, qa_error *error)
{
    if(!f) return false;
    const qa_frontend_network *n=f->network;
    if(!f->capture) {
        if(!n || !n->q2_host) return frontend_network_world_change_ready((qa_frontend *)f,error);
        return !n->round && !n->busy && qa_q3_server_authorization_idle(n->q3_authorization) &&
            qa_q3_client_authorization_idle(n->q3_clients[0].q3_client_authorization) &&
            frontend_remote_q3_initial_idle(n->q3_clients[0].q3_initial) && frontend_remote_q3_modules_idle(n->q3_clients[0].q3_initial_modules) &&
            frontend_remote_q3_idle((qa_frontend *)f) && frontend_network_q1_client_idle(n->q1_client_owner) &&
            frontend_network_q2_client_idle(n->q2_client_owner) && frontend_remote_q2_idle((qa_frontend *)f) &&
            frontend_network_unified_idle(n->unified) && frontend_network_unified_client_idle(n->unified_client_service) &&
            (!n->kex_transport || qa_kex_transport_idle(n->kex_transport)) &&
            (!n->kex_browser || frontend_kex_browser_idle(n->kex_browser)) &&
            frontend_nq_idle(n->nq_host) && frontend_qw_idle(n->qw_host) &&
            frontend_network_q2_host_capture_current(n->q2_host,error);
    }
    return !n || (n->frontend==f && !n->round && !n->busy &&
        qa_q3_server_authorization_idle(n->q3_authorization) &&
        qa_q3_client_authorization_idle(n->q3_clients[0].q3_client_authorization) &&
        qa_network_callbacks_idle(n->runtime) && frontend_nq_idle(n->nq_host) && frontend_qw_idle(n->qw_host) &&
        (!n->kex_transport || qa_kex_transport_idle(n->kex_transport)) &&
        (!n->kex_browser || frontend_kex_browser_idle(n->kex_browser)) &&
        frontend_network_q1_client_idle(n->q1_client_owner) &&
        frontend_network_q2_client_idle(n->q2_client_owner) && frontend_network_q2_host_capture_current(n->q2_host,error) &&
        frontend_remote_q3_capture_current(f,f->capture) &&
        (!n->q3_clients[0].q3_initial || frontend_remote_q3_initial_capture_current(n->q3_clients[0].q3_initial)) &&
        (!n->q3_clients[0].q3_initial_modules || frontend_remote_q3_modules_capture_returned(n->q3_clients[0].q3_initial_modules,error))) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Network capture lost its actual returned owners and retained asset roster");
}
bool frontend_network_content_visit(const qa_frontend *f, const qa_application *application,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!f || f->application != application || !visitor || !visitor->pool || !visitor->catalog || !visitor->view ||
        f->stepping || f->preparing || !network_capture_ready(f, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Network content inventory requires its actual idle application and holders");
    const qa_frontend_network *n = f->network;
    if (!n) return true;
    if ((n->q1_client_owner && !frontend_network_q1_client_content_visit(n->q1_client_owner,visitor,error)) ||
        !frontend_network_q2_client_content_visit(n->q2_client_owner,visitor,error) ||
        !frontend_network_q2_host_content_visit(n->q2_host,visitor,error)) return false;
    for (uint32_t physical = 0; physical < f->options.seats; ++physical)
        if (n->q3_clients[physical].q3_client_content &&
            !frontend_q3_content_visit(n->q3_clients[physical].q3_client_content,visitor,error)) return false;
    return true;
}
static bool prediction_map(frontend_q3_client *n, const qa_resource **map, qa_error *error)
{
    frontend_q3_content_view content;
    if (!frontend_q3_content_read(n->q3_client_content, &content, error)) return false;
    *map = content.map; return true;
}
static bool prediction_geometry(const qa_frontend *f, const qa_application_q3_client_context *receiver,
    const qa_collision_geometry **geometry, qa_trace_scratch **scratch, const qa_resource **map, bool *present, qa_error *error)
{
    if (receiver->native_source)
        return frontend_remote_q3_geometry_read(f, receiver, map, geometry, scratch, present, error);
    return frontend_source_role_geometry_read(f, receiver->receiver, QA_QVM_CGAME,
        receiver->seat, receiver->service_owner, geometry, scratch, map, present, error);
}
bool frontend_network_prediction_source_read(const qa_frontend *f,
    const qa_application_q3_client_context *receiver,
    frontend_network_prediction_source *out, bool *present, qa_error *error)
{
    if (!f || !out || !present) return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid remote prediction observation");
    memset(out, 0, sizeof(*out)); *present = false;
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    if (!n || !n->q3_client_attached || !n->q3_client_active || !n->q3_client_gamestate ||
        n->q3_client_retiring || n->q3_client_closed) return true;
    if (!qa_network_q3_client_live(n->runtime, n->q3_client))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote prediction lost its actual decoded connection");
    /* CL_GetSnapshot may reject every retained entry before the first retail
     * snapshot is admitted, even though the real connection is active. */
    if (!qa_q3_prediction_scene_read(n->q3_prediction_scene, &out->scene)) return true;
    if (!frontend_network_q3_client_context_read((qa_frontend *)f, n->q3_cgame_owner,
            n->q3_client_launch_seat, &out->receiver, error) ||
        !client_player(n, &out->viewer, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote prediction lost its actual decoded scene or viewing receiver");
    bool geometry_present = false; const qa_resource *map = NULL;
    if (!prediction_geometry(f, &out->receiver, &out->geometry, &out->scratch, &out->map, &geometry_present, error) ||
        !prediction_map(n, &map, error)) return false;
    if (!geometry_present || !out->geometry || !out->map || map != out->map)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote prediction lacks its actual private map and geometry");
    out->connection = n->q3_client; out->epoch = n->q3_client_epoch;
    out->restart_generation = n->q3_client_restart_generation;
    out->previous_presentation_time = n->q3_previous_presentation_time; *present = true; return true;
}
bool frontend_network_prediction_source_current(const qa_frontend *f,
    const frontend_network_prediction_source *source)
{
    if (!f || !source || !f->network) return false;
    frontend_q3_client *n = q3_client_receiver(f, &source->receiver);
    const qa_collision_geometry *geometry = NULL; const qa_resource *map = NULL, *prepared = NULL; bool present = false;
    qa_trace_scratch *scratch=NULL;
    qa_actor_id viewer = {0}; qa_error ignored = {0};
    return n && n->q3_client_attached && n->q3_client_active && n->q3_client_gamestate &&
        !n->q3_client_retiring && !n->q3_client_closed &&
        qa_net_client_id_equal(source->connection, n->q3_client) && source->epoch == n->q3_client_epoch &&
        source->restart_generation == n->q3_client_restart_generation &&
        source->previous_presentation_time == n->q3_previous_presentation_time &&
        qa_network_q3_client_live(n->runtime, n->q3_client) &&
        qa_q3_prediction_scene_current(n->q3_prediction_scene, &source->scene) &&
        frontend_network_q3_client_context_current((qa_frontend *)f, &source->receiver) &&
        source->receiver.source_milliseconds == n->q3_client_time &&
        source->receiver.receiver == n->q3_cgame_owner && source->receiver.seat == n->q3_client_launch_seat &&
        client_player(n, &viewer, &ignored) && qa_actor_id_equal(viewer, source->viewer) &&
        prediction_geometry(f, &source->receiver, &geometry, &scratch, &map, &present, &ignored) &&
        present && geometry == source->geometry && map == source->map &&
        prediction_map(n, &prepared, &ignored) && prepared == map;
}
bool frontend_network_prediction_entity_current(const qa_frontend *f,
    const frontend_network_prediction_source *source, const qa_q3_prediction_scene_entity_view *entity)
{
    return frontend_network_prediction_source_current(f, source) &&
        qa_q3_prediction_scene_entity_current(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene, entity);
}
bool frontend_network_prediction_acknowledgement(const qa_frontend *f,
    const frontend_network_prediction_source *source, bool *has_sequence, uint64_t *sequence,
    bool *history_unavailable, qa_error *error)
{
    return frontend_network_prediction_source_current(f, source) && source->scene.prediction_snapshot &&
        qa_network_q3_client_acknowledged_command_time(q3_client_receiver(f, &source->receiver)->runtime, source->connection,
            source->scene.prediction_snapshot->player.commandTime, has_sequence, sequence, history_unavailable, error) &&
        (frontend_network_prediction_source_current(f, source) ||
         frontend_fail(error, QA_ERROR_ARGUMENT, "Prediction acknowledgement lost its real source receipt"));
}
static bool prediction_snapshot_receipt(const qa_q3_snapshot *a, const qa_q3_snapshot *b)
{
    if (!a || !b) return a == b;
    return a->valid && b->valid && a->message_number == b->message_number &&
        a->server_time == b->server_time && a->flags == b->flags &&
        a->player.product == b->player.product && a->player.clientNum == b->player.clientNum;
}
static bool prediction_retail_current(const qa_frontend *f,
    const frontend_network_prediction_source *source, const frontend_remote_snapshots_view *retail)
{
    if (!retail || !frontend_remote_snapshots_current(retail->owner, retail) ||
        !frontend_network_prediction_source_current(f, source)) return false;
    const qa_native_q3_remote_client_basis *basis = &retail->source.basis;
    const qa_application_q3_client_context *a = &source->receiver, *b = &basis->client;
    return basis->application == f->application && basis->session == a->session &&
        qa_net_client_id_equal(basis->connection, source->connection) && basis->epoch == source->epoch &&
        basis->restart_generation == source->restart_generation && basis->map == source->map &&
        basis->geometry == source->geometry && qa_actor_id_equal(retail->source.publication.viewer, source->viewer) &&
        a->receiver == b->receiver && a->seat == b->seat && a->source_client == b->source_client &&
        a->service_owner == b->service_owner && a->frontend_lifetime == b->frontend_lifetime &&
        a->console == b->console && a->cvars == b->cvars && a->client_time_cvars == b->client_time_cvars &&
        a->client_time_owner == b->client_time_owner && a->native_source == b->native_source &&
        a->initialized == b->initialized && retail->time == source->scene.time &&
        retail->processed_message == source->scene.processed_snapshot &&
        retail->next_frame_teleport == source->scene.next_frame_teleport &&
        prediction_snapshot_receipt(retail->snapshot, source->scene.snapshot) &&
        prediction_snapshot_receipt(retail->next_snapshot, source->scene.next_snapshot);
}
bool frontend_network_prediction_teleport_feedback(qa_frontend *f,
    const frontend_network_prediction_source *source, const frontend_remote_snapshots_view *retail,
    frontend_network_prediction_source *out, qa_error *error)
{
    if (!out || !prediction_retail_current(f, source, retail))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Prediction feedback lost its actual returned CG snapshot receipt");
    frontend_network_prediction_source refreshed = *source;
    if (retail->this_frame_teleport &&
        !qa_q3_prediction_scene_mark_teleport(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene, error)) return false;
    if (!qa_q3_prediction_scene_read(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &refreshed.scene) ||
        !prediction_retail_current(f, &refreshed, retail))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Prediction feedback changed its actual CG or scene owner");
    *out = refreshed;
    return true;
}
bool frontend_network_prediction_teleport_consume(qa_frontend *f,
    const frontend_network_prediction_source *source, const frontend_remote_snapshots_view *retail,
    const frontend_remote_prediction *predictor, const frontend_remote_prediction_source *prediction,
    frontend_network_prediction_source *out, qa_error *error)
{
    frontend_remote_prediction_view completed;
    if (!out || !prediction || !prediction_retail_current(f, source, retail) ||
        retail->this_frame_teleport || !source->scene.this_frame_teleport ||
        !frontend_remote_prediction_read(predictor, prediction, &completed) || !completed.consumed_teleport ||
        !qa_net_client_id_equal(prediction->input.connection, source->connection) ||
        prediction->input.epoch != source->epoch || prediction->restart_generation != source->restart_generation ||
        prediction->geometry != source->geometry ||
        !qa_actor_id_equal(prediction->configuration.input.actor, source->viewer) ||
        !frontend_network_q3_client_context_current(f, &prediction->input.receiver) ||
        prediction->input.receiver.receiver != source->receiver.receiver ||
        prediction->input.receiver.seat != source->receiver.seat ||
        prediction->input.receiver.service_owner != source->receiver.service_owner ||
        prediction->scene.snapshot != source->scene.snapshot || prediction->scene.next_snapshot != source->scene.next_snapshot ||
        prediction->scene.prediction_snapshot != source->scene.prediction_snapshot ||
        prediction->scene.revision != source->scene.revision || prediction->scene.time != source->scene.time ||
        prediction->scene.physics_time != source->scene.physics_time ||
        prediction->scene.processed_snapshot != source->scene.processed_snapshot ||
        prediction->scene.this_frame_teleport != source->scene.this_frame_teleport ||
        prediction->scene.next_frame_teleport != source->scene.next_frame_teleport)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Teleport consumption lost its returned retail and predictor receipts");
    if (!qa_q3_prediction_scene_consume_teleport(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, error)) return false;
    frontend_network_prediction_source refreshed = *source;
    if (!qa_q3_prediction_scene_read(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &refreshed.scene) ||
        refreshed.scene.this_frame_teleport || refreshed.scene.prediction_snapshot != source->scene.prediction_snapshot ||
        refreshed.scene.physics_time != source->scene.physics_time || !prediction_retail_current(f, &refreshed, retail))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Teleport consumption changed its retained prediction seed or source");
    *out = refreshed;
    return true;
}
static bool prediction_actor_at(void *context, uint32_t number, qa_actor_id *actor, bool *present, qa_error *error)
{
    frontend_q3_client *n = context; (void)error;
    *present = false; *actor = (qa_actor_id){0};
    if (number >= QA_Q3_ENTITY_NONE || number == QA_Q3_ENTITY_WORLD) return true;
    *actor = n->q3_projection.actors[number];
    const qa_actor_record *record = qa_actors_get(qa_session_actors(qa_application_session(n->frontend->application)), *actor);
    *present = record && record->owner == n->q3_cgame_owner && !record->has_source &&
        record->definition == n->q3_projection.definition;
    return true;
}
static bool prediction_number_of(void *context, qa_actor_id actor, uint32_t *number, bool *present, qa_error *error)
{
    frontend_q3_client *n = context; *present = false; *number = QA_Q3_ENTITY_NONE;
    qa_actor_id viewer;
    if (!client_player(n, &viewer, error)) return false;
    if (qa_actor_id_equal(actor, viewer)) {
        qa_q3_prediction_scene_view scene;
        if (!qa_q3_prediction_scene_read(n->q3_prediction_scene, &scene)) return false;
        *number = (uint32_t)scene.snapshot->player.clientNum; *present = true; return true;
    }
    for (uint32_t i = 0; i < QA_Q3_ENTITY_NONE; ++i) if (qa_actor_id_equal(actor, n->q3_projection.actors[i])) {
        if (i == QA_Q3_ENTITY_WORLD) return true;
        qa_actor_id qualified;
        if (!prediction_actor_at(n, i, &qualified, present, error)) return false;
        if (*present) *number = i;
        return true;
    }
    return true;
}
bool frontend_network_prediction_actor_at(const qa_frontend *f,
    const qa_application_q3_client_context *receiver, uint32_t number,
    qa_actor_id *actor, bool *present, qa_error *error)
{
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    return n && prediction_actor_at(n, number, actor, present, error);
}
bool frontend_network_prediction_number_of(const qa_frontend *f,
    const qa_application_q3_client_context *receiver, qa_actor_id actor,
    uint32_t *number, bool *present, qa_error *error)
{
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    return n && prediction_number_of(n, actor, number, present, error);
}
static qa_q3_prediction_scene_collision prediction_collision(frontend_q3_client *n,
    const frontend_network_prediction_source *source)
{
    return (qa_q3_prediction_scene_collision){.geometry=(qa_collision_geometry *)source->geometry,
        .context=n,.actor_at=prediction_actor_at,.number_of=prediction_number_of,.scratch=source->scratch};
}
bool frontend_network_prediction_trace(qa_frontend *f, const frontend_network_prediction_source *source,
    const qa_trace_query *query, qa_trace_result *out, qa_error *error)
{
    if (!frontend_network_prediction_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote prediction trace source is stale");
    qa_q3_prediction_scene_collision collision = prediction_collision(q3_client_receiver(f, &source->receiver), source);
    return qa_q3_prediction_scene_trace(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene, &collision, query, out, error) &&
        (frontend_network_prediction_source_current(f, source) || frontend_fail(error, QA_ERROR_ARGUMENT, "Remote trace source changed"));
}
bool frontend_network_prediction_trace_with_number(qa_frontend *f, const frontend_network_prediction_source *source,
    const qa_trace_query *query, qa_trace_result *out, int32_t *number, qa_error *error)
{
    if (!frontend_network_prediction_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote prediction trace source is stale");
    qa_q3_prediction_scene_collision collision = prediction_collision(q3_client_receiver(f, &source->receiver), source);
    return qa_q3_prediction_scene_trace_with_number(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene,
        &collision, query, out, number, error) &&
        (frontend_network_prediction_source_current(f, source) || frontend_fail(error, QA_ERROR_ARGUMENT, "Remote trace source changed"));
}
bool frontend_network_prediction_point_contents(qa_frontend *f, const frontend_network_prediction_source *source,
    const qa_point_query *query, qa_point_contents *out, qa_error *error)
{
    if (!frontend_network_prediction_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote contents source is stale");
    qa_q3_prediction_scene_collision collision = prediction_collision(q3_client_receiver(f, &source->receiver), source);
    return qa_q3_prediction_scene_point_contents(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene, &collision, query, out, error) &&
        (frontend_network_prediction_source_current(f, source) || frontend_fail(error, QA_ERROR_ARGUMENT, "Remote contents source changed"));
}
bool frontend_network_prediction_is_bsp(qa_frontend *f, const frontend_network_prediction_source *source,
    const qa_trace_result *trace, bool *out, qa_error *error)
{
    if (!frontend_network_prediction_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote BSP hit source is stale");
    qa_q3_prediction_scene_collision collision = prediction_collision(q3_client_receiver(f, &source->receiver), source);
    return qa_q3_prediction_scene_is_bsp(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene, &collision, trace, out, error) &&
        (frontend_network_prediction_source_current(f, source) || frontend_fail(error, QA_ERROR_ARGUMENT, "Remote BSP source changed"));
}
bool frontend_network_prediction_adjust_mover(qa_frontend *f, const frontend_network_prediction_source *source,
    qa_vec3 origin, int32_t mover, int32_t from, int32_t to, qa_vec3 *out, qa_error *error)
{
    if (!frontend_network_prediction_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote mover source is stale");
    return qa_q3_prediction_scene_adjust_mover(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene,
        origin, mover, from, to, out, error) &&
        (frontend_network_prediction_source_current(f, source) || frontend_fail(error, QA_ERROR_ARGUMENT, "Remote mover source changed"));
}
bool frontend_network_prediction_trigger_count(qa_frontend *f, const frontend_network_prediction_source *source,
    size_t *out, qa_error *error)
{
    if (!frontend_network_prediction_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote trigger source is stale");
    return qa_q3_prediction_scene_trigger_count(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene, out, error);
}
bool frontend_network_prediction_trigger_at(qa_frontend *f, const frontend_network_prediction_source *source,
    size_t ordinal, qa_q3_prediction_scene_entity_view *out, bool *present, qa_error *error)
{
    if (!frontend_network_prediction_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote trigger source is stale");
    return qa_q3_prediction_scene_trigger_at(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene, ordinal, out, present, error);
}
bool frontend_network_prediction_trigger_overlap(qa_frontend *f, const frontend_network_prediction_source *source,
    const qa_q3_prediction_scene_entity_view *entity, qa_vec3 origin, qa_bounds bounds, bool *out, qa_error *error)
{
    if (!frontend_network_prediction_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote trigger overlap source is stale");
    return qa_q3_prediction_scene_trigger_overlap(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene,
        (qa_collision_geometry *)source->geometry, source->scratch, entity, origin, bounds, out, error) &&
        (frontend_network_prediction_source_current(f, source) || frontend_fail(error, QA_ERROR_ARGUMENT, "Remote trigger source changed"));
}
bool frontend_network_prediction_item_position(qa_frontend *f, const frontend_network_prediction_source *source,
    const qa_q3_prediction_scene_entity_view *entity, qa_vec3 *out, qa_error *error)
{
    if (!frontend_network_prediction_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote item source is stale");
    return qa_q3_prediction_scene_item_position(q3_client_receiver(f, &source->receiver)->q3_prediction_scene, &source->scene, entity, out, error);
}
bool frontend_network_construction_source_read(const qa_frontend *f,
    const qa_application_q3_client_context *receiver,
    frontend_network_construction_source *out, qa_error *error)
{
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    frontend_q3_content_view content;
    const qa_q3_client_peer *peer = n ? q3_view(n) : NULL;
    if (!out || !n || !peer || !n->q3_client_initializing || !n->q3_initial_tuple ||
        !n->q3_client_decoded || !qa_network_callbacks_idle(n->runtime) ||
        !client_connection_current(n, n->q3_client_epoch, error) ||
        !qa_application_q3_remote_source_read(f->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, n->q3_client_epoch, &out->source, error) ||
        !frontend_network_q3_client_context_read((qa_frontend *)f, n->q3_cgame_owner,
            n->q3_client_launch_seat, &out->source.receiver, error) ||
        !frontend_q3_content_read(n->q3_client_content, &content, error) ||
        frontend_q3_content_state(n->q3_client_content) != FRONTEND_Q3_CONTENT_PUBLISHED ||
        !content.map || !content.mounts ||
        !qa_network_q3_client_init_read(n->runtime, n->q3_client, &out->init, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote CLIENT construction lacks its actual idle published content and Init tuple");
    out->connection = n->q3_client; out->epoch = n->q3_client_epoch;
    out->restart_generation = n->q3_client_restart_generation;
    out->content_owner = n->q3_client_content;
    out->content = out->source.descriptor->content; out->prepared_mounts = content.mounts;
    out->map = content.map; out->product = content.selected;
    out->gamestate = qa_q3_client_peer_gamestate(peer);
    return frontend_network_construction_source_current(f, out) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Remote CLIENT construction cut changed during observation");
}
bool frontend_network_construction_source_current(const qa_frontend *f,
    const frontend_network_construction_source *source)
{
    frontend_q3_client *n = q3_client_receiver(f, source ? &source->source.receiver : NULL);
    const qa_q3_client_peer *peer = n ? q3_view(n) : NULL;
    frontend_q3_content_view content; qa_error ignored = {0};
    return source && n && peer && n->q3_client_initializing && n->q3_initial_tuple &&
        n->q3_client_decoded && qa_network_callbacks_idle(n->runtime) &&
        qa_net_client_id_equal(source->connection, n->q3_client) &&
        source->epoch == n->q3_client_epoch && source->restart_generation == n->q3_client_restart_generation &&
        source->content_owner == n->q3_client_content &&
        source->source.receiver.receiver == n->q3_cgame_owner &&
        source->source.receiver.seat == n->q3_client_launch_seat &&
        source->source.connection_epoch == source->epoch &&
        qa_application_q3_remote_source_current(f->application, &source->source) &&
        frontend_network_q3_client_context_current((qa_frontend *)f, &source->source.receiver) &&
        client_connection_current(n, source->epoch, &ignored) &&
        frontend_q3_content_state(n->q3_client_content) == FRONTEND_Q3_CONTENT_PUBLISHED &&
        frontend_q3_content_read(n->q3_client_content, &content, &ignored) &&
        content.map == source->map && content.mounts == source->prepared_mounts &&
        source->source.descriptor->content == source->content && source->content && content.selected == source->product &&
        source->gamestate == qa_q3_client_peer_gamestate(peer) &&
        content.gamestate && content.gamestate->client_number == source->init.client_number &&
        content.gamestate->checksum_feed == source->gamestate->checksum_feed &&
        source->init.server_message == n->q3_initial_message &&
        source->init.last_executed_server_command == n->q3_initial_command &&
        qa_network_q3_client_init_current(n->runtime, n->q3_client, &source->init);
}
bool frontend_network_client_domain_read(const qa_frontend *f,
    const qa_application_q3_client_context *receiver, frontend_network_client_domain *out, qa_error *error)
{
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    const qa_q3_client_peer *peer = n ? q3_view(n) : NULL;
    frontend_q3_content_view content;
    if (!out || !n || !peer || !n->q3_initial_tuple ||
        (!n->q3_client_initializing && !n->q3_client_gamestate) ||
        !client_connection_current(n, n->q3_client_epoch, error) ||
        !qa_application_q3_remote_source_read(f->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, n->q3_client_epoch, &out->source, error) ||
        !frontend_network_q3_client_context_read((qa_frontend *)f, n->q3_cgame_owner,
            n->q3_client_launch_seat, &out->source.receiver, error) ||
        !frontend_q3_content_read(n->q3_client_content, &content, error) || !content.map)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote CLIENT domain lacks its actual published source, map and Init entry");
    out->connection = n->q3_client; out->epoch = n->q3_client_epoch;
    out->restart_generation = n->q3_client_restart_generation;
    out->content_owner = n->q3_client_content;
    out->content = out->source.descriptor->content; out->prepared_mounts = content.mounts;
    out->map = content.map; out->product = content.selected;
    out->gamestate = qa_q3_client_peer_gamestate(peer);
    out->initial = (qa_network_q3_client_init){n->q3_initial_message, n->q3_initial_command,
        out->gamestate->client_number};
    return frontend_network_client_domain_current(f, out) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Remote CLIENT domain changed during observation");
}
bool frontend_network_client_domain_current(const qa_frontend *f,
    const frontend_network_client_domain *source)
{
    frontend_q3_client *n = q3_client_receiver(f, source ? &source->source.receiver : NULL);
    const qa_q3_client_peer *peer = n ? q3_view(n) : NULL;
    frontend_q3_content_view content; qa_error ignored = {0};
    frontend_q3_content_phase phase = n ? frontend_q3_content_state(n->q3_client_content) : FRONTEND_Q3_CONTENT_CATALOG;
    return source && n && peer && n->q3_initial_tuple && n->q3_client_decoded &&
        ((n->q3_client_initializing && (phase == FRONTEND_Q3_CONTENT_PUBLISHED || phase == FRONTEND_Q3_CONTENT_MEDIA_READY)) ||
         (n->q3_client_gamestate && phase == FRONTEND_Q3_CONTENT_MEDIA_READY)) &&
        qa_net_client_id_equal(source->connection, n->q3_client) &&
        source->epoch == n->q3_client_epoch && source->restart_generation == n->q3_client_restart_generation &&
        source->content_owner == n->q3_client_content &&
        source->source.receiver.receiver == n->q3_cgame_owner && source->source.receiver.seat == n->q3_client_launch_seat &&
        source->source.connection_epoch == source->epoch &&
        qa_application_q3_remote_source_current(f->application, &source->source) &&
        frontend_network_q3_client_context_current((qa_frontend *)f, &source->source.receiver) &&
        client_connection_current(n, source->epoch, &ignored) &&
        frontend_q3_content_read(n->q3_client_content, &content, &ignored) &&
        content.map == source->map && content.mounts == source->prepared_mounts && content.selected == source->product &&
        source->source.descriptor->content == source->content && source->content &&
        source->gamestate == qa_q3_client_peer_gamestate(peer) && content.gamestate &&
        content.gamestate->client_number == source->initial.client_number &&
        source->gamestate->client_number == source->initial.client_number &&
        content.gamestate->checksum_feed == source->gamestate->checksum_feed &&
        source->initial.server_message == n->q3_initial_message &&
        source->initial.last_executed_server_command == n->q3_initial_command;
}
bool frontend_network_q3_video_reinit_read(const qa_frontend *f,
    const qa_application_q3_client_context *receiver,
    frontend_network_q3_video_reinit_view *out,qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    frontend_q3_client *client=q3_client_receiver(f,receiver);
    qa_application_q3_client_context actual;
    qa_network_q3_client_init counters;
    const qa_net_client *state=client?qa_net_connections_get(qa_network_connections(client->runtime),client->q3_client):NULL;
    if(!out || !n || n->busy || n->detached_transport || !client || !client->q3_session ||
        (!frontend_remote_q3_idle(f) && !frontend_video_guests_read(f)) || !qa_network_callbacks_idle(client->runtime) ||
        !qa_application_q3_remote_context_read(f->application,client->q3_cgame_owner,
            client->q3_client_launch_seat,&actual,error) ||
        !frontend_network_client_domain_read(f,&actual,&out->domain,error) ||
        !qa_network_q3_client_init_read(client->runtime,client->q3_client,&counters,error) || !state)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Video restart lacks its returned decoded Q3 CLIENT");
    out->network=n; out->video_stage=frontend_video_guests_read(f); out->connecting=state->phase!=QA_NET_ACTIVE;
    out->init=(qa_application_q3_remote_init){.source=out->domain.source,
        .server_message=counters.server_message,.last_executed_server_command=counters.last_executed_server_command,
        .client_number=counters.client_number,.connection=client,.current=client_init_current};
    return frontend_network_q3_video_reinit_current(f,out) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Video restart CLIENT tuple changed during observation");
}
bool frontend_network_q3_video_reinit_current(const qa_frontend *f,
    const frontend_network_q3_video_reinit_view *view)
{
    qa_frontend_network *n=f?f->network:NULL;
    const frontend_q3_client *client=view?q3_client_receiver(f,&view->domain.source.receiver):NULL;
    const qa_net_client *state=client?qa_net_connections_get(qa_network_connections(client->runtime),client->q3_client):NULL;
    qa_network_q3_client_init counters;
    return view && n && client && view->network==n && !n->busy && !n->detached_transport && client->q3_session &&
        ((!view->video_stage && !frontend_video_guests_read(f) && frontend_remote_q3_idle(f)) ||
            (view->video_stage && frontend_video_guests_read(f)==view->video_stage &&
            frontend_video_guests_parent_is(f,view->video_stage))) && qa_network_callbacks_idle(client->runtime) &&
        frontend_network_client_domain_current(f,&view->domain) &&
        qa_network_q3_client_init_read(client->runtime,client->q3_client,&counters,NULL) && state &&
        view->connecting==(state->phase!=QA_NET_ACTIVE) && view->init.connection==client && view->init.current==client_init_current &&
        view->init.source.descriptor==view->domain.source.descriptor &&
        qa_application_q3_remote_source_current(f->application,&view->init.source) &&
        view->init.server_message==counters.server_message &&
        view->init.last_executed_server_command==counters.last_executed_server_command &&
        view->init.client_number==counters.client_number;
}
bool frontend_network_q3_video_initial_read(const qa_frontend *f,const frontend_remote_q3_initial *owner,
    frontend_remote_q3_modules **modules,qa_error *error)
{
    if(!f || !owner || !modules)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial video observation lacks its actual outputs");
    *modules=NULL;
    qa_frontend_network *n=f->network;
    frontend_remote_q3_initial_view view;
    bool read=frontend_remote_q3_initial_read(owner,&view,error);
    frontend_q3_client *client=read?q3_client_receiver(f,&view.attempt.source.receiver):NULL;
    if(!n || n->busy || n->detached_transport || !client || client->q3_initial!=owner ||
        client->q3_session || !client->q3_initial_modules ||
        !qa_network_callbacks_idle(client->runtime) || !frontend_remote_q3_initial_idle(owner) ||
        !frontend_remote_q3_modules_idle(client->q3_initial_modules) ||
        frontend_remote_q3_modules_initial_parent(client->q3_initial_modules)!=owner ||
        !frontend_remote_q3_initial_current(&view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial video restart lacks its returned real UI parent");
    *modules=client->q3_initial_modules; return true;
}
bool frontend_network_client_domain_metadata_read(const qa_frontend *f,
    const qa_application_q3_client_context *receiver,
    frontend_network_client_domain *out, qa_error *error)
{
    const frontend_q3_client *n=q3_client_receiver(f,receiver);
    const qa_q3_client_peer *peer=n?qa_network_q3_client_view(n->runtime,n->q3_client):NULL;
    frontend_q3_content_metadata metadata;
    frontend_network_client_domain domain={0};
    if(!f || !f->resource_inventory || !out || !n || n->frontend!=f || !peer || !n->q3_initial_tuple ||
        !frontend_q3_content_metadata_read(n->q3_client_content,&metadata,error) ||
        !metadata.source.receiver.native_source ||
        !qa_application_q3_remote_source_read(f->application,n->q3_cgame_owner,n->q3_client_launch_seat,
            n->q3_client_epoch,&domain.source,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Resource inventory lacks its actual native CLIENT domain metadata");
    const qa_q3_gamestate *state=qa_q3_client_peer_gamestate(peer);
    if(!state || state->client_number<0 || state->client_number>=64)
        return frontend_fail(error,QA_ERROR_FORMAT,"Resource inventory lost its decoded native CLIENT ordinal");
    domain.source.receiver.source_client=(uint32_t)state->client_number;
    domain.source.receiver.source_milliseconds=n->q3_client_time;
    domain.connection=n->q3_client; domain.epoch=n->q3_client_epoch;
    domain.restart_generation=n->q3_client_restart_generation; domain.content_owner=n->q3_client_content;
    domain.content=domain.source.descriptor->content; domain.prepared_mounts=metadata.content.mounts;
    domain.map=metadata.content.map; domain.product=metadata.content.selected; domain.gamestate=state;
    domain.initial=(qa_network_q3_client_init){n->q3_initial_message,n->q3_initial_command,state->client_number};
    if(!frontend_network_client_domain_metadata_current(f,&domain))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Resource inventory native CLIENT metadata differs from its retained owners");
    *out=domain; return true;
}
bool frontend_network_client_domain_metadata_current(const qa_frontend *f,
    const frontend_network_client_domain *source)
{
    const frontend_q3_client *n=q3_client_receiver(f,source?&source->source.receiver:NULL);
    const qa_q3_client_peer *peer=n?qa_network_q3_client_view(n->runtime,n->q3_client):NULL;
    const qa_net_client *client=n?qa_net_connections_get(qa_network_connections(n->runtime),n->q3_client):NULL;
    frontend_q3_content_metadata metadata;
    qa_application_q3_client_context receiver;
    if(!f || !f->resource_inventory || !source || !source->source.receiver.native_source || !n || n->frontend!=f ||
        !peer || !client || !n->q3_client_requested || !n->q3_client_attached || n->q3_client_retiring || n->q3_client_closed ||
        !n->q3_client_decoded || !n->q3_initial_tuple || !qa_network_callbacks_idle(n->runtime) ||
        !qa_network_q3_client_live(n->runtime,n->q3_client) || qa_network_epoch(n->runtime,n->q3_client)!=1 ||
        client->attachment!=QA_NET_REMOTE || client->protocol.kind!=QA_NET_Q3_68 || client->protocol.revision || client->protocol.flags ||
        client->seat_count!=1 || client->seats[0].seat.owner!=n->seat.owner || client->seats[0].seat.index!=n->seat.index ||
        client->seats[0].remote_index || !qa_net_address_equal(&client->endpoint,&n->q3_client_admission.address,true) ||
        !qa_net_client_id_equal(source->connection,n->q3_client) || source->epoch!=n->q3_client_epoch ||
        source->restart_generation!=n->q3_client_restart_generation || source->content_owner!=n->q3_client_content ||
        source->source.receiver.receiver!=n->q3_cgame_owner || source->source.receiver.seat!=n->q3_client_launch_seat ||
        source->source.connection_epoch!=source->epoch ||
        !frontend_q3_content_metadata_read(n->q3_client_content,&metadata,NULL) || metadata.application!=f->application ||
        !qa_application_q3_remote_context_read(f->application,n->q3_cgame_owner,n->q3_client_launch_seat,&receiver,NULL) ||
        !receiver.native_source || (metadata.source.receiver.initialized && !receiver.initialized)) return false;
    frontend_q3_content_phase phase=frontend_q3_content_state(n->q3_client_content);
    if(!((n->q3_client_initializing && (phase==FRONTEND_Q3_CONTENT_PUBLISHED || phase==FRONTEND_Q3_CONTENT_MEDIA_READY)) ||
        (n->q3_client_gamestate && phase==FRONTEND_Q3_CONTENT_MEDIA_READY))) return false;
    metadata.source.receiver.initialized=receiver.initialized;
    const qa_q3_gamestate *state=qa_q3_client_peer_gamestate(peer);
    const qa_launch_instance *actual=metadata.source.descriptor,*held=source->source.descriptor;
    return actual && held && qa_application_q3_remote_source_current(f->application,&metadata.source) &&
        qa_application_q3_remote_source_current(f->application,&source->source) &&
        actual->storage==held->storage && actual->content==held->content && (actual == held) &&
        metadata.source.configuration_generation==source->source.configuration_generation &&
        metadata.source.connection_epoch==source->epoch &&
        source->source.receiver.session==receiver.session && source->source.receiver.console==receiver.console &&
        source->source.receiver.cvars==receiver.cvars && source->source.receiver.service_owner==receiver.service_owner &&
        source->source.receiver.frontend_lifetime==receiver.frontend_lifetime &&
        source->source.receiver.initialized==receiver.initialized && source->source.receiver.source_milliseconds==n->q3_client_time &&
        state && source->gamestate==state && metadata.content.gamestate &&
        state->client_number>=0 && state->client_number<64 &&
        source->source.receiver.source_client==(uint32_t)state->client_number &&
        metadata.content.gamestate->client_number==state->client_number &&
        metadata.content.gamestate->checksum_feed==state->checksum_feed &&
        metadata.content.map==source->map && source->map && metadata.content.mounts==source->prepared_mounts &&
        metadata.content.selected==source->product && source->content==held->content && source->content &&
        source->initial.client_number==state->client_number && source->initial.server_message==n->q3_initial_message &&
        source->initial.last_executed_server_command==n->q3_initial_command;
}
bool frontend_network_native_publication_read(const qa_frontend *f,
    const qa_application_q3_client_context *receiver,
    q3n_remote_publication *out, qa_error *error)
{
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    frontend_network_client_domain domain; qa_network_q3_client_init counters;
    if (!out || !frontend_network_client_domain_read(f, receiver, &domain, error) ||
        !domain.source.receiver.native_source ||
        !qa_network_q3_client_init_read(n->runtime, domain.connection, &counters, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native publication requires its actual CLIENT domain and transport counters");
    const qa_q3_client_peer *peer = qa_network_q3_client_view(n->runtime, domain.connection);
    if (!peer) return frontend_fail(error, QA_ERROR_ARGUMENT, "Native publication lost its actual decoded transport owner");
    const qa_q3_snapshot *latest = qa_q3_client_peer_snapshot(peer);
    q3n_remote_publication publication = {.connection = domain.connection, .epoch = domain.epoch,
        .restart_generation = domain.restart_generation, .gamestate = domain.gamestate,
        .initial_message = domain.initial.server_message, .initial_command = domain.initial.last_executed_server_command,
        .latest_message = latest ? latest->message_number : 0, .latest_time = latest ? latest->server_time : 0,
        .presentation_time = n->q3_client_time, .server_message = counters.server_message,
        .received_command = qa_q3_client_peer_server_command_sequence(peer),
        .executed_command = counters.last_executed_server_command,
        .initializing = n->q3_client_initializing, .has_snapshot = latest != NULL,
        .demo_playback = qa_q3_client_peer_demo(peer)};
    if (!client_player(n, &publication.viewer, error) ||
        !frontend_network_client_domain_current(f, &domain))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native publication changed its genuine receiver, viewer or decoded owner");
    *out = publication; return true;
}
bool frontend_network_native_publication_current(const qa_frontend *f,
    const q3n_remote_publication *publication)
{
    q3n_remote_publication actual;
    frontend_q3_client *n = publication ? q3_client_connection(f, publication->connection) : NULL;
    qa_application_q3_client_context receiver;
    return n && qa_application_q3_remote_context_read(f->application, n->q3_cgame_owner,
        n->q3_client_launch_seat, &receiver, NULL) && frontend_network_native_publication_read(f, &receiver, &actual, NULL) &&
        qa_net_client_id_equal(publication->connection, actual.connection) && publication->epoch == actual.epoch &&
        publication->restart_generation == actual.restart_generation && publication->gamestate == actual.gamestate &&
        qa_actor_id_equal(publication->viewer, actual.viewer) && publication->initial_message == actual.initial_message &&
        publication->initial_command == actual.initial_command && publication->latest_message == actual.latest_message &&
        publication->latest_time == actual.latest_time && publication->presentation_time == actual.presentation_time &&
        publication->server_message == actual.server_message && publication->received_command == actual.received_command &&
        publication->executed_command == actual.executed_command && publication->initializing == actual.initializing &&
        publication->has_snapshot == actual.has_snapshot && publication->demo_playback == actual.demo_playback;
}
bool frontend_network_native_command_read(qa_frontend *f, const qa_application_q3_client_context *receiver, int32_t sequence,
    q3n_remote_command *out, qa_error *error)
{
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    q3n_remote_publication before; frontend_network_client_domain domain, after;
    if (!out || !frontend_network_native_publication_read(f,receiver, &before, error) ||
        !frontend_network_client_domain_read(f, receiver, &domain, error)) return false;
    *out = (q3n_remote_command){0}; bool present;
    if (!service_server_command(f->seats+n->physical, sequence, &present, error) ||
        !frontend_network_native_publication_read(f,receiver, &out->publication, error) ||
        !frontend_network_client_domain_read(f, receiver, &after, error)) return false;
    if (!qa_net_client_id_equal(before.connection, out->publication.connection) ||
        before.epoch != out->publication.epoch || before.gamestate != out->publication.gamestate ||
        domain.source.descriptor->storage != after.source.descriptor->storage ||
        domain.source.configuration_generation != after.source.configuration_generation || domain.map != after.map ||
        domain.source.receiver.receiver != after.source.receiver.receiver || domain.source.receiver.seat != after.source.receiver.seat ||
        domain.source.receiver.service_owner != after.source.receiver.service_owner ||
        domain.source.receiver.frontend_lifetime != after.source.receiver.frontend_lifetime ||
        domain.source.receiver.console != after.source.receiver.console || domain.source.receiver.cvars != after.source.receiver.cvars)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native reliable execution replaced its actual transport domain");
    out->sequence = sequence; out->present = present;
    if (present) out->tokens = &n->q3_reached_command;
    return frontend_network_native_command_current(f, out) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Native reliable receipt differs from its actual execute result");
}
bool frontend_network_native_command_current(const qa_frontend *f,
    const q3n_remote_command *command)
{
    if (!command || !frontend_network_native_publication_current(f, &command->publication)) return false;
    const frontend_q3_client *n = q3_client_connection(f, command->publication.connection);
    if (!n->q3_reliable_receipt || command->sequence != n->q3_reliable_receipt_sequence ||
        command->present != n->q3_command_present) return false;
    if (command->present) return command->tokens == &n->q3_reached_command &&
        command->sequence == n->q3_reached_command_sequence && command->sequence == command->publication.executed_command;
    const qa_q3_client_peer *peer = qa_network_q3_client_view(n->runtime, n->q3_client);
    return !command->tokens && peer && command->sequence <= command->publication.received_command &&
        (command->sequence == command->publication.executed_command ||
         (qa_q3_client_peer_demo(peer) &&
          (int64_t)command->sequence <= (int64_t)command->publication.received_command - 64));
}
bool frontend_network_presentation_services(qa_frontend *f,
    const qa_application_q3_client_context *receiver, qa_q3_host_client_services *out, qa_error *error)
{
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    if (!out || !n || !n->q3_client_decoded || !n->q3_client_attached ||
        (!n->q3_client_gamestate && !n->q3_client_initializing) ||
        n->q3_client_retiring || n->q3_client_closed ||
        !qa_network_q3_client_live(n->runtime, n->q3_client) ||
        !frontend_network_q3_client_context_current(f, receiver))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote imports lack their actual decoded connection and receiver lease");
    *out = (qa_q3_host_client_services){.context=f->seats+n->physical,.gamestate=service_gamestate,.current_snapshot=service_current_snapshot,
        .snapshot=service_snapshot,.server_command=service_server_command,.current_command=service_current_command,
        .user_command=service_user_command,.command_values=service_command_values,.source_actor=service_source_actor};
    return true;
}
bool frontend_network_presentation_source_read(const qa_frontend *f,
    const qa_application_q3_client_context *receiver,
    frontend_network_presentation_source *out, bool *present, qa_error *error)
{
    frontend_q3_client *n = q3_client_receiver(f, receiver);
    if (!out || !present) return frontend_fail(error, QA_ERROR_ARGUMENT, "Missing actual remote presentation source output");
    memset(out, 0, sizeof(*out));
    if (!frontend_network_prediction_source_read(f,receiver, &out->prediction, present, error)) return false;
    if (!*present) return true;
    const qa_q3_client_peer *peer = qa_network_q3_client_view(n->runtime, n->q3_client);
    if (!peer || !n->q3_initial_tuple)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote presentation lacks its actual source Init tuple");
    out->gamestate = qa_q3_client_peer_gamestate(peer);
    out->initial_message = n->q3_initial_message; out->initial_command = n->q3_initial_command;
    const qa_q3_snapshot *latest = qa_q3_client_peer_snapshot(peer);
    if (!latest) return frontend_fail(error, QA_ERROR_ARGUMENT, "Active remote presentation lacks its actual latest snapshot");
    out->latest_message = latest->message_number; out->latest_time = latest->server_time;
    out->presentation_time = n->q3_client_time;
    qa_network_q3_client_init counters;
    if (!qa_network_q3_client_init_read(n->runtime, n->q3_client, &counters, error)) return false;
    out->server_message = counters.server_message;
    out->received_command = qa_q3_client_peer_server_command_sequence(peer);
    out->executed_command = counters.last_executed_server_command;
    return frontend_network_presentation_source_current(f, out) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Remote presentation source changed during observation");
}
bool frontend_network_presentation_source_current(const qa_frontend *f,
    const frontend_network_presentation_source *source)
{
    if (!source || !frontend_network_prediction_source_current(f, &source->prediction)) return false;
    const frontend_q3_client *n = q3_client_receiver(f, &source->prediction.receiver);
    const qa_q3_client_peer *peer = qa_network_q3_client_view(n->runtime, n->q3_client);
    const qa_q3_snapshot *latest = peer ? qa_q3_client_peer_snapshot(peer) : NULL;
    qa_network_q3_client_init counters;
    return peer && n->q3_initial_tuple && source->gamestate == qa_q3_client_peer_gamestate(peer) &&
        source->initial_message == n->q3_initial_message && source->initial_command == n->q3_initial_command &&
        latest && latest->message_number == source->latest_message && latest->server_time == source->latest_time &&
        source->presentation_time == n->q3_client_time &&
        qa_network_q3_client_init_read(n->runtime, n->q3_client, &counters, NULL) &&
        source->server_message == counters.server_message && source->executed_command == counters.last_executed_server_command &&
        source->received_command == qa_q3_client_peer_server_command_sequence(peer);
}
bool frontend_network_presentation_snapshot(const qa_frontend *f,
    const frontend_network_presentation_source *source, int32_t number,
    const qa_q3_snapshot **out, int32_t *ping, qa_error *error)
{
    if (!out || !ping || !frontend_network_presentation_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote snapshot history lost its actual presentation source");
    frontend_q3_client *n = q3_client_receiver(f, &source->prediction.receiver);
    return service_snapshot(f->seats+n->physical, number, out, ping, error) &&
        (frontend_network_presentation_source_current(f, source) ||
            frontend_fail(error, QA_ERROR_ARGUMENT, "Remote snapshot history source changed during observation"));
}
bool frontend_network_presentation_snapshot_current(const qa_frontend *f,
    const frontend_network_presentation_source *source, const qa_q3_snapshot *snapshot)
{
    if (!snapshot || !frontend_network_presentation_source_current(f, source)) return false;
    const qa_q3_client_peer *peer = qa_network_q3_client_view(q3_client_receiver(f, &source->prediction.receiver)->runtime, source->prediction.connection);
    return peer && qa_q3_client_peer_presentation_snapshot_at(peer, snapshot->message_number) == snapshot;
}
bool frontend_network_presentation_execute(qa_frontend *f,
    const frontend_network_presentation_source *source, int32_t sequence,
    frontend_network_presentation_command *out, qa_error *error)
{
    if (!out || !frontend_network_presentation_source_current(f, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote reliable execution lost its actual presentation source");
    memset(out, 0, sizeof(*out)); bool reached = false;
    frontend_q3_client *n = q3_client_receiver(f, &source->prediction.receiver);
    if (!service_server_command(f->seats+n->physical, sequence, &reached, error)) return false;
    bool present = false;
    if (!frontend_network_presentation_source_read(f,&source->prediction.receiver, &out->source, &present, error)) return false;
    if (!present) return true;
    if (!qa_net_client_id_equal(out->source.prediction.connection, source->prediction.connection) ||
        out->source.prediction.epoch != source->prediction.epoch ||
        out->source.prediction.receiver.receiver != source->prediction.receiver.receiver ||
        out->source.prediction.receiver.service_owner != source->prediction.receiver.service_owner)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote reliable execution replaced its admitted receiver");
    out->sequence = sequence; out->present = reached;
    if (reached) out->tokens = &n->q3_reached_command;
    return frontend_network_presentation_command_current(f, out) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Remote reliable arguments differ from the actual reached command");
}
bool frontend_network_presentation_command_current(const qa_frontend *f,
    const frontend_network_presentation_command *command)
{
    if (!command || !frontend_network_presentation_source_current(f, &command->source)) return false;
    const frontend_q3_client *n = q3_client_receiver(f, &command->source.prediction.receiver); qa_network_q3_client_init counters;
    if (!n->q3_reliable_receipt || command->sequence != n->q3_reliable_receipt_sequence ||
        command->present != n->q3_command_present ||
        !qa_network_q3_client_init_read(n->runtime, n->q3_client, &counters, NULL)) return false;
    if (command->present) return command->tokens == &n->q3_reached_command &&
        command->sequence == n->q3_reached_command_sequence && counters.last_executed_server_command == command->sequence;
    const qa_q3_client_peer *peer = qa_network_q3_client_view(n->runtime, n->q3_client);
    return !command->tokens && peer && command->sequence <= qa_q3_client_peer_server_command_sequence(peer) &&
        (counters.last_executed_server_command == command->sequence ||
         (qa_q3_client_peer_demo(peer) &&
          (int64_t)command->sequence <= (int64_t)qa_q3_client_peer_server_command_sequence(peer) - 64));
}
bool frontend_network_client_pose_publish(qa_frontend *f, qa_error *error)
{
    if (!f || !f->network) return true;
    for (uint32_t physical = 0; physical < f->options.seats; ++physical) {
        frontend_q3_client *n = f->network->q3_clients + physical;
        if (!n->q3_client_requested) continue;
        qa_application_q3_client_context receiver;
        frontend_network_prediction_source source; bool present;
        if (!qa_application_q3_remote_context_read(f->application, n->q3_cgame_owner,
            n->q3_client_launch_seat, &receiver, error) ||
            !frontend_network_prediction_source_read(f, &receiver, &source, &present, error)) return false;
        if (!present) continue;
        const qa_cvar_view *smooth = qa_cvars_read(source.receiver.cvars, n->cg_smoothClients);
        if (!qa_q3_prediction_scene_publish_poses(n->q3_prediction_scene,
            smooth && smooth->integer != 0, error)) return false;
        n->q3_previous_presentation_time = source.scene.time;
    }
    return true;
}
bool frontend_network_close_client(qa_frontend *f, qa_error *error)
{
    if (!f || !f->network) return true;
    if (!frontend_network_world_change_ready(f, error)) return false;
    for (uint32_t i = 0; i < f->options.seats; ++i) {
        frontend_q3_client *n = f->network->q3_clients + i;
        if (!n->q3_client_requested || (!n->q3_client_attached && !n->q3_session &&
            !n->q3_initial && !n->q3_initial_modules)) continue;
        if (n->network->detached_transport) {
            if (!n->q3_session && !n->q3_initial && !n->q3_initial_modules) continue;
            qa_application_q3_remote_source retained = n->q3_session_source;
            if (!client_native_retire(n, &retained, error)) return false;
        } else if (!client_close_attempt(n, error)) return false;
    }
    return true;
}
static bool q3_client_destroy(frontend_q3_client *n, qa_error *error)
{
    if (n->q3_client_requested && (n->q3_client_attached || n->q3_session ||
        n->q3_initial || n->q3_initial_modules)) {
        if (n->network->detached_transport) {
            qa_application_q3_remote_source retained = n->q3_session_source;
            if (!client_native_retire(n, &retained, error)) return false;
        } else if (!client_close_attempt(n, error)) return false;
    }
    if (!qa_application_network_q3_client_unproject(n->frontend->application, &n->q3_projection, error)) return false;
    qa_q3_client_downloads_destroy(n->q3_client_downloads);
    frontend_q3_content_destroy(n->q3_client_content);
    qa_q3_prediction_scene_destroy(n->q3_prediction_scene);
    frontend_remote_input_destroy(n->q3_input);
    frontend_network_predictor_destroy(n->q3_predictor);
    qa_buffer_free(&n->q3_predictor_pending);
    qa_buffer_free(&n->q3_download_pending);
    qa_buffer_free(&n->q3_connections_prefix);
    qa_q3_client_authorization_destroy(n->q3_client_authorization);
    *n = (frontend_q3_client){.network=n->network, .frontend=n->frontend,
        .physical=n->physical, .seat=n->seat, .q3_sensitivity=1};
    return true;
}
bool frontend_network_client_draw(qa_frontend *f,uint32_t physical,uint32_t stereo,
    bool *rendered,qa_audio_listener *listener,bool *has_listener,qa_error *error)
{
    if(!rendered || !listener || !has_listener || stereo>2)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote Draw requires its physical pass and actual outputs");
    *rendered=false; *has_listener=false;
    frontend_q3_client *n=q3_client_physical(f,physical);
    if(!n || !n->q3_client_requested || n->q3_client_retiring || n->q3_client_closed) return true;
    if(n->q3_session) {
        frontend_remote_q3_session_view session;
        if(!frontend_remote_q3_session_read(n->q3_session,&session,error)) return false;
        if(session.resources.physical_seat!=physical) return true;
        if(!n->q3_client_gamestate || !frontend_q3_content_media_current(n->q3_client_content,error))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Decoded remote Draw lacks completed source media");
        frontend_remote_prediction *predictor=frontend_network_predictor_read(n->q3_predictor);
        if(!frontend_remote_q3_session_draw(n->q3_session,predictor,stereo,listener,has_listener,error)) return false;
        *rendered=true; return true;
    }
    if(!n->q3_initial && !n->q3_initial_modules) return true;
    frontend_remote_q3_initial_view view; frontend_remote_q3_module_media media;
    application_native_q3_client_modules *modules=frontend_remote_q3_modules_owner(n->q3_initial_modules);
    if(!modules || !frontend_remote_q3_initial_read(n->q3_initial,&view,error)) return false;
    if(view.physical_seat!=physical) return true;
    if(!frontend_remote_q3_modules_media_read(n->q3_initial_modules,QA_QVM_UI,&media,error) ||
        !frontend_remote_q3_modules_media_current(&media) ||
        !qa_q3_presentation_frame(media.presentation,&f->frame,frontend_viewport(f,physical),error)) return false;
    uint32_t word=(uint32_t)(f->wall_time_ns/UINT64_C(1000000)); int32_t realtime,result;
    memcpy(&realtime,&word,sizeof(realtime));
    if(!qa_application_native_q3_client_modules_call(modules,QA_QVM_UI,5,&realtime,1,&result,error) ||
        !frontend_remote_q3_modules_media_current(&media) || !frontend_remote_q3_initial_current(&view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial source UI lost its returned module and attempt receipt");
    *rendered=true; return true;
}
bool frontend_network_retire_clients(qa_frontend *f,qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    if(!f || f->stepping || f->preparing || f->capture || f->resource_inventory ||
        (n && (n->frontend!=f || n->round || n->busy || !qa_network_callbacks_idle(n->runtime))) ||
        !qa_http_callbacks_idle(frontend_tools_http(f)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement requires its returned frontend and Network callbacks");
    if(!n) return true;
    if(!local_clients_destroy(n,error) || !frontend_network_close_client(f,error) ||
        !frontend_network_q2_client_destroy(&n->q2_client_owner,error) ||
        !frontend_network_q1_client_destroy(&n->q1_client_owner,error)) return false;
    if(n->unified_client_service && !frontend_network_unified_destroy(&n->unified,error)) return false;
    return frontend_network_unified_client_destroy(&n->unified_client_service,error);
}
bool frontend_network_retire_connections(qa_frontend *f, bool *complete, qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    if (!f || !complete || f->stepping || f->preparing || f->capture || f->resource_inventory ||
        (n && (n->frontend!=f || n->round || n->busy ||
            !qa_network_callbacks_idle(n->runtime))))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Server transport shutdown requires returned Source callbacks");
    *complete=true;
    if(n) {
        if(n->detached_transport) {
            if(frontend_network_q2_host_importing(n->q2_host)) {
                if(!frontend_network_q2_host_import_retirement_idle(n->q2_host))
                    return frontend_fail(error,QA_ERROR_ARGUMENT,
                        "Q2 import retirement requires its returned HOST and transport");
            } else {
                /* Publication transferred the wire transport. Release the
                 * displaced HOST's Source claims before its roster retires. */
                if(!frontend_network_q2_host_destroy(&n->q2_host,error)) return false;
            }
        } else if(!frontend_network_q2_host_stop(n->q2_host,f->wall_time_ns,complete,error)) return false;
    }
    if (!*complete) return true;
    uint32_t cursor=0; const qa_net_client *client;
    while (n && qa_net_connections_next(qa_network_connections(n->runtime),&cursor,&client)) {
        qa_net_client_id id=client->id;
        if (!qa_network_detach(n->runtime,id,"Server was killed.\n",error)) return false;
    }
    return true;
}
bool frontend_network_stop_server(qa_frontend *f, bool *complete, qa_error *error)
{
    if (!frontend_network_retire_connections(f,complete,error)) return false;
    return !*complete || frontend_network_destroy(f,error);
}

bool frontend_network_destroy(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n = f->network; if (!n) return true;
    if(n->demo_q1_owner || n->demo_q3_sink.append || n->demo_playback)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo receivers must detach before Network retirement");
    if (!frontend_network_world_change_ready(f, error) || !qa_http_callbacks_idle(frontend_tools_http(f)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "network/HTTP callbacks must return before teardown");
    if (!local_clients_destroy(n, error) || !frontend_network_close_client(f, error)) return false;
    if (!frontend_network_q2_client_destroy(&n->q2_client_owner, error)) return false;
    if (!frontend_network_q1_client_destroy(&n->q1_client_owner,error)) return false;
    if(n->detached_transport && frontend_network_q2_host_importing(n->q2_host))
        frontend_network_q2_host_restore_abort(&n->q2_host);
    else if (!frontend_network_q2_host_destroy(&n->q2_host, error)) return false;
    if (!frontend_network_unified_destroy(&n->unified, error)) return false;
    if (!frontend_network_unified_client_destroy(&n->unified_client_service,error)) return false;
    if (n->admin && !n->detached_transport && !qa_server_admin_shutdown(n->admin, error)) return false;
    for (uint32_t i = 0; i < f->options.seats; ++i)
        if (!q3_client_destroy(n->q3_clients + i, error)) return false;
    qa_console *console=qa_application_console(f->application);
    qa_cvars *cvars=qa_application_cvars(f->application);
    if(f->engine_shutdown) {
        if(qa_application_engine_shutdown_owner(f->engine_shutdown)!=f->application)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Network shutdown lost its retained ENGINE console and registry");
        if(!qa_application_engine_shutdown_read(f->engine_shutdown,&console,&cvars,error)) return false;
    }
    if (n->registered) {
        if (f->engine_shutdown) {
            if (!qa_console_remove_owner(console, NETWORK_OWNER, error)) return false;
        } else for (size_t i=0;i<sizeof(names)/sizeof(*names);++i) {
            uint64_t owner=0;
            if (qa_console_registration_owner(console,names[i],0,&owner) && owner==NETWORK_OWNER &&
                !qa_console_unregister(console,names[i],0))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Network callback lost its actual ENGINE registration");
        }
    }
    if (f->engine_shutdown) qa_cvars_remove_owner(cvars, NETWORK_OWNER);
    qa_downloads_destroy(n->downloads); frontend_q3_browser_destroy(n->q3_browser);
    frontend_kex_browser_destroy(n->kex_browser);
    qa_server_browser_destroy(n->browser); qa_server_admin_destroy(n->admin);
    qa_net_interfaces_destroy(n->interfaces);
    qa_buffer_free(&n->menu_connections_prefix);
    qa_network_destroy(n->runtime); qa_net_loopback_close(n->loopback);
    qa_q3_server_admission_destroy(n->q3_admission);
    qa_q3_server_authorization_destroy(n->q3_authorization);
    frontend_nq_destroy(n->nq_host);
    frontend_qw_destroy(n->qw_host);
    for (size_t i = 0; i < 64; ++i) qa_q3_download_window_destroy(n->q3_peers[i].download);
    frontend_q3_packages_destroy(n->q3_packages);
    client_attempts_dispose(n);
    qa_fs_root_close(n->preferences); qa_fs_root_close(n->content);
    free(n); f->network = NULL; return true;
}
static bool q1_client_tick_returned(qa_frontend_network *n,qa_error *error)
{
    if(!n->q1_client_owner) return true;
    if(!frontend_network_q1_client_tick(n->q1_client_owner,n->frontend->wall_time_ns,error)) return false;
    if(!frontend_network_q1_client_retired(n->q1_client_owner)) return true;
    if(!frontend_demo_dispatch_sources_returned(n->frontend->demos,error)) return false;
    if(n->demo_source.owner) return true;
    qa_error retirement={0};
    if(frontend_network_q1_client_destroy(&n->q1_client_owner,&retirement)) return true;
    if(retirement.code==QA_OK) return true;
    if(error) *error=retirement;
    return false;
}
static bool q2_client_tick_returned(qa_frontend_network *n,qa_error *error)
{
    if(!n->q2_client_owner) return true;
    if(!frontend_network_q2_client_tick(n->q2_client_owner,n->frontend->wall_time_ns,error)) return false;
    if(!frontend_network_q2_client_retired(n->q2_client_owner)) return true;
    if(!frontend_demo_dispatch_sources_returned(n->frontend->demos,error)) return false;
    if(n->demo_source.owner) return true;
    qa_error retirement={0};
    if(frontend_network_q2_client_destroy(&n->q2_client_owner,&retirement)) return true;
    if(retirement.code==QA_OK) return true;
    if(error) *error=retirement;
    return false;
}
bool frontend_network_admin_resume(qa_frontend *f,qa_error *error)
{
    if(!f) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source administration continuation requires its frontend");
    if(!frontend_config_store_admin_pending(f->config_store)) return true;
    if(!f->stepping || f->preparing || f->capture || f->resource_inventory || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source administration continuation requires the ordinary returned frame pump");
    if(f->shutdown || qa_application_should_stop(f->application) ||
        qa_application_startup_pending(f->application) || !qa_application_launch(f->application)) return true;
    qa_application_startup_source source; bool present=false;
    if(!frontend_config_store_primary_server_read(f->config_store,&source,&present,error)) return false;
    if(!present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source administration continuation lost its published primary Source");
    if(f->network) {
        qa_frontend_network *n=f->network;
        if(!n->runtime || !n->admin || n->busy || n->detached_transport || !qa_network_callbacks_idle(n->runtime))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source administration continuation requires its complete returned Network owner");
        return network_runtime_valid(n,true,error) && frontend_config_store_admin_adopt(f->config_store,error);
    }
    return frontend_network_create(f,error);
}
bool frontend_network_prepare(qa_frontend *f, qa_error *error)
{
    if(!frontend_network_admin_resume(f,error)) return false;
    qa_frontend_network *n = f->network; if (!n) return true;
    /* Console connection transitions retire their old wire owner before the
     * collector can poll or maintenance can flush that attempt. */
    if (!client_attempts_drain(n, error)) return false;
    if(!q2_local_groups_prepare(n,error) || !q2_timeout_sync(n,error)) return false;
    if(!q2_client_tick_returned(n,error)) return false;
    if(!q1_client_tick_returned(n,error)) return false;
    if(n->q2_host && !frontend_network_q2_host_tick(n->q2_host,f->wall_time_ns,error)) return false;
    if(!unified_tick(n,error)) return false;
    if (n->downloads && (!qa_downloads_pump(n->downloads, error) || !frontend_tools_sync(f, error))) return false;
    if (!q3_prepare(n, error) || !frontend_nq_prepare(n->nq_host, error) || !frontend_qw_prepare(n->qw_host, error)) return false;
    qa_admin_options policy;
    if (!admin_options(n,&policy,error) ||
        !qa_server_admin_policy(n->admin,policy.dialect,policy.deny_matches,policy.public_server,error)) return false;
    if (f->options.network_host && policy.dialect==QA_CONSOLE_Q3) {
        qa_application_startup_source source; bool present=false;
        if (!frontend_config_store_primary_server_read(f->config_store,&source,&present,error) || !present ||
            !qa_server_admin_refresh_masters(n->admin,source.cvars,error)) return false;
    }
    qa_server_browser_expire(n->browser, f->wall_time_ns);
    return true;
}
static bool transport_collect(void *context,uint64_t now,qa_net_transport_event *out,qa_error *error)
{ return qa_net_transport_collect(context,now,out,error); }
static bool browser_collect(void *context,uint64_t now,qa_net_transport_event *out,qa_error *error)
{ return frontend_kex_browser_collect(context,now,out,error); }
bool frontend_network_intake(qa_frontend *f,qa_platform_events *events,uint64_t now,qa_error *error)
{
    qa_frontend_network *n=f->network;
    if(!n) return true;
    ++n->busy;
    qa_net_transport *transport=qa_network_transport(n->runtime);
    qa_network_event_source source={.id=n->input_serial, .context=transport, .collect=transport_collect};
    bool ok=qa_net_transport_maintenance(transport,now,error) &&
        (!n->kex_browser || frontend_kex_browser_maintenance(n->kex_browser,now,error));
    if(ok && qa_network_receive_ready(n->runtime))
        ok=qa_network_events_collect(&source,events,now,256,error);
    if(ok && n->kex_browser) {
        source.destination=1; source.context=n->kex_browser; source.collect=browser_collect;
        ok=qa_network_events_collect(&source,events,now,256,error);
    }
    if (ok) for (uint32_t i = 0; i < f->options.seats; ++i) {
        frontend_local_client *local = n->local_clients + i;
        if (!local->runtime) continue;
        qa_net_transport *endpoint = qa_network_transport(local->runtime);
        source = (qa_network_event_source){.id = n->input_serial, .destination = (int32_t)i + 2,
            .context = endpoint, .collect = transport_collect};
        if (!qa_net_transport_maintenance(endpoint, now, error) ||
            (qa_network_receive_ready(local->runtime) &&
                !qa_network_events_collect(&source, events, now, 256, error))) { ok = false; break; }
    }
    --n->busy;
    return ok;
}
bool frontend_network_receive_ready(const qa_frontend *f)
{
    const qa_frontend_network *n=f?f->network:NULL;
    return !n || qa_network_receive_ready(n->runtime);
}
bool frontend_network_event_ready(const qa_frontend *f, const qa_platform_event *event)
{
    const qa_frontend_network *n = f ? f->network : NULL;
    if (!n || event->value2 == 1) return true;
    if (event->value2 >= 2) {
        uint32_t physical = (uint32_t)(event->value2 - 2);
        return physical >= f->options.seats || !n->local_clients[physical].runtime ||
            qa_network_receive_ready(n->local_clients[physical].runtime);
    }
    return qa_network_receive_ready(n->runtime);
}
bool frontend_network_local_input_owned(const qa_frontend *f, uint32_t physical)
{
    return f && f->network && physical < f->options.seats &&
        f->network->local_clients[physical].runtime != NULL;
}
bool frontend_network_receive(qa_frontend *f,const qa_platform_event *event,qa_bytes bytes,
    bool *consumed,qa_error *error)
{
    *consumed=true;
    qa_frontend_network *n=f->network;
    if(!n) return true;
    qa_net_transport_event input; uint64_t source_id;
    qa_network_event_packet(event,bytes,&source_id,&input);
    /* Closed owners leave copied input behind; their load serial drops it. */
    if(source_id!=n->input_serial) return true;
    const qa_net_transport_event *physical=input.packet.kind==QA_NET_POLL_EMPTY ? NULL : &input;
    ++n->busy;
    bool ok=true;
    if(event->value2==1) {
        if(n->kex_browser) ok=frontend_kex_browser_dispatch(n->kex_browser,physical,error);
    } else {
        qa_network_runtime *runtime = n->runtime;
        if (event->value2 >= 2) {
            uint32_t seat = (uint32_t)(event->value2 - 2);
            runtime = seat < f->options.seats ? n->local_clients[seat].runtime : NULL;
        }
        if (!runtime) { --n->busy; return true; }
        qa_net_datagram packet; bool present;
        ok=qa_net_transport_dispatch(qa_network_transport(runtime),physical,&packet,&present,error);
        if(ok && present) {
            ok=qa_network_receive(runtime,&packet,error);
            /* Keep the boundary until all saved logical deliveries have run. */
            if(!physical) *consumed=false;
        }
    }
    --n->busy;
    return ok;
}
bool frontend_network_maintenance(qa_frontend *f,qa_error *error)
{
    qa_frontend_network *n=f->network;
    if(!n) return true;
    ++n->busy;
    bool ok = qa_network_tick(n->runtime, f->wall_time_ns, error) && qa_server_browser_q3_pump(n->browser, f->wall_time_ns, error) &&
        frontend_q3_browser_poll(n->q3_browser, error) && qa_server_admin_tick(n->admin, f->wall_time_ns, false, error);
    --n->busy;
    return ok && q2_client_tick_returned(n,error) &&
        q1_client_tick_returned(n,error) &&
        unified_tick(n,error) && local_clients_tick(n,error) &&
        (!n->q2_host || frontend_network_q2_host_tick(n->q2_host,f->wall_time_ns,error)) &&
        (!n->q3_admission || q3_drain(n, error)) &&
        (!frontend_network_remote(f) || client_drain(&n->q3_clients[0], false, error)) &&
        frontend_nq_pump(n->nq_host, error) && frontend_qw_pump(n->qw_host, error);
}
static bool client_predictor_initial(frontend_q3_client *, bool *, qa_error *);
static bool client_copied_prediction(frontend_q3_client *n, bool *copied, qa_error *error)
{
    *copied=false;
    if(!n->q3_predictor || !n->q3_client_active || !n->q3_client_gamestate) return true;
    qa_application_q3_remote_source source; qa_application_native_q3_client_modules_recipe recipe;
    const qa_q3_client_peer *current=q3_view(n);
    if(!current || !qa_application_q3_remote_source_read(n->frontend->application,n->q3_cgame_owner,
        n->q3_client_launch_seat,n->q3_client_epoch,&source,error) ||
        !qa_application_native_q3_client_modules_recipe_read(n->frontend->application,&source,
            qa_q3_client_peer_gamestate(current),&recipe,error)) return false;
    *copied=!recipe.pure; return true;
}
bool frontend_network_client_frame(qa_frontend *f, qa_error *error)
{
    if (!f || !f->network) return true;
    for (uint32_t i = 0; i < f->options.seats; ++i) {
        frontend_q3_client *n = f->network->q3_clients + i;
        if (!n->q3_client_requested) continue;
        bool copied = false, ready = false;
        if (!client_drain(n, true, error) || !client_copied_prediction(n, &copied, error) ||
            (copied && !client_predictor_initial(n, &ready, error))) return false;
    }
    return true;
}
bool frontend_network_q3_input_owned(const qa_frontend *f, uint32_t physical)
{
    frontend_q3_client *n = q3_client_physical(f, physical);
    return n && n->q3_client_requested && n->runtime;
}
bool frontend_network_q1_input_owned(const qa_frontend *f,uint32_t physical)
{
    return f && physical<f->options.seats && f->network &&
        frontend_network_q1_client_owns_input(q1_client_at(f->network,physical),physical);
}
bool frontend_network_q1_frame_time(qa_frontend *f,const qa_cvars **cvars,
    uint64_t *source_ns,bool *handled,qa_error *error)
{
    if(!f || !cvars || !source_ns || !handled)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 clock requires its actual frontend and output");
    *cvars=NULL; *source_ns=0; *handled=false;
    qa_frontend_network *n=f->network;
    if(!n || !q1_client_at(n,0)) return true;
    if(n->busy || n->detached_transport || !qa_network_callbacks_idle(n->runtime))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 clock requires its returned live transport");
    return frontend_network_q1_client_frame_time(q1_client_at(n,0),cvars,source_ns,handled,error);
}
bool frontend_network_q1_input_prepare(const qa_frontend *f,uint32_t physical,
    bool *accepted,uint64_t *source_ns,uint64_t *wall_ns,qa_error *error)
{
    if(!f || physical>=f->options.seats || !f->network || !q1_client_at(f->network,physical))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 input admission lost its actual physical CLIENT");
    const qa_frontend_network *n=f->network;
    if(n->busy || n->detached_transport || !qa_network_callbacks_idle(n->runtime))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 input admission requires its returned live transport");
    return frontend_network_q1_client_input_prepare(q1_client_at(n,physical),physical,accepted,source_ns,wall_ns,error);
}
bool frontend_network_q1_input(qa_frontend *f,uint32_t physical,const qa_seat_input_sample *sample,
    uint64_t sequence,double source_frame_ms,bool *handled,qa_error *error)
{
    if(!f || !sample || !handled || physical>=f->options.seats)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 input requires its actual physical sample");
    *handled=false;
    qa_frontend_network *n=f->network;
    if(!n || !q1_client_at(n,physical)) return true;
    if(n->busy || n->detached_transport || !qa_network_callbacks_idle(n->runtime))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 input requires its returned live transport");
    return frontend_network_q1_client_input(q1_client_at(n,physical),physical,sample,sequence,source_frame_ms,handled,error);
}
bool frontend_network_q2_input_owned(const qa_frontend *f,uint32_t physical)
{
    return f && physical<f->options.seats && f->network &&
        frontend_network_q2_client_owns_input(q2_client_at(f->network,physical),physical);
}
bool frontend_network_q2_input(qa_frontend *f, uint32_t physical,
    const qa_seat_input_sample *sample, uint64_t sequence, bool *handled, qa_error *error)
{
    if (!f || !sample || !handled || physical >= f->options.seats)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 input requires its actual physical sample");
    *handled = false;
    frontend_remote_q2 *owner = NULL;
    frontend_remote_q2_view view;
    for (size_t i = 0; i < frontend_remote_q2_count(f); ++i) {
        frontend_remote_q2 *row = frontend_remote_q2_at(f, i);
        frontend_remote_q2_view candidate;
        if (!frontend_remote_q2_metadata_read(row, &candidate, error)) return false;
        if (candidate.retired || candidate.domain.physical_seat != physical) continue;
        if (owner) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 input has multiple physical CLIENT owners");
        owner = row; view = candidate;
    }
    if (!owner) {
        *handled=f->network && frontend_network_q2_client_owns_input(q2_client_at(f->network,physical),physical);
        return true;
    }
    *handled = true;
    if (!view.domain.client.owner) return true;
    qa_frontend_network *n = f->network;
    const qa_net_client *client = n ? qa_net_connections_get(qa_network_connections(view.domain.runtime), view.domain.client) : NULL;
    if (!client || client->seat_count != 1 ||
        client->seats[0].seat.owner != view.domain.seat.owner ||
        client->seats[0].seat.index != view.domain.seat.index || client->seats[0].remote_index != 0 ||
        qa_network_epoch(view.domain.runtime, client->id) != view.domain.epoch ||
        !frontend_remote_q2_current(&view))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 input lost its sole actual ordered connection seat");
    qa_q2_usercmd command; bool owned = false, ready = false;
    if (!frontend_remote_q2_input(f, physical, sample, sequence, &command, &owned, &ready, error) ||
        !owned || !frontend_remote_q2_current(&view)) return false;
    return !ready || qa_network_q2_client_usercmds(view.domain.runtime, client->id, &command, 1, error);
}
bool frontend_network_client_predictor_read(qa_frontend *f, frontend_remote_prediction **out,
    bool *present, qa_error *error)
{
    if(!f || !out || !present) return frontend_fail(error,QA_ERROR_ARGUMENT,"Prediction observation needs its actual frontend");
    *out=NULL; *present=false;
    qa_frontend_network *n=f->network;
    if(!n || !n->q3_clients[0].q3_client_requested || !n->q3_clients[0].q3_predictor) return true;
    if(n->q3_clients[0].q3_predictor_pending.data) return frontend_fail(error,QA_ERROR_ARGUMENT,"Prediction owner is still importing its native continuation");
    qa_application_q3_client_context receiver;
    if(!qa_application_q3_remote_context_read(f->application,n->q3_clients[0].q3_cgame_owner,n->q3_clients[0].q3_client_launch_seat,&receiver,error) ||
        !receiver.native_source || !qa_application_q3_remote_context_current(f->application,&receiver))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Prediction owner lost its actual compiled physical CLIENT");
    *out=frontend_network_predictor_read(n->q3_clients[0].q3_predictor); *present=true; return true;
}
bool frontend_network_client_predictor_finish_restore(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    if(!n || !n->q3_clients[0].q3_predictor_pending.data) return true;
    if(!n->detached_transport || n->q3_clients[0].q3_predictor || !n->q3_clients[0].q3_client_requested)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Prediction import requires its actual detached native candidate");
    qa_bytes bytes={n->q3_clients[0].q3_predictor_pending.data,n->q3_clients[0].q3_predictor_pending.size};
    frontend_network_predictor *restored=NULL;
    qa_application_q3_client_context receiver;
    if(!qa_application_q3_remote_context_read(f->application,n->q3_clients[0].q3_cgame_owner,
        n->q3_clients[0].q3_client_launch_seat,&receiver,error) ||
        !frontend_network_predictor_restore(f,&receiver,bytes,&restored,error)) return false;
    if(n->q3_clients[0].q3_predictor_zero_pending && frontend_remote_prediction_initialized(frontend_network_predictor_read(restored))) {
        frontend_network_predictor_destroy(restored);
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved initialized prediction retains an unconsumed reset receipt");
    }
    n->q3_clients[0].q3_predictor=restored;
    qa_buffer_free(&n->q3_clients[0].q3_predictor_pending); return true;
}
static bool client_predictor_initial(frontend_q3_client *n, bool *ready, qa_error *error)
{
    *ready=false;
    if(!n->q3_predictor) return true;
    frontend_remote_prediction *prediction=frontend_network_predictor_read(n->q3_predictor);
    frontend_remote_prediction_source source; bool present=false;
    if(!frontend_remote_prediction_source_read(prediction,&source,&present,error)) return false;
    if(!present) return true;
    if(frontend_remote_prediction_initialized(prediction)) { *ready=true; return true; }
    const qa_q3_client_peer *peer=q3_view(n);
    uint64_t number=n->q3_predictor_zero_sequence;
    const qa_q3_usercmd *zero=peer?qa_q3_client_peer_usercmd_at(peer,number):NULL;
    if(!n->q3_client_entered || !n->q3_predictor_zero_pending || !number || !peer ||
        qa_q3_client_peer_usercmd_number(peer)!=number || !zero ||
        zero->serverTime || zero->buttons || zero->weapon || zero->forwardmove || zero->rightmove || zero->upmove ||
        zero->angles[0] || zero->angles[1] || zero->angles[2])
        return frontend_fail(error,QA_ERROR_FORMAT,"Prediction lost its genuine zero reset receipt before physical input");
    qa_movement_command command={.kind=QA_MOVEMENT_Q3,.sequence=number};
    if(!frontend_remote_prediction_admit_initial(prediction,&command,error)) return false;
    n->q3_predictor_zero_sequence=0; n->q3_predictor_zero_pending=false;
    *ready=true; return true;
}
bool frontend_network_client_predictor_admit(qa_frontend *f, frontend_remote_prediction *prediction, qa_error *error)
{
    if (!f || !f->network) return false;
    for (uint32_t i = 0; i < f->options.seats; ++i) {
        frontend_q3_client *n = f->network->q3_clients + i;
        if (!n->q3_predictor || frontend_network_predictor_read(n->q3_predictor) != prediction) continue;
        bool copied = false, ready = false;
        return client_copied_prediction(n, &copied, error) && copied &&
            client_predictor_initial(n, &ready, error) && ready;
    }
    return false;
}
bool frontend_network_client_sample(qa_frontend *f, uint32_t seat, qa_actor_id actor,
    const qa_movement_command *selected, frontend_remote_prediction_angle_space angle_space,
    const qa_seat_input_sample *sample, double duration, qa_error *error)
{
    frontend_q3_client *n=q3_client_physical(f,seat); qa_actor_id actual;
    if(!n || !n->q3_client_requested || !selected || !sample ||
        !client_player(n,&actual,error) || !qa_actor_id_equal(actor,actual))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Paired remote input lost its real physical viewing player");
    if(!n->q3_client_attached || !n->q3_client_active || !n->q3_client_gamestate || n->q3_client_retiring || n->q3_client_closed)
        return true;
    bool copied=false;
    if(!client_copied_prediction(n,&copied,error)) return false;
    if(copied) {
        bool ready=false;
        if(!client_predictor_initial(n,&ready,error)) return false;
        if(!ready) return true;
    }
    qa_movement_command raw; bool present=false;
    if(!frontend_remote_input_build(n->q3_input,sample,duration,&raw,&present,error)) return false;
    if(!present) return true;
    const qa_q3_client_peer *peer=q3_view(n);
    uint64_t number=peer?qa_q3_client_peer_usercmd_number(peer):0;
    if(!peer || raw.kind!=QA_MOVEMENT_Q3 || raw.sequence!=number+1 || number==UINT64_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Paired raw input differs from its genuine next transport command");
    qa_q3_usercmd command={.serverTime=raw.server_time_ms,.buttons=(int32_t)raw.buttons,.weapon=raw.weapon,
        .forwardmove=(int8_t)raw.forward_move,.rightmove=(int8_t)raw.side_move,.upmove=(int8_t)raw.up_move};
    memcpy(command.angles,raw.angle_words,sizeof(command.angles));
    if(!qa_network_q3_client_usercmd(n->runtime,n->q3_client,&command,error)) return false;
    const qa_q3_usercmd *stored=qa_q3_client_peer_usercmd_at(peer,raw.sequence);
    if(!stored || stored->serverTime!=command.serverTime || stored->buttons!=command.buttons || stored->weapon!=command.weapon ||
        stored->forwardmove!=command.forwardmove || stored->rightmove!=command.rightmove || stored->upmove!=command.upmove ||
        stored->angles[0]!=command.angles[0] || stored->angles[1]!=command.angles[1] || stored->angles[2]!=command.angles[2])
        return frontend_fail(error,QA_ERROR_FORMAT,"Raw command append lost its actual retained native receipt");
    if(!copied) return true;
    frontend_remote_prediction *prediction=frontend_network_predictor_read(n->q3_predictor);
    frontend_remote_prediction_source source; bool available=false;
    if(!frontend_remote_prediction_source_read(prediction,&source,&available,error) || !available ||
        source.input.frame.sequence!=raw.sequence || source.input.frame.server_time_ms!=raw.server_time_ms ||
        source.input.frame.weapon!=raw.weapon || !frontend_network_predictor_source_current(n->q3_predictor,&source))
        return frontend_fail(error,QA_ERROR_FORMAT,"Paired prediction lost its actual appended source receipt");
    return frontend_remote_prediction_submit(prediction,selected,angle_space,&raw,error) &&
        (frontend_network_predictor_source_current(n->q3_predictor,&source) ||
         frontend_fail(error,QA_ERROR_FORMAT,"Paired prediction changed its retained physical source receipt"));
}
static uint64_t network_events_retired(qa_frontend_network *network)
{
    uint64_t next = frontend_network_unified_events_after(network ? network->unified : NULL);
    uint64_t retired = frontend_nq_events_retired(network ? network->nq_host : NULL);
    if (retired < next) next = retired;
    retired = frontend_qw_events_retired(network ? network->qw_host : NULL);
    if (retired < next) next = retired;
    retired = frontend_network_q2_host_events_retired(network ? network->q2_host : NULL);
    return retired < next ? retired : next;
}

static bool network_events_admit(qa_frontend *f, bool *ready, qa_error *error)
{
    qa_frontend_network *network = f->network;
    for (;;) {
        uint64_t minimum = network_events_retired(network);
        application_unified_events_consume(f->application, minimum);
        if (qa_application_events_admit(f->application, NULL)) {
            *ready = true;
            return true;
        }
        bool released = false;
        if (!frontend_network_unified_release_pressure(network ? network->unified : NULL,
            minimum, &released, error)) return false;
        if (!released && !frontend_nq_events_pressure(network ? network->nq_host : NULL,
            minimum, &released, error)) return false;
        if (!released && !frontend_qw_events_pressure(network ? network->qw_host : NULL,
            minimum, &released, error)) return false;
        if (!released && !frontend_network_q2_host_events_pressure(network ? network->q2_host : NULL,
            minimum, &released, error)) return false;
        if (!released) {
            *ready = false;
            return true;
        }
    }
}

bool frontend_network_tick(qa_frontend *f, uint64_t elapsed_ns, bool retiring_map, bool *source_ready, qa_error *error)
{
    if(!source_ready) return frontend_fail(error,QA_ERROR_ARGUMENT,"Network Source step requires an admission output");
    *source_ready=true;
    if(!f) return true;
    if(f->network && f->network->unified && !retiring_map) {
        if(!frontend_network_unified_step_ready(f->network->unified,source_ready,error)) return false;
        if(!*source_ready) return true;
    }
    if (!network_events_admit(f, source_ready, error)) return false;
    if (!*source_ready || !f->network) return true;
    if (f->network->unified && !retiring_map &&
        !frontend_network_unified_pre_frame(f->network->unified,error)) return false;
    return frontend_nq_tick(f->network->nq_host,elapsed_ns,retiring_map,error);
}
void frontend_network_events_consume(qa_frontend *f)
{
    application_unified_events_consume(f->application, network_events_retired(f->network));
}
bool frontend_network_publish(qa_frontend *f, qa_error *error)
{
    if(f->qc_messages && !frontend_qc_messages_drain(f->qc_messages,error))return false;
    if(!frontend_demo_dispatch_publish(f->demos,error))return false;
    if (!f->network) return true;
    qa_frontend_network *n = f->network;
    if(n->unified) {
        if(f->options.network_host) {
            application_unified_source source;
            if(!application_unified_source_read(f->application,&source,error)) return false;
            if(source.map_revision!=n->unified_map_revision) {
                if(!frontend_network_unified_travel(n->unified,NULL,0,error)) return false;
                n->unified_map_revision=source.map_revision;
            }
        }
        return frontend_network_unified_publish(n->unified,NULL,error);
    }
    if(frontend_network_client_only(f) && (n->q1_client_owner || n->q2_client_owner))
        return frontend_network_q1_client_idle(n->q1_client_owner) && frontend_network_q2_client_idle(n->q2_client_owner);
    if(n->q2_host) return frontend_network_q2_host_publish(n->q2_host,f->wall_time_ns,error);
    if (n->nq_host) return frontend_nq_publish(n->nq_host, error);
    if (n->qw_host) return frontend_qw_publish(n->qw_host, error);
    if (frontend_network_remote(f)) return client_drain(&n->q3_clients[0], false, error);
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
    if (!n) return (!f->options.network_host && !f->options.network_connect) ||
        frontend_fail(error, QA_ERROR_UNSUPPORTED, "Configured networking lacks its actual restart owner");
    if (n->q3_clients[0].q3_client_requested || n->nq_host || (f->options.network_host && !n->q3_admission))
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
    qa_actor_id actor = {0}; qa_net_client_id client = {0}; qa_net_seat_id seat = {0};
    bool remote = false, matched = false;
    if (!application_unified_source_drop_recipient(lease->frontend->application, lease->owner,
        slot, reason, &actor, &client, &seat, &remote, error)) return false;
    if (remote && n && n->unified && lease->frontend->options.network_host &&
        !frontend_network_unified_source_drop(n->unified, lease->owner, slot, reason, &matched, error))
        return false;
    if (remote && !matched && n) {
        frontend_q3_peer *recipient = NULL;
        for (size_t i = 0; i < 64; ++i) {
            frontend_q3_peer *peer = n->q3_peers + i;
            if (!peer->occupied || !qa_net_client_id_equal(peer->client, client)) continue;
            qa_actor_id actual;
            const qa_net_client *transport = qa_net_connections_get(qa_network_connections(n->runtime), client);
            if (recipient || peer->seat.owner != seat.owner || peer->seat.index != seat.index ||
                !transport || transport->protocol.kind != QA_NET_Q3_68 || transport->seat_count != 1 ||
                !transport->seats || transport->seats[0].seat.owner != seat.owner ||
                transport->seats[0].seat.index != seat.index)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Source DROP lost its actual raw Q3 full recipient");
            if (!q3_actor(peer, &actual, error)) return false;
            if (!qa_actor_id_equal(actual, actor))
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Source DROP changed its actual raw Q3 full actor");
            recipient = peer;
        }
        if (recipient) {
            if (!q3_drop(recipient, reason, error)) return false;
            matched = true;
        }
    }
    if (lease->original.drop_client)
        return lease->original.drop_client(lease->original.context, slot, reason, error);
    return !remote || matched || frontend_fail(error, QA_ERROR_UNSUPPORTED,
        "Source remote DROP has no matched transport owner");
}

bool frontend_network_qw_command_realtime(const qa_frontend *f,qa_actor_owner owner,
    qa_actor_id actor,uint64_t *out,qa_error *error)
{
    const qa_frontend_network *network=f?f->network:NULL;
    return network && network->frontend==f && network->qw_host?
        frontend_qw_command_realtime(network->qw_host,owner,actor,out,error):
        frontend_fail(error,QA_ERROR_ARGUMENT,"Remote QW chat has no actual action owner");
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

static bool client_restore_content_read(const qa_frontend_network *n, frontend_q3_content_view *out, qa_error *error)
{
    if(!n || !n->detached_transport || !n->q3_clients[0].q3_native_restore || !n->q3_clients[0].q3_client_content)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native data import lacks its detached staged content owner");
    return frontend_q3_content_native_restore_pending(n->q3_clients[0].q3_client_content) ?
        frontend_q3_content_native_restore_read(n->q3_clients[0].q3_client_content,out,error) :
        frontend_q3_content_read(n->q3_clients[0].q3_client_content,out,error);
}
bool frontend_network_client_restore_domain_read(const qa_frontend *f,
    frontend_network_client_domain *out, qa_error *error)
{
    const qa_frontend_network *n=f?f->network:NULL;
    const qa_q3_client_peer *peer=n?qa_network_q3_client_view(n->runtime,n->q3_clients[0].q3_client):NULL;
    frontend_network_client_domain domain={0}; frontend_q3_content_view content;
    if(!out || !n || !peer || !n->q3_clients[0].q3_initial_tuple || !n->q3_clients[0].q3_client_decoded ||
        !client_restore_content_read(n,&content,error) || !content.map ||
        !qa_application_q3_remote_source_read(f->application,n->q3_clients[0].q3_cgame_owner,n->q3_clients[0].q3_client_launch_seat,
            n->q3_clients[0].q3_client_epoch,&domain.source,error) || !domain.source.receiver.native_source ||
        !frontend_network_q3_client_context_read((qa_frontend *)f,n->q3_clients[0].q3_cgame_owner,n->q3_clients[0].q3_client_launch_seat,
            &domain.source.receiver,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native graph import lacks its real source, map and recorded Init entry");
    domain.connection=n->q3_clients[0].q3_client; domain.epoch=n->q3_clients[0].q3_client_epoch;
    domain.restart_generation=n->q3_clients[0].q3_client_restart_generation; domain.content_owner=n->q3_clients[0].q3_client_content;
    domain.content=domain.source.descriptor->content; domain.prepared_mounts=content.mounts;
    domain.map=content.map; domain.product=content.selected; domain.gamestate=qa_q3_client_peer_gamestate(peer);
    domain.initial=(qa_network_q3_client_init){n->q3_clients[0].q3_initial_message,n->q3_clients[0].q3_initial_command,domain.gamestate->client_number};
    if(!frontend_network_client_restore_domain_current(f,&domain))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native graph import changed its actual staged binding");
    *out=domain; return true;
}
static bool client_restore_receiver_same(const qa_application_q3_client_context *a,
    const qa_application_q3_client_context *b)
{
    return a->session==b->session && a->receiver==b->receiver && a->seat==b->seat &&
        a->source_owner==b->source_owner && qa_actor_id_equal(a->source_actor,b->source_actor) &&
        a->source_client==b->source_client && a->source_cvars==b->source_cvars &&
        a->service_owner==b->service_owner && a->frontend_lifetime==b->frontend_lifetime &&
        a->console==b->console && a->cvars==b->cvars && a->client_time_cvars==b->client_time_cvars &&
        a->client_time_owner==b->client_time_owner && a->native_source==b->native_source &&
        a->initialized==b->initialized && a->source_milliseconds==b->source_milliseconds &&
        a->source_frame.provider==b->source_frame.provider && a->source_frame.kind==b->source_frame.kind &&
        a->source_frame.phase==b->source_frame.phase && a->source_frame.number==b->source_frame.number &&
        a->source_frame.start_ns==b->source_frame.start_ns && a->source_frame.elapsed_ns==b->source_frame.elapsed_ns &&
        a->source_frame.time_ns==b->source_frame.time_ns &&
        a->command_context.session==b->command_context.session && a->command_context.owner==b->command_context.owner &&
        a->command_context.client==b->command_context.client && a->command_context.seat==b->command_context.seat &&
        a->command_context.dialect==b->command_context.dialect && a->command_context.origin==b->command_context.origin &&
        a->command_context.direct==b->command_context.direct && a->command_context.console_text==b->command_context.console_text &&
        a->command_context.script==b->command_context.script && a->command_context.registry==b->command_context.registry &&
        a->command_context.generation==b->command_context.generation &&
        qa_actor_id_equal(a->command_context.actor,b->command_context.actor);
}
bool frontend_network_client_restore_domain_current(const qa_frontend *f,
    const frontend_network_client_domain *domain)
{
    const qa_frontend_network *n=f?f->network:NULL;
    const qa_q3_client_peer *peer=n?qa_network_q3_client_view(n->runtime,n->q3_clients[0].q3_client):NULL;
    frontend_q3_content_view content; frontend_remote_config_view configuration;
    qa_application_q3_client_context actual;
    qa_error ignored={0};
    if(!domain || !n || n->frontend!=f || !peer || !n->q3_clients[0].q3_initial_tuple || !n->q3_clients[0].q3_client_decoded ||
        !n->detached_transport || !n->q3_clients[0].q3_native_restore || n->q3_clients[0].q3_client_retiring || n->q3_clients[0].q3_client_closed ||
        !qa_network_callbacks_idle(n->runtime) || !domain->source.receiver.native_source ||
        !client_restore_content_read(n,&content,&ignored) ||
        !client_connection_current((void *)&n->q3_clients[0],n->q3_clients[0].q3_client_epoch,&ignored) ||
        !client_configuration_view(f && f->network ? f->network->q3_clients : NULL,&configuration,&ignored) ||
        !frontend_network_q3_client_context_read((qa_frontend *)f,n->q3_clients[0].q3_cgame_owner,n->q3_clients[0].q3_client_launch_seat,
            &actual,&ignored)) return false;
    const qa_q3_gamestate *state=qa_q3_client_peer_gamestate(peer);
    const qa_application_q3_client_context *receiver=&domain->source.receiver;
    return qa_net_client_id_equal(domain->connection,n->q3_clients[0].q3_client) && domain->epoch==n->q3_clients[0].q3_client_epoch &&
        domain->restart_generation==n->q3_clients[0].q3_client_restart_generation && domain->content_owner==n->q3_clients[0].q3_client_content &&
        domain->source.connection_epoch==domain->epoch && receiver->receiver==n->q3_clients[0].q3_cgame_owner &&
        receiver->seat==n->q3_clients[0].q3_client_launch_seat && receiver->console==configuration.console &&
        receiver->cvars==configuration.cvars && configuration.scope.provider==receiver->receiver &&
        configuration.scope.seat==receiver->seat &&
        qa_application_q3_remote_source_current(f->application,&domain->source) &&
        client_restore_receiver_same(receiver,&actual) &&
        frontend_network_q3_client_context_current((qa_frontend *)f,receiver) &&
        receiver->source_milliseconds==n->q3_clients[0].q3_client_time &&
        domain->source.descriptor && domain->source.descriptor->content==domain->content && domain->content &&
        state && state==domain->gamestate && state->client_number>=0 && state->client_number<64 &&
        receiver->source_client==(uint32_t)state->client_number && content.gamestate &&
        content.gamestate->client_number==state->client_number && content.gamestate->checksum_feed==state->checksum_feed &&
        content.map==domain->map && domain->map && content.mounts==domain->prepared_mounts &&
        content.selected==domain->product && domain->initial.client_number==state->client_number &&
        domain->initial.server_message==n->q3_clients[0].q3_initial_message &&
        domain->initial.last_executed_server_command==n->q3_clients[0].q3_initial_command;
}
bool frontend_network_client_restore_adopt(qa_frontend *f,
    const frontend_network_client_domain *domain, frontend_remote_q3 *row, qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    frontend_remote_q3_resources resources;
    if(!n || !n->detached_transport || !n->q3_clients[0].q3_native_restore || !row ||
        frontend_remote_q3_frontend(row)!=f || (n->q3_clients[0].q3_session && n->q3_clients[0].q3_session!=row) ||
        n->q3_clients[0].q3_initial || n->q3_clients[0].q3_initial_modules ||
        !frontend_network_client_restore_domain_current(f,domain) ||
        !frontend_remote_q3_resources_import_read(row,&resources,error) ||
        !frontend_remote_q3_resources_import_current(&resources) || resources.owner!=row ||
        resources.domain.content_owner!=domain->content_owner || resources.domain.map!=domain->map ||
        resources.domain.prepared_mounts!=domain->prepared_mounts || resources.domain.content!=domain->content ||
        resources.domain.source.descriptor->storage!=domain->source.descriptor->storage ||
        resources.domain.source.configuration_generation!=domain->source.configuration_generation ||
        resources.domain.epoch!=domain->epoch || resources.domain.restart_generation!=domain->restart_generation ||
        !qa_net_client_id_equal(resources.domain.connection,domain->connection) ||
        !client_restore_receiver_same(&resources.domain.source.receiver,&domain->source.receiver))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native graph adoption requires its actual candidate parent");
    n->q3_clients[0].q3_session=row;
    n->q3_clients[0].q3_session_source=resources.domain.source;
    if(!frontend_network_client_restore_domain_current(f,domain) ||
        !frontend_remote_q3_resources_import_current(&resources))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Adopted native parent differs from its staged source binding");
    return true;
}
bool frontend_network_client_restore_finish(qa_frontend *f, qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    if(!n || !n->q3_clients[0].q3_native_restore) return true;
    if(!n->detached_transport || !n->q3_clients[0].q3_download_pending.data)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native import completion lacks its actual retained download cut");
    if(frontend_q3_content_native_restore_pending(n->q3_clients[0].q3_client_content) &&
        !frontend_q3_content_native_restore_finish(n->q3_clients[0].q3_client_content,error)) return false;
    if(n->q3_clients[0].q3_client_gamestate && !frontend_q3_content_media_current(n->q3_clients[0].q3_client_content,error)) return false;
    if(!n->q3_clients[0].q3_client_downloads) {
        qa_q3_client_download_bindings bindings;
        if(!client_download_bindings(&n->q3_clients[0],&bindings,error) ||
            !qa_q3_client_downloads_restore((qa_bytes){n->q3_clients[0].q3_download_pending.data,n->q3_clients[0].q3_download_pending.size},
                &bindings,&n->q3_clients[0].q3_client_downloads,error)) return false;
    }
    if(!network_runtime_valid(n,false,error)) return false;
    n->q3_clients[0].q3_native_restore=false; qa_buffer_free(&n->q3_clients[0].q3_download_pending);
    return true;
}
bool frontend_network_initial_metadata_read(const qa_frontend *f,
    frontend_network_client_attempt *out, frontend_remote_q3_initial **owner,
    frontend_remote_q3_modules **modules, bool *present, qa_error *error)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(!f || !f->resource_inventory || !out || !owner || !modules || !present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial resource inventory lacks its real observation fence");
    *out=(frontend_network_client_attempt){0}; *owner=NULL; *modules=NULL; *present=false;
    if(!n || (!n->q3_clients[0].q3_initial && !n->q3_clients[0].q3_initial_modules)) return true;
    *owner=n->q3_clients[0].q3_initial; *modules=n->q3_clients[0].q3_initial_modules; *present=true;
    frontend_network_client_attempt actual={0};
    if(!n->q3_clients[0].q3_initial || !client_configuration_view(f && f->network ? f->network->q3_clients : NULL,&actual.configuration,error) ||
        !qa_application_q3_remote_source_read(f->application,n->q3_clients[0].q3_cgame_owner,n->q3_clients[0].q3_client_launch_seat,
            n->q3_clients[0].q3_client_epoch,&actual.source,error)) return false;
    actual.connection=n->q3_clients[0].q3_client_attached?n->q3_clients[0].q3_client:(qa_net_client_id){0};
    actual.endpoint=n->q3_clients[0].q3_client_admission.address; actual.epoch=n->q3_clients[0].q3_client_epoch;
    actual.restart_generation=n->q3_clients[0].q3_client_restart_generation; actual.phase=n->q3_clients[0].q3_client_admission.phase;
    actual.attached=n->q3_clients[0].q3_client_attached;
    if(!frontend_network_initial_metadata_current(f,&actual,*owner,*modules))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial resource inventory differs from its retained physical namespace");
    *out=actual; return true;
}
bool frontend_network_initial_metadata_current(const qa_frontend *f,
    const frontend_network_client_attempt *attempt, const frontend_remote_q3_initial *owner,
    const frontend_remote_q3_modules *modules)
{
    const qa_frontend_network *n=f?f->network:NULL;
    frontend_network_client_attempt actual={0};
    if(!f || !f->resource_inventory || !n || n->frontend!=f || !attempt || !owner ||
        owner!=n->q3_clients[0].q3_initial || modules!=n->q3_clients[0].q3_initial_modules ||
        !qa_network_callbacks_idle(n->runtime) || !n->q3_clients[0].q3_session_source.descriptor ||
        !client_configuration_view(f && f->network ? f->network->q3_clients : NULL,&actual.configuration,NULL) ||
        !qa_application_q3_remote_source_read(f->application,n->q3_clients[0].q3_cgame_owner,n->q3_clients[0].q3_client_launch_seat,
            n->q3_clients[0].q3_client_epoch,&actual.source,NULL) || !actual.source.receiver.native_source ||
        actual.source.receiver.initialized || actual.source.descriptor->storage!=n->q3_clients[0].q3_session_source.descriptor->storage ||
        actual.source.configuration_generation!=n->q3_clients[0].q3_session_source.configuration_generation ||
        actual.source.connection_epoch!=n->q3_clients[0].q3_session_source.connection_epoch) return false;
    actual.endpoint=n->q3_clients[0].q3_client_admission.address; actual.epoch=n->q3_clients[0].q3_client_epoch;
    actual.restart_generation=n->q3_clients[0].q3_client_restart_generation;
    if(n->q3_clients[0].q3_client_attached) {
        const qa_net_client *client=qa_net_connections_get(qa_network_connections(n->runtime),n->q3_clients[0].q3_client);
        if(!client || !qa_network_q3_client_live(n->runtime,n->q3_clients[0].q3_client) ||
            qa_network_epoch(n->runtime,n->q3_clients[0].q3_client)!=1 || client->protocol.kind!=QA_NET_Q3_68 ||
            client->protocol.flags || client->protocol.revision || client->attachment!=QA_NET_REMOTE ||
            client->seat_count!=1 || client->seats[0].seat.owner!=NETWORK_OWNER || client->seats[0].seat.index ||
            client->seats[0].remote_index || !qa_net_address_equal(&client->endpoint,&actual.endpoint,true)) return false;
    }
    frontend_network_client_attempt held=actual; held.source=n->q3_clients[0].q3_session_source;
    return client_attempt_owner_current(f,&held,&actual) && client_attempt_owner_current(f,attempt,&actual) &&
        attempt->attached==n->q3_clients[0].q3_client_attached && attempt->phase==n->q3_clients[0].q3_client_admission.phase &&
        qa_net_client_id_equal(attempt->connection,n->q3_clients[0].q3_client_attached?n->q3_clients[0].q3_client:(qa_net_client_id){0});
}
bool frontend_network_restore_publication_read(const qa_frontend *f,
    q3n_remote_publication *out, qa_error *error)
{
    frontend_network_client_domain domain; qa_network_q3_client_init counters;
    if(!out || !frontend_network_client_restore_domain_read(f,&domain,error) ||
        !qa_network_q3_client_init_read(f->network->runtime,domain.connection,&counters,error)) return false;
    const qa_q3_client_peer *peer=qa_network_q3_client_view(f->network->runtime,domain.connection);
    if(!peer) return frontend_fail(error,QA_ERROR_ARGUMENT,"Native graph publication lost its imported transport owner");
    const qa_q3_snapshot *latest=qa_q3_client_peer_snapshot(peer);
    q3n_remote_publication publication={.connection=domain.connection,.epoch=domain.epoch,
        .restart_generation=domain.restart_generation,.gamestate=domain.gamestate,
        .initial_message=domain.initial.server_message,.initial_command=domain.initial.last_executed_server_command,
        .latest_message=latest?latest->message_number:0,.latest_time=latest?latest->server_time:0,
        .presentation_time=f->network->q3_clients[0].q3_client_time,.server_message=counters.server_message,
        .received_command=qa_q3_client_peer_server_command_sequence(peer),
        .executed_command=counters.last_executed_server_command,
        .initializing=f->network->q3_clients[0].q3_client_initializing,.has_snapshot=latest!=NULL,
        .demo_playback=qa_q3_client_peer_demo(peer)};
    if(!remote_player(f->application,&publication.viewer,error) ||
        !frontend_network_client_restore_domain_current(f,&domain))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native graph publication changed its actual source binding");
    *out=publication; return true;
}
bool frontend_network_restore_publication_current(const qa_frontend *f,
    const q3n_remote_publication *publication)
{
    q3n_remote_publication actual;
    return publication && frontend_network_restore_publication_read(f,&actual,NULL) &&
        qa_net_client_id_equal(publication->connection,actual.connection) && publication->epoch==actual.epoch &&
        publication->restart_generation==actual.restart_generation && publication->gamestate==actual.gamestate &&
        qa_actor_id_equal(publication->viewer,actual.viewer) && publication->initial_message==actual.initial_message &&
        publication->initial_command==actual.initial_command && publication->latest_message==actual.latest_message &&
        publication->latest_time==actual.latest_time && publication->presentation_time==actual.presentation_time &&
        publication->server_message==actual.server_message && publication->received_command==actual.received_command &&
        publication->executed_command==actual.executed_command && publication->initializing==actual.initializing &&
        publication->has_snapshot==actual.has_snapshot && publication->demo_playback==actual.demo_playback;
}
bool frontend_network_restore_command_current(const qa_frontend *f, const q3n_remote_command *command)
{
    if(!command || !frontend_network_restore_publication_current(f,&command->publication)) return false;
    const qa_frontend_network *n=f->network;
    if(!n->q3_clients[0].q3_reliable_receipt || command->sequence!=n->q3_clients[0].q3_reliable_receipt_sequence ||
        command->present!=n->q3_clients[0].q3_command_present) return false;
    if(command->present) return command->tokens==&n->q3_clients[0].q3_reached_command &&
        command->sequence==n->q3_clients[0].q3_reached_command_sequence && command->sequence==command->publication.executed_command;
    const qa_q3_client_peer *peer=qa_network_q3_client_view(n->runtime,n->q3_clients[0].q3_client);
    return !command->tokens && peer && command->sequence<=command->publication.received_command &&
        (command->sequence==command->publication.executed_command ||
         (qa_q3_client_peer_demo(peer) &&
          (int64_t)command->sequence<=(int64_t)command->publication.received_command-64));
}
bool frontend_network_client_restore_attempt_read(const qa_frontend *f,
    frontend_network_client_attempt *out, bool *present, qa_error *error)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(!f || !out || !present || !n || n->frontend!=f || !n->detached_transport)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI import requires its real detached Network candidate");
    *out=(frontend_network_client_attempt){0}; *present=false;
    if(!n->q3_clients[0].q3_client_requested || n->q3_clients[0].q3_client_decoded || n->q3_clients[0].q3_client_closed || n->q3_clients[0].q3_client_retiring ||
        n->q3_clients[0].q3_client_rebind || n->q3_clients[0].q3_client_admission.phase==QA_Q3_DISCONNECTED) return true;
    frontend_network_client_attempt actual={0};
    if(n->q3_clients[0].q3_client_content || n->q3_clients[0].q3_client_downloads || n->q3_clients[0].q3_native_restore || n->q3_clients[0].q3_client_initializing ||
        n->q3_clients[0].q3_initial_tuple || n->q3_clients[0].q3_client_gamestate || !n->q3_clients[0].q3_client_epoch ||
        n->q3_clients[0].q3_client_generation!=qa_application_configuration_generation(f->application) ||
        !qa_network_callbacks_idle(n->runtime) || !qa_network_local_address(n->runtime) ||
        !client_configuration_view(f && f->network ? f->network->q3_clients : NULL,&actual.configuration,error) ||
        !qa_application_q3_remote_source_read(f->application,n->q3_clients[0].q3_cgame_owner,n->q3_clients[0].q3_client_launch_seat,
            n->q3_clients[0].q3_client_epoch,&actual.source,error) || !actual.source.receiver.native_source ||
        actual.source.receiver.initialized || !actual.source.descriptor ||
        actual.source.descriptor->selection.runtime!=QA_PROGRAM_BUILTIN || actual.source.descriptor->artifact ||
        (n->q3_clients[0].q3_client_attached && !client_connection_current((void *)&n->q3_clients[0],n->q3_clients[0].q3_client_epoch,error)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI import differs from its actual CLIENT configuration and absent gamestate");
    actual.connection=n->q3_clients[0].q3_client_attached?n->q3_clients[0].q3_client:(qa_net_client_id){0};
    actual.endpoint=n->q3_clients[0].q3_client_admission.address; actual.epoch=n->q3_clients[0].q3_client_epoch;
    actual.restart_generation=n->q3_clients[0].q3_client_restart_generation;
    actual.phase=n->q3_clients[0].q3_client_admission.phase; actual.attached=n->q3_clients[0].q3_client_attached;
    if(!client_attempt_owner_current(f,&actual,&actual))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI import changed its physical CLIENT prefix");
    *out=actual; *present=true; return true;
}
bool frontend_network_client_restore_attempt_current(const qa_frontend *f,
    const frontend_network_client_attempt *attempt)
{
    frontend_network_client_attempt actual; bool present;
    return attempt && frontend_network_client_restore_attempt_read(f,&actual,&present,NULL) && present &&
        client_attempt_owner_current(f,attempt,&actual) && attempt->attached==actual.attached &&
        attempt->phase==actual.phase && qa_net_client_id_equal(attempt->connection,actual.connection);
}
bool frontend_network_client_restore_attempt_services(qa_frontend *f,
    const frontend_network_client_attempt *attempt, frontend_network_initial_services_binding *binding,
    void *lease_context,
    bool (*entered)(void *, const frontend_network_client_attempt *, qa_error *),
    qa_q3_host_client_services *out, qa_error *error)
{
    if(!binding || !out || !entered || !frontend_network_client_restore_attempt_current(f,attempt))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI import lacks its actual staged physical namespace");
    *binding=(frontend_network_initial_services_binding){.frontend=f,.application=f->application,
        .network=f->network,.attempt=*attempt,.context=lease_context,.entered=entered,.restore_candidate=true};
    *out=(qa_q3_host_client_services){.context=binding,.gamestate=initial_service_gamestate,
        .current_snapshot=initial_service_current_snapshot,.snapshot=initial_service_snapshot,
        .server_command=initial_service_server_command,.current_command=initial_service_current_command,
        .user_command=initial_service_user_command,.command_values=initial_service_command_values,
        .source_actor=initial_service_source_actor,.configstring_absent=initial_service_configstring_absent};
    return true;
}
bool frontend_network_client_restore_initial_adopt(qa_frontend *f,
    const frontend_network_client_attempt *attempt, frontend_remote_q3_initial *owner,
    frontend_remote_q3_modules *modules, qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    frontend_remote_q3_initial_view actual;
    if(!n || !n->detached_transport || !owner || n->q3_clients[0].q3_session ||
        (n->q3_clients[0].q3_initial && n->q3_clients[0].q3_initial!=owner) ||
        (n->q3_clients[0].q3_initial_modules && n->q3_clients[0].q3_initial_modules!=modules) ||
        !frontend_network_client_restore_attempt_current(f,attempt) ||
        frontend_remote_q3_initial_frontend(owner)!=f ||
        !frontend_remote_q3_initial_import_read(owner,&actual,error) ||
        !frontend_remote_q3_initial_import_current(&actual) || actual.owner!=owner ||
        !client_attempt_owner_current(f,attempt,&actual.attempt) ||
        actual.attempt.attached!=attempt->attached || actual.attempt.phase!=attempt->phase ||
        !qa_net_client_id_equal(actual.attempt.connection,attempt->connection) ||
        (modules && frontend_remote_q3_modules_initial_parent(modules)!=owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI adoption lacks its actual restored structural parents");
    n->q3_clients[0].q3_initial=owner; n->q3_clients[0].q3_initial_modules=modules;
    n->q3_clients[0].q3_session_source=actual.attempt.source;
    return frontend_network_client_restore_attempt_current(f,attempt) &&
        frontend_remote_q3_initial_import_current(&actual);
}
bool frontend_network_client_restore_initial_completed_current(const qa_frontend *f,
    const frontend_remote_q3_initial *owner,const frontend_network_client_attempt *attempt)
{
    const qa_frontend_network *n=f?f->network:NULL;
    return n && n->frontend==f && n->detached_transport && n->q3_clients[0].q3_connections_prefix.data &&
        n->q3_clients[0].q3_initial_restore_complete && owner && n->q3_clients[0].q3_initial==owner && !n->q3_clients[0].q3_session &&
        frontend_remote_q3_initial_frontend(owner)==f && n->q3_clients[0].q3_initial_modules &&
        frontend_remote_q3_modules_initial_parent(n->q3_clients[0].q3_initial_modules)==owner &&
        !frontend_remote_q3_modules_retired(n->q3_clients[0].q3_initial_modules) &&
        qa_application_native_q3_client_modules_idle(frontend_remote_q3_modules_owner(n->q3_clients[0].q3_initial_modules)) &&
        frontend_network_client_restore_attempt_current(f,attempt);
}
bool frontend_network_client_restore_initial_finish(qa_frontend *f,qa_error *error)
{
    qa_frontend_network *n=f?f->network:NULL;
    frontend_network_client_attempt attempt; bool present=false;
    if(!n || !n->q3_clients[0].q3_initial) return true;
    if(!frontend_network_client_restore_attempt_read(f,&attempt,&present,error) || !present) return false;
    if(n->q3_clients[0].q3_initial_restore_complete)
        return frontend_network_client_restore_initial_completed_current(f,n->q3_clients[0].q3_initial,&attempt) ||
            frontend_fail(error,QA_ERROR_ARGUMENT,"Completed initial import lost its actual retained candidate");
    frontend_remote_q3_initial_view parent; frontend_remote_q3_module_media ui;
    if(!n->q3_clients[0].q3_connections_prefix.data || !n->q3_clients[0].q3_initial_modules || n->q3_clients[0].q3_session || n->busy ||
        !frontend_remote_q3_initial_import_read(n->q3_clients[0].q3_initial,&parent,error) ||
        !frontend_remote_q3_initial_import_current(&parent) ||
        !frontend_remote_q3_initial_idle(n->q3_clients[0].q3_initial) ||
        frontend_remote_q3_modules_initial_parent(n->q3_clients[0].q3_initial_modules)!=n->q3_clients[0].q3_initial ||
        !frontend_remote_q3_modules_idle(n->q3_clients[0].q3_initial_modules) ||
        !frontend_remote_q3_modules_media_read(n->q3_clients[0].q3_initial_modules,QA_QVM_UI,&ui,error) ||
        !frontend_remote_q3_modules_media_current(&ui) || ui.physical_seat!=parent.physical_seat ||
        ui.receipt.receiver!=attempt.source.receiver.receiver || ui.receipt.seat!=attempt.source.receiver.seat ||
        ui.receipt.connection_epoch!=attempt.source.connection_epoch ||
        !ui.receipt.descriptor || ui.receipt.descriptor->storage!=attempt.source.descriptor->storage ||
        !frontend_network_client_restore_attempt_current(f,&attempt))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial import completion lacks its actual returned UI continuation");
    n->q3_clients[0].q3_initial_restore_complete=true;
    if(!frontend_remote_q3_initial_finish_import(n->q3_clients[0].q3_initial,error)) {
        n->q3_clients[0].q3_initial_restore_complete=false; return false;
    }
    return frontend_network_client_restore_initial_completed_current(f,n->q3_clients[0].q3_initial,&attempt) &&
        frontend_remote_q3_modules_media_current(&ui);
}

static bool restore_services_current(frontend_network_restore_services_binding *binding,
    frontend_network_client_domain *out, qa_error *error)
{
    if(!binding || !binding->frontend || binding->frontend->application!=binding->application ||
        binding->frontend->network!=binding->network)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported DATA facade lost its retained candidate owner");
    const qa_frontend_network *n=binding->frontend->network;
    frontend_network_client_domain actual;
    bool read=n && n->detached_transport ?
        frontend_network_client_restore_domain_read(binding->frontend,&actual,NULL):
        frontend_network_client_domain_read(binding->frontend,&binding->domain.source.receiver,&actual,NULL);
    const frontend_network_client_domain *held=&binding->domain;
    const frontend_q3_client *client=q3_client_receiver(binding->frontend,&held->source.receiver);
    if(read) {
        qa_application_q3_client_context receiver=held->source.receiver;
        /* Service restore alone authors this physical row's Init transition.
         * DATA admission keeps its namespace; it makes no Init/media claim. */
        receiver.initialized=actual.source.receiver.initialized;
        if(qa_net_client_id_equal(held->connection,actual.connection) && held->epoch==actual.epoch &&
            held->restart_generation==actual.restart_generation && held->content_owner==actual.content_owner &&
            held->content==actual.content && held->prepared_mounts==actual.prepared_mounts && held->map==actual.map &&
            held->product==actual.product && held->gamestate==actual.gamestate &&
            held->source.descriptor->storage==actual.source.descriptor->storage &&
            held->source.configuration_generation==actual.source.configuration_generation &&
            held->source.connection_epoch==actual.source.connection_epoch &&
            client_restore_receiver_same(&receiver,&actual.source.receiver) &&
            held->initial.server_message==actual.initial.server_message &&
            held->initial.last_executed_server_command==actual.initial.last_executed_server_command &&
            held->initial.client_number==actual.initial.client_number) {
            if(out) *out=actual;
            return true;
        }
    }
    if(client && client->q3_client_epoch==held->epoch && client->q3_client_restart_generation==held->restart_generation &&
        client->q3_cgame_owner==held->source.receiver.receiver && client->q3_client_launch_seat==held->source.receiver.seat &&
        qa_net_client_id_equal(client->q3_client,held->connection) && client->q3_client_content==held->content_owner &&
        binding->entered && binding->entered(binding->context,held,error)) {
        if(out) *out=*held;
        return true;
    }
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported DATA facade lost its actual source or entered role namespace");
}
static frontend_seat *restore_service_seat(const frontend_network_restore_services_binding *binding)
{
    frontend_q3_client *n = q3_client_receiver(binding->frontend, &binding->domain.source.receiver);
    return n ? binding->frontend->seats + n->physical : NULL;
}
static const qa_q3_gamestate *restore_service_gamestate(void *context)
{
    frontend_network_client_domain domain;
    return restore_services_current(context,&domain,NULL)?domain.gamestate:NULL;
}
static bool restore_service_current_snapshot(void *context, int32_t *number, int32_t *time, qa_error *error)
{
    frontend_network_restore_services_binding *binding=context;
    return restore_services_current(binding,NULL,error) && service_current_snapshot(restore_service_seat(binding),number,time,error);
}
static bool restore_service_snapshot(void *context, int32_t number,
    const qa_q3_snapshot **out, int32_t *ping, qa_error *error)
{
    frontend_network_restore_services_binding *binding=context;
    return restore_services_current(binding,NULL,error) && service_snapshot(restore_service_seat(binding),number,out,ping,error);
}
static bool restore_service_server_command(void *context, int32_t sequence, bool *present, qa_error *error)
{
    frontend_network_restore_services_binding *binding=context;
    if(!restore_services_current(binding,NULL,error)) return false;
    if(binding->frontend->network->detached_transport)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported DATA facade cannot replay reliable command execution");
    return service_server_command(restore_service_seat(binding),sequence,present,error) && restore_services_current(binding,NULL,error);
}
static int32_t restore_service_current_command(void *context)
{
    frontend_network_restore_services_binding *binding=context;
    return restore_services_current(binding,NULL,NULL)?service_current_command(restore_service_seat(binding)):0;
}
static bool restore_service_user_command(void *context, int32_t number,
    qa_q3_usercmd *out, bool *present, qa_error *error)
{
    frontend_network_restore_services_binding *binding=context;
    return restore_services_current(binding,NULL,error) && service_user_command(restore_service_seat(binding),number,out,present,error);
}
static bool restore_service_command_values(void *context, int32_t weapon, float sensitivity, qa_error *error)
{
    frontend_network_restore_services_binding *binding=context;
    if(!restore_services_current(binding,NULL,error)) return false;
    if(binding->frontend->network->detached_transport)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported DATA facade cannot publish live input values");
    return service_command_values(restore_service_seat(binding),weapon,sensitivity,error) && restore_services_current(binding,NULL,error);
}
static bool restore_service_source_actor(void *context, uint32_t number,
    qa_actor_id *out, bool *present, qa_error *error)
{
    frontend_network_restore_services_binding *binding=context;
    return restore_services_current(binding,NULL,error) && service_source_actor(restore_service_seat(binding),number,out,present,error);
}
bool frontend_network_client_restore_services(qa_frontend *f, const frontend_network_client_domain *domain,
    frontend_network_restore_services_binding *binding, void *lease_context,
    bool (*entered)(void *, const frontend_network_client_domain *, qa_error *),
    qa_q3_host_client_services *out, qa_error *error)
{
    if(!binding || !out || !entered || !frontend_network_client_restore_domain_current(f,domain))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported DATA facade requires its real staged domain and role lease");
    *binding=(frontend_network_restore_services_binding){.frontend=f,.application=f->application,
        .network=f->network,.domain=*domain,.context=lease_context,.entered=entered};
    *out=(qa_q3_host_client_services){.context=binding,.gamestate=restore_service_gamestate,
        .current_snapshot=restore_service_current_snapshot,.snapshot=restore_service_snapshot,
        .server_command=restore_service_server_command,.current_command=restore_service_current_command,
        .user_command=restore_service_user_command,.command_values=restore_service_command_values,
        .source_actor=restore_service_source_actor};
    return true;
}

bool frontend_network_restore_prediction_pending(const qa_frontend *f)
{
    const qa_frontend_network *n=f?f->network:NULL;
    return n && n->frontend==f && n->detached_transport && n->q3_clients[0].q3_native_restore;
}
bool frontend_network_restore_prediction_read(const qa_frontend *f,
    frontend_network_prediction_source *out, bool *present, qa_error *error)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(!out || !present || !n || !n->detached_transport || !n->q3_clients[0].q3_native_restore)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported prediction requires its actual detached Network stage");
    *out=(frontend_network_prediction_source){0}; *present=false;
    if(!n->q3_clients[0].q3_client_active || !n->q3_clients[0].q3_client_gamestate) return true;
    frontend_network_client_domain domain; frontend_remote_q3_resources resources;
    if(!frontend_network_client_restore_domain_read(f,&domain,error)) return false;
    if(!qa_q3_prediction_scene_read(n->q3_clients[0].q3_prediction_scene,&out->scene)) return true;
    if(!n->q3_clients[0].q3_session || !frontend_remote_q3_resources_import_read(n->q3_clients[0].q3_session,&resources,error) ||
        !frontend_remote_q3_resources_import_current(&resources) || resources.domain.content_owner!=domain.content_owner ||
        resources.domain.source.descriptor->storage!=domain.source.descriptor->storage ||
        resources.map!=domain.map || !resources.geometry || !remote_player(f->application,&out->viewer,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported prediction lost its actual resource parent or viewer");
    out->connection=domain.connection; out->epoch=domain.epoch; out->restart_generation=domain.restart_generation;
    out->receiver=domain.source.receiver; out->geometry=resources.geometry;
    out->scratch=resources.trace_scratch; out->map=resources.map;
    out->previous_presentation_time=n->q3_clients[0].q3_previous_presentation_time;
    if(!frontend_network_client_restore_domain_current(f,&domain) ||
        !frontend_remote_q3_resources_import_current(&resources))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported prediction changed its real staged source and geometry");
    *present=true; return true;
}
bool frontend_network_restore_prediction_current(const qa_frontend *f,
    const frontend_network_prediction_source *source)
{
    frontend_network_prediction_source actual; bool present=false;
    return source && frontend_network_restore_prediction_read(f,&actual,&present,NULL) && present &&
        qa_net_client_id_equal(source->connection,actual.connection) && source->epoch==actual.epoch &&
        source->restart_generation==actual.restart_generation && source->map==actual.map &&
        source->geometry==actual.geometry && qa_actor_id_equal(source->viewer,actual.viewer) &&
        source->previous_presentation_time==actual.previous_presentation_time &&
        client_restore_receiver_same(&source->receiver,&actual.receiver) &&
        qa_q3_prediction_scene_current(f->network->q3_clients[0].q3_prediction_scene,&source->scene);
}
bool frontend_network_restore_prediction_acknowledgement(const qa_frontend *f,
    const frontend_network_prediction_source *source, bool *has_sequence, uint64_t *sequence,
    bool *history_unavailable, qa_error *error)
{
    return frontend_network_restore_prediction_current(f,source) && source->scene.prediction_snapshot &&
        qa_network_q3_client_acknowledged_command_time(f->network->runtime,source->connection,
            source->scene.prediction_snapshot->player.commandTime,has_sequence,sequence,history_unavailable,error) &&
        (frontend_network_restore_prediction_current(f,source) ||
         frontend_fail(error,QA_ERROR_ARGUMENT,"Imported acknowledgement lost its actual raw scene clock"));
}
bool frontend_network_restore_prediction_input_read(const qa_frontend *f,
    frontend_remote_input_source *out, bool *present, qa_error *error)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(!out || !present || !n || !n->detached_transport || !n->q3_clients[0].q3_native_restore)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported input requires its actual detached Network stage");
    *out=(frontend_remote_input_source){0}; *present=false;
    if(!n->q3_clients[0].q3_client_active || !n->q3_clients[0].q3_client_gamestate) return true;
    frontend_network_client_domain domain; frontend_remote_config_view configuration;
    const qa_q3_client_peer *peer=remote_view(f);
    uint64_t number=peer?qa_q3_client_peer_usercmd_number(peer):0;
    const qa_q3_usercmd *command=peer?qa_q3_client_peer_usercmd_at(peer,number):NULL;
    if(!command || !frontend_network_client_restore_domain_read(f,&domain,error) ||
        !client_configuration_view(f && f->network ? f->network->q3_clients : NULL,&configuration,error) || !configuration.q3_mouse || !configuration.q3_view ||
        configuration.cvars!=domain.source.receiver.cvars || configuration.console!=domain.source.receiver.console)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Imported input lost its actual retained raw ring and CLIENT settings");
    out->connection=domain.connection; out->epoch=domain.epoch; out->receiver=domain.source.receiver;
    out->input_settings=configuration.q3_mouse; out->movement_settings=configuration.q3_view;
    out->input_tuning=configuration.q3_input_tuning;
    out->media_owner=n->q3_clients[0].q3_client_content;
    out->frame=(qa_input_command_frame){.kind=QA_MOVEMENT_Q3,.sequence=number,
        .server_time_ms=n->q3_clients[0].q3_client_time,.weapon=command->weapon};
    if(!frontend_network_client_restore_domain_current(f,&domain)) return false;
    *present=true; return true;
}
bool frontend_network_restore_prediction_input_current(const qa_frontend *f,
    const frontend_remote_input_source *source)
{
    frontend_remote_input_source actual; bool present=false;
    if(!source || !frontend_network_restore_prediction_input_read(f,&actual,&present,NULL) || !present ||
        !qa_net_client_id_equal(source->connection,actual.connection) || source->epoch!=actual.epoch ||
        !client_restore_receiver_same(&source->receiver,&actual.receiver) ||
        source->input_settings!=actual.input_settings || source->movement_settings!=actual.movement_settings ||
        source->media_owner!=actual.media_owner) return false;
    const qa_input_command_frame *v=&source->frame;
    return v->kind==actual.frame.kind && v->sequence==actual.frame.sequence &&
        v->server_time_ms==actual.frame.server_time_ms && v->weapon==actual.frame.weapon &&
        v->acknowledged_server_seconds == 0 && !v->server_frame && !v->light_level && v->sensitivity == 0 &&
        !v->attack_allowed && !v->has_pitch_drift && !v->grounded && !v->drift_disabled &&
        v->ideal_pitch == 0 && v->delta_angles.x == 0 && v->delta_angles.y == 0 && v->delta_angles.z == 0 &&
        !source->has_initial_angles && source->initial_angles.x == 0 && source->initial_angles.y == 0 && source->initial_angles.z == 0;
}

bool frontend_network_menu_read(const qa_frontend *f, uint32_t physical,
    frontend_network_menu_view *out, qa_error *error)
{
    const qa_frontend_network *n=f?f->network:NULL; uint32_t authored=0;
    bool restoring=n && n->detached_transport && f->source_restoring &&
        (n->menu_connections_prefix.data || n->q3_clients[0].q3_connections_prefix.data);
    if(!out || !n || n->frontend!=f || (n->detached_transport && !restoring) || n->busy ||
        !n->runtime || !n->browser || !n->admin || !n->preferences ||
        !qa_network_callbacks_idle(n->runtime) || !f->seats || physical>=f->options.seats ||
        f->seats[physical].frontend!=f || f->seats[physical].id!=physical ||
        !f->seats[physical].input || !f->seats[physical].console)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Network menu lacks its published physical seat and retained services");
    *out=(frontend_network_menu_view){.network=n,.application=f->application,
        .browser=n->browser,.admin=n->admin,.downloads=n->downloads,.preferences=n->preferences,
        .input=f->seats[physical].input,.console=f->seats[physical].console,
        .configuration_generation=qa_application_configuration_generation(f->application),
        .physical_seat=physical,.restore_readonly=restoring};
    out->has_authored_seat=frontend_seat_launch_id_read(f,physical,&authored);
    if(out->has_authored_seat) out->authored_seat=authored;
    return true;
}
bool frontend_network_menu_current(const qa_frontend *f, const frontend_network_menu_view *view)
{
    frontend_network_menu_view actual;
    return view && frontend_network_menu_read(f,view->physical_seat,&actual,NULL) &&
        view->network==actual.network && view->application==actual.application &&
        view->browser==actual.browser && view->admin==actual.admin && view->downloads==actual.downloads &&
        view->preferences==actual.preferences && view->configuration_generation==actual.configuration_generation &&
        view->input==actual.input && view->console==actual.console &&
        view->has_authored_seat==actual.has_authored_seat && view->authored_seat==actual.authored_seat &&
        view->restore_readonly==actual.restore_readonly;
}
static bool menu_admitted(const qa_frontend *f, const frontend_network_menu_view *view, qa_error *error)
{
    return frontend_network_menu_current(f,view) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Network menu operation has a retired physical owner");
}
static bool menu_mutable(const qa_frontend *f,const frontend_network_menu_view *view,qa_error *error)
{
    return menu_admitted(f,view,error) && (!view->restore_readonly ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Restored menu metadata cannot execute Network operations"));
}
bool frontend_network_menu_status_read(const qa_frontend *f,const frontend_network_menu_view *view,
    qa_net_protocol_id protocol,frontend_network_menu_status *out,qa_error *error)
{
    if(!out || !menu_admitted(f,view,error) || !qa_net_protocol_valid(protocol,error))return false;
    int family=menu_family(protocol);
    if(family<0)return frontend_fail(error,QA_ERROR_ARGUMENT,"Unknown server browser protocol family");
    const qa_frontend_network *n=f->network; *out=n->browser_status;
    if(n->browser_expired[family]>out->receipt) {
        out->receipt=n->browser_expired[family];
        snprintf(out->text,sizeof(out->text),"No response from server");
    }
    return true;
}
static uint16_t menu_port(qa_net_protocol_id protocol)
{
    return protocol.kind<=QA_NET_RMQ999?26000:protocol.kind<=QA_NET_QW29?27500:
        protocol.kind==QA_NET_Q3_68?27960:27910;
}
bool frontend_network_menu_download_policy_read(const qa_frontend *f,const frontend_network_menu_view *view,
    frontend_network_menu_download_policy *out,bool *present,qa_error *error)
{
    if(!out || !present || !menu_admitted(f,view,error)) return false;
    *out=(frontend_network_menu_download_policy){0}; *present=false;
    const qa_frontend_network *n=f->network;
    if(n->q2_client_owner) {
        if(frontend_network_q2_client_retired(n->q2_client_owner)) return true;
        qa_application_client_source source; bool configured=false;
        if(!frontend_network_q2_client_configuration_read(n->q2_client_owner,&source,&configured,error)) return false;
        if(!configured || source.context.physical_seat!=view->physical_seat) return true;
        const qa_cvar_view *row=qa_cvars_find(source.context.cvars,"allow_download");
        if(!row) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 CLIENT lacks its registered automatic-transfer policy");
        *out=(frontend_network_menu_download_policy){.owner=n->q2_client_owner,.registry=source.context.cvars,
            .handle=row->handle,.modification=row->modification_count,.integer=row->integer,.number=row->number,
            .numeric_permission=true,.allowed=row->number>0};
        if(!qa_application_client_current(f->application,&source) || !menu_admitted(f,view,error)) return false;
        *present=true; return true;
    }
    if(!n->q3_clients[0].q3_client_requested) return true;
    frontend_remote_config_view configuration; bool configured=false;
    if(!frontend_network_client_configuration_read(f,n->q3_clients[0].q3_client_launch_seat,&configuration,&configured,error)) return false;
    if(!configured || configuration.physical_seat!=view->physical_seat) return true;
    const qa_cvar_view *row=qa_cvars_find(configuration.cvars,"cl_allowDownload");
    if(!row) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 CLIENT lacks its registered automatic-transfer policy");
    *out=(frontend_network_menu_download_policy){.owner=configuration.owner,.registry=configuration.cvars,
        .handle=row->handle,.modification=row->modification_count,.integer=row->integer,.number=row->number,
        .allowed=row->integer!=0};
    if(!frontend_remote_config_current(configuration.owner,&configuration) || !menu_admitted(f,view,error)) return false;
    *present=true; return true;
}
bool frontend_network_menu_download_policy_current(const qa_frontend *f,const frontend_network_menu_view *view,
    const frontend_network_menu_download_policy *policy)
{
    frontend_network_menu_download_policy actual; bool present=false;
    return policy && frontend_network_menu_download_policy_read(f,view,&actual,&present,NULL) && present &&
        policy->owner==actual.owner && policy->registry==actual.registry && policy->handle==actual.handle &&
        policy->modification==actual.modification && policy->integer==actual.integer && policy->number==actual.number &&
        policy->numeric_permission==actual.numeric_permission && policy->allowed==actual.allowed;
}
bool frontend_network_menu_download_policy_set(qa_frontend *f,const frontend_network_menu_view *view,
    const frontend_network_menu_download_policy *policy,bool allowed,qa_error *error)
{
    if(!menu_mutable(f,view,error) || !frontend_network_menu_download_policy_current(f,view,policy))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Automatic-transfer edit lost its actual CLIENT policy row");
    const char *name=policy->numeric_permission?"allow_download":"cl_allowDownload";
    if(!qa_cvars_set(policy->registry,name,allowed?"1":"0",false,error)) return false;
    frontend_network_menu_download_policy actual; bool present=false;
    return frontend_network_menu_download_policy_read(f,view,&actual,&present,error) && present &&
        actual.owner==policy->owner && actual.registry==policy->registry && actual.handle==policy->handle &&
        actual.allowed==allowed;
}
static bool menu_prefix(const char *text, const char *prefix)
{
    while(*prefix) {
        unsigned char value=(unsigned char)*text++;
        if(value>='A' && value<='Z') value=(unsigned char)(value+('a'-'A'));
        if(value!=(unsigned char)*prefix++) return false;
    }
    return true;
}
static bool menu_resolve(const char *text, uint16_t port, qa_net_address *out, qa_error *error)
{
    if(!menu_direct_text_valid(text))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid direct server address");
    return qa_net_address_resolve(text,port,4,out,error);
}
bool frontend_network_menu_rows(const qa_frontend *f, const frontend_network_menu_view *view,
    qa_net_protocol_id protocol, qa_server_entry *rows, size_t capacity, size_t *count, qa_error *error)
{
    if(!count || (capacity && !rows) || !menu_admitted(f,view,error) || !qa_net_protocol_valid(protocol,error))
        return false;
    size_t total=qa_server_browser_count(view->browser), listed=0;
    uint32_t *indices=total?malloc(total*sizeof(*indices)):NULL;
    if(total && !indices) return frontend_fail(error,QA_ERROR_MEMORY,"Reading retained startup browser rows");
    qa_browser_filter filter={.sort=QA_BROWSER_NAME};
    bool ok=qa_server_browser_list(view->browser,&filter,indices,total,&listed,error);
    size_t used=0;
    for(size_t i=0;ok && i<listed;++i) {
        qa_server_entry entry;
        ok=qa_server_browser_at(view->browser,indices[i],&entry);
        if(ok && entry.protocol.kind==protocol.kind && entry.protocol.revision==protocol.revision &&
            entry.protocol.flags==protocol.flags) {
            if(used<capacity) rows[used]=entry;
            ++used;
        }
    }
    free(indices);
    if(!ok) return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained startup browser row changed during observation");
    *count=used; return menu_admitted(f,view,error);
}
bool frontend_network_menu_query(qa_frontend *f, const frontend_network_menu_view *view,
    qa_net_protocol_id protocol, const char *remote, qa_error *error)
{
    qa_net_address address;
    if(!menu_mutable(f,view,error) || !qa_net_protocol_valid(protocol,error) ||
        !menu_resolve(remote,menu_port(protocol),&address,error) || !menu_admitted(f,view,error)) return false;
    if(!menu_preferences_direct(f->network,protocol,remote,&address,error) || !menu_admitted(f,view,error)) return false;
    return qa_server_browser_query(f->network->browser,&address,protocol,false,f->wall_time_ns,
        UINT64_C(5000000000),error) && menu_admitted(f,view,error);
}
bool frontend_network_menu_scan(qa_frontend *f, const frontend_network_menu_view *view,
    qa_net_protocol_id protocol, qa_error *error)
{
    if(!menu_mutable(f,view,error) || !qa_net_protocol_valid(protocol,error)) return false;
    if(protocol.kind==QA_NET_Q3_68)
        return frontend_q3_browser_scan(f->network->q3_browser,error) && menu_admitted(f,view,error);
    if(protocol.kind==QA_NET_Q2KEX_2023) {
        qa_frontend_network *n=f->network;
        if(!n->kex_browser) {
            frontend_kex_browser_hooks hooks={n,send_address};
            if(!frontend_kex_browser_open(n->browser,&hooks,&n->kex_browser,error)) return false;
        }
        return frontend_kex_browser_scan(n->kex_browser,error) && menu_admitted(f,view,error);
    }
    qa_net_address broadcast={.kind=QA_NET_IPV4,.port=menu_port(protocol),.host.ipv4={255,255,255,255}};
    return qa_server_browser_query(f->network->browser,&broadcast,protocol,true,f->wall_time_ns,
        UINT64_C(5000000000),error) && menu_admitted(f,view,error);
}
bool frontend_network_menu_master(qa_frontend *f, const frontend_network_menu_view *view,
    qa_net_protocol_id protocol, const char *remote, qa_error *error)
{
    if(!remote || !menu_mutable(f,view,error) || !qa_net_protocol_valid(protocol,error)) return false;
    if(!menu_preferences_master(f->network,protocol,remote,error) || !menu_admitted(f,view,error)) return false;
    bool ok;
    if(menu_prefix(remote,"http://") || menu_prefix(remote,"https://"))
        ok=qa_server_browser_master_http(f->network->browser,remote,protocol,f->wall_time_ns,error);
    else {
        const char *text=!strncmp(remote,"udp://",6)?remote+6:remote;
        qa_net_address address;
        if(!menu_resolve(text,protocol.kind==QA_NET_Q3_68?27950:27900,&address,error) ||
            !menu_admitted(f,view,error)) return false;
        ok=qa_server_browser_master_udp(f->network->browser,&address,protocol,f->wall_time_ns,
            UINT64_C(5000000000),error);
    }
    return ok && menu_admitted(f,view,error);
}
bool frontend_network_menu_favorite(qa_frontend *f, const frontend_network_menu_view *view,
    qa_net_protocol_id protocol, const char *remote, bool *added, qa_error *error)
{
    qa_net_address address; qa_buffer before={0}; qa_server_entry existing;
    if(!added || !menu_mutable(f,view,error) || !qa_net_protocol_valid(protocol,error) ||
        !menu_resolve(remote,menu_port(protocol),&address,error) || !menu_admitted(f,view,error)) return false;
    qa_frontend_network *n=f->network;
    bool removing=false; qa_net_protocol_id memberships[QA_NET_UNIFIED_1+1]; size_t membership_count=0;
    size_t count=qa_server_browser_count(n->browser);
    uint32_t *indices=count?malloc(count*sizeof(*indices)):NULL;
    if(count && !indices) return frontend_fail(error,QA_ERROR_MEMORY,"Reading actual favorite membership");
    qa_browser_filter filter={.sort=QA_BROWSER_NAME}; size_t listed=0;
    bool ok=qa_server_browser_list(n->browser,&filter,indices,count,&listed,error);
    for(size_t i=0;ok && i<listed;++i) {
        ok=qa_server_browser_at(n->browser,indices[i],&existing);
        if(ok && menu_family(existing.protocol)==menu_family(protocol) &&
            qa_net_address_equal(&existing.address,&address,true) && (existing.sources&QA_SERVER_FAVORITE)) {
            if(membership_count>=sizeof(memberships)/sizeof(*memberships)) { ok=false; break; }
            memberships[membership_count++]=existing.protocol; removing=true;
        }
    }
    free(indices);
    if(!ok || !qa_server_browser_save(n->browser,&before,error)) return false;
    if(removing) for(size_t i=0;ok && i<membership_count;++i)
        ok=qa_server_browser_remove_source(n->browser,&address,memberships[i],QA_SERVER_FAVORITE,error);
    else ok=qa_server_browser_add(n->browser,&address,protocol,QA_SERVER_FAVORITE,error);
    if(ok) ok=save_favorites(n,error);
    if(!ok) {
        qa_error failure=error?*error:(qa_error){0}, rollback={0};
        if(!qa_server_browser_restore_favorites(n->browser,(qa_bytes){before.data,before.size},&rollback))
            qa_error_set(error,failure.code?failure.code:rollback.code,0,"%s; favorite rollback: %s",
                failure.message,rollback.message);
        else if(error) *error=failure;
    }
    qa_buffer_free(&before);
    if(!ok) return false;
    *added=!removing; return menu_admitted(f,view,error);
}
bool frontend_network_menu_download_read(const qa_frontend *f, const frontend_network_menu_view *view,
    qa_download_id id, qa_download_view *out, qa_error *error)
{
    return menu_admitted(f,view,error) && qa_downloads_view(view->downloads,id,out) &&
        menu_admitted(f,view,error);
}
bool frontend_network_menu_connection_read(const qa_frontend *f, const frontend_network_menu_view *view,
    qa_net_protocol_id protocol, const char *remote, frontend_network_menu_connection *out, qa_error *error)
{
    if(!out || !menu_admitted(f,view,error) || !qa_net_protocol_valid(protocol,error)) return false;
    frontend_network_menu_connection actual={.protocol=protocol};
    if(!menu_resolve(remote,menu_port(protocol),&actual.endpoint,error) || !menu_admitted(f,view,error)) return false;
    memcpy(actual.remote,remote,strlen(remote)+1);
    const qa_frontend_network *n=f->network;
    if(protocol.kind!=QA_NET_Q3_68 || protocol.flags || protocol.revision)
        snprintf(actual.reason,sizeof(actual.reason),"This protocol's client admission adapter is not installed in the current session.");
    else if(!n->q3_clients[0].q3_client_requested)
        snprintf(actual.reason,sizeof(actual.reason),"The current session has no constructed Q3 remote CLIENT receiver.");
    else if(!view->has_authored_seat || view->physical_seat!=0 || view->authored_seat!=n->q3_clients[0].q3_client_launch_seat)
        snprintf(actual.reason,sizeof(actual.reason),"The selected physical seat does not own the constructed remote CLIENT.");
    else {
        frontend_remote_config_view configuration;
        if(!client_configuration_view(f && f->network ? f->network->q3_clients : NULL,&configuration,error)) return false;
        actual.available=configuration.ready && configuration.published &&
            configuration.scope.provider==n->q3_clients[0].q3_cgame_owner && configuration.scope.seat==view->authored_seat;
        if(!actual.available)
            snprintf(actual.reason,sizeof(actual.reason),"The remote CLIENT configuration has not completed publication.");
    }
    *out=actual; return menu_admitted(f,view,error);
}
bool frontend_network_menu_connect(qa_frontend *f, const frontend_network_menu_view *view,
    const frontend_network_menu_connection *connection, qa_error *error)
{
    frontend_network_menu_connection actual;
    if(!menu_mutable(f,view,error) || !connection ||
        !frontend_network_menu_connection_read(f,view,connection->protocol,connection->remote,&actual,error))
        return false;
    if(!actual.available || !connection->available ||
        !qa_net_address_equal(&actual.endpoint,&connection->endpoint,true))
        return frontend_fail(error,QA_ERROR_ARGUMENT,actual.reason[0]?actual.reason:"Connection endpoint changed during preparation");
    bool queued = menu_preferences_direct(f->network,actual.protocol,actual.remote,&actual.endpoint,error) &&
        menu_admitted(f,view,error) && client_attempt_enqueue(f->network,actual.remote,false,error) && menu_admitted(f,view,error);
    if (queued) frontend_demo_dispatch_manual_game(f->demos);
    return queued;
}
bool frontend_network_menu_download_begin(qa_frontend *f, const frontend_network_menu_view *view,
    const qa_download_request *request, const char *url, qa_download_id *id, qa_error *error)
{
    if(!menu_mutable(f,view,error) || !downloads_ready(f->network,error)) return false;
    frontend_network_menu_view current=*view;
    current.downloads=f->network->downloads;
    return menu_mutable(f,&current,error) && qa_downloads_begin(f->network->downloads,request,url,id,error) &&
        menu_admitted(f,&current,error);
}
bool frontend_network_menu_download_stop(qa_frontend *f, const frontend_network_menu_view *view,
    qa_download_id id, bool suspend, qa_error *error)
{
    qa_download_view actual;
    if(!menu_mutable(f,view,error) || !qa_downloads_view(f->network->downloads,id,&actual))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Download control lacks its actual retained job");
    if(suspend) qa_downloads_suspend(f->network->downloads,id);
    else qa_downloads_cancel(f->network->downloads,id);
    return menu_admitted(f,view,error);
}
bool frontend_network_menu_download_release(qa_frontend *f, const frontend_network_menu_view *view,
    qa_download_id id, qa_error *error)
{
    qa_download_view actual;
    if(!menu_mutable(f,view,error)) return false;
    if(!qa_downloads_view(f->network->downloads,id,&actual) || actual.publication_pending ||
        (actual.state!=QA_DOWNLOAD_COMPLETE && actual.state!=QA_DOWNLOAD_FAILED &&
         actual.state!=QA_DOWNLOAD_CANCELED))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Download removal requires its actual terminal job");
    qa_downloads_release(f->network->downloads,id);
    if(qa_downloads_view(f->network->downloads,id,&actual))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Download removal awaits its actual callback boundary");
    return menu_admitted(f,view,error);
}
bool frontend_network_initial_graph_read(const qa_frontend *f,
    frontend_network_initial_graph_view *out, qa_error *error)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(!f || !out || (!f->capture && !f->source_restoring) || f->resource_inventory)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial graph observation requires its actual capture or restore operation");
    *out=(frontend_network_initial_graph_view){.network=n};
    if(!n || (!n->q3_clients[0].q3_initial && !n->q3_clients[0].q3_initial_modules)) return true;
    out->parent=n->q3_clients[0].q3_initial; out->modules=n->q3_clients[0].q3_initial_modules; out->present=true;
    out->restore_candidate=n->detached_transport;
    if(n->frontend!=f || !n->runtime || n->busy || !qa_network_callbacks_idle(n->runtime) ||
        !n->q3_clients[0].q3_initial || frontend_remote_q3_initial_frontend(n->q3_clients[0].q3_initial)!=f || n->q3_clients[0].q3_session ||
        (n->detached_transport && !f->source_restoring))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial graph has a partial or mismatched Network parent");
    bool present=false;
    bool ok=n->detached_transport?
        frontend_network_client_restore_attempt_read(f,&out->attempt,&present,error):
        frontend_network_client_attempt_read(f,&n->q3_clients[0].q3_session_source.receiver,&out->attempt,&present,error);
    if(!ok) return false;
    frontend_network_client_attempt retained=out->attempt;
    retained.source=n->q3_clients[0].q3_session_source;
    if(!present || !n->q3_clients[0].q3_session_source.descriptor ||
        !client_attempt_owner_current(f,&retained,&out->attempt) ||
        (n->q3_clients[0].q3_initial_modules && frontend_remote_q3_modules_initial_parent(n->q3_clients[0].q3_initial_modules)!=n->q3_clients[0].q3_initial))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial graph differs from its actual retained attempt and module parent");
    return true;
}
bool frontend_network_initial_graph_current(const qa_frontend *f,
    const frontend_network_initial_graph_view *view)
{
    frontend_network_initial_graph_view actual;
    if(!view || !frontend_network_initial_graph_read(f,&actual,NULL) ||
        view->network!=actual.network || view->parent!=actual.parent || view->modules!=actual.modules ||
        view->present!=actual.present || view->restore_candidate!=actual.restore_candidate) return false;
    if(!actual.present) return true;
    return client_attempt_owner_current(f,&view->attempt,&actual.attempt) &&
        view->attempt.attached==actual.attempt.attached && view->attempt.phase==actual.attempt.phase &&
        qa_net_client_id_equal(view->attempt.connection,actual.attempt.connection);
}
bool frontend_network_menu_download_rows(const qa_frontend *f, const frontend_network_menu_view *view,
    qa_download_view *out, size_t capacity, size_t *count, qa_error *error)
{
    if(!count || (capacity && !out) || !menu_admitted(f,view,error)) return false;
    if(view->restore_readonly && !view->downloads)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Download jobs await their actual restored resource owner");
    if(!view->downloads) { *count=0; return true; }
    size_t total=qa_downloads_count(view->downloads);
    for(size_t i=0;i<total && i<capacity;++i)
        if(!qa_downloads_at(view->downloads,i,out+i))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained download inventory changed during its actual observation");
    *count=total;
    return menu_admitted(f,view,error);
}
bool frontend_network_menu_details_read(const qa_frontend *f, const frontend_network_menu_view *view,
    const qa_server_entry *entry, qa_server_browser_details *out, qa_error *error)
{
    if(!entry || !menu_admitted(f,view,error) ||
        !qa_server_browser_details_read(view->browser,&entry->address,entry->protocol,out,error)) return false;
    if(!menu_admitted(f,view,error) || !qa_server_browser_details_current(view->browser,out)) {
        qa_server_browser_details_free(out);
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Discovery detail receipt changed during its actual observation");
    }
    return true;
}
bool frontend_network_menu_details_current(const qa_frontend *f, const frontend_network_menu_view *view,
    const qa_server_browser_details *details)
{
    return frontend_network_menu_current(f,view) && qa_server_browser_details_current(view->browser,details);
}
bool frontend_network_menu_preferences_read(const qa_frontend *f,const frontend_network_menu_view *view,
    qa_net_protocol_id protocol,frontend_network_menu_preferences *out,qa_error *error)
{
    int family=menu_family(protocol);
    if(!out || family<0 || !qa_net_protocol_valid(protocol,error) || !menu_admitted(f,view,error)) return false;
    *out=f->network->menu_preferences[family];
    return menu_admitted(f,view,error);
}
bool frontend_network_menu_direct_read(const qa_frontend *f,const frontend_network_menu_view *view,
    const qa_server_entry *entry,char remote[256],bool *present,qa_error *error)
{
    if(!entry || !remote || !present || !menu_admitted(f,view,error)) return false;
    *present=false; remote[0]=0;
    frontend_network_menu_preferences preferences;
    if(!frontend_network_menu_preferences_read(f,view,entry->protocol,&preferences,error)) return false;
    if(entry->sources&QA_SERVER_DIRECT) for(uint32_t i=0;i<preferences.direct_count;++i)
        if(qa_net_address_equal(&entry->address,&preferences.direct[i].address,true)) {
            memcpy(remote,preferences.direct[i].remote,256); *present=true; break;
        }
    return menu_admitted(f,view,error);
}

bool frontend_network_client_restore_abort_ready(const qa_frontend *f,
    const qa_application_client_source *source,qa_error *error)
{
    if(!f || !source)return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT import abort requires its actual Source");
    if(f->network && f->network->q2_client_owner &&
        frontend_network_q2_client_configuration_primary(f->network->q2_client_owner,source))
        return frontend_network_q2_client_restore_abort_ready(f->network->q2_client_owner,source,error);
    return frontend_client_sources_restore_abort_ready(f,source,error);
}

static bool demo_local_format(const qa_frontend *f,frontend_demo_format *out,qa_error *error)
{
    qa_application_startup_source source;bool present=false;
    if(!f||!f->config_store||!out||
        !frontend_config_store_primary_server_read(f->config_store,&source,&present,error))return false;
    if(!present||!source.descriptor)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local recording requires its actual primary Source configuration");
    switch(source.descriptor->selection.clock.kind) {
    case QA_CLOCK_NETQUAKE:*out=FRONTEND_DEMO_NQ;return true;
    case QA_CLOCK_QUAKEWORLD:*out=FRONTEND_DEMO_QW;return true;
    case QA_CLOCK_Q2_CLASSIC:case QA_CLOCK_Q2_RERELEASE:*out=FRONTEND_DEMO_Q2;return true;
    case QA_CLOCK_Q3:*out=FRONTEND_DEMO_Q3;return true;
    }
    return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Selected local Source has no native demo format");
}
frontend_demo_format frontend_network_demo_format(const qa_frontend *f,const qa_command_context *source)
{
    const qa_frontend_network *n=f?f->network:NULL;
    if(n&&n->demo_playback) return n->demo_format;
    if(n&&n->q1_client_owner) {
        frontend_network_q1_client_view view;qa_error error={0};
        if(frontend_network_q1_client_metadata_read(n->q1_client_owner,&view,&error))
            return qa_q1_is_qw(view.protocol)?FRONTEND_DEMO_QW:FRONTEND_DEMO_NQ;
    }
    if(n&&(n->q2_client_owner||n->q2_host)) return FRONTEND_DEMO_Q2;
    if(n&&n->q3_clients[0].q3_client_attached) return FRONTEND_DEMO_Q3;
    frontend_demo_format format;
    if(demo_local_format(f,&format,NULL))return format;
    if(source&&source->dialect==QA_CONSOLE_Q3) return FRONTEND_DEMO_Q3;
    if(source&&(source->dialect==QA_CONSOLE_Q2||source->dialect==QA_CONSOLE_Q2_RERELEASE)) return FRONTEND_DEMO_Q2;
    if(source&&source->dialect==QA_CONSOLE_QW) return FRONTEND_DEMO_QW;
    if(source&&source->dialect==QA_CONSOLE_Q1) return FRONTEND_DEMO_NQ;
    return f&&qa_q1_is_qw(f->options.network_protocol)?FRONTEND_DEMO_QW:FRONTEND_DEMO_NQ;
}
static bool demo_network_current(const qa_frontend_network *n)
{
    return n&&n->frontend&&n->frontend->network==n&&!n->detached_transport;
}
static bool demo_q1_record_current(const void *context)
{
    const qa_frontend_network *n=context;
    return demo_network_current(n)&&n->demo_q1_owner&&n->q1_client_owner==n->demo_q1_owner&&
        !frontend_network_q1_client_retired(n->demo_q1_owner);
}
static bool demo_q1_seed(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    qa_frontend_network *n=context;
    if(!demo_q1_record_current(n))return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 recording lost its active Source receiver");
    return n->demo_q1_source.seed(n->demo_q1_source.owner,sink,error);
}
static bool demo_q1_attach(void *context,const frontend_demo_sink *sink,bool *attached,qa_error *error)
{
    qa_frontend_network *n=context;*attached=false;
    bool ok=demo_q1_record_current(n)&&frontend_network_q1_client_demo_follow(n->demo_q1_owner,sink,attached,error);
    if(*attached)n->demo_q1_sink=*sink;
    return ok;
}
static bool demo_q1_detach(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    qa_frontend_network *n=context;
    if(!demo_network_current(n)||!n->demo_q1_owner||n->q1_client_owner!=n->demo_q1_owner)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Recording lost its retained Q1 receiver");
    if(!frontend_network_q1_client_demo_unfollow(n->demo_q1_owner,sink,error)) return false;
    n->demo_q1_sink=(frontend_demo_sink){0};return true;
}
static bool demo_q1_reconnect(void *context,const qa_command_context *source,const frontend_demo_sink *sink,
    bool *attached,qa_error *error)
{
    qa_frontend_network *n=context;frontend_network_q1_client_view previous;uint32_t physical;
    *attached=false;
    if(!demo_q1_record_current(n)||!frontend_command_seat_read(n->frontend,source,&physical)||
        !frontend_network_q1_client_metadata_read(n->demo_q1_owner,&previous,error)||
        physical!=previous.physical_seat||!qa_q1_is_qw(previous.protocol))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Rerecord requires its actual QuakeWorld connection");
    if(!frontend_network_q1_client_disconnect(n->demo_q1_owner,"rerecord",error))return false;
    if(!n->demo_q1_source.release(&n->demo_q1_source.owner,error))return false;
    if(!frontend_network_q1_client_destroy(&n->q1_client_owner,error))return false;
    n->demo_q1_owner=NULL;
    if(!q1_client_construct(n,&previous.remote,previous.protocol,physical,false,error)) {
        n->demo_q1_owner=n->q1_client_owner;return false;
    }
    n->demo_q1_owner=n->q1_client_owner;
    bool ok=frontend_network_q1_client_demo_follow(n->demo_q1_owner,sink,attached,error);
    if(*attached)n->demo_q1_sink=*sink;
    return ok;
}
static bool demo_q1_record_release(void **owner,qa_error *error)
{
    qa_frontend_network *n=owner?*owner:NULL;if(!n)return true;
    if(n->demo_q1_sink.append)return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 recording still owns its receiver sink");
    if(n->demo_q1_source.owner&&!n->demo_q1_source.release(&n->demo_q1_source.owner,error))return false;
    n->demo_q1_source=(frontend_demo_record_source){0};n->demo_q1_owner=NULL;*owner=NULL;return true;
}
static bool demo_q3_record_current(const void *context)
{
    const qa_frontend_network *n=context;
    return demo_network_current(n)&&!n->demo_playback&&!n->q3_clients[0].q3_client_retiring&&n->q3_clients[0].q3_client_attached&&
        qa_net_client_id_equal(n->demo_q3_record_client,n->q3_clients[0].q3_client)&&
        qa_network_q3_client_live(n->runtime,n->demo_q3_record_client);
}
static bool demo_q3_seed(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    qa_frontend_network *n=context;uint8_t bytes[65536];qa_q3_writer writer;int32_t sequence;
    if(!demo_q3_record_current(n))return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 recording lost its actual CLIENT");
    qa_q3_writer_init(&writer,bytes,sizeof(bytes),false,error);
    if(!qa_network_q3_client_record_seed(n->runtime,n->demo_q3_record_client,&writer,&sequence,error))return false;
    frontend_demo_packet packet={.format=FRONTEND_DEMO_Q3,.value.q3={sequence,{bytes,qa_q3_writer_size(&writer)}}};
    return sink->append(sink->owner,&packet,error);
}
static bool demo_q3_attach(void *context,const frontend_demo_sink *sink,bool *attached,qa_error *error)
{
    qa_frontend_network *n=context;*attached=false;
    if(!demo_q3_record_current(n)||n->demo_q3_sink.append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 recording requires its unattached actual packet receiver");
    n->demo_q3_sink=*sink;*attached=true;return true;
}
static bool demo_q3_detach(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    qa_frontend_network *n=context;
    if(!demo_network_current(n)||n->demo_q3_sink.owner!=sink->owner||n->demo_q3_sink.append!=sink->append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 recording sink differs from its actual receiver");
    n->demo_q3_sink=(frontend_demo_sink){0};return true;
}
static bool demo_q3_record_release(void **owner,qa_error *error)
{
    qa_frontend_network *n=owner?*owner:NULL;if(!n)return true;
    if(n->demo_q3_sink.append)return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 recording is still attached");
    n->demo_q3_record_client=(qa_net_client_id){0};*owner=NULL;return true;
}
static bool demo_q3_accepted(void *context,int32_t sequence,qa_bytes bytes,bool ready,qa_error *error)
{
    frontend_q3_client *n=context;(void)error;
    if(ready&&n->network->demo_q3_sink.append) {
        frontend_demo_packet packet={.format=FRONTEND_DEMO_Q3,.value.q3={sequence,bytes}};
        qa_error write_error={0};
        (void)n->network->demo_q3_sink.append(n->network->demo_q3_sink.owner,&packet,&write_error);
    }
    return true;
}
/* Local recording borrows the genuine PLAYER/CLIENT wire publication. File
 * custody and codec framing remain in the one common demo service. */
typedef struct local_q3_demo_source {
    qa_frontend *frontend;
    qa_actor_id actor;
    qa_actor_owner provider;
    uint32_t seat,slot;
    uint64_t map_revision,last_frame;
    qa_q3_gamestate *state;
    int32_t sequence,commands;
    frontend_demo_sink sink;
    bool published;
} local_q3_demo_source;
static bool local_q3_demo_current(const void *context)
{
    const local_q3_demo_source *source=context;qa_actor_id actor;uint32_t slot;qa_q3_product product;
    int32_t sequence;
    return source&&source->frontend&&source->frontend->application&&
        qa_application_player_actor(source->frontend->application,source->seat,&actor)&&qa_actor_id_equal(actor,source->actor)&&
        qa_application_network_q3_client_bound(source->frontend->application,source->provider,actor,source->slot)&&
        qa_application_network_q3_source(source->frontend->application,actor,&slot,&product,NULL)&&slot==source->slot&&
        qa_application_network_q3_record_read(source->frontend->application,actor,NULL,&sequence,NULL);
}
static bool local_q3_demo_seed(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    local_q3_demo_source *source=context;qa_application_map_view map;
    if(!local_q3_demo_current(source)||!qa_application_map_read(source->frontend->application,&map)||
        !qa_application_network_q3_record_read(source->frontend->application,
        source->actor,source->state,&source->commands,error))return false;
    if(source->sequence==INT32_MAX)return frontend_fail(error,QA_ERROR_FORMAT,"Local Q3 demo message ordinal exhausted");
    uint8_t bytes[65536];qa_q3_writer writer;qa_q3_writer_init(&writer,bytes,sizeof(bytes),false,error);
    if(!qa_q3_server_begin(&writer,0)||!qa_q3_server_gamestate(&writer,source->state)||!qa_q3_server_end(&writer))return false;
    frontend_demo_packet packet={.format=FRONTEND_DEMO_Q3,.value.q3={source->sequence,{bytes,qa_q3_writer_size(&writer)}}};
    qa_error write_error={0};
    if(!sink->append(sink->owner,&packet,&write_error)&&!source->sink.append) {
        if(error)*error=write_error;
        return false;
    }
    ++source->sequence;source->map_revision=map.revision;source->published=false;return true;
}
static bool local_q3_demo_attach(void *context,const frontend_demo_sink *sink,bool *attached,qa_error *error)
{
    local_q3_demo_source *source=context;*attached=false;
    if(!local_q3_demo_current(source)||source->sink.append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local Q3 recording requires its unattached Source publication");
    source->sink=*sink;*attached=true;return true;
}
static bool local_q3_demo_detach(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    local_q3_demo_source *source=context;
    if(source->sink.owner!=sink->owner||source->sink.append!=sink->append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local Q3 recording lost its actual sink");
    source->sink=(frontend_demo_sink){0};return true;
}
static bool local_q3_demo_publish(void *context,qa_error *error)
{
    local_q3_demo_source *source=context;qa_application *app=source->frontend->application;qa_clock_state clock;
    qa_application_map_view map;
    if(!local_q3_demo_current(source)||!source->sink.append||
        !qa_application_map_read(app,&map)||
        !qa_session_clock(qa_application_session(app),source->provider,&clock)||
        clock.frame.provider!=source->provider||clock.frame.kind!=QA_CLOCK_Q3||clock.frame.phase!=QA_FRAME_EXIT)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local Q3 recording requires its completed Source frame");
    if(source->map_revision!=map.revision)return local_q3_demo_seed(source,&source->sink,error);
    if(source->sequence==INT32_MAX)return frontend_fail(error,QA_ERROR_FORMAT,"Local Q3 demo message ordinal exhausted");
    int32_t commands;
    if(!qa_application_network_q3_record_read(app,source->actor,NULL,&commands,error))return false;
    if(source->published&&source->last_frame==clock.frame.number&&commands==source->commands)return true;
    if(commands<source->commands)return frontend_fail(error,QA_ERROR_FORMAT,"Local Q3 demo lost its retained reliable generation");
    uint8_t bytes[65536];qa_q3_writer writer;qa_q3_writer_init(&writer,bytes,sizeof(bytes),false,error);
    if(!qa_q3_server_begin(&writer,0))return false;
    for(int64_t i=(int64_t)source->commands+1;i<=commands;++i) {
        const char *text;
        if(!qa_application_network_q3_record_command(app,source->actor,(int32_t)i,&text,error)||
            !qa_q3_server_command(&writer,(int32_t)i,text))return false;
    }
    qa_application_network_q3_frame frame;
    if(!qa_application_network_q3_snapshot(app,source->actor,source->sequence,commands,0,&frame,error)||
        !qa_q3_server_snapshot(&writer,NULL,&frame.snapshot,source->state)||!qa_q3_server_end(&writer))return false;
    frontend_demo_packet packet={.format=FRONTEND_DEMO_Q3,.value.q3={source->sequence,{bytes,qa_q3_writer_size(&writer)}}};
    qa_error write_error={0};(void)source->sink.append(source->sink.owner,&packet,&write_error);
    ++source->sequence;source->commands=commands;source->last_frame=clock.frame.number;source->published=true;
    return true;
}
static bool local_q3_demo_release(void **owner,qa_error *error)
{
    local_q3_demo_source *source=owner?*owner:NULL;if(!source)return true;
    if(source->sink.append)return frontend_fail(error,QA_ERROR_ARGUMENT,"Local Q3 recording still owns its Source sink");
    free(source->state);free(source);*owner=NULL;return true;
}
static bool local_q3_demo_record(qa_frontend *f,qa_actor_id actor,qa_fs_root *root,
    frontend_demo_record_source *out,qa_error *error)
{
    local_q3_demo_source *source=calloc(1,sizeof(*source));
    if(!source)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining local Q3 demo Source");
    source->state=malloc(sizeof(*source->state));
    if(!source->state){free(source);return frontend_fail(error,QA_ERROR_MEMORY,"Retaining local Q3 demo baselines");}
    source->frontend=f;source->actor=actor;
    qa_q3_product product;int32_t sequence;
    if(!qa_application_player_seat(f->application,actor,&source->seat)||
        !qa_application_network_q3_owner(f->application,&source->provider,&product,error)||
        !qa_application_network_q3_source(f->application,actor,&source->slot,&product,error)||
        !qa_application_network_q3_record_read(f->application,actor,NULL,&sequence,error)) {
        free(source->state);free(source);return false;
    }
    *out=(frontend_demo_record_source){.owner=source,.format=FRONTEND_DEMO_Q3,.protocol={QA_NET_Q3_68,0,0},
        .root=root,.current=local_q3_demo_current,.seed=local_q3_demo_seed,.attach=local_q3_demo_attach,
        .detach=local_q3_demo_detach,.publish=local_q3_demo_publish,.release=local_q3_demo_release};return true;
}
bool frontend_network_demo_record(qa_frontend *f,const qa_command_context *source,frontend_demo_action action,
    frontend_demo_record_source *out,qa_error *error)
{
    uint32_t physical;
    if(!f||!out||!frontend_command_seat_read(f,source,&physical))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Recording requires its actual physical Source seat");
    if(!frontend_network_create(f,error))return false;
    qa_frontend_network *n=f->network;
    if(n->busy||n->detached_transport||!qa_network_callbacks_idle(n->runtime)||n->demo_playback)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Recording requires returned live Source receivers");
    if(action==FRONTEND_DEMO_SERVER_RECORD||action==FRONTEND_DEMO_MVD_RECORD)
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Native server/MVD recording has no installed Source feed");
    if(n->q1_client_owner) {
        if(n->demo_q1_owner)return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 recorder already retains its Source");
        frontend_network_q1_client_view view;
        if(!frontend_network_q1_client_metadata_read(n->q1_client_owner,&view,error))return false;
        if(physical!=view.physical_seat||!view.configured||view.retired)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Recording requires its active actual Q1 CLIENT seat");
        if(!frontend_network_q1_client_demo_record(n->q1_client_owner,&n->demo_q1_source,error)) {
            if(error&&error->code==QA_OK)frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 recording Source is not ready");
            return false;
        }
        n->demo_q1_owner=n->q1_client_owner;*out=n->demo_q1_source;
        out->owner=n;out->current=demo_q1_record_current;out->seed=demo_q1_seed;
        out->attach=demo_q1_attach;out->detach=demo_q1_detach;out->reconnect=demo_q1_reconnect;out->release=demo_q1_record_release;
        return true;
    }
    if(action==FRONTEND_DEMO_RERECORD)return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Rerecord requires QuakeWorld");
    if(n->q2_client_owner)return frontend_network_q2_client_demo_record(n->q2_client_owner,out,error);
    if(n->q2_host) {
        qa_actor_id actor;qa_fs_root *root=NULL;
        if(!frontend_seat_actor_read(f,physical,&actor))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 recording requires its actual controlled player");
        if(!frontend_content_library_demo_root(f,source,&root,error))return false;
        return frontend_network_q2_host_demo_record(n->q2_host,actor,root,out,error);
    }
    if(n->q3_clients[0].q3_client_attached) {
        qa_fs_root *root=NULL;
        if(n->demo_q3_record_client.generation)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 recording already retains its actual CLIENT");
        if(!frontend_content_library_demo_root(f,source,&root,error))return false;
        n->demo_q3_record_client=n->q3_clients[0].q3_client;
        *out=(frontend_demo_record_source){.owner=n,.format=FRONTEND_DEMO_Q3,.protocol={QA_NET_Q3_68,0,0},
            .root=root,.current=demo_q3_record_current,.seed=demo_q3_seed,.attach=demo_q3_attach,
            .detach=demo_q3_detach,.release=demo_q3_record_release};return true;
    }
    qa_actor_id actor;qa_fs_root *root=NULL;
    if(!frontend_seat_actor_read(f,physical,&actor))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local recording requires its actual controlled player");
    if(!frontend_content_library_demo_root(f,source,&root,error))return false;
    frontend_demo_format format;
    if(!demo_local_format(f,&format,error))return false;
    if(format==FRONTEND_DEMO_Q3)return local_q3_demo_record(f,actor,root,out,error);
    if(format==FRONTEND_DEMO_NQ)return frontend_nq_demo_record(f,n->runtime,actor,root,out,error);
    return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Selected local Source has no native recording feed");
}
static bool demo_playback_current(const void *context)
{
    const qa_frontend_network *n=context;
    return demo_network_current(n)&&n->demo_playback;
}
static bool demo_playback_release(void **owner,qa_error *error)
{
    qa_frontend_network *n=owner?*owner:NULL;if(!n)return true;
    if(n->demo_source.owner&&!n->demo_source.release(&n->demo_source.owner,error))return false;
    if(n->demo_format==FRONTEND_DEMO_Q3&&n->q3_clients[0].q3_client_requested) {
        if(n->q3_clients[0].q3_client_attached) {
            if(!client_close_attempt(&n->q3_clients[0],error))return false;
            n->q3_clients[0].q3_client_attached=false;
        } else if(n->q3_clients[0].q3_client_rebind) {
            /* Attachment failure leaves the previous cleared Source at its real
             * prior epoch; no new receiver was published to clear. */
            n->q3_clients[0].q3_client_epoch=n->q3_clients[0].q3_client_previous_epoch;
            n->q3_clients[0].q3_client_rebind=false;n->q3_clients[0].q3_client_closed=true;n->q3_clients[0].q3_client_retiring=true;
        }
    }
    if(!frontend_network_q1_client_destroy(&n->q1_client_owner,error)||
        !frontend_network_q2_client_destroy(&n->q2_client_owner,error))return false;
    n->demo_source=(frontend_demo_playback_source){0};n->demo_playback=false;*owner=NULL;return true;
}
static bool demo_q3_sequence(void *context,int32_t sequence,qa_error *error)
{
    qa_frontend_network *n=context;
    return qa_network_q3_client_demo_sequence(n->runtime,n->q3_clients[0].q3_client,sequence,error);
}
static bool demo_q3_read(qa_frontend_network *n,frontend_demo_reader *reader,frontend_demo_end *end,qa_error *error)
{
    frontend_demo_packet packet;bool present;
    if(!frontend_demo_read_next(reader,demo_q3_sequence,n,&packet,&present,end,error))return false;
    if(!present)return true;
    if(!qa_network_q3_client_demo_message(n->runtime,n->q3_clients[0].q3_client,packet.value.q3.sequence,
        packet.value.q3.message,(int32_t)((n->frontend->time_ns/UINT64_C(1000000))&INT32_MAX),error)||
        !client_drain(&n->q3_clients[0],false,error))return false;
    if(n->q3_clients[0].q3_client_retiring)*end=FRONTEND_DEMO_DISCONNECTED;
    return true;
}
static bool demo_playback_advance(void *context,frontend_demo_reader *reader,uint64_t elapsed,
    uint64_t frame,bool timedemo,frontend_demo_end *end,qa_error *error)
{
    qa_frontend_network *n=context;
    if(n->demo_source.owner)return n->demo_source.advance(n->demo_source.owner,reader,elapsed,frame,timedemo,end,error);
    *end=FRONTEND_DEMO_RUNNING;
    if(!demo_playback_current(n)||n->demo_format!=FRONTEND_DEMO_Q3)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo playback lost its actual receiver");
    while(!n->q3_clients[0].q3_client_clock.has_snapshot&&*end==FRONTEND_DEMO_RUNNING)
        if(!demo_q3_read(n,reader,end,error))return false;
    if(*end!=FRONTEND_DEMO_RUNNING)return true;
    if(n->demo_first_frame){n->demo_first_frame=false;return true;}
    qa_application_q3_client_context role;
    if(!qa_application_q3_remote_context_read(n->frontend->application,n->q3_clients[0].q3_cgame_owner,n->q3_clients[0].q3_client_launch_seat,&role,error))return false;
    qa_q3_clock_options options={.demo=true,.timedemo=timedemo,.timescale=1};
    const qa_cvar_view *scale=qa_cvars_read(role.cvars,n->frontend->engine_cvars.timescale),
        *nudge=qa_cvars_read(role.cvars,n->q3_clients[0].cl_timeNudge);
    if(scale)options.timescale=scale->number;
    if(nudge)options.time_nudge=nudge->integer;
    if(!qa_q3_clock_advance(&n->q3_clients[0].q3_client_clock,(int32_t)((n->frontend->time_ns/UINT64_C(1000000))&INT32_MAX),
        &options,&n->q3_clients[0].q3_client_active,&n->q3_clients[0].q3_client_time,error))return false;
    while(qa_q3_clock_needs_demo_message(&n->q3_clients[0].q3_client_clock)&&*end==FRONTEND_DEMO_RUNNING)
        if(!demo_q3_read(n,reader,end,error))return false;
    if(*end!=FRONTEND_DEMO_RUNNING)return true;
    const qa_net_client *client=qa_net_connections_get(qa_network_connections(n->runtime),n->q3_clients[0].q3_client);
    if(n->q3_clients[0].q3_client_active&&client&&client->phase==QA_NET_PRIMED&&
        !qa_network_phase(n->runtime,n->q3_clients[0].q3_client,QA_NET_ACTIVE,error))return false;
    return client_project(&n->q3_clients[0],error);
}
bool frontend_network_demo_playback(qa_frontend *f,const qa_command_context *source,frontend_demo_format format,
    uint32_t protocol,frontend_demo_reader *reader,frontend_demo_playback_source *out,qa_error *error)
{
    uint32_t physical;
    if(!f||!reader||!out||!frontend_command_seat_read(f,source,&physical))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Playback requires its actual physical Source seat");
    if(!frontend_network_create(f,error))return false;
    qa_frontend_network *n=f->network;
    if(n->busy||n->demo_playback||n->detached_transport||!qa_network_callbacks_idle(n->runtime)||f->options.dedicated)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Demo playback requires its returned presentation CLIENT");
    if(format==FRONTEND_DEMO_MVD||format==FRONTEND_DEMO_Q2_SERVER)
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Selected demo format has no native CLIENT receiver");
    if(format==FRONTEND_DEMO_Q3&&(protocol!=68||!n->q3_clients[0].q3_client_requested||!n->q3_clients[0].q3_cgame_owner))
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Q3 demo playback requires protocol 68 and an installed real Q3 CLIENT");
    if(n->demo_q1_owner||n->demo_q3_sink.append)return frontend_fail(error,QA_ERROR_ARGUMENT,"Recording sink must detach before demo playback");
    if(format!=FRONTEND_DEMO_Q3&&!frontend_network_local_groups_retire(f,error))return false;
    if(n->q1_client_owner&&!frontend_network_q1_client_disconnect(n->q1_client_owner,"demo playback",error))return false;
    if(!frontend_network_q1_client_destroy(&n->q1_client_owner,error)||
        !frontend_network_q2_client_destroy(&n->q2_client_owner,error))return false;
    if(n->q3_clients[0].q3_client_requested&&!n->q3_clients[0].q3_client_closed&&!client_close_attempt(&n->q3_clients[0],error))return false;
    if(format==FRONTEND_DEMO_Q3&&n->q3_clients[0].q3_client_epoch==UINT64_MAX)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 demo connection epoch is exhausted");
    if(n->q3_clients[0].q3_client_requested)n->q3_clients[0].q3_client_attached=false;
    n->demo_format=format;n->demo_playback=true;n->demo_first_frame=true;
    *out=(frontend_demo_playback_source){n,demo_playback_current,demo_playback_advance,demo_playback_release};
    qa_net_address local={.kind=QA_NET_LOOPBACK,.host.loopback="demo"};
    if(format==FRONTEND_DEMO_NQ||format==FRONTEND_DEMO_QW) {
        qa_net_protocol_id wire={format==FRONTEND_DEMO_QW?QA_NET_QW28:QA_NET_NQ15,0,0};
        return q1_client_construct(n,&local,wire,physical,true,error)&&
            frontend_network_q1_client_demo_playback(n->q1_client_owner,reader,&n->demo_source,error);
    }
    if(format==FRONTEND_DEMO_Q2) {
        qa_net_protocol_id wire;
        if(!frontend_network_q2_demo_protocol(reader,&wire,error))return false;
        frontend_network_q2_client_options options={.frontend=f,.runtime=n->runtime,.remote=local,.protocol=wire,
            .physical_seat=physical,.demo=true,.context=n,.current=q2_client_current,
            .download_stage=q2_download_stage,.restore_stage=q2_restore_stage};
        return frontend_network_q2_client_create(&options,&n->q2_client_owner,error)&&
            frontend_network_q2_client_demo_playback(n->q2_client_owner,&n->demo_source,error);
    }
    n->q3_clients[0].q3_client_previous_epoch=n->q3_clients[0].q3_client_epoch;n->q3_clients[0].q3_client_previous=n->q3_clients[0].q3_client;
    ++n->q3_clients[0].q3_client_epoch;n->q3_clients[0].q3_client=(qa_net_client_id){0};n->q3_clients[0].q3_client_rebind=true;
    n->q3_clients[0].q3_client_closed=false;n->q3_clients[0].q3_client_retiring=false;n->q3_clients[0].q3_client_attached=false;
    n->q3_clients[0].q3_client_reason[0]=0;n->q3_clients[0].q3_client_userinfo[0]=0;
    qa_q3_client_admission_begin(&n->q3_clients[0].q3_client_admission,&local,(uint16_t)n->rotation_random);
    qa_net_seat_binding seat={{NETWORK_OWNER,0},0};
    qa_net_connect request={.attachment=QA_NET_LOCAL_SEAT,.endpoint=local,.protocol={QA_NET_Q3_68,0,0},
        .seats=&seat,.seat_count=1,.composition=n->composition};
    qa_q3_client_hooks hooks={.context=n->q3_clients,.generation=client_generation,.clear_active=client_clear,
        .gamestate=client_gamestate,.system_info=client_system_info,.snapshot=client_snapshot,
        .download_size=client_download_size,.download=client_download,.command=client_source_command,
        .map_restart=client_map_restart,.disconnect=client_disconnect,.level_shot=client_level_shot,
        .local_server_running=client_local_server,.accepted_message=demo_q3_accepted,.defer_source=true};
    qa_network_q3_client_policy policy={n->q3_clients,remote_client_settings};
    return qa_network_attach_q3_demo(n->runtime,&request,n->q3_clients[0].q3_client_product,&hooks,&policy,
        f->wall_time_ns,&n->q3_clients[0].q3_client,error)&&(n->q3_clients[0].q3_client_attached=true)&&client_bind_attempt(&n->q3_clients[0],error);
}
