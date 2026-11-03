/* Port of src/network/common/transport.ts. DatagramTransport<T> becomes
 * a fn+ctx vtable over qa_net_address. UdpTransport: Bun socket -> POSIX
 * nonblocking UDP pumped in poll() (PORTING platform rule). Error events carry
 * a malloc'd message string. Throws go through qa_raise with the TS class name.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_TRANSPORT_H
#define QA_NETWORK_COMMON_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/common/endpoint.h"
#include "qa/network/common/socks.h"
typedef struct qa_datagram_limits { int64_t max_bytes, queue_packets; } qa_datagram_limits;
extern const qa_datagram_limits QA_Q3_DATAGRAM_LIMITS_NET;
extern const qa_datagram_limits QA_Q2_DATAGRAM_LIMITS_NET;
extern const qa_datagram_limits QA_UNIFIED_DATAGRAM_LIMITS_NET;

typedef enum qa_receive_event_kind { QA_RECV_PACKET, QA_RECV_ERROR, QA_RECV_DROPPED } qa_receive_event_kind;
typedef struct qa_receive_event {
  qa_receive_event_kind kind;
  qa_net_address from;         /* owned; destroy after use */
  uint8_t *payload; size_t length; double received_at;
  char *error;                 /* owned message for QA_RECV_ERROR */
  const char *reason;          /* "oversize"|"overflow" for dropped */
} qa_receive_event;
void qa_receive_event_destroy(qa_receive_event *e);

typedef double (*qa_net_now_fn)(void *ctx);
typedef void (*qa_net_listener_fn)(void *ctx);
struct qa_net_listener { long long id; qa_net_listener_fn fn; void *ctx; };
typedef struct qa_packet_queue {
  qa_datagram_limits limits; qa_net_now_fn now; void *now_ctx;
  qa_receive_event *events; size_t head, count, capacity;
  struct qa_net_listener *listeners; size_t listener_count, listener_cap; long long next_id;
  bool ended; long long dropped;
} qa_packet_queue;
int qa_packet_queue_init(qa_packet_queue *q, qa_datagram_limits limits, qa_net_now_fn now, void *now_ctx);
void qa_packet_queue_destroy(qa_packet_queue *q);
qa_datagram_limits qa_packet_queue_get_limits(const qa_packet_queue *q);
long long qa_packet_queue_dropped(const qa_packet_queue *q);
void qa_packet_queue_accept(qa_packet_queue *q, const qa_net_address *from,
                            const uint8_t *payload, size_t length, bool truncated);
void qa_packet_queue_push_error(qa_packet_queue *q, const char *message);
void qa_packet_queue_push_event(qa_packet_queue *q, qa_receive_event *event);
int qa_packet_queue_poll(qa_packet_queue *q, qa_receive_event *out);
/* Returns listener id >=0; unsubscribe with that id. */
long long qa_packet_queue_subscribe(qa_packet_queue *q, qa_net_listener_fn fn, void *ctx);
void qa_packet_queue_unsubscribe(qa_packet_queue *q, long long id);
void qa_packet_queue_close(qa_packet_queue *q);
bool qa_packet_queue_closed(const qa_packet_queue *q);
double qa_net_default_now(void *ctx);

typedef struct qa_datagram_transport qa_datagram_transport;
struct qa_datagram_transport {
  void *ctx;
  int (*get_address)(void *ctx, qa_net_address *out);
  bool (*is_closed)(void *ctx);
  int64_t (*max_datagram_bytes)(void *ctx); /* -1 = unspecified */
  bool (*send)(void *ctx, const qa_net_address *to, const uint8_t *payload, size_t length);
  int (*poll)(void *ctx, qa_receive_event *out);
  long long (*subscribe)(void *ctx, qa_net_listener_fn fn, void *listener_ctx);
  void (*unsubscribe)(void *ctx, long long id);
  void (*close)(void *ctx);
};

typedef struct qa_udp_bind_options {
  const char *host; int64_t port;
  const qa_datagram_limits *limits; qa_net_now_fn now; void *now_ctx; bool broadcast;
} qa_udp_bind_options;
typedef struct qa_udp_transport {
  int fd; bool ended, pumping; qa_packet_queue queue; qa_net_address address;
  bool has_socks; qa_socks_association socks;
} qa_udp_transport;
int qa_udp_transport_bind(qa_udp_transport *t, const qa_udp_bind_options *opts);
void qa_udp_transport_destroy(qa_udp_transport *t);
qa_datagram_transport qa_udp_transport_as_transport(qa_udp_transport *t);
bool qa_udp_transport_closed(const qa_udp_transport *t);
long long qa_udp_transport_dropped_packets(const qa_udp_transport *t);
int qa_udp_transport_get_address(const qa_udp_transport *t, qa_net_address *out);
void qa_udp_transport_connect_socks(qa_udp_transport *t, const qa_socks_options *opts);
bool qa_udp_transport_send(qa_udp_transport *t, const qa_net_address *to, const uint8_t *payload, size_t length);
int qa_udp_transport_poll(qa_udp_transport *t, qa_receive_event *out);
long long qa_udp_transport_subscribe(qa_udp_transport *t, qa_net_listener_fn fn, void *ctx);
void qa_udp_transport_unsubscribe(qa_udp_transport *t, long long id);
void qa_udp_transport_close(qa_udp_transport *t);

/* TS Pick<UdpTransport, "connectSocks">. */
typedef struct qa_socks_connector {
  void *ctx;
  void (*connect_socks)(void *ctx, const qa_socks_options *options);
} qa_socks_connector;
qa_socks_connector qa_udp_transport_as_socks_connector(qa_udp_transport *t);

/* UnsupportedTransportError: raises name "UnsupportedTransportError", "<transport>: <detail>". */
void qa_unsupported_transport_error(const char *transport, const char *detail);
#endif
