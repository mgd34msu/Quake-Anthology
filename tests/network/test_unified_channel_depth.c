/* Port of tests/network/unified/channel.test.ts. GPL-2.0-or-later. */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/network/unified/channel.h"
#include "qa/network/unified/packet.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(...) do { checks++; pid_t pid_ = fork(); if (pid_ == 0) { \
    (void)prctl(PR_SET_DUMPABLE, 0); if (!freopen("/dev/null", "w", stderr)) _exit(2); __VA_ARGS__; _exit(0); } \
    int status_ = 0; waitpid(pid_, &status_, 0); \
    if (!(WIFSIGNALED(status_) && WTERMSIG(status_) == SIGABRT)) { failures++; \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); } } while (0)

static const char token[] = "0123456789abcdef0123456789abcdef";
static qa_unified_channel_limits test_limits(void) {
  qa_unified_channel_limits l = qa_unified_channel_default_limits();
  l.datagram_bytes = 80; l.packets_per_flush = 32; l.message_bytes = 4096; l.queued_reliable_bytes = 8192; l.retry_milliseconds = 10;
  return l;
}
static uint8_t *make_bytes(size_t length, int seed) {
  uint8_t *b = malloc(length ? length : 1);
  for (size_t i = 0; i < length; i++) b[i] = (uint8_t)((i + (size_t)seed) % 256);
  return b;
}
static void queue_reliable(qa_unified_channel *c, size_t length, int seed) {
  uint8_t *b = make_bytes(length, seed);
  qa_unified_channel_queue_reliable(c, b, length);
  free(b);
}
static void queue_frame(qa_unified_channel *c, size_t length, int seed, double required) {
  uint8_t *b = make_bytes(length, seed);
  qa_unified_channel_queue_frame(c, b, length, required);
  free(b);
}

typedef struct packets { uint8_t **items; size_t *lengths; size_t count; } packets;
static packets flush_all(qa_unified_channel *c, double now) {
  packets p = {0};
  p.items = qa_unified_channel_flush(c, now, &p.lengths, &p.count);
  return p;
}
static void free_packets(packets *p) {
  for (size_t i = 0; i < p->count; i++) free(p->items[i]);
  free(p->items); free(p->lengths);
  memset(p, 0, sizeof *p);
}
typedef struct log { qa_unified_delivery *items; size_t count, cap; } log_t;
static void take(log_t *log, qa_unified_channel *c, const uint8_t *bytes, size_t length, double now) {
  size_t count = 0;
  qa_unified_delivery *got = qa_unified_channel_receive(c, bytes, length, now, &count);
  for (size_t i = 0; i < count; i++) {
    if (log->count == log->cap) {
      log->cap = log->cap ? log->cap * 2 : 16;
      log->items = realloc(log->items, log->cap * sizeof *log->items);
    }
    log->items[log->count++] = got[i];
  }
  free(got);
}
static void log_clear(log_t *log) {
  qa_unified_deliveries_destroy(log->items, log->count);
  memset(log, 0, sizeof *log);
}
static bool is_delivery(const qa_unified_delivery *d, const char *kind, int64_t sequence, size_t length, int seed, int64_t required) {
  uint8_t *b = make_bytes(length, seed);
  bool same = strcmp(d->kind, kind) == 0 && d->sequence == sequence && d->length == length
      && (length == 0 || memcmp(d->payload, b, length) == 0) && d->required_reliable_sequence == required;
  free(b);
  return same;
}
static void pump(qa_unified_channel *from, qa_unified_channel *to, double now) {
  packets p = flush_all(from, now);
  log_t ignored = {0};
  for (size_t i = 0; i < p.count; i++) take(&ignored, to, p.items[i], p.lengths[i], now);
  log_clear(&ignored);
  free_packets(&p);
}
static int packet_kind(const uint8_t *b, size_t n) {
  qa_unified_packet v;
  return qa_decode_unified_packet(b, n, &v) ? (int)v.kind : -1;
}
static int64_t packet_sequence(const uint8_t *b, size_t n) {
  qa_unified_packet v;
  return qa_decode_unified_packet(b, n, &v) ? v.sequence : -1;
}

static void test_reordered(void) {
  qa_unified_channel_limits la = test_limits(), lb = test_limits();
  la.reliable_window_messages = 1;
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, token, &la); qa_unified_channel_init(&b, token, &lb);
  queue_reliable(&a, 95, 0); queue_reliable(&a, 33, 5);
  packets p = flush_all(&a, 0);
  CHECK(p.count == 3);
  log_t log = {0};
  take(&log, &b, p.items[2], p.lengths[2], 0); CHECK(log.count == 0);
  take(&log, &b, p.items[0], p.lengths[0], 0); CHECK(log.count == 0);
  pump(&b, &a, 0);
  packets none = flush_all(&a, 9); CHECK(none.count == 0); free_packets(&none);
  packets retry = flush_all(&a, 10); CHECK(retry.count == 1);
  take(&log, &b, retry.items[0], retry.lengths[0], 10);
  CHECK(log.count == 1 && is_delivery(&log.items[0], "reliable", 1, 95, 0, 0));
  log_clear(&log); free_packets(&retry);
  packets lost = flush_all(&b, 10); free_packets(&lost);
  packets duplicate = flush_all(&a, 20); CHECK(duplicate.count == 1);
  take(&log, &b, duplicate.items[0], duplicate.lengths[0], 20); CHECK(log.count == 0);
  free_packets(&duplicate);
  pump(&b, &a, 20);
  CHECK(qa_unified_channel_acknowledged_reliable_sequence(&a) == 1);
  packets next = flush_all(&a, 20);
  for (size_t i = 0; i < next.count; i++) take(&log, &b, next.items[i], next.lengths[i], 20);
  CHECK(log.count == 1 && is_delivery(&log.items[0], "reliable", 2, 33, 5, 0));
  log_clear(&log); free_packets(&next);
  pump(&b, &a, 20);
  CHECK(qa_unified_channel_acknowledged_reliable_sequence(&a) == 2);
  free_packets(&p);
  qa_unified_channel_destroy(&a); qa_unified_channel_destroy(&b);
}

static void test_frames_wait(void) {
  qa_unified_channel_limits l = test_limits();
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, token, &l); qa_unified_channel_init(&b, token, &l);
  queue_reliable(&a, 34, 0); queue_frame(&a, 80, 9, 1);
  packets p = flush_all(&a, 0);
  log_t log = {0};
  for (size_t i = p.count; i-- > 0;)
    if (packet_kind(p.items[i], p.lengths[i]) == QA_UNIFIED_FRAME) take(&log, &b, p.items[i], p.lengths[i], 0);
  CHECK(log.count == 0);
  for (size_t i = 0; i < p.count; i++)
    if (packet_kind(p.items[i], p.lengths[i]) == QA_UNIFIED_RELIABLE) take(&log, &b, p.items[i], p.lengths[i], 0);
  CHECK(log.count == 2);
  CHECK(is_delivery(&log.items[0], "reliable", 1, 34, 0, 0));
  CHECK(is_delivery(&log.items[1], "frame", 1, 80, 9, 1));
  log_clear(&log);
  for (size_t i = p.count; i-- > 0;)
    if (packet_kind(p.items[i], p.lengths[i]) == QA_UNIFIED_FRAME) take(&log, &b, p.items[i], p.lengths[i], 1);
  CHECK(log.count == 0);
  free_packets(&p);
  qa_unified_channel_destroy(&a); qa_unified_channel_destroy(&b);
}

static void test_latest_replacement(void) {
  qa_unified_channel_limits la = test_limits(), lb = test_limits();
  la.packets_per_flush = 1;
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, token, &la); qa_unified_channel_init(&b, token, &lb);
  queue_frame(&a, 64, 1, 0);
  packets old = flush_all(&a, 0);
  queue_frame(&a, 3, 2, 0); queue_frame(&a, 4, 3, 0);
  packets ignored = flush_all(&a, 1); free_packets(&ignored);
  packets p = flush_all(&a, 2);
  log_t log = {0};
  for (size_t i = 0; i < p.count; i++) take(&log, &b, p.items[i], p.lengths[i], 2);
  CHECK(log.count == 1 && is_delivery(&log.items[0], "frame", 3, 4, 3, 0));
  log_clear(&log);
  for (size_t i = 0; i < old.count; i++) take(&log, &b, old.items[i], old.lengths[i], 3);
  CHECK(log.count == 0);
  free_packets(&p); free_packets(&old);
  qa_unified_channel_destroy(&a); qa_unified_channel_destroy(&b);
}

static int lossy_count = 0;
static void lossy_deliver(packets *p, qa_unified_channel *receiver, log_t *output, double now) {
  for (size_t i = p->count; i-- > 0;) {
    lossy_count++;
    if (lossy_count % 7 == 0) continue;
    take(output, receiver, p->items[i], p->lengths[i], now);
    if (lossy_count % 3 == 0) take(output, receiver, p->items[i], p->lengths[i], now);
  }
  free_packets(p);
}
static void test_bidirectional(void) {
  qa_unified_channel_limits l = test_limits();
  l.packets_per_flush = 5;
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, token, &l); qa_unified_channel_init(&b, token, &l);
  for (int i = 0; i < 6; i++) { queue_reliable(&a, (size_t)(200 + i), i); queue_reliable(&b, (size_t)(150 + i), i + 10); }
  log_t at_a = {0}, at_b = {0};
  for (int now = 0; now < 1000; now += 10) {
    if (now < 200) { queue_frame(&a, 120, now, 6); queue_frame(&b, 120, now + 1, 6); }
    packets pa = flush_all(&a, now);
    lossy_deliver(&pa, &b, &at_b, now);
    packets pb = flush_all(&b, now);
    lossy_deliver(&pb, &a, &at_a, now);
  }
  int ra = 0, rb = 0, fa = 0, fb = 0;
  for (size_t i = 0; i < at_a.count; i++) {
    if (strcmp(at_a.items[i].kind, "reliable") == 0) {
      CHECK(is_delivery(&at_a.items[i], "reliable", ra + 1, (size_t)(150 + ra), ra + 10, 0));
      ra++;
    } else fa++;
  }
  for (size_t i = 0; i < at_b.count; i++) {
    if (strcmp(at_b.items[i].kind, "reliable") == 0) {
      CHECK(is_delivery(&at_b.items[i], "reliable", rb + 1, (size_t)(200 + rb), rb, 0));
      rb++;
    } else fb++;
  }
  CHECK(ra == 6 && rb == 6);
  CHECK(qa_unified_channel_acknowledged_reliable_sequence(&a) == 6);
  CHECK(qa_unified_channel_acknowledged_reliable_sequence(&b) == 6);
  CHECK(fa > 0 && fb > 0);
  log_clear(&at_a); log_clear(&at_b);
  qa_unified_channel_destroy(&a); qa_unified_channel_destroy(&b);
}

static void test_bounds(void) {
  qa_unified_channel_limits la = test_limits(), lb = test_limits();
  la.queued_reliable_messages = 1; la.maximum_transmissions = 2;
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, token, &la); qa_unified_channel_init(&b, token, &lb);
  CHECK_ABORTS(queue_reliable(&a, 4097, 0));
  CHECK_ABORTS(queue_frame(&a, 1, 0, 1));
  queue_reliable(&a, 40, 0);
  CHECK_ABORTS(queue_reliable(&a, 0, 0));
  packets p = flush_all(&a, 0);
  uint8_t *first = p.items[0];
  size_t first_length = p.lengths[0];
  qa_unified_packet decoded;
  CHECK(qa_decode_unified_packet(first, first_length, &decoded) == 1);
  log_t log = {0};
  qa_unified_packet foreign = decoded;
  memset(foreign.token, 'f', 32);
  size_t n = 0;
  uint8_t *wire = qa_encode_unified_packet(&foreign, &n);
  take(&log, &b, wire, n, 0); CHECK(log.count == 0);
  free(wire);
  take(&log, &b, first + 1, first_length - 1, 0); CHECK(log.count == 0);
  take(&log, &b, first, first_length, 0); CHECK(log.count == 0);
  qa_unified_packet conflicting = decoded;
  conflicting.total_bytes = 41;
  conflicting.fragment = 0;
  conflicting.fragments = 2;
  wire = qa_encode_unified_packet(&conflicting, &n);
  take(&log, &b, wire, n, 0); CHECK(log.count == 0);
  free(wire);
  packets retry = flush_all(&a, 10); free_packets(&retry);
  CHECK(a.failure == NULL);
  packets beyond = flush_all(&a, 20);
  CHECK(beyond.count == 0 && a.failure && strstr(a.failure, "retry limit"));
  CHECK(qa_unified_channel_closed(&a));
  free_packets(&p);
  qa_unified_channel_destroy(&a); qa_unified_channel_destroy(&b);

  qa_unified_channel empty, other;
  qa_unified_channel_init(&empty, token, &lb); qa_unified_channel_init(&other, token, &lb);
  queue_reliable(&empty, 0, 0);
  packets e = flush_all(&empty, 0);
  for (size_t i = 0; i < e.count; i++) take(&log, &other, e.items[i], e.lengths[i], 0);
  CHECK(log.count == 1 && is_delivery(&log.items[0], "reliable", 1, 0, 0, 0));
  log_clear(&log);
  free_packets(&e);
  qa_unified_channel_close(&empty); qa_unified_channel_close(&empty);
  CHECK_ABORTS(queue_frame(&empty, 0, 0, 0));
  qa_unified_channel_destroy(&empty); qa_unified_channel_destroy(&other);
}

static void test_expiry(void) {
  qa_unified_channel_limits l = test_limits(), short_limits = test_limits();
  short_limits.assembly_milliseconds = 20;
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, token, &l); qa_unified_channel_init(&b, token, &short_limits);
  queue_reliable(&a, 64, 0);
  packets p = flush_all(&a, 0);
  log_t log = {0};
  take(&log, &b, p.items[0], p.lengths[0], 0);
  packets none = flush_all(&b, 20);
  CHECK(none.count == 0 && b.failure && strstr(b.failure, "assembly timed out"));
  CHECK(qa_unified_channel_closed(&b));
  free_packets(&p);
  qa_unified_channel_destroy(&a); qa_unified_channel_destroy(&b);

  qa_unified_channel c, d;
  qa_unified_channel_init(&c, token, &l); qa_unified_channel_init(&d, token, &short_limits);
  queue_frame(&c, 64, 0, 0);
  packets old = flush_all(&c, 0);
  take(&log, &d, old.items[0], old.lengths[0], 0);
  packets idle = flush_all(&d, 20); CHECK(idle.count == 0 && d.failure == NULL);
  queue_frame(&c, 2, 0, 0);
  packets fresh = flush_all(&c, 21);
  for (size_t i = 0; i < fresh.count; i++) take(&log, &d, fresh.items[i], fresh.lengths[i], 21);
  CHECK(log.count == 1 && is_delivery(&log.items[0], "frame", 2, 2, 0, 0));
  log_clear(&log);
  free_packets(&old); free_packets(&fresh);
  qa_unified_channel_destroy(&c); qa_unified_channel_destroy(&d);
}

static void test_fair_lanes(void) {
  qa_unified_channel_limits l = test_limits();
  l.packets_per_flush = 1;
  qa_unified_channel a;
  qa_unified_channel_init(&a, token, &l);
  queue_reliable(&a, 200, 0); queue_frame(&a, 64, 0, 0);
  const int expected[] = {QA_UNIFIED_RELIABLE, QA_UNIFIED_FRAME, QA_UNIFIED_RELIABLE, QA_UNIFIED_FRAME};
  for (int t = 0; t < 4; t++) {
    packets p = flush_all(&a, t);
    CHECK(p.count == 1 && packet_kind(p.items[0], p.lengths[0]) == expected[t]);
    free_packets(&p);
  }
  qa_unified_channel_destroy(&a);
}

static void test_flight(void) {
  qa_unified_channel_limits l = test_limits();
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, token, &l); qa_unified_channel_init(&b, token, &l);
  for (int i = 0; i < 10; i++) queue_reliable(&a, 1, i);
  queue_frame(&a, 1, 44, 8);
  packets p = flush_all(&a, 0);
  size_t controls[16], control_count = 0;
  log_t log = {0};
  for (size_t i = 0; i < p.count; i++) {
    if (packet_kind(p.items[i], p.lengths[i]) == QA_UNIFIED_RELIABLE) controls[control_count++] = i;
    else take(&log, &b, p.items[i], p.lengths[i], 0);
  }
  CHECK(log.count == 0);
  CHECK(control_count == 8);
  for (size_t i = 0; i < control_count; i++)
    CHECK(packet_sequence(p.items[controls[i]], p.lengths[controls[i]]) == (int64_t)i + 1);
  for (size_t i = control_count; i-- > 1;) {
    take(&log, &b, p.items[controls[i]], p.lengths[controls[i]], 0);
    CHECK(log.count == 0);
  }
  CHECK(qa_unified_channel_received_reliable_sequence(&b) == 0);
  take(&log, &b, p.items[controls[0]], p.lengths[controls[0]], 0);
  CHECK(log.count == 9);
  for (size_t i = 0; i < 8 && i < log.count; i++)
    CHECK(strcmp(log.items[i].kind, "reliable") == 0 && log.items[i].sequence == (int64_t)i + 1);
  CHECK(log.count == 9 && is_delivery(&log.items[8], "frame", 1, 1, 44, 8));
  log_clear(&log);
  pump(&b, &a, 0);
  CHECK(qa_unified_channel_acknowledged_reliable_sequence(&a) == 8);
  packets next = flush_all(&a, 1);
  for (size_t i = 0; i < next.count; i++) take(&log, &b, next.items[i], next.lengths[i], 1);
  CHECK(log.count == 2 && log.items[0].sequence == 9 && log.items[1].sequence == 10);
  log_clear(&log);
  free_packets(&next); free_packets(&p);
  qa_unified_channel_destroy(&a); qa_unified_channel_destroy(&b);
}

static void test_future_ack(void) {
  qa_unified_channel_limits l = test_limits();
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, token, &l); qa_unified_channel_init(&b, token, &l);
  queue_reliable(&a, 64, 0); queue_reliable(&a, 40, 90);
  packets p = flush_all(&a, 0);
  log_t log = {0};
  for (size_t i = 0; i < p.count; i++) {
    qa_unified_packet v;
    CHECK(qa_decode_unified_packet(p.items[i], p.lengths[i], &v) == 1);
    if (v.kind == QA_UNIFIED_RELIABLE && v.sequence == 1 && v.fragment == 1) continue;
    take(&log, &b, p.items[i], p.lengths[i], 0);
    CHECK(log.count == 0);
  }
  free_packets(&p);
  pump(&b, &a, 0);
  packets retry = flush_all(&a, 10);
  CHECK(retry.count == 1);
  qa_unified_packet v;
  CHECK(qa_decode_unified_packet(retry.items[0], retry.lengths[0], &v) == 1);
  CHECK(v.kind == QA_UNIFIED_RELIABLE && v.sequence == 1 && v.fragment == 1);
  take(&log, &b, retry.items[0], retry.lengths[0], 10);
  CHECK(log.count == 2 && log.items[0].sequence == 1 && log.items[1].sequence == 2);
  log_clear(&log); free_packets(&retry);
  packets lost = flush_all(&b, 10); free_packets(&lost); /* Lose the cumulative ACK for both controls. */
  packets probe = flush_all(&a, 20);
  CHECK(probe.count == 1 && packet_sequence(probe.items[0], probe.lengths[0]) == 1);
  take(&log, &b, probe.items[0], probe.lengths[0], 20);
  CHECK(log.count == 0);
  free_packets(&probe);
  pump(&b, &a, 20);
  CHECK(qa_unified_channel_acknowledged_reliable_sequence(&a) == 2);
  qa_unified_channel_destroy(&a); qa_unified_channel_destroy(&b);
}

static void test_receiver_window(void) {
  qa_unified_channel_limits ls = test_limits(), lr = test_limits();
  ls.reliable_window_messages = 4;
  lr.reliable_window_messages = 3; lr.message_bytes = 64; lr.queued_reliable_bytes = 64;
  qa_unified_channel sender, receiver;
  qa_unified_channel_init(&sender, token, &ls); qa_unified_channel_init(&receiver, token, &lr);
  for (int i = 0; i < 4; i++) queue_reliable(&sender, 64, i);
  packets p = flush_all(&sender, 0);
  log_t log = {0};
  for (size_t i = 0; i < p.count; i++)
    if (packet_sequence(p.items[i], p.lengths[i]) != 1) take(&log, &receiver, p.items[i], p.lengths[i], 0);
  CHECK(log.count == 0);
  packets none = flush_all(&receiver, 0); CHECK(none.count == 0); free_packets(&none);
  for (size_t i = 0; i < p.count; i++)
    if (packet_sequence(p.items[i], p.lengths[i]) == 1) take(&log, &receiver, p.items[i], p.lengths[i], 1);
  CHECK(log.count == 1 && log.items[0].sequence == 1);
  log_clear(&log); free_packets(&p);
  pump(&receiver, &sender, 1);
  for (int now = 10; now < 100; now += 10) {
    packets out = flush_all(&sender, now);
    for (size_t i = 0; i < out.count; i++) take(&log, &receiver, out.items[i], out.lengths[i], now);
    free_packets(&out);
    pump(&receiver, &sender, now);
  }
  CHECK(log.count == 3 && log.items[0].sequence == 2 && log.items[1].sequence == 3 && log.items[2].sequence == 4);
  CHECK(qa_unified_channel_acknowledged_reliable_sequence(&sender) == 4);
  log_clear(&log);
  qa_unified_channel_destroy(&sender); qa_unified_channel_destroy(&receiver);
}

int main(void) {
  test_reordered();
  test_frames_wait();
  test_latest_replacement();
  test_bidirectional();
  test_bounds();
  test_expiry();
  test_fair_lanes();
  test_flight();
  test_future_ack();
  test_receiver_window();
  printf("unified_channel_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
