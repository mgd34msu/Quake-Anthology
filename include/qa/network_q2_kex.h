#ifndef QA_NETWORK_Q2_KEX_H
#define QA_NETWORK_Q2_KEX_H
#include "qa/network_q2.h"
#define QA_KEX_DATAGRAM_BYTES 1400
#define QA_KEX_MESSAGE_BYTES (1024u*1024u)
#define QA_KEX_LAN_PORT 5069

typedef struct qa_kex_packet {
    uint8_t flags, kind;
    bool has_kind;
    uint16_t sequence, reliable;
    qa_bytes payload;
} qa_kex_packet;
bool qa_kex_packet_read(qa_bytes, qa_kex_packet *, qa_error *);
bool qa_kex_packet_write(qa_net_writer *, const qa_kex_packet *);
bool qa_kex_text_valid(qa_bytes);
uint64_t qa_kex_read_varint(qa_net_reader *);
bool qa_kex_write_varint(qa_net_writer *, uint64_t);
bool qa_kex_read_string(qa_net_reader *, char *, size_t);
bool qa_kex_write_string(qa_net_writer *, const char *);
typedef bool (*qa_kex_emit_fn)(void *, qa_bytes, qa_error *);
typedef struct qa_kex_channel qa_kex_channel;
typedef enum qa_kex_mode { QA_KEX_UNSEQUENCED=0, QA_KEX_SEQUENCED=2, QA_KEX_RELIABLE=3 } qa_kex_mode;
typedef struct qa_kex_message { uint8_t kind; qa_bytes payload; } qa_kex_message;
bool qa_kex_channel_create(qa_kex_emit_fn, void *, qa_kex_channel **, qa_error *);
void qa_kex_channel_destroy(qa_kex_channel *);
/* Reliable success means the complete message was accepted into the retry
 * queue. A failed initial emission is retried by tick without requeueing. */
bool qa_kex_channel_send(qa_kex_channel *, uint8_t kind, qa_bytes, qa_kex_mode, uint64_t now_ns, qa_error *);
/* Message payload remains borrowed until the next receive or channel destruction. */
bool qa_kex_channel_receive(qa_kex_channel *, qa_bytes, uint64_t now_ns, qa_kex_message *, bool *present, qa_error *);
bool qa_kex_channel_tick(qa_kex_channel *, uint64_t now_ns, qa_error *);

typedef struct qa_kex_attribute { char key[1024], value[4096]; } qa_kex_attribute;
typedef struct qa_kex_discovery {
    char name[1025];
    uint8_t players, max_players;
    qa_kex_attribute attributes[256];
    size_t attribute_count;
} qa_kex_discovery;
bool qa_kex_discovery_query(qa_net_writer *);
bool qa_kex_discovery_read(qa_bytes, qa_kex_discovery *, qa_error *);
bool qa_kex_discovery_write(qa_net_writer *, const qa_kex_discovery *);
/* DNS-SD codecs are independent of the multicast socket owned by discovery. */
typedef struct qa_kex_mdns_endpoint { char instance[256], target[256]; uint16_t port; } qa_kex_mdns_endpoint;
typedef struct qa_kex_mdns_address { char target[256]; qa_net_address address; } qa_kex_mdns_address;
typedef struct qa_kex_mdns_result {
    bool question;
    qa_kex_mdns_endpoint endpoints[256]; size_t endpoint_count;
    qa_kex_mdns_address addresses[256]; size_t address_count;
} qa_kex_mdns_result;
bool qa_kex_mdns_query(qa_net_writer *);
bool qa_kex_mdns_announce(qa_net_writer *, const char *hostname, uint16_t port, const qa_net_address *, size_t, uint32_t ttl);
bool qa_kex_mdns_read(qa_bytes, qa_kex_mdns_result *, qa_error *);

typedef struct qa_kex_lan qa_kex_lan;
typedef struct qa_kex_lan_options { bool host; uint8_t max_players, local_players; const char *name; qa_net_address server; } qa_kex_lan_options;
/* Takes ownership of transport only after successful construction. */
bool qa_kex_lan_open(qa_net_transport *, const qa_kex_lan_options *, qa_kex_lan **, qa_error *);
/* Wraps the admitted game stream in the shared transport API. The optional
 * control pointer is borrowed until the returned transport is closed. */
bool qa_kex_lan_transport_open(qa_net_transport *, const qa_kex_lan_options *, qa_net_transport **, qa_kex_lan **control, qa_error *);
void qa_kex_lan_close(qa_kex_lan *);
bool qa_kex_lan_tick(qa_kex_lan *, uint64_t now_ns, qa_error *);
bool qa_kex_lan_send(qa_kex_lan *, const qa_net_address *, qa_bytes, qa_error *);
bool qa_kex_lan_receive(qa_kex_lan *, qa_net_datagram *, qa_error *);
bool qa_kex_lan_admitted(const qa_kex_lan *, const qa_net_address *);
bool qa_kex_lan_ready(const qa_kex_lan *);
bool qa_kex_lan_set_attribute(qa_kex_lan *, const char *, const char *, qa_error *);
size_t qa_kex_lan_player_count(const qa_kex_lan *);
bool qa_kex_lan_player(const qa_kex_lan *, size_t, uint64_t *id, const qa_kex_attribute **, size_t *count);
#endif
