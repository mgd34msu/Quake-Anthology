/* Port of src/app/bootstrap/application-tools.ts: ApplicationTools over the spline camera
 * (camera/application.c), the diagnostic, runtime-diagnostic and renderer-diagnostic registrations,
 * ConfigStore and the MountedContent the services return. GPL-2.0-or-later.
 * C accommodations: TS promises settle synchronously (queued diagnostics run when queued; a failure
 * prints "Diagnostic: <message>"), so drain() has nothing to wait for and returns NULL. The timer's
 * clock is the monotonic process clock (TS performance.now()). */
#ifndef QA_APP_BOOTSTRAP_APPLICATION_TOOLS_H
#define QA_APP_BOOTSTRAP_APPLICATION_TOOLS_H
#include <stdbool.h>
#include "qa/app/bootstrap/diagnostic_tools.h"
#include "qa/contracts/render.h"
#include "qa/core/cvars/index.h"
#include "qa/core/omnitimer.h"
typedef struct qa_app_application_tools qa_app_application_tools;
/* C-only: closes (when not closed) and frees. */
void qa_app_application_tools_destroy(qa_app_application_tools *tools);
qa_omni_timer *qa_app_application_tools_timer(qa_app_application_tools *tools);
void qa_app_application_tools_bind(qa_app_application_tools *tools, qa_command_buffer *const *commands, size_t count);
void qa_app_application_tools_before_world_change(qa_app_application_tools *tools);
qa_scene_camera qa_app_application_tools_apply_camera(qa_app_application_tools *tools, qa_scene_camera camera);
qa_app_capture_frame_result qa_app_application_tools_frame_time(qa_app_application_tools *tools, double milliseconds, bool active);
/* TS measureAsync: runs operation, timed when the timer is enabled. */
void *qa_app_application_tools_measure(qa_app_application_tools *tools, const char *name, void *(*operation)(void *ctx), void *ctx);
/* drain/close: NULL (failures raise). */
const char *qa_app_application_tools_drain(qa_app_application_tools *tools);
const char *qa_app_application_tools_close(qa_app_application_tools *tools);
/* TS ApplicationToolServices and the ApplicationTools(commands, services)
 * constructor. print's source NULL is TS undefined; output_root is malloc'd. */
struct qa_content_mounted;
struct qa_app_renderer_diagnostics;
struct qa_registered_scene_material;
struct qa_q3_pres_q3_renderer_resources;
typedef struct qa_app_application_tool_services {
  void *ctx;
  struct qa_content_mounted *(*mounts)(void *ctx);
  char *(*output_root)(void *ctx);
  double (*milliseconds)(void *ctx);
  qa_cvar_registry *(*cvars)(void *ctx);
  qa_app_diagnostic_frame (*frame)(void *ctx);
  struct qa_app_renderer_diagnostics (*renderer)(void *ctx);
  const struct qa_registered_scene_material *(*shaders)(void *ctx, bool sorted, size_t *out_count);
  struct qa_q3_pres_q3_renderer_resources *(*resources)(void *ctx, qa_command_context source);
  void (*print)(void *ctx, const char *text, const qa_command_context *source);
} qa_app_application_tool_services;
qa_app_application_tools *qa_app_application_tools_open(qa_command_buffer *commands, qa_app_application_tool_services services);
#endif
