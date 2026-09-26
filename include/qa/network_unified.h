#ifndef QA_NETWORK_UNIFIED_H
#define QA_NETWORK_UNIFIED_H

#include "qa/network.h"
#include "qa/json.h"
#include "qa/movement.h"

#define QA_UNIFIED_HEADER_BYTES 48u
#define QA_UNIFIED_MAX_DATAGRAM 65507u
#define QA_UNIFIED_SAFE_INTEGER UINT64_C(9007199254740991)

typedef struct qa_unified_token { uint8_t bytes[16]; } qa_unified_token;
bool qa_unified_token_parse(const char *, qa_unified_token *, qa_error *);
void qa_unified_token_format(qa_unified_token, char out[33]);
typedef enum qa_unified_packet_kind {
    QA_UNIFIED_ACK, QA_UNIFIED_RELIABLE, QA_UNIFIED_FRAME
} qa_unified_packet_kind;
typedef struct qa_unified_packet {
    qa_unified_packet_kind kind;
    qa_unified_token token;
    uint32_t sequence, acknowledged_reliable, required_reliable;
    uint32_t total_bytes, fragment_bytes;
    uint16_t fragment, fragments;
    qa_bytes payload;
} qa_unified_packet;
/* Decode borrows the datagram. Invalid packets leave out unchanged. */
bool qa_unified_packet_decode(qa_bytes, qa_unified_packet *, qa_error *);
bool qa_unified_packet_encode(const qa_unified_packet *, void *, size_t,
                               size_t *written, qa_error *);

typedef struct qa_unified_limits {
    uint32_t datagram_bytes, message_bytes;
    size_t queued_reliable_bytes;
    uint32_t queued_reliable_messages, reliable_window_messages;
    uint16_t fragments;
    uint32_t packets_per_flush, maximum_transmissions;
    uint64_t retry_ns, assembly_ns;
} qa_unified_limits;
typedef struct qa_unified_channel qa_unified_channel;
typedef struct qa_unified_delivery {
    qa_unified_packet_kind kind;
    uint32_t sequence, required_reliable;
    qa_bytes payload;
} qa_unified_delivery;
typedef bool (*qa_unified_send_fn)(void *, qa_bytes, qa_error *);
/* Delivery bytes live only for this synchronous callback. Callbacks may queue
 * outgoing messages, but must not receive, flush, close or destroy the channel.
 * Delivery failure closes it: accepted reliable
 * state cannot be silently discarded and acknowledged. Send failure retains
 * the unsent packet and consumes neither a retry nor an ACK. */
typedef bool (*qa_unified_deliver_fn)(void *, const qa_unified_delivery *, qa_error *);
qa_unified_limits qa_unified_limits_default(void);
bool qa_unified_channel_create(qa_unified_token, const qa_unified_limits *,
                                qa_unified_channel **, qa_error *);
void qa_unified_channel_close(qa_unified_channel *);
void qa_unified_channel_destroy(qa_unified_channel *);
bool qa_unified_channel_closed(const qa_unified_channel *);
uint32_t qa_unified_channel_received(const qa_unified_channel *);
uint32_t qa_unified_channel_acknowledged(const qa_unified_channel *);
bool qa_unified_channel_reliable(qa_unified_channel *, qa_bytes,
                                  uint32_t *sequence, qa_error *);
bool qa_unified_channel_frame(qa_unified_channel *, qa_bytes,
                               uint32_t required_reliable, qa_error *);
/* Foreign, malformed, obsolete and out-of-window packets are ignored without
 * acquiring assembly storage. now_ns is an external monotonic timestamp. */
bool qa_unified_channel_receive(qa_unified_channel *, qa_bytes, uint64_t now_ns,
                                 qa_unified_deliver_fn, void *, qa_error *);
bool qa_unified_channel_flush(qa_unified_channel *, uint64_t now_ns,
                               qa_unified_send_fn, void *, size_t *sent, qa_error *);

/* QTCM v1 deliberately retains binary64 command values until the selected
 * provider rounds them. The wire dialect is independent of gameplay floats. */
typedef struct qa_unified_vec3 { double x, y, z; } qa_unified_vec3;
typedef struct qa_unified_movement {
    qa_movement_kind kind;
    union {
        struct { double acknowledged_seconds; qa_unified_vec3 angles;
                 double forward, side, up, buttons, impulse; } nq;
        struct { double milliseconds; qa_unified_vec3 angles;
                 double forward, side, up, buttons, impulse; } qw;
        struct { double milliseconds, angle_shorts[3];
                 double forward, side, up, buttons, impulse, light_level; } q2;
        struct { double milliseconds; qa_unified_vec3 angles;
                 double forward, side, buttons, server_frame; } q2r;
        struct { double server_time_ms, angle_words[3], buttons, weapon;
                 double forward, right, up; } q3;
    } data;
} qa_unified_movement;
typedef struct qa_unified_arsenal {
    qa_bytes provider, weapon; /* UTF-8; weapon.size == 0 means no selection. */
    bool use_holdable;
} qa_unified_arsenal;
typedef enum qa_unified_source_kind {
    QA_UNIFIED_SOURCE_LOCAL, QA_UNIFIED_SOURCE_REMOTE, QA_UNIFIED_SOURCE_BOT
} qa_unified_source_kind;
typedef struct qa_unified_source {
    qa_unified_source_kind kind;
    qa_net_client_id client;
    qa_net_seat_id seat;
} qa_unified_source;
typedef struct qa_unified_command {
    qa_actor_id actor;
    qa_unified_source source;
    uint64_t sequence;
    qa_unified_movement movement;
    bool has_arsenal;
    qa_unified_arsenal arsenal;
} qa_unified_command;
typedef struct qa_unified_controlled_actor {
    qa_actor_id actor;
    qa_movement_kind movement;
    qa_bytes arsenal;
} qa_unified_controlled_actor;
typedef struct qa_unified_command_receiver {
    qa_unified_source source; /* Authenticated, never decoded from the packet. */
    /* Current world registry, independent of the connection owner's namespace.
     * Update after world restoration before accepting further commands. */
    uint64_t actor_registry;
    void *context;
    bool (*controlled)(void *, uint32_t slot, uint32_t generation,
                       qa_unified_controlled_actor *, qa_error *);
} qa_unified_command_receiver;
bool qa_unified_command_encode(const qa_unified_command *, qa_buffer *, qa_error *);
/* Arsenal strings borrow bytes. Authority checks run after complete parsing. */
bool qa_unified_command_decode(qa_bytes, const qa_unified_command_receiver *,
                                qa_unified_command *, qa_error *);

typedef struct qa_unified_composition {
    qa_buffer canonical;
    qa_sha256_digest digest;
} qa_unified_composition;
/* Canonicalizes JSON using the donor's UTF-16 key order and number syntax. */
bool qa_unified_composition_create(qa_bytes json, qa_unified_composition *, qa_error *);
void qa_unified_composition_free(qa_unified_composition *);
bool qa_unified_composition_offer(const qa_unified_composition *, qa_buffer *, qa_error *);
bool qa_unified_composition_admit(const qa_unified_composition *, qa_bytes offer, qa_error *);

/* Checkpoint envelopes retain every schema field, including tagged bytes,
 * bigint, non-finite numbers and negative zero. Documents own their source and
 * index. Owners use the JSON reader plus these tagged-value readers; resolving
 * identities and publishing world state belongs to the admitted session. */
typedef struct qa_unified_document qa_unified_document;
typedef enum qa_unified_document_kind {
    QA_UNIFIED_CHECKPOINT, QA_UNIFIED_CONTROL_DOCUMENT,
    QA_UNIFIED_INPUT_DOCUMENT, QA_UNIFIED_HANDSHAKE_DOCUMENT,
    QA_UNIFIED_FRAME_DOCUMENT, QA_UNIFIED_PREDICTION_DOCUMENT,
    QA_UNIFIED_EVENTS_DOCUMENT
} qa_unified_document_kind;
/* Create reads an uncompressed checkpoint JSON value, including for frames.
 * Decode additionally applies the wire compression and wire byte limits.
 * These functions validate the envelope and core schema. Rich presentation,
 * resource, component ABI and event owners validate their records through
 * document_validate before any gameplay or presentation state is published. */
bool qa_unified_document_create(qa_unified_document_kind, qa_bytes checkpoint_json,
                                 qa_unified_document **, qa_error *);
bool qa_unified_document_decode(qa_unified_document_kind, qa_bytes,
                                 qa_unified_document **, qa_error *);
typedef bool (*qa_unified_document_validator)(void *, const qa_unified_document *, qa_error *);
bool qa_unified_document_validate(const qa_unified_document *,
                                   qa_unified_document_validator, void *, qa_error *);
qa_unified_document_kind qa_unified_document_type(const qa_unified_document *);
bool qa_unified_document_encode(const qa_unified_document *, qa_buffer *, qa_error *);
void qa_unified_document_destroy(qa_unified_document *);
const qa_json_document *qa_unified_document_json(const qa_unified_document *);
qa_json_id qa_unified_document_root(const qa_unified_document *);
bool qa_unified_document_bytes(const qa_unified_document *, qa_json_id, qa_buffer *, qa_error *);
bool qa_unified_document_number(const qa_unified_document *, qa_json_id, double *, qa_error *);
/* Bigints are returned as canonical decimal text, without a precision limit. */
bool qa_unified_document_bigint(const qa_unified_document *, qa_json_id, qa_buffer *, qa_error *);
bool qa_unified_checkpoint_bytes(qa_bytes, qa_buffer *json, qa_error *);
bool qa_unified_checkpoint_bigint(const char *decimal, qa_buffer *json, qa_error *);
bool qa_unified_checkpoint_number(double, qa_buffer *json, qa_error *);

#endif
