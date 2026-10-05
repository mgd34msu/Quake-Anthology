#ifndef QA_NETWORK_Q2_SESSION_INTERNAL_H
#define QA_NETWORK_Q2_SESSION_INTERNAL_H

#include "../runtime/internal.h"
#include "qa/network_q2_session.h"
#include <stdlib.h>
#include <string.h>

typedef struct q2_game_state {
    qa_q2_game_state view;
    qa_q2_config_entry *configs;
    qa_q2_entity *baselines;
} q2_game_state;
typedef struct q2_owned_record {
    qa_buffer raw, text, values;
    qa_q2_wire_frame *frame;
} q2_owned_record;
typedef struct q2_records {
    qa_q2_server_record *records;
    q2_owned_record *owned;
    size_t count, capacity, cursor;
    uint64_t received_ns;
} q2_records;
typedef struct q2_command_group {
    qa_q2_usercmd commands[QA_NETWORK_MAX_SEATS];
    uint64_t number;
} q2_command_group;
typedef struct q2_server {
    qa_network_q2_server_hooks hooks;
    qa_network_q2_server_policy policy;
    q2_game_state signon;
    qa_q2_frame_history *frames;
    qa_q2_command_replay replay[QA_NETWORK_MAX_SEATS];
    uint64_t source_sequence[QA_NETWORK_MAX_SEATS];
    qa_q2_server_settings settings;
    char userinfo[8193];
    uint64_t last_source_frame;
    int32_t wire_frame;
    bool has_source_frame;
    char *drop_reason;
    bool drop_notice, drop_queued, drop_sent, drop_hook_done;
    qa_resource *download;
    const qa_vfs *download_view;
    qa_vfs_acquisition download_opening;
    qa_buffer download_source, download_wire;
    bool download_memory;
    size_t download_offset;
    qa_buffer datagram;
    uint32_t dropped;
    uint8_t reading_seat;
    bool signon_started;
} q2_server;
typedef struct q2_client {
    qa_network_q2_client_hooks hooks;
    qa_network_q2_client_policy policy;
    qa_q2_messages *messages;
    qa_q2_packet_sink recording;
    q2_records batch;
    q2_game_state preparing;
    qa_q2_serverdata server_data;
    qa_q2_usercmd oldest[QA_NETWORK_MAX_SEATS], previous[QA_NETWORK_MAX_SEATS];
    q2_command_group *commands;
    size_t command_count, command_capacity;
    q2_command_group sent;
    uint32_t sent_sequence;
    uint64_t sent_ns;
    size_t sent_cursor;
    bool sent_pending;
    uint64_t loading_generation, command_number;
    uint32_t acknowledged;
    size_t command_offset;
    int32_t last_frame;
    char *drop_reason;
    bool drop_canceled, drop_hook_done;
    bool drop_notice, drop_queued, drop_sent, drop_notify, drop_records_needed, drop_records_done;
    uint8_t drop_transmissions;
    bool has_server_data, preparing_game_state, selecting_server_data, receive_held, acknowledgement_held, preparation_held;
} q2_client;
typedef struct q2_session {
    qa_network_runtime *runtime;
    qa_net_client_id id;
    qa_q2_channel *channel;
    qa_q2_codec codec;
    size_t seats;
    bool server, active, retiring, busy;
    union { q2_server server; q2_client client; } state;
} q2_session;

bool q2_fail(qa_error *, qa_status, const char *);
bool q2_buffer_copy(qa_bytes, qa_buffer *, qa_error *);
bool q2_buffer_append(qa_buffer *, qa_bytes, size_t maximum, qa_error *);
void q2_game_state_free(q2_game_state *);
bool q2_game_state_clone(const qa_q2_game_state *, q2_game_state *, qa_error *);
void q2_records_free(q2_records *);
bool q2_record_retain(void *, const qa_q2_server_record *, qa_error *);
q2_session *q2_get(qa_network_runtime *, qa_net_client_id, bool server, qa_error *);
bool q2_queue_event(q2_session *, const qa_q2_server_event *, uint8_t, bool, qa_error *);
bool q2_queue_bytes(q2_session *, qa_bytes, uint8_t, bool, qa_error *);
bool q2_send(q2_session *, qa_bytes, uint64_t, bool *included, qa_error *);
bool q2_server_record(void *, const qa_q2_client_record *, qa_error *);
bool q2_server_restart(q2_session *, qa_error *);
bool q2_server_drop_progress(q2_session *, uint64_t, bool *complete, qa_error *);
void q2_server_clear(q2_server *);
bool q2_client_submit(q2_session *, const qa_network_command *, qa_error *);
bool q2_client_send(q2_session *, uint64_t, qa_error *);
bool q2_client_restart(q2_session *, qa_error *);
bool q2_client_receive(q2_session *, qa_bytes, uint32_t, uint64_t, qa_error *);
void q2_client_clear(q2_client *);
bool q2_download_begin(q2_session *, const char *, const char *, qa_error *);
bool q2_download_next(q2_session *, qa_error *);
bool q2_server_setting(q2_session *, int16_t, int16_t, qa_error *);
bool q2_server_align(q2_session *, uint64_t source_frame, qa_error *);
bool q2_server_userinfo(q2_session *, const char *, qa_error *);
bool q2_server_userinfo_delta(q2_session *, const char *, const char *, qa_error *);
bool q2_server_projection(q2_session *, const qa_q2_wire_frame *, const qa_q2_wire_frame *,
    const qa_q2_source_motion *, qa_q2_wire_frame *, qa_error *);
void q2_download_close(q2_server *);
qa_bytes q2_download_bytes(const q2_server *);
extern const qa_network_peer_ops qa_network_q2_peer_ops;
bool q2_server_hooks_valid(const qa_network_q2_server_hooks *);
bool q2_client_hooks_valid(const qa_network_q2_client_hooks *);
bool qa_network_q2_peer(const qa_network_peer *);
bool qa_network_q2_retirement_pending(const qa_network_peer *);
bool qa_network_q2_delivery_pending(const qa_network_peer *);
bool qa_network_q2_timeout(qa_network_peer *, const char *, qa_error *);
bool q2_server_drop_request(q2_session *, const char *, qa_error *);
bool q2_client_drop_progress(q2_session *, const char *, qa_error *);
bool qa_network_q2_peer_matches(const qa_network_peer *, const qa_net_datagram *);

#endif
