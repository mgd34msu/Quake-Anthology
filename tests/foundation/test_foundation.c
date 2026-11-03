/*
 * Port of tests/foundation/primitives.test.ts and event-ring.test.ts.
 * The arena, pool, handles, vector-store and system-queue cases left with
 * those TS modules (removed 2026-11). TypeScript `toThrow(RangeError)` cases run in a child
 * process and must abort. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/foundation.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static void samples(double *out, int count) {
  int32_t seed = 22222;
  for (int i = 0; i < count; i++) {
    seed = (int32_t)((uint32_t)seed * 69069u + 1u);
    out[i] = (((uint32_t)seed & 0xffffu) / 65536.0 - 0.5) * 2000.0;
  }
}

static void test_binary(void) {
  CHECK(qa_float32_bits(0.0) == 0x00000000u);
  CHECK(qa_float32_bits(-0.0) == 0x80000000u);
  CHECK(qa_float32_bits(1.0) == 0x3f800000u);
  CHECK(qa_float32_bits(-1.0) == 0xbf800000u);
  CHECK(qa_float32_bits(0.5) == 0x3f000000u);
  CHECK(qa_float32_bits(-273.15) == 0xc3889333u);
  CHECK(qa_float32_bits(1e-40) == 0x000116c2u);
  CHECK(qa_float32_bits(3.4028235e38) == 0x7f7fffffu);
  CHECK(qa_float32_bits(INFINITY) == 0x7f800000u);
  CHECK(qa_float32_bits(-INFINITY) == 0xff800000u);
  CHECK(qa_float32_bits(NAN) == 0x7fc00000u);
  double vals[64];
  samples(vals, 64);
  for (int i = 0; i < 64; i++) {
    float round = qa_float32_from_bits(qa_float32_bits(vals[i]));
    CHECK(round == (float)vals[i]);
  }
  CHECK(qa_float64_from_bits(qa_float64_bits(-273.15)) == -273.15);

  uint8_t bytes[8] = {0};
  qa_store_u32_le(bytes, sizeof bytes, 0, 0xdeadbeefu);
  qa_store_f32_le(bytes, sizeof bytes, 4, 1.5);
  CHECK(qa_load_u32_le(bytes, sizeof bytes, 0) == 0xdeadbeefu);
  CHECK(qa_load_i32_le(bytes, sizeof bytes, 0) == (int32_t)0xdeadbeefu);
  CHECK(qa_load_f32_le(bytes, sizeof bytes, 4) == 1.5f);
  CHECK(qa_load_u16_le(bytes, sizeof bytes, 0) == 0xbeefu);
  CHECK_ABORTS((void)qa_load_u32_le(bytes, sizeof bytes, 6));

  /* loadF64LE/storeF64LE (binary.ts): low word first. */
  uint8_t wide[8] = {0};
  qa_store_f64_le(wide, sizeof wide, 0, -273.15);
  CHECK(qa_load_u32_le(wide, sizeof wide, 0) == (uint32_t)qa_float64_bits(-273.15));
  CHECK(qa_load_u32_le(wide, sizeof wide, 4) == (uint32_t)(qa_float64_bits(-273.15) >> 32));
  CHECK(qa_load_f64_le(wide, sizeof wide, 0) == -273.15);
}

static void test_stamp(void) {
  qa_stamp_set stamps;
  CHECK(qa_stamp_set_init(&stamps, 4));
  CHECK(qa_stamp_set_mark(&stamps, 2) == true);
  CHECK(qa_stamp_set_mark(&stamps, 2) == false);
  CHECK(qa_stamp_set_test(&stamps, 2) == true);
  CHECK(qa_stamp_set_test(&stamps, 1) == false);
  qa_stamp_set_begin(&stamps);
  CHECK(qa_stamp_set_test(&stamps, 2) == false);
  CHECK(qa_stamp_set_mark(&stamps, 2) == true);
  uint32_t saved = qa_stamp_set_value(&stamps, 2);
  qa_stamp_set_begin(&stamps);
  qa_stamp_set_restore_mark(&stamps, 2, saved);
  CHECK(qa_stamp_set_test(&stamps, 2) == false);
  CHECK(stamps.capacity == 4);
  qa_stamp_set_destroy(&stamps);

  /* primitives.test.ts "the epoch re-zeroes at the 32-bit limit". */
  qa_stamp_set wrap;
  CHECK(qa_stamp_set_init(&wrap, 2));
  wrap.epoch = 0xffffffffu;
  CHECK(qa_stamp_set_mark(&wrap, 1) == true);
  qa_stamp_set_begin(&wrap);
  CHECK(wrap.epoch == 1);
  CHECK(qa_stamp_set_mark(&wrap, 1) == true);
  CHECK(qa_stamp_set_mark(&wrap, 1) == false);
  qa_stamp_set_destroy(&wrap);
}

static void test_strings(void) {
  qa_string_table *strings = qa_string_table_create(NULL, NULL);
  CHECK(strings != NULL);
  uint32_t monster = qa_string_table_intern(strings, "monster_demon1");
  CHECK(qa_string_table_intern(strings, "monster_demon1") == monster);
  CHECK(qa_string_table_find(strings, "absent") == QA_STRING_NONE);
  CHECK(strcmp(qa_string_table_text(strings, monster), "monster_demon1") == 0);
  CHECK((qa_string_table_name_flags(strings, monster) & QA_NAME_MONSTER) != 0);
  uint32_t ammo = qa_string_table_intern(strings, "q1:ammo/shells");
  CHECK((qa_string_table_name_flags(strings, ammo) & QA_NAME_Q1_AMMO) != 0);
  uint32_t red = qa_string_table_intern(strings, "red");
  uint32_t want = QA_NAME_TEAM_RED_ALIAS | QA_NAME_TEAM_RED_EXACT;
  CHECK((qa_string_table_name_flags(strings, red) & want) == want);
  const char *names[] = {"alpha", "beta", "alpha"};
  uint32_t bound[3];
  qa_string_table_bind_names(strings, names, 3, bound);
  CHECK(bound[0] == bound[2]);
  CHECK(bound[0] != bound[1]);
  CHECK(qa_string_table_count(strings) == 5);
  CHECK(qa_quake_name_flags("*deathp") & QA_NAME_BRUSH_PATH);
  CHECK(qa_quake_name_flags("func_door") & QA_NAME_FUNC);
  CHECK(qa_quake_name_flags("info_player_start") & QA_NAME_PLAYER_START);
  qa_string_table_destroy(strings);
}

static bool eq_int(const void *value, void *ctx) {
  return *(const int *)value == *(const int *)ctx;
}
static bool is_odd(const void *value, void *ctx) {
  (void)ctx;
  return *(const int *)value % 2 == 1;
}
struct collect { int values[16]; size_t count; };
static void collect_visit(const void *value, size_t index, void *ctx) {
  (void)index;
  struct collect *c = (struct collect *)ctx;
  c->values[c->count++] = *(const int *)value;
}
static bool queue_eq(qa_bounded_queue *queue, const int *want, size_t n) {
  size_t count = 0;
  int *got = (int *)qa_bounded_queue_to_array(queue, &count);
  bool ok = count == n && (n == 0 || memcmp(got, want, n * sizeof(int)) == 0);
  free(got);
  return ok;
}

static void test_bounded_queue(void) {
  qa_bounded_queue queue;
  CHECK(qa_bounded_queue_init(&queue, 4, sizeof(int)));
  for (int v = 1; v <= 5; v++) qa_bounded_queue_push(&queue, &v);
  const int want1[] = {2, 3, 4, 5};
  CHECK(queue_eq(&queue, want1, 4));
  CHECK(queue.dropped == 1);
  CHECK(*(const int *)qa_bounded_queue_peek_oldest(&queue) == 2);
  CHECK(*(const int *)qa_bounded_queue_peek_newest(&queue) == 5);
  int shifted = 0;
  CHECK(qa_bounded_queue_shift(&queue, &shifted) && shifted == 2);
  int four = 4;
  CHECK(qa_bounded_queue_remove_first(&queue, eq_int, &four) == true);
  const int want2[] = {3, 5};
  CHECK(queue_eq(&queue, want2, 2));
  int six = 6, seven = 7;
  qa_bounded_queue_push(&queue, &six);
  qa_bounded_queue_push(&queue, &seven);
  qa_bounded_queue_retain(&queue, is_odd, NULL);
  const int want3[] = {3, 5, 7};
  CHECK(queue_eq(&queue, want3, 3));
  int five = 5;
  CHECK(qa_bounded_queue_find_index(&queue, eq_int, &five) == 1);
  int thirty = 30, nine = 9;
  qa_bounded_queue_set_at(&queue, 0, &thirty);
  qa_bounded_queue_set_at(&queue, queue.count, &nine);
  const int want4[] = {30, 5, 7, 9};
  CHECK(queue_eq(&queue, want4, 4));
  struct collect c = {{0}, 0};
  qa_bounded_queue_for_each(&queue, collect_visit, &c);
  CHECK(c.count == 4 && memcmp(c.values, want4, sizeof want4) == 0);
  size_t taken_count = 0;
  int *taken = (int *)qa_bounded_queue_take_all(&queue, &taken_count);
  CHECK(taken_count == 4 && memcmp(taken, want4, sizeof want4) == 0);
  free(taken);
  CHECK(queue.count == 0);
  CHECK(qa_bounded_queue_shift(&queue, &shifted) == false);
  qa_bounded_queue_destroy(&queue);

  qa_bounded_queue small;
  CHECK(qa_bounded_queue_init(&small, 3, sizeof(int)));
  for (int v = 1; v <= 4; v++) qa_bounded_queue_push(&small, &v);
  const int want5[] = {2, 3, 4};
  CHECK(queue_eq(&small, want5, 3));
  qa_bounded_queue_destroy(&small);
  CHECK_ABORTS({ qa_bounded_queue q; qa_bounded_queue_init(&q, 0, sizeof(int)); });
}

struct cleanup_log { char text[32]; size_t length; qa_cleanup_stack *stack; };
static void log_char(struct cleanup_log *log, char ch) {
  log->text[log->length++] = ch;
  log->text[log->length] = '\0';
}
static void cleanup_a(void *ctx) { log_char((struct cleanup_log *)ctx, 'a'); }
static void cleanup_late(void *ctx) { log_char((struct cleanup_log *)ctx, 'L'); }
static void cleanup_b(void *ctx) {
  struct cleanup_log *log = (struct cleanup_log *)ctx;
  log_char(log, 'b');
  qa_cleanup_stack_add(log->stack, cleanup_late, log);
}
static void cleanup_x(void *ctx) { log_char((struct cleanup_log *)ctx, 'x'); }
static void cleanup_y(void *ctx) { log_char((struct cleanup_log *)ctx, 'y'); }

static void test_cleanup(void) {
  qa_cleanup_stack stack;
  qa_cleanup_stack_init(&stack);
  struct cleanup_log log = {"", 0, &stack};
  CHECK(qa_cleanup_stack_add(&stack, cleanup_a, &log) == 0);
  CHECK(qa_cleanup_stack_add(&stack, cleanup_b, &log) == 0);
  qa_cleanup_stack_run_all(&stack);
  CHECK(strcmp(log.text, "ab") == 0);
  CHECK(stack.length == 1);
  qa_cleanup_stack_run_reverse(&stack);
  CHECK(strcmp(log.text, "abL") == 0);
  CHECK(qa_cleanup_stack_add(&stack, cleanup_x, &log) == 0);
  CHECK(qa_cleanup_stack_add(&stack, cleanup_y, &log) == 0);
  qa_cleanup_stack_invoke_reverse(&stack);
  CHECK(strcmp(log.text, "abLyx") == 0);
  CHECK(stack.length == 2);
  qa_cleanup_stack_run_reverse(&stack);
  CHECK(strcmp(log.text, "abLyxyx") == 0);
  CHECK(stack.length == 0);
  qa_cleanup_stack_destroy(&stack);
}

static void test_id_index(void) {
  qa_id_index index;
  qa_id_index_init(&index);
  qa_id_index_reserve(&index, 3);
  qa_id_index_insert(&index, 10, 1, 100);
  qa_id_index_insert(&index, 10, 1, 101);
  qa_id_index_insert(&index, 10, 2, 102);
  CHECK(index.entry_count == 3);
  CHECK(qa_id_index_find(&index, 10, 1) == 101);
  CHECK(qa_id_index_find(&index, 10, 3) == -1);
  qa_id_index_cursor cursor = qa_id_index_begin(&index, 10, 1);
  CHECK(qa_id_index_next(&index, &cursor) == 101);
  CHECK(qa_id_index_next(&index, &cursor) == 100);
  CHECK(qa_id_index_next(&index, &cursor) == -1);
  CHECK(qa_id_index_remove_value(&index, 10, 1, 101) == true);
  CHECK(qa_id_index_find(&index, 10, 1) == 100);
  CHECK(qa_id_index_remove(&index, 10, 1) == true);
  CHECK(qa_id_index_find(&index, 10, 2) == 102);
  qa_id_index_destroy(&index);
  CHECK_ABORTS({
    qa_id_index idx;
    qa_id_index_init(&idx);
    qa_id_index_reserve(&idx, 1);
    qa_id_index_insert(&idx, 1, 1, 1);
    qa_id_index_insert(&idx, 1, 1, 1);
  });

  qa_id_index grown;
  qa_id_index_init(&grown);
  qa_id_index_reserve(&grown, 1);
  qa_id_index_insert(&grown, 10, 1, 100);
  qa_id_index_reserve(&grown, 1);
  qa_id_index_insert(&grown, 10, 1, 101);
  for (int i = 0; i < 500; i++) {
    qa_id_index_reserve(&grown, 1);
    qa_id_index_insert(&grown, (uint32_t)(1000 + i), 1, 2000 + i);
  }
  qa_id_index_reserve(&grown, 1);
  qa_id_index_insert(&grown, 10, 1, 102);
  CHECK(qa_id_index_find(&grown, 10, 1) == 102);
  qa_id_index_cursor gc = qa_id_index_begin(&grown, 10, 1);
  CHECK(qa_id_index_next(&grown, &gc) == 102);
  CHECK(qa_id_index_next(&grown, &gc) == 101);
  CHECK(qa_id_index_next(&grown, &gc) == 100);
  CHECK(qa_id_index_next(&grown, &gc) == -1);
  qa_id_index_destroy(&grown);
}

struct seen { int64_t values[16]; size_t count; qa_event_ring *ring; int64_t appends; };
static void seen_visit(const void *record, int64_t id, void *ctx) {
  (void)id;
  struct seen *seen = (struct seen *)ctx;
  seen->values[seen->count++] = *(const int64_t *)record;
  if (seen->appends != 0) {
    int64_t extra = seen->appends;
    seen->appends = 0;
    qa_event_ring_append(seen->ring, &extra);
  }
}
static bool is_value(const void *record, void *ctx) {
  return *(const int64_t *)record == *(const int64_t *)ctx;
}
static const int64_t *ring_read(qa_event_ring *ring, qa_event_cursor *cursor) {
  return (const int64_t *)qa_event_ring_read(ring, cursor);
}

static void test_event_ring(void) {
  qa_event_ring ring;
  CHECK(qa_event_ring_init(&ring, 4, sizeof(int64_t)));
  const char *letters = "abcd";
  (void)letters;
  for (int64_t v = 1; v <= 4; v++) CHECK(qa_event_ring_append(&ring, &v) == v);
  int64_t five = 5;
  CHECK(qa_event_ring_append(&ring, &five) == 0);
  CHECK(ring.blocked_count == 1);
  CHECK(*(const int64_t *)qa_event_ring_at(&ring, 1) == 1);
  CHECK(qa_event_ring_at(&ring, 5) == NULL);
  qa_event_ring_stats stats = qa_event_ring_stats_of(&ring);
  CHECK(stats.capacity == 4 && stats.live == 4 && stats.accepted == 4 && stats.blocked == 1);
  CHECK(*(const int64_t *)qa_event_ring_head(&ring) == 1);
  CHECK(*(const int64_t *)qa_event_ring_newest(&ring) == 4);
  CHECK(*(const int64_t *)qa_event_ring_at_offset(&ring, 2) == 3);
  qa_event_ring_destroy(&ring);

  CHECK(qa_event_ring_init(&ring, 8, sizeof(int64_t)));
  qa_event_cursor slow, fast;
  CHECK(qa_event_ring_open_cursor(&ring, &slow, false));
  CHECK(qa_event_ring_open_cursor(&ring, &fast, false));
  for (int64_t v = 10; v <= 30; v += 10) qa_event_ring_append(&ring, &v);
  const int64_t *r;
  r = ring_read(&ring, &fast); CHECK(r != NULL && *r == 10);
  r = ring_read(&ring, &fast); CHECK(r != NULL && *r == 20);
  qa_event_ring_retire(&ring);
  CHECK(ring.first_id == 1);
  r = ring_read(&ring, &slow); CHECK(r != NULL && *r == 10);
  qa_event_ring_retire(&ring);
  CHECK(ring.first_id == 2);
  CHECK(qa_event_ring_at(&ring, 1) == NULL);
  struct seen seen = {{0}, 0, &ring, 0};
  CHECK(qa_event_ring_drain(&ring, &slow, seen_visit, &seen) == 2);
  CHECK(seen.count == 2 && seen.values[0] == 20 && seen.values[1] == 30);
  size_t taken_count = 0;
  int64_t *taken = (int64_t *)qa_event_ring_take(&ring, &fast, &taken_count);
  CHECK(taken_count == 1 && taken[0] == 30);
  free(taken);
  qa_event_ring_retire(&ring);
  CHECK(qa_event_ring_live(&ring) == 0);
  int64_t forty = 40;
  CHECK(qa_event_ring_append(&ring, &forty) == 4);
  qa_event_ring_close_cursor(&ring, &slow);
  qa_event_ring_close_cursor(&ring, &fast);
  qa_event_ring_destroy(&ring);

  CHECK(qa_event_ring_init(&ring, 8, sizeof(int64_t)));
  qa_event_cursor cursor;
  CHECK(qa_event_ring_open_cursor(&ring, &cursor, false));
  int64_t one = 1;
  qa_event_ring_append(&ring, &one);
  struct seen deferred = {{0}, 0, &ring, 2};
  qa_event_ring_drain(&ring, &cursor, seen_visit, &deferred);
  CHECK(deferred.count == 1 && deferred.values[0] == 1);
  r = ring_read(&ring, &cursor); CHECK(r != NULL && *r == 2);
  qa_event_ring_destroy(&ring);

  /* Leases with pointer payloads keep the TypeScript identity check. */
  int record = 7, other = 8;
  void *rec_ptr = &record, *other_ptr = &other;
  CHECK(qa_event_ring_init(&ring, 4, sizeof(void *)));
  CHECK(qa_event_ring_open_cursor(&ring, &cursor, false));
  int64_t id = qa_event_ring_append(&ring, &rec_ptr);
  qa_event_ring_append(&ring, &other_ptr);
  CHECK(qa_event_ring_retain(&ring, id) == true);
  taken = (int64_t *)qa_event_ring_take(&ring, &cursor, &taken_count);
  CHECK(taken_count == 2);
  free(taken);
  qa_event_ring_retire(&ring);
  CHECK(qa_event_ring_at(&ring, id) == NULL);
  const void *const *leased =
      (const void *const *)qa_event_ring_leased_record(&ring, id);
  CHECK(leased != NULL && *leased == &record);
  CHECK(qa_event_ring_release_lease(&ring, id) == true);
  CHECK(qa_event_ring_leased_record(&ring, id) == NULL);
  CHECK(qa_event_ring_retain(&ring, 999) == false);
  qa_event_ring_destroy(&ring);

  CHECK(qa_event_ring_init(&ring, 4, sizeof(int64_t)));
  CHECK(qa_event_ring_open_cursor(&ring, &cursor, false));
  const int64_t batch123[] = {1, 2, 3};
  const int64_t batch45[] = {4, 5};
  const int64_t batch4[] = {4};
  CHECK(qa_event_ring_append_all(&ring, batch123, 3) == 1);
  CHECK(qa_event_ring_append_all(&ring, batch45, 2) == 0);
  CHECK(qa_event_ring_live(&ring) == 3);
  CHECK(qa_event_ring_append_all(&ring, batch4, 1) == 4);
  qa_event_ring_reset(&ring);
  CHECK(qa_event_ring_live(&ring) == 0);
  CHECK(qa_event_ring_pending(&ring, &cursor) == 0);
  int64_t nine = 9;
  CHECK(qa_event_ring_append(&ring, &nine) == 1);
  r = ring_read(&ring, &cursor); CHECK(r != NULL && *r == 9);
  qa_event_ring_destroy(&ring);

  CHECK(qa_event_ring_init(&ring, 8, sizeof(int64_t)));
  CHECK(qa_event_ring_open_cursor(&ring, &cursor, false));
  const int64_t batch34[] = {3, 4};
  const int64_t batch12[] = {1, 2};
  qa_event_ring_admit_all(&ring, batch34, 2);
  int64_t four64 = 4, nine64 = 9;
  CHECK(qa_event_ring_some_live(&ring, is_value, &four64) == true);
  CHECK(qa_event_ring_some_live(&ring, is_value, &nine64) == false);
  qa_event_ring_prepend(&ring, batch12, 2);
  taken = (int64_t *)qa_event_ring_take(&ring, &cursor, &taken_count);
  const int64_t want1234[] = {1, 2, 3, 4};
  CHECK(taken_count == 4 && memcmp(taken, want1234, sizeof want1234) == 0);
  free(taken);
  qa_event_ring_retire(&ring);
  int64_t five64 = 5;
  qa_event_ring_admit(&ring, &five64);
  taken = (int64_t *)qa_event_ring_drain_all(&ring, &taken_count);
  CHECK(taken_count == 1 && taken[0] == 5);
  free(taken);
  CHECK(qa_event_ring_live(&ring) == 0);
  qa_event_ring_destroy(&ring);

  CHECK_ABORTS({
    qa_event_ring rr;
    qa_event_ring_init(&rr, 8, sizeof(int64_t));
    const int64_t big[] = {6, 7, 8, 9, 10, 11, 12, 13};
    int64_t v = 5;
    qa_event_ring_admit(&rr, &v);
    qa_event_ring_admit_all(&rr, big, 8);
  });
  CHECK_ABORTS({
    qa_event_ring rr;
    qa_event_ring_init(&rr, 4, sizeof(int64_t));
    qa_event_cursor cc;
    qa_event_ring_open_cursor(&rr, &cc, false);
    int64_t v = 1;
    qa_event_ring_append(&rr, &v);
    qa_event_ring_retire_before(&rr, 2);
    (void)qa_event_ring_read(&rr, &cc);
  });
  CHECK_ABORTS({
    qa_event_ring rr;
    qa_event_ring_init(&rr, 2, sizeof(int64_t));
    int64_t v = 1;
    qa_event_ring_admit(&rr, &v);
    qa_event_ring_admit(&rr, &v);
    qa_event_ring_admit(&rr, &v);
  });
}

int main(void) {
  test_binary();
  test_stamp();
  test_strings();
  test_bounded_queue();
  test_cleanup();
  test_id_index();
  test_event_ring();
  if (failures == 0) {
    printf("foundation: %d checks passed\n", checks);
    return 0;
  }
  fprintf(stderr, "foundation: %d/%d checks FAILED\n", failures, checks);
  return 1;
}
