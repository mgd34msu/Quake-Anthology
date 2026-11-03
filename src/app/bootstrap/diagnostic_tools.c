/* Port of src/app/bootstrap/diagnostic-tools.ts. process.memoryUsage
 * becomes /proc/self/statm + mallinfo-free host bytes (rss only is
 * portable here; heap fields report rss-derived zeros as noted).
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/diagnostic_tools.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
typedef struct { const char *name; qa_command_handler_fn handler; void *ctx; } handler_row;
struct qa_app_diagnostic_tools {
  qa_command_buffer *commands;
  handler_row handlers[8];
  size_t handler_count;
  void *owned_ctx[8];
  size_t owned_count;
};
static void add(qa_app_diagnostic_tools *tools, qa_command_buffer *commands, const char *name,
                qa_command_handler_fn handler, void *ctx, const char *summary, const char *usage) {
  if (qa_command_buffer_exists(commands, name)) qa_range_error("Diagnostic command already registered: %s", name);
  qa_command_documentation doc = {summary, usage, NULL, 0, NULL, 0};
  qa_command_buffer_register_engine(commands, name, handler, ctx, &doc);
  tools->handlers[tools->handler_count++] = (handler_row){name, handler, ctx};
}
typedef struct { qa_omni_timer *timer; void (*print)(void *, const char *); void *print_ctx; } tool_ctx;
static const char *arg_at(qa_command_invocation *cmd, size_t i) {
  return cmd->argc > i + 1 ? cmd->argv[i + 1] : NULL;
}
static size_t arg_count(qa_command_invocation *cmd) { return cmd->argc > 0 ? cmd->argc - 1 : 0; }
static void timers_handler(void *ctx, qa_command_invocation *cmd) {
  tool_ctx *tool = ctx;
  const char *action = arg_at(cmd, 0);
  action = action ? action : "report";
  if (strcmp(action, "on") == 0 || strcmp(action, "off") == 0) qa_omni_timer_set_enabled(tool->timer, strcmp(action, "on") == 0);
  else if (strcmp(action, "reset") == 0) qa_omni_timer_reset(tool->timer);
  else if (strcmp(action, "report") == 0) {
    char *text = qa_omni_timer_format(tool->timer);
    tool->print(tool->print_ctx, text ? text : "");
    free(text);
  } else if (strcmp(action, "stamps") == 0) {
    size_t count = 0;
    qa_timer_stamp *stamps = qa_omni_timer_stamp_list(tool->timer, &count);
    size_t cap = 256 + count * 128, len = 0;
    char *text = malloc(cap);
    len += (size_t)snprintf(text + len, cap - len, "milliseconds\tname\n");
    for (size_t i = 0; i < count; i++) {
      char fixed[QA_JS_TO_FIXED_CAPACITY]; /* milliseconds.toFixed(3) */
      qa_js_to_fixed(stamps[i].milliseconds, 3, fixed, sizeof fixed);
      len += (size_t)snprintf(text + len, cap - len, "%s%s\t%s", i ? "\n" : "", fixed, stamps[i].name);
    }
    snprintf(text + len, cap - len, "\n");
    tool->print(tool->print_ctx, text);
    free(text);
    free(stamps);
  } else qa_range_error("Usage: timers [on|off|reset|report|stamps]");
}
static void timerstamp_handler(void *ctx, qa_command_invocation *cmd) {
  tool_ctx *tool = ctx;
  char label[1024] = "";
  for (size_t i = 0; i < arg_count(cmd); i++) {
    if (i) strncat(label, " ", sizeof label - strlen(label) - 1);
    strncat(label, arg_at(cmd, i), sizeof label - strlen(label) - 1);
  }
  if (label[0] == '\0') qa_range_error("Usage: timerstamp <label>");
  qa_omni_timer_stamp(tool->timer, label);
}
qa_app_capture_frame_result qa_app_source_capture_frame(double elapsed, double fps, double timescale, bool active, bool force) {
  /* Users set these cvars from the console; a negative value just means no capture clock. */
  if (isfinite(elapsed) && elapsed >= 0 && (!isfinite(fps) || !isfinite(timescale) || fps < 0 || timescale < 0))
    return (qa_app_capture_frame_result){elapsed, false};
  if (!isfinite(elapsed) || !isfinite(fps) || !isfinite(timescale) || elapsed < 0 || fps < 0 || timescale < 0)
    qa_range_error("Invalid capture clock");
  double frames = trunc(fps);
  if (frames == 0 || elapsed == 0) return (qa_app_capture_frame_result){elapsed, false};
  float product = (float)(trunc(1000.0 / frames) * (float)timescale); /* Math.fround staged */
  double milliseconds = qa_js_max(1, trunc(product));
  return (qa_app_capture_frame_result){milliseconds, active || force};
}
qa_app_diagnostic_tools *qa_app_register_diagnostic_tools(qa_command_buffer *commands, qa_omni_timer *timer,
                                                          void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_app_diagnostic_tools *tools = calloc(1, sizeof *tools);
  if (!tools) return NULL;
  tools->commands = commands;
  tool_ctx *ctx = malloc(sizeof *ctx);
  *ctx = (tool_ctx){timer, print, print_ctx};
  tools->owned_ctx[tools->owned_count++] = ctx;
  add(tools, commands, "timers", timers_handler, ctx, "Inspect application profiler timings.", "timers [on|off|reset|report|stamps]");
  add(tools, commands, "timerstamp", timerstamp_handler, ctx, "Inspect application profiler timings.", "timerstamp <label>");
  return tools;
}
typedef struct { qa_app_runtime_diagnostic_services services; } runtime_ctx;
static void meminfo_handler(void *ctx, qa_command_invocation *cmd) {
  (void)cmd;
  runtime_ctx *rt = ctx;
  long rss = 0;
  FILE *file = fopen("/proc/self/statm", "r");
  if (file) { long total = 0, resident = 0; if (fscanf(file, "%ld %ld", &total, &resident) == 2) rss = resident * sysconf(_SC_PAGESIZE); fclose(file); }
  char text[256];
  /* Bun heap counters have no C counterpart; rss is the actual host bytes. */
  snprintf(text, sizeof text, "Host process bytes: rss=%ld heapTotal=0 heapUsed=0 external=0 arrayBuffers=0\n", rss);
  rt->services.print(rt->services.ctx, text);
}
static void path_handler(void *ctx, qa_command_invocation *cmd) {
  (void)cmd;
  runtime_ctx *rt = ctx;
  qa_app_runtime_mounts mounts = rt->services.mounts(rt->services.ctx);
  mounts.assert_open(mounts.ctx);
  char *text = mounts.path_report(mounts.ctx);
  rt->services.print(rt->services.ctx, text ? text : "");
  free(text);
}
typedef struct { runtime_ctx *rt; char *directory, *extension; } dir_op;
static void dir_run(void *op_ctx) {
  dir_op *op = op_ctx;
  qa_app_runtime_mounts mounts = op->rt->services.mounts(op->rt->services.ctx);
  size_t count = 0;
  char **files = mounts.list_files(mounts.ctx, op->directory, op->extension, &count);
  size_t cap = 256;
  for (size_t i = 0; i < count; i++) cap += strlen(files[i]) + 1;
  char *text = malloc(cap); size_t len = 0; text[0] = '\0';
  for (size_t i = 0; i < count; i++) { len += (size_t)snprintf(text + len, cap - len, "%s%s", i ? "\n" : "", files[i]); free(files[i]); }
  free(files);
  snprintf(text + len, cap - len, "\n%zu files\n", count);
  op->rt->services.print(op->rt->services.ctx, text);
  free(text);
  free(op->directory); free(op->extension); free(op);
}
static char *dup_text(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static void dir_handler(void *ctx, qa_command_invocation *cmd) {
  runtime_ctx *rt = ctx;
  const char *directory = arg_at(cmd, 0);
  dir_op *op = malloc(sizeof *op);
  op->rt = rt;
  op->directory = dup_text(directory && strcmp(directory, ".") != 0 ? directory : "");
  const char *extension = arg_at(cmd, 1);
  op->extension = dup_text(extension ? extension : "");
  rt->services.queue(rt->services.ctx, dir_run, op);
}
typedef struct { runtime_ctx *rt; char *path; } touch_op;
static void touch_run(void *op_ctx) {
  touch_op *op = op_ctx;
  qa_app_runtime_mounts mounts = op->rt->services.mounts(op->rt->services.ctx);
  mounts.open_file(mounts.ctx, op->path);
  free(op->path); free(op);
}
static void touch_handler(void *ctx, qa_command_invocation *cmd) {
  runtime_ctx *rt = ctx;
  const char *path = arg_at(cmd, 0);
  if (arg_count(cmd) != 1 || !path) qa_range_error("Usage: touchFile <file>");
  touch_op *op = malloc(sizeof *op);
  op->rt = rt; op->path = dup_text(path);
  rt->services.queue(rt->services.ctx, touch_run, op);
}
static void resourceinfo_handler(void *ctx, qa_command_invocation *cmd) {
  (void)cmd;
  runtime_ctx *rt = ctx;
  qa_app_runtime_mounts mounts = rt->services.mounts(rt->services.ctx);
  mounts.assert_open(mounts.ctx);
  char *json = mounts.opened_resources_json(mounts.ctx);
  char *text = malloc(strlen(json ? json : "null") + 2);
  sprintf(text, "%s\n", json ? json : "null");
  rt->services.print(rt->services.ctx, text);
  free(text); free(json);
}
static void frameinfo_handler(void *ctx, qa_command_invocation *cmd) {
  (void)cmd;
  runtime_ctx *rt = ctx;
  qa_app_diagnostic_frame frame = rt->services.frame(rt->services.ctx);
  char text[1024];
  snprintf(text, sizeof text, "{\n  \"frame\": %s,\n  \"milliseconds\": %s,\n  \"map\": \"%s\",\n  \"renderer\": \"%s\",\n  \"clients\": %s\n}\n",
      QA_JS_NUMBER(frame.frame), QA_JS_NUMBER(frame.milliseconds), frame.map ? frame.map : "", frame.renderer ? frame.renderer : "", QA_JS_NUMBER(frame.clients));
  rt->services.print(rt->services.ctx, text);
}
qa_app_diagnostic_tools *qa_app_register_runtime_diagnostics(qa_app_runtime_diagnostic_services services) {
  qa_app_diagnostic_tools *tools = calloc(1, sizeof *tools);
  if (!tools) return NULL;
  tools->commands = services.commands;
  runtime_ctx *ctx = malloc(sizeof *ctx);
  ctx->services = services;
  tools->owned_ctx[tools->owned_count++] = ctx;
  add(tools, services.commands, "meminfo", meminfo_handler, ctx, "Report the actual host process memory in bytes.", "meminfo");
  add(tools, services.commands, "path", path_handler, ctx, "Show active filesystem search order and overrides.", "path");
  add(tools, services.commands, "dir", dir_handler, ctx, "List files through the active mounted resource owner.", "dir [path] [extension]");
  add(tools, services.commands, "touchFile", touch_handler, ctx, "Open a file through the active filesystem and record its actual reference.", "touchFile <file>");
  add(tools, services.commands, "resourceinfo", resourceinfo_handler, ctx, "Report resources actually opened by the active filesystem.", "resourceinfo");
  add(tools, services.commands, "frameinfo", frameinfo_handler, ctx, "Report current application frame and source state.", "frameinfo");
  return tools;
}
void qa_app_diagnostic_tools_destroy(qa_app_diagnostic_tools *tools) {
  if (!tools) return;
  for (size_t i = 0; i < tools->handler_count; i++)
    qa_command_buffer_unregister(tools->commands, tools->handlers[i].name, tools->handlers[i].handler, tools->handlers[i].ctx, true);
  for (size_t i = 0; i < tools->owned_count; i++) free(tools->owned_ctx[i]);
  free(tools);
}

/* ---- RuntimeDiagnosticServices.mounts over a MountedContent ---- */
#include "qa/content/mounts.h"
#include "qa/content/mods/declaration_writer.h"
#include "qa/content/roots.h"
#include "qa/persistence/save_image.h"
static void rm_assert_open(void *ctx) { qa_content_mounted_assert_open(ctx); }
typedef struct { char *data; size_t length, capacity; } text_builder;
static void tb_append(text_builder *b, const char *text, size_t length) {
  if (b->length + length + 1 > b->capacity) {
    while (b->length + length + 1 > b->capacity) b->capacity = b->capacity ? b->capacity * 2 : 256;
    b->data = realloc(b->data, b->capacity);
    if (b->data == NULL) qa_range_error("Out of memory");
  }
  memcpy(b->data + b->length, text, length);
  b->length += length;
  b->data[b->length] = '\0';
}
static void tb_text(text_builder *b, const char *text) { tb_append(b, text, strlen(text)); }
/* TS describe(id): `${id} ${archivePath | rootPath}`; a missing mount throws. */
static void describe_mount(text_builder *b, const qa_resolved_mount_plan *plan, qa_mount_id id) {
  for (size_t i = 0; i < plan->mount_count; i++) {
    const qa_content_mount *mount = &plan->mounts[i];
    const qa_mount_identity *identity = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? &mount->as.archive.identity : &mount->as.loose.identity;
    if (strcmp(identity->id, id) != 0) continue;
    tb_text(b, id);
    tb_text(b, " ");
    tb_text(b, mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? mount->as.archive.archive_path : mount->as.loose.root_path);
    return;
  }
  free(b->data);
  qa_raise("Error", 0, 0, "Missing active mount %s", id);
}
/* The text the TS path command prints: the default order, then each prefix order. */
static char *rm_path_report(void *ctx) {
  const qa_resolved_mount_plan *plan = qa_content_mounted_plan(ctx);
  text_builder b = {0};
  tb_text(&b, "");
  for (size_t i = 0; i < plan->default_order_count; i++) {
    if (i != 0) tb_text(&b, "\n");
    describe_mount(&b, plan, plan->default_order[i]);
  }
  tb_text(&b, "\n");
  for (size_t o = 0; o < plan->prefix_order_count; o++) {
    const qa_prefix_mount_order *order = &plan->prefix_orders[o];
    tb_text(&b, order->prefix);
    tb_text(&b, ":\n");
    for (size_t i = 0; i < order->mount_count; i++) {
      if (i != 0) tb_text(&b, "\n");
      describe_mount(&b, plan, order->mounts[i]);
    }
    tb_text(&b, "\n");
  }
  return b.data;
}
/* JSON.stringify(value, null, 2) layout of a compact JSON text. */
static char *json_indent(const char *compact) {
  text_builder b = {0};
  tb_text(&b, "");
  int depth = 0;
  bool in_string = false;
  for (const char *p = compact; *p != '\0'; p++) {
    char c = *p;
    if (in_string) {
      tb_append(&b, p, 1);
      if (c == '\\' && p[1] != '\0') { tb_append(&b, ++p, 1); continue; }
      if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') { in_string = true; tb_append(&b, p, 1); continue; }
    if ((c == '{' && p[1] == '}') || (c == '[' && p[1] == ']')) { tb_append(&b, p, 2); p++; continue; }
    if (c == '{' || c == '[' || c == ',') {
      tb_append(&b, p, 1);
      if (c != ',') depth++;
      tb_text(&b, "\n");
      for (int i = 0; i < depth; i++) tb_text(&b, "  ");
      continue;
    }
    if (c == '}' || c == ']') {
      depth--;
      tb_text(&b, "\n");
      for (int i = 0; i < depth; i++) tb_text(&b, "  ");
      tb_append(&b, p, 1);
      continue;
    }
    if (c == ':') { tb_text(&b, ": "); continue; }
    tb_append(&b, p, 1);
  }
  return b.data;
}
static char *rm_opened_resources_json(void *ctx) {
  size_t count = 0;
  const qa_resolved_resource_reference *resources = qa_content_mounted_opened_resources(ctx, &count);
  qa_save_value *array = qa_save_value_array();
  if (array == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_value *value = qa_save_resource_reference_value(&resources[i]);
    if (value == NULL || !qa_save_value_push(array, value)) qa_range_error("Out of memory");
  }
  char *compact = qa_json_stringify_value(array, NULL);
  qa_save_value_free(array);
  char *text = json_indent(compact);
  free(compact);
  return text;
}
static char **rm_list_files(void *ctx, const char *directory, const char *extension, size_t *out_count) {
  char **files = qa_content_mounted_list_files(ctx, directory, extension, out_count);
  if (files == NULL) qa_range_error("Out of memory");
  return files;
}
static void rm_open_file(void *ctx, const char *path) {
  qa_content_opened_resource opened;
  if (qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) free(opened.bytes);
}
qa_app_runtime_mounts qa_app_runtime_mounts_of(struct qa_content_mounted *mounts) {
  return (qa_app_runtime_mounts){mounts, rm_assert_open, rm_path_report, rm_opened_resources_json, rm_list_files, rm_open_file};
}
