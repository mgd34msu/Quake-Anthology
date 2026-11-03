/* Port of src/camera/application.ts. GPL-2.0-or-later. */
#include "qa/camera/application.h"
#include "qa/network/common/js_number.h"
#include "qa/core/math.h"
#include "qa/render/scene/view.h"
#include "qa/foundation/support.h"
#include <math.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS throws plain Error in this module: a catchable raise named "Error". */
#define camera_error(...) qa_raise("Error", 0, 0, __VA_ARGS__)

static void *xrealloc(void *p, size_t size) {
  void *q = realloc(p, size == 0 ? 1 : size);
  if (q == NULL) abort();
  return q;
}

void qa_application_spline_camera_init(
    qa_application_spline_camera *camera,
    const qa_application_camera_services *services) {
  memset(camera, 0, sizeof *camera);
  camera->services = *services;
}

void qa_application_spline_camera_destroy(qa_application_spline_camera *camera) {
  qa_application_spline_camera_stop(camera);
  if (camera->has_definition) qa_camera_definition_destroy(&camera->definition);
  free(camera->registered);
  memset(camera, 0, sizeof *camera);
}

static void clear_definition(qa_application_spline_camera *camera) {
  if (camera->has_definition) qa_camera_definition_destroy(&camera->definition);
  camera->has_definition = false;
  memset(&camera->definition, 0, sizeof camera->definition);
}

void qa_application_spline_camera_load(qa_application_spline_camera *camera,
                                       const char *path) {
  const double generation = ++camera->load_generation;
  char *text = camera->services.read(camera->services.ctx, path);
  if (generation != camera->load_generation) {
    free(text);
    return;
  }
  if (text == NULL) camera_error("Camera file not found: %s", path);
  qa_camera_definition definition;
  qa_cleanup node;
  qa_cleanup_push(&node, free, text);
  qa_parse_camera(text, &definition);
  qa_cleanup_pop(&node);
  free(text);
  qa_application_spline_camera_stop(camera);
  clear_definition(camera);
  camera->definition = definition;
  camera->has_definition = true;
  /* `${seconds}s`: String(number) spelling. */
  const size_t length = strlen(path) + 128;
  char *message = (char *)xrealloc(NULL, length);
  (void)snprintf(message, length, "Loaded camera %s: %ss, %zu events\n", path,
                 QA_JS_NUMBER(definition.seconds), definition.event_count);
  camera->services.print(camera->services.ctx, message);
  free(message);
}

void qa_application_spline_camera_start(qa_application_spline_camera *camera) {
  if (!camera->has_definition) camera_error("No camera loaded");
  qa_application_spline_camera_stop(camera);
  if (!qa_camera_playback_init(&camera->playback, &camera->definition,
                               camera->services.milliseconds(camera->services.ctx))) {
    abort(); /* allocation failure has no TS counterpart */
  }
  camera->has_playback = true;
}

void qa_application_spline_camera_stop(qa_application_spline_camera *camera) {
  if (camera->has_playback) qa_camera_playback_destroy(&camera->playback);
  camera->has_playback = false;
  memset(&camera->playback, 0, sizeof camera->playback);
}

void qa_application_spline_camera_reset(qa_application_spline_camera *camera) {
  camera->load_generation++;
  qa_application_spline_camera_stop(camera);
  clear_definition(camera);
}

void qa_application_spline_camera_drain(qa_application_spline_camera *camera) {
  (void)camera; /* queued work ran synchronously; nothing is pending */
}

/* Command handlers. The TS invocation.args is argv without the
 * command name; the C invocation carries the full token argv. */
static const char *const *invocation_args(const qa_command_invocation *command,
                                          size_t *out_count) {
  if (command->argc == 0) {
    *out_count = 0;
    return (const char *const *)command->argv;
  }
  *out_count = command->argc - 1;
  return (const char *const *)(command->argv + 1);
}

/* TS queue(operation): a rejected operation prints "Camera: <message>\n". */
typedef struct camera_job {
  qa_application_spline_camera *camera;
  const char *path;
  const char *text;
} camera_job;

static void queue(qa_application_spline_camera *camera, void (*operation)(void *), camera_job *job) {
  qa_caught_error caught;
  if (qa_try(operation, job, &caught)) return;
  const size_t length = strlen(caught.message) + 16;
  char *message = (char *)xrealloc(NULL, length);
  (void)snprintf(message, length, "Camera: %s\n", caught.message);
  camera->services.print(camera->services.ctx, message);
  free(message);
}

static void load_job(void *raw) {
  camera_job *job = (camera_job *)raw;
  qa_application_spline_camera_load(job->camera, job->path);
}

static void write_job(void *raw) {
  camera_job *job = (camera_job *)raw;
  job->camera->services.write(job->camera->services.ctx, job->path, job->text);
}

static void run_loadcamera(qa_application_spline_camera *camera, size_t count,
                           const char *const *args) {
  if (count != 1) camera_error("Usage: loadcamera <file.camera>");
  camera_job job = {camera, args[0], NULL};
  queue(camera, load_job, &job);
}

static void run_startcamera(qa_application_spline_camera *camera, size_t count,
                            const char *const *args) {
  (void)args; /* TS checks only args.length */
  if (count != 0) camera_error("Usage: startcamera");
  qa_application_spline_camera_start(camera);
}

static void run_stopcamera(qa_application_spline_camera *camera, size_t count,
                           const char *const *args) {
  (void)count; /* the TS handler ignores its arguments */
  (void)args;
  qa_application_spline_camera_stop(camera);
}

static void run_savecamera(qa_application_spline_camera *camera, size_t count,
                           const char *const *args) {
  if (count != 1) camera_error("Usage: savecamera <file.camera>");
  if (!camera->has_definition) camera_error("No camera loaded");
  char *text = qa_serialize_camera(&camera->definition);
  if (text == NULL) abort(); /* allocation failure has no TS counterpart */
  camera_job job = {camera, args[0], text};
  queue(camera, write_job, &job);
  free(text);
}

/* TS handler: try { run(invocation.args) } catch (error) { throw new
 * CommandUsageError(`Camera: ${error.message}`) }. */
typedef struct camera_run {
  void (*run)(qa_application_spline_camera *, size_t, const char *const *);
  qa_application_spline_camera *camera;
  size_t count;
  const char *const *args;
} camera_run;

static void camera_run_call(void *raw) {
  camera_run *call = (camera_run *)raw;
  call->run(call->camera, call->count, call->args);
}

static void handle(void *ctx, qa_command_invocation *command,
                   void (*run)(qa_application_spline_camera *, size_t, const char *const *)) {
  camera_run call = {run, (qa_application_spline_camera *)ctx, 0, NULL};
  call.args = invocation_args(command, &call.count);
  qa_caught_error caught;
  if (!qa_try(camera_run_call, &call, &caught))
    qa_raise(QA_COMMAND_USAGE_ERROR, 0, 0, "Camera: %s", caught.message);
}

static void loadcamera_command(void *ctx, qa_command_invocation *command) {
  handle(ctx, command, run_loadcamera);
}

static void startcamera_command(void *ctx, qa_command_invocation *command) {
  handle(ctx, command, run_startcamera);
}

static void stopcamera_command(void *ctx, qa_command_invocation *command) {
  handle(ctx, command, run_stopcamera);
}

static void savecamera_command(void *ctx, qa_command_invocation *command) {
  handle(ctx, command, run_savecamera);
}

/* The TS passes examples: []; a non-NULL empty array keeps a present
 * empty list distinct from an absent field. */
static const char *const no_examples[] = {NULL};

static void add_command(qa_application_spline_camera *camera,
                        qa_command_buffer *commands, const char *name,
                        const char *usage, qa_command_handler_fn handler) {
  if (qa_command_buffer_exists(commands, name)) {
    camera_error("Camera command already registered: %s", name);
  }
  const qa_command_documentation documentation = {
      "Control the loaded source spline camera.", usage, no_examples, 0, NULL, 0};
  (void)qa_command_buffer_register(commands, name, handler, camera,
                                   &documentation, QA_COMMAND_WORLD);
}

void qa_application_spline_camera_register(qa_application_spline_camera *camera,
                                           qa_command_buffer *commands) {
  for (size_t i = 0; i < camera->registered_count; i++) {
    if (camera->registered[i] == commands) return;
  }
  add_command(camera, commands, "loadcamera", "loadcamera <file.camera>",
              loadcamera_command);
  add_command(camera, commands, "startcamera", "startcamera", startcamera_command);
  add_command(camera, commands, "stopcamera", "stopcamera", stopcamera_command);
  add_command(camera, commands, "savecamera", "savecamera <file.camera>",
              savecamera_command);
  if (camera->registered_count == camera->registered_capacity) {
    camera->registered_capacity =
        camera->registered_capacity == 0 ? 4 : camera->registered_capacity * 2;
    camera->registered = (qa_command_buffer **)xrealloc(
        camera->registered, camera->registered_capacity * sizeof *camera->registered);
  }
  camera->registered[camera->registered_count++] = commands;
}

void qa_application_spline_camera_unregister(qa_application_spline_camera *camera,
                                             qa_command_buffer *commands) {
  (void)qa_command_buffer_unregister(commands, "loadcamera", loadcamera_command,
                                     camera, true);
  (void)qa_command_buffer_unregister(commands, "startcamera", startcamera_command,
                                     camera, true);
  (void)qa_command_buffer_unregister(commands, "stopcamera", stopcamera_command,
                                     camera, true);
  (void)qa_command_buffer_unregister(commands, "savecamera", savecamera_command,
                                     camera, true);
  for (size_t i = 0; i < camera->registered_count; i++) {
    if (camera->registered[i] != commands) continue;
    memmove(&camera->registered[i], &camera->registered[i + 1],
            (camera->registered_count - i - 1) * sizeof *camera->registered);
    camera->registered_count--;
    break;
  }
}

void qa_application_spline_camera_activate(qa_application_spline_camera *camera) {
  qa_application_spline_camera_register(camera, camera->services.commands);
}

void qa_application_spline_camera_close(qa_application_spline_camera *camera) {
  camera->load_generation++;
  qa_application_spline_camera_stop(camera);
  while (camera->registered_count > 0) {
    qa_application_spline_camera_unregister(camera, camera->registered[0]);
  }
  qa_application_spline_camera_drain(camera);
  clear_definition(camera);
}

/* TS apply(camera, milliseconds). */
qa_scene_camera qa_application_spline_camera_apply(qa_application_spline_camera *camera, qa_scene_camera scene,
                                                   double milliseconds) {
  if (!camera->has_playback || scene.clip_is_portal) return scene;
  qa_camera_sample sample;
  if (!qa_camera_playback_sample(&camera->playback, milliseconds, &sample)) {
    qa_application_spline_camera_stop(camera);
    return scene;
  }
  const double *projection = scene.projection.values;
  double near_plane = projection[14] / (projection[10] - 1);
  double far_plane = projection[14] / (projection[10] + 1);
  double aspect = projection[5] / projection[0];
  double fov_y = (atan(tan((sample.fov * 3.141592653589793) / 360) / aspect) * 360) / 3.141592653589793;
  qa_scene_camera result = scene;
  result.origin = sample.origin;
  result.axis = qa_length3(sample.direction) == 0 ? scene.axis : qa_angles_to_axis(qa_vector_to_angles(sample.direction));
  result.projection = qa_scene_perspective_projection(sample.fov, fov_y, far_plane, near_plane);
  return result;
}
