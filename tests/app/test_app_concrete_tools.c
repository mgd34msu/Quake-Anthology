/*
 * ApplicationTools (src/app/bootstrap/application-tools.ts) over the real spline camera, diagnostic
 * registrations, ConfigStore and a loose MountedContent: a port of
 * ../quake-anthology-ts/tests/camera/application-tools.test.ts, plus the runtime diagnostics reaching the
 * MountedContent (path, touchFile, resourceinfo, dir) with the text bun prints for the same mounts
 * (tests/app/oracles/app_concrete_tools.ts). Every services callback checks its sentinel ctx.
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/app/bootstrap/application_tools.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/content/mounts.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/view.h"
#include "qa/app/bootstrap/renderer_diagnostics.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)

static int sentinel;
static void check_ctx(void *ctx) {
  if (ctx != &sentinel) { fprintf(stderr, "wrong services ctx\n"); abort(); }
}
typedef struct fixture {
  char root[256];
  qa_content_mounted *mounts;
  qa_cvar_registry *cvars;
  double time;
  char printed[16384];
} fixture;
static fixture fx;

static qa_content_mounted *sv_mounts(void *ctx) { check_ctx(ctx); return fx.mounts; }
static char *sv_output_root(void *ctx) {
  check_ctx(ctx);
  char *path = malloc(strlen(fx.root) + 8);
  sprintf(path, "%s/output", fx.root);
  return path;
}
static double sv_milliseconds(void *ctx) { check_ctx(ctx); return fx.time; }
static qa_cvar_registry *sv_cvars(void *ctx) { check_ctx(ctx); return fx.cvars; }
static qa_app_diagnostic_frame sv_frame(void *ctx) {
  check_ctx(ctx);
  return (qa_app_diagnostic_frame){0, fx.time, 1, "fixture", "cpu"};
}
static qa_app_renderer_diagnostics sv_renderer(void *ctx) {
  check_ctx(ctx);
  qa_app_renderer_diagnostics d;
  memset(&d, 0, sizeof d);
  d.backend = "cpu";
  d.width = 640;
  d.height = 480;
  return d;
}
static const qa_registered_scene_material *sv_shaders(void *ctx, bool sorted, size_t *out_count) {
  check_ctx(ctx);
  (void)sorted;
  *out_count = 0;
  return NULL;
}
static qa_q3_pres_q3_renderer_resources *sv_resources(void *ctx, qa_command_context source) {
  check_ctx(ctx);
  (void)source;
  return NULL;
}
static void sv_print(void *ctx, const char *text, const qa_command_context *source) {
  check_ctx(ctx);
  (void)source;
  strncat(fx.printed, text, sizeof fx.printed - strlen(fx.printed) - 1);
}

static void write_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  fputs(text, file);
  fclose(file);
}
static bool exists(const char *path) {
  struct stat info;
  return stat(path, &info) == 0;
}

static qa_timer_report *find_report(qa_timer_report *reports, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) if (strcmp(reports[i].name, name) == 0) return &reports[i];
  return NULL;
}
static void *answer(void *ctx) { return ctx; }

static void test_application_tools(void) {
  char template[] = "/tmp/application-tools-XXXXXX";
  CHECK(mkdtemp(template) != NULL);
  snprintf(fx.root, sizeof fx.root, "%s", template);
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "application-tools"));
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner.session;
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context = context;
  qa_command_buffer *authority = qa_command_buffer_create(&options);
  qa_command_buffer *input = qa_command_buffer_create(&options);
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context = context;
  fx.cvars = qa_cvar_registry_create(&cvar_options);
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_register(fx.cvars, "cl_avidemo", "30", 0, &snapshot));
  qa_cvar_read_free(&snapshot);
  CHECK(qa_cvar_registry_register(fx.cvars, "cl_forceavidemo", "0", 0, &snapshot));
  qa_cvar_read_free(&snapshot);
  CHECK(qa_cvar_registry_register(fx.cvars, "timescale", "1", 0, &snapshot));
  qa_cvar_read_free(&snapshot);

  qa_content_mount mount;
  memset(&mount, 0, sizeof mount);
  mount.kind = QA_CONTENT_MOUNT_LOOSE;
  mount.as.loose.identity = qa_create_mount_identity("mount:tools:fixture", "q3:classic:base:fixture", 0);
  mount.as.loose.root_path = fx.root;
  qa_mount_id order[1] = {"mount:tools:fixture"};
  qa_resolved_mount_plan plan = {"mount-plan:tools:fixture", &mount, 1, order, 1, NULL, 0};
  qa_open_mount_options mount_options;
  memset(&mount_options, 0, sizeof mount_options);
  fx.mounts = qa_open_mount_plan(&plan, &mount_options);
  CHECK(fx.mounts != NULL);
  char path[512];
  snprintf(path, sizeof path, "%s/view.camera", fx.root);
  write_file(path, "cameraPathDef { time 1 camera_interpolated { startPos ( 0 0 0 ) endPos ( 10 0 0 ) } }");

  qa_app_application_tool_services services = {
    .ctx = &sentinel, .mounts = sv_mounts, .output_root = sv_output_root, .milliseconds = sv_milliseconds,
    .cvars = sv_cvars, .frame = sv_frame, .renderer = sv_renderer, .shaders = sv_shaders,
    .resources = sv_resources, .print = sv_print,
  };
  qa_app_application_tools *tools = qa_app_application_tools_open(authority, services);
  CHECK(tools != NULL);
  qa_command_buffer *both[2] = {authority, input};
  qa_app_application_tools_bind(tools, both, 2);
  qa_app_application_tools_bind(tools, both, 2);
  qa_command_buffer_append(authority, "loadcamera view.camera\n", NULL, -1);
  qa_command_buffer_execute(authority);
  CHECK(qa_app_application_tools_drain(tools) == NULL);
  qa_command_buffer_append(input, "startcamera;timers on;timerstamp started;savecamera exported.camera\n", NULL, -1);
  qa_command_buffer_execute(input);
  qa_scene_camera camera;
  memset(&camera, 0, sizeof camera);
  camera.origin = (qa_vec3){50, 0, 0};
  camera.axis.vectors[0] = (qa_vec3){1, 0, 0};
  camera.axis.vectors[1] = (qa_vec3){0, 1, 0};
  camera.axis.vectors[2] = (qa_vec3){0, 0, 1};
  camera.viewport = (qa_rect){0, 0, 640, 480};
  camera.projection = qa_scene_perspective_projection(90, 60, 1000, 4);
  fx.time = 500;
  CHECK(qa_app_application_tools_apply_camera(tools, camera).origin.x == 5);
  snprintf(path, sizeof path, "%s/output/exported.camera", fx.root);
  CHECK(exists(path));
  qa_app_capture_frame_result frame = qa_app_application_tools_frame_time(tools, 100, true);
  CHECK(frame.milliseconds == 33 && frame.capture);
  CHECK(qa_app_application_tools_measure(tools, "simulation", answer, &sentinel) == &sentinel);
  size_t report_count = 0;
  qa_timer_report *reports = qa_omni_timer_report(qa_app_application_tools_timer(tools), &report_count);
  qa_timer_report *simulation = find_report(reports, report_count, "simulation");
  CHECK(simulation != NULL && simulation->calls == 1);
  free(reports);
  size_t stamp_count = 0;
  qa_timer_stamp *stamps = qa_omni_timer_stamp_list(qa_app_application_tools_timer(tools), &stamp_count);
  CHECK(stamp_count > 0 && strcmp(stamps[0].name, "started") == 0);
  free(stamps);

  /* Runtime diagnostics over the MountedContent (oracle: tests/app/oracles/app_concrete_tools.ts). */
  fx.printed[0] = '\0';
  qa_command_buffer_append(authority, "touchFile view.camera\npath\nresourceinfo\ndir . camera\n", NULL, -1);
  qa_command_buffer_execute(authority);
  FILE *file = fopen("tests/app/oracles/app_concrete_tools.txt", "r");
  CHECK(file != NULL);
  if (file != NULL) {
    char expected[16384];
    size_t got = fread(expected, 1, sizeof expected - 1, file);
    expected[got] = '\0';
    fclose(file);
    char shown[16384], encoded[512];
    size_t at = 0, root_length = strlen(fx.root), encoded_length = 0;
    for (const char *p = fx.root; *p != '\0'; p++)
      encoded_length += (size_t)snprintf(encoded + encoded_length, sizeof encoded - encoded_length, *p == '/' ? "%%2F" : "%c", *p);
    for (const char *p = fx.printed; *p != '\0' && at + 8 < sizeof shown;) {
      if (strncmp(p, fx.root, root_length) == 0) { at += (size_t)snprintf(shown + at, sizeof shown - at, "<root>"); p += root_length; continue; }
      if (strncmp(p, encoded, encoded_length) == 0) { at += (size_t)snprintf(shown + at, sizeof shown - at, "<eroot>"); p += encoded_length; continue; }
      shown[at++] = *p++;
    }
    shown[at] = '\0';
    CHECK(strcmp(expected, shown) == 0);
    if (strcmp(expected, shown) != 0) fprintf(stderr, "expected:\n%s\nactual:\n%s\n", expected, shown);
  }

  qa_app_application_tools_before_world_change(tools);
  qa_scene_camera unchanged = qa_app_application_tools_apply_camera(tools, camera);
  CHECK(memcmp(&unchanged, &camera, sizeof camera) == 0);
  CHECK(!qa_command_buffer_exists(input, "loadcamera"));
  qa_command_buffer *one[1] = {authority};
  qa_app_application_tools_bind(tools, one, 1);
  CHECK(qa_command_buffer_exists(authority, "loadcamera"));
  CHECK(!qa_command_buffer_exists(input, "loadcamera"));
  CHECK(qa_app_application_tools_close(tools) == NULL);
  CHECK(!qa_command_buffer_exists(authority, "timers"));
  qa_app_application_tools_destroy(tools);

  qa_content_mounted_close(fx.mounts);
  qa_content_mounted_destroy(fx.mounts);
  qa_cvar_registry_destroy(fx.cvars);
  qa_command_buffer_destroy(input);
  qa_command_buffer_destroy(authority);
  qa_identity_owner_destroy(&owner);
  char command[400];
  snprintf(command, sizeof command, "rm -rf '%s'", fx.root);
  CHECK(system(command) == 0);
}

int main(void) {
  test_application_tools();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
