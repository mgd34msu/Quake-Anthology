#ifndef QA_NETWORK_H
#define QA_NETWORK_H

#include "qa/binary.h"
#include "qa/actors.h"
#include "qa/hash.h"

/* Sticky checked cursors. Bits are least-significant first. Byte access also
 * works at a bit offset; protocol-specific Huffman coding owns its own cursor.
 * Failed operations set failed and preserve the first error. */
typedef struct qa_net_reader {
    qa_bytes bytes;
    size_t bit;
    bool failed;
    qa_error *error;
} qa_net_reader;
typedef struct qa_net_writer {
    uint8_t *data;
    size_t capacity, bit;
    bool failed;
    qa_error *error;
} qa_net_writer;
void qa_net_reader_init(qa_net_reader *, qa_bytes, qa_error *);
void qa_net_writer_init(qa_net_writer *, void *, size_t, qa_error *);
size_t qa_net_reader_remaining(const qa_net_reader *);
size_t qa_net_writer_size(const qa_net_writer *);
bool qa_net_reader_finish(qa_net_reader *);
bool qa_net_reader_fail(qa_net_reader *, const char *);
bool qa_net_writer_fail(qa_net_writer *, const char *);
uint32_t qa_net_read_bits(qa_net_reader *, unsigned);
int32_t qa_net_read_sbits(qa_net_reader *, unsigned);
uint8_t qa_net_read_u8(qa_net_reader *);
int8_t qa_net_read_i8(qa_net_reader *);
uint16_t qa_net_read_u16(qa_net_reader *);
int16_t qa_net_read_i16(qa_net_reader *);
uint32_t qa_net_read_u32(qa_net_reader *);
int32_t qa_net_read_i32(qa_net_reader *);
uint64_t qa_net_read_u64(qa_net_reader *);
float qa_net_read_f32(qa_net_reader *);
double qa_net_read_f64(qa_net_reader *);
bool qa_net_read_data(qa_net_reader *, void *, size_t);
/* Borrow requires byte alignment. */
bool qa_net_read_bytes(qa_net_reader *, size_t, qa_bytes *);
bool qa_net_read_string(qa_net_reader *, char *, size_t);
bool qa_net_write_bits(qa_net_writer *, uint32_t, unsigned);
bool qa_net_write_u8(qa_net_writer *, uint8_t);
bool qa_net_write_i8(qa_net_writer *, int8_t);
bool qa_net_write_u16(qa_net_writer *, uint16_t);
bool qa_net_write_i16(qa_net_writer *, int16_t);
bool qa_net_write_u32(qa_net_writer *, uint32_t);
bool qa_net_write_i32(qa_net_writer *, int32_t);
bool qa_net_write_u64(qa_net_writer *, uint64_t);
bool qa_net_write_f32(qa_net_writer *, float);
bool qa_net_write_f64(qa_net_writer *, double);
bool qa_net_write_data(qa_net_writer *, const void *, size_t);
bool qa_net_write_string(qa_net_writer *, const char *);

typedef enum qa_net_protocol {
    QA_NET_NQ15, QA_NET_FITZ666, QA_NET_RMQ999,
    QA_NET_QW28, QA_NET_QW29,
    QA_NET_Q2_34, QA_NET_R1Q2_35, QA_NET_Q2PRO_36,
    QA_NET_Q2REPRO_1038, QA_NET_Q2KEX_2023,
    QA_NET_Q2KEX_DEMO_2022, QA_NET_Q2PRIVATE_4038,
    QA_NET_Q3_68, QA_NET_UNIFIED_1
} qa_net_protocol;
typedef struct qa_net_protocol_id {
    qa_net_protocol kind;
    uint32_t revision, flags;
} qa_net_protocol_id;
bool qa_net_protocol_valid(qa_net_protocol_id, qa_error *);

typedef enum qa_net_address_kind {
    QA_NET_IPV4, QA_NET_IPV6, QA_NET_LOOPBACK, QA_NET_IPX
} qa_net_address_kind;
typedef struct qa_net_address {
    qa_net_address_kind kind;
    uint16_t port;
    union {
        uint8_t ipv4[4];
        struct { uint8_t bytes[16]; uint32_t scope; } ipv6;
        char loopback[128];
        struct { uint32_t network; uint8_t node[6]; } ipx;
    } host;
} qa_net_address;
bool qa_net_address_equal(const qa_net_address *, const qa_net_address *, bool include_port);
bool qa_net_address_parse(const char *, uint16_t default_port, bool allow_zero,
                           qa_net_address *, qa_error *);
bool qa_net_address_resolve(const char *, uint16_t default_port,
                             unsigned family, qa_net_address *, qa_error *);
bool qa_net_address_format(const qa_net_address *, char *, size_t, qa_error *);

typedef struct qa_net_transport qa_net_transport;
typedef struct qa_net_loopback qa_net_loopback;
/* queue_packets bounds user-space queues and requests a UDP receive-buffer
 * size; the operating system may clamp that byte-size hint. */
typedef struct qa_net_limits { size_t datagram_bytes, queue_packets; } qa_net_limits;
typedef enum qa_net_poll_kind {
    QA_NET_POLL_EMPTY, QA_NET_POLL_PACKET, QA_NET_POLL_OVERSIZE, QA_NET_POLL_DROPPED
} qa_net_poll_kind;
typedef struct qa_net_datagram {
    qa_net_poll_kind kind;
    qa_net_address from;
    qa_bytes payload;
    uint64_t received_ns;
} qa_net_datagram;
typedef struct qa_net_udp_options {
    qa_net_address bind;
    qa_net_limits limits;
    bool broadcast, ipv6_only;
} qa_net_udp_options;
/* Each transport has one caller thread. Poll payloads are borrowed until its
 * next collection or close. now_ns is supplied by the application, not gameplay. */
typedef struct qa_net_transport_event {
    qa_net_datagram packet;
    /* Decoder metadata belongs to the child protocol and stays unchanged when
     * a host combines physical and loopback transports. */
    uint32_t source;
    /* Host routing is separate: low bit 0 selects external, 1 local; each host
     * wrapper shifts the child's route left on collect and right on dispatch. */
    uint32_t route;
} qa_net_transport_event;
/* Local owner-lifetime serials. Only native reliable completion advances
 * acknowledged; these values are never written into a protocol packet. */
typedef struct qa_network_reliable_receipt {
    uint64_t queued, inflight, acknowledged;
} qa_network_reliable_receipt;
typedef struct qa_net_transport_ops {
    bool (*send)(void *, const qa_net_address *, qa_bytes, qa_error *);
    bool (*collect)(void *, uint64_t now_ns, qa_net_transport_event *, qa_error *);
    void (*close)(void *);
    bool (*ready)(const void *);
    /* Decode one copied physical event. NULL drains saved logical deliveries.
     * Plain physical transports need no decoder. Neither operation reads input. */
    bool (*dispatch)(void *, const qa_net_transport_event *, qa_net_datagram *, bool *present, qa_error *);
    bool (*maintenance)(void *, uint64_t now_ns, qa_error *);
    bool (*reliable_receipt)(const void *, const qa_net_address *, qa_network_reliable_receipt *);
} qa_net_transport_ops;
bool qa_net_transport_create(const qa_net_address *, qa_net_limits,
                              const qa_net_transport_ops *, void *owned_state,
                              qa_net_transport **, qa_error *);
/* create transfers state ownership only on success. */
bool qa_net_udp_open(const qa_net_udp_options *, qa_net_transport **, qa_error *);
typedef struct qa_net_udp_policy { qa_net_address bound; bool ipv6_only; } qa_net_udp_policy;
/* Observes the owned native socket, including its actual IPV6_V6ONLY value.
 * Other transport kinds report present=false without claiming socket policy. */
bool qa_net_udp_policy_read(const qa_net_transport *, qa_net_udp_policy *, bool *present, qa_error *);
bool qa_net_loopback_create(qa_net_limits, qa_net_loopback **, qa_error *);
bool qa_net_loopback_bind(qa_net_loopback *, const char *, qa_net_transport **, qa_error *);
/* Combines an optional external transport and an existing local server endpoint
 * into one host stream. Both handles transfer only on success; the caller keeps
 * the local endpoint's hub alive. Bound address and UDP policy use the external
 * child when present. The local-only stream reports the local bound address.
 * Its datagram budget is the maximum child budget; each selected child still
 * enforces its own limit. */
bool qa_net_host_transport_create(qa_net_transport *external, qa_net_transport *local,
                                  qa_net_transport **out, qa_error *);
/* Closing a hub closes its endpoints; external transport handles remain valid
 * until transport_close, and operations on them report a closed endpoint. */
void qa_net_loopback_close(qa_net_loopback *);
const qa_net_address *qa_net_transport_address(const qa_net_transport *);
size_t qa_net_transport_limit(const qa_net_transport *);
bool qa_net_transport_ready(const qa_net_transport *);
bool qa_net_transport_send(qa_net_transport *, const qa_net_address *, qa_bytes, qa_error *);
bool qa_net_transport_collect(qa_net_transport *, uint64_t, qa_net_transport_event *, qa_error *);
bool qa_net_transport_dispatch(qa_net_transport *, const qa_net_transport_event *, qa_net_datagram *, bool *, qa_error *);
bool qa_net_transport_maintenance(qa_net_transport *, uint64_t, qa_error *);
/* Ordinary datagram transports return false and a zero receipt. */
bool qa_net_transport_reliable_receipt(const qa_net_transport *, const qa_net_address *, qa_network_reliable_receipt *);
void qa_net_transport_close(qa_net_transport *);

typedef struct qa_net_ipx_packet {
    qa_net_address from, to;
    uint8_t packet_type, hops;
    qa_bytes payload;
} qa_net_ipx_packet;
bool qa_net_ipx_encode(const qa_net_ipx_packet *, qa_net_writer *);
bool qa_net_ipx_decode(qa_bytes, qa_net_ipx_packet *, qa_error *);
bool qa_net_ipx_native_open(const qa_net_address *bind, qa_net_limits,
                             uint8_t packet_type, qa_net_transport **, qa_error *);
bool qa_net_ipx_game_wrap(qa_net_transport *raw, bool quake_sequence,
                           qa_net_transport **, qa_error *);
typedef struct qa_net_ipx_tunnel qa_net_ipx_tunnel;
/* Takes UDP ownership on success; qa_net_ipx_tunnel_transport is borrowed. */
bool qa_net_ipx_tunnel_create(qa_net_transport *udp, const qa_net_address *local,
                               bool quake_sequence, uint8_t packet_type,
                               qa_net_ipx_tunnel **, qa_error *);
qa_net_transport *qa_net_ipx_tunnel_transport(qa_net_ipx_tunnel *);
bool qa_net_ipx_tunnel_peer(qa_net_ipx_tunnel *, const qa_net_address *ipx,
                             const qa_net_address *udp, qa_error *);
bool qa_net_ipx_tunnel_remove(qa_net_ipx_tunnel *, const qa_net_address *);
void qa_net_ipx_tunnel_destroy(qa_net_ipx_tunnel *);
typedef struct qa_net_dosbox qa_net_dosbox;
/* Registration is nonblocking. Collect and dispatch queued input, then bind.
 * The network owns UDP on success; bound transport handles are caller-owned. */
bool qa_net_dosbox_create(qa_net_transport *udp, const qa_net_address *server,
                           uint64_t now_ns, uint64_t timeout_ns,
                           qa_net_dosbox **, qa_error *);
bool qa_net_dosbox_collect(qa_net_dosbox *, uint64_t now_ns, qa_net_transport_event *, qa_error *);
bool qa_net_dosbox_dispatch(qa_net_dosbox *, const qa_net_transport_event *, bool *registered, qa_error *);
bool qa_net_dosbox_maintenance(qa_net_dosbox *, uint64_t now_ns, qa_error *);
bool qa_net_dosbox_bind(qa_net_dosbox *, uint16_t port, uint8_t packet_type,
                         qa_net_transport **, qa_error *);
void qa_net_dosbox_destroy(qa_net_dosbox *);
/* SOCKS wrapper owns UDP after successful creation. Handshake advances from
 * queued dispatch; sends fail until ready. Credentials are never included in errors. */
typedef struct qa_net_socks_options {
    qa_net_address server;
    qa_bytes username, password;
    uint64_t timeout_ns;
} qa_net_socks_options;
bool qa_net_socks_wrap(qa_net_transport *udp, const qa_net_socks_options *,
                         uint64_t now_ns, qa_net_transport **, qa_error *);

typedef struct qa_net_toggle qa_net_toggle;
typedef struct qa_net_toggle_packet {
    uint32_t sequence, acknowledged;
    bool reliable, reliable_acknowledged;
    qa_bytes payload;
} qa_net_toggle_packet;
bool qa_net_toggle_create(size_t capacity, uint32_t first_sequence,
                           qa_net_toggle **, qa_error *);
void qa_net_toggle_destroy(qa_net_toggle *);
bool qa_net_toggle_queue(qa_net_toggle *, qa_bytes, qa_error *);
bool qa_net_toggle_transmit(qa_net_toggle *, qa_bytes unreliable, size_t capacity,
                             qa_net_toggle_packet *, qa_error *);
bool qa_net_toggle_receive(qa_net_toggle *, const qa_net_toggle_packet *,
                            bool *accepted, uint32_t *dropped, qa_error *);
bool qa_net_toggle_pending(const qa_net_toggle *);
uint32_t qa_net_toggle_incoming(const qa_net_toggle *);
uint32_t qa_net_toggle_outgoing(const qa_net_toggle *);
bool qa_net_toggle_advance(qa_net_toggle *, uint32_t, qa_error *);
/* Native demo sequence records apply only to a channel without reliable work. */
bool qa_net_toggle_demo_sequences(qa_net_toggle *, uint32_t outgoing, uint32_t incoming, qa_error *);

typedef struct qa_net_stopwait qa_net_stopwait;
typedef struct qa_net_reliable_fragment {
    uint32_t sequence;
    bool final;
    qa_bytes payload;
} qa_net_reliable_fragment;
typedef enum qa_net_fragment_result {
    QA_NET_FRAGMENT_DUPLICATE, QA_NET_FRAGMENT_PENDING, QA_NET_FRAGMENT_COMPLETE
} qa_net_fragment_result;
bool qa_net_stopwait_create(size_t message_bytes, size_t fragment_bytes,
                             uint64_t retry_ns, qa_net_stopwait **, qa_error *);
void qa_net_stopwait_destroy(qa_net_stopwait *);
bool qa_net_stopwait_begin(qa_net_stopwait *, qa_bytes, qa_error *);
bool qa_net_stopwait_next(qa_net_stopwait *, uint64_t now_ns,
                          bool *present, qa_net_reliable_fragment *, qa_error *);
bool qa_net_stopwait_acknowledge(qa_net_stopwait *, uint32_t);
bool qa_net_stopwait_receive(qa_net_stopwait *, const qa_net_reliable_fragment *,
                             qa_net_fragment_result *, qa_bytes *, qa_error *);
bool qa_net_stopwait_ready(const qa_net_stopwait *);

typedef struct qa_net_fragments qa_net_fragments;
typedef struct qa_net_fragment {
    uint32_t sequence;
    size_t offset;
    bool final;
    qa_bytes payload;
} qa_net_fragment;
bool qa_net_fragments_create(size_t message_bytes, size_t fragment_bytes,
                              bool terminal_empty, qa_net_fragments **, qa_error *);
void qa_net_fragments_destroy(qa_net_fragments *);
bool qa_net_fragments_begin(qa_net_fragments *, uint32_t, qa_bytes, qa_error *);
bool qa_net_fragments_next(qa_net_fragments *, bool *present, qa_net_fragment *, qa_error *);
bool qa_net_fragments_pending(const qa_net_fragments *);
bool qa_net_fragments_receive(qa_net_fragments *, const qa_net_fragment *,
                               qa_net_fragment_result *, qa_bytes *, qa_error *);
bool qa_net_fragments_accept(qa_net_fragments *, uint32_t sequence);

typedef struct qa_net_connections qa_net_connections;
typedef struct qa_net_client_id { uint64_t owner, generation; uint32_t slot; } qa_net_client_id;
typedef struct qa_net_seat_id { uint64_t owner; uint32_t index; } qa_net_seat_id;
typedef struct qa_net_seat_binding { qa_net_seat_id seat; uint32_t remote_index; } qa_net_seat_binding;
typedef enum qa_net_attachment { QA_NET_LOCAL_SEAT, QA_NET_REMOTE, QA_NET_HEADLESS } qa_net_attachment;
typedef enum qa_net_phase { QA_NET_CONNECTED, QA_NET_PRIMED, QA_NET_ACTIVE } qa_net_phase;
typedef struct qa_net_client {
    qa_net_client_id id;
    qa_net_attachment attachment;
    qa_net_address endpoint;
    qa_net_protocol_id protocol;
    qa_net_phase phase;
    const qa_net_seat_binding *seats;
    size_t seat_count;
    uint64_t connected_ns, received_ns;
    uint64_t composition;
} qa_net_client;
typedef struct qa_net_connect {
    qa_net_attachment attachment;
    qa_net_address endpoint;
    qa_net_protocol_id protocol;
    const qa_net_seat_binding *seats;
    size_t seat_count;
    uint64_t composition;
} qa_net_connect;
/* Source admission is mandatory: native protocols cannot silently omit mixed
 * state. The caller compares representability against its resolved recipe. */
typedef bool (*qa_net_admit_fn)(void *, const qa_net_connect *, qa_error *);
bool qa_net_connections_create(uint64_t owner, uint32_t capacity,
                                qa_net_admit_fn, void *, qa_net_connections **, qa_error *);
void qa_net_connections_destroy(qa_net_connections *);
bool qa_net_connections_add(qa_net_connections *, const qa_net_connect *,
                             uint64_t now_ns, qa_net_client_id *, qa_error *);
const qa_net_client *qa_net_connections_get(const qa_net_connections *, qa_net_client_id);
bool qa_net_connections_next(const qa_net_connections *, uint32_t *cursor, const qa_net_client **);
bool qa_net_connections_phase(qa_net_connections *, qa_net_client_id, qa_net_phase, qa_error *);
/* Re-admits a changed world/composition and starts gamestate sign-on while
 * retaining the connection identity and seats. Rejection leaves it unchanged. */
bool qa_net_connections_restart(qa_net_connections *, qa_net_client_id,
                                 const uint64_t *, qa_error *);
bool qa_net_connections_received(qa_net_connections *, qa_net_client_id, uint64_t, qa_error *);
bool qa_net_connections_rebind(qa_net_connections *, qa_net_client_id, const qa_net_address *, qa_error *);
bool qa_net_connections_remove(qa_net_connections *, qa_net_client_id, qa_error *);
bool qa_net_client_expired(const qa_net_client *, uint64_t now_ns, uint64_t timeout_ns);
bool qa_net_client_owns_seat(const qa_net_client *, qa_net_seat_id);
bool qa_net_client_id_equal(qa_net_client_id, qa_net_client_id);

typedef struct qa_net_rate {
    double clear_ns;
    uint32_t bytes_per_second, backup_bytes;
} qa_net_rate;
bool qa_net_rate_ready(const qa_net_rate *, uint64_t now_ns, bool paused);
void qa_net_rate_sent(qa_net_rate *, size_t bytes, uint64_t now_ns, bool paused);

#endif
