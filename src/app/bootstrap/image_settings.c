/* Port of src/app/bootstrap/image-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/image_settings.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_image_settings {
  qa_app_image_settings_options options;
  qa_app_image_settings_host host;
  qa_cvar_registry *cvars;
  char *applied, *saved;
  bool persistence_enabled;
  double applied_debug_line_width;
  void (*print_sink)(void *ctx, const char *text);
  void *print_ctx;
};
static void doc(qa_cvar_registry *cvars, const char *name, const char *summary, const char *usage,
                const char *const *examples, size_t example_count, const char *const *allowed, size_t allowed_count) {
  qa_command_documentation documentation = {summary, usage, examples, example_count, allowed, allowed_count};
  qa_cvar_registry_document(cvars, name, documentation);
}
static void register_cvar(qa_cvar_registry *cvars, const char *name, const char *value, double flags) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(cvars, name, value, flags, &snapshot)) qa_cvar_read_free(&snapshot);
}
static char *signature(qa_app_image_settings *settings);
static void set_cvar_text(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
}

qa_app_image_settings *qa_app_image_settings_open(qa_app_image_settings_options options, qa_app_image_settings_host host) {
  qa_app_image_settings *settings = calloc(1, sizeof *settings);
  if (!settings) return NULL;
  settings->options = options;
  settings->host = host;
  settings->print_sink = options.print;
  settings->print_ctx = options.print_ctx;
  settings->persistence_enabled = !options.defer_persistence;
  settings->applied_debug_line_width = 2;
  qa_cvar_registry_options registry_options = {0};
  registry_options.dialect = options.dialect;
  registry_options.context = options.context;
  registry_options.print = host.print;
  registry_options.print_ctx = host.ctx;
  settings->cvars = qa_cvar_registry_create(&registry_options);
  qa_cvar_registry *cvars = settings->cvars;
  register_cvar(cvars, "fov", "90", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "con_scale", "0", QA_CVAR_FLAG_ARCHIVE);
  static const char *const con_scale_examples[] = {"con_scale 2"};
  static const char *const con_scale_allowed[] = {"0: Auto", "1: 1x", "2: 2x", "3: 3x", "4: 4x"};
  doc(cvars, "con_scale", "Console text size. Auto chooses a readable size; small viewports limit the size to keep text usable.",
      "con_scale <0|1|2|3|4>", con_scale_examples, 1, con_scale_allowed, 5);
  char gamma[64];
  snprintf(gamma, sizeof gamma, "%.17g", options.has_gamma ? options.gamma : 1);
  register_cvar(cvars, "r_gamma", gamma, QA_CVAR_FLAG_ARCHIVE);
  host.register_shared(host.ctx, cvars, options.has_audio_format, options.audio_output_format);
  register_cvar(cvars, "r_customwidth", "0", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_customheight", "0", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_fullscreen", "0", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_swapInterval", "1", QA_CVAR_FLAG_ARCHIVE);
  register_cvar(cvars, "r_smp", "0", QA_CVAR_FLAG_ARCHIVE);
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
  /* images.cfg load + override application (host executes the text). */
  char *text = NULL;
  if (host.load_text(host.ctx, "images.cfg", &text) && text) free(text);
  settings->saved = signature(settings);
  if (options.has_render_worker) set_cvar_text(cvars, "r_smp", options.render_worker ? "1" : "0");
  char number[64];
  if (options.has_display_width) { snprintf(number, sizeof number, "%.17g", options.display_width); set_cvar_text(cvars, "r_customwidth", number); }
  if (options.has_display_height) { snprintf(number, sizeof number, "%.17g", options.display_height); set_cvar_text(cvars, "r_customheight", number); }
  if (options.has_display_gamma) { snprintf(number, sizeof number, "%.17g", options.display_gamma); set_cvar_text(cvars, "r_gamma", number); }
  settings->applied = signature(settings);
  return settings;
}
static bool image_setting(const char *name) {
  static const char *const excluded[] = {"gamma", "volume", "bgmvolume", "music_shuffle", "music_menu_track"};
  for (size_t i = 0; i < 5; i++) if (strcmp(name, excluded[i]) == 0) return false;
  return true;
}
static char *signature(qa_app_image_settings *settings) {
  size_t count = 0;
  qa_cvar_snapshot *snapshots = qa_cvar_registry_canonical_snapshots(settings->cvars, 0, &count);
  size_t cap = 256, len = 0;
  char *out = malloc(cap);
  out[0] = '['; out[1] = '\0'; len = 1;
  bool first = true;
  for (size_t i = 0; i < count; i++) {
    if (!image_setting(snapshots[i].name) || !(snapshots[i].flags & QA_CVAR_FLAG_ARCHIVE)) continue;
    size_t need = len + strlen(snapshots[i].name) + strlen(snapshots[i].value) + 16;
    if (need > cap) { cap = need * 2; out = realloc(out, cap); }
    len += (size_t)snprintf(out + len, cap - len, "%s[\"%s\",\"%s\"]", first ? "" : ",", snapshots[i].name, snapshots[i].value);
    first = false;
  }
  if (len + 2 > cap) { cap = len + 2; out = realloc(out, cap); }
  snprintf(out + len, cap - len, "]");
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&snapshots[i]);
  free(snapshots);
  return out;
}
void qa_app_image_settings_destroy(qa_app_image_settings *settings) {
  if (!settings) return;
  qa_cvar_registry_destroy(settings->cvars);
  free(settings->applied);
  free(settings->saved);
  free(settings);
}
qa_cvar_registry *qa_app_image_settings_cvars(qa_app_image_settings *settings) { return settings->cvars; }
void qa_app_image_settings_enable_persistence(qa_app_image_settings *settings) { settings->persistence_enabled = true; }
void qa_app_image_settings_bind_print(qa_app_image_settings *settings, void (*print)(void *ctx, const char *text), void *print_ctx) {
  settings->print_sink = print;
  settings->print_ctx = print_ctx;
}
qa_app_image_policy qa_app_image_settings_policy(qa_app_image_settings *settings) {
  /* imagePolicyFromControls is render-owned; the control values are
   * read here and the host maps them in refresh. */
  qa_app_image_policy policy;
  policy.override_level = qa_cvar_registry_variable_value(settings->cvars, "r_override_textures");
  policy.override_mask = qa_cvar_registry_variable_value(settings->cvars, "r_texture_overrides");
  policy.formats = NULL;
  return policy;
}
double qa_app_image_settings_debug_line_width(qa_app_image_settings *settings) {
  double width = qa_cvar_registry_variable_value(settings->cvars, "gl_debug_linewidth");
  if (isfinite(width) && width > 0) settings->applied_debug_line_width = width;
  else {
    char text[64];
    snprintf(text, sizeof text, "%.17g", settings->applied_debug_line_width);
    set_cvar_text(settings->cvars, "gl_debug_linewidth", text);
    if (settings->print_sink) settings->print_sink(settings->print_ctx, "Debug line width must be a positive finite number.\n");
  }
  return settings->applied_debug_line_width;
}
double qa_app_image_settings_gamma(qa_app_image_settings *settings) {
  return qa_cvar_registry_variable_value(settings->cvars, "r_gamma");
}
qa_app_model_policy qa_app_image_settings_model_policy(qa_app_image_settings *settings) {
  char *selected = qa_cvar_registry_variable_string(settings->cvars, "r_model_distance");
  char lowered[128];
  size_t i = 0, j = 0;
  while (selected[i] && isspace((unsigned char)selected[i])) i++;
  for (; selected[i] && j < sizeof lowered - 1; i++) lowered[j++] = (char)tolower((unsigned char)selected[i]);
  while (j > 0 && isspace((unsigned char)lowered[j - 1])) j--;
  lowered[j] = '\0';
  free(selected);
  qa_app_model_policy policy = {0};
  policy.distance_is_source = strcmp(lowered, "source") == 0;
  if (!policy.distance_is_source) {
    char *end = NULL;
    policy.distance = strtod(lowered, &end);
    if (end == lowered || *end != '\0' || !isfinite(policy.distance))
      qa_range_error("r_model_distance requires source or a finite distance");
  }
  policy.q1_enhanced = qa_cvar_registry_variable_value(settings->cvars, "r_enhancedmodels") != 0;
  policy.q2_load = qa_cvar_registry_variable_value(settings->cvars, "gl_md5_load") != 0;
  policy.q2_use = qa_cvar_registry_variable_value(settings->cvars, "gl_md5_use") != 0;
  policy.q2_distance = qa_cvar_registry_variable_value(settings->cvars, "gl_md5_distance");
  return policy;
}
static const char *save(qa_app_image_settings *settings) {
  if (!settings->persistence_enabled) return NULL;
  char *selected = signature(settings);
  if (settings->saved && strcmp(selected, settings->saved) == 0) { free(selected); return NULL; }
  size_t count = 0;
  char **commands = qa_cvar_registry_archive_commands(settings->cvars, NULL, NULL, &count);
  size_t cap = 512, len = 0;
  char *text = malloc(cap);
  len += (size_t)snprintf(text, cap, "// Generated by quake-typescript\n");
  for (size_t i = 0; i < count; i++) {
    size_t need = len + strlen(commands[i]) + 2;
    if (need > cap) { cap = need * 2; text = realloc(text, cap); }
    len += (size_t)snprintf(text + len, cap - len, "%s%s", i ? "\n" : "", commands[i]);
    free(commands[i]);
  }
  free(commands);
  if (len + 2 > cap) { cap = len + 2; text = realloc(text, cap); }
  snprintf(text + len, cap - len, "\n");
  const char *error = settings->host.dump_text(settings->host.ctx, "images.cfg", text);
  free(text);
  if (!error) { free(settings->saved); settings->saved = selected; }
  else free(selected);
  return error;
}
const char *qa_app_image_settings_refresh_display(qa_app_image_settings *settings) {
  const char *error = settings->host.apply_display(settings->host.ctx, settings);
  if (error) return error;
  return save(settings);
}
const char *qa_app_image_settings_refresh(qa_app_image_settings *settings) {
  const char *error = settings->host.apply_display(settings->host.ctx, settings);
  if (error) return error;
  char *selected = signature(settings);
  if (settings->applied && strcmp(selected, settings->applied) == 0) { free(selected); return NULL; }
  error = settings->host.refresh_images(settings->host.ctx, settings,
      qa_app_image_settings_policy(settings), qa_app_image_settings_model_policy(settings));
  if (error) {
    if (settings->print_sink) {
      char text[2048];
      snprintf(text, sizeof text, "Image settings rejected: %s\n", error);
      settings->print_sink(settings->print_ctx, text);
    }
    free(selected);
    return NULL; /* TS catches and restores applied values via host. */
  }
  free(settings->applied);
  settings->applied = selected;
  return save(settings);
}
const char *qa_app_image_settings_close(qa_app_image_settings *settings) { return save(settings); }
qa_app_image_settings *qa_app_image_settings_prepare_client(qa_app_image_settings *settings) {
  qa_app_image_settings_options options = settings->options;
  options.defer_persistence = true;
  return qa_app_image_settings_open(options, settings->host);
}
