/* Port of src/camera/application.ts. GPL-2.0-or-later. */
#include "qa/camera/application.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS throws plain Error in this module; abort with identical message. */
static noreturn void camera_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  abort();
}

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
  qa_parse_camera(text, &definition);
  free(text);
  qa_application_spline_camera_stop(camera);
  clear_definition(camera);
  camera->definition = definition;
  camera->has_definition = true;
  /* JS prints seconds in shortest form ("2s"); %g matches that for
   * the integral and short fractional values camera files carry. */
  const size_t length = strlen(path) + 128;
  char *message = (char *)xrealloc(NULL, length);
  (void)snprintf(message, length, "Loaded camera %s: %gs, %zu events\n", path,
                 definition.seconds, definition.event_count);
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

static void loadcamera_command(void *ctx, qa_command_invocation *command) {
  qa_application_spline_camera *camera = (qa_application_spline_camera *)ctx;
  size_t count = 0;
  const char *const *args = invocation_args(command, &count);
  if (count != 1) camera_error("Usage: loadcamera <file.camera>");
  qa_application_spline_camera_load(camera, args[0]);
}

static void startcamera_command(void *ctx, qa_command_invocation *command) {
  qa_application_spline_camera *camera = (qa_application_spline_camera *)ctx;
  size_t count = 0;
  (void)invocation_args(command, &count);
  if (count != 0) camera_error("Usage: startcamera");
  qa_application_spline_camera_start(camera);
}

static void stopcamera_command(void *ctx, qa_command_invocation *command) {
  (void)command; /* the TS handler ignores its arguments */
  qa_application_spline_camera_stop((qa_application_spline_camera *)ctx);
}

static void savecamera_command(void *ctx, qa_command_invocation *command) {
  qa_application_spline_camera *camera = (qa_application_spline_camera *)ctx;
  size_t count = 0;
  const char *const *args = invocation_args(command, &count);
  if (count != 1) camera_error("Usage: savecamera <file.camera>");
  if (!camera->has_definition) camera_error("No camera loaded");
  char *text = qa_serialize_camera(&camera->definition);
  if (text == NULL) abort(); /* allocation failure has no TS counterpart */
  camera->services.write(camera->services.ctx, args[0], text);
  free(text);
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
