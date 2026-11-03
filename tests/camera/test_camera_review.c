/*
 * Confirmation-gate review checks for src/camera. Each expected value comes
 * from running the TypeScript under bun. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/camera/application.h"
#include "qa/camera/spline.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/view.h"

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

static const char *FILE_TEXT =
    "cameraPathDef { time 2\n"
    "camera_interpolated { name \"dolly\" startPos ( 0 0 0 ) endPos ( 20 0 0 ) }\n"
    "target_fixed { name \"look\" pos ( 20 20 0 ) }\n"
    "event { type 4 param \"look\" time 0 }\n"
    "event { type 1 param \"1\" time 500 }\n"
    "event { type 9 param \"\" time 2500 }\n"
    "fov { fov 90 startFOV 90 endFOV 60 time 2000 }\n"
    "}";

typedef struct recorder {
  char text[4096];
  int count;
} recorder;

static void record(recorder *r, const char *text) {
  strncat(r->text, text, sizeof r->text - strlen(r->text) - 1);
  r->count++;
}

static void record_source(void *ctx, const char *text, const qa_command_context *source) {
  (void)source;
  record((recorder *)ctx, text);
}

typedef struct services_state {
  const char *file; /* NULL: read returns null */
  char *saved;
  double time;
  recorder printed;
  bool write_fails;
} services_state;

static char *svc_read(void *ctx, const char *path) {
  (void)path;
  services_state *s = (services_state *)ctx;
  if (s->file == NULL) return NULL;
  char *copy = malloc(strlen(s->file) + 1);
  strcpy(copy, s->file);
  return copy;
}
static void svc_write(void *ctx, const char *path, const char *text) {
  (void)path;
  services_state *s = (services_state *)ctx;
  if (s->write_fails) qa_raise("Error", 0, 0, "disk full");
  free(s->saved);
  s->saved = malloc(strlen(text) + 1);
  strcpy(s->saved, text);
}
static double svc_ms(void *ctx) { return ((services_state *)ctx)->time; }
static void svc_print(void *ctx, const char *text) { record(&((services_state *)ctx)->printed, text); }

static qa_command_buffer *make_buffer(qa_identity_owner *owner, recorder *out) {
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = owner->session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  options.context.origin.seat = qa_identity_owner_seat(owner, 0);
  options.context.origin.client = qa_identity_owner_client(owner, 0, 0);
  qa_command_buffer *commands = qa_command_buffer_create(&options);
  (void)qa_command_buffer_bind_output(commands, record_source, out);
  return commands;
}

/* TS PLATFORM-004: usage errors are printed through the command buffer, not
 * thrown: ["Camera: No camera loaded\n", "Camera: Usage: loadcamera
 * <file.camera>\n", "Camera: No camera loaded\n"]. C aborted the process. */
static void test_usage_errors_printed(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "camera-usage"));
  recorder out = {{0}, 0};
  qa_command_buffer *commands = make_buffer(&owner, &out);
  services_state state = {NULL, NULL, 0, {{0}, 0}, false};
  qa_application_camera_services services = {commands, &state, svc_read, svc_write, svc_ms, svc_print};
  qa_application_spline_camera camera;
  qa_application_spline_camera_init(&camera, &services);
  qa_application_spline_camera_activate(&camera);
  qa_command_buffer_append(commands, "startcamera\nloadcamera\nsavecamera x\n", NULL, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(out.count == 3);
  CHECK(strcmp(out.text,
               "Camera: No camera loaded\nCamera: Usage: loadcamera <file.camera>\n"
               "Camera: No camera loaded\n") == 0);
  qa_application_spline_camera_close(&camera);
  qa_application_spline_camera_destroy(&camera);
  qa_command_buffer_destroy(commands);
  qa_identity_owner_destroy(&owner);
}

/* TS queue(): a load that fails (missing or malformed file) prints
 * "Camera: <message>\n" through services.print; the buffer prints nothing.
 * bun: "Camera: Camera file not found: a.camera\n" then
 * "Camera: Unknown camera property bogus\n", then "Camera: disk full\n"
 * for a rejected write. C aborted on each. */
static void test_queued_failures_printed(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "camera-queue"));
  recorder out = {{0}, 0};
  qa_command_buffer *commands = make_buffer(&owner, &out);
  services_state state = {NULL, NULL, 0, {{0}, 0}, false};
  qa_application_camera_services services = {commands, &state, svc_read, svc_write, svc_ms, svc_print};
  qa_application_spline_camera camera;
  qa_application_spline_camera_init(&camera, &services);
  qa_application_spline_camera_activate(&camera);
  (void)qa_command_buffer_execute_now(commands, "loadcamera a.camera", NULL, -1);
  state.file = "cameraPathDef { time 2 bogus 1 }";
  (void)qa_command_buffer_execute_now(commands, "loadcamera b.camera", NULL, -1);
  state.file = FILE_TEXT;
  (void)qa_command_buffer_execute_now(commands, "loadcamera c.camera", NULL, -1);
  state.write_fails = true;
  (void)qa_command_buffer_execute_now(commands, "savecamera d.camera", NULL, -1);
  CHECK(out.count == 0);
  CHECK(strcmp(state.printed.text,
               "Camera: Camera file not found: a.camera\n"
               "Camera: Unknown camera property bogus\n"
               "Loaded camera c.camera: 2s, 3 events\n"
               "Camera: disk full\n") == 0);
  qa_application_spline_camera_close(&camera);
  qa_application_spline_camera_destroy(&camera);
  qa_command_buffer_destroy(commands);
  qa_identity_owner_destroy(&owner);
  free(state.saved);
}

/* TS "actual command dispatch loads, starts, applies and saves a source camera". */
static void test_dispatch(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "camera-tools"));
  recorder out = {{0}, 0};
  qa_command_buffer *commands = make_buffer(&owner, &out);
  services_state state = {FILE_TEXT, NULL, 100, {{0}, 0}, false};
  qa_application_camera_services services = {commands, &state, svc_read, svc_write, svc_ms, svc_print};
  qa_application_spline_camera camera;
  qa_application_spline_camera_init(&camera, &services);
  qa_application_spline_camera_activate(&camera);
  qa_command_buffer_append(commands, "loadcamera fixture.camera;startcamera\n", NULL, -1);
  (void)qa_command_buffer_execute(commands);
  qa_scene_camera source;
  memset(&source, 0, sizeof source);
  source.origin.x = 99;
  source.axis.vectors[0].x = 1;
  source.axis.vectors[1].y = 1;
  source.axis.vectors[2].z = 1;
  source.projection = qa_scene_perspective_projection(90, 60, 1000, 4);
  source.viewport.width = 640;
  source.viewport.height = 480;
  CHECK(qa_application_spline_camera_apply(&camera, source, state.time).origin.x == 0);
  state.time = 350;
  CHECK(qa_application_spline_camera_apply(&camera, source, state.time).origin.x == 2.5);
  (void)qa_command_buffer_execute_now(commands, "savecamera exported.camera", NULL, -1);
  CHECK(state.saved != NULL);
  if (state.saved != NULL) {
    qa_camera_definition a, b;
    qa_parse_camera(state.saved, &a);
    qa_parse_camera(FILE_TEXT, &b);
    char *sa = qa_serialize_camera(&a), *sb = qa_serialize_camera(&b);
    CHECK(strcmp(sa, sb) == 0);
    free(sa);
    free(sb);
    qa_camera_definition_destroy(&a);
    qa_camera_definition_destroy(&b);
  }
  (void)qa_command_buffer_execute_now(commands, "stopcamera", NULL, -1);
  qa_scene_camera same = qa_application_spline_camera_apply(&camera, source, state.time);
  CHECK(memcmp(&same, &source, sizeof same) == 0);
  qa_application_spline_camera_close(&camera);
  CHECK(!qa_command_buffer_exists(commands, "loadcamera"));
  qa_application_spline_camera_destroy(&camera);
  qa_command_buffer_destroy(commands);
  qa_identity_owner_destroy(&owner);
  free(state.saved);
}

/* Parse errors raise catchably (TS throw) and free the partial definition. */
typedef struct parse_job {
  const char *text;
  qa_camera_definition out;
} parse_job;
static void parse_run(void *raw) {
  parse_job *job = raw;
  qa_parse_camera(job->text, &job->out);
}
static void test_parse_raises(void) {
  static const struct {
    const char *text;
    const char *message;
  } cases[] = {
      {"cameraPathDef { time 1 target_spline { name a target { ( 0 0 0 ) ( 0 0 0 ) } } }",
       "Spline requires four points and granularity between 0.0001 and 1"},
      {"cameraPathDef { time 1 camera_fixed { pos ( 1 2 3 ) } camera_fixed { velocity 1 -1 0 } }",
       "Negative camera timing or speed"},
      {"cameraPathDef { time 1 camera_fixed { pos ( 1 2 3 ) } event { type 2 param \"x\" foo 1 } }",
       "Unknown camera event property foo"},
      {"cameraPathDef { time 1 camera_fixed { name \"x", "Unexpected end of camera file"},
      {"cameraPathDef { time 1 camera_fixed { } event { type 4 param \"nope\" time 0 } }",
       "Unknown camera target nope"},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    parse_job job;
    memset(&job, 0, sizeof job);
    job.text = cases[i].text;
    qa_caught_error caught;
    CHECK(!qa_try(parse_run, &job, &caught));
    CHECK(strcmp(caught.name, "Error") == 0);
    CHECK(strcmp(caught.message, cases[i].message) == 0);
  }
}

/* totalMilliseconds = seconds * 1000 + waits.reduce(...): the waits are summed
 * first. bun: 401.59840159840167 (C summed left to right: 401.5984015984016). */
static void test_total_association(void) {
  qa_camera_definition definition;
  qa_parse_camera(
      "cameraPathDef { time 0.14285714285714285 camera_fixed { pos ( 0 1 0 ) } "
      "target_interpolated { name t startPos ( 0 0 0 ) endPos ( 1000 0 0 ) } "
      "event { type 1 param \"0.07692307692307693\" time 0 } "
      "event { type 1 param \"0.18181818181818182\" time 0 } }",
      &definition);
  qa_camera_playback playback;
  CHECK(qa_camera_playback_init(&playback, &definition, 0));
  CHECK(playback.total_milliseconds == 401.59840159840167);
  CHECK(playback.targets[0].duration == 401.59840159840167);
  qa_camera_playback_destroy(&playback);
  qa_camera_definition_destroy(&definition);
}

int main(void) {
  test_usage_errors_printed();
  test_queued_failures_printed();
  test_dispatch();
  test_parse_raises();
  test_total_association();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
