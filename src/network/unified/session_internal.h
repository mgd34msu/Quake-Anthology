#ifndef QA_UNIFIED_SESSION_INTERNAL_H
#define QA_UNIFIED_SESSION_INTERNAL_H
#include "qa/network_unified_session.h"

typedef struct qa_unified_held {
    struct qa_unified_held *next;
    qa_unified_document *document;
    qa_unified_document_kind kind;
    qa_buffer wire;
    size_t bytes;
    uint32_t sequence, required;
    qa_unified_session_commit commit;
    uint32_t response_first, response_last;
    bool source_finished, responses_queued;
} qa_unified_held;
struct qa_unified_session {
    qa_network_runtime *runtime;
    qa_net_client_id id;
    qa_net_seat_id seat;
    qa_unified_token token;
    qa_unified_limits limits;
    qa_unified_channel *channel;
    qa_unified_session_hooks hooks;
    qa_unified_held *held, *tail;
    qa_unified_held *timeout_delivery;
    size_t held_bytes, held_count;
    qa_unified_input_batch inputs;
    uint32_t epoch, required;
    uint32_t reliable_applied, frame_applied;
    uint8_t close_cause; /* 0 none, 1 actual timeout, 2 actual Source request. */
    uint64_t now_ns, closing_ns;
    int64_t acknowledged;
    bool server, admitted, disconnected, closing, timeout_pending, bound_source, entered, processing;
};
bool qa_unified_session_fail(qa_error *, qa_status, const char *);
bool qa_unified_session_queue_control(qa_unified_session *, const qa_unified_document *, qa_error *);
bool qa_unified_session_command(void *, const qa_network_command *, qa_error *);
bool qa_unified_session_queue_inputs(qa_unified_session *, qa_error *);
bool qa_unified_session_receive_resume(qa_unified_session *, qa_error *);
void qa_unified_session_ack(qa_unified_session *, int64_t);
bool qa_unified_session_player_read(const qa_unified_session *, qa_unified_session_player *, qa_error *);
bool qa_unified_session_document_epoch(const qa_unified_document *, uint32_t *, qa_error *);
qa_json_id qa_unified_session_value(const qa_unified_document *);
bool qa_unified_session_kind(const qa_unified_document *, const char *);
void qa_unified_session_release(qa_unified_session *);
void qa_unified_session_delivery_free(qa_unified_held *);
bool qa_unified_session_reply_valid(const qa_unified_session *, const qa_unified_document *, qa_error *);
bool qa_unified_session_continuation_valid(const qa_unified_session *, const qa_unified_held *, qa_error *);
bool qa_unified_session_continuation_extent(const qa_unified_held *, size_t *, qa_error *);
bool qa_unified_session_continuation_write(qa_net_writer *, const qa_unified_held *);
bool qa_unified_session_continuation_read(qa_net_reader *, qa_unified_held *);
qa_network_peer_ops qa_unified_session_operations(void);
struct qa_network_peer;
bool qa_unified_session_peer(const struct qa_network_peer *);
bool qa_unified_session_peer_matches(const struct qa_network_peer *, const qa_net_datagram *);
bool qa_unified_session_peer_checkpoint(const struct qa_network_peer *, qa_buffer *, qa_error *);
bool qa_unified_session_attachment(const qa_network_runtime *, const qa_network_peer_ops *, const void *, const qa_net_connect *);
bool qa_unified_session_token_conflict(const void *, const struct qa_network_peer *);
bool qa_unified_session_peer_tokens_equal(const struct qa_network_peer *, const struct qa_network_peer *);
bool qa_unified_session_peer_source_ready(const struct qa_network_peer *, qa_error *);
void qa_unified_session_peer_source_publish(struct qa_network_peer *);
void qa_unified_session_peer_source_retire(struct qa_network_peer *);
#endif
