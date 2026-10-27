/* Port of src/network/q3/demo.ts. GPL-2.0-or-later. */
#include "qa/network/q3/demo.h"
#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/message.h"
#include <stdlib.h>
#include <string.h>
struct qa_q3_demo_reader {
  const uint8_t *bytes; size_t len; char *source;
  size_t position; bool has_ended; qa_q3_demo_end_reason end_reason; int64_t end_offset;
};
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
bool qa_q3_demo_reader_init(qa_q3_demo_reader **out, const uint8_t *bytes, size_t len, const char *source) {
  qa_q3_demo_reader *r = calloc(1, sizeof *r);
  if (!r) return false;
  r->source = xstrdup(source ? source : "<demo>");
  if (!r->source) { free(r); return false; }
  r->bytes = bytes; r->len = len;
  *out = r; return true;
}
void qa_q3_demo_reader_destroy(qa_q3_demo_reader *r) { if (!r) return; free(r->source); free(r); }
int64_t qa_q3_demo_reader_offset(const qa_q3_demo_reader *r) { return (int64_t)r->position; }
const char *qa_q3_demo_reader_source(const qa_q3_demo_reader *r) { return r->source; }
static int32_t rd32s(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); /* LE host assumed like DataView LE on x86 */
  v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); return (int32_t)v; }
static void set_end(qa_q3_demo_reader *r, qa_q3_demo_end_reason reason, int64_t offset, qa_q3_demo_result *out) {
  r->has_ended = true; r->end_reason = reason; r->end_offset = offset;
  out->kind = QA_Q3_DEMO_END; out->reason = reason; out->offset = offset;
}
bool qa_q3_demo_reader_next(qa_q3_demo_reader *r, qa_q3_demo_sequence_fn on_sequence, void *ctx, qa_q3_demo_result *out) {
  memset(out, 0, sizeof *out);
  if (r->has_ended) { out->kind = QA_Q3_DEMO_END; out->reason = r->end_reason; out->offset = r->end_offset; return true; }
  size_t start = r->position;
  size_t remaining = r->len - start;
  if (remaining == 0) { set_end(r, QA_Q3_DEMO_EOF, (int64_t)start, out); return true; }
  if (remaining < 4) { r->position = r->len; set_end(r, QA_Q3_DEMO_TRUNCATED_HEADER, (int64_t)start, out); return true; }
  int32_t sequence = rd32s(r->bytes + start);
  r->position += 4;
  if (on_sequence) on_sequence(ctx, sequence);
  if (remaining < 8) { r->position = r->len; set_end(r, QA_Q3_DEMO_TRUNCATED_HEADER, (int64_t)start, out); return true; }
  int32_t length = rd32s(r->bytes + start + 4);
  r->position += 4;
  if (length == -1) { set_end(r, QA_Q3_DEMO_TERMINATOR, (int64_t)start, out); return true; }
  if (length < 0 || length > QA_Q3_MAX_MESSAGE_LENGTH)
    qa_binary_error(r->source, (int64_t)start + 4, "Invalid demo message length %d", length);
  if ((size_t)length > r->len - r->position) { r->position = r->len; set_end(r, QA_Q3_DEMO_TRUNCATED_PAYLOAD, (int64_t)start, out); return true; }
  out->payload = malloc(length ? (size_t)length : 1);
  if (!out->payload) return false;
  memcpy(out->payload, r->bytes + r->position, (size_t)length);
  out->payload_len = (size_t)length;
  r->position += (size_t)length;
  out->kind = QA_Q3_DEMO_MESSAGE; out->sequence = sequence;
  return true;
}
static void wr32(uint8_t *p, int32_t v) { uint32_t u = (uint32_t)v; p[0] = (uint8_t)u; p[1] = (uint8_t)(u >> 8); p[2] = (uint8_t)(u >> 16); p[3] = (uint8_t)(u >> 24); }
bool qa_q3_encode_demo(const qa_q3_demo_message *messages, size_t count, uint8_t **out, size_t *out_len) {
  size_t length = 8;
  for (size_t i = 0; i < count; i++) {
    if (messages[i].sequence < -((int64_t)0x80000000) || messages[i].sequence > 0x7fffffff)
      qa_range_error("Demo sequence must be int32");
    if (messages[i].payload_len > QA_Q3_MAX_MESSAGE_LENGTH) qa_range_error("Demo message exceeds MAX_MSGLEN");
    length += 8 + messages[i].payload_len;
  }
  *out = calloc(length, 1);
  if (!*out) return false;
  size_t offset = 0;
  for (size_t i = 0; i < count; i++) {
    wr32(*out + offset, (int32_t)messages[i].sequence);
    wr32(*out + offset + 4, (int32_t)messages[i].payload_len);
    memcpy(*out + offset + 8, messages[i].payload, messages[i].payload_len);
    offset += 8 + messages[i].payload_len;
  }
  wr32(*out + offset, -1); wr32(*out + offset + 4, -1);
  *out_len = length; return true;
}
bool qa_q3_encode_demo_message(const qa_q3_demo_message *message, uint8_t **out, size_t *out_len) {
  if (!qa_q3_encode_demo(message, 1, out, out_len)) return false;
  *out_len -= 8;
  return true;
}
bool qa_q3_finish_demo(uint8_t **out, size_t *out_len) {
  *out = malloc(8);
  if (!*out) return false;
  memset(*out, 255, 8); *out_len = 8; return true;
}
