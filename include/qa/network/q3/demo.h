/* Port of src/network/q3/demo.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_DEMO_H
#define QA_NETWORK_Q3_DEMO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum qa_q3_demo_result_kind { QA_Q3_DEMO_MESSAGE, QA_Q3_DEMO_END } qa_q3_demo_result_kind;
typedef enum qa_q3_demo_end_reason { QA_Q3_DEMO_TERMINATOR, QA_Q3_DEMO_EOF, QA_Q3_DEMO_TRUNCATED_HEADER, QA_Q3_DEMO_TRUNCATED_PAYLOAD } qa_q3_demo_end_reason;
/* TS DemoMessage | DemoEnd. payload owned for message. */
typedef struct qa_q3_demo_result {
  qa_q3_demo_result_kind kind;
  int64_t sequence; uint8_t *payload; size_t payload_len;
  qa_q3_demo_end_reason reason; int64_t offset;
} qa_q3_demo_result;
typedef void (*qa_q3_demo_sequence_fn)(void *ctx, int64_t sequence);
typedef struct qa_q3_demo_reader qa_q3_demo_reader;
/* TS DemoMessageReader interface (fn+ctx form via qa_q3_demo_reader_next). */
bool qa_q3_demo_reader_init(qa_q3_demo_reader **out, const uint8_t *bytes, size_t len, const char *source);
void qa_q3_demo_reader_destroy(qa_q3_demo_reader *r);
int64_t qa_q3_demo_reader_offset(const qa_q3_demo_reader *r);
const char *qa_q3_demo_reader_source(const qa_q3_demo_reader *r);
bool qa_q3_demo_reader_next(qa_q3_demo_reader *r, qa_q3_demo_sequence_fn on_sequence, void *ctx, qa_q3_demo_result *out);
typedef struct qa_q3_demo_message { int64_t sequence; const uint8_t *payload; size_t payload_len; } qa_q3_demo_message;
bool qa_q3_encode_demo(const qa_q3_demo_message *messages, size_t count, uint8_t **out, size_t *out_len);
bool qa_q3_encode_demo_message(const qa_q3_demo_message *message, uint8_t **out, size_t *out_len);
bool qa_q3_finish_demo(uint8_t **out, size_t *out_len);
#endif
