/* Port of src/app/bootstrap/render-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/render_settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/input/support.h"
#include "qa/materials/compile.h"

static void cvar_register_(qa_cvar_registry *r, const char *name, const char *def, double flags) {
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (qa_cvar_registry_register(r, name, def, flags, &out)) qa_cvar_read_free(&out);
}

/* value.trim() !== "" && Number.isFinite(Number(value)) */
static bool finite_text(const char *value, double *out) {
  char *trimmed = qa_input_js_trim(value);
  bool empty = trimmed == NULL || trimmed[0] == '\0';
  free(trimmed);
  double n = qa_input_js_parse_number(value);
  *out = n;
  return !empty && isfinite(n);
}

static char *validate_shadows(void *ctx, const char *value) {
  (void)ctx; /* TS validate closure captures nothing. */
  double n;
  return finite_text(value, &n) ? NULL : qa_input_dup("Shadows require a finite number");
}
static void changed_noop(void *ctx, const char *value) { (void)ctx; (void)value; }

void qa_app_register_render_settings(qa_cvar_registry *cvars) {
  cvar_register_(cvars, "r_shadows", "0", QA_CVAR_FLAG_ARCHIVE);
  qa_cvar_value_binding b = {NULL, validate_shadows, changed_noop};
  qa_cvar_registry_bind_value(cvars, "r_shadows", b);
  static const char *const ex[] = {"r_shadows 1", "r_shadows 0"};
  qa_command_documentation doc = {"Quake model shadows projected onto the sampled floor. Zero disables; nonzero enables.",
                                  "r_shadows [0|1]", ex, 2, NULL, 0};
  qa_cvar_registry_document(cvars, "r_shadows", doc);
  qa_app_register_q3_renderer_cvars(cvars);
}

/* TS finiteNumber(label) / flag validators. The ctx carries the label. */
static char *validate_finite_number(void *ctx, const char *value) {
  const char *label = ctx;
  double n;
  if (finite_text(value, &n) && n >= 0) return NULL;
  size_t length = strlen(label) + sizeof " requires a non-negative number";
  char *message = malloc(length);
  if (message != NULL) snprintf(message, length, "%s requires a non-negative number", label);
  return message;
}
static char *validate_flag(void *ctx, const char *value) {
  (void)ctx; /* TS flag captures nothing. */
  return strcmp(value, "0") == 0 || strcmp(value, "1") == 0 ? NULL : qa_input_dup("Use 0 or 1");
}

/* The registry stores documentation by reference, so each row's usage and
 * example text are static: `${name} <value>` and `${name} ${value}`. */
typedef struct renderer_cvar {
  const char *name, *value, *summary, *usage;
  const char *const example[1];
  bool flag;
} renderer_cvar;
static const renderer_cvar renderer_cvars[] = {
  {"r_vertexLight", "0", "Collapse multitexture shaders to vertex lighting. Applies to shaders loaded after a map or vid_restart.",
   "r_vertexLight <value>", {"r_vertexLight 0"}, true},
  {"r_detailTextures", "1", "Draw detail texture stages. Applies to shaders loaded after a map or vid_restart.",
   "r_detailTextures <value>", {"r_detailTextures 1"}, true},
  {"r_railWidth", "16", "Width of railgun ring segments.", "r_railWidth <value>", {"r_railWidth 16"}, false},
  {"r_railCoreWidth", "6", "Width of the railgun core beam.", "r_railCoreWidth <value>", {"r_railCoreWidth 6"}, false},
  {"r_railSegmentLength", "32", "Distance between railgun rings.", "r_railSegmentLength <value>",
   {"r_railSegmentLength 32"}, false},
  {"r_lodCurveError", "250", "Distance factor at which patch meshes drop tessellation levels.",
   "r_lodCurveError <value>", {"r_lodCurveError 250"}, false},
};

void qa_app_register_q3_renderer_cvars(qa_cvar_registry *cvars) {
  for (size_t i = 0; i < sizeof renderer_cvars / sizeof renderer_cvars[0]; i++) {
    const renderer_cvar *row = &renderer_cvars[i];
    cvar_register_(cvars, row->name, row->value, QA_CVAR_FLAG_ARCHIVE);
    qa_cvar_value_binding binding = {(void *)row->name, row->flag ? validate_flag : validate_finite_number, changed_noop};
    qa_cvar_registry_bind_value(cvars, row->name, binding);
    qa_command_documentation doc = {row->summary, row->usage, row->example, 1, NULL, 0};
    qa_cvar_registry_document(cvars, row->name, doc);
  }
}

static bool has_cvar(qa_cvar_registry *cvars, const char *name) {
  if (cvars == NULL) return false;
  qa_cvar_read read;
  if (!qa_cvar_registry_find(cvars, name, &read)) return false;
  qa_cvar_read_free(&read);
  return true;
}

static double read_number(qa_cvar_registry *cvars, const char *name, double fallback) {
  double value = !has_cvar(cvars, name) ? fallback : qa_cvar_registry_variable_value(cvars, name);
  return isfinite(value) ? value : fallback;
}

qa_app_q3_renderer_settings qa_app_q3_renderer_settings_from(qa_cvar_registry *cvars) {
  qa_app_q3_renderer_settings settings;
  settings.rail.core_width = read_number(cvars, "r_railCoreWidth", qa_default_rail_settings.core_width);
  settings.rail.ring_width = read_number(cvars, "r_railWidth", qa_default_rail_settings.ring_width);
  settings.rail.segment_length = read_number(cvars, "r_railSegmentLength", qa_default_rail_settings.segment_length);
  settings.curve_error = read_number(cvars, "r_lodCurveError", 250);
  return settings;
}

static bool flag_value(qa_cvar_registry *cvars, const char *name, bool fallback) {
  /* variableValue(name) !== 0: NaN counts as nonzero, as in JS. */
  return !has_cvar(cvars, name) ? fallback : qa_cvar_registry_variable_value(cvars, name) != 0;
}

qa_mat_finish_profile qa_app_shader_profile_from_cvars(qa_cvar_registry *cvars) {
  qa_mat_finish_profile profile = qa_default_shader_profile;
  profile.vertex_light = flag_value(cvars, "r_vertexLight", qa_default_shader_profile.vertex_light);
  profile.detail_textures = flag_value(cvars, "r_detailTextures", qa_default_shader_profile.detail_textures);
  return profile;
}
