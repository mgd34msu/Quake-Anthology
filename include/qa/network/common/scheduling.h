/* Port of src/network/common/scheduling.ts (PacketRate; TS dropped PacketScheduler).
 * RangeError -> qa_range_error. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_SCHEDULING_H
#define QA_NETWORK_COMMON_SCHEDULING_H
#include <stdbool.h>
typedef struct qa_packet_rate {
  double bytes_per_second, backup_bytes, clear_time;
} qa_packet_rate;
void qa_packet_rate_init(qa_packet_rate *r, double bytes_per_second, double backup_bytes);
bool qa_packet_rate_can_send(const qa_packet_rate *r, double now_ms, bool paused);
void qa_packet_rate_sent(qa_packet_rate *r, double bytes, double now_ms, bool paused);
double qa_packet_rate_next_send_ms(const qa_packet_rate *r);
void qa_packet_rate_reset(qa_packet_rate *r);
#endif
