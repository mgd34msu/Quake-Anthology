/*
 * Port of src/network/q2/kex/channel.ts (KexChannel). The emit
 * callback is fn + ctx; received payloads are malloc'd. Pending
 * reliable packets live in the landed foundation event ring.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_KEX_CHANNEL_H
#define QA_NETWORK_Q2_KEX_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/foundation/event_ring.h"

/* TS KexMessage (payload owned). */
typedef struct qa_kex_message {
  double kind;
  uint8_t *payload;
  size_t payload_length;
} qa_kex_message;

void qa_kex_message_free(qa_kex_message *message);

/* One pending reliable datagram (ring element). */
typedef struct qa_kex_pending_packet {
  double reliable;
  uint8_t *bytes; /* owned */
  size_t bytes_length;
} qa_kex_pending_packet;

typedef bool (*qa_kex_emit_fn)(void *ctx, const uint8_t *bytes, size_t length);

/* TS KexChannel. */
typedef struct qa_kex_channel {
  qa_kex_emit_fn emit;
  void *emit_ctx;
  int64_t sequence, reliable, incoming_sequence, incoming_reliable;
  qa_event_ring pending;
  qa_event_cursor pending_cursor;
  bool pending_ready;
  double pending_bytes;
  double retry_at;
  int64_t retries;
  double received_at;
  int64_t acknowledgment;
  bool has_fragments;
  double fragment_kind;
  int64_t fragment_sequence;
  uint8_t *fragment_bytes; /* owned */
  size_t fragment_length;
} qa_kex_channel;

/* False only on allocation failure. */
bool qa_kex_channel_init(qa_kex_channel *channel, qa_kex_emit_fn emit, void *ctx);
void qa_kex_channel_destroy(qa_kex_channel *channel);
/* TS send(kind, payload, mode 0|2|3, now) / receive / tick.
 * receive false is TS null (also false on OOM). */
bool qa_kex_channel_send(qa_kex_channel *channel, double kind, const uint8_t *payload,
                         size_t length, double mode, double now);
bool qa_kex_channel_receive(qa_kex_channel *channel, const uint8_t *bytes, size_t length,
                            double now, qa_kex_message *out);
/* False when the TS tick would throw 'KEX LAN peer timed out'
 * (callers that catch tick errors remove the peer); other protocol
 * errors still abort. */
bool qa_kex_channel_tick(qa_kex_channel *channel, double now);

#endif
