/* Port of src/network/common/scheduling.ts (PacketRate, PacketScheduler<T>).
 * Scheduler payloads use element-size storage (PORTING generics rule);
 * pointer payloads store the pointer value. RangeError -> qa_range_error.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_SCHEDULING_H
#define QA_NETWORK_COMMON_SCHEDULING_H
#include <stdbool.h>
#include <stddef.h>
typedef struct qa_packet_rate {
  double bytes_per_second, backup_bytes, clear_time;
} qa_packet_rate;
void qa_packet_rate_init(qa_packet_rate *r, double bytes_per_second, double backup_bytes);
bool qa_packet_rate_can_send(const qa_packet_rate *r, double now_ms, bool paused);
void qa_packet_rate_sent(qa_packet_rate *r, double bytes, double now_ms, bool paused);
double qa_packet_rate_next_send_ms(const qa_packet_rate *r);
void qa_packet_rate_reset(qa_packet_rate *r);

typedef void (*qa_packet_scheduler_deliver_fn)(void *ctx, const void *value);
struct qa_sched_item { long long sequence; double due; };
typedef struct qa_packet_scheduler {
  size_t elem_size, count, capacity;
  struct qa_sched_item *meta;
  unsigned char *values;
  long long next_sequence;
} qa_packet_scheduler;
int qa_packet_scheduler_init(qa_packet_scheduler *s, size_t elem_size);
void qa_packet_scheduler_destroy(qa_packet_scheduler *s);
/* Returns sequence id (>=0). Aborts on non-finite due. 0 only on OOM via -1. */
long long qa_packet_scheduler_schedule(qa_packet_scheduler *s, double due_ms, const void *value);
bool qa_packet_scheduler_cancel(qa_packet_scheduler *s, long long sequence);
void qa_packet_scheduler_drain(qa_packet_scheduler *s, double now_ms,
                              qa_packet_scheduler_deliver_fn deliver, void *ctx);
void qa_packet_scheduler_clear(qa_packet_scheduler *s);
size_t qa_packet_scheduler_pending_count(const qa_packet_scheduler *s);
#endif
