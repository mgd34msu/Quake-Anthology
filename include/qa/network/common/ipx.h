/* Port of src/network/common/ipx.ts. IpxUdpTransport wraps a
 * qa_udp_transport by value (owned). GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_IPX_H
#define QA_NETWORK_COMMON_IPX_H
#include "qa/network/common/transport.h"
typedef struct qa_ipx_packet {
  qa_net_address from, to; int64_t packet_type, hops;
  uint8_t *payload; size_t length;
} qa_ipx_packet;
void qa_ipx_packet_destroy(qa_ipx_packet *p);
/* Malloc'd wire bytes, caller frees. Invalid packet aborts. */
uint8_t *qa_encode_ipx_packet(const qa_ipx_packet *packet, size_t *out_len);
/* Returns 1 and fills *out (payload malloc'd, addresses owned), else 0. */
int qa_decode_ipx_packet(const uint8_t *bytes, size_t length, qa_ipx_packet *out);

typedef enum qa_ipx_profile_kind { QA_IPX_PROFILE_QUAKE1, QA_IPX_PROFILE_DATAGRAM } qa_ipx_profile_kind;
typedef struct qa_ipx_profile { qa_ipx_profile_kind kind; int64_t packet_type; } qa_ipx_profile;
typedef struct qa_ipx_peer { qa_net_address ipx, udp; } qa_ipx_peer;
typedef struct qa_ipx_udp_transport {
  qa_net_address address; qa_udp_transport transport; qa_ipx_profile profile;
  qa_packet_queue queue; qa_ipx_peer *peers; size_t peer_count, peer_cap;
  bool ended; uint32_t sequence; long long subscription;
} qa_ipx_udp_transport;
/* Takes ownership of *udp (moved). */
int qa_ipx_udp_transport_init(qa_ipx_udp_transport *t, const qa_net_address *address,
                              qa_udp_transport *udp, qa_ipx_profile profile, qa_net_now_fn now, void *now_ctx);
void qa_ipx_udp_transport_destroy(qa_ipx_udp_transport *t);
bool qa_ipx_udp_transport_closed(const qa_ipx_udp_transport *t);
void qa_ipx_udp_transport_add_peer(qa_ipx_udp_transport *t, const qa_net_address *ipx, const qa_net_address *udp);
void qa_ipx_udp_transport_remove_peer(qa_ipx_udp_transport *t, const qa_net_address *ipx);
bool qa_ipx_udp_transport_send(qa_ipx_udp_transport *t, const qa_net_address *to, const uint8_t *payload, size_t length);
int qa_ipx_udp_transport_poll(qa_ipx_udp_transport *t, qa_receive_event *out);
long long qa_ipx_udp_transport_subscribe(qa_ipx_udp_transport *t, qa_net_listener_fn fn, void *ctx);
void qa_ipx_udp_transport_unsubscribe(qa_ipx_udp_transport *t, long long id);
void qa_ipx_udp_transport_close(qa_ipx_udp_transport *t);
qa_datagram_transport qa_ipx_udp_transport_as_transport(qa_ipx_udp_transport *t);
#endif
