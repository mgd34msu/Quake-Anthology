#ifndef QA_APPLICATION_NETWORK_UNIFIED_PRIVATE_H
#define QA_APPLICATION_NETWORK_UNIFIED_PRIVATE_H
#include "network_unified.h"
#include "unified_output_capture.h"
#include "../../network/event_receipts.h"

struct application_player_record;
const struct application_player_record *application_players_connection_read(
    const qa_application *, qa_net_client_id, qa_net_seat_id);
bool application_players_connection_disconnect(qa_application *, qa_net_client_id,
    qa_net_seat_id, qa_error *);
bool application_unified_player_bind_local(qa_application *, const qa_net_client *,
    qa_net_seat_id, uint32_t application_seat, qa_unified_session_player *, qa_error *);

typedef struct retained_input {
    qa_unified_input value;
    qa_buffer provider, weapon;
    size_t provider_capacity, weapon_capacity;
} retained_input;
struct application_unified_inputs {
    qa_application *application;
    qa_network_runtime *runtime;
    qa_net_client_id client;
    qa_net_seat_id seat;
    qa_actor_id actor;
    qa_actor_owner source;
    uint32_t epoch, source_slot;
    uint64_t publication, map_revision, runtime_epoch;
    retained_input *commands;
    size_t count, cursor, capacity;
    int64_t queued, submitted;
    bool advancing;
};
struct application_unified_server {
    qa_application *application;
    qa_network_runtime *runtime;
    qa_net_seat_id seat;
    uint32_t application_seat, epoch;
    qa_net_client_id client;
    qa_unified_session *session;
    application_unified_inputs *inputs;
    application_unified_component_publisher *components;
    application_unified_output_capture *pending_capture;
    qa_unified_frame_pool *recipient_pool;
    qa_application_visual_visibility *visibility;
    application_unified_metadata_receipt committed_metadata;
    qa_unified_document *committed_source_metadata;
    application_unified_source offered;
    qa_unified_session_player admitted_player;
    qa_buffer admitted_arsenal;
    uint64_t composition;
    qa_unified_document *offer;
    application_unified_output pending;
    size_t control_cursor;
    uint32_t pending_first, pending_last;
    uint64_t frame_before, published_frame;
    uint64_t events_after, pending_events_through;
    qa_event_receipts event_receipts;
    uint64_t publication_overflows, resync_started_ns, resync_after;
    uint32_t resync_sequence;
    bool resync_pending, resync_prepared, resync_started;
    /* Derived setup receipts; a restored transport declares its dictionary again. */
    size_t declared_resources, pending_declared_resources;
    int64_t acknowledged;
    bool bound, admitted, player_attached, admitted_receipt, preparing_frame, entered, closed;
    bool restore_pending;
    bool source_dropped;
    qa_actor_owner drop_source_owner;
    uint32_t drop_source_slot;
    const qa_launch_instance *drop_source_launch;
    bool drop_player_detached;
};
void application_unified_server_resync(application_unified_server *);
#endif
