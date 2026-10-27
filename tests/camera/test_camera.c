/* Port of the spline-only cases in tests/camera/spline.test.ts.
 * The command-dispatch case needs src/camera/application.ts (not in
 * this assignment). Deep TS toEqual on definitions is checked by
 * serialize->parse->serialize string equality plus field spot checks.
 * GPL-2.0-or-later. */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/camera/spline.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_CLOSE(a, b) CHECK(fabs((a) - (b)) < 1e-5)

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

static const char *FILE_TEXT =
    "cameraPathDef { time 2\n"
    "camera_interpolated { name \"dolly\" startPos ( 0 0 0 ) endPos ( 20 0 0 ) }\n"
    "target_fixed { name \"look\" pos ( 20 20 0 ) }\n"
    "event { type 4 param \"look\" time 0 }\n"
    "event { type 1 param \"1\" time 500 }\n"
    "event { type 9 param \"\" time 2500 }\n"
    "fov { fov 90 startFOV 90 endFOV 60 time 2000 }\n"
    "}";

static bool roundtrips(const qa_camera_definition *definition) {
  char *text = qa_serialize_camera(definition);
  if (text == NULL) return false;
  qa_camera_definition again;
  qa_parse_camera(text, &again);
  char *text2 = qa_serialize_camera(&again);
  bool eq = text2 != NULL && strcmp(text, text2) == 0 &&
            again.seconds == definition->seconds &&
            again.target_count == definition->target_count &&
            again.event_count == definition->event_count;
  free(text);
  free(text2);
  qa_camera_definition_destroy(&again);
  return eq;
}

static void test_serialization_and_playback(void) {
  qa_camera_definition camera;
  qa_parse_camera(FILE_TEXT, &camera);
  CHECK(roundtrips(&camera));
  CHECK(camera.seconds == 2);
  CHECK(camera.position.kind == QA_CAMERA_POSITION_INTERPOLATED);
  CHECK(camera.target_count == 1 && camera.event_count == 3);
  CHECK(strcmp(camera.position.name, "dolly") == 0);

  qa_camera_playback playback;
  qa_camera_sample sample;
  CHECK(qa_camera_playback_init(&playback, &camera, 100));
  CHECK(qa_camera_playback_sample(&playback, 100, &sample));
  CHECK(sample.origin.x == 0);
  CHECK(qa_camera_playback_sample(&playback, 350, &sample));
  CHECK(sample.origin.x == 2.5);
  CHECK(qa_camera_playback_sample(&playback, 600, &sample));
  CHECK(sample.origin.x == 2.5);
  CHECK(qa_camera_playback_sample(&playback, 1100, &sample));
  CHECK(sample.origin.x == 2.5);
  CHECK(qa_camera_playback_sample(&playback, 1100, &sample));
  CHECK(sample.event_count == 0);
  CHECK(qa_camera_playback_sample(&playback, 1850, &sample));
  CHECK(sample.origin.x == 10);
  CHECK(qa_camera_playback_sample(&playback, 2100, &sample));
  CHECK(sample.fov == 60);
  CHECK(!qa_camera_playback_sample(&playback, 2600, &sample));
  CHECK(!qa_camera_playback_sample(&playback, 2700, &sample));
  qa_camera_playback_destroy(&playback);
  qa_camera_definition_destroy(&camera);
}

static void test_spline(void) {
  qa_camera_definition definition;
  qa_parse_camera(
      "cameraPathDef { time 1 camera_spline { name path target { "
      "granularity 0.5 ( 0 0 0 ) ( 6 0 0 ) ( 12 0 0 ) ( 18 0 0 ) } } }",
      &definition);
  CHECK(roundtrips(&definition));
  qa_camera_playback playback;
  qa_camera_sample sample;
  CHECK(qa_camera_playback_init(&playback, &definition, 0));
  CHECK(qa_camera_playback_sample(&playback, 0, &sample));
  CHECK_CLOSE(sample.origin.x, 6);
  CHECK(qa_camera_playback_sample(&playback, 500, &sample));
  CHECK_CLOSE(sample.origin.x, 9);
  CHECK(qa_camera_playback_sample(&playback, 1000, &sample));
  CHECK_CLOSE(sample.origin.x, 12);
  /* Monotonic clock RangeError aborts in a child. */
  qa_camera_playback_destroy(&playback);
  CHECK_ABORTS(
      qa_camera_definition d;
      qa_parse_camera(
          "cameraPathDef { time 1 camera_spline { name path target { "
          "granularity 0.5 ( 0 0 0 ) ( 6 0 0 ) ( 12 0 0 ) ( 18 0 0 ) } } }",
          &d);
      qa_camera_playback p; qa_camera_sample sm;
      qa_camera_playback_init(&p, &d, 0);
      qa_camera_playback_sample(&p, 1000, &sm);
      qa_camera_playback_sample(&p, 900, &sm));
  qa_camera_definition_destroy(&definition);
}

static void test_bad_files(void) {
  CHECK_ABORTS(qa_camera_definition d;
               qa_parse_camera(
                   "cameraPathDef { time 2 camera_interpolated { name "
                   "\"dolly\" startPos ( 0 0 0 ) endPos ( 20 0 0 ) } "
                   "target_fixed { name \"look\" pos ( 20 20 0 ) } event { "
                   "type 4 param \"missing\" time 0 } }",
                   &d));
  CHECK_ABORTS(qa_camera_definition d;
               qa_parse_camera(
                   "cameraPathDef { time NaN camera_fixed { pos ( 0 0 0 ) } }",
                   &d));
  CHECK_ABORTS(qa_camera_definition d;
               qa_parse_camera(
                   "cameraPathDef { time 1 camera_spline { target { "
                   "granularity 0 ( 0 0 0 ) ( 0 0 0 ) ( 0 0 0 ) ( 0 0 0 ) "
                   "} } }",
                   &d));
  CHECK_ABORTS(qa_camera_definition d; qa_parse_camera("", &d));
}

int main(void) {
  test_serialization_and_playback();
  test_spline();
  test_bad_files();
  printf("test_camera: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
