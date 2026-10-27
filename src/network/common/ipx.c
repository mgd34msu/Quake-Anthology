/* Port of src/network/common/ipx.ts. GPL-2.0-or-later. */
#include "qa/network/common/ipx.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
static void put_u32be(uint8_t *b, size_t off, uint32_t v) { b[off] = (uint8_t)(v >> 24); b[off+1] = (uint8_t)(v >> 16); b[off+2] = (uint8_t)(v >> 8); b[off+3] = (uint8_t)v; }
static void put_u16be(uint8_t *b, size_t off, uint16_t v) { b[off] = (uint8_t)(v >> 8); b[off+1] = (uint8_t)v; }
static uint32_t get_u32be(const uint8_t *b, size_t off) { return ((uint32_t)b[off] << 24) | ((uint32_t)b[off+1] << 16) | ((uint32_t)b[off+2] << 8) | b[off+3]; }
static uint16_t get_u16be(const uint8_t *b, size_t off) { return (uint16_t)(((uint16_t)b[off] << 8) | b[off+1]); }
static void write_addr(uint8_t *b, size_t off, const qa_net_address *a) {
  put_u32be(b, off, a->network); memcpy(b + off + 4, a->node, 6); put_u16be(b, off + 10, (uint16_t)a->port);
}
static void read_addr(const uint8_t *b, size_t off, qa_net_address *out) {
  int64_t node[6]; for (int i = 0; i < 6; i++) node[i] = b[off + 4 + (size_t)i];
  (void)qa_net_make_ipx(out, (int64_t)get_u32be(b, off), node, (int64_t)get_u16be(b, off + 10));
}
void qa_ipx_packet_destroy(qa_ipx_packet *p) {
  qa_net_address_destroy(&p->from); qa_net_address_destroy(&p->to); free(p->payload); p->payload = NULL;
}
uint8_t *qa_encode_ipx_packet(const qa_ipx_packet *packet, size_t *out_len) {
  if (packet->length > 65505 || packet->packet_type < 0 || packet->packet_type > 255 || packet->hops < 0 || packet->hops > 255)
    qa_range_error("Invalid IPX packet");
  size_t n = packet->length + 30;
  uint8_t *b = malloc(n); if (!b) return NULL;
  put_u16be(b, 0, 65535); put_u16be(b, 2, (uint16_t)n);
  b[4] = (uint8_t)packet->hops; b[5] = (uint8_t)packet->packet_type;
  write_addr(b, 6, &packet->to); write_addr(b, 18, &packet->from);
  memcpy(b + 30, packet->payload, packet->length); *out_len = n; return b;
}
int qa_decode_ipx_packet(const uint8_t *bytes, size_t length, qa_ipx_packet *out) {
  if (length < 30) return 0;
  if (get_u16be(bytes, 0) != 65535 || (size_t)get_u16be(bytes, 2) != length || get_u16be(bytes, 16) == 0 || get_u16be(bytes, 28) == 0) return 0;
  memset(out, 0, sizeof *out);
  read_addr(bytes, 18, &out->from); read_addr(bytes, 6, &out->to);
  out->packet_type = bytes[5]; out->hops = bytes[4]; out->length = length - 30;
  out->payload = malloc(out->length ? out->length : 1);
  if (out->length) memcpy(out->payload, bytes + 30, out->length);
  return 1;
}
static bool node_broadcast(const qa_net_address *a) { for (int i = 0; i < 6; i++) if (a->node[i] != 255) return false; return true; }
static void ipx_receive_packets(qa_ipx_udp_transport *t) {
  if (t->ended) return;
  for (;;) {
    qa_receive_event e;
    if (!qa_udp_transport_poll(&t->transport, &e)) return;
    if (e.kind == QA_RECV_ERROR) { qa_packet_queue_push_event(&t->queue, &e); continue; }
    if (e.kind != QA_RECV_PACKET) { qa_receive_event_destroy(&e); continue; }
    qa_ipx_packet pkt;
    if (qa_decode_ipx_packet(e.payload, e.length, &pkt)) {
      bool known = false;
      for (size_t i = 0; i < t->peer_count; i++)
        if (qa_net_same_address(&t->peers[i].ipx, &pkt.from, false) && qa_net_same_address(&t->peers[i].udp, &e.from, true)) known = true;
      bool broadcast = node_broadcast(&pkt.to) && (pkt.to.network == 0 || pkt.to.network == t->address.network);
      int64_t expected = t->profile.kind == QA_IPX_PROFILE_QUAKE1 ? 4 : t->profile.packet_type;
      if (known && pkt.to.port == t->address.port && (broadcast || qa_net_same_address(&pkt.to, &t->address, true)) && pkt.packet_type == expected) {
        if (t->profile.kind == QA_IPX_PROFILE_QUAKE1) {
          if (pkt.length >= 4) qa_packet_queue_accept(&t->queue, &pkt.from, pkt.payload + 4, pkt.length - 4, false);
        } else qa_packet_queue_accept(&t->queue, &pkt.from, pkt.payload, pkt.length, false);
      }
      qa_ipx_packet_destroy(&pkt);
    }
    qa_receive_event_destroy(&e);
  }
}
static void ipx_readable(void *ctx) { ipx_receive_packets(ctx); }
int qa_ipx_udp_transport_init(qa_ipx_udp_transport *t, const qa_net_address *address,
                              qa_udp_transport *udp, qa_ipx_profile profile, qa_net_now_fn now, void *now_ctx) {
  memset(t, 0, sizeof *t);
  (void)qa_net_address_clone(address, &t->address);
  t->transport = *udp; memset(udp, 0, sizeof *udp); udp->fd = -1;
  t->profile = profile;
  qa_datagram_limits lim = { QA_UNIFIED_DATAGRAM_LIMITS_NET.max_bytes - 34, 256 };
  if (!qa_packet_queue_init(&t->queue, lim, now, now_ctx)) return 0;
  t->subscription = qa_udp_transport_subscribe(&t->transport, ipx_readable, t);
  return 1;
}
void qa_ipx_udp_transport_destroy(qa_ipx_udp_transport *t) {
  qa_ipx_udp_transport_close(t);
  for (size_t i = 0; i < t->peer_count; i++) { qa_net_address_destroy(&t->peers[i].ipx); qa_net_address_destroy(&t->peers[i].udp); }
  free(t->peers); qa_packet_queue_destroy(&t->queue); qa_net_address_destroy(&t->address);
  qa_udp_transport_destroy(&t->transport);
}
bool qa_ipx_udp_transport_closed(const qa_ipx_udp_transport *t) { return t->ended; }
void qa_ipx_udp_transport_add_peer(qa_ipx_udp_transport *t, const qa_net_address *ipx, const qa_net_address *udp) {
  if (t->ended) qa_range_error("IPX tunnel is closed");
  for (size_t i = 0; i < t->peer_count; i++) if (qa_net_same_address(&t->peers[i].ipx, ipx, false)) {
    qa_net_address_destroy(&t->peers[i].ipx); qa_net_address_destroy(&t->peers[i].udp);
    (void)qa_net_address_clone(ipx, &t->peers[i].ipx); (void)qa_net_address_clone(udp, &t->peers[i].udp); return;
  }
  if (t->peer_count == t->peer_cap) { size_t nc = t->peer_cap ? t->peer_cap * 2 : 4; t->peers = realloc(t->peers, nc * sizeof *t->peers); t->peer_cap = nc; }
  (void)qa_net_address_clone(ipx, &t->peers[t->peer_count].ipx);
  (void)qa_net_address_clone(udp, &t->peers[t->peer_count].udp); t->peer_count++;
}
void qa_ipx_udp_transport_remove_peer(qa_ipx_udp_transport *t, const qa_net_address *ipx) {
  for (size_t i = 0; i < t->peer_count; i++) if (qa_net_same_address(&t->peers[i].ipx, ipx, false)) {
    qa_net_address_destroy(&t->peers[i].ipx); qa_net_address_destroy(&t->peers[i].udp);
    memmove(&t->peers[i], &t->peers[i + 1], (t->peer_count - i - 1) * sizeof *t->peers); t->peer_count--; return;
  }
}
bool qa_ipx_udp_transport_send(qa_ipx_udp_transport *t, const qa_net_address *to, const uint8_t *payload, size_t length) {
  if (t->ended) qa_range_error("IPX tunnel is closed");
  if ((long long)length > qa_packet_queue_get_limits(&t->queue).max_bytes) qa_range_error("IPX payload exceeds tunnel capacity");
  uint8_t *owned = NULL; const uint8_t *bytes = payload; size_t blen = length;
  if (t->profile.kind == QA_IPX_PROFILE_QUAKE1) {
    owned = malloc(length + 4); uint32_t seq = t->sequence++;
    owned[0] = (uint8_t)seq; owned[1] = (uint8_t)(seq >> 8); owned[2] = (uint8_t)(seq >> 16); owned[3] = (uint8_t)(seq >> 24);
    memcpy(owned + 4, payload, length); bytes = owned; blen = length + 4;
  }
  qa_ipx_packet pkt; memset(&pkt, 0, sizeof pkt);
  (void)qa_net_address_clone(&t->address, &pkt.from); (void)qa_net_address_clone(to, &pkt.to);
  pkt.packet_type = t->profile.kind == QA_IPX_PROFILE_QUAKE1 ? 4 : t->profile.packet_type;
  pkt.hops = 0; pkt.payload = (uint8_t *)bytes; pkt.length = blen;
  size_t wire_len = 0; uint8_t *wire = qa_encode_ipx_packet(&pkt, &wire_len);
  pkt.payload = NULL; qa_ipx_packet_destroy(&pkt);
  bool result;
  if (node_broadcast(to)) {
    result = false;
    for (size_t i = 0; i < t->peer_count; i++)
      if (to->network == 0 || to->network == t->peers[i].ipx.network)
        result = qa_udp_transport_send(&t->transport, &t->peers[i].udp, wire, wire_len) || result;
  } else {
    result = false;
    for (size_t i = 0; i < t->peer_count; i++)
      if (qa_net_same_address(&t->peers[i].ipx, to, false)) { result = qa_udp_transport_send(&t->transport, &t->peers[i].udp, wire, wire_len); break; }
  }
  free(wire); free(owned); return result;
}
int qa_ipx_udp_transport_poll(qa_ipx_udp_transport *t, qa_receive_event *out) {
  if (t->ended) qa_range_error("IPX tunnel is closed");
  ipx_receive_packets(t); return qa_packet_queue_poll(&t->queue, out);
}
long long qa_ipx_udp_transport_subscribe(qa_ipx_udp_transport *t, qa_net_listener_fn fn, void *ctx) {
  return qa_packet_queue_subscribe(&t->queue, fn, ctx);
}
void qa_ipx_udp_transport_unsubscribe(qa_ipx_udp_transport *t, long long id) { qa_packet_queue_unsubscribe(&t->queue, id); }
void qa_ipx_udp_transport_close(qa_ipx_udp_transport *t) {
  if (t->ended) return;
  t->ended = true;
  qa_udp_transport_unsubscribe(&t->transport, t->subscription);
  qa_packet_queue_close(&t->queue);
  qa_udp_transport_close(&t->transport);
}
static int vt_addr(void *ctx, qa_net_address *out) { return qa_net_address_clone(&((qa_ipx_udp_transport *)ctx)->address, out); }
static bool vt_closed(void *ctx) { return qa_ipx_udp_transport_closed(ctx); }
static int64_t vt_max(void *ctx) { (void)ctx; return -1; }
static bool vt_send(void *ctx, const qa_net_address *to, const uint8_t *p, size_t n) { return qa_ipx_udp_transport_send(ctx, to, p, n); }
static int vt_poll(void *ctx, qa_receive_event *out) { return qa_ipx_udp_transport_poll(ctx, out); }
static long long vt_sub(void *ctx, qa_net_listener_fn fn, void *c) { return qa_ipx_udp_transport_subscribe(ctx, fn, c); }
static void vt_unsub(void *ctx, long long id) { qa_ipx_udp_transport_unsubscribe(ctx, id); }
static void vt_close(void *ctx) { qa_ipx_udp_transport_close(ctx); }
qa_datagram_transport qa_ipx_udp_transport_as_transport(qa_ipx_udp_transport *t) {
  qa_datagram_transport v; v.ctx = t; v.get_address = vt_addr; v.is_closed = vt_closed;
  v.max_datagram_bytes = vt_max; v.send = vt_send; v.poll = vt_poll;
  v.subscribe = vt_sub; v.unsubscribe = vt_unsub; v.close = vt_close; return v;
}
