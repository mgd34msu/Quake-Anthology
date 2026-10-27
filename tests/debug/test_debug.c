/* Port of tests/debug/shapes.test.ts cases for src/debug/shapes.ts and
 * src/debug/world.ts only. The guest-decoder and renderer cases need
 * compat/q2, guest and render modules (not in this assignment), and
 * tests/debug/settings.test.ts needs src/app/bootstrap. The
 * rereleaseDebugLifetime value 4294966296 is used as the literal the
 * TS test computes. GPL-2.0-or-later. */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/debug/shapes.h"
#include "qa/debug/world.h"
#include "qa/types/png.d.h" /* documented empty header must include cleanly */

static int failures = 0, checks = 0;
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
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__,         \
              __LINE__);                                                \
    }                                                                   \
  } while (0)

static qa_debug_line lines[1024];
static const qa_vec3 ORIGIN = {32, 0, 0};
static const qa_vec4 COLOR = {1, 0, 0, 0.5};

static qa_debug_shape shape(qa_debug_shape_kind kind) {
  qa_debug_shape s;
  memset(&s, 0, sizeof s);
  s.kind = kind;
  s.origin = ORIGIN;
  return s;
}

static void test_topology(void) {
  qa_debug_shape s = shape(QA_DEBUG_SHAPE_POINT);
  s.size = 8;
  size_t n = qa_debug_shape_lines(s, COLOR, true, lines, 1024);
  CHECK(n == 3);
  CHECK(lines[0].start.x == 28);
  CHECK(lines[0].end.x == 36);

  s = shape(QA_DEBUG_SHAPE_CYLINDER);
  s.half_height = 3;
  s.radius = 8;
  n = qa_debug_shape_lines(s, COLOR, true, lines, 1024);
  CHECK(n == 18);
  CHECK(lines[2].start.z == -3);
  CHECK(lines[2].end.z == 3);

  s = shape(QA_DEBUG_SHAPE_BOUNDS);
  s.min = ORIGIN;
  s.max = (qa_vec3){40, 8, 8};
  CHECK(qa_debug_shape_lines(s, COLOR, true, lines, 1024) == 12);

  s = shape(QA_DEBUG_SHAPE_CIRCLE);
  s.radius = 10000;
  CHECK(qa_debug_shape_lines(s, COLOR, true, lines, 1024) == 16);

  s = shape(QA_DEBUG_SHAPE_SPHERE);
  s.radius = 0;
  CHECK(qa_debug_shape_lines(s, COLOR, true, lines, 1024) == 84);
  s.radius = 10000;
  CHECK(qa_debug_shape_lines(s, COLOR, true, lines, 1024) == 608);
  /* count-only call */
  CHECK(qa_debug_shape_lines(s, COLOR, true, NULL, 0) == 608);
}

static void test_ray_arrow(void) {
  qa_debug_shape s = shape(QA_DEBUG_SHAPE_RAY);
  s.direction = (qa_vec3){2, 0, 0};
  s.length = 10;
  s.size = 2;
  size_t n = qa_debug_shape_lines(s, COLOR, false, lines, 1024);
  CHECK(n == 4);
  CHECK(lines[1].end.x == 52);
  CHECK(lines[0].depth_test == false);

  qa_vec4 cap = {0, 1, 0, 1};
  s = shape(QA_DEBUG_SHAPE_ARROW);
  s.start = ORIGIN;
  s.end = (qa_vec3){42, 0, 0};
  s.size = 2;
  s.cap_color = cap;
  n = qa_debug_shape_lines(s, COLOR, true, lines, 1024);
  CHECK(n == 4);
  CHECK(memcmp(&lines[0].color, &COLOR, sizeof COLOR) == 0);
  bool caps = true;
  for (size_t i = 1; i < n; i++)
    if (memcmp(&lines[i].color, &cap, sizeof cap) != 0) caps = false;
  CHECK(caps);

  s.end = (qa_vec3){33, 0, 0};
  n = qa_debug_shape_lines(s, COLOR, true, lines, 1024);
  CHECK(n == 3);
  CHECK(lines[0].start.x == 33);
  CHECK(lines[0].end.x == 34);
}

static size_t snap(qa_world_debug_line_store *store, double now, double frame,
                   const qa_debug_line **out) {
  size_t count = 0;
  *out = qa_world_debug_line_store_snapshot(store, now, frame, &count);
  return count;
}

static void test_store(void) {
  qa_world_debug_line_store store;
  CHECK(qa_world_debug_line_store_init(&store, 3));
  qa_vec3 mutable = {32, 0, 0};
  qa_debug_line line = {mutable, ORIGIN, COLOR, false};
  const qa_debug_line *out = NULL;
  CHECK(qa_world_debug_line_store_submit(&store, &line, 1, 1000, 25));
  mutable.x = 0;
  line.start = mutable; /* submitted copy must retain 32 */
  CHECK(snap(&store, 1000, 1, &out) == 1);
  CHECK(out[0].start.x == 32);
  CHECK(snap(&store, 1024, 2, &out) == 1);
  CHECK(snap(&store, 1025, 3, &out) == 0);

  line.start = (qa_vec3){32, 0, 0};
  CHECK(qa_world_debug_line_store_submit(&store, &line, 1, 1100, 0));
  CHECK(snap(&store, 1100, 4, &out) == 1);
  CHECK(snap(&store, 1100, 4, &out) == 1);
  CHECK(snap(&store, 1100, 5, &out) == 0);

  qa_debug_line four[4] = {line, line, line, line};
  CHECK(qa_world_debug_line_store_submit(&store, four, 4, 1200, 100));
  CHECK(snap(&store, 1200, 6, &out) == 3);
  qa_world_debug_line_store_clear(&store);
  CHECK(snap(&store, 1200, 6, &out) == 0);
  qa_world_debug_line_store_destroy(&store);
}

static void test_wrap(void) {
  qa_world_debug_line_store store;
  CHECK(qa_world_debug_line_store_init_default(&store));
  CHECK(store.capacity == 9216);
  qa_debug_line line = {ORIGIN, ORIGIN, COLOR, false};
  const qa_debug_line *out = NULL;
  /* rereleaseDebugLifetime(-1) == 4294966296 */
  CHECK(qa_world_debug_line_store_submit(&store, &line, 1, 2000, 4294966296LL));
  CHECK(snap(&store, 2000, 1, &out) == 0);
  CHECK(qa_world_debug_line_store_submit(&store, &line, 1, 0xfffffff0, 32));
  CHECK(snap(&store, 0xfffffff0, 2, &out) == 0);
  CHECK(qa_world_debug_line_store_submit(&store, &line, 1,
                                         4294967296.0 + 100, 25));
  CHECK(snap(&store, 4294967296.0 + 124, 3, &out) == 1);
  CHECK(snap(&store, 4294967296.0 + 125, 4, &out) == 0);
  CHECK(qa_world_debug_line_store_submit(&store, &line, 1, 0xfffffff0, 16));
  CHECK(snap(&store, 0xfffffff0, 5, &out) == 1);
  CHECK(snap(&store, 0xfffffff0, 5, &out) == 1);
  CHECK(snap(&store, 0xfffffff0, 6, &out) == 0);
  qa_world_debug_line_store_destroy(&store);
}

static void test_invalid(void) {
  CHECK_ABORTS(qa_world_debug_line_store s;
               qa_world_debug_line_store_init(&s, 0));
  CHECK_ABORTS(qa_world_debug_line_store s;
               qa_world_debug_line_store_init(&s, 3);
               qa_debug_line l = {ORIGIN, ORIGIN, COLOR, false};
               qa_world_debug_line_store_submit(&s, &l, 1, 0, -1));
  CHECK_ABORTS(qa_world_debug_line_store s;
               qa_world_debug_line_store_init(&s, 3);
               qa_debug_line l = {{NAN, 0, 0}, ORIGIN, COLOR, false};
               qa_world_debug_line_store_submit(&s, &l, 1, 0, 1));
  CHECK_ABORTS(qa_world_debug_line_store s;
               qa_world_debug_line_store_init(&s, 3);
               size_t n;
               qa_world_debug_line_store_snapshot(&s, INFINITY, 1, &n));
}

int main(void) {
  test_topology();
  test_ray_arrow();
  test_store();
  test_wrap();
  test_invalid();
  printf("test_debug: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
