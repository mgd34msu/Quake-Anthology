/* Port of src/app/bootstrap/image-settings.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/image_settings.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/input_devices.h"
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/q1_client_settings.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/rerelease_presentation.h"
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/content/user_data.h"
#include "qa/core/commands/index.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/input/device_settings.h"
#include "qa/input/support.h"
#include "qa/network/common/js_number.h"
#include "qa/settings/config.h"
#include "qa/ui/settings/accessibility.h"

/* TS WeakMap<ApplicationAssets, string> appliedFor (keyed by the assets' identity). */
typedef struct applied_for_entry { const qa_app_application_assets *assets; char *signature; } applied_for_entry;
struct qa_app_image_view_binding {
  qa_app_image_settings *settings;
  qa_app_view_settings *view;
  bool staged, released;
  qa_app_image_view_binding *next;
};
struct qa_app_image_settings {
  qa_app_image_settings_options options;
  char *user_content_root; /* owned copy of options.user_content_root */
  qa_cvar_registry *cvars;
  qa_config_store store;
  char *applied;
  applied_for_entry *applied_for; size_t applied_for_count, applied_for_capacity;
  char *saved, *saved_stable;
  bool persistence_enabled;
  double applied_debug_line_width;
  char *display_applied;
  qa_sdl_window *display_window; /* NULL is TS null */
  bool has_restored_size; double restored_width, restored_height;
  qa_cvar_archive_entry *persisted; size_t persisted_count;
  qa_cvar_archive_entry *applied_values; size_t applied_value_count;
  bool has_candidate_fov; int64_t candidate_fov; /* TS releaseCandidateFovValidation */
  void (*print_sink)(void *ctx, const char *text); void *print_ctx;
  char *display_observed;
  double display_changed_at;
  qa_app_image_view_binding *bindings;
};

static char *xdup(const char *text) {
  char *copy = qa_input_dup(text != NULL ? text : "");
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
static void *xcalloc(size_t count, size_t size) {
  void *memory = calloc(count == 0 ? 1 : count, size);
  if (memory == NULL) qa_range_error("Out of memory");
  return memory;
}
static void doc(qa_cvar_registry *cvars, const char *name, const char *summary, const char *usage,
                const char *const *examples, size_t example_count, const char *const *allowed, size_t allowed_count) {
  qa_command_documentation documentation = {summary, usage, examples, example_count, allowed, allowed_count};
  qa_cvar_registry_document(cvars, name, documentation);
}
static void register_cvar(qa_cvar_registry *cvars, const char *name, const char *value, double flags) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(cvars, name, value, flags, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void set_forced(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void set_forced_number(qa_cvar_registry *cvars, const char *name, double value) {
  char text[64];
  qa_net_js_number(value, text, sizeof text); /* String(value) */
  set_forced(cvars, name, text);
}
static void print_sink(qa_app_image_settings *settings, const char *text) {
  if (settings->print_sink != NULL) settings->print_sink(settings->print_ctx, text);
}
static void changed_noop(void *ctx, const char *value) { (void)ctx; (void)value; }
static char *validate_smp(void *ctx, const char *value) {
  (void)ctx;
  return strcmp(value, "0") == 0 || strcmp(value, "1") == 0 ? NULL : xdup("r_smp must be 0 or 1");
}
static char *validate_fov(void *ctx, const char *value) {
  (void)ctx;
  const char *message = qa_app_validate_field_of_view(value);
  return message == NULL ? NULL : xdup(message);
}
static void registry_print(void *ctx, const char *text) {
  const qa_app_image_settings_options *options = ctx;
  if (options->print != NULL) options->print(options->print_ctx, text);
}

/* TS private constructor. */
static qa_app_image_settings *construct(const qa_app_image_settings_options *source) {
  qa_app_image_settings *settings = xcalloc(1, sizeof *settings);
  settings->options = *source;
  if (source->user_content_root != NULL) {
    settings->user_content_root = xdup(source->user_content_root);
    settings->options.user_content_root = settings->user_content_root;
  }
  const qa_app_image_settings_options *options = &settings->options;
  settings->print_sink = options->print;
  settings->print_ctx = options->print_ctx;
  settings->persistence_enabled = !options->defer_persistence;
  settings->applied_debug_line_width = 2;
  settings->applied = xdup(""); settings->saved = xdup(""); settings->saved_stable = xdup("");
  settings->display_applied = xdup(""); settings->display_observed = xdup("");
  /* new ConfigStore(join(userContentRoot ?? defaultUserContentRoot(), "settings")) */
  char *root = options->user_content_root != NULL ? xdup(options->user_content_root) : qa_default_user_content_root();
  if (root == NULL) qa_range_error("Out of memory");
  size_t length = strlen(root) + sizeof "/settings";
  char *settings_root = xcalloc(length, 1);
  snprintf(settings_root, length, "%s/settings", root);
  free(root);
  if (!qa_config_store_init(&settings->store, settings_root)) qa_range_error("Out of memory");
  free(settings_root);
  qa_cvar_registry_options registry_options = {0};
  registry_options.dialect = options->dialect;
  registry_options.context = options->context;
  if (options->print != NULL) { registry_options.print = registry_print; registry_options.print_ctx = &settings->options; }
  settings->cvars = qa_cvar_registry_create(&registry_options);
  if (settings->cvars == NULL) qa_range_error("Out of memory");
  qa_cvar_registry *cvars = settings->cvars;
  register_cvar(cvars, "fov", "90", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "con_scale", "0", QA_CVAR_FLAG_ARCHIVE);
  static const char *const con_scale_examples[] = {"con_scale 2"};
  static const char *const con_scale_allowed[] = {"0: Auto", "1: 1x", "2: 2x", "3: 3x", "4: 4x"};
  doc(cvars, "con_scale", "Console text size. Auto chooses a readable size; small viewports limit the size to keep text usable.",
      "con_scale <0|1|2|3|4>", con_scale_examples, 1, con_scale_allowed, 5);
  char gamma[64];
  snprintf(gamma, sizeof gamma, "%s", QA_JS_NUMBER(options->has_gamma ? options->gamma : 1));
  register_cvar(cvars, "r_gamma", gamma, QA_CVAR_FLAG_ARCHIVE);
  qa_app_register_shared_client_settings(cvars, options->has_audio_format ? &options->audio_output_format : NULL);
  qa_ui_register_accessibility_settings(cvars);
  qa_app_register_q1_client_settings(cvars, options->dialect, true);
  register_cvar(cvars, "r_customwidth", "0", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_customheight", "0", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_fullscreen", "0", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_swapInterval", "1", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_smp", "0", QA_CVAR_FLAG_ARCHIVE);
  qa_cvar_value_binding smp = {NULL, validate_smp, changed_noop};
  (void)qa_cvar_registry_bind_value(cvars, "r_smp", smp); /* TS discards the remove closure */
  static const char *const smp_examples[] = {"r_smp 1", "vid_restart"};
  static const char *const smp_allowed[] = {"0: main thread", "1: worker"};
  doc(cvars, "r_smp", "Render on a worker after vid_restart.", "r_smp <0|1>", smp_examples, 2, smp_allowed, 2);
  register_cvar(cvars, "gl_debug_linewidth", "2", QA_CVAR_FLAG_NONE);
  register_cvar(cvars, "gl_debug_distfrac", "0.004", QA_CVAR_FLAG_NONE);
  register_cvar(cvars, "r_override_textures", "1", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_texture_overrides", "-1", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_texture_formats", "source", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_enhancedmodels", "1", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "gl_md5_load", "1", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "gl_md5_use", "1", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "gl_md5_distance", "2048", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_model_distance", "source", QA_CVAR_FLAG_ARCHIVE);
  static const char *const gamma_examples[] = {"r_gamma 1.3"};
  static const char *const gamma_allowed[] = {"Finite numbers from 0.5 through 3"};
  doc(cvars, "r_gamma", "Display brightness for CPU and GL output: 1 is unchanged, above 1 brightens, below 1 darkens.",
      "r_gamma <0.5..3>", gamma_examples, 1, gamma_allowed, 1);
  static const char *const width_examples[] = {"r_customwidth 1280"};
  static const char *const width_allowed[] = {"0: current width", "Integers from 64 through 16384"};
  doc(cvars, "r_customwidth", "Window width in logical pixels, applied while windowed; 0 uses the current width. Restored oversized windows recover to desktop bounds.",
      "r_customwidth <width>", width_examples, 1, width_allowed, 2);
  static const char *const height_examples[] = {"r_customheight 720"};
  static const char *const height_allowed[] = {"0: current height", "Integers from 64 through 16384"};
  doc(cvars, "r_customheight", "Window height in logical pixels, applied while windowed; 0 uses the current height. Restored oversized windows recover to desktop bounds.",
      "r_customheight <height>", height_examples, 1, height_allowed, 2);
  static const char *const fs_examples[] = {"r_fullscreen 1"};
  static const char *const fs_allowed[] = {"0: windowed", "1: borderless desktop fullscreen"};
  doc(cvars, "r_fullscreen", "Switch between a window and borderless desktop fullscreen; does not select an exclusive display mode.",
      "r_fullscreen <0|1>", fs_examples, 1, fs_allowed, 2);
  static const char *const swap_examples[] = {"r_swapInterval 1"};
  static const char *const swap_allowed[] = {"0: off", "1: on"};
  doc(cvars, "r_swapInterval", "GL vertical synchronization (vsync), when supported by the display backend. Has no effect on the CPU renderer.",
      "r_swapInterval <0|1>", swap_examples, 1, swap_allowed, 2);
  static const char *const line_examples[] = {"gl_debug_linewidth 2"};
  static const char *const line_allowed[] = {"Positive finite numbers"};
  doc(cvars, "gl_debug_linewidth", "Width in pixels for shared debug shapes. Not saved.",
      "gl_debug_linewidth <width>", line_examples, 1, line_allowed, 1);
  static const char *const dist_examples[] = {"gl_debug_distfrac 0.004"};
  doc(cvars, "gl_debug_distfrac", "Distance culling factor for world text that requests distance culling: text is hidden when its cell size is smaller than forward camera distance times this factor. Not saved.",
      "gl_debug_distfrac <factor>", dist_examples, 1, NULL, 0);
  static const char *const override_examples[] = {"r_override_textures 1", "r_override_textures 2"};
  doc(cvars, "r_override_textures", "Replacement image priority: below 1 keeps requested files first, 1 prioritizes replacements for native images, above 1 also prioritizes them for truecolor images. Fallback image searches still run at 0.",
      "r_override_textures <level>", override_examples, 2, NULL, 0);
  static const char *const mask_examples[] = {"r_texture_overrides -1", "r_texture_overrides 5"};
  doc(cvars, "r_texture_overrides", "Usage bitmask for replacement image priority: skin 1, sprite 2, wall 4, picture 8, sky 16; add bits to combine. -1 selects all, 0 selects none. Does not disable fallback searches.",
      "r_texture_overrides <mask>", mask_examples, 2, NULL, 0);
  static const char *const format_examples[] = {"r_texture_formats \"png tga jpg\"", "r_texture_formats source"};
  doc(cvars, "r_texture_formats", "Replacement image search order. source uses the content family's order; otherwise lists png, jpg, tga, jpeg, bmp, gif. Legacy format initials are accepted and unknown letters ignored.",
      "r_texture_formats <source|quoted format list>", format_examples, 2, NULL, 0);
  static const char *const enhanced_examples[] = {"r_enhancedmodels 1"};
  static const char *const on_off[] = {"0: disabled", "Nonzero: enabled"};
  doc(cvars, "r_enhancedmodels", "Enable loading and drawing available mounted Quake I enhanced model replacements; native models remain the fallback.",
      "r_enhancedmodels <number>", enhanced_examples, 1, on_off, 2);
  static const char *const md5_load_examples[] = {"gl_md5_load 1"};
  doc(cvars, "gl_md5_load", "Enable loading available mounted Quake II MD5 model replacements. Drawing them also requires gl_md5_use.",
      "gl_md5_load <number>", md5_load_examples, 1, on_off, 2);
  static const char *const md5_use_examples[] = {"gl_md5_use 1"};
  doc(cvars, "gl_md5_use", "Draw loaded Quake II MD5 replacements instead of native models, subject to replacement distance limits. Requires gl_md5_load.",
      "gl_md5_use <number>", md5_use_examples, 1, on_off, 2);
  static const char *const md5_distance_examples[] = {"gl_md5_distance 2048", "gl_md5_distance 0"};
  doc(cvars, "gl_md5_distance", "Quake II replacement model view distance in map units when r_model_distance is source. Positive values fall back to native models beyond the limit; nonpositive values remove the cutoff. Shadows bypass this distance cutoff.",
      "gl_md5_distance <distance>", md5_distance_examples, 2, NULL, 0);
  static const char *const model_distance_examples[] = {"r_model_distance source", "r_model_distance 4096"};
  doc(cvars, "r_model_distance", "Shared replacement model view distance in map units. source uses no cutoff for Quake I and gl_md5_distance for Quake II. Positive numeric overrides fall back to native models beyond the limit; nonpositive values remove it. Shadows bypass this cutoff.",
      "r_model_distance <source|finite distance>", model_distance_examples, 2, NULL, 0);

  return settings;
}

/* TS imageSetting(name): audio, music and input-device cvars are saved elsewhere. */
static bool image_setting(void *ctx, const char *name) {
  (void)ctx;
  static const char *const excluded[] = {"gamma", "volume", "bgmvolume", "music_shuffle", "music_menu_track"};
  for (size_t i = 0; i < sizeof excluded / sizeof excluded[0]; i++) if (strcmp(name, excluded[i]) == 0) return false;
  for (size_t i = 0; i < qa_app_audio_output_cvar_name_count; i++) if (strcmp(name, qa_app_audio_output_cvar_names[i]) == 0) return false;
  for (size_t i = 0; i < QA_INPUT_DEVICE_CVAR_NAME_COUNT; i++) if (strcmp(name, qa_input_device_cvar_names[i]) == 0) return false;
  return true;
}
static void entries_free(qa_cvar_archive_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) { free(entries[i].name); free(entries[i].value); }
  free(entries);
}
/* TS archivedValues(): canonical snapshots of archived image settings. */
static qa_cvar_archive_entry *archived_values(qa_app_image_settings *settings, size_t *out_count) {
  size_t count = 0;
  qa_cvar_snapshot *snapshots = qa_cvar_registry_canonical_snapshots(settings->cvars, 0, &count);
  qa_cvar_archive_entry *entries = xcalloc(count, sizeof *entries);
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    if (image_setting(NULL, snapshots[i].name) && (snapshots[i].flags & QA_CVAR_FLAG_ARCHIVE) != 0) {
      entries[kept].name = xdup(snapshots[i].name);
      entries[kept].value = xdup(snapshots[i].value);
      kept++;
    }
    qa_cvar_read_free(&snapshots[i]);
  }
  free(snapshots);
  *out_count = kept;
  return entries;
}
typedef struct text_builder { char *text; size_t length, capacity; } text_builder;
static void builder_append(text_builder *builder, const char *text, size_t length) {
  if (builder->length + length + 1 > builder->capacity) {
    size_t capacity = builder->capacity == 0 ? 256 : builder->capacity;
    while (builder->length + length + 1 > capacity) capacity *= 2;
    char *grown = realloc(builder->text, capacity);
    if (grown == NULL) qa_range_error("Out of memory");
    builder->text = grown; builder->capacity = capacity;
  }
  memcpy(builder->text + builder->length, text, length);
  builder->length += length;
  builder->text[builder->length] = '\0';
}
static void builder_text(text_builder *builder, const char *text) { builder_append(builder, text, strlen(text)); }
/* JSON.stringify of a string. */
static void builder_json_string(text_builder *builder, const char *text) {
  builder_text(builder, "\"");
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    char escaped[8];
    switch (*p) {
      case '"': builder_text(builder, "\\\""); break;
      case '\\': builder_text(builder, "\\\\"); break;
      case '\b': builder_text(builder, "\\b"); break;
      case '\f': builder_text(builder, "\\f"); break;
      case '\n': builder_text(builder, "\\n"); break;
      case '\r': builder_text(builder, "\\r"); break;
      case '\t': builder_text(builder, "\\t"); break;
      default:
        if (*p < 0x20) { snprintf(escaped, sizeof escaped, "\\u%04x", *p); builder_text(builder, escaped); }
        else builder_append(builder, (const char *)p, 1);
    }
  }
  builder_text(builder, "\"");
}
/* TS signature() / stableSignature(): JSON of [name, value] pairs. */
static char *signature_of(qa_app_image_settings *settings, bool stable) {
  size_t count = 0;
  qa_cvar_archive_entry *values = archived_values(settings, &count);
  text_builder builder = {0};
  builder_text(&builder, "[");
  bool first = true;
  for (size_t i = 0; i < count; i++) {
    if (stable && (strcmp(values[i].name, "r_customwidth") == 0 || strcmp(values[i].name, "r_customheight") == 0)) continue;
    builder_text(&builder, first ? "[" : ",[");
    builder_json_string(&builder, values[i].name);
    builder_text(&builder, ",");
    builder_json_string(&builder, values[i].value);
    builder_text(&builder, "]");
    first = false;
  }
  builder_text(&builder, "]");
  entries_free(values, count);
  return builder.text;
}
static char *signature(qa_app_image_settings *settings) { return signature_of(settings, false); }
static char *stable_signature(qa_app_image_settings *settings) { return signature_of(settings, true); }
static void replace_text(char **slot, char *value) { free(*slot); *slot = value; }

/* images.cfg execution: every archived cvar a command sets is remembered in load order (TS Map). */
typedef struct load_state {
  qa_app_image_settings *settings;
  qa_command_buffer *commands;
  qa_cvar_archive_entry *loaded; size_t count, capacity;
} load_state;
static void load_after_dispatch(void *ctx) {
  load_state *state = ctx;
  size_t count = 0;
  char *const *tokens = qa_command_buffer_tokenized_arguments(state->commands, &count);
  const char *command = count > 0 ? tokens[0] : NULL, *argument = count > 1 ? tokens[1] : NULL;
  const char *name = command != NULL && (strcmp(command, "set") == 0 || strcmp(command, "seta") == 0) ? argument : command;
  if (name == NULL) return;
  char *canonical = qa_cvar_registry_canonical_name(state->settings->cvars, name);
  if (canonical == NULL) qa_range_error("Out of memory");
  qa_cvar_read found;
  bool exists = qa_cvar_registry_find(state->settings->cvars, canonical, &found);
  free(canonical);
  if (!exists) return;
  if ((found.flags & QA_CVAR_FLAG_ARCHIVE) != 0) {
    size_t at = 0;
    while (at < state->count && strcmp(state->loaded[at].name, found.name) != 0) at++;
    if (at == state->count) {
      if (state->count == state->capacity) {
        size_t capacity = state->capacity == 0 ? 8 : state->capacity * 2;
        qa_cvar_archive_entry *grown = realloc(state->loaded, capacity * sizeof *grown);
        if (grown == NULL) qa_range_error("Out of memory");
        state->loaded = grown; state->capacity = capacity;
      }
      state->loaded[at].name = xdup(found.name);
      state->loaded[at].value = NULL;
      state->count++;
    }
    replace_text(&state->loaded[at].value, xdup(found.value));
  }
  qa_cvar_read_free(&found);
}
static void command_print(void *ctx, const char *text, const qa_command_context *source) {
  (void)source;
  registry_print(ctx, text);
}

qa_app_image_settings *qa_app_image_settings_open(qa_app_image_settings_options options) {
  qa_app_image_settings *settings = construct(&options);
  qa_cvar_registry *cvars = settings->cvars;
  size_t length = 0;
  char *text = qa_config_store_load_text(&settings->store, "images.cfg", &length);
  if (text != NULL) {
    qa_command_buffer_options buffer_options;
    memset(&buffer_options, 0, sizeof buffer_options);
    buffer_options.dialect = settings->options.dialect;
    buffer_options.context = settings->options.context;
    buffer_options.cvars = cvars;
    if (settings->options.print != NULL) { buffer_options.print = command_print; buffer_options.print_ctx = &settings->options; }
    qa_command_buffer *commands = qa_command_buffer_create(&buffer_options);
    if (commands == NULL) qa_range_error("Out of memory");
    load_state state = {settings, commands, NULL, 0, 0};
    qa_command_buffer_append(commands, text, &settings->options.context, -1);
    free(text);
    (void)qa_command_buffer_execute_async(commands, load_after_dispatch, &state, NULL, NULL);
    qa_command_buffer_destroy(commands);
    settings->persisted = state.loaded;
    settings->persisted_count = state.count;
    settings->has_restored_size = true;
    settings->restored_width = qa_cvar_registry_variable_value(cvars, "r_customwidth");
    settings->restored_height = qa_cvar_registry_variable_value(cvars, "r_customheight");
  }
  char *saved = signature(settings), *saved_stable = stable_signature(settings);
  if (options.has_render_worker) set_forced(cvars, "r_smp", options.render_worker ? "1" : "0");
  if (options.has_display_width) set_forced_number(cvars, "r_customwidth", options.display_width);
  if (options.has_display_height) set_forced_number(cvars, "r_customheight", options.display_height);
  if (options.has_display_gamma) set_forced_number(cvars, "r_gamma", options.display_gamma);
  replace_text(&settings->applied, signature(settings));
  replace_text(&settings->saved, saved);
  replace_text(&settings->saved_stable, saved_stable);
  entries_free(settings->applied_values, settings->applied_value_count);
  settings->applied_values = archived_values(settings, &settings->applied_value_count);
  return settings;
}
void qa_app_image_settings_destroy(qa_app_image_settings *settings) {
  if (settings == NULL) return;
  for (qa_app_image_view_binding *binding = settings->bindings; binding != NULL;) {
    qa_app_image_view_binding *next = binding->next;
    free(binding);
    binding = next;
  }
  qa_cvar_registry_destroy(settings->cvars);
  qa_config_store_destroy(&settings->store);
  free(settings->applied); free(settings->saved); free(settings->saved_stable);
  free(settings->display_applied); free(settings->display_observed);
  for (size_t i = 0; i < settings->applied_for_count; i++) free(settings->applied_for[i].signature);
  free(settings->applied_for);
  entries_free(settings->persisted, settings->persisted_count);
  entries_free(settings->applied_values, settings->applied_value_count);
  free(settings->user_content_root);
  free(settings);
}
qa_cvar_registry *qa_app_image_settings_cvars(qa_app_image_settings *settings) { return settings->cvars; }
void qa_app_image_settings_enable_persistence(qa_app_image_settings *settings) { settings->persistence_enabled = true; }
const qa_cvar_archive_entry *qa_app_image_settings_persisted_entries(const qa_app_image_settings *settings, size_t *out_count) {
  *out_count = settings->persisted_count;
  return settings->persisted;
}
void qa_app_image_settings_bind_print(qa_app_image_settings *settings, void (*print)(void *ctx, const char *text), void *print_ctx) {
  settings->print_sink = print;
  settings->print_ctx = print_ctx;
}

/* ---- bindViewSettings / prepareClientSettings --------------------------- */
static void bind_candidate_fov(qa_app_image_settings *settings) {
  qa_cvar_value_binding binding = {NULL, validate_fov, changed_noop};
  settings->candidate_fov = qa_cvar_registry_bind_value(settings->cvars, "fov", binding);
  settings->has_candidate_fov = true;
}
static void restore_validation(qa_app_image_view_binding *binding) {
  if (binding->staged) bind_candidate_fov(binding->settings);
}
typedef struct bind_view_call { qa_app_image_view_binding *binding; } bind_view_call;
static void bind_view_body(void *ctx) {
  bind_view_call *call = ctx;
  qa_app_view_settings_bind_cvars(call->binding->view, call->binding->settings->cvars);
}
qa_app_image_view_binding *qa_app_image_settings_bind_view_settings(qa_app_image_settings *settings, qa_app_view_settings *view) {
  qa_app_image_view_binding *binding = xcalloc(1, sizeof *binding);
  binding->settings = settings;
  binding->view = view;
  binding->staged = settings->has_candidate_fov;
  if (settings->has_candidate_fov) qa_cvar_registry_unbind_value(settings->cvars, settings->candidate_fov);
  settings->has_candidate_fov = false;
  binding->next = settings->bindings;
  settings->bindings = binding;
  bind_view_call call = {binding};
  qa_caught_error caught;
  if (!qa_try(bind_view_body, &call, &caught)) {
    binding->released = true;
    restore_validation(binding);
    qa_rethrow(&caught);
  }
  return binding;
}
void qa_app_image_view_binding_release(qa_app_image_view_binding *binding) {
  if (binding->released) return;
  binding->released = true;
  qa_app_view_settings_unbind_cvars(binding->view);
  restore_validation(binding);
}
qa_app_image_client_settings qa_app_image_settings_prepare_client_settings(qa_app_image_settings *settings) {
  qa_app_image_settings_options options = settings->options;
  options.defer_persistence = true;
  qa_app_image_client_settings client = {construct(&options), NULL};
  bind_candidate_fov(client.settings);
  client.transfer = qa_cvar_registry_prepare_transfer(settings->cvars, client.settings->cvars);
  if (client.transfer == NULL) qa_range_error("Out of memory");
  return client;
}
void qa_app_image_client_settings_validate_publication(const qa_app_image_client_settings *client) {
  client->transfer->validate_publication(client->transfer->ctx);
}
void qa_app_image_client_settings_publish(const qa_app_image_client_settings *client) {
  client->transfer->publish(client->transfer->ctx);
}
void qa_app_image_client_settings_destroy(qa_app_image_client_settings *client) {
  if (client->transfer != NULL) {
    client->transfer->destroy(client->transfer->ctx);
    free(client->transfer);
  }
  qa_app_image_settings_destroy(client->settings);
  client->transfer = NULL;
  client->settings = NULL;
}

/* ---- getters ------------------------------------------------------------ */
void qa_app_image_settings_policy(qa_app_image_settings *settings, qa_scene_image_policy_owned *out) {
  char *formats = qa_cvar_registry_variable_string(settings->cvars, "r_texture_formats");
  if (formats == NULL) qa_range_error("Out of memory");
  qa_scene_image_controls controls;
  controls.override_level = qa_cvar_registry_variable_value(settings->cvars, "r_override_textures");
  controls.override_mask = qa_cvar_registry_variable_value(settings->cvars, "r_texture_overrides");
  controls.formats = formats;
  bool ok = qa_image_policy_from_controls(controls, out);
  free(formats);
  if (!ok) qa_range_error("Out of memory");
}
double qa_app_image_settings_debug_line_width(qa_app_image_settings *settings) {
  double width = qa_cvar_registry_variable_value(settings->cvars, "gl_debug_linewidth");
  if (isfinite(width) && width > 0) settings->applied_debug_line_width = width;
  else {
    set_forced_number(settings->cvars, "gl_debug_linewidth", settings->applied_debug_line_width);
    print_sink(settings, "Debug line width must be a positive finite number.\n");
  }
  return settings->applied_debug_line_width;
}
double qa_app_image_settings_gamma(qa_app_image_settings *settings) {
  return qa_cvar_registry_variable_value(settings->cvars, "r_gamma");
}
qa_app_model_policy qa_app_image_settings_model_policy(qa_app_image_settings *settings) {
  char *value = qa_cvar_registry_variable_string(settings->cvars, "r_model_distance");
  char *trimmed = value != NULL ? qa_input_js_trim(value) : NULL;
  free(value);
  if (trimmed == NULL) qa_range_error("Out of memory");
  for (char *p = trimmed; *p != '\0'; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a'); /* toLowerCase (ASCII) */
  qa_app_model_policy policy;
  memset(&policy, 0, sizeof policy);
  policy.has_distance = strcmp(trimmed, "source") != 0;
  if (policy.has_distance) policy.distance = qa_js_string_to_number(trimmed);
  free(trimmed);
  if (policy.has_distance && !isfinite(policy.distance)) qa_raise("Error", 0, 0, "r_model_distance requires source or a finite distance");
  policy.q1_enhanced = qa_cvar_registry_variable_value(settings->cvars, "r_enhancedmodels") != 0;
  policy.q2_load = qa_cvar_registry_variable_value(settings->cvars, "gl_md5_load") != 0;
  policy.q2_use = qa_cvar_registry_variable_value(settings->cvars, "gl_md5_use") != 0;
  policy.q2_distance = qa_cvar_registry_variable_value(settings->cvars, "gl_md5_distance");
  return policy;
}

/* ---- display ------------------------------------------------------------ */
static char *display_signature(qa_app_image_settings *settings) {
  static const char *const names[] = {"r_customwidth", "r_customheight", "r_fullscreen", "r_swapInterval"};
  text_builder builder = {0};
  builder_text(&builder, "");
  for (size_t i = 0; i < 4; i++) {
    char *value = qa_cvar_registry_variable_string(settings->cvars, names[i]);
    if (value == NULL) qa_range_error("Out of memory");
    if (i > 0) builder_text(&builder, "/");
    builder_text(&builder, value);
    free(value);
  }
  return builder.text;
}
/* `String(error)` for a caught error: "<name>: <message>". */
static void print_caught(qa_app_image_settings *settings, const char *prefix, const qa_caught_error *caught, bool with_name) {
  size_t length = strlen(prefix) + strlen(caught->name) + strlen(caught->message) + 8;
  char *text = xcalloc(length, 1);
  if (with_name) snprintf(text, length, "%s%s: %s\n", prefix, caught->name, caught->message);
  else snprintf(text, length, "%s%s\n", prefix, caught->message);
  print_sink(settings, text);
  free(text);
}
typedef struct gamma_call { qa_app_native_renderer *renderer; double gamma; } gamma_call;
static void gamma_body(void *ctx) {
  gamma_call *call = ctx;
  qa_app_native_renderer_set_output_gamma(call->renderer, call->gamma);
}
typedef struct window_call {
  qa_app_image_settings *settings;
  qa_app_native_renderer *renderer;
  qa_sdl_window *window;
  double width, height;
  bool restored_width, restored_height; /* TS restored?.width / height (false = TS null) */
  bool has_restored; double restored_w, restored_h;
} window_call;
static bool safe_integer(double value) { return isfinite(value) && value == trunc(value) && fabs(value) <= (double)QA_MAX_SAFE_INTEGER; }
static void window_mutation(void *ctx) {
  window_call *call = ctx;
  qa_sdl_window *window = call->window;
  if (!qa_sdl_window_fullscreen(window)) {
    qa_sdl_display display;
    if (!qa_sdl_window_display(window, &display)) qa_range_error("Out of memory");
    double desktop_width = display.bounds.width, desktop_height = display.bounds.height;
    qa_sdl_display_free(&display);
    bool restored_width = !call->settings->options.has_display_width && call->has_restored && call->restored_w == call->width;
    bool restored_height = !call->settings->options.has_display_height && call->has_restored && call->restored_h == call->height;
    qa_sdl_window_set_size(window, restored_width ? qa_js_min(call->width, desktop_width) : call->width,
        restored_height ? qa_js_min(call->height, desktop_height) : call->height);
  }
  double fullscreen = qa_cvar_registry_variable_value(call->settings->cvars, "r_fullscreen");
  if (fullscreen != 0 && fullscreen != 1) qa_range_error("Invalid fullscreen setting");
  if (qa_sdl_window_fullscreen(window) != (fullscreen == 1)) qa_sdl_window_set_fullscreen(window, fullscreen == 1);
}
static void display_body(void *ctx) {
  window_call *call = ctx;
  if (!safe_integer(call->width) || call->width < 64 || call->width > 16384 ||
      !safe_integer(call->height) || call->height < 64 || call->height > 16384)
    qa_range_error("Invalid saved window size");
  qa_app_native_renderer_mutate_window(call->renderer, window_mutation, call);
  if (qa_sdl_window_backend(call->window) == QA_SDL_BACKEND_GL) {
    double interval = qa_cvar_registry_variable_value(call->settings->cvars, "r_swapInterval");
    if (interval != 0 && interval != 1) qa_range_error("Vertical sync must be on or off");
    if (qa_app_native_renderer_swap_interval(call->renderer) != interval) qa_app_native_renderer_set_swap_interval(call->renderer, interval);
  }
}
/* JS `a || b` for numbers: 0 and NaN select b. */
static double or_number(double a, double b) { return a == 0 || isnan(a) ? b : a; }
static void apply_display(qa_app_image_settings *settings, qa_app_native_renderer *renderer) {
  double gamma = qa_app_image_settings_gamma(settings);
  if (qa_app_native_renderer_output_gamma(renderer) != gamma) {
    gamma_call call = {renderer, gamma};
    qa_caught_error caught;
    if (!qa_try(gamma_body, &call, &caught)) {
      set_forced_number(settings->cvars, "r_gamma", qa_app_native_renderer_output_gamma(renderer));
      print_caught(settings, "Brightness rejected: ", &caught, true);
    }
  }
  qa_sdl_window *window = qa_app_native_renderer_window(renderer);
  char *current_signature = display_signature(settings);
  bool changed = window != settings->display_window || strcmp(current_signature, settings->display_applied) != 0;
  free(current_signature);
  if (changed) {
    qa_sdl_size current = qa_sdl_window_logical_size(window);
    window_call call;
    memset(&call, 0, sizeof call);
    call.settings = settings; call.renderer = renderer; call.window = window;
    call.has_restored = settings->display_applied[0] == '\0' && settings->has_restored_size;
    call.restored_w = settings->restored_width; call.restored_h = settings->restored_height;
    call.width = or_number(qa_cvar_registry_variable_value(settings->cvars, "r_customwidth"), current.width);
    call.height = or_number(qa_cvar_registry_variable_value(settings->cvars, "r_customheight"), current.height);
    qa_caught_error caught;
    if (!qa_try(display_body, &call, &caught)) print_caught(settings, "Display settings rejected: ", &caught, true);
  }
  if (!qa_sdl_window_fullscreen(window)) {
    qa_sdl_size size = qa_sdl_window_logical_size(window);
    set_forced_number(settings->cvars, "r_customwidth", size.width);
    set_forced_number(settings->cvars, "r_customheight", size.height);
  }
  set_forced(settings->cvars, "r_fullscreen", qa_sdl_window_fullscreen(window) ? "1" : "0");
  if (qa_sdl_window_backend(window) == QA_SDL_BACKEND_GL)
    set_forced(settings->cvars, "r_swapInterval", qa_app_native_renderer_swap_interval(renderer) == 0 ? "0" : "1");
  replace_text(&settings->display_applied, display_signature(settings));
  settings->display_window = window;
}

/* ---- save ---------------------------------------------------------------- */
static void save(qa_app_image_settings *settings) {
  if (!settings->persistence_enabled) return;
  char *selected = signature(settings);
  if (strcmp(selected, settings->saved) == 0) { free(selected); return; }
  /* Marked saved before the write so a failing disk is reported once rather than on every frame. */
  replace_text(&settings->saved, selected);
  replace_text(&settings->saved_stable, stable_signature(settings));
  size_t count = 0;
  char **commands = qa_cvar_registry_archive_commands(settings->cvars, image_setting, NULL, &count);
  text_builder builder = {0};
  builder_text(&builder, "// Generated by quake-typescript\n");
  for (size_t i = 0; i < count; i++) {
    if (i > 0) builder_text(&builder, "\n");
    builder_text(&builder, commands[i]);
    free(commands[i]);
  }
  free(commands);
  builder_text(&builder, "\n");
  errno = 0;
  if (!qa_config_store_dump(&settings->store, "images.cfg", builder.text)) {
    /* TS prints the rejected write's error.message; the C store reports errno. */
    const char *reason = errno != 0 ? strerror(errno) : "write failed";
    size_t length = strlen(reason) + 64;
    char *text = xcalloc(length, 1);
    snprintf(text, length, "Could not save image settings: %s\n", reason);
    print_sink(settings, text);
    free(text);
  }
  free(builder.text);
}
void qa_app_image_settings_refresh_display(qa_app_image_settings *settings, qa_app_native_renderer *renderer) {
  apply_display(settings, renderer);
  char *selected = signature(settings);
  double now = qa_app_performance_now();
  if (strcmp(selected, settings->display_observed) != 0) {
    replace_text(&settings->display_observed, xdup(selected));
    settings->display_changed_at = now;
  }
  bool unchanged = strcmp(selected, settings->saved) == 0;
  free(selected);
  if (unchanged) return;
  /* Only a changed window size waits for the drag to settle; every other setting is saved at once. */
  char *stable = stable_signature(settings);
  bool stable_changed = strcmp(stable, settings->saved_stable) != 0;
  free(stable);
  if (stable_changed || now - settings->display_changed_at >= 500) save(settings);
}

/* ---- refresh ------------------------------------------------------------- */
/* JSON.stringify(policy) equality for two ImagePolicy values (NULL is TS undefined). */
static bool json_number_equal(double a, double b) {
  if (!isfinite(a) || !isfinite(b)) return !isfinite(a) && !isfinite(b); /* both stringify as null */
  return a == b;
}
static bool policy_equal(const qa_scene_image_policy *a, const qa_scene_image_policy *b) {
  if (a == NULL || b == NULL) return a == b;
  if (!json_number_equal(a->override_level, b->override_level) || a->override_usage_count != b->override_usage_count) return false;
  for (size_t i = 0; i < a->override_usage_count; i++) if (strcmp(a->override_usages[i], b->override_usages[i]) != 0) return false;
  if (a->has_formats != b->has_formats) return false;
  if (!a->has_formats) return true;
  if (a->format_count != b->format_count) return false;
  for (size_t i = 0; i < a->format_count; i++) if (strcmp(a->formats[i], b->formats[i]) != 0) return false;
  return true;
}
typedef struct refresh_call {
  qa_app_image_settings *settings;
  qa_app_application_assets *assets;
  qa_app_world_seat_presentation *const *presentations; size_t presentation_count;
  qa_app_rerelease_presentation *rerelease;
  bool has_policy; qa_scene_image_policy_owned policy;
  qa_app_prepared_images *images; bool images_committed;
  qa_app_world_seat_image_refresh **bindings; size_t binding_count;
  qa_app_rerelease_refresh *sky;
} refresh_call;
static void refresh_body(void *ctx) {
  refresh_call *call = ctx;
  qa_app_image_settings_policy(call->settings, &call->policy);
  call->has_policy = true;
  qa_app_model_policy model_policy = qa_app_image_settings_model_policy(call->settings);
  qa_model_replacement_policy current = qa_app_application_assets_model_policy(call->assets);
  bool load_changed = model_policy.q1_enhanced != current.q1_enhanced || model_policy.q2_load != current.q2_load;
  if (load_changed || !policy_equal(&call->policy.policy, qa_app_application_assets_image_policy(call->assets))) {
    call->images = qa_app_application_assets_prepare_image_refresh(call->assets, &call->policy.policy, &model_policy);
    for (size_t i = 0; i < call->presentation_count; i++) {
      call->bindings[i] = qa_app_world_seat_presentation_prepare_image_refresh(call->presentations[i], call->images);
      call->binding_count = i + 1;
    }
    if (call->rerelease != NULL) call->sky = qa_app_rerelease_prepare_image_refresh(call->rerelease, call->images);
  } else qa_app_application_assets_set_model_policy(call->assets, model_policy);
  if (call->images != NULL) {
    qa_app_prepared_images_commit(call->images);
    call->images_committed = true;
  }
}
static void set_applied_for(qa_app_image_settings *settings, const qa_app_application_assets *assets, const char *selected) {
  for (size_t i = 0; i < settings->applied_for_count; i++)
    if (settings->applied_for[i].assets == assets) { replace_text(&settings->applied_for[i].signature, xdup(selected)); return; }
  if (settings->applied_for_count == settings->applied_for_capacity) {
    size_t capacity = settings->applied_for_capacity == 0 ? 4 : settings->applied_for_capacity * 2;
    applied_for_entry *grown = realloc(settings->applied_for, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    settings->applied_for = grown; settings->applied_for_capacity = capacity;
  }
  settings->applied_for[settings->applied_for_count].assets = assets;
  settings->applied_for[settings->applied_for_count].signature = xdup(selected);
  settings->applied_for_count++;
}
static const char *applied_for(const qa_app_image_settings *settings, const qa_app_application_assets *assets) {
  for (size_t i = 0; i < settings->applied_for_count; i++)
    if (settings->applied_for[i].assets == assets) return settings->applied_for[i].signature;
  return NULL;
}
void qa_app_image_settings_refresh(qa_app_image_settings *settings, qa_app_application_assets *assets,
    qa_app_world_seat_presentation *const *presentations, size_t presentation_count,
    qa_app_rerelease_presentation *rerelease, qa_app_native_renderer *renderer) {
  if (renderer != NULL) apply_display(settings, renderer);
  char *selected = signature(settings);
  /* Several asset owners can share these settings, each needing the selection applied once. */
  const char *previous = applied_for(settings, assets);
  if (strcmp(selected, settings->applied) == 0 && previous != NULL && strcmp(previous, selected) == 0) { free(selected); return; }
  refresh_call call;
  memset(&call, 0, sizeof call);
  call.settings = settings; call.assets = assets;
  call.presentations = presentations; call.presentation_count = presentation_count;
  call.rerelease = rerelease;
  call.bindings = xcalloc(presentation_count, sizeof *call.bindings);
  qa_caught_error caught;
  if (!qa_try(refresh_body, &call, &caught)) {
    for (size_t i = 0; i < call.binding_count; i++) qa_app_world_seat_image_refresh_discard(call.bindings[i]);
    if (call.images != NULL && !call.images_committed) qa_app_prepared_images_discard(call.images);
    if (call.sky != NULL) qa_app_rerelease_refresh_discard(call.sky);
    for (size_t i = 0; i < settings->applied_value_count; i++)
      set_forced(settings->cvars, settings->applied_values[i].name, settings->applied_values[i].value);
    print_caught(settings, "Image settings rejected: ", &caught, false);
    if (call.has_policy) qa_scene_image_policy_owned_destroy(&call.policy);
    free(call.bindings);
    free(selected);
    return;
  }
  for (size_t i = 0; i < call.binding_count; i++) qa_app_world_seat_image_refresh_commit(call.bindings[i]);
  if (call.sky != NULL) qa_app_rerelease_refresh_publish(call.sky);
  qa_app_application_assets_finish_image_refresh(assets);
  qa_scene_image_policy_owned_destroy(&call.policy);
  free(call.bindings);
  set_applied_for(settings, assets, selected);
  replace_text(&settings->applied, selected);
  entries_free(settings->applied_values, settings->applied_value_count);
  settings->applied_values = archived_values(settings, &settings->applied_value_count);
  save(settings);
}
void qa_app_image_settings_close(qa_app_image_settings *settings) { save(settings); }
