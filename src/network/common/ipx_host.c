/* Port of src/network/common/ipx-host.ts. GPL-2.0-or-later. */
#include "qa/network/common/ipx_host.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
void qa_ipx_game_transport_init(qa_ipx_game_transport *t, qa_datagram_transport socket, const char *game) {
  t->socket = socket; t->is_quake1 = strcmp(game, "quake1") == 0; t->sequence = 0;
}
void qa_ipx_game_transport_destroy(qa_ipx_game_transport *t) { (void)t; }
int64_t qa_ipx_game_transport_max_datagram_bytes(qa_ipx_game_transport *t) {
  int64_t base = t->socket.max_datagram_bytes(t->socket.ctx);
  if (base < 0) base = 65507;
  return base - (t->is_quake1 ? 4 : 0);
}
bool qa_ipx_game_transport_send(qa_ipx_game_transport *t, const qa_net_address *to, const uint8_t *payload, size_t length) {
  if (!t->is_quake1) return t->socket.send(t->socket.ctx, to, payload, length);
  uint8_t *packet = malloc(length + 4);
  uint32_t seq = t->sequence++;
  packet[0] = (uint8_t)seq; packet[1] = (uint8_t)(seq >> 8); packet[2] = (uint8_t)(seq >> 16); packet[3] = (uint8_t)(seq >> 24);
  memcpy(packet + 4, payload, length);
  bool result = t->socket.send(t->socket.ctx, to, packet, length + 4);
  free(packet); return result;
}
int qa_ipx_game_transport_poll(qa_ipx_game_transport *t, qa_receive_event *out) {
  for (;;) {
    if (!t->socket.poll(t->socket.ctx, out)) return 0;
    if (!t->is_quake1 || out->kind != QA_RECV_PACKET) return 1;
    if (out->length >= 4) {
      memmove(out->payload, out->payload + 4, out->length - 4);
      out->length -= 4;
      return 1;
    }
    qa_receive_event_destroy(out);
  }
}
static int vt_addr(void *ctx, qa_net_address *out) {
  qa_ipx_game_transport *t = ctx; return t->socket.get_address(t->socket.ctx, out);
}
static bool vt_closed(void *ctx) { qa_ipx_game_transport *t = ctx; return t->socket.is_closed(t->socket.ctx); }
static int64_t vt_max(void *ctx) { return qa_ipx_game_transport_max_datagram_bytes(ctx); }
static bool vt_send(void *ctx, const qa_net_address *to, const uint8_t *p, size_t n) { return qa_ipx_game_transport_send(ctx, to, p, n); }
static int vt_poll(void *ctx, qa_receive_event *out) { return qa_ipx_game_transport_poll(ctx, out); }
static long long vt_sub(void *ctx, qa_net_listener_fn fn, void *c) {
  qa_ipx_game_transport *t = ctx; return t->socket.subscribe(t->socket.ctx, fn, c);
}
static void vt_unsub(void *ctx, long long id) {
  qa_ipx_game_transport *t = ctx; t->socket.unsubscribe(t->socket.ctx, id);
}
static void vt_close(void *ctx) { qa_ipx_game_transport *t = ctx; t->socket.close(t->socket.ctx); }
qa_datagram_transport qa_ipx_game_transport_as_transport(qa_ipx_game_transport *t) {
  qa_datagram_transport v; v.ctx = t; v.get_address = vt_addr; v.is_closed = vt_closed;
  v.max_datagram_bytes = vt_max; v.send = vt_send; v.poll = vt_poll;
  v.subscribe = vt_sub; v.unsubscribe = vt_unsub; v.close = vt_close; return v;
}
void qa_bind_ipx_transport(const qa_ipx_host *host, const char *game, double port, qa_ipx_game_transport *out) {
  (void)qa_net_port_number((int64_t)port, true);
  double packet_type = strcmp(game, "quake1") == 0 ? 4 : 0;
  if (host->is_dosbox) {
    qa_dosbox_ipx_socket *socket = qa_dosbox_ipx_network_bind(host->network, port, packet_type);
    qa_ipx_game_transport_init(out, qa_dosbox_ipx_socket_as_transport(socket), game);
    return;
  }
  if (!host->capability.available)
    qa_unsupported_transport_error("ipx-native", host->capability.reason);
  qa_datagram_transport inner;
  if (!host->capability.bind(host->capability.ctx, port, packet_type, &inner))
    qa_range_error("IPX socket is already bound or reserved");
  qa_ipx_game_transport_init(out, inner, game);
}
