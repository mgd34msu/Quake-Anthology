/* Port of src/network/common/fragments.ts. Byte results are malloc'd,
 * caller frees. RangeError/Error -> qa_range_error. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_FRAGMENTS_H
#define QA_NETWORK_COMMON_FRAGMENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_message_fragment {
  long long sequence, offset;
  uint8_t *bytes; size_t length; bool final;
} qa_message_fragment;
typedef enum qa_fragment_result_kind {
  QA_FRAGMENT_PENDING, QA_FRAGMENT_COMPLETE, QA_FRAGMENT_REJECTED
} qa_fragment_result_kind;
typedef struct qa_fragment_result {
  qa_fragment_result_kind kind;
  size_t byte_length;          /* pending */
  long long sequence;          /* complete */
  uint8_t *bytes; size_t length; /* complete, caller frees */
  const char *reason;          /* rejected: "sequence"|"order"|"length" */
} qa_fragment_result;

typedef struct qa_fragment_sender {
  long long fragment_bytes, max_message_bytes;
  bool terminal_empty;
  uint8_t *bytes; size_t length, offset; long long sequence;
} qa_fragment_sender;
int qa_fragment_sender_init(qa_fragment_sender *s, long long fragment_bytes,
                            long long max_message_bytes, bool terminal_empty);
void qa_fragment_sender_destroy(qa_fragment_sender *s);
bool qa_fragment_sender_pending(const qa_fragment_sender *s);
void qa_fragment_sender_begin(qa_fragment_sender *s, long long sequence,
                              const uint8_t *bytes, size_t length);
/* Returns 1 and fills *out (bytes malloc'd), 0 if none pending. */
int qa_fragment_sender_next(qa_fragment_sender *s, qa_message_fragment *out);

typedef struct qa_fragment_receiver {
  long long max_message_bytes, current, accepted;
  size_t length; uint8_t *storage;
} qa_fragment_receiver;
int qa_fragment_receiver_init(qa_fragment_receiver *r, long long max_message_bytes);
void qa_fragment_receiver_destroy(qa_fragment_receiver *r);
qa_fragment_result qa_fragment_receiver_receive(qa_fragment_receiver *r,
                                                const qa_message_fragment *frag);
bool qa_fragment_receiver_accept_unfragmented(qa_fragment_receiver *r, long long sequence);
#endif
