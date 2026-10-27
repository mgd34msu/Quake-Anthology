/*
 * Port of the capture cases in tests/capture/application.test.ts:
 * that integration test drives a full application and checks the
 * saved PNG/TGA frames decode non-blank and the levelshot is
 * 128x128. The C port has no application yet, so this checks the
 * same exported capture surface (encodeScreenshot, makeLevelshot
 * and FrameCapture's named/auto-numbered screenshot and levelshot
 * file writes, from src/capture/index.ts) on synthetic frames:
 * TGA/PNG/JPEG headers and sizes, the 512x384 -> 128x128 averaging
 * grid, and files written under a scratch capture root.
 * GPL-2.0-or-later.
 */
#define _POSIX_C_SOURCE 200809L
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

#include "qa/capture/index.h"

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

static qa_image_level uniform_frame(uint8_t *pixels, size_t length, double width,
                                    double height, uint8_t r, uint8_t g, uint8_t b) {
  for (size_t i = 0; i + 3 < length; i += 4) {
    pixels[i] = r;
    pixels[i + 1] = g;
    pixels[i + 2] = b;
    pixels[i + 3] = 255;
  }
  qa_image_level frame;
  frame.width = width;
  frame.height = height;
  frame.pixels.bytes = pixels;
  frame.pixels.length = length;
  return frame;
}

static void test_levelshot_uniform(void) {
  static uint8_t pixels[512 * 384 * 4];
  qa_image_level frame = uniform_frame(pixels, sizeof pixels, 512, 384, 120, 60, 30);
  qa_image_level shot;
  CHECK(qa_make_levelshot(&frame, NULL, 0, &shot));
  CHECK(shot.width == 128 && shot.height == 128);
  CHECK(shot.pixels.length == 128 * 128 * 4);
  bool uniform = true;
  for (size_t i = 0; i + 3 < shot.pixels.length; i += 4) {
    if (shot.pixels.bytes[i] != 120 || shot.pixels.bytes[i + 1] != 60 ||
        shot.pixels.bytes[i + 2] != 30 || shot.pixels.bytes[i + 3] != 255) {
      uniform = false;
    }
  }
  CHECK(uniform);
  free((uint8_t *)shot.pixels.bytes);
}

/* On an exactly 512x384 frame each texel averages one 4x3 block. */
static void test_levelshot_blocks(void) {
  static uint8_t pixels[512 * 384 * 4];
  for (int y = 0; y < 384; y++) {
    for (int x = 0; x < 512; x++) {
      uint8_t *pixel = pixels + ((size_t)y * 512 + (size_t)x) * 4;
      pixel[0] = (uint8_t)(x / 4);        /* 0..127 across */
      pixel[1] = (uint8_t)(y / 3);        /* 0..127 down */
      pixel[2] = (uint8_t)((x / 4 + y / 3) / 2);
      pixel[3] = 7; /* input alpha is dropped; output is opaque */
    }
  }
  qa_image_level frame;
  frame.width = 512;
  frame.height = 384;
  frame.pixels.bytes = pixels;
  frame.pixels.length = sizeof pixels;
  qa_image_level shot;
  CHECK(qa_make_levelshot(&frame, NULL, 0, &shot));
  bool blocks = true;
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) {
      const uint8_t *pixel = shot.pixels.bytes + ((size_t)y * 128 + (size_t)x) * 4;
      if (pixel[0] != (uint8_t)x || pixel[1] != (uint8_t)y ||
          pixel[2] != (uint8_t)((x + y) / 2) || pixel[3] != 255) {
        blocks = false;
      }
    }
  }
  CHECK(blocks);
  /* An inverting gamma table maps every averaged channel. */
  uint8_t gamma[256];
  for (int i = 0; i < 256; i++) gamma[i] = (uint8_t)(255 - i);
  qa_image_level inverted;
  CHECK(qa_make_levelshot(&frame, gamma, 256, &inverted));
  const uint8_t *first = inverted.pixels.bytes;
  CHECK(first[0] == 255 && first[1] == 255 && first[2] == 255 && first[3] == 255);
  const uint8_t *second = inverted.pixels.bytes + 4;
  CHECK(second[0] == 254 && second[1] == 255 && second[2] == 255);
  free((uint8_t *)shot.pixels.bytes);
  free((uint8_t *)inverted.pixels.bytes);
}

static void test_levelshot_average_truncates(void) {
  /* One texel of a 512x384 frame: reds sum to 25 over the 12 samples,
   * so the texel is trunc(25/12) == 2. */
  static uint8_t pixels[512 * 384 * 4];
  qa_image_level frame = uniform_frame(pixels, sizeof pixels, 512, 384, 0, 0, 0);
  pixels[0] = 25;
  qa_image_level shot;
  CHECK(qa_make_levelshot(&frame, NULL, 0, &shot));
  CHECK(shot.pixels.bytes[0] == 2);
  CHECK(shot.pixels.bytes[4] == 0);
  free((uint8_t *)shot.pixels.bytes);
}

static void test_encode_screenshot(void) {
  uint8_t pixels[2 * 2 * 4];
  qa_image_level frame = uniform_frame(pixels, sizeof pixels, 2, 2, 10, 20, 30);
  size_t length = 0;
  uint8_t *tga = qa_encode_screenshot(&frame, QA_SCREENSHOT_TGA, 90, &length);
  CHECK(tga != NULL);
  CHECK(length == 18 + sizeof pixels);
  if (tga != NULL) {
    CHECK(tga[2] == 2);                              /* uncompressed true-color */
    CHECK(tga[12] == 2 && tga[13] == 0);             /* width, little-endian */
    CHECK(tga[14] == 2 && tga[15] == 0);             /* height */
    CHECK(tga[16] == 32 && tga[17] == 40);
    CHECK(tga[18] == 30 && tga[19] == 20 && tga[20] == 10 && tga[21] == 255); /* BGRA */
  }
  free(tga);
  uint8_t *png = qa_encode_screenshot(&frame, QA_SCREENSHOT_PNG, 90, &length);
  CHECK(png != NULL && length > 8);
  if (png != NULL) {
    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    CHECK(memcmp(png, signature, sizeof signature) == 0);
  }
  free(png);
  uint8_t *jpg = qa_encode_screenshot(&frame, QA_SCREENSHOT_JPG, 90, &length);
  CHECK(jpg != NULL && length > 2);
  if (jpg != NULL) CHECK(jpg[0] == 0xff && jpg[1] == 0xd8); /* SOI */
  free(jpg);
}

static uint8_t capture_pixels[4 * 2 * 4];
static qa_image_level read_capture_frame(void *ctx) {
  (void)ctx;
  return uniform_frame(capture_pixels, sizeof capture_pixels, 4, 2, 200, 100, 50);
}

/* Whole file bytes; NULL when unreadable. */
static uint8_t *read_whole_file(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  uint8_t *bytes = size > 0 ? malloc((size_t)size) : NULL;
  size_t length = bytes == NULL ? 0 : fread(bytes, 1, (size_t)size, file);
  fclose(file);
  *out_length = length;
  return bytes;
}

static bool ends_with(const char *text, const char *suffix) {
  size_t text_length = strlen(text), suffix_length = strlen(suffix);
  return text_length >= suffix_length &&
         strcmp(text + text_length - suffix_length, suffix) == 0;
}

static void remove_tree(const char *path) {
  char command[1024];
  snprintf(command, sizeof command, "rm -rf '%s'", path);
  (void)system(command);
}

static void test_frame_capture_writes(void) {
  char root_template[] = "/tmp/qa-capture-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  if (root == NULL) return;
  qa_frame_readback readback = {NULL, read_capture_frame};
  qa_frame_capture capture;
  CHECK(qa_frame_capture_init(&capture, root, readback));
  size_t length = 0;

  /* Named screenshot: screenshots/<name>.<format> via writeAtomic,
   * with the source frame dimensions and PNG bytes on disk. */
  qa_capture_result named;
  CHECK(qa_frame_capture_screenshot(&capture, QA_SCREENSHOT_PNG, "actual", 90, &named));
  CHECK(ends_with(named.path, "/screenshots/actual.png"));
  CHECK(named.width == 4 && named.height == 2);
  uint8_t *saved = read_whole_file(named.path, &length);
  CHECK(saved != NULL && (double)length == named.byte_length);
  if (saved != NULL && length > 8) {
    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    CHECK(memcmp(saved, signature, sizeof signature) == 0);
  }
  free(saved);
  qa_capture_result_free(&named);

  /* Unnamed screenshots take the first free shotNNNN exclusively. */
  qa_capture_result first;
  CHECK(qa_frame_capture_screenshot(&capture, QA_SCREENSHOT_TGA, NULL, 90, &first));
  CHECK(ends_with(first.path, "/screenshots/shot0000.tga"));
  CHECK(first.byte_length == 18 + sizeof capture_pixels);
  qa_capture_result second;
  CHECK(qa_frame_capture_screenshot(&capture, QA_SCREENSHOT_TGA, NULL, 90, &second));
  CHECK(ends_with(second.path, "/screenshots/shot0001.tga"));
  saved = read_whole_file(first.path, &length);
  CHECK(saved != NULL && length == 18 + sizeof capture_pixels);
  if (saved != NULL && length > 21) {
    CHECK(saved[12] == 4 && saved[14] == 2); /* 4x2 */
    CHECK(saved[18] == 50 && saved[19] == 100 && saved[20] == 200); /* BGRA */
  }
  free(saved);
  qa_capture_result_free(&first);
  qa_capture_result_free(&second);

  /* Levelshot: "maps/base1.bsp" -> levelshots/base1.tga at 128x128. */
  qa_capture_result level;
  CHECK(qa_frame_capture_levelshot(&capture, "maps/base1.bsp", NULL, 0, &level));
  CHECK(ends_with(level.path, "/levelshots/base1.tga"));
  CHECK(level.width == 128 && level.height == 128);
  CHECK(level.byte_length == 18 + 128 * 128 * 4);
  saved = read_whole_file(level.path, &length);
  CHECK(saved != NULL && (double)length == level.byte_length);
  free(saved);
  qa_capture_result_free(&level);

  qa_frame_capture_destroy(&capture);
  remove_tree(root);
}

static void test_invalid_frames_abort(void) {
  uint8_t pixels[4 * 4];
  qa_image_level good = uniform_frame(pixels, sizeof pixels, 2, 2, 0, 0, 0);
  qa_image_level short_frame = good;
  short_frame.pixels.length = 4; /* not width*height*4 */
  qa_image_level fractional = good;
  fractional.width = 1.5;
  fractional.pixels.length = sizeof pixels;
  qa_image_level empty = good;
  empty.width = 0;
  empty.pixels.length = 0;
  size_t length = 0;
  CHECK_ABORTS((void)qa_encode_screenshot(&short_frame, QA_SCREENSHOT_TGA, 90, &length));
  CHECK_ABORTS((void)qa_encode_screenshot(&fractional, QA_SCREENSHOT_PNG, 90, &length));
  qa_image_level shot;
  CHECK_ABORTS((void)qa_make_levelshot(&empty, NULL, 0, &shot));
  uint8_t gamma[255] = {0};
  CHECK_ABORTS((void)qa_make_levelshot(&good, gamma, sizeof gamma, &shot));
}

int main(void) {
  test_levelshot_uniform();
  test_levelshot_blocks();
  test_levelshot_average_truncates();
  test_encode_screenshot();
  test_frame_capture_writes();
  test_invalid_frames_abort();
  printf("test_capture: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
