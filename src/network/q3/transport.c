/* Port of src/network/q3/transport.ts. GPL-2.0-or-later. */
#include "qa/network/q3/transport.h"
#include <string.h>
static void delivery_send(void *ctx, const uint8_t *packet, size_t len) {
  qa_q3_channel_delivery_storage *s = ctx;
  qa_q3_address remote; memset(&remote, 0, sizeof remote);
  s->remote(s->remote_ctx, &remote);
  s->transport_send(s->transport_ctx, &remote, packet, len);
  qa_q3_mirror_address_destroy(&remote);
}
static void delivery_trace(void *ctx, const char *text) {
  qa_q3_channel_delivery_storage *s = ctx;
  if (s->trace) s->trace(s->trace_ctx, text);
}
const qa_q3_channel_delivery *qa_q3_channel_delivery_create(qa_q3_channel_delivery_storage *storage,
                                                            qa_q3_mirror_datagram_send_fn transport_send, void *transport_ctx,
                                                            qa_q3_remote_address_fn remote, void *remote_ctx,
                                                            qa_q3_source_message_state *source_state,
                                                            qa_q3_message_print_fn trace, void *trace_ctx) {
  memset(storage, 0, sizeof *storage);
  storage->transport_send = transport_send; storage->transport_ctx = transport_ctx;
  storage->remote = remote; storage->remote_ctx = remote_ctx;
  storage->trace = trace; storage->trace_ctx = trace_ctx;
  storage->delivery.source_state = source_state;
  storage->delivery.send = delivery_send; storage->delivery.send_ctx = storage;
  storage->delivery.trace = delivery_trace; storage->delivery.trace_ctx = storage;
  return &storage->delivery;
}
