/* Port of src/app/bootstrap/capture.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/capture/index.h"
#include "qa/foundation/support.h"
#include "qa/input/bindings.h"
#include "qa/settings/config.h"

char *qa_app_application_capture_root(const char *user_content_root) {
  return qa_app_console_config_root(user_content_root);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length + 1);
  return copy;
}

/* ---- inputCaptureServices ---- */
typedef struct input_capture_ctx {
  qa_app_application_input *input;
  char *root;
  const char *(*map_name)(void *ctx);
  void *map_ctx;
  void (*print)(void *ctx, const char *text);
  void *print_ctx;
} input_capture_ctx;

static qa_app_local_input *local_for(input_capture_ctx *c, qa_seat_id seat) {
  size_t count = qa_app_application_input_local_count(c->input);
  for (size_t i = 0; i < count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(c->input, i);
    if (local != NULL && qa_same_seat(qa_session_seat_id(local->player.seat), seat)) return local;
  }
  return NULL;
}
static qa_seat_console *ic_console(void *ctx, qa_seat_id seat) {
  qa_app_local_input *local = local_for(ctx, seat);
  return local == NULL ? NULL : local->console;
}
static qa_seat_input *ic_input(void *ctx, qa_seat_id seat) {
  qa_app_local_input *local = local_for(ctx, seat);
  return local == NULL ? NULL : local->input;
}
static bool ic_can_chat(void *ctx) {
  input_capture_ctx *c = ctx;
  return qa_app_application_input_binding_capabilities(c->input).chat;
}
static const char *ic_root(void *ctx) { return ((input_capture_ctx *)ctx)->root; }
static const char *ic_map_name(void *ctx) {
  input_capture_ctx *c = ctx;
  return c->map_name(c->map_ctx);
}
static void ic_print(void *ctx, const char *text) {
  input_capture_ctx *c = ctx;
  c->print(c->print_ctx, text);
}
static const char *ic_write(void *ctx, const char *(*operation)(void *op_ctx), void *op_ctx) {
  input_capture_ctx *c = ctx;
  return qa_app_console_script_files_write(qa_app_application_input_scripts(c->input), operation, op_ctx);
}
static void ic_release(void *ctx) {
  input_capture_ctx *c = ctx;
  free(c->root);
  free(c);
}

qa_app_application_capture_services qa_app_input_capture_services(qa_app_application_input *input, const char *root,
    const char *(*map_name)(void *ctx), void *map_ctx, void (*print)(void *ctx, const char *text), void *print_ctx) {
  input_capture_ctx *c = calloc(1, sizeof *c);
  if (c == NULL) qa_range_error("Out of memory");
  c->input = input;
  c->root = dup_text(root);
  c->map_name = map_name;
  c->map_ctx = map_ctx;
  c->print = print;
  c->print_ctx = print_ctx;
  return (qa_app_application_capture_services){
    .ctx = c,
    .commands = qa_app_application_input_commands(input),
    .configuration_write_started = NULL,
    .console = ic_console,
    .input = ic_input,
    .can_chat = ic_can_chat,
    .root = ic_root,
    .map_name = ic_map_name,
    .write = ic_write,
    .print = ic_print,
    .release = ic_release,
  };
}

/* ---- ApplicationCapture ---- */
typedef struct capture_read capture_read;
struct capture_read {
  qa_app_capture *owner;
  qa_console_queued_operation_fn operation;
  void *op_ctx;
  capture_read *next;
};
typedef struct seat_store { qa_seat_id seat; qa_config_store store; } seat_store;
struct qa_app_capture {
  qa_app_application_capture_services services;
  qa_app_native_renderer *renderer;
  qa_console_command_registration *unregister; /* NULL = TS null */
  bool closed;
  bool has_frame_capture;
  qa_frame_capture frame_capture;
  capture_read *reads; /* TS reads: armed renderer captures */
  size_t read_count;
  /* The frame a resolved read hands FrameCapture.readRgba, or the rejection the
   * read raises, while its operation runs. */
  bool has_frame;
  qa_image_level frame;
  char *read_failure;
  /* seatConsoleConfig stores the console commands borrow (TS GC). */
  seat_store *stores;
  size_t store_count, store_cap;
};

static qa_image_level read_frame(void *ctx) {
  qa_app_capture *capture = ctx;
  if (!capture->has_frame) qa_raise("Error", 0, 0, "%s", capture->read_failure != NULL ? capture->read_failure : "Frame capture aborted");
  return capture->frame;
}

static qa_frame_capture *current_frame_capture(qa_app_capture *capture) {
  const char *root = capture->services.root(capture->services.ctx);
  if (!capture->has_frame_capture || strcmp(capture->frame_capture.root, root) != 0) {
    qa_frame_capture next;
    if (!qa_frame_capture_init(&next, root, (qa_frame_readback){capture, read_frame})) qa_range_error("Out of memory");
    if (capture->has_frame_capture) qa_frame_capture_destroy(&capture->frame_capture);
    capture->frame_capture = next;
    capture->has_frame_capture = true;
  }
  return &capture->frame_capture;
}

static void print_failure(qa_app_capture *capture, const char *message) {
  size_t length = strlen(message) + sizeof "Console output failed: \n";
  char *text = malloc(length);
  if (text == NULL) qa_range_error("Out of memory");
  snprintf(text, length, "Console output failed: %s\n", message);
  capture->services.print(capture->services.ctx, text);
  free(text);
}

typedef struct run_operation { qa_console_queued_operation_fn operation; void *op_ctx; } run_operation;
static void run_operation_fn(void *ctx) {
  run_operation *run = ctx;
  run->operation(run->op_ctx);
}
static const char *write_operation(void *ctx) {
  run_operation_fn(ctx);
  return NULL;
}
typedef struct write_call { qa_app_capture *capture; run_operation run; const char *error; } write_call;
static void write_call_fn(void *ctx) {
  write_call *call = ctx;
  call->error = call->capture->services.write(call->capture->services.ctx, write_operation, &call->run);
}

/* Runs a resolved or rejected read's operation and reports its failure. */
static void settle_read(capture_read *read) {
  qa_app_capture *capture = read->owner;
  run_operation run = {read->operation, read->op_ctx};
  free(read);
  qa_caught_error caught;
  if (!qa_try(run_operation_fn, &run, &caught))
    print_failure(capture, capture->read_failure != NULL ? capture->read_failure : caught.message);
  capture->has_frame = false;
  free(capture->read_failure);
  capture->read_failure = NULL;
}
static void remove_read(capture_read *read) {
  qa_app_capture *capture = read->owner;
  for (capture_read **at = &capture->reads; *at != NULL; at = &(*at)->next) {
    if (*at != read) continue;
    *at = read->next;
    capture->read_count--;
    return;
  }
}
static void read_resolve(void *ctx, qa_image_level image) {
  capture_read *read = ctx;
  qa_app_capture *capture = read->owner;
  remove_read(read);
  capture->has_frame = true;
  capture->frame = image;
  settle_read(read);
  free((uint8_t *)image.pixels.bytes);
}
static void read_reject(void *ctx, const char *error) {
  capture_read *read = ctx;
  qa_app_capture *capture = read->owner;
  remove_read(read);
  capture->has_frame = false;
  free(capture->read_failure);
  capture->read_failure = dup_text(error);
  settle_read(read);
}

/* TS queue(operation, frameReadback). */
static void capture_queue(void *ctx, qa_console_queued_operation_fn operation, void *op_ctx, bool frame_readback) {
  qa_app_capture *capture = ctx;
  if (capture->closed) {
    capture->services.print(capture->services.ctx, "Console output owner is closed\n");
    return;
  }
  if (frame_readback) {
    capture_read *read = calloc(1, sizeof *read);
    if (read == NULL) qa_range_error("Out of memory");
    read->owner = capture;
    read->operation = operation;
    read->op_ctx = op_ctx;
    read->next = capture->reads;
    capture->reads = read;
    capture->read_count++;
    qa_app_native_renderer_capture_next_frame(capture->renderer,
        (qa_app_frame_capture_request){read, read_resolve, read_reject, false, NULL});
    return;
  }
  write_call call = {capture, {operation, op_ctx}, NULL};
  qa_caught_error caught;
  if (!qa_try(write_call_fn, &call, &caught)) print_failure(capture, caught.message);
  else if (call.error != NULL) print_failure(capture, call.error);
}

/* ---- ConsoleCommandServices over ApplicationCaptureServices ---- */
static qa_config_store *cs_config(void *ctx, qa_seat_id seat) {
  qa_app_capture *capture = ctx;
  const char *root = capture->services.root(capture->services.ctx);
  for (size_t i = 0; i < capture->store_count; i++) {
    seat_store *row = &capture->stores[i];
    if (!qa_same_seat(row->seat, seat)) continue;
    qa_config_store probe;
    if (!qa_app_seat_console_config(root, seat, &probe)) qa_range_error("Out of memory");
    if (strcmp(probe.root, row->store.root) == 0) { qa_config_store_destroy(&probe); return &row->store; }
    qa_config_store_destroy(&row->store);
    row->store = probe;
    return &row->store;
  }
  if (capture->store_count == capture->store_cap) {
    capture->store_cap = capture->store_cap ? capture->store_cap * 2 : 4;
    capture->stores = realloc(capture->stores, capture->store_cap * sizeof *capture->stores);
    if (capture->stores == NULL) qa_range_error("Out of memory");
  }
  seat_store *row = &capture->stores[capture->store_count];
  row->seat = seat;
  if (!qa_app_seat_console_config(root, seat, &row->store)) qa_range_error("Out of memory");
  capture->store_count++;
  return &row->store;
}

static char *cs_configuration(void *ctx, qa_command_invocation *invocation, char **error_out) {
  qa_app_capture *capture = ctx;
  (void)error_out; /* the TS configuration callback does not throw */
  const qa_command_origin *origin = &invocation->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  qa_seat_input *input = origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? capture->services.input(capture->services.ctx, origin->seat) : NULL;
  char **bindings = NULL;
  size_t binding_count = 0;
  if (input != NULL) {
    size_t count = 0;
    const qa_input_binding *rows = qa_seat_input_bindings(input, &count);
    bindings = qa_archived_bindings(rows, count, false, &binding_count);
    if (bindings == NULL) qa_range_error("Out of memory");
  }
  size_t archive_count = 0;
  char **archive = qa_command_buffer_archive_commands(capture->services.commands, &invocation->source, &archive_count);
  static const char header[] = "// Generated by quake-typescript\n";
  size_t length = sizeof header;
  for (size_t i = 0; i < binding_count; i++) length += strlen(bindings[i]) + 1;
  for (size_t i = 0; i < archive_count; i++) length += strlen(archive[i]) + 1;
  char *text = malloc(length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  size_t at = 0, lines = 0;
  memcpy(text, header, sizeof header - 1);
  at = sizeof header - 1;
  for (size_t i = 0; i < binding_count + archive_count; i++) {
    const char *line = i < binding_count ? bindings[i] : archive[i - binding_count];
    if (lines++ != 0) text[at++] = '\n';
    size_t n = strlen(line);
    memcpy(text + at, line, n);
    at += n;
  }
  text[at++] = '\n';
  text[at] = '\0';
  for (size_t i = 0; i < binding_count; i++) free(bindings[i]);
  free(bindings);
  for (size_t i = 0; i < archive_count; i++) free(archive[i]);
  free(archive);
  return text;
}

static qa_seat_console *cs_console(void *ctx, qa_seat_id seat) {
  qa_app_capture *capture = ctx;
  return capture->services.console(capture->services.ctx, seat);
}
static bool cs_can_chat(void *ctx) {
  qa_app_capture *capture = ctx;
  return capture->services.can_chat(capture->services.ctx);
}
/* TS capture(seat): services.console(seat) === null ? null : currentFrameCapture(). */
static bool cs_has_capture(void *ctx, qa_seat_id seat) {
  qa_app_capture *capture = ctx;
  if (capture->services.console(capture->services.ctx, seat) == NULL) return false;
  (void)current_frame_capture(capture);
  return true;
}
static qa_screenshot_format screenshot_format(const char *format) {
  if (strcmp(format, "tga") == 0) return QA_SCREENSHOT_TGA;
  if (strcmp(format, "jpg") == 0) return QA_SCREENSHOT_JPG;
  return QA_SCREENSHOT_PNG;
}
/* A rejected read reaches the operation as a failed capture (NULL path); settle_read prints the rejection. */
static char *cs_capture_screenshot(void *ctx, qa_seat_id seat, const char *format, const char *name) {
  qa_app_capture *capture = ctx;
  (void)seat; /* TS capture(seat) returns the one FrameCapture for every seat */
  if (!capture->has_frame) return NULL;
  qa_capture_result result;
  if (!qa_frame_capture_screenshot(current_frame_capture(capture), screenshot_format(format), name, 90, &result)) return NULL;
  return result.path;
}
static char *cs_capture_levelshot(void *ctx, qa_seat_id seat, const char *map_name) {
  qa_app_capture *capture = ctx;
  (void)seat; /* as cs_capture_screenshot */
  if (!capture->has_frame) return NULL;
  qa_capture_result result;
  if (!qa_frame_capture_levelshot(current_frame_capture(capture), map_name, NULL, 0, &result)) return NULL;
  return result.path;
}
static const char *cs_map_name(void *ctx) {
  qa_app_capture *capture = ctx;
  return capture->services.map_name(capture->services.ctx);
}
static void cs_print(void *ctx, const char *text) {
  qa_app_capture *capture = ctx;
  capture->services.print(capture->services.ctx, text);
}
/* configurationWriteStarted is forwarded with the services' own context. */
static qa_configuration_write_notify cs_configuration_write_started(void *ctx, qa_command_context source, const char *path) {
  qa_app_capture *capture = ctx;
  return capture->services.configuration_write_started(capture->services.ctx, source, path);
}

qa_app_capture *qa_app_capture_create(const qa_app_application_capture_services *services, qa_app_native_renderer *renderer) {
  qa_app_capture *capture = calloc(1, sizeof *capture);
  if (capture == NULL) qa_range_error("Out of memory");
  capture->services = *services;
  capture->renderer = renderer;
  return capture;
}

qa_app_capture *qa_app_application_capture_open(qa_app_application_input *input, const char *root,
    const char *(*map_name)(void *ctx), void *map_ctx, void (*print)(void *ctx, const char *text), void *print_ctx,
    qa_app_native_renderer *renderer) {
  qa_app_application_capture_services services = qa_app_input_capture_services(input, root, map_name, map_ctx, print, print_ctx);
  return qa_app_capture_create(&services, renderer);
}

void qa_app_capture_activate(qa_app_capture *capture) {
  if (capture->closed) qa_raise("Error", 0, 0, "Cannot activate a closed capture owner");
  if (capture->unregister != NULL) return;
  qa_console_command_services services = {
    .ctx = capture,
    .commands = capture->services.commands,
    .config = cs_config,
    .configuration = cs_configuration,
    .configuration_write_started = capture->services.configuration_write_started == NULL ? NULL : cs_configuration_write_started,
    .console = cs_console,
    .can_chat = cs_can_chat,
    .has_capture = cs_has_capture,
    .capture_screenshot = cs_capture_screenshot,
    .capture_levelshot = cs_capture_levelshot,
    .map_name = cs_map_name,
    .print = cs_print,
    .queue = capture_queue,
  };
  capture->unregister = qa_register_console_commands(&services);
  if (capture->unregister == NULL) qa_range_error("Out of memory");
}

/* cl_avidemo: currentFrameCapture().screenshot({ format: "tga" }) on the pending readback path. */
static void capture_frame_operation(void *ctx) {
  qa_app_capture *capture = ctx;
  qa_capture_result result;
  if (!qa_frame_capture_screenshot(current_frame_capture(capture), QA_SCREENSHOT_TGA, NULL, 90, &result))
    qa_raise("Error", 0, 0, "Screenshot capture failed");
  qa_capture_result_free(&result);
}
void qa_app_capture_frame(qa_app_capture *capture) {
  capture_queue(capture, capture_frame_operation, capture, true);
}

bool qa_app_capture_pending_readback(const qa_app_capture *capture) { return capture->read_count != 0; }

/* Queued writes settled when they were queued; armed reads settle when the renderer presents or aborts them. */
void qa_app_capture_drain(qa_app_capture *capture) { (void)capture; }

void qa_app_capture_before_world_change(qa_app_capture *capture) {
  static const char reason[] = "Screenshot cancelled before the requested frame was presented";
  while (capture->reads != NULL) {
    capture_read *head = capture->reads;
    qa_app_native_renderer_abort_capture(capture->renderer, head, reason);
    if (capture->reads == head) read_reject(head, reason); /* no longer pending in the renderer */
  }
  qa_app_capture_drain(capture);
}

void qa_app_capture_close(qa_app_capture *capture) {
  if (capture == NULL || capture->closed) return;
  capture->closed = true;
  if (capture->unregister != NULL) qa_console_command_registration_remove(capture->unregister);
  capture->unregister = NULL;
  qa_app_capture_before_world_change(capture);
}

void qa_app_capture_destroy(qa_app_capture *capture) {
  if (capture == NULL) return;
  qa_app_capture_close(capture);
  if (capture->has_frame_capture) qa_frame_capture_destroy(&capture->frame_capture);
  for (size_t i = 0; i < capture->store_count; i++) qa_config_store_destroy(&capture->stores[i].store);
  free(capture->stores);
  free(capture->read_failure);
  if (capture->services.release != NULL) capture->services.release(capture->services.ctx);
  free(capture);
}
