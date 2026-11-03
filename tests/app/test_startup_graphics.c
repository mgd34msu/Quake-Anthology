/* Hidden graphical StartupApplication (TS startup.ts StartupApplication.open -> openGraphics) over the
 * retail Q1 classic id1 content, with the real NativeRenderer (CPU backend, 640x480 hidden window), SDL
 * controllers, StartupAudio and the StartupMenu. The application is stepped and driven through seat key
 * input; every RenderFrame graphics.draw hands to native.execute (observed through the graphics'
 * frame_observer seam, as the oracle spies on NativeRenderer.execute), the active menu and status after
 * each step and every host print are compared line for line with the bun oracle
 * tests/app/oracles/startup_graphics.ts (its output is startup_graphics.txt).
 * Also checks the graphics record openGraphics publishes, the client record, captionCommands through the
 * client, and close()/destroy.
 * Needs QTS_TEST_CORPUS (skipped without it); SDL_AUDIODRIVER defaults to dummy, as for the oracle.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/startup_application.h"
#include "qa/foundation/support.h"
#include "qa/input/key_codes.h"
#include "qa/network/common/js_number.h"
#include "../../src/app/bootstrap/startup_application_internal.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define HOST_MAGIC 0x57a6
typedef struct transcript {
  int magic, bad_ctx;
  char *text; size_t length, capacity;
  size_t frames;
} transcript;
static transcript *transcript_of(void *ctx) {
  transcript *t = ctx;
  if (t == NULL || t->magic != HOST_MAGIC) abort();
  return t;
}
static void append(transcript *t, const char *line) {
  size_t n = strlen(line);
  if (t->length + n + 2 > t->capacity) {
    size_t capacity = t->capacity ? t->capacity * 2 : 1 << 16;
    while (capacity < t->length + n + 2) capacity *= 2;
    t->text = realloc(t->text, capacity);
    if (!t->text) abort();
    t->capacity = capacity;
  }
  memcpy(t->text + t->length, line, n);
  t->length += n;
  t->text[t->length++] = '\n';
  t->text[t->length] = '\0';
}
/* JSON.stringify(text) for the ASCII and control characters the startup prints. */
static void json_string(const char *text, char *out, size_t size) {
  size_t n = 0;
  if (n < size) out[n++] = '"';
  for (const unsigned char *p = (const unsigned char *)text; *p && n + 8 < size; p++) {
    if (*p == '"' || *p == '\\') { out[n++] = '\\'; out[n++] = (char)*p; }
    else if (*p == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
    else if (*p == '\r') { out[n++] = '\\'; out[n++] = 'r'; }
    else if (*p == '\t') { out[n++] = '\\'; out[n++] = 't'; }
    else if (*p < 0x20) n += (size_t)snprintf(out + n, size - n, "\\u%04x", *p);
    else out[n++] = (char)*p;
  }
  if (n < size) out[n++] = '"';
  out[n < size ? n : size - 1] = '\0';
}
static void host_print(void *ctx, const char *text) {
  transcript *t = transcript_of(ctx);
  char quoted[8192], line[8300];
  json_string(text, quoted, sizeof quoted);
  snprintf(line, sizeof line, "print %s", quoted);
  append(t, line);
}
/* The oracle's describe(): JS String() numbers, generated names over 48 characters shortened. */
static void describe_image(const qa_renderer_image *image, char *out, size_t size) {
  char source[1024];
  if (image->source_is_resource) snprintf(source, sizeof source, "resource:%s", image->source_resource.id);
  else {
    size_t length = strlen(image->source_name);
    if (length > 48) snprintf(source, sizeof source, "generated:%.48s~%zu", image->source_name, length);
    else snprintf(source, sizeof source, "generated:%s", image->source_name);
  }
  snprintf(out, size, "%s %sx%s", source, QA_JS_NUMBER(image->width), QA_JS_NUMBER(image->height));
}
static void describe(const qa_render_command *command, char *line, size_t size) {
  char image[1200];
  switch (command->kind) {
    case QA_RENDER_COMMAND_DRAW_BUFFER:
      snprintf(line, size, "draw-buffer %s clear %s", command->buffer, command->clear ? "true" : "false");
      break;
    case QA_RENDER_COMMAND_IMAGE_RESOURCE: {
      const qa_image_resource_operation *op = &command->operation;
      if (op->kind == QA_IMAGE_OP_TEXTURE_MODE) { snprintf(line, size, "image-resource texture-mode"); break; }
      describe_image(&op->image, image, sizeof image);
      snprintf(line, size, "image-resource %s %s", op->kind == QA_IMAGE_OP_CREATE ? "create-image"
          : op->kind == QA_IMAGE_OP_UPDATE ? "update-image" : "release-image", image);
      break;
    }
    case QA_RENDER_COMMAND_SET_COLOR:
      snprintf(line, size, "set-color %s,%s,%s,%s", QA_JS_NUMBER(command->color.x), QA_JS_NUMBER(command->color.y),
               QA_JS_NUMBER(command->color.z), QA_JS_NUMBER(command->color.w));
      break;
    case QA_RENDER_COMMAND_STRETCH_PIC:
      describe_image(&command->image, image, sizeof image);
      snprintf(line, size, "stretch-pic %s,%s,%s,%s uv %s,%s,%s,%s %s", QA_JS_NUMBER(command->rect.x),
               QA_JS_NUMBER(command->rect.y), QA_JS_NUMBER(command->rect.width), QA_JS_NUMBER(command->rect.height),
               QA_JS_NUMBER(command->uv.s1), QA_JS_NUMBER(command->uv.t1), QA_JS_NUMBER(command->uv.s2),
               QA_JS_NUMBER(command->uv.t2), image);
      break;
    case QA_RENDER_COMMAND_VIEW: snprintf(line, size, "view"); break;
    case QA_RENDER_COMMAND_SWAP_BUFFERS: snprintf(line, size, "swap-buffers"); break;
    default: snprintf(line, size, "kind-%d", (int)command->kind); break;
  }
}
static void observe_frame(void *ctx, const qa_render_frame *frame) {
  transcript *t = transcript_of(ctx);
  char line[2048];
  snprintf(line, sizeof line, "frame %zu commands %zu", t->frames++, frame->command_count);
  append(t, line);
  for (size_t i = 0; i < frame->command_count; i++) {
    describe(&frame->commands[i], line, sizeof line);
    append(t, line);
  }
}
static void step(qa_startup_application *app, transcript *t, const char *name) {
  qa_startup_application_step(app);
  const char *active = qa_native_ui_controller_active_menu(qa_app_startup_menu_controller(app->graphics->menu));
  char quoted[1024], line[1400];
  json_string(qa_app_startup_menu_status(app->graphics->menu), quoted, sizeof quoted);
  snprintf(line, sizeof line, "step %s active %s status %s", name, active != NULL ? active : "null", quoted);
  append(t, line);
}
static void key(qa_startup_application *app, qa_seat_id seat, double code) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.seat = seat;
  event.kind = "key";
  event.code = code;
  event.down = true;
  event.time_milliseconds = qa_startup_now();
  (void)qa_startup_application_input(app, &event);
  event.down = false;
  event.time_milliseconds = qa_startup_now();
  (void)qa_startup_application_input(app, &event);
}
static char *read_text(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  char *text = malloc((size_t)size + 1);
  size_t read = fread(text, 1, (size_t)size, file);
  text[read] = '\0';
  fclose(file);
  return text;
}
static void compare(const char *actual, const char *expected) {
  size_t line = 1;
  const char *a = actual, *e = expected;
  while (*a && *e) {
    const char *an = strchr(a, '\n'), *en = strchr(e, '\n');
    size_t al = an ? (size_t)(an - a) : strlen(a), el = en ? (size_t)(en - e) : strlen(e);
    if (al != el || memcmp(a, e, al) != 0) {
      fprintf(stderr, "FAIL graphics oracle line %zu:\n  C:  %.*s\n  TS: %.*s\n", line, (int)al, a, (int)el, e);
      failures++;
      return;
    }
    a += al + (an ? 1 : 0); e += el + (en ? 1 : 0); line++;
  }
  CHECK(*a == '\0' && *e == '\0');
  if (*a != '\0' || *e != '\0') fprintf(stderr, "  C has %s lines left, TS has %s\n", *a ? "more" : "no", *e ? "more" : "no");
  printf("graphics oracle: %zu lines compared\n", line - 1);
}
static void remove_tree(const char *path) {
  char command[600];
  snprintf(command, sizeof command, "rm -rf '%s'", path);
  if (system(command) != 0) fprintf(stderr, "could not remove %s\n", path);
}

typedef struct open_call { const qa_app_options *options; qa_startup_application_host host; const char *saves; qa_startup_application *app; } open_call;
static void open_body(void *ctx) {
  open_call *call = ctx;
  call->app = qa_startup_application_open(call->options, call->host, call->saves, "menu");
}
static void close_body(void *ctx) { qa_startup_application_close(ctx); }

int main(void) {
  const char *corpus = getenv("QTS_TEST_CORPUS");
  if (corpus == NULL || corpus[0] == '\0') {
    printf("test_startup_graphics: skipped (QTS_TEST_CORPUS unset)\n");
    return 0;
  }
  if (getenv("SDL_AUDIODRIVER") == NULL) setenv("SDL_AUDIODRIVER", "dummy", 1);
  char root[256], content[300], saves[300];
  snprintf(root, sizeof root, "/tmp/qa-startup-graphics-%ld", (long)getpid());
  remove_tree(root);
  mkdir(root, 0700);
  snprintf(content, sizeof content, "%s/content", root);
  snprintf(saves, sizeof saves, "%s/saves", root);
  const char *argv[] = {"--menu", "--content-root", corpus, "--user-content-root", content, "--game", "q1-classic-id1", "--hidden",
                        "--renderer", "cpu", "--width", "640", "--height", "480", "+set", "s_outputRate", "44100"};
  qa_content_root_search search = {0};
  qa_app_application_command command = qa_app_parse_application_command(argv, sizeof argv / sizeof *argv, search, NULL);
  CHECK(strcmp(command.kind, "menu") == 0);
  transcript t;
  memset(&t, 0, sizeof t);
  t.magic = HOST_MAGIC;
  open_call call;
  memset(&call, 0, sizeof call);
  call.options = &command.options;
  call.host.base.ctx = &t;
  call.host.base.print = host_print;
  call.saves = saves;
  qa_caught_error caught;
  if (!qa_try(open_body, &call, &caught)) {
    fprintf(stderr, "FAIL StartupApplication.open raised: %s\n", caught.message);
    failures++;
    qa_app_application_command_destroy(&command);
    remove_tree(root);
    printf("test_startup_graphics: %d checks, %d failures\n", checks, failures);
    return 1;
  }
  qa_startup_application *app = call.app;
  /* openGraphics published the graphics and the client record (TS this.graphics / this.client). */
  qa_startup_graphics *graphics = app->graphics;
  CHECK(graphics != NULL && graphics->renderer != NULL && graphics->menu != NULL && graphics->router != NULL);
  CHECK(graphics != NULL && graphics->audio != NULL && graphics->controllers != NULL && graphics->controller_settings != NULL);
  CHECK(graphics != NULL && graphics->input_profile != NULL && graphics->image_settings == app->image_settings);
  CHECK(graphics != NULL && graphics->display_hidden && graphics->display_width == 640 && graphics->display_height == 480);
  CHECK(graphics != NULL && graphics->display_renderer != NULL && strcmp(graphics->display_renderer, "cpu") == 0);
  qa_app_client_bootstrap *client = app->client;
  CHECK(client != NULL && client->platform.kind == QA_APP_CLIENT_INPUT_MENU && client->platform.router == graphics->router);
  CHECK(client != NULL && client->local_count == 1 && client->renderer == graphics->renderer);
  CHECK(client != NULL && client->output_current == qa_app_startup_audio_engine(graphics->audio));
  CHECK(client != NULL && client->capture != NULL && client->video_restart != NULL && client->source_current == NULL);
  CHECK(client != NULL && client->console_count == 1); /* bindFrontendConsole published the primary console */
  CHECK(qa_startup_application_pending(app) == NULL);  /* entry "menu" stages no initial source */
  qa_seat_id seat;
  CHECK(qa_startup_application_input_seat(app, &seat));
  graphics->frame_observer = observe_frame;
  graphics->frame_observer_ctx = &t;
  step(app, &t, "first");
  step(app, &t, "second");
  key(app, seat, QA_KEY_DOWN);
  step(app, &t, "down");
  key(app, seat, QA_KEY_UP);
  step(app, &t, "up");
  key(app, seat, QA_KEY_ENTER);
  step(app, &t, "enter");
  key(app, seat, QA_KEY_ESCAPE);
  step(app, &t, "escape");
  /* graphics.captionCommands through the published client record (ClientBootstrap.captionCommands). */
  static qa_caption_cue cues[2];
  qa_active_caption active[2];
  memset(active, 0, sizeof active);
  active[0].cue = &cues[0];
  active[0].localized_text = "The slipgate is open. Find the exit before the Shambler finds you.";
  active[0].localized_speaker = "Ranger";
  active[1].cue = &cues[1];
  active[1].localized_text = "Second line\nwith a break";
  active[1].localized_speaker = NULL;
  for (size_t pass = 0; pass < 2; pass++) {
    size_t count = 0, caption_count = pass == 0 ? 0 : 2;
    const qa_render_command *commands = client->caption_commands(client->bootstrap_ctx, pass == 0 ? NULL : active, caption_count,
        (qa_rect){0, 0, 640, 480}, 500, &count);
    char line[2048];
    snprintf(line, sizeof line, "captions %zu commands %zu", caption_count, count);
    append(&t, line);
    for (size_t i = 0; i < count; i++) {
      describe(&commands[i], line, sizeof line);
      append(&t, line);
    }
  }
  CHECK(qa_try(close_body, app, &caught));
  CHECK(app->client == NULL && graphics->closed); /* TS this.client = null; the closed graphics free at destroy */
  qa_startup_application_destroy(app);
  char *expected = read_text("tests/app/oracles/startup_graphics.txt");
  CHECK(expected != NULL);
  if (expected != NULL) compare(t.text != NULL ? t.text : "", expected);
  free(expected);
  free(t.text);
  qa_app_application_command_destroy(&command);
  remove_tree(root);
  printf("test_startup_graphics: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
