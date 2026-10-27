/* Port of src/network/common/scheduling.ts. GPL-2.0-or-later. */
#include "qa/network/common/scheduling.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void qa_packet_rate_init(qa_packet_rate *r, double bps, double backup) {
  if (!isfinite(bps) || bps <= 0) qa_range_error("Network rate must be positive");
  r->bytes_per_second = bps; r->backup_bytes = backup; r->clear_time = 0;
}
bool qa_packet_rate_can_send(const qa_packet_rate *r, double now_ms, bool paused) {
  return paused || r->clear_time < now_ms + r->backup_bytes * 1000.0 / r->bytes_per_second;
}
void qa_packet_rate_sent(qa_packet_rate *r, double bytes, double now_ms, bool paused) {
  double base = r->clear_time > now_ms ? r->clear_time : now_ms;
  r->clear_time = paused ? now_ms : base + bytes * 1000.0 / r->bytes_per_second;
}
double qa_packet_rate_next_send_ms(const qa_packet_rate *r) { return r->clear_time; }
void qa_packet_rate_reset(qa_packet_rate *r) { r->clear_time = 0; }

static int sched_cmp(const struct qa_sched_item *a, const struct qa_sched_item *b) {
  if (a->due < b->due) return -1;
  if (a->due > b->due) return 1;
  if (a->sequence < b->sequence) return -1;
  return a->sequence > b->sequence ? 1 : 0;
}
int qa_packet_scheduler_init(qa_packet_scheduler *s, size_t elem_size) {
  memset(s, 0, sizeof *s); s->elem_size = elem_size ? elem_size : 1; return 1;
}
void qa_packet_scheduler_destroy(qa_packet_scheduler *s) {
  free(s->meta); free(s->values); memset(s, 0, sizeof *s);
}
long long qa_packet_scheduler_schedule(qa_packet_scheduler *s, double due_ms, const void *value) {
  if (!isfinite(due_ms)) qa_range_error("Invalid packet time");
  if (s->count == s->capacity) {
    size_t nc = s->capacity ? s->capacity * 2 : 8;
    struct qa_sched_item *nm = realloc(s->meta, nc * sizeof *nm);
    if (!nm) return -1;
    uint8_t *nv = realloc(s->values, nc * s->elem_size);
    if (!nv) { s->meta = nm; return -1; }
    s->meta = nm; s->values = nv; s->capacity = nc;
  }
  long long seq = s->next_sequence++;
  size_t pos = s->count;
  s->meta[pos].sequence = seq; s->meta[pos].due = due_ms;
  memcpy(s->values + pos * s->elem_size, value, s->elem_size);
  s->count++;
  while (pos > 0 && sched_cmp(&s->meta[pos - 1], &s->meta[pos]) > 0) {
    struct qa_sched_item t = s->meta[pos - 1]; s->meta[pos - 1] = s->meta[pos]; s->meta[pos] = t;
    uint8_t tmp[64];
    size_t off = (pos - 1) * s->elem_size, off2 = pos * s->elem_size;
    if (s->elem_size <= sizeof tmp) {
      memcpy(tmp, s->values + off, s->elem_size);
      memcpy(s->values + off, s->values + off2, s->elem_size);
      memcpy(s->values + off2, tmp, s->elem_size);
    } else {
      for (size_t i = 0; i < s->elem_size; i++) {
        uint8_t b = s->values[off + i]; s->values[off + i] = s->values[off2 + i]; s->values[off2 + i] = b;
      }
    }
    pos--;
  }
  return seq;
}
bool qa_packet_scheduler_cancel(qa_packet_scheduler *s, long long sequence) {
  for (size_t i = 0; i < s->count; i++) {
    if (s->meta[i].sequence != sequence) continue;
    memmove(&s->meta[i], &s->meta[i + 1], (s->count - i - 1) * sizeof *s->meta);
    memmove(s->values + i * s->elem_size, s->values + (i + 1) * s->elem_size,
            (s->count - i - 1) * s->elem_size);
    s->count--; return true;
  }
  return false;
}
void qa_packet_scheduler_drain(qa_packet_scheduler *s, double now_ms,
                              qa_packet_scheduler_deliver_fn deliver, void *ctx) {
  size_t count = 0;
  while (count < s->count && s->meta[count].due <= now_ms) count++;
  if (!count) return;
  for (size_t i = 0; i < count; i++) deliver(ctx, s->values + i * s->elem_size);
  memmove(s->meta, &s->meta[count], (s->count - count) * sizeof *s->meta);
  memmove(s->values, s->values + count * s->elem_size, (s->count - count) * s->elem_size);
  s->count -= count;
}
void qa_packet_scheduler_clear(qa_packet_scheduler *s) { s->count = 0; }
size_t qa_packet_scheduler_pending_count(const qa_packet_scheduler *s) { return s->count; }
