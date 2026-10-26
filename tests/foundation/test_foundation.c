/*
 * Port of tests/foundation/primitives.test.ts and event-ring.test.ts.
 * Vector-store expectations are bit patterns produced by the TypeScript
 * implementation (see docs/PORTING.md); core/math cross-checks land with
 * the core port. TypeScript `toThrow(RangeError)` cases run in a child
 * process and must abort. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static uint32_t fbits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  return bits;
}

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

static void test_arena_pool(void) {
  qa_byte_arena arena;
  CHECK(qa_byte_arena_init(&arena, 16));
  CHECK(qa_byte_arena_alloc(&arena, 3, 1) == 0);
  CHECK(qa_byte_arena_alloc(&arena, 4, 4) == 4);
  CHECK(qa_byte_arena_alloc(&arena, 100, 1) == -1);
  CHECK(arena.overflows == 1);
  const uint8_t two[] = {7, 8};
  int64_t offset = qa_byte_arena_alloc_copy(&arena, two, 2, 1);
  uint8_t *view = qa_byte_arena_view(&arena, offset, 2);
  CHECK(view[0] == 7 && view[1] == 8);
  qa_byte_arena_reset(&arena);
  CHECK(arena.used == 0);
  CHECK(qa_byte_arena_alloc(&arena, 16, 1) == 0);
  CHECK(arena.capacity == 16);
  qa_byte_arena_destroy(&arena);
  CHECK_ABORTS({ qa_byte_arena bad; qa_byte_arena_init(&bad, 0); });

  qa_fixed_pool pool;
  CHECK(qa_fixed_pool_init(&pool, 2, sizeof(int), NULL, NULL));
  int32_t first = qa_fixed_pool_take_slot(&pool);
  int32_t second = qa_fixed_pool_take_slot(&pool);
  CHECK(first == 0 && second == 1);
  CHECK(qa_fixed_pool_take_slot(&pool) == -1);
  CHECK(pool.overflows == 1);
  *(int *)qa_fixed_pool_at(&pool, first) = 42;
  CHECK(*(int *)qa_fixed_pool_at(&pool, first) == 42);
  qa_fixed_pool_release(&pool, first);
  CHECK(qa_fixed_pool_take_slot(&pool) == first);
  CHECK(pool.peak == 2);
  qa_fixed_pool_reset(&pool);
  CHECK(pool.active == 0);
  CHECK(qa_fixed_pool_is_leased(&pool, second) == false);
  qa_fixed_pool_destroy(&pool);
  CHECK_ABORTS({
    qa_fixed_pool p;
    qa_fixed_pool_init(&p, 1, sizeof(int), NULL, NULL);
    int32_t s = qa_fixed_pool_take_slot(&p);
    qa_fixed_pool_release(&p, s);
    qa_fixed_pool_release(&p, s);
  });
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

static void test_handles_id_index(void) {
  int64_t key = qa_pack_actor_key(495, 7);
  CHECK(qa_actor_key_slot(key) == 495);
  CHECK(qa_actor_key_generation(key) == 7);
  CHECK(qa_pack_actor_key(1, 0) == 1);
  CHECK(qa_actor_key_slot(QA_INVALID_ACTOR_KEY) == -1);
  CHECK(qa_actor_key_generation(QA_INVALID_ACTOR_KEY) == -1);
  CHECK(qa_is_valid_actor_key(key));
  CHECK(!qa_is_valid_actor_key(-1));
  CHECK_ABORTS((void)qa_pack_actor_key(1 << 20, 0));

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

static const uint32_t oracle_a[4][3] = {
    {0x446c426cu, 0x444cae70u, 0x440b61a4u},
    {0xc382f370u, 0x442f319cu, 0xc3216080u},
    {0xc310beb0u, 0x441a1938u, 0xc38be868u},
    {0xc4132fb0u, 0xc211dfc0u, 0xc452ccd8u}};
static const uint32_t oracle_b[4][3] = {
    {0x441a1938u, 0xc38be868u, 0xc4132fb0u},
    {0xc211dfc0u, 0xc452ccd8u, 0x447300fcu},
    {0x43025f00u, 0x441646b4u, 0xc39477d0u},
    {0x4467412cu, 0x42b37180u, 0x4476ae64u}};
static const uint32_t oracle_dot[4] = {0x46c4bde0u, 0xc93439bcu, 0x48d445c6u, 0xc9a87138u};
static const uint32_t oracle_cross[4][3] = {
    {0xc89f3009u, 0x495bbc9eu, 0xc93bc460u},
    {0x49051432u, 0x487e5a73u, 0x48709e03u},
    {0xc667bc50u, 0xc79b31ecu, 0xc82371b3u},
    {0x471af500u, 0xc84261a8u, 0xc6952254u}};
static const uint32_t oracle_ma[4][3] = {
    {0x4489645du, 0x443b3163u, 0x43cd2b70u},
    {0xc387826eu, 0x43f4fcccu, 0x42a340f8u},
    {0xc2e04de0u, 0x443faae5u, 0xc3b1065cu},
    {0xc3b2becau, 0xc1609c00u, 0xc415213fu}};
static const uint32_t oracle_norm[4][3] = {
    {0x3f30b755u, 0x3f1918b7u, 0x3ed081f1u},
    {0xbeaf3634u, 0x3f6a6891u, 0xbe57ebeeu},
    {0xbe561dbeu, 0x3f63f3e4u, 0xbecef5feu},
    {0xbf1276e1u, 0xbd112897u, 0xbf51c429u}};

static void test_vec(void) {
  double values[3 * 40];
  samples(values, 3 * 40);
  const size_t len = 8 * 3;
  float *store = qa_create_vec3_store(8);
  CHECK(store != NULL);
  for (int index = 0; index < 4; index++) {
    double ax = values[index * 3], ay = values[index * 3 + 1], az = values[index * 3 + 2];
    double bx = values[(index * 3 + 7) % 120];
    double by = values[(index * 3 + 8) % 120];
    double bz = values[(index * 3 + 9) % 120];
    qa_vec3_set(store, len, 0, ax, ay, az);
    qa_vec3_set(store, len, 1, bx, by, bz);
    CHECK(fbits(store[0]) == oracle_a[index][0]);
    CHECK(fbits(store[1]) == oracle_a[index][1]);
    CHECK(fbits(store[2]) == oracle_a[index][2]);
    CHECK(fbits(store[3]) == oracle_b[index][0]);
    CHECK(fbits(qa_vec3_dot(store, len, 0, 1)) == oracle_dot[index]);
    qa_vec3_cross(store, len, 2, 0, 1);
    CHECK(fbits(store[6]) == oracle_cross[index][0]);
    CHECK(fbits(store[7]) == oracle_cross[index][1]);
    CHECK(fbits(store[8]) == oracle_cross[index][2]);
    qa_vec3_ma(store, len, 3, 0, 0.25, 1);
    CHECK(fbits(store[9]) == oracle_ma[index][0]);
    CHECK(fbits(store[10]) == oracle_ma[index][1]);
    CHECK(fbits(store[11]) == oracle_ma[index][2]);
    qa_vec3_normalize(store, len, 4, 0);
    CHECK(fbits(store[12]) == oracle_norm[index][0]);
    CHECK(fbits(store[13]) == oracle_norm[index][1]);
    CHECK(fbits(store[14]) == oracle_norm[index][2]);
  }
  qa_vec3_set(store, len, 0, 15, 40, -70);
  qa_vec3_angle_vectors(store, len, 1, 2, 3, 0);
  CHECK(fbits(store[3]) == 0x3f3d6cd8u);
  CHECK(fbits(store[4]) == 0x3f1ef254u);
  CHECK(fbits(store[5]) == 0xbe8483eeu);
  CHECK(fbits(store[6]) == 0x3ecff3b6u);
  CHECK(fbits(store[7]) == 0xbdd8698cu);
  CHECK(fbits(store[8]) == 0x3f685d48u);
  CHECK(fbits(store[9]) == 0xbf094528u);
  CHECK(fbits(store[10]) == 0x3f46d8e0u);
  CHECK(fbits(store[11]) == 0x3ea925bfu);
  qa_vec3_set(store, len, 0, -0.0, 0, 0);
  qa_vec3_normalize(store, len, 1, 0);
  CHECK(fbits(store[3]) == 0x80000000u);
  CHECK(qa_vec3_equal(store, len, 0, 1) == true);
  free(store);

  const size_t blen = 2 * 6;
  float *bounds = qa_create_bounds_store(2);
  CHECK(bounds != NULL);
  qa_bounds_set(bounds, blen, 0, 0, 0, 0, 10, 10, 10);
  qa_bounds_set(bounds, blen, 1, 10, 20, 0, 30, 30, 30);
  CHECK(qa_bounds_overlap(bounds, blen, 0, 1) == false);
  qa_bounds_set(bounds, blen, 1, 10, 5, 0, 30, 30, 30);
  CHECK(qa_bounds_overlap(bounds, blen, 0, 1) == true);
  CHECK(qa_bounds_contains_point(bounds, blen, 0, 10, 10, 10) == true);
  CHECK(qa_bounds_contains_point(bounds, blen, 0, 11, 10, 10) == false);
  free(bounds);
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

static void test_sys_queue(void) {
  qa_sys_event_queue queue;
  CHECK(qa_sys_event_queue_init(&queue, 4, 64));
  qa_sys_event_cursor keys, packets;
  CHECK(qa_sys_event_queue_open_cursor(&queue, &keys));
  CHECK(qa_sys_event_queue_open_cursor(&queue, &packets));
  CHECK(qa_sys_event_queue_admit(&queue, QA_SYS_PACKET, 65) == QA_SYS_OVERSIZE);
  CHECK(qa_sys_event_queue_oversize(&queue, QA_SYS_PACKET) == 1);
  CHECK(qa_sys_event_queue_frame(&queue, 1000000) == QA_SYS_ACCEPTED);
  const uint8_t payload[] = {9, 8, 7};
  CHECK(qa_sys_event_queue_push(&queue, QA_SYS_PACKET, 2000000, 1, 2, 3, 4,
                                payload, 3, NULL, 0) == QA_SYS_ACCEPTED);
  CHECK(qa_sys_event_queue_push(&queue, QA_SYS_KEY, 3000000, 32, 0, 1, 0,
                                NULL, 0, NULL, 0) == QA_SYS_ACCEPTED);
  qa_sys_event_slot *slot;
  slot = qa_sys_event_queue_read(&queue, &keys);
  CHECK(slot != NULL && slot->kind == QA_SYS_TIME);
  qa_sys_event_queue_consume(&queue, &keys);
  slot = qa_sys_event_queue_read(&queue, &keys);
  CHECK(slot != NULL && slot->kind == QA_SYS_PACKET);
  qa_sys_event_queue_consume(&queue, &keys);
  slot = qa_sys_event_queue_read(&queue, &keys);
  CHECK(slot != NULL && slot->kind == QA_SYS_KEY && slot->a == 32);
  qa_sys_event_queue_consume(&queue, &keys);
  slot = qa_sys_event_queue_read(&queue, &packets);
  CHECK(slot != NULL && slot->kind == QA_SYS_TIME);
  qa_sys_event_queue_consume(&queue, &packets);
  slot = qa_sys_event_queue_read(&queue, &packets);
  CHECK(slot != NULL && slot->kind == QA_SYS_PACKET);
  size_t payload_length = 0;
  const uint8_t *bytes = qa_sys_event_queue_payload_of(&queue, slot, &payload_length);
  CHECK(payload_length == 3 && bytes[0] == 9 && bytes[1] == 8 && bytes[2] == 7);
  qa_sys_event_queue_consume(&queue, &packets);
  qa_sys_event_queue_consume(&queue, &packets);
  qa_sys_queue_stats stats = qa_sys_event_queue_stats_of(&queue);
  CHECK(stats.accepted == 3 && stats.consumed == 6 && stats.records == 0 && stats.bytes == 0);
  qa_sys_event_queue_destroy(&queue);

  CHECK(qa_sys_event_queue_init(&queue, 2, 16));
  qa_sys_event_cursor cursor;
  CHECK(qa_sys_event_queue_open_cursor(&queue, &cursor));
  CHECK(qa_sys_event_queue_push(&queue, QA_SYS_QUIT, 0, 0, 0, 0, 0,
                                NULL, 0, NULL, 0) == QA_SYS_ACCEPTED);
  CHECK(qa_sys_event_queue_quit_requested(&queue) == true);
  CHECK(qa_sys_event_queue_pending(&queue, QA_SYS_QUIT) == true);
  CHECK(qa_sys_event_queue_push(&queue, QA_SYS_KEY, 0, 1, 0, 0, 0,
                                NULL, 0, NULL, 0) == QA_SYS_ACCEPTED);
  CHECK(qa_sys_event_queue_push(&queue, QA_SYS_KEY, 0, 2, 0, 0, 0,
                                NULL, 0, NULL, 0) == QA_SYS_FULL);
  CHECK(qa_sys_event_queue_stats_of(&queue).full_records == 1);
  slot = qa_sys_event_queue_read(&queue, &cursor);
  CHECK(slot != NULL && slot->kind == QA_SYS_QUIT);
  qa_sys_event_queue_reset(&queue);
  CHECK(qa_sys_event_queue_quit_requested(&queue) == false);
  CHECK(qa_sys_event_queue_live(&queue) == 0);
  qa_sys_event_queue_destroy(&queue);
}

int main(void) {
  test_binary();
  test_stamp();
  test_strings();
  test_arena_pool();
  test_bounded_queue();
  test_cleanup();
  test_handles_id_index();
  test_vec();
  test_event_ring();
  test_sys_queue();
  if (failures == 0) {
    printf("foundation: %d checks passed\n", checks);
    return 0;
  }
  fprintf(stderr, "foundation: %d/%d checks FAILED\n", failures, checks);
  return 1;
}
