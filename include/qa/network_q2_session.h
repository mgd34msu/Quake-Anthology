#ifndef QA_NETWORK_Q2_SESSION_H
#define QA_NETWORK_Q2_SESSION_H

#include "qa/network_runtime.h"
#include "qa/network_q2_messages.h"
#include "qa/vfs.h"
#include "qa/console.h"

typedef struct qa_q2_config_entry {
    uint16_t index;
    const char *value;
} qa_q2_config_entry;
typedef struct qa_q2_game_state {
    qa_q2_serverdata data;
    const qa_q2_config_entry *configs;
    size_t config_count;
    qa_q2_entity_span baselines;
} qa_q2_game_state;

/* These are physical GAME receipts. A selected character or canonical actor
 * index does not establish the source owner or edict number. */
typedef struct qa_network_q2_player {
    qa_actor_id actor;
    qa_actor_owner source_owner;
    uint32_t source_slot;
    qa_movement_kind movement;
} qa_network_q2_player;
typedef enum qa_q2_download_resource_status {
    QA_Q2_DOWNLOAD_UNHANDLED,
    QA_Q2_DOWNLOAD_HELD,
    QA_Q2_DOWNLOAD_MEMORY,
    QA_Q2_DOWNLOAD_MISSING
} qa_q2_download_resource_status;
typedef struct qa_network_q2_download_source {
    qa_vfs *content;
    const qa_cvars *cvars;
    void *resource_context;
    /* HELD loans the actual view and transfers a retained
     * resource plus its owned opening. Optional nonempty owned wire bytes
     * carry a Source-derived artifact; the original resource remains its
     * immutable provenance. UNHANDLED leaves owned outputs empty and permits
     * ordinary Source lookup. MEMORY loans the actual Source view and transfers
     * its owned catalog bytes without a file resource or opening; optional wire
     * bytes retain a derived artifact. MISSING loans the actual dependency view, leaves
     * owned outputs empty and refuses that known alias without fallback. */
    bool (*resource)(void *, const char *requested, const qa_vfs **view,
        qa_resource **retained, qa_vfs_acquisition *owned_opening,
        qa_buffer *source_bytes, qa_buffer *wire_bytes, qa_q2_download_resource_status *, qa_error *);
} qa_network_q2_download_source;
typedef struct qa_network_q2_server_hooks {
    void *context;
    bool (*player)(void *, qa_net_client_id, qa_net_seat_id,
        qa_network_q2_player *, qa_error *);
    bool (*game_state)(void *, qa_net_client_id, qa_q2_game_state *, qa_error *);
    bool (*begin)(void *, qa_net_client_id, qa_net_seat_id, qa_error *);
    bool (*input)(void *, qa_net_client_id, qa_net_seat_id,
        const qa_network_q2_player *, const qa_q2_usercmd *, uint64_t source_sequence,
        qa_error *);
    /* Actual Source console macro expansion precedes protocol command routing.
     * Owned bytes include the terminating NUL. Empty output means that Source
     * discarded the line after its genuine lexical diagnostic. */
    bool (*expand_command)(void *, qa_net_client_id, const char *, qa_buffer *, qa_error *);
    bool (*command)(void *, qa_net_client_id, qa_net_seat_id, const char *, qa_error *);
    /* Returns the actual GAME-mutated dictionary as owned NUL-terminated
     * bytes. Later native deltas use this Source result. */
    bool (*userinfo)(void *, qa_net_client_id, qa_net_seat_id, const char *, qa_buffer *returned, qa_error *);
    bool (*download_source)(void *, qa_net_client_id,
        qa_network_q2_download_source *, qa_error *);
    /* Records retirement for the enclosing owner. Never detaches in a hook. */
    bool (*drop)(void *, qa_net_client_id, const char *, qa_error *);
    /* Commits already allocated beforeimages after complete signon queue
     * acceptance. This hook performs no fallible Source work or allocation. */
    void (*game_state_accepted)(void *, qa_net_client_id);
} qa_network_q2_server_hooks;
typedef struct qa_network_q2_server_policy {
    qa_q2_channel_options channel;
    uint32_t max_clients;
    size_t history_capacity;
    int32_t server_count;
    uint64_t source_interval_ns;
} qa_network_q2_server_policy;
typedef struct qa_q2_server_settings {
    int32_t values[14];
    uint32_t fps, frame_divisor;
    uint64_t source_interval_ns;
} qa_q2_server_settings;

/* Genuine physical Source link receipts. The eight cells are the engine's
 * link-time origin ring, independent of recipient visibility and wire deltas. */
typedef struct qa_q2_source_origin {
    uint64_t source_frame;
    qa_vec3 origin;
    bool present;
} qa_q2_source_origin;
typedef struct qa_q2_source_entity_motion {
    qa_actor_id actor;
    uint32_t source_slot;
    uint64_t link_count, source_frame, creation_frame;
    qa_vec3 origin, creation_origin;
    bool creation_present;
    qa_q2_source_origin origins[8];
} qa_q2_source_entity_motion;
typedef struct qa_q2_source_motion {
    qa_actor_owner source_owner;
    uint64_t source_frame;
    const qa_q2_source_entity_motion *rows;
    size_t count;
} qa_q2_source_motion;

typedef enum qa_q2_preparation {
    QA_Q2_PREPARATION_WAITING,
    QA_Q2_PREPARATION_READY,
    QA_Q2_PREPARATION_CANCELED,
    /* A genuine acquisition is awaiting further game-channel download records.
     * Its retained operation remains pending while this batch can be drained. */
    QA_Q2_PREPARATION_RECEIVING
} qa_q2_preparation;
typedef struct qa_network_q2_client_hooks {
    void *context;
    bool (*current)(void *, qa_net_client_id, qa_error *);
    /* A retained preparation may wait. Its real owner resumes that operation;
     * repeated calls must not repeat directory selection or Source startup. */
    bool (*server_data)(void *, qa_net_client_id, uint64_t loading_generation,
        const qa_q2_serverdata *, qa_q2_preparation *, qa_error *);
    bool (*prepare)(void *, qa_net_client_id, uint64_t loading_generation,
        const qa_q2_game_state *, qa_q2_preparation *, qa_error *);
    bool (*frame)(void *, qa_net_client_id, const qa_q2_wire_frame *,
        const qa_q2_server_record *, size_t record_count, uint64_t received_ns, qa_error *);
    bool (*records)(void *, qa_net_client_id, const qa_q2_server_record *, size_t, qa_error *);
    bool (*download)(void *, qa_net_client_id,
        const qa_q2_server_event *, bool *complete, qa_error *);
    bool (*cancel_loading)(void *, qa_net_client_id, qa_error *);
    bool (*acknowledged)(void *, qa_net_client_id, uint32_t, uint64_t, qa_error *);
    /* A failed notification retains this accepted packet/command receipt for
     * retry. The callback must accept the same receipt without replaying input. */
    bool (*sent)(void *, qa_net_client_id, qa_net_seat_id, uint32_t packet_sequence, uint64_t command_number,
        const qa_q2_usercmd *, uint64_t, qa_error *);
    bool (*command)(void *, const qa_network_command *, qa_q2_usercmd *, qa_error *);
    /* Remaining stufftext reaches the actual client Source command owner.
     * source_seat retains the service marker (0 broadcast, 1..N recipients). */
    bool (*server_command)(void *, qa_net_client_id, uint8_t source_seat, const char *, qa_error *);
    bool (*print)(void *, qa_net_client_id, const char *, qa_error *);
    bool (*drop)(void *, qa_net_client_id, const char *, qa_error *);
} qa_network_q2_client_hooks;
typedef struct qa_network_q2_client_policy {
    qa_q2_channel_options channel;
    qa_q2_message_options messages;
    size_t pending_commands;
} qa_network_q2_client_policy;

/* Attach only after genuine handshake/source admission. The existing runtime
 * owns the transport and its sole receive loop. A KEX LAN wrapper is installed
 * by its transport owner before runtime creation; this is the game channel. */
bool qa_network_attach_q2_server(qa_network_runtime *, const qa_net_connect *,
    const qa_network_q2_server_policy *, const qa_network_q2_server_hooks *,
    uint64_t now_ns, qa_net_client_id *, qa_error *);
bool qa_network_attach_q2_client(qa_network_runtime *, const qa_net_connect *,
    const qa_network_q2_client_policy *, const qa_network_q2_client_hooks *,
    uint64_t now_ns, qa_net_client_id *, qa_error *);

bool qa_network_q2_server_event(qa_network_runtime *, qa_net_client_id,
    const qa_q2_server_event *, uint8_t wire_seat, bool reliable, qa_error *);
bool qa_network_q2_server_bytes(qa_network_runtime *, qa_net_client_id,
    qa_bytes, uint8_t wire_seat, bool reliable, qa_error *);
/* Frame and recipient filtering come from the actual physical GAME. The
 * adapter owns wire delta history, never another simulation world or clock. */
bool qa_network_q2_server_frame(qa_network_runtime *, qa_net_client_id,
    const qa_q2_wire_frame *, const qa_q2_source_motion *, uint64_t now_ns, qa_error *);
bool qa_network_q2_server_drop(qa_network_runtime *, qa_net_client_id,
    const char *, uint64_t now_ns, qa_error *);
/* Retains the reason and starts retirement; the sole pump sends the notice. */
bool qa_network_q2_server_request_drop(qa_network_runtime *, qa_net_client_id,
    const char *, qa_error *);
bool qa_network_q2_server_command(qa_network_runtime *, qa_net_client_id,
    uint8_t wire_seat, const char *, qa_error *);
bool qa_network_q2_server_userinfo(qa_network_runtime *, qa_net_client_id,
    const char *, qa_error *);
/* Loans the actual retained GAME-returned dictionary; no callback or change. */
bool qa_network_q2_server_userinfo_read(qa_network_runtime *, qa_net_client_id,
    const char **, qa_error *);
bool qa_network_q2_server_settings(qa_network_runtime *, qa_net_client_id,
    const qa_q2_server_settings **, qa_error *);
/* Applies the returned upcoming Source policy before the enclosing generic
 * restart. It preserves channel ownership; restart clears signon/history and
 * advances servercount exactly once. */
bool qa_network_q2_server_prepare_restart(qa_network_runtime *, qa_net_client_id,
    uint32_t source_max_clients, uint64_t source_interval_ns, qa_error *);
/* Pure loan of the actual outbound codec, including negotiated serverdata
 * flags. The caller must not mutate it or retain it after connection release. */
bool qa_network_q2_server_codec(qa_network_runtime *, qa_net_client_id,
    const qa_q2_codec **, qa_error *);
/* Pure immutable-holder inventory for the enclosing capture lease. A memory
 * Source artifact has a real view and absent resource/opening. All outputs are
 * absent when no hosted download remains; nothing is reopened. */
bool qa_network_q2_server_download(qa_network_runtime *, qa_net_client_id,
    const qa_vfs **, const qa_resource **, const qa_vfs_acquisition **, qa_error *);

/* The decoder retains complete owned records before invoking the actual
 * application outside the pump. Waiting preparation holds the receiver and
 * blocks subsequent polling until continue has consumed its retained batch. */
bool qa_network_q2_client_continue(qa_network_runtime *, qa_net_client_id, qa_error *);
bool qa_network_q2_client_command(qa_network_runtime *, qa_net_client_id,
    const char *, uint8_t wire_seat, qa_error *);
bool qa_network_q2_client_control(qa_network_runtime *, qa_net_client_id,
    const qa_q2_client_event *, uint8_t wire_seat, qa_error *);
bool qa_network_q2_client_send(qa_network_runtime *, qa_net_client_id,
    uint64_t now_ns, qa_error *);
bool qa_network_q2_client_usercmds(qa_network_runtime *, qa_net_client_id,
    const qa_q2_usercmd *, size_t seats, qa_error *);
bool qa_network_q2_client_disconnect(qa_network_runtime *, qa_net_client_id,
    uint64_t now_ns, qa_error *);
bool qa_network_q2_client_request_full_frame(qa_network_runtime *, qa_net_client_id, qa_error *);
const qa_q2_messages *qa_network_q2_client_messages(qa_network_runtime *, qa_net_client_id);
/* Borrowed actual server-data receipt, absent before its accepted service.
 * A zero fps remains a genuinely absent classic-wire field; its Source
 * profile defines the frame interval outside this transport owner. */
bool qa_network_q2_client_serverdata(qa_network_runtime *, qa_net_client_id,
    const qa_q2_serverdata **, qa_error *);

typedef struct qa_network_q2_state {
    bool server, active, retiring, receive_pending, preparing;
    int32_t server_count, acknowledged_frame;
    uint64_t loading_generation;
    size_t pending_commands, pending_records;
    qa_q2_channel_status channel;
} qa_network_q2_state;
bool qa_network_q2_state_read(qa_network_runtime *, qa_net_client_id,
    qa_network_q2_state *, qa_error *);

#endif
