/* Port of src/network/unified/channel.ts. Deliveries are malloc'd
 * arrays of events (kind string mirrors TS); payloads malloc'd.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_UNIFIED_CHANNEL_H
#define QA_NETWORK_UNIFIED_CHANNEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_unified_delivery {
  const char *kind; /* "reliable"|"frame"|"invalid-frame"|"rejected-fragment" */
  uint8_t *payload; size_t length;
  int64_t sequence, required_reliable_sequence, required;
  const char *reason;
} qa_unified_delivery;
void qa_unified_deliveries_destroy(qa_unified_delivery *events, size_t count);
typedef struct qa_unified_receive_result {
  qa_unified_delivery *deliveries; size_t delivery_count;
  uint8_t **replies; size_t *reply_lens; size_t reply_count;
} qa_unified_receive_result;
void qa_unified_receive_result_destroy(qa_unified_receive_result *r);
struct qa_unified_assembly_key { int64_t sequence, fragments, fragment_bytes, total_bytes; };
struct qa_unified_assembly {
  struct qa_unified_assembly_key key;
  uint8_t **chunks; size_t *chunk_lens; int64_t count, size;
};
struct qa_unified_pending_frame { int64_t required; uint8_t *payload; size_t length; };
typedef struct qa_unified_channel {
  char token[33]; int64_t fragment_bytes, max_message_bytes; double retry_ms;
  int64_t next_packet, reliable_sequence, next_reliable;
  uint8_t *reliable; size_t reliable_len; bool has_reliable;
  double reliable_sent_at; bool reliable_sent;
  int64_t acknowledged_reliable, last_packet, delivered_reliable;
  double last_received_at; bool ack_sent, ack_dirty;
  struct qa_unified_assembly *reliable_assembly, *frame_assembly;
  struct qa_unified_pending_frame *frames; size_t frame_count, frame_cap;
  int64_t *frame_sequences; size_t seq_count, seq_cap;
} qa_unified_channel;
void qa_unified_channel_init(qa_unified_channel *c, const char *token, int64_t fragment_bytes,
                             int64_t max_message_bytes, double retry_ms, int64_t first_sequence, bool has_first);
void qa_unified_channel_destroy(qa_unified_channel *c);
bool qa_unified_channel_can_send_reliable(const qa_unified_channel *c);
void qa_unified_channel_queue_reliable(qa_unified_channel *c, const uint8_t *bytes, size_t length);
uint8_t **qa_unified_channel_transmit_reliable(qa_unified_channel *c, double now_ms, size_t **out_lens, size_t *out_count);
uint8_t *qa_unified_channel_transmit_frame(qa_unified_channel *c, const uint8_t *bytes, size_t length,
                                           bool has_required, int64_t required, size_t *out_len);
uint8_t **qa_unified_channel_receive(qa_unified_channel *c, const uint8_t *bytes, size_t length, double now_ms,
                                     qa_unified_receive_result *out);
#endif
