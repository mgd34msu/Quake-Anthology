/* Port of src/network/common/loopback.ts. GPL-2.0-or-later. */
#include "qa/network/common/loopback.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
int qa_loopback_hub_init(qa_loopback_hub *h, const qa_datagram_limits *limits, qa_net_now_fn now, void *now_ctx) {
  memset(h, 0, sizeof *h);
  h->limits = limits ? *limits : QA_UNIFIED_DATAGRAM_LIMITS_NET;
  h->now = now; h->now_ctx = now_ctx; return 1;
}
static void transport_free(qa_loopback_transport *t) {
  qa_packet_queue_destroy(&t->queue); qa_net_address_destroy(&t->address); free(t->id); free(t);
}
void qa_loopback_hub_destroy(qa_loopback_hub *h) {
  qa_loopback_hub_close(h);
  for (size_t i = 0; i < h->retired_count; i++) transport_free(h->retired[i]);
  free(h->retired); free(h->endpoints); memset(h, 0, sizeof *h);
}
qa_loopback_transport *qa_loopback_hub_bind(qa_loopback_hub *h, const char *id) {
  if (!id[0]) qa_raise("Error", 0, 0, "Loopback endpoint must have a unique nonempty name");
  for (size_t i = 0; i < h->count; i++) if (strcmp(h->endpoints[i]->id, id) == 0)
    qa_raise("Error", 0, 0, "Loopback endpoint must have a unique nonempty name");
  qa_loopback_transport *t = calloc(1, sizeof *t); if (!t) return NULL;
  (void)qa_net_make_loopback(&t->address, id);
  (void)qa_packet_queue_init(&t->queue, h->limits, h->now, h->now_ctx);
  t->hub = h; t->id = dupstr(id);
  if (h->count == h->capacity) {
    size_t nc = h->capacity ? h->capacity * 2 : 4;
    h->endpoints = realloc(h->endpoints, nc * sizeof *h->endpoints); h->capacity = nc;
  }
  h->endpoints[h->count++] = t; return t;
}
void qa_loopback_hub_close(qa_loopback_hub *h) {
  while (h->count > 0) {
    qa_loopback_transport *t = h->endpoints[0];
    qa_loopback_transport_close(t);
  }
}
bool qa_loopback_transport_closed(const qa_loopback_transport *t) { return t->ended; }
static void opened(const qa_loopback_transport *t) { if (t->ended) qa_raise("Error", 0, 0, "Loopback transport is closed"); }
void qa_loopback_transport_receive(qa_loopback_transport *t, const qa_net_address *from, const uint8_t *bytes, size_t length) {
  opened(t); qa_packet_queue_accept(&t->queue, from, bytes, length, false);
}
bool qa_loopback_transport_send(qa_loopback_transport *t, const qa_net_address *to, const uint8_t *payload, size_t length) {
  opened(t);
  if ((long long)length > qa_packet_queue_get_limits(&t->queue).max_bytes)
    qa_range_error("Loopback message exceeds selected limit");
  if (to->kind != QA_NET_LOOPBACK || !to->host) return false;
  for (size_t i = 0; i < t->hub->count; i++) {
    qa_loopback_transport *peer = t->hub->endpoints[i];
    if (strcmp(peer->id, to->host) == 0) { qa_loopback_transport_receive(peer, &t->address, payload, length); return true; }
  }
  return false;
}
int qa_loopback_transport_poll(qa_loopback_transport *t, qa_receive_event *out) { opened(t); return qa_packet_queue_poll(&t->queue, out); }
long long qa_loopback_transport_subscribe(qa_loopback_transport *t, qa_net_listener_fn fn, void *ctx) {
  opened(t); return qa_packet_queue_subscribe(&t->queue, fn, ctx);
}
void qa_loopback_transport_unsubscribe(qa_loopback_transport *t, long long id) { qa_packet_queue_unsubscribe(&t->queue, id); }
void qa_loopback_transport_close(qa_loopback_transport *t) {
  if (t->ended) return;
  t->ended = true;
  qa_loopback_hub *h = t->hub;
  for (size_t i = 0; i < h->count; i++) if (h->endpoints[i] == t) {
    memmove(&h->endpoints[i], &h->endpoints[i + 1], (h->count - i - 1) * sizeof *h->endpoints);
    h->count--; break;
  }
  if (h->retired_count == h->retired_capacity) {
    size_t nc = h->retired_capacity ? h->retired_capacity * 2 : 4;
    qa_loopback_transport **grown = realloc(h->retired, nc * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    h->retired = grown; h->retired_capacity = nc;
  }
  h->retired[h->retired_count++] = t;
  qa_packet_queue_close(&t->queue);
}
static int vt_addr(void *ctx, qa_net_address *out) { return qa_net_address_clone(&((qa_loopback_transport *)ctx)->address, out); }
static bool vt_closed(void *ctx) { return qa_loopback_transport_closed(ctx); }
static int64_t vt_max(void *ctx) { (void)ctx; return -1; }
static bool vt_send(void *ctx, const qa_net_address *to, const uint8_t *p, size_t n) { return qa_loopback_transport_send(ctx, to, p, n); }
static int vt_poll(void *ctx, qa_receive_event *out) { return qa_loopback_transport_poll(ctx, out); }
static long long vt_sub(void *ctx, qa_net_listener_fn fn, void *c) { return qa_loopback_transport_subscribe(ctx, fn, c); }
static void vt_unsub(void *ctx, long long id) { qa_loopback_transport_unsubscribe(ctx, id); }
static void vt_close(void *ctx) { qa_loopback_transport_close(ctx); }
qa_datagram_transport qa_loopback_transport_as_transport(qa_loopback_transport *t) {
  qa_datagram_transport v; v.ctx = t; v.get_address = vt_addr; v.is_closed = vt_closed;
  v.max_datagram_bytes = vt_max; v.send = vt_send; v.poll = vt_poll;
  v.subscribe = vt_sub; v.unsubscribe = vt_unsub; v.close = vt_close; return v;
}
