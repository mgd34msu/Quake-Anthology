/* C checks for the application's lower dependencies (docs/application_layout.md
 * "Dependency gaps"): ApplicationImageSettings (image-settings.ts) and the shared
 * client settings it registers. Expected values come from TS under bun:
 *   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/application_deps.ts
 * Content rows need QTS_TEST_CORPUS (the TS corpus root). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/authored_start.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/image_settings.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/qvm_grapple_selection.h"
#include "qa/app/bootstrap/weapon_behavior_selection.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/content/roots.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/compat/qvm/grapple_profile.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/foundation/support.h"
#include "qa/app/bootstrap/server_administration.h"
#include "qa/settings/config.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/js_number.h"

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; \
  if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0) { failures++; \
    fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", b_ ? b_ : "(null)"); } } while (0)
#define CHECK_NUM(a, b) do { double a_ = (a), b_ = (b); checks++; \
  if (!(a_ == b_)) { failures++; fprintf(stderr, "%s:%d: %.17g != %.17g\n", __FILE__, __LINE__, a_, b_); } } while (0)

static int sentinel;
static char printed[4096];
static void capture_print(void *ctx, const char *text) {
  CHECK(ctx == &sentinel);
  size_t used = strlen(printed);
  snprintf(printed + used, sizeof printed - used, "%s", text);
}
static void make_root(char *buffer, size_t capacity) {
  const char *tmp = getenv("TMPDIR");
  snprintf(buffer, capacity, "%s/qa-deps-XXXXXX", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
  if (mkdtemp(buffer) == NULL) { perror("mkdtemp"); exit(2); }
}
static void remove_tree(const char *path) {
  char command[1200];
  snprintf(command, sizeof command, "rm -rf '%s'", path);
  if (system(command) != 0) fprintf(stderr, "cleanup failed: %s\n", path);
}
static char *variable(qa_cvar_registry *cvars, const char *name) { return qa_cvar_registry_variable_string(cvars, name); }
static void check_variable(qa_cvar_registry *cvars, const char *name, const char *expected) {
  char *value = variable(cvars, name);
  CHECK_STR(value, expected);
  free(value);
}
/* "<value>/<flags>" of the canonical cvar, "-" when absent (oracle rows `<dialect>:<name>`). */
static void check_row(qa_cvar_registry *cvars, const char *name, const char *expected) {
  qa_cvar_read found;
  char text[128] = "-";
  if (qa_cvar_registry_find(cvars, name, &found)) {
    char *canonical = qa_cvar_registry_canonical_name(cvars, name);
    qa_cvar_read target;
    double flags = found.flags;
    if (canonical != NULL && qa_cvar_registry_find(cvars, canonical, &target)) { flags = target.flags; qa_cvar_read_free(&target); }
    free(canonical);
    char *value = variable(cvars, name);
    snprintf(text, sizeof text, "%s/%s", value, QA_JS_NUMBER(flags));
    free(value);
    qa_cvar_read_free(&found);
  }
  if (strcmp(text, expected) != 0) fprintf(stderr, "  row %s\n", name);
  CHECK_STR(text, expected);
}
static qa_command_context console_context(qa_identity_owner *identity) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = identity->session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  return context;
}
static void check_default_policy(qa_app_image_settings *settings) {
  qa_scene_image_policy_owned policy;
  qa_app_image_settings_policy(settings, &policy);
  CHECK_NUM(policy.policy.override_level, 1);
  CHECK(policy.policy.override_usage_count == 5);
  static const char *const usages[] = {"skin", "sprite", "wall", "picture", "sky"};
  for (size_t i = 0; i < 5 && i < policy.policy.override_usage_count; i++) CHECK_STR(policy.policy.override_usages[i], usages[i]);
  CHECK(!policy.policy.has_formats);
  qa_scene_image_policy_owned_destroy(&policy);
  qa_app_model_policy model = qa_app_image_settings_model_policy(settings);
  CHECK(model.q1_enhanced && model.q2_load && model.q2_use && model.q2_distance == 2048 && !model.has_distance);
}

/* oracle rows q1-netquake:* and q3:* */
static void test_registered_settings(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "deps"));
  static const struct { qa_command_dialect dialect; const char *volume, *bgmvolume; } rows[] = {
    { QA_COMMAND_DIALECT_Q1_NETQUAKE, "0.7/1", "1/1" }, { QA_COMMAND_DIALECT_Q3, "0.8/1", "0.25/1" },
  };
  for (size_t r = 0; r < 2; r++) {
    char root[1024];
    make_root(root, sizeof root);
    qa_app_image_settings_options options;
    memset(&options, 0, sizeof options);
    options.context = console_context(&identity);
    options.dialect = rows[r].dialect;
    options.user_content_root = root;
    options.defer_persistence = true;
    options.print = capture_print; options.print_ctx = &sentinel;
    printed[0] = '\0';
    qa_app_image_settings *settings = qa_app_image_settings_open(options);
    CHECK(settings != NULL);
    qa_cvar_registry *cvars = qa_app_image_settings_cvars(settings);
    check_row(cvars, "volume", rows[r].volume);
    check_row(cvars, "bgmvolume", rows[r].bgmvolume);
    check_row(cvars, "gamma", "1/1");
    check_row(cvars, "vid_gamma", "1/1");
    check_row(cvars, "r_gamma", "1/1");
    check_row(cvars, "fov", "90/1");
    check_row(cvars, "s_geometryAcoustics", "0/1");
    check_row(cvars, "viewsize", "100/1");
    check_row(cvars, "cl_sbar", "0/1");
    check_row(cvars, "s_outputRate", "44100/1");
    check_row(cvars, "music_shuffle", "0/1");
    check_default_policy(settings);
    size_t persisted = 99;
    (void)qa_app_image_settings_persisted_entries(settings, &persisted);
    CHECK(persisted == 0);
    qa_app_image_settings_close(settings);
    qa_app_image_settings_destroy(settings);
    CHECK_STR(printed, "");
    remove_tree(root);
  }
  qa_identity_owner_destroy(&identity);
}

static const char expected_images_cfg[] =
  "// Generated by quake-typescript\n"
  "set r_model_distance \"SOURCE\"\n"
  "set gl_md5_distance \"2048\"\n"
  "set gl_md5_use \"1\"\n"
  "set gl_md5_load \"1\"\n"
  "set r_enhancedmodels \"0\"\n"
  "set r_texture_formats \"png tga\"\n"
  "set r_texture_overrides \"-1\"\n"
  "set r_override_textures \"1\"\n"
  "set gl_debug_linewidth \"2\"\n"
  "set r_smp \"1\"\n"
  "set r_swapInterval \"1\"\n"
  "set r_fullscreen \"0\"\n"
  "set r_customheight \"700\"\n"
  "set r_customwidth \"900\"\n"
  "set cl_sbar \"0\"\n"
  "set viewsize \"100\"\n"
  "set ui_seat4_colorMode \"standard\"\n"
  "set ui_seat4_typeface \"standard\"\n"
  "set ui_seat4_crosshair \"1\"\n"
  "set ui_seat4_captions \"1\"\n"
  "set ui_seat4_reducedFlashes \"0\"\n"
  "set ui_seat4_highContrast \"0\"\n"
  "set ui_seat4_crosshairSize \"8\"\n"
  "set ui_seat4_menuScale \"1\"\n"
  "set ui_seat4_textScale \"1\"\n"
  "set ui_seat4_hudScale \"1\"\n"
  "set ui_seat3_colorMode \"standard\"\n"
  "set ui_seat3_typeface \"standard\"\n"
  "set ui_seat3_crosshair \"1\"\n"
  "set ui_seat3_captions \"1\"\n"
  "set ui_seat3_reducedFlashes \"0\"\n"
  "set ui_seat3_highContrast \"0\"\n"
  "set ui_seat3_crosshairSize \"8\"\n"
  "set ui_seat3_menuScale \"1\"\n"
  "set ui_seat3_textScale \"1\"\n"
  "set ui_seat3_hudScale \"1\"\n"
  "set ui_seat2_colorMode \"standard\"\n"
  "set ui_seat2_typeface \"standard\"\n"
  "set ui_seat2_crosshair \"1\"\n"
  "set ui_seat2_captions \"1\"\n"
  "set ui_seat2_reducedFlashes \"0\"\n"
  "set ui_seat2_highContrast \"0\"\n"
  "set ui_seat2_crosshairSize \"8\"\n"
  "set ui_seat2_menuScale \"1\"\n"
  "set ui_seat2_textScale \"1\"\n"
  "set ui_seat2_hudScale \"1\"\n"
  "set ui_seat1_colorMode \"standard\"\n"
  "set ui_seat1_typeface \"standard\"\n"
  "set ui_seat1_crosshair \"1\"\n"
  "set ui_seat1_captions \"1\"\n"
  "set ui_seat1_reducedFlashes \"0\"\n"
  "set ui_seat1_highContrast \"0\"\n"
  "set ui_seat1_crosshairSize \"8\"\n"
  "set ui_seat1_menuScale \"1\"\n"
  "set ui_seat1_textScale \"1\"\n"
  "set ui_seat1_hudScale \"1\"\n"
  "set ui_seat4_language \"english\"\n"
  "set ui_seat3_language \"english\"\n"
  "set ui_seat2_language \"english\"\n"
  "set ui_seat1_language \"english\"\n"
  "set s_geometryAcoustics \"0\"\n"
  "set r_lodCurveError \"250\"\n"
  "set r_railSegmentLength \"32\"\n"
  "set r_railCoreWidth \"6\"\n"
  "set r_railWidth \"16\"\n"
  "set r_detailTextures \"1\"\n"
  "set r_vertexLight \"0\"\n"
  "set r_shadows \"0\"\n"
  "set r_gamma \"1.5\"\n"
  "set con_scale \"0\"\n"
  "set fov \"90\"\n";

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  static char buffer[16384];
  size_t length = fread(buffer, 1, sizeof buffer - 1, file);
  fclose(file);
  buffer[length] = '\0';
  return buffer;
}
static void model_distance_far(void *ctx) { (void)qa_app_image_settings_model_policy(ctx); }
/* oracle rows loaded:* */
static void test_loaded_settings(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "deps"));
  char root[1024], path[1200];
  make_root(root, sizeof root);
  snprintf(path, sizeof path, "%s/settings", root);
  mkdir(path, 0700);
  snprintf(path, sizeof path, "%s/settings/images.cfg", root);
  FILE *file = fopen(path, "w");
  CHECK(file != NULL);
  fputs("seta r_gamma \"2\"\nseta r_customwidth \"900\"\nset volume \"0.3\"\nr_enhancedmodels 0\nseta gl_debug_linewidth \"5\"\n"
        "unknowncmd 1\nseta r_texture_formats \"png tga\"\nseta r_gamma \"1.5\"\n", file);
  fclose(file);
  qa_app_image_settings_options options;
  memset(&options, 0, sizeof options);
  options.context = console_context(&identity);
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.user_content_root = root;
  options.has_display_height = true; options.display_height = 700;
  options.has_render_worker = true; options.render_worker = true;
  options.has_gamma = true; options.gamma = 1.25;
  options.print = capture_print; options.print_ctx = &sentinel;
  printed[0] = '\0';
  qa_app_image_settings *settings = qa_app_image_settings_open(options);
  qa_cvar_registry *cvars = qa_app_image_settings_cvars(settings);
  size_t count = 0;
  const qa_cvar_archive_entry *persisted = qa_app_image_settings_persisted_entries(settings, &count);
  static const char *const expected[][2] = {
    {"r_gamma", "1.5"}, {"r_customwidth", "900"}, {"volume", "0.3"}, {"r_enhancedmodels", "0"}, {"gl_debug_linewidth", "5"}, {"r_texture_formats", "png tga"},
  };
  CHECK(count == 6);
  for (size_t i = 0; i < count && i < 6; i++) { CHECK_STR(persisted[i].name, expected[i][0]); CHECK_STR(persisted[i].value, expected[i][1]); }
  /* TS prints nothing while loading images.cfg, the unknown command included. */
  CHECK_STR(printed, "");
  check_variable(cvars, "r_gamma", "1.5");
  check_variable(cvars, "r_customwidth", "900");
  check_variable(cvars, "r_customheight", "700");
  check_variable(cvars, "r_smp", "1");
  check_variable(cvars, "volume", "0.3");
  check_variable(cvars, "r_enhancedmodels", "0");
  check_variable(cvars, "gl_debug_linewidth", "5");
  CHECK_NUM(qa_app_image_settings_gamma(settings), 1.5);
  qa_scene_image_policy_owned policy;
  qa_app_image_settings_policy(settings, &policy);
  CHECK(policy.policy.has_formats && policy.policy.format_count == 2);
  if (policy.policy.format_count == 2) { CHECK_STR(policy.policy.formats[0], "png"); CHECK_STR(policy.policy.formats[1], "tga"); }
  qa_scene_image_policy_owned_destroy(&policy);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, "gl_debug_linewidth", "-1", false, &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK_NUM(qa_app_image_settings_debug_line_width(settings), 2);
  check_variable(cvars, "gl_debug_linewidth", "2");
  if (qa_cvar_registry_set(cvars, "r_model_distance", "  4096 ", false, &snapshot)) qa_cvar_read_free(&snapshot);
  qa_app_model_policy model = qa_app_image_settings_model_policy(settings);
  CHECK(!model.q1_enhanced && model.q2_load && model.q2_use && model.q2_distance == 2048 && model.has_distance && model.distance == 4096);
  if (qa_cvar_registry_set(cvars, "r_model_distance", "far", false, &snapshot)) qa_cvar_read_free(&snapshot);
  qa_caught_error caught;
  CHECK(!qa_try(model_distance_far, settings, &caught));
  CHECK_STR(caught.name, "Error");
  CHECK_STR(caught.message, "r_model_distance requires source or a finite distance");
  if (qa_cvar_registry_set(cvars, "r_model_distance", "SOURCE", false, &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK_STR(printed, "Debug line width must be a positive finite number.\n");
  qa_app_image_settings_close(settings);
  CHECK_STR(read_file(path), expected_images_cfg);
  qa_app_image_settings_enable_persistence(settings);
  qa_app_image_settings_close(settings);
  CHECK_STR(read_file(path), expected_images_cfg);
  qa_app_image_settings_destroy(settings);
  remove_tree(root);
  qa_identity_owner_destroy(&identity);
}

/* oracle rows cand:* (tests/app/candidate-image-settings.test.ts) */
static double *seen_values; static size_t seen_count;
static double candidate_seen[8]; static size_t candidate_seen_count;
static int view_sentinel, candidate_sentinel, replacement_sentinel;
static void view_changed(void *ctx, double value) { CHECK(ctx == &view_sentinel); seen_values = realloc(seen_values, (seen_count + 1) * sizeof *seen_values); seen_values[seen_count++] = value; }
static void candidate_changed(void *ctx, double value) { CHECK(ctx == &candidate_sentinel); if (candidate_seen_count < 8) candidate_seen[candidate_seen_count++] = value; }
static void replacement_changed(void *ctx, double value) { CHECK(ctx == &replacement_sentinel); (void)value; }
static void republish(void *ctx) { qa_app_image_client_settings_publish(ctx); }
static void execute(qa_command_buffer *commands, const char *text, const qa_command_context *context) {
  (void)qa_command_buffer_execute_now(commands, text, context, -1);
}
static void test_candidate_settings(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "deps"));
  qa_command_context context = console_context(&identity);
  char root[1024];
  make_root(root, sizeof root);
  qa_app_image_settings_options options;
  memset(&options, 0, sizeof options);
  options.context = context;
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.user_content_root = root;
  options.defer_persistence = true;
  options.print = capture_print; options.print_ctx = &sentinel;
  qa_app_image_settings *images = qa_app_image_settings_open(options);
  qa_app_view_settings *view = qa_app_view_settings_create(view_changed, &view_sentinel);
  qa_app_image_view_binding *release = qa_app_image_settings_bind_view_settings(images, view);
  qa_app_image_client_settings candidate = qa_app_image_settings_prepare_client_settings(images);
  qa_app_view_settings *candidate_view = qa_app_view_settings_create(candidate_changed, &candidate_sentinel);
  qa_app_image_view_binding *release_candidate_view = qa_app_image_settings_bind_view_settings(candidate.settings, candidate_view);
  qa_cvar_registry *candidate_cvars = qa_app_image_settings_cvars(candidate.settings);
  qa_command_buffer_options buffer_options;
  memset(&buffer_options, 0, sizeof buffer_options);
  buffer_options.dialect = QA_COMMAND_DIALECT_Q3;
  buffer_options.context = context;
  buffer_options.cvars = candidate_cvars;
  qa_command_buffer *commands = qa_command_buffer_create(&buffer_options);
  execute(commands, "gamma 0.5", &context);
  execute(commands, "s_volume 0.2", &context);
  execute(commands, "fov 120", &context);
  CHECK_NUM(qa_app_image_settings_gamma(candidate.settings), 2);
  check_variable(candidate_cvars, "volume", "0.2");
  check_variable(qa_app_image_settings_cvars(images), "volume", "0.8");
  CHECK_NUM(qa_app_view_settings_field_of_view(view), 90);
  CHECK(seen_count == 0);
  CHECK_NUM(qa_app_view_settings_field_of_view(candidate_view), 120);
  CHECK(candidate_seen_count == 1 && candidate_seen[0] == 120);
  execute(commands, "fov 15", &context);
  CHECK_NUM(qa_app_view_settings_field_of_view(candidate_view), 120);
  check_variable(candidate_cvars, "fov", "120");
  qa_app_image_view_binding_release(release_candidate_view);
  execute(commands, "fov 15", &context);
  check_variable(candidate_cvars, "fov", "120");
  qa_app_view_settings *replacement = qa_app_view_settings_create(replacement_changed, &replacement_sentinel);
  qa_app_image_view_binding *release_replacement = qa_app_image_settings_bind_view_settings(candidate.settings, replacement);
  qa_app_image_view_binding_release(release_candidate_view);
  execute(commands, "fov 125", &context);
  CHECK_NUM(qa_app_view_settings_field_of_view(replacement), 125);
  execute(commands, "fov 120", &context);
  qa_app_image_view_binding_release(release_replacement);
  qa_app_image_client_settings_validate_publication(&candidate);
  qa_app_image_view_binding_release(release);
  qa_app_image_client_settings_publish(&candidate);
  qa_cvar_registry *owner = qa_app_image_settings_cvars(images);
  CHECK_NUM(qa_app_image_settings_gamma(images), 2);
  check_variable(owner, "s_volume", "0.2");
  check_variable(owner, "fov", "120");
  qa_caught_error caught;
  CHECK(!qa_try(republish, &candidate, &caught));
  CHECK_STR(caught.message, "Cvar transfer already published");
  qa_app_image_client_settings rejected = qa_app_image_settings_prepare_client_settings(images);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(qa_app_image_settings_cvars(rejected.settings), "fov", "15", false, &snapshot)) qa_cvar_read_free(&snapshot);
  check_variable(qa_app_image_settings_cvars(rejected.settings), "fov", "120");
  if (qa_cvar_registry_set(owner, "volume", "0.4", false, &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK(!qa_try(republish, &rejected, &caught));
  CHECK_STR(caught.message, "Cvar owner changed during preparation");
  check_variable(owner, "volume", "0.4");
  release = qa_app_image_settings_bind_view_settings(images, view);
  qa_app_image_view_binding_release(release);
  qa_command_buffer_destroy(commands);
  qa_app_image_client_settings_destroy(&rejected);
  qa_app_image_client_settings_destroy(&candidate);
  qa_app_view_settings_destroy(replacement);
  qa_app_view_settings_destroy(candidate_view);
  qa_app_view_settings_destroy(view);
  qa_app_image_settings_close(images);
  qa_app_image_settings_destroy(images);
  free(seen_values);
  remove_tree(root);
  qa_identity_owner_destroy(&identity);
}

/* ---- content rows (QTS_TEST_CORPUS) ------------------------------------- */
static int opener_sentinel, for_content_sentinel, grapple_host_sentinel;
typedef struct reader_state { qa_content_mounted *mounted; uint8_t **buffers; size_t count; } reader_state;
static bool reader_open(void *ctx, const char *path, qa_catalog_opened_resource *out) {
  reader_state *state = ctx;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(state->mounted, path, NULL, NULL, &opened)) return false;
  state->buffers = realloc(state->buffers, (state->count + 1) * sizeof *state->buffers);
  state->buffers[state->count++] = opened.bytes;
  out->reference = *opened.reference; out->bytes = opened.bytes; out->byte_length = opened.byte_length;
  return true;
}
static void reader_close(void *ctx) {
  reader_state *state = ctx;
  qa_content_mounted_destroy(state->mounted);
  for (size_t i = 0; i < state->count; i++) free(state->buffers[i]);
  free(state->buffers); free(state);
}
static bool opener_open_plan(void *ctx, const qa_resolved_mount_plan *plan, qa_catalog_mounted_reader *out) {
  CHECK(ctx == &opener_sentinel);
  qa_open_mount_options options;
  memset(&options, 0, sizeof options);
  reader_state *state = calloc(1, sizeof *state);
  state->mounted = qa_open_mount_plan(plan, &options);
  out->ctx = state; out->open = reader_open; out->close = reader_close;
  return state->mounted != NULL;
}
/* forContent over the catalog's mounts for a content id (the test owns the mount sets). */
typedef struct opened_plan { qa_catalog_mount_list list; qa_mount_id *order; char *plan_id; qa_content_mounted *mounted; } opened_plan;
typedef struct content_mounts { int *sentinel; qa_content_catalog *catalog; opened_plan plans[8]; size_t count; } content_mounts;
static qa_content_mounted *open_content_plan(qa_content_catalog *catalog, qa_content_id content, opened_plan *plan) {
  memset(plan, 0, sizeof *plan);
  CHECK(qa_content_catalog_mounts_for(catalog, content, &plan->list));
  plan->order = calloc(plan->list.count + 1, sizeof *plan->order);
  for (size_t i = 0; i < plan->list.count; i++)
    plan->order[i] = plan->list.mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE ? plan->list.mounts[i].as.archive.identity.id : plan->list.mounts[i].as.loose.identity.id;
  plan->plan_id = qa_create_mount_plan_id("test", "74657374");
  qa_resolved_mount_plan resolved = {plan->plan_id, plan->list.mounts, plan->list.count, plan->order, plan->list.count, NULL, 0};
  qa_open_mount_options options;
  memset(&options, 0, sizeof options);
  plan->mounted = qa_open_mount_plan(&resolved, &options);
  return plan->mounted;
}
static void close_content_plan(opened_plan *plan) {
  qa_content_mounted_destroy(plan->mounted);
  free(plan->order); free(plan->plan_id);
  qa_content_catalog_mount_list_free(&plan->list);
}
static qa_content_mounted *for_content(void *ctx, qa_content_id content) {
  content_mounts *mounts = ctx;
  CHECK(mounts->sentinel == &for_content_sentinel);
  return open_content_plan(mounts->catalog, content, &mounts->plans[mounts->count++]);
}
static void close_content_mounts(content_mounts *mounts) {
  for (size_t i = 0; i < mounts->count; i++) close_content_plan(&mounts->plans[i]);
  mounts->count = 0;
}
typedef struct grapple_host_state { int *sentinel; opened_plan plan; char *path, *digest, *revision; } grapple_host_state;
static qa_content_mounted *grapple_open_plan(void *ctx, const qa_content_catalog *catalog, qa_content_id content) {
  grapple_host_state *state = ctx;
  CHECK(state->sentinel == &grapple_host_sentinel);
  return open_content_plan((qa_content_catalog *)catalog, content, &state->plan);
}
static bool grapple_profile_for(void *ctx, const qa_resolved_qvm_artifact *artifact, qa_qvm_grapple_definition *out) {
  grapple_host_state *state = ctx;
  CHECK(state->sentinel == &grapple_host_sentinel);
  return qa_app_qvm_grapple_profile(artifact, out, &state->path, &state->digest, &state->revision);
}
typedef struct grapple_call { qa_app_qvm_grapple_selection selection; content_mounts *mounts; qa_app_prepared_qvm_grapple *out; } grapple_call;
static void prepare_grapple(void *ctx) {
  grapple_call *call = ctx;
  qa_app_prepare_application_qvm_grapple(call->selection, for_content, call->mounts, call->out);
}
typedef struct behavior_call { qa_content_catalog *catalog; qa_resolved_weapon_behavior_selection selection; content_mounts *mounts; } behavior_call;
static void prepare_behavior(void *ctx) {
  behavior_call *call = ctx;
  qa_app_prepared_weapon_behavior prepared = qa_app_prepare_application_weapon_behavior(call->catalog, call->selection, for_content, call->mounts);
  qa_app_prepared_weapon_behavior_destroy(&prepared);
}
static void test_content_rows(const char *corpus) {
  qa_content_catalog catalog;
  memset(&catalog, 0, sizeof catalog);
  qa_discover_content_options discover;
  memset(&discover, 0, sizeof discover);
  discover.corpus_root = corpus;
  discover.discover_mods = true;
  discover.inspector = qa_app_default_archive_inspector();
  CHECK(qa_discover_installed_content(&discover, &catalog));
  /* oracle rows movement:* (resolveApplicationMovement) */
  static const struct { const char *movement; qa_game_family family; const char *product; } movements[] = {
    {"qw", QA_GAME_FAMILY_Q1, "q1-quakeworld"}, {"q2-classic-baseq2", QA_GAME_FAMILY_Q2, "q2-classic-baseq2"},
    {"q2", QA_GAME_FAMILY_Q2, NULL}, {"q3-baseq3", QA_GAME_FAMILY_Q3, "q3-baseq3"},
  };
  for (size_t i = 0; i < 4; i++) {
    const char *argv[] = {"--game", "q1-classic-id1", "--movement", movements[i].movement};
    qa_content_root_search search = {NULL, NULL};
    qa_app_application_command command = qa_app_parse_application_command(argv, 4, search, NULL);
    qa_app_options resolved = qa_app_resolve_application_movement(&catalog, &command.options);
    CHECK(resolved.movement == movements[i].family);
    if (movements[i].product == NULL) CHECK(resolved.movement_product == NULL);
    else CHECK_STR(resolved.movement_product, movements[i].product);
    qa_app_application_command_destroy(&command);
  }
  /* oracle rows start:* (authoredCampaignStart) */
  static const struct { const char *product, *map; const char *name, *next, *items; bool new_unit; } starts[] = {
    {"q2-rerelease-baseq2", "base1", "ntro.cin", "base1", "", true},
    {"q2-rerelease-baseq2", "maps/bunk1.bsp", "eou1_.cin", "bunk1", "weapon_shotgun;ammo_shells 20;weapon_machinegun;ammo_bullets 50;ammo_grenades 5", false},
    {"q2-rerelease-xatrix", "xcompnd1", "xu1.cin", "xcompnd1",
     "item_flashlight;weapon_shotgun;weapon_supershotgun;ammo_shells 60;weapon_machinegun;ammo_bullets 150;ammo_grenades 20;item_armor_jacket", false},
    {"q2-rerelease-baseq2", "q2dm1", NULL, NULL, NULL, false},
    {"q1-classic-id1", "start", NULL, NULL, NULL, false},
  };
  qa_catalog_mount_plan_opener opener = {&opener_sentinel, opener_open_plan};
  for (size_t i = 0; i < 5; i++) {
    qa_authored_start_catalog authored;
    memset(&authored, 0, sizeof authored);
    bool found = false;
    const char *id = qa_content_catalog_require(&catalog, starts[i].product)->id;
    CHECK(qa_content_catalog_authored_starts_for(&catalog, opener, id, &authored, &found));
    qa_app_authored_campaign_start start;
    memset(&start, 0, sizeof start);
    bool selected = qa_app_authored_campaign_start_for(found ? &authored : NULL, starts[i].map, &start);
    CHECK(selected == (starts[i].name != NULL));
    if (selected && starts[i].name != NULL) {
      CHECK_STR(start.target->kind, "cinematic");
      CHECK_STR(start.target->name, starts[i].name);
      CHECK_STR(start.target->spawn_point, "");
      CHECK(start.target->new_unit == starts[i].new_unit);
      CHECK(start.target->next != NULL);
      if (start.target->next != NULL) CHECK_STR(start.target->next->name, starts[i].next);
      CHECK_STR(start.start_items, starts[i].items);
      qa_app_q2_travel_target_destroy(start.target);
    }
    if (found) qa_authored_start_catalog_free(&authored);
  }
  /* oracle rows grapple:* (applicationQvmGrappleSelection + prepareApplicationQvmGrapple) */
  content_mounts mounts;
  memset(&mounts, 0, sizeof mounts);
  mounts.sentinel = &for_content_sentinel; mounts.catalog = &catalog;
  size_t grapples = 0;
  for (size_t i = 0; i < catalog.product_count; i++) {
    const qa_catalog_discovered_product *product = &catalog.products[i];
    if (product->expectation.family != QA_GAME_FAMILY_Q3 || product->availability.kind != QA_CATALOG_INSTALLED) continue;
    grapple_host_state host_state;
    memset(&host_state, 0, sizeof host_state);
    host_state.sentinel = &grapple_host_sentinel;
    qa_app_qvm_grapple_host host = {&host_state, grapple_open_plan, grapple_profile_for};
    qa_app_qvm_grapple_selection selection;
    qa_qvm_grapple_definition profile;
    char *module_id = NULL;
    bool found = qa_app_application_qvm_grapple_selection(&catalog, product->id, &host, &selection, &profile, &module_id);
    if (host_state.plan.order != NULL) { free(host_state.plan.order); free(host_state.plan.plan_id); qa_content_catalog_mount_list_free(&host_state.plan.list); }
    if (!found) { CHECK(strcmp(product->id, "q3:classic:lrctf:installed") != 0); continue; }
    grapples++;
    CHECK_STR(product->id, "q3:classic:lrctf:installed");
    CHECK_STR(selection.source.provider, "q3:grapple/q3:classic:lrctf:installed");
    CHECK_STR(profile.id, "lrctf-1.2");
    qa_app_prepared_qvm_grapple prepared;
    grapple_call call = {selection, &mounts, &prepared};
    qa_caught_error caught;
    CHECK(qa_try(prepare_grapple, &call, &caught));
    CHECK_STR(prepared.artifact.module.artifact_path, "vm/qagame.qvm");
    CHECK_STR(prepared.artifact.module.digest, "sha256:b9e396cf5ed2b913548cd92e2b0886ad5992653c8903fa3f9ed0b1f4167ca43e");
    CHECK(prepared.mounts == mounts.plans[0].mounted && prepared.borrowed_mounts);
    CHECK(prepared.selection.profile == selection.profile);
    qa_app_prepared_qvm_grapple_destroy(&prepared);
    qa_app_qvm_grapple_selection other = selection;
    other.source.provider = "q3:grapple/other";
    grapple_call mismatch = {other, &mounts, &prepared};
    CHECK(!qa_try(prepare_grapple, &mismatch, &caught));
    CHECK_STR(caught.name, "Error");
    CHECK_STR(caught.message, "Selected grapple differs from its installed source executable or declaration");
    close_content_mounts(&mounts);
    free((void *)profile.fire_arguments.values); free((void *)profile.movement.words);
    free((void *)profile.initial_cvars); free((void *)profile.presentation.view_attachments);
    free(module_id); free(host_state.path); free(host_state.digest); free(host_state.revision);
  }
  CHECK(grapples == 1);
  /* oracle row behavior:q1-classic-id1 (prepareApplicationWeaponBehavior without a mounted declaration) */
  qa_weapon_behavior_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.id = "id1:none";
  qa_resolved_weapon_behavior_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.source.provider = "weapon-behavior:q1-classic-id1";
  selection.source.content = qa_content_catalog_require(&catalog, "q1-classic-id1")->id;
  selection.artifact.requested_path = "progs.dat";
  selection.artifact.digest = "x";
  selection.definition = &definition;
  behavior_call behavior = {&catalog, selection, &mounts};
  qa_caught_error caught;
  CHECK(!qa_try(prepare_behavior, &behavior, &caught));
  CHECK_STR(caught.message, "Selected weapon behavior differs from its mounted declaration or artifact");
  close_content_mounts(&mounts);
  qa_content_catalog_destroy(&catalog);
}

/* ---- trustedNetworksOnly and the source administration (oracle rows private-*, lan-only*) ---- */
static int admin_sentinel;
static void admin_execute(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx) {
  CHECK(ctx == &admin_sentinel); (void)command; (void)output; (void)output_ctx;
}
static void admin_record(void *ctx, const qa_net_address *address, const char *result) {
  CHECK(ctx == &admin_sentinel); (void)address; (void)result;
}
static void test_network_gaps(void) {
  static const char *const hosts[] = {"localhost", "a.localhost", "box.local", "svc.internal", "10.1.2.3", "127.0.0.1", "0.0.0.0", "169.254.1.1",
    "172.16.0.1", "172.32.0.1", "192.168.1.1", "100.64.0.1", "100.128.0.1", "8.8.8.8", "::", "::1", "fc00::1", "fd12::1", "fe80::1", "febf::1",
    "fec0::1", "::ffff:8.8.8.8", "2001:db8::1", "[::1]", "LOCALHOST", "Box.LOCAL", "example.com", "1.2.3"};
  static const bool expected[] = {true, true, true, true, true, true, true, true, true, false, true, true, false, false, true, true, true, true,
    true, true, false, true, false, true, true, true, false, false};
  for (size_t i = 0; i < sizeof hosts / sizeof hosts[0]; i++) {
    if (qa_net_is_private_host(hosts[i]) != expected[i]) fprintf(stderr, "  host %s\n", hosts[i]);
    CHECK(qa_net_is_private_host(hosts[i]) == expected[i]);
  }
  qa_net_address address;
  memset(&address, 0, sizeof address);
  address.kind = QA_NET_LOOPBACK; address.host = (char *)"x";
  CHECK(qa_net_is_private_network_address(&address));
  address.kind = QA_NET_IPX; address.host = NULL;
  CHECK(qa_net_is_private_network_address(&address));
  address.kind = QA_NET_IPV4; address.ipv4[0] = 192; address.ipv4[1] = 168; address.ipv4[2] = 0; address.ipv4[3] = 9;
  CHECK(qa_net_is_private_network_address(&address));
  address.ipv4[0] = 203; address.ipv4[1] = 0; address.ipv4[2] = 113;
  CHECK(!qa_net_is_private_network_address(&address));
  address.kind = QA_NET_IPV6; address.host = (char *)"fe80::2";
  CHECK(qa_net_is_private_network_address(&address));
  address.host = (char *)"2001:db8::2";
  CHECK(!qa_net_is_private_network_address(&address));
  qa_content_root_search search = {NULL, NULL};
  const char *lan_argv[] = {"--game", "q1-classic-id1", "--lan-only", "--listen", "26000"};
  qa_app_application_command lan = qa_app_parse_application_command(lan_argv, 5, search, NULL);
  CHECK_STR(lan.kind, "run");
  CHECK(lan.options.trusted_networks_only);
  qa_app_application_command_destroy(&lan);
  const char *plain_argv[] = {"--game", "q1-classic-id1"};
  qa_app_application_command plain = qa_app_parse_application_command(plain_argv, 2, search, NULL);
  CHECK_STR(plain.kind, "run");
  CHECK(!plain.options.trusted_networks_only);
  qa_app_application_command_destroy(&plain);
  /* sourceServerAdministration: rconPassword reads the cvar live; destroy frees the returned strings. */
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "deps"));
  qa_cvar_registry_options registry_options;
  memset(&registry_options, 0, sizeof registry_options);
  registry_options.dialect = QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  registry_options.context = console_context(&identity);
  qa_cvar_registry *cvars = qa_cvar_registry_create(&registry_options);
  char root[1024];
  make_root(root, sizeof root);
  qa_config_store store;
  CHECK(qa_config_store_init(&store, root));
  qa_app_server_operator_state *state = qa_app_server_operator_state_open(&store, "q1-quakeworld");
  qa_app_source_administration_options admin_options;
  memset(&admin_options, 0, sizeof admin_options);
  admin_options.dialect = QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  admin_options.state = state; admin_options.cvars = cvars; admin_options.dedicated = true;
  admin_options.execute_ctx = &admin_sentinel; admin_options.execute = admin_execute;
  admin_options.record_ctx = &admin_sentinel; admin_options.record = admin_record;
  qa_server_administration administration = qa_app_source_server_administration(&admin_options);
  CHECK_STR(administration.rcon_password(administration.ctx), "");
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, "rcon_password", "secret", false, &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK_STR(administration.rcon_password(administration.ctx), "secret");
  size_t master_count = 0;
  (void)administration.masters(administration.ctx, &master_count);
  qa_app_source_server_administration_destroy(&administration);
  CHECK(administration.ctx == NULL);
  qa_app_server_operator_state_destroy(state);
  qa_config_store_destroy(&store);
  remove_tree(root);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_registered_settings();
  test_loaded_settings();
  test_candidate_settings();
  test_network_gaps();
  const char *corpus = getenv("QTS_TEST_CORPUS");
  if (corpus != NULL && corpus[0] != '\0') test_content_rows(corpus);
  else printf("skipped content rows: QTS_TEST_CORPUS unset\n");
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
