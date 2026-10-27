/* Port of src/network/common/ipx-dosbox.ts. connect() is synchronous:
 * the registration wait pumps the UDP transport with a bounded poll
 * loop (now/clock via fn+ctx). GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_IPX_DOSBOX_H
#define QA_NETWORK_COMMON_IPX_DOSBOX_H
#include <stdbool.h>
#include "qa/network/common/ipx.h"
#include "qa/network/common/transport.h"
typedef struct qa_dosbox_ipx_network qa_dosbox_ipx_network;
typedef struct qa_dosbox_ipx_socket {
  qa_net_address address; qa_dosbox_ipx_network *network;
  double packet_type; qa_packet_queue queue; bool ended;
} qa_dosbox_ipx_socket;
struct qa_dosbox_ipx_network {
  qa_udp_transport udp; qa_net_address server, address;
  qa_net_now_fn now; void *now_ctx;
  qa_dosbox_ipx_socket **sockets; size_t socket_count, socket_cap;
  long long *ports;
  long long subscription; bool ended;
};
typedef struct qa_dosbox_connect_options {
  bool has_timeout; double timeout_ms, has_now; qa_net_now_fn now; void *now_ctx;
  const bool *aborted;
} qa_dosbox_connect_options;
/* Takes ownership of *udp (moved), including on failure (TS closes it). */
int qa_dosbox_ipx_network_connect(qa_dosbox_ipx_network *n, qa_udp_transport *udp,
                                  const qa_net_address *server, const qa_dosbox_connect_options *options);
void qa_dosbox_ipx_network_destroy(qa_dosbox_ipx_network *n);
bool qa_dosbox_ipx_network_closed(const qa_dosbox_ipx_network *n);
int qa_dosbox_ipx_network_get_server(const qa_dosbox_ipx_network *n, qa_net_address *out);
int qa_dosbox_ipx_network_get_address(const qa_dosbox_ipx_network *n, qa_net_address *out);
qa_dosbox_ipx_socket *qa_dosbox_ipx_network_bind(qa_dosbox_ipx_network *n, double requested_port, double packet_type);
bool qa_dosbox_ipx_network_send(qa_dosbox_ipx_network *n, const qa_ipx_packet *packet);
void qa_dosbox_ipx_network_receive(qa_dosbox_ipx_network *n);
void qa_dosbox_ipx_network_close(qa_dosbox_ipx_network *n);

qa_datagram_transport qa_dosbox_ipx_socket_as_transport(qa_dosbox_ipx_socket *s);
bool qa_dosbox_ipx_socket_closed(const qa_dosbox_ipx_socket *s);
int qa_dosbox_ipx_socket_get_address(const qa_dosbox_ipx_socket *s, qa_net_address *out);
bool qa_dosbox_ipx_socket_send(qa_dosbox_ipx_socket *s, const qa_net_address *to, const uint8_t *payload, size_t length);
int qa_dosbox_ipx_socket_poll(qa_dosbox_ipx_socket *s, qa_receive_event *out);
long long qa_dosbox_ipx_socket_subscribe(qa_dosbox_ipx_socket *s, qa_net_listener_fn fn, void *ctx);
void qa_dosbox_ipx_socket_unsubscribe(qa_dosbox_ipx_socket *s, long long id);
void qa_dosbox_ipx_socket_close(qa_dosbox_ipx_socket *s);
#endif
