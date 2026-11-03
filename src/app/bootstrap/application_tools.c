/* Port of src/app/bootstrap/application-tools.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/application_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/app/bootstrap/renderer_diagnostics.h"
#include "qa/camera/application.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/settings/config.h"

/* One owner's registrations, released in reverse (TS bindings map value). */
typedef struct binding_row {
  qa_command_buffer *owner;
  bool camera;
  qa_app_diagnostic_tools *diagnostics;
  qa_app_diagnostic_tools *runtime;
  qa_app_renderer_diagnostics_registration *renderer;
} binding_row;
struct qa_app_application_tools {
  qa_omni_timer *timer;
  qa_application_spline_camera camera;
  qa_app_application_tool_services services;
  binding_row *bindings;
  size_t binding_count, binding_cap;
  bool closed;
};

/* TS performance.now(): milliseconds on the monotonic process clock. */
static double performance_now(void *ctx) {
  (void)ctx; /* the TS default clock takes no context */
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}

static void tools_print(void *ctx, const char *text) {
  qa_app_application_tools *tools = ctx;
  tools->services.print(tools->services.ctx, text, NULL);
}
static double tools_milliseconds(void *ctx) {
  qa_app_application_tools *tools = ctx;
  return tools->services.milliseconds(tools->services.ctx);
}
/* read: new ConfigStore(outputRoot()).loadText(path), else services.mounts().open(path) decoded. */
static char *camera_read(void *ctx, const char *path) {
  qa_app_application_tools *tools = ctx;
  char *root = tools->services.output_root(tools->services.ctx);
  qa_config_store store;
  if (!qa_config_store_init(&store, root)) qa_range_error("Out of memory");
  free(root);
  size_t length = 0;
  char *saved = qa_config_store_load_text(&store, path, &length);
  qa_config_store_destroy(&store);
  if (saved != NULL) return saved;
  qa_content_opened_resource file;
  if (!qa_content_mounted_open(tools->services.mounts(tools->services.ctx), path, NULL, NULL, &file)) return NULL;
  char *text = malloc(file.byte_length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  memcpy(text, file.bytes, file.byte_length);
  text[file.byte_length] = '\0';
  free(file.bytes);
  return text;
}
/* write: new ConfigStore(outputRoot()).dump(path, text). */
static void camera_write(void *ctx, const char *path, const char *text) {
  qa_app_application_tools *tools = ctx;
  char *root = tools->services.output_root(tools->services.ctx);
  qa_config_store store;
  if (!qa_config_store_init(&store, root)) qa_range_error("Out of memory");
  free(root);
  bool written = qa_config_store_dump(&store, path, text);
  qa_config_store_destroy(&store);
  if (!written) qa_raise("Error", 0, 0, "Failed to write %s", path);
}

qa_app_application_tools *qa_app_application_tools_open(qa_command_buffer *commands, qa_app_application_tool_services services) {
  qa_app_application_tools *tools = calloc(1, sizeof *tools);
  if (tools == NULL) return NULL;
  tools->services = services;
  tools->timer = qa_omni_timer_create(performance_now, NULL, 4096);
  if (tools->timer == NULL) { free(tools); return NULL; }
  qa_application_camera_services camera = {
    .commands = commands, .ctx = tools, .read = camera_read, .write = camera_write,
    .milliseconds = tools_milliseconds, .print = tools_print,
  };
  qa_application_spline_camera_init(&tools->camera, &camera);
  return tools;
}

qa_omni_timer *qa_app_application_tools_timer(qa_app_application_tools *tools) { return tools->timer; }

/* ---- RuntimeDiagnosticServices / RendererDiagnosticServices over the tool services ---- */
static qa_app_runtime_mounts runtime_mounts(void *ctx) {
  qa_app_application_tools *tools = ctx;
  return qa_app_runtime_mounts_of(tools->services.mounts(tools->services.ctx));
}
static qa_app_diagnostic_frame runtime_frame(void *ctx) {
  qa_app_application_tools *tools = ctx;
  return tools->services.frame(tools->services.ctx);
}
/* TS queue(operation): a failure prints "Diagnostic: <message>". */
static void runtime_queue(void *ctx, void (*operation)(void *op_ctx), void *op_ctx) {
  qa_app_application_tools *tools = ctx;
  qa_caught_error caught;
  if (qa_try(operation, op_ctx, &caught)) return;
  size_t length = strlen(caught.message) + sizeof "Diagnostic: \n";
  char *text = malloc(length);
  if (text == NULL) qa_range_error("Out of memory");
  snprintf(text, length, "Diagnostic: %s\n", caught.message);
  tools->services.print(tools->services.ctx, text, NULL);
  free(text);
}
static qa_app_renderer_diagnostics renderer_state(void *ctx) {
  qa_app_application_tools *tools = ctx;
  return tools->services.renderer(tools->services.ctx);
}
static const qa_registered_scene_material *renderer_shaders(void *ctx, bool sorted, size_t *out_count) {
  qa_app_application_tools *tools = ctx;
  return tools->services.shaders(tools->services.ctx, sorted, out_count);
}
static qa_q3_pres_q3_renderer_resources *renderer_resources(void *ctx, qa_command_context source) {
  qa_app_application_tools *tools = ctx;
  return tools->services.resources(tools->services.ctx, source);
}
static void renderer_print(void *ctx, const char *text, qa_command_context source) {
  qa_app_application_tools *tools = ctx;
  tools->services.print(tools->services.ctx, text, &source);
}

static void release_row(qa_app_application_tools *tools, binding_row *row) {
  if (row->renderer != NULL) qa_app_renderer_diagnostics_unregister(row->renderer);
  if (row->runtime != NULL) qa_app_diagnostic_tools_destroy(row->runtime);
  if (row->diagnostics != NULL) qa_app_diagnostic_tools_destroy(row->diagnostics);
  if (row->camera) qa_application_spline_camera_unregister(&tools->camera, row->owner);
  memset(row, 0, sizeof *row);
}

typedef struct bind_call { qa_app_application_tools *tools; binding_row row; } bind_call;
static void register_owner(void *ctx) {
  bind_call *call = ctx;
  qa_app_application_tools *tools = call->tools;
  qa_command_buffer *owner = call->row.owner;
  qa_application_spline_camera_register(&tools->camera, owner);
  call->row.camera = true;
  call->row.diagnostics = qa_app_register_diagnostic_tools(owner, tools->timer, tools_print, tools);
  if (call->row.diagnostics == NULL) qa_range_error("Out of memory");
  call->row.runtime = qa_app_register_runtime_diagnostics((qa_app_runtime_diagnostic_services){
    .ctx = tools, .commands = owner, .mounts = runtime_mounts, .frame = runtime_frame, .print = tools_print, .queue = runtime_queue,
  });
  if (call->row.runtime == NULL) qa_range_error("Out of memory");
  call->row.renderer = qa_app_register_renderer_diagnostics((qa_app_renderer_diagnostic_services){
    .ctx = tools, .commands = owner, .renderer = renderer_state, .shaders = renderer_shaders, .resources = renderer_resources,
    .print = renderer_print,
  });
  if (call->row.renderer == NULL) qa_range_error("Out of memory");
}

void qa_app_application_tools_bind(qa_app_application_tools *tools, qa_command_buffer *const *commands, size_t count) {
  for (size_t i = 0; i < tools->binding_count;) {
    bool kept = false;
    for (size_t j = 0; j < count; j++) if (commands[j] == tools->bindings[i].owner) kept = true;
    if (kept) { i++; continue; }
    release_row(tools, &tools->bindings[i]);
    memmove(&tools->bindings[i], &tools->bindings[i + 1], (tools->binding_count - i - 1) * sizeof *tools->bindings);
    tools->binding_count--;
  }
  for (size_t j = 0; j < count; j++) {
    bool known = false;
    for (size_t i = 0; i < tools->binding_count; i++) if (tools->bindings[i].owner == commands[j]) known = true;
    if (known) continue;
    bind_call call = {tools, {.owner = commands[j]}};
    qa_caught_error caught;
    if (!qa_try(register_owner, &call, &caught)) {
      release_row(tools, &call.row);
      qa_rethrow(&caught);
    }
    if (tools->binding_count == tools->binding_cap) {
      tools->binding_cap = tools->binding_cap ? tools->binding_cap * 2 : 4;
      tools->bindings = realloc(tools->bindings, tools->binding_cap * sizeof *tools->bindings);
      if (tools->bindings == NULL) qa_range_error("Out of memory");
    }
    tools->bindings[tools->binding_count++] = call.row;
  }
}

void qa_app_application_tools_before_world_change(qa_app_application_tools *tools) {
  qa_application_spline_camera_reset(&tools->camera);
  for (size_t i = 0; i < tools->binding_count; i++) release_row(tools, &tools->bindings[i]);
  tools->binding_count = 0;
}

qa_scene_camera qa_app_application_tools_apply_camera(qa_app_application_tools *tools, qa_scene_camera camera) {
  return qa_application_spline_camera_apply(&tools->camera, camera, tools->services.milliseconds(tools->services.ctx));
}

qa_app_capture_frame_result qa_app_application_tools_frame_time(qa_app_application_tools *tools, double milliseconds, bool active) {
  qa_cvar_registry *cvars = tools->services.cvars(tools->services.ctx);
  double fps = cvars ? qa_cvar_registry_variable_value(cvars, "cl_avidemo") : 0;
  double timescale = 1;
  if (cvars) {
    qa_cvar_read read;
    if (qa_cvar_registry_find(cvars, "timescale", &read)) { timescale = read.numeric_value; qa_cvar_read_free(&read); }
  }
  bool force = (cvars ? qa_cvar_registry_variable_value(cvars, "cl_forceavidemo") : 0) != 0;
  return qa_app_source_capture_frame(milliseconds, fps, timescale, active, force);
}

static void timer_pop(void *timer) { qa_omni_timer_pop(timer); }
void *qa_app_application_tools_measure(qa_app_application_tools *tools, const char *name, void *(*operation)(void *ctx), void *ctx) {
  if (!qa_omni_timer_enabled(tools->timer)) return operation(ctx);
  qa_omni_timer_push(tools->timer, name);
  qa_cleanup pop;
  qa_cleanup_push(&pop, timer_pop, tools->timer);
  void *result = operation(ctx);
  qa_cleanup_pop(&pop);
  qa_omni_timer_pop(tools->timer);
  return result;
}

/* Queued diagnostics and camera loads settled when they were queued. */
const char *qa_app_application_tools_drain(qa_app_application_tools *tools) {
  qa_application_spline_camera_drain(&tools->camera);
  return NULL;
}

const char *qa_app_application_tools_close(qa_app_application_tools *tools) {
  qa_app_application_tools_before_world_change(tools);
  (void)qa_app_application_tools_drain(tools);
  qa_application_spline_camera_close(&tools->camera);
  qa_omni_timer_set_enabled(tools->timer, false);
  qa_omni_timer_reset(tools->timer);
  tools->closed = true;
  return NULL;
}

void qa_app_application_tools_destroy(qa_app_application_tools *tools) {
  if (tools == NULL) return;
  if (!tools->closed) (void)qa_app_application_tools_close(tools);
  qa_application_spline_camera_destroy(&tools->camera);
  qa_omni_timer_destroy(tools->timer);
  free(tools->bindings);
  free(tools);
}
