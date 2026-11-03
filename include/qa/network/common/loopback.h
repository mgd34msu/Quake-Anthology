/* Port of src/network/common/loopback.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_LOOPBACK_H
#define QA_NETWORK_COMMON_LOOPBACK_H
#include "qa/network/common/transport.h"
typedef struct qa_loopback_hub qa_loopback_hub;
typedef struct qa_loopback_transport qa_loopback_transport;
struct qa_loopback_transport {
  qa_net_address address; qa_packet_queue queue; bool ended;
  qa_loopback_hub *hub; char *id;
};
struct qa_loopback_hub {
  qa_datagram_limits limits; qa_net_now_fn now; void *now_ctx;
  qa_loopback_transport **endpoints; size_t count, capacity;
  /* Closed endpoints leave `endpoints` (TS deletes them from the hub) but stay valid for
   * their holders until qa_loopback_hub_destroy frees them. */
  qa_loopback_transport **retired; size_t retired_count, retired_capacity;
};
int qa_loopback_hub_init(qa_loopback_hub *h, const qa_datagram_limits *limits, qa_net_now_fn now, void *now_ctx);
void qa_loopback_hub_destroy(qa_loopback_hub *h);
/* Returned transport is owned by the hub until closed/destroyed. */
qa_loopback_transport *qa_loopback_hub_bind(qa_loopback_hub *h, const char *id);
void qa_loopback_hub_close(qa_loopback_hub *h);
bool qa_loopback_transport_closed(const qa_loopback_transport *t);
void qa_loopback_transport_receive(qa_loopback_transport *t, const qa_net_address *from, const uint8_t *bytes, size_t length);
bool qa_loopback_transport_send(qa_loopback_transport *t, const qa_net_address *to, const uint8_t *payload, size_t length);
int qa_loopback_transport_poll(qa_loopback_transport *t, qa_receive_event *out);
long long qa_loopback_transport_subscribe(qa_loopback_transport *t, qa_net_listener_fn fn, void *ctx);
void qa_loopback_transport_unsubscribe(qa_loopback_transport *t, long long id);
void qa_loopback_transport_close(qa_loopback_transport *t);
qa_datagram_transport qa_loopback_transport_as_transport(qa_loopback_transport *t);
#endif
