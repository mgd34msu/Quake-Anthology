/* Port of src/app/bootstrap/application-tools.ts. Spline camera and
 * diagnostic registrations are landed/host services wired through
 * bind steps; binding replacement, timer measure and frame-time
 * cvar reads are kept here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_APPLICATION_TOOLS_H
#define QA_APP_BOOTSTRAP_APPLICATION_TOOLS_H
#include <stdbool.h>
#include "qa/app/bootstrap/diagnostic_tools.h"
#include "qa/contracts/render.h"
#include "qa/core/cvars/index.h"
#include "qa/core/omnitimer.h"
typedef struct qa_app_application_tools qa_app_application_tools;
typedef struct qa_app_application_tools_host {
  void *ctx;
  double (*milliseconds)(void *ctx);
  qa_cvar_registry *(*cvars)(void *ctx);
  void (*print)(void *ctx, const char *text);
  /* Per-buffer registration bundle (camera + diagnostics); the host
   * returns a release token per buffer. */
  const void *(*bind_buffer)(void *ctx, qa_command_buffer *commands, qa_omni_timer *timer);
  void (*release_buffer)(void *ctx, qa_command_buffer *commands, const void *token);
  void (*camera_reset)(void *ctx);
  const char *(*camera_drain)(void *ctx);
  const char *(*camera_close)(void *ctx);
  qa_scene_camera (*camera_apply)(void *ctx, qa_scene_camera camera);
} qa_app_application_tools_host;
qa_app_application_tools *qa_app_application_tools_create(qa_command_buffer *commands, qa_app_application_tools_host host);
void qa_app_application_tools_destroy(qa_app_application_tools *tools);
qa_omni_timer *qa_app_application_tools_timer(qa_app_application_tools *tools);
void qa_app_application_tools_bind(qa_app_application_tools *tools, qa_command_buffer *const *commands, size_t count);
void qa_app_application_tools_before_world_change(qa_app_application_tools *tools);
qa_scene_camera qa_app_application_tools_apply_camera(qa_app_application_tools *tools, qa_scene_camera camera);
qa_app_capture_frame_result qa_app_application_tools_frame_time(qa_app_application_tools *tools, double milliseconds, bool active);
/* TS measureAsync: runs operation, timed when the timer is enabled. */
void *qa_app_application_tools_measure(qa_app_application_tools *tools, const char *name, void *(*operation)(void *ctx), void *ctx);
const char *qa_app_application_tools_drain(qa_app_application_tools *tools);
const char *qa_app_application_tools_close(qa_app_application_tools *tools);
#endif
