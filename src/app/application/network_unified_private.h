#ifndef QA_APPLICATION_NETWORK_UNIFIED_PRIVATE_H
#define QA_APPLICATION_NETWORK_UNIFIED_PRIVATE_H
#include "network_unified.h"
#include "unified_output_capture.h"

typedef struct retained_input {
    qa_unified_input value;
    qa_buffer provider, weapon;
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
    size_t count, cursor;
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
    application_unified_source offered;
    qa_unified_session_player admitted_player;
    qa_buffer admitted_arsenal;
    qa_sha256_digest composition;
    qa_unified_document *offer;
    application_unified_output pending;
    size_t control_cursor;
    uint32_t pending_first, pending_last;
    uint64_t frame_before, published_frame;
    uint64_t events_after, pending_events_through;
    int64_t acknowledged;
    bool bound, admitted, player_attached, admitted_receipt, preparing_frame, entered, closed;
    bool restore_pending;
    bool source_dropped;
    qa_actor_owner drop_source_owner;
    uint32_t drop_source_slot;
    const qa_launch_instance *drop_source_launch;
    bool drop_player_detached;
};
#endif
