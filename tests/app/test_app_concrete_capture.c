/*
 * ApplicationCapture (src/app/bootstrap/capture.ts) over the real console commands, FrameCapture and a CPU
 * NativeRenderer: the same scenario as tests/app/oracles/app_concrete_capture.ts, whose bun transcript
 * (tests/app/oracles/app_concrete_capture.txt) this test reproduces line for line. Every services callback
 * checks its sentinel context. GPL-2.0-or-later.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/app/bootstrap/capture.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/foundation/support.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)

static char transcript[16384];
static char root[512];
static void out(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *format, ...) {
  char line[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  strncat(transcript, line, sizeof transcript - strlen(transcript) - 2);
  strcat(transcript, "\n");
}
/* The oracle's show(): root replaced by <root>, newlines escaped. */
static void show(const char *text, char *buffer, size_t capacity) {
  size_t at = 0, root_length = strlen(root);
  for (const char *p = text; *p != '\0' && at + 8 < capacity;) {
    if (strncmp(p, root, root_length) == 0) { at += (size_t)snprintf(buffer + at, capacity - at, "<root>"); p += root_length; continue; }
    if (*p == '\n') { buffer[at++] = '\\'; buffer[at++] = 'n'; p++; continue; }
    buffer[at++] = *p++;
  }
  buffer[at] = '\0';
}

static int sentinel;
static int buffer_sentinel;
typedef struct services_state {
  qa_seat_id seat;
  qa_seat_console console;
  int writes;
} services_state;
static services_state state;

static void check_ctx(void *ctx) {
  if (ctx != &sentinel) { fprintf(stderr, "wrong services ctx\n"); abort(); }
}
static qa_seat_console *sv_console(void *ctx, qa_seat_id seat) {
  check_ctx(ctx);
  return qa_same_seat(seat, state.seat) ? &state.console : NULL;
}
static qa_seat_input *sv_input(void *ctx, qa_seat_id seat) {
  check_ctx(ctx);
  (void)seat;
  return NULL;
}
static bool sv_can_chat(void *ctx) { check_ctx(ctx); return false; }
static const char *sv_root(void *ctx) { check_ctx(ctx); return root; }
static const char *sv_map_name(void *ctx) { check_ctx(ctx); return "maps/q2dm1.bsp"; }
static const char *sv_write(void *ctx, const char *(*operation)(void *op_ctx), void *op_ctx) {
  check_ctx(ctx);
  state.writes++;
  return operation(op_ctx);
}
static void sv_print(void *ctx, const char *text) {
  check_ctx(ctx);
  char shown[2048];
  show(text, shown, sizeof shown);
  out("print: %s", shown);
}
static int released;
static void sv_release(void *ctx) { check_ctx(ctx); released++; }
static void buffer_print(void *ctx, const char *text, const qa_command_context *source) {
  (void)source;
  if (ctx != &buffer_sentinel) abort();
  char shown[2048];
  show(text, shown, sizeof shown);
  out("buffer: %s", shown);
}

static const void *owner_identity;
static const void *owner_token;
static qa_renderer_resource_owner test_owner(void) {
  qa_renderer_resource_owner owner = {&owner_identity, {"test", &owner_token}, 1};
  return owner;
}
static void present(qa_app_native_renderer *renderer) {
  qa_render_command command = {0};
  command.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  qa_render_frame frame = {test_owner(), 1, &command, 1};
  qa_app_native_renderer_execute(renderer, frame);
}
static long file_size(const char *relative) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", root, relative);
  struct stat info;
  return stat(path, &info) == 0 ? (long)info.st_size : -1;
}
static char *read_text(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);
  char *text = malloc((size_t)length + 1);
  size_t got = fread(text, 1, (size_t)length, file);
  text[got] = '\0';
  fclose(file);
  return text;
}
static void activate_closed(void *ctx) { qa_app_capture_activate(ctx); }

static void test_capture_scenario(void) {
  char base_template[] = "/tmp/app-concrete-capture-XXXXXX";
  char *base = mkdtemp(base_template);
  CHECK(base != NULL);
  char *capture_root = qa_app_application_capture_root(base);
  snprintf(root, sizeof root, "%s", capture_root);
  free(capture_root);

  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "app-concrete-capture"));
  state.seat = qa_identity_owner_seat(&identity, 0);
  qa_command_context seat_context;
  memset(&seat_context, 0, sizeof seat_context);
  seat_context.session = identity.session;
  seat_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  seat_context.origin.seat = state.seat;
  seat_context.origin.client = qa_identity_owner_client(&identity, 0, 0);
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.context = seat_context;
  options.print = buffer_print;
  options.print_ctx = &buffer_sentinel;
  qa_command_buffer *commands = qa_command_buffer_create(&options);
  CHECK(commands != NULL);

  qa_app_native_renderer_host host = {0};
  qa_app_native_renderer *renderer = qa_app_native_renderer_open(host, "cpu", 4, 2, true, 1, false, test_owner(), NULL);
  CHECK(renderer != NULL);

  qa_app_application_capture_services services = {
    .ctx = &sentinel, .commands = commands, .console = sv_console, .input = sv_input, .can_chat = sv_can_chat,
    .root = sv_root, .map_name = sv_map_name, .write = sv_write, .print = sv_print, .release = sv_release,
  };
  qa_app_capture *capture = qa_app_capture_create(&services, renderer);
  qa_app_capture_activate(capture);
  qa_app_capture_activate(capture);
  out("registered writeconfig=%s screenshot=%s levelshot=%s", qa_command_buffer_exists(commands, "writeconfig") ? "true" : "false",
      qa_command_buffer_exists(commands, "screenshot") ? "true" : "false", qa_command_buffer_exists(commands, "levelshot") ? "true" : "false");
  qa_command_buffer_execute_now(commands, "writeconfig test", &seat_context, -1);
  out("writes=%d", state.writes);
  char config_path[1024];
  snprintf(config_path, sizeof config_path, "%s/settings/seat-0/test.cfg", root);
  char *config = read_text(config_path);
  char shown[2048];
  show(config != NULL ? config : "", shown, sizeof shown);
  out("config=%s", shown);
  free(config);
  qa_command_buffer_execute_now(commands, "screenshot", &seat_context, -1);
  out("pending=%s", qa_app_capture_pending_readback(capture) ? "true" : "false");
  present(renderer);
  out("pending=%s", qa_app_capture_pending_readback(capture) ? "true" : "false");
  out("shot0000.tga bytes=%ld", file_size("screenshots/shot0000.tga"));
  qa_command_buffer_execute_now(commands, "screenshotPNG named", &seat_context, -1);
  qa_command_buffer_execute_now(commands, "levelshot", &seat_context, -1);
  out("pending=%s", qa_app_capture_pending_readback(capture) ? "true" : "false");
  qa_app_capture_before_world_change(capture);
  out("pending=%s", qa_app_capture_pending_readback(capture) ? "true" : "false");
  qa_command_buffer_execute_now(commands, "levelshot", &seat_context, -1);
  present(renderer);
  out("levelshot bytes=%ld", file_size("levelshots/q2dm1.tga"));
  qa_app_capture_frame(capture);
  out("pending=%s", qa_app_capture_pending_readback(capture) ? "true" : "false");
  present(renderer);
  out("shot0001.tga bytes=%ld", file_size("screenshots/shot0001.tga"));
  qa_command_buffer_execute_now(commands, "screenshot", &seat_context, -1);
  qa_app_capture_close(capture);
  out("closed pending=%s writeconfig=%s", qa_app_capture_pending_readback(capture) ? "true" : "false",
      qa_command_buffer_exists(commands, "writeconfig") ? "true" : "false");
  qa_app_capture_frame(capture);
  qa_caught_error caught;
  if (!qa_try(activate_closed, capture, &caught)) out("activate: %s", caught.message);
  qa_app_capture_destroy(capture);
  CHECK(released == 1);
  /* named PNG was cancelled: no file. */
  CHECK(file_size("screenshots/named.png") == -1);

  FILE *file = fopen("tests/app/oracles/app_concrete_capture.txt", "r");
  CHECK(file != NULL);
  if (file != NULL) {
    char expected[16384];
    size_t got = fread(expected, 1, sizeof expected - 1, file);
    expected[got] = '\0';
    fclose(file);
    CHECK(strcmp(expected, transcript) == 0);
    if (strcmp(expected, transcript) != 0) fprintf(stderr, "expected:\n%s\nactual:\n%s\n", expected, transcript);
  }
  qa_app_native_renderer_destroy(renderer);
  qa_command_buffer_destroy(commands);
  qa_identity_owner_destroy(&identity);
  char command[1200];
  snprintf(command, sizeof command, "rm -rf '%s'", base);
  CHECK(system(command) == 0);
}

int main(void) {
  test_capture_scenario();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
