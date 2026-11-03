/* Port of src/network/common/scheduling.ts. GPL-2.0-or-later. */
#include "qa/network/common/scheduling.h"
#include "qa/foundation/support.h"
#include <math.h>

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
