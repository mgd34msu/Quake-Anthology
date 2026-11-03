/*
 * Confirmation-gate review checks for src/capture. Each expected value comes
 * from running the TypeScript under bun. GPL-2.0-or-later.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/capture/index.h"
#include "qa/foundation/support.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static uint8_t pixels[32];

static qa_image_level read_frame(void *ctx) {
  (void)ctx;
  qa_image_level image;
  memset(&image, 0, sizeof image);
  image.width = 4;
  image.height = 2;
  image.pixels.bytes = pixels;
  image.pixels.length = sizeof pixels;
  return image;
}

static bool ends_with(const char *text, const char *suffix) {
  size_t a = strlen(text), b = strlen(suffix);
  return a >= b && strcmp(text + a - b, suffix) == 0;
}

typedef struct shot_job {
  qa_frame_capture *capture;
} shot_job;
static void shot_run(void *raw) {
  shot_job *job = raw;
  qa_capture_result result;
  (void)qa_frame_capture_screenshot(job->capture, QA_SCREENSHOT_JPG, NULL, 90, &result);
  qa_capture_result_free(&result);
}

/* bun: shot0000.tga, shot0001.tga; after deleting shot0000 the next tga is
 * shot0002 (nextShot), and png starts at shot0000. With nextShot at 10000 the
 * screenshot rejects "No free screenshot filename between shot0000 and
 * shot9999". C rescanned from shot0000 and aborted on the error. */
static void test_next_shot(void) {
  char root_template[] = "/tmp/qa-capture-review-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  if (root == NULL) return;
  qa_frame_capture capture;
  CHECK(qa_frame_capture_init(&capture, root, (qa_frame_readback){NULL, read_frame}));
  qa_capture_result a, b, c, d;
  CHECK(qa_frame_capture_screenshot(&capture, QA_SCREENSHOT_TGA, NULL, 90, &a));
  CHECK(qa_frame_capture_screenshot(&capture, QA_SCREENSHOT_TGA, NULL, 90, &b));
  CHECK(ends_with(a.path, "/screenshots/shot0000.tga"));
  CHECK(ends_with(b.path, "/screenshots/shot0001.tga"));
  CHECK(unlink(a.path) == 0);
  CHECK(qa_frame_capture_screenshot(&capture, QA_SCREENSHOT_TGA, NULL, 90, &c));
  CHECK(ends_with(c.path, "/screenshots/shot0002.tga"));
  CHECK(qa_frame_capture_screenshot(&capture, QA_SCREENSHOT_PNG, NULL, 90, &d));
  CHECK(ends_with(d.path, "/screenshots/shot0000.png"));
  capture.next_shot[QA_SCREENSHOT_JPG] = 10000;
  shot_job job = {&capture};
  qa_caught_error caught;
  CHECK(!qa_try(shot_run, &job, &caught));
  CHECK(strcmp(caught.message, "No free screenshot filename between shot0000 and shot9999") == 0);
  qa_capture_result_free(&a);
  qa_capture_result_free(&b);
  qa_capture_result_free(&c);
  qa_capture_result_free(&d);
  qa_frame_capture_destroy(&capture);
  char command[256];
  snprintf(command, sizeof command, "rm -rf '%s'", root);
  (void)system(command);
}

int main(void) {
  test_next_shot();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
