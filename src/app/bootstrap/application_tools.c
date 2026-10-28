/* Port of src/app/bootstrap/application-tools.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/application_tools.h"
#include <stdlib.h>
typedef struct { qa_command_buffer *buffer; const void *token; } binding_row;
struct qa_app_application_tools {
  qa_app_application_tools_host host;
  qa_omni_timer *timer;
  binding_row *bindings; size_t binding_count, binding_cap;
};
static double clock_now(void *ctx) {
  qa_app_application_tools *tools = ctx;
  return tools->host.milliseconds(tools->host.ctx);
}
qa_app_application_tools *qa_app_application_tools_create(qa_command_buffer *commands, qa_app_application_tools_host host) {
  (void)commands; /* camera construction is host-owned */
  qa_app_application_tools *tools = calloc(1, sizeof *tools);
  if (!tools) return NULL;
  tools->host = host;
  tools->timer = qa_omni_timer_create(clock_now, tools, 4096);
  return tools;
}
void qa_app_application_tools_destroy(qa_app_application_tools *tools) {
  if (!tools) return;
  qa_omni_timer_destroy(tools->timer);
  free(tools->bindings);
  free(tools);
}
qa_omni_timer *qa_app_application_tools_timer(qa_app_application_tools *tools) { return tools->timer; }
void qa_app_application_tools_bind(qa_app_application_tools *tools, qa_command_buffer *const *commands, size_t count) {
  for (size_t i = 0; i < tools->binding_count;) {
    bool kept = false;
    for (size_t j = 0; j < count; j++) if (commands[j] == tools->bindings[i].buffer) kept = true;
    if (kept) { i++; continue; }
    tools->host.release_buffer(tools->host.ctx, tools->bindings[i].buffer, tools->bindings[i].token);
    tools->bindings[i] = tools->bindings[--tools->binding_count];
  }
  for (size_t j = 0; j < count; j++) {
    bool known = false;
    for (size_t i = 0; i < tools->binding_count; i++) if (tools->bindings[i].buffer == commands[j]) known = true;
    if (known) continue;
    const void *token = tools->host.bind_buffer(tools->host.ctx, commands[j], tools->timer);
    if (tools->binding_count == tools->binding_cap) {
      tools->binding_cap = tools->binding_cap ? tools->binding_cap * 2 : 8;
      tools->bindings = realloc(tools->bindings, tools->binding_cap * sizeof *tools->bindings);
    }
    tools->bindings[tools->binding_count++] = (binding_row){commands[j], token};
  }
}
void qa_app_application_tools_before_world_change(qa_app_application_tools *tools) {
  tools->host.camera_reset(tools->host.ctx);
  for (size_t i = 0; i < tools->binding_count; i++)
    tools->host.release_buffer(tools->host.ctx, tools->bindings[i].buffer, tools->bindings[i].token);
  tools->binding_count = 0;
}
qa_scene_camera qa_app_application_tools_apply_camera(qa_app_application_tools *tools, qa_scene_camera camera) {
  return tools->host.camera_apply(tools->host.ctx, camera);
}
qa_app_capture_frame_result qa_app_application_tools_frame_time(qa_app_application_tools *tools, double milliseconds, bool active) {
  qa_cvar_registry *cvars = tools->host.cvars(tools->host.ctx);
  double fps = cvars ? qa_cvar_registry_variable_value(cvars, "cl_avidemo") : 0;
  double timescale = 1;
  if (cvars) {
    qa_cvar_read read;
    if (qa_cvar_registry_find(cvars, "timescale", &read)) { timescale = read.numeric_value; qa_cvar_read_free(&read); }
  }
  bool force = cvars && qa_cvar_registry_variable_value(cvars, "cl_forceavidemo") != 0;
  return qa_app_source_capture_frame(milliseconds, fps, timescale, active, force);
}
void *qa_app_application_tools_measure(qa_app_application_tools *tools, const char *name, void *(*operation)(void *ctx), void *ctx) {
  if (!qa_omni_timer_enabled(tools->timer)) return operation(ctx);
  qa_omni_timer_push(tools->timer, name);
  void *result = operation(ctx);
  qa_omni_timer_pop(tools->timer);
  return result;
}
const char *qa_app_application_tools_drain(qa_app_application_tools *tools) {
  return tools->host.camera_drain(tools->host.ctx);
}
const char *qa_app_application_tools_close(qa_app_application_tools *tools) {
  qa_app_application_tools_before_world_change(tools);
  const char *error = qa_app_application_tools_drain(tools);
  if (!error) error = tools->host.camera_close(tools->host.ctx);
  qa_omni_timer_set_enabled(tools->timer, false);
  qa_omni_timer_reset(tools->timer);
  return error;
}
