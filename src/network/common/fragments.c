/* Port of src/network/common/fragments.ts. GPL-2.0-or-later. */
#include "qa/network/common/fragments.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>

static int is_safe(long long v) { return v >= -QA_MAX_SAFE_INTEGER && v <= QA_MAX_SAFE_INTEGER; }
int qa_fragment_sender_init(qa_fragment_sender *s, long long fb, long long mm, bool te) {
  if (!is_safe(fb) || fb <= 0 || !is_safe(mm) || mm < fb)
    qa_range_error("Invalid fragment sizes");
  memset(s, 0, sizeof *s); s->fragment_bytes = fb; s->max_message_bytes = mm; s->terminal_empty = te;
  return 1;
}
void qa_fragment_sender_destroy(qa_fragment_sender *s) { free(s->bytes); memset(s, 0, sizeof *s); }
bool qa_fragment_sender_pending(const qa_fragment_sender *s) { return s->bytes != NULL; }
void qa_fragment_sender_begin(qa_fragment_sender *s, long long seq, const uint8_t *bytes, size_t length) {
  if (s->bytes) qa_range_error("Fragmented message is still pending");
  if ((long long)length > s->max_message_bytes) qa_range_error("Message exceeds fragment capacity");
  s->sequence = seq; s->offset = 0; s->length = length;
  s->bytes = malloc(length ? length : 1);
  if (length) memcpy(s->bytes, bytes, length);
}
int qa_fragment_sender_next(qa_fragment_sender *s, qa_message_fragment *out) {
  if (!s->bytes) return 0;
  size_t offset = s->offset;
  size_t end = offset + (size_t)s->fragment_bytes;
  if (end > s->length) end = s->length;
  size_t n = end - offset;
  out->sequence = s->sequence; out->offset = (long long)offset; out->length = n;
  out->bytes = malloc(n ? n : 1);
  if (n) memcpy(out->bytes, s->bytes + offset, n);
  s->offset = end;
  out->final = end == s->length && (!s->terminal_empty || (long long)n < s->fragment_bytes);
  if (out->final) { free(s->bytes); s->bytes = NULL; }
  return 1;
}

int qa_fragment_receiver_init(qa_fragment_receiver *r, long long mm) {
  if (!is_safe(mm) || mm <= 0) qa_range_error("Invalid fragment receive capacity");
  memset(r, 0, sizeof *r); r->max_message_bytes = mm; r->current = -1; r->accepted = 0;
  r->storage = malloc((size_t)mm);
  return r->storage != NULL;
}
void qa_fragment_receiver_destroy(qa_fragment_receiver *r) { free(r->storage); memset(r, 0, sizeof *r); }
qa_fragment_result qa_fragment_receiver_receive(qa_fragment_receiver *r, const qa_message_fragment *f) {
  qa_fragment_result out; memset(&out, 0, sizeof out);
  if (f->sequence <= r->accepted) { out.kind = QA_FRAGMENT_REJECTED; out.reason = "sequence"; return out; }
  if (f->sequence != r->current) { r->current = f->sequence; r->length = 0; }
  if (f->offset != (long long)r->length) { out.kind = QA_FRAGMENT_REJECTED; out.reason = "order"; return out; }
  if ((long long)(r->length + f->length) > r->max_message_bytes) {
    out.kind = QA_FRAGMENT_REJECTED; out.reason = "length"; return out;
  }
  memcpy(r->storage + r->length, f->bytes, f->length); r->length += f->length;
  if (!f->final) { out.kind = QA_FRAGMENT_PENDING; out.byte_length = r->length; return out; }
  out.kind = QA_FRAGMENT_COMPLETE; out.sequence = f->sequence; out.length = r->length;
  out.bytes = malloc(r->length ? r->length : 1);
  if (r->length) memcpy(out.bytes, r->storage, r->length);
  r->length = 0; r->accepted = f->sequence;
  return out;
}
bool qa_fragment_receiver_accept_unfragmented(qa_fragment_receiver *r, long long seq) {
  if (seq <= r->accepted) return false;
  r->accepted = seq; return true;
}
