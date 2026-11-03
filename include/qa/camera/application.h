/*
 * Port of src/camera/application.ts (ApplicationSplineCamera): one
 * view override owner; loading completes before a later command can
 * start it. GPL-2.0-or-later.
 * C accommodations:
 *  - Plain TS `Error` throws abort printing the identical message
 *    (the src/camera/spline.ts port pattern). The TS queue() catch
 *    that prints `Camera: <message>` has no counterpart once those
 *    errors abort: queued operations run synchronously inside the
 *    command handler, so drain() is a no-op and there is no pending
 *    set. loadGeneration is kept, so a read/write callback that
 *    re-enters load/reset/close still supersedes the outer load.
 *  - ApplicationCameraServices.read/write are synchronous callbacks
 *    (TS promises): read returns a malloc'd string (caller-owned by
 *    this module after return) or NULL for the TS null; write
 *    borrows its text. Callback failures abort in the callback,
 *    like other plain Errors in this port.
 *  - The TS register() release closure becomes
 *    qa_application_spline_camera_unregister(camera, commands).
 *    Registration bookkeeping is owned by the camera; init/destroy
 *    replace the constructor/GC.
 *  - apply() takes the TS default `milliseconds = services.milliseconds()`
 *    explicitly; callers pass services.milliseconds(ctx) for the default.
 */
#ifndef QA_CAMERA_APPLICATION_H
#define QA_CAMERA_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/camera/spline.h"
#include "qa/core/commands/index.h"
#include "qa/contracts/render.h"

/* TS ApplicationCameraServices. */
typedef struct qa_application_camera_services {
  qa_command_buffer *commands;
  void *ctx;
  char *(*read)(void *ctx, const char *path);
  void (*write)(void *ctx, const char *path, const char *text);
  double (*milliseconds)(void *ctx);
  void (*print)(void *ctx, const char *text);
} qa_application_camera_services;

typedef struct qa_application_spline_camera {
  qa_application_camera_services services;
  qa_camera_definition definition;
  bool has_definition;
  qa_camera_playback playback;
  bool has_playback;
  double load_generation;
  qa_command_buffer **registered;
  size_t registered_count;
  size_t registered_capacity;
} qa_application_spline_camera;

/* Zeroes the camera and stores a copy of the services struct. */
void qa_application_spline_camera_init(
    qa_application_spline_camera *camera,
    const qa_application_camera_services *services);
/* Frees the loaded definition/playback and registration records.
 * Does not unregister commands (call close first for that). */
void qa_application_spline_camera_destroy(qa_application_spline_camera *camera);

void qa_application_spline_camera_activate(qa_application_spline_camera *camera);
/* Registers the four camera commands; a second call for the same
 * buffer is a no-op, as the TS returns the previous release. */
void qa_application_spline_camera_register(qa_application_spline_camera *camera,
                                           qa_command_buffer *commands);
/* The TS release closure for one buffer. */
void qa_application_spline_camera_unregister(qa_application_spline_camera *camera,
                                             qa_command_buffer *commands);

/* TS async load; synchronous here (see header note). */
void qa_application_spline_camera_load(qa_application_spline_camera *camera,
                                       const char *path);
void qa_application_spline_camera_start(qa_application_spline_camera *camera);
void qa_application_spline_camera_stop(qa_application_spline_camera *camera);
void qa_application_spline_camera_reset(qa_application_spline_camera *camera);
/* No-op: queued work already ran synchronously. */
void qa_application_spline_camera_drain(qa_application_spline_camera *camera);
/* TS apply(camera, milliseconds): the playback's view override, or camera unchanged. */
qa_scene_camera qa_application_spline_camera_apply(qa_application_spline_camera *camera, qa_scene_camera scene,
                                                   double milliseconds);
/* TS async close; synchronous here. Leaves the camera reusable. */
void qa_application_spline_camera_close(qa_application_spline_camera *camera);

#endif
