/* Port of src/network/common/reliability.ts. Payload results malloc'd,
 * caller frees. RangeError/Error -> qa_range_error. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_RELIABILITY_H
#define QA_NETWORK_COMMON_RELIABILITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_toggle_packet {
  long long sequence, acknowledged;
  bool reliable; int reliable_acknowledged;
  uint8_t *payload; size_t length;
} qa_toggle_packet;
typedef struct qa_toggle_receive_result {
  bool accepted; long long dropped;
  uint8_t *payload; size_t length;
  const char *reason;
} qa_toggle_receive_result;
typedef struct qa_toggle_channel {
  long long capacity;
  uint8_t *pending; size_t pending_len;
  uint8_t *reliable; size_t reliable_len;
  int reliable_sequence, incoming_reliable, incoming_reliable_ack;
  long long incoming_ack, last_reliable, incoming, outgoing;
} qa_toggle_channel;
int qa_toggle_channel_init(qa_toggle_channel *c, long long capacity, long long first_sequence);
void qa_toggle_channel_destroy(qa_toggle_channel *c);
long long qa_toggle_channel_incoming_sequence(const qa_toggle_channel *c);
long long qa_toggle_channel_outgoing_sequence(const qa_toggle_channel *c);
void qa_toggle_channel_advance_outgoing(qa_toggle_channel *c, long long sequence);
bool qa_toggle_channel_has_pending_reliable(const qa_toggle_channel *c);
void qa_toggle_channel_queue(qa_toggle_channel *c, const uint8_t *payload, size_t length);
/* payload_capacity < 0 means channel capacity. Fills *out (payload malloc'd). */
void qa_toggle_channel_transmit(qa_toggle_channel *c, const uint8_t *unreliable, size_t length,
                                long long payload_capacity, qa_toggle_packet *out);
qa_toggle_receive_result qa_toggle_channel_receive(qa_toggle_channel *c, const qa_toggle_packet *packet);

typedef struct qa_reliable_fragment {
  uint32_t sequence; bool final; uint8_t *payload; size_t length;
} qa_reliable_fragment;
typedef struct qa_reliable_fragment_result {
  const char *kind; /* "duplicate"|"fragment"|"message" */
  uint32_t acknowledge; uint8_t *payload; size_t length;
} qa_reliable_fragment_result;
typedef struct qa_stop_and_wait {
  long long max_message_bytes, fragment_bytes; double retry_ms;
  uint32_t outgoing, incoming;
  uint8_t *sending; size_t sending_len, offset; bool has_sending, sent; double sent_at;
  uint8_t *received; size_t received_len;
} qa_stop_and_wait;
int qa_stop_and_wait_init(qa_stop_and_wait *c, long long max_message_bytes,
                          long long fragment_bytes, double retry_ms);
void qa_stop_and_wait_destroy(qa_stop_and_wait *c);
bool qa_stop_and_wait_can_send(const qa_stop_and_wait *c);
void qa_stop_and_wait_begin(qa_stop_and_wait *c, const uint8_t *bytes, size_t length);
int qa_stop_and_wait_next(qa_stop_and_wait *c, double now, qa_reliable_fragment *out);
bool qa_stop_and_wait_acknowledge(qa_stop_and_wait *c, uint32_t sequence);
qa_reliable_fragment_result qa_stop_and_wait_receive(qa_stop_and_wait *c, const qa_reliable_fragment *frag);
#endif
