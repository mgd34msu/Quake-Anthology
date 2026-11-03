/*
 * Port of tests/platform/native-libraries.test.ts and
 * window-headless.test.ts (dedicated lifecycle; the /proc maps check
 * has no C counterpart because the test binary links SDL2 directly),
 * plus headless-safe SDL cases under SDL_VIDEODRIVER=dummy.
 * TypeScript toThrow cases run in a child process and must abort.
 * GPL-2.0-or-later.
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

#include "qa/platform.h"

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

static bool candidates_equal(qa_native_library kind,
                             const qa_native_library_options *options,
                             const char *const *expected, size_t expected_count) {
  qa_native_library_candidates candidates;
  if (!qa_native_library_candidates_of(kind, options, &candidates)) return false;
  bool equal = candidates.count == expected_count;
  for (size_t i = 0; equal && i < expected_count; i++) {
    equal = strcmp(candidates.items[i], expected[i]) == 0;
  }
  qa_native_library_candidates_free(&candidates);
  return equal;
}

static bool candidates_contain(qa_native_library kind,
                               const qa_native_library_options *options,
                               const char *wanted) {
  qa_native_library_candidates candidates;
  if (!qa_native_library_candidates_of(kind, options, &candidates)) return false;
  bool found = false;
  for (size_t i = 0; i < candidates.count; i++) {
    if (strcmp(candidates.items[i], wanted) == 0) found = true;
  }
  qa_native_library_candidates_free(&candidates);
  return found;
}

static void test_native_libraries(void) {
  qa_native_library_options linux = {0};
  linux.platform = "linux";
  linux.exec_path = "/opt/quake/quake";
  linux.has_environment = true;
  const char *const sdl2[] = {"/opt/quake/libSDL2-2.0.so.0", "/opt/quake/libSDL2.so",
                              "libSDL2-2.0.so.0", "libSDL2.so"};
  CHECK(candidates_equal(QA_NATIVE_LIBRARY_SDL2, &linux, sdl2, 4));
  const char *const sdl3[] = {"/opt/quake/libSDL3.so.0", "/opt/quake/libSDL3.so",
                              "libSDL3.so.0", "libSDL3.so"};
  CHECK(candidates_equal(QA_NATIVE_LIBRARY_SDL3, &linux, sdl3, 4));
  CHECK(candidates_contain(QA_NATIVE_LIBRARY_GL, &linux, "libGL.so.1"));
  CHECK(candidates_contain(QA_NATIVE_LIBRARY_VORBISFILE, &linux, "libvorbisfile.so.3"));
  CHECK(candidates_contain(QA_NATIVE_LIBRARY_FREETYPE, &linux, "libfreetype.so.6"));

  qa_native_library_env override_env[] = {{"QUAKE_SDL2_LIBRARY", "/missing/libSDL2.so"}};
  qa_native_library_options overridden = {0};
  overridden.platform = "linux";
  overridden.environment = override_env;
  overridden.environment_count = 1;
  overridden.has_environment = true;
  const char *const only[] = {"/missing/libSDL2.so"};
  CHECK(candidates_equal(QA_NATIVE_LIBRARY_SDL2, &overridden, only, 1));

  qa_native_library_env empty_env[] = {{"QUAKE_SDL2_LIBRARY", ""}};
  qa_native_library_options empty = {0};
  empty.environment = empty_env;
  empty.environment_count = 1;
  empty.has_environment = true;
  CHECK_ABORTS((void)qa_native_library_candidates_of(
      QA_NATIVE_LIBRARY_SDL2, &empty, &(qa_native_library_candidates){0}));

  qa_native_library_env sdl3_env[] = {{"QUAKE_SDL3_LIBRARY", "/private/SDL3"}};
  qa_native_library_options sdl3_override = {0};
  sdl3_override.environment = sdl3_env;
  sdl3_override.environment_count = 1;
  sdl3_override.has_environment = true;
  const char *const private_sdl3[] = {"/private/SDL3"};
  CHECK(candidates_equal(QA_NATIVE_LIBRARY_SDL3, &sdl3_override, private_sdl3, 1));

  qa_native_library_options win32 = {0};
  win32.platform = "win32";
  win32.exec_path = "C:\\quake\\quake.exe";
  win32.has_environment = true;
  CHECK(candidates_contain(QA_NATIVE_LIBRARY_SDL3, &win32, "SDL3.dll"));
  CHECK(candidates_contain(QA_NATIVE_LIBRARY_SDL3, &win32, "C:\\quake\\SDL3.dll"));

  qa_native_library_options darwin = {0};
  darwin.platform = "darwin";
  darwin.exec_path = "/app/quake";
  darwin.home_directory = "/private/home";
  darwin.has_environment = true;
  CHECK(candidates_contain(QA_NATIVE_LIBRARY_SDL3, &darwin,
                           "/Library/Frameworks/SDL3.framework/SDL3"));
  CHECK(candidates_contain(QA_NATIVE_LIBRARY_SDL3, &darwin,
                           "/private/home/Library/Frameworks/SDL3.framework/SDL3"));
  CHECK(candidates_contain(QA_NATIVE_LIBRARY_SDL2, &darwin,
                           "/opt/homebrew/lib/libSDL2-2.0.0.dylib"));

  CHECK(strcmp(qa_default_open_gl_driver("linux"), "libGL.so.1") == 0);
  CHECK(strcmp(qa_default_open_gl_driver("win32"), "opengl32.dll") == 0);
  CHECK(strcmp(qa_default_open_gl_driver("darwin"),
               "/System/Library/Frameworks/OpenGL.framework/Libraries/libGL.dylib") == 0);
  CHECK_ABORTS((void)qa_default_open_gl_driver("plan9"));
  CHECK_ABORTS((void)qa_native_library_candidates_of(
      QA_NATIVE_LIBRARY_SDL2,
      &(qa_native_library_options){.platform = "plan9", .has_environment = true},
      &(qa_native_library_candidates){0}));
}

typedef struct open_probe {
  char attempted[8][256];
  char loaded[256];
  size_t count;
} open_probe;

static bool probe_open(const char *path, void *ctx) {
  open_probe *probe = (open_probe *)ctx;
  snprintf(probe->attempted[probe->count], sizeof probe->attempted[0], "%s", path);
  probe->count++;
  if (strncmp(path, "/missing/", 9) == 0) return false;
  snprintf(probe->loaded, sizeof probe->loaded, "%s", path);
  return true;
}

static bool failing_open(const char *path, void *ctx) {
  open_probe *probe = (open_probe *)ctx;
  snprintf(probe->attempted[probe->count], sizeof probe->attempted[0], "%s", path);
  probe->count++;
  return false;
}

static void test_open_native_library(void) {
  qa_native_library_options options = {0};
  options.platform = "linux";
  options.exec_path = "/missing/quake";
  options.has_environment = true;
  open_probe probe = {0};
  CHECK(qa_open_native_library(QA_NATIVE_LIBRARY_VORBISFILE, probe_open, &probe,
                               &options));
  CHECK(probe.count == 3);
  CHECK(strcmp(probe.attempted[0], "/missing/libvorbisfile.so.3") == 0);
  CHECK(strcmp(probe.attempted[1], "/missing/libvorbisfile.so") == 0);
  CHECK(strcmp(probe.attempted[2], "libvorbisfile.so.3") == 0);
  CHECK(strcmp(probe.loaded, "libvorbisfile.so.3") == 0);

  qa_native_library_env env[] = {{"QUAKE_SDL2_LIBRARY", "/missing/libSDL2.so"}};
  qa_native_library_options failing = {0};
  failing.platform = "linux";
  failing.environment = env;
  failing.environment_count = 1;
  failing.has_environment = true;
  open_probe failing_probe = {0};
  CHECK_ABORTS((void)qa_open_native_library(QA_NATIVE_LIBRARY_SDL2, failing_open,
                                            &failing_probe, &failing));
}

static void test_clipboard_bytes(void) {
  size_t length = 0;
  const uint8_t plain[] = "abc\ndef";
  uint8_t *out = qa_source_clipboard_bytes(plain, sizeof plain - 1, &length);
  CHECK(out != NULL && length == 4 && memcmp(out, "abc\0", 4) == 0);
  free(out);
  const uint8_t leading[] = "\n\rabc\rdef";
  out = qa_source_clipboard_bytes(leading, sizeof leading - 1, &length);
  CHECK(out != NULL && length == 6 && memcmp(out, "\n\rabc\0", 6) == 0);
  free(out);
  const uint8_t embedded[] = {'a', 'b', 0, 'c'};
  out = qa_source_clipboard_bytes(embedded, sizeof embedded, &length);
  CHECK(out != NULL && length == 3 && memcmp(out, "ab\0", 3) == 0);
  free(out);
  const uint8_t delimiters[] = "\b\b";
  out = qa_source_clipboard_bytes(delimiters, sizeof delimiters - 1, &length);
  CHECK(out != NULL && length == 3 && out[0] == 8 && out[1] == 8 && out[2] == 0);
  free(out);
}

static void test_display_helpers(void) {
  CHECK(qa_sdl_display_index(-1, 2) == 0);
  CHECK(qa_sdl_display_index(1, 2) == 1);
  CHECK(qa_sdl_display_index(5, 2) == 0);
  CHECK_ABORTS((void)qa_sdl_display_index(0, 0));
  CHECK_ABORTS((void)qa_sdl_display_index(1.5, 2));

  const qa_sdl_display_mode modes[] = {
      {800, 600, 32, 60}, {800, 600, 32, 75}, {1024, 768, 32, 60}, {800, 600, 32, 75}};
  qa_sdl_display_mode_request request = {800, 600, 32, 0, 0};
  CHECK(qa_sdl_matching_display_mode(modes, 4, request) == 3);
  request.min_display_refresh = 70;
  CHECK(qa_sdl_matching_display_mode(modes, 4, request) == 3);
  request.max_display_refresh = 70;
  request.min_display_refresh = 0;
  CHECK(qa_sdl_matching_display_mode(modes, 4, request) == 0);
  request.width = 640;
  CHECK(qa_sdl_matching_display_mode(modes, 4, request) == -1);
  qa_sdl_display_mode_request inverted = {800, 600, 32, 90, 30};
  CHECK_ABORTS((void)qa_sdl_matching_display_mode(modes, 4, inverted));
}

static void put_u32(uint8_t *bytes, size_t offset, uint32_t value) {
  memcpy(bytes + offset, &value, sizeof value);
}

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  memcpy(bytes + offset, &value, sizeof value);
}

static void put_f32(uint8_t *bytes, size_t offset, float value) {
  memcpy(bytes + offset, &value, sizeof value);
}

static void test_decode_event(void) {
  uint8_t text[56] = {0};
  put_u32(text, 0, 0x303);
  put_u32(text, 4, 77);
  memcpy(text + 12, "caf\xc3\xa9", 5);
  qa_sdl_event event = qa_decode_sdl_event(text, sizeof text);
  CHECK(event.kind == QA_SDL_EVENT_TEXT && event.timestamp == 77);
  CHECK(strcmp(event.as.text.text, "caf\xc3\xa9") == 0);

  uint8_t wheel[56] = {0};
  put_u32(wheel, 0, 0x403);
  put_u32(wheel, 4, 78);
  put_i32(wheel, 16, -1);
  put_i32(wheel, 20, 2);
  put_u32(wheel, 24, 1);
  put_f32(wheel, 28, -1.5f);
  put_f32(wheel, 32, 2.25f);
  event = qa_decode_sdl_event(wheel, sizeof wheel);
  CHECK(event.kind == QA_SDL_EVENT_MOUSE_WHEEL && event.timestamp == 78);
  CHECK(event.as.mouse_wheel.x == -1 && event.as.mouse_wheel.y == 2);
  CHECK(event.as.mouse_wheel.precise_x == -1.5f);
  CHECK(event.as.mouse_wheel.precise_y == 2.25f);
  CHECK(event.as.mouse_wheel.flipped);

  uint8_t key[56] = {0};
  put_u32(key, 0, 0x300);
  put_u32(key, 4, 9);
  key[13] = 1;
  put_i32(key, 16, 30);
  put_i32(key, 20, 97);
  uint16_t modifiers = 0x41;
  memcpy(key + 24, &modifiers, sizeof modifiers);
  event = qa_decode_sdl_event(key, sizeof key);
  CHECK(event.kind == QA_SDL_EVENT_KEY && event.as.key.down && event.as.key.repeat);
  CHECK(event.as.key.scancode == 30 && event.as.key.keycode == 97);
  CHECK(event.as.key.modifiers == 0x41);

  uint8_t quit[56] = {0};
  put_u32(quit, 0, 0x100);
  put_u32(quit, 4, 3);
  event = qa_decode_sdl_event(quit, sizeof quit);
  CHECK(event.kind == QA_SDL_EVENT_QUIT && event.timestamp == 3);

  uint8_t other[56] = {0};
  put_u32(other, 0, 0x999);
  event = qa_decode_sdl_event(other, sizeof other);
  CHECK(event.kind == QA_SDL_EVENT_UNSUPPORTED && event.as.unsupported.type == 0x999);

  uint8_t axis[56] = {0};
  put_u32(axis, 0, 0x600);
  put_i32(axis, 8, 7);
  axis[12] = 2;
  int16_t value = -1234;
  memcpy(axis + 16, &value, sizeof value);
  event = qa_decode_sdl_event(axis, sizeof axis);
  CHECK(event.kind == QA_SDL_EVENT_JOYSTICK_AXIS);
  CHECK(event.as.joystick_axis.instance == 7 && event.as.joystick_axis.axis == 2);
  CHECK(event.as.joystick_axis.value == -1234);
  CHECK(!qa_sdl_joystick_queue_event(&(qa_sdl_event){.kind = QA_SDL_EVENT_QUIT}));
  CHECK(qa_sdl_joystick_queue_event(&event));
}

static void test_cpu_window(void) {
  qa_sdl_window_options options = {0};
  options.title = "platform native test";
  options.width = 3;
  options.height = 2;
  options.backend = QA_SDL_BACKEND_CPU;
  options.hidden = true;
  options.resizable = true;
  qa_sdl_window window;
  CHECK(qa_sdl_window_open(&options, &window));
  CHECK(!qa_sdl_window_closed(&window));
  CHECK(window.id != 0);
  CHECK(qa_sdl_window_backend(&window) == QA_SDL_BACKEND_CPU);
  qa_sdl_size logical = qa_sdl_window_logical_size(&window);
  CHECK(logical.width == 3 && logical.height == 2);
  qa_sdl_size drawable = qa_sdl_window_drawable_size(&window);
  CHECK(drawable.width == 3 && drawable.height == 2);
  CHECK(qa_sdl_window_width(&window) == 3 && qa_sdl_window_height(&window) == 2);
  CHECK(!qa_sdl_window_fullscreen(&window));

  uint8_t frame[3 * 2 * 4];
  for (size_t i = 0; i < sizeof frame; i++) {
    /* The dummy window surface has no alpha channel; keep alpha opaque. */
    frame[i] = i % 4 == 3 ? 0xff : (uint8_t)(i * 7 + 1);
  }
  qa_sdl_window_present(&window, frame, sizeof frame);
  size_t pixel_length = 0;
  uint8_t *pixels = qa_sdl_window_read_pixels(&window, &pixel_length);
  CHECK(pixels != NULL && pixel_length == sizeof frame);
  CHECK(pixels != NULL && memcmp(pixels, frame, sizeof frame) == 0);
  free(pixels);

  size_t event_count = 999;
  qa_sdl_event *events = qa_sdl_window_poll_events(&window, &event_count);
  free(events);
  qa_sdl_event injected = {0};
  injected.kind = QA_SDL_EVENT_KEY;
  injected.timestamp = 42;
  injected.as.key.down = true;
  injected.as.key.scancode = 30;
  injected.as.key.keycode = 97;
  qa_sdl_window_push_event(&window, &injected);
  events = qa_sdl_window_poll_events(&window, &event_count);
  bool saw_key = false;
  for (size_t i = 0; i < event_count; i++) {
    if (events[i].kind == QA_SDL_EVENT_KEY && events[i].as.key.scancode == 30 &&
        events[i].as.key.down) {
      saw_key = true;
    }
  }
  free(events);
  CHECK(saw_key);

  qa_sdl_display display;
  CHECK(qa_sdl_window_display(&window, &display));
  CHECK(display.count >= 1 && display.bounds.width > 0);
  qa_sdl_display_free(&display);
  size_t mode_count = 0;
  qa_sdl_display_mode *modes = qa_sdl_window_display_modes(&window, &mode_count);
  CHECK(modes != NULL && mode_count >= 1);
  free(modes);

  qa_sdl_window_presentation presentation =
      qa_sdl_window_capture_presentation(&window);
  CHECK(presentation.size.width == 3 && presentation.size.height == 2);
  CHECK(presentation.fullscreen == QA_SDL_FULLSCREEN_WINDOWED);

  qa_sdl_input_lease *input = qa_sdl_window_begin_input(&window);
  CHECK(input != NULL && !qa_sdl_input_lease_closed(input));
  qa_sdl_input_lease_close(input);
  CHECK(qa_sdl_input_lease_closed(input));
  qa_sdl_input_lease_destroy(input);

  qa_sdl_window_close(&window);
  CHECK(qa_sdl_window_closed(&window));
  qa_sdl_window_close(&window);
  CHECK_ABORTS((void)qa_sdl_window_poll_events(&window, &(size_t){0}));
  CHECK_ABORTS(qa_sdl_window_set_size(&window, 4, 4));
}

static void test_window_validation(void) {
  const double widths[] = {0, -1, 1.5, NAN, INFINITY, 16385};
  for (size_t i = 0; i < sizeof widths / sizeof widths[0]; i++) {
    qa_sdl_window_options bad = {0};
    bad.title = "test";
    bad.width = widths[i];
    bad.height = 2;
    bad.backend = QA_SDL_BACKEND_CPU;
    CHECK_ABORTS((void)qa_sdl_window_open(&bad, &(qa_sdl_window){0}));
  }
}

int main(void) {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  test_native_libraries();
  test_open_native_library();
  test_clipboard_bytes();
  test_display_helpers();
  test_decode_event();
  test_window_validation();
  test_cpu_window();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
