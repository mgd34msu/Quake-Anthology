/* Port of src/network/unified/channel.ts: one established peer; control uses a bounded
 * ordered window and snapshots never block control. Deliveries and datagrams are malloc'd.
 *
 * Throws: the invalid-argument RangeErrors abort, per docs/PORTING.md. The two faults TS
 * raises while running and its callers catch (reliable assembly timeout, retry limit) close
 * the channel and set `failure` to the TS message; flush and receive then return nothing.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_UNIFIED_CHANNEL_H
#define QA_NETWORK_UNIFIED_CHANNEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_unified_channel_limits {
  int64_t datagram_bytes, message_bytes, queued_reliable_bytes, queued_reliable_messages;
  int64_t reliable_window_messages, fragments, packets_per_flush;
  int64_t retry_milliseconds, maximum_transmissions, assembly_milliseconds;
  /* TS inboundMessageBytes: the largest message a peer may announce. 0 stands for the TS
   * `limits.inboundMessageBytes ?? limits.messageBytes ?? defaults.inboundMessageBytes`
   * fallback and resolves to message_bytes at init, so the defaults (4 MiB) hold. */
  int64_t inbound_message_bytes;
} qa_unified_channel_limits;
/* TS `defaults`. */
qa_unified_channel_limits qa_unified_channel_default_limits(void);

typedef struct qa_unified_delivery {
  const char *kind; /* "reliable" | "frame" */
  uint8_t *payload; size_t length;
  int64_t sequence, required_reliable_sequence;
} qa_unified_delivery;
void qa_unified_deliveries_destroy(qa_unified_delivery *events, size_t count);

struct qa_unified_outgoing;
struct qa_unified_assembly;
struct qa_unified_ack { bool delivered; int64_t sequence, fragment; };

typedef struct qa_unified_channel {
  char token[33];
  qa_unified_channel_limits limits;
  bool ended;
  const char *failure; /* static text; set when a running fault closed the channel */
  int lane;
  int64_t next_reliable, next_frame, reliable_received, reliable_acknowledged;
  int64_t frame_received, newest_frame, queued_bytes, received_bytes;
  struct qa_unified_outgoing **reliable; size_t reliable_count, reliable_cap;
  size_t reliable_cursor;
  struct qa_unified_outgoing *frame, *pending_frame;
  struct qa_unified_assembly **assemblies; size_t assembly_count, assembly_cap;
  struct qa_unified_assembly *frame_assembly;
  qa_unified_delivery waiting_frame; bool has_waiting_frame;
  struct qa_unified_ack *acks; size_t ack_count, ack_cap;
} qa_unified_channel;

/* `limits` NULL means the defaults; start from qa_unified_channel_default_limits for a partial set. */
void qa_unified_channel_init(qa_unified_channel *c, const char *token, const qa_unified_channel_limits *limits);
void qa_unified_channel_destroy(qa_unified_channel *c);
void qa_unified_channel_close(qa_unified_channel *c);
int64_t qa_unified_channel_received_reliable_sequence(const qa_unified_channel *c);
int64_t qa_unified_channel_acknowledged_reliable_sequence(const qa_unified_channel *c);
bool qa_unified_channel_closed(const qa_unified_channel *c);
/* False where queue_reliable would throw: closed, queue overflow, or message over capacity. */
bool qa_unified_channel_can_queue_reliable(const qa_unified_channel *c, size_t length);
int64_t qa_unified_channel_queue_reliable(qa_unified_channel *c, const uint8_t *payload, size_t length);
void qa_unified_channel_queue_frame(qa_unified_channel *c, const uint8_t *payload, size_t length,
                                    double required_reliable_sequence);
/* Malloc'd array (NULL when empty). Check c->failure afterwards. */
qa_unified_delivery *qa_unified_channel_receive(qa_unified_channel *c, const uint8_t *datagram, size_t length,
                                                double now, size_t *out_count);
/* Malloc'd datagrams and lengths (NULL when empty). Check c->failure afterwards. */
uint8_t **qa_unified_channel_flush(qa_unified_channel *c, double now, size_t **out_lengths, size_t *out_count);
#endif
