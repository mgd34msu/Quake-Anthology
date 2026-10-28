/* Port of src/app/bootstrap/shared-setting-cvars.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/render_settings.h"
#include "qa/input/device_settings.h"
#include "qa/input/support.h"

static bool cvar_exists_(qa_cvar_registry *r, const char *name) {
  qa_cvar_read out; memset(&out, 0, sizeof out);
  bool found = qa_cvar_registry_find(r, name, &out);
  if (found) qa_cvar_read_free(&out);
  return found;
}
static void cvar_set_(qa_cvar_registry *r, const char *name, const char *value, bool force) {
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (qa_cvar_registry_set(r, name, value, force, &out)) qa_cvar_read_free(&out);
}
static void cvar_register_(qa_cvar_registry *r, const char *name, const char *def, double flags) {
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (qa_cvar_registry_register(r, name, def, flags, &out)) qa_cvar_read_free(&out);
}

static bool fov_syntax(const char *t) {
  const char *p = t;
  if (*p == '+' || *p == '-') p++;
  bool digits = false, dot = false;
  if (*p == '.') { dot = true; p++; while (isdigit((unsigned char)*p)) { p++; digits = true; } return digits && *p == '\0'; }
  while (isdigit((unsigned char)*p)) { p++; digits = true; }
  if (*p == '.') { dot = true; p++; while (isdigit((unsigned char)*p)) p++; }
  (void)dot;
  return digits && *p == '\0';
}
const char *qa_app_validate_field_of_view(const char *text) {
  double value = qa_input_js_parse_number(text);
  return (!fov_syntax(text) || !isfinite(value) || value < 60 || value > 160)
             ? "Field of view must be between 60 and 160 degrees" : NULL;
}
static bool dialect_is_q1(qa_command_dialect d) { return d == QA_COMMAND_DIALECT_Q1_NETQUAKE || d == QA_COMMAND_DIALECT_Q1_QUAKEWORLD; }
void qa_app_register_run_cvar(qa_cvar_registry *cvars, qa_command_dialect movement_dialect) {
  const char *def = qa_default_view_input_tuning(movement_dialect).always_run ? "1" : "0";
  bool stored = cvar_exists_(cvars, "cl_run");
  if (!stored || !dialect_is_q1(qa_cvar_registry_dialect(cvars)) || qa_cvar_registry_is_console_created(cvars, "cl_run"))
    cvar_register_(cvars, "cl_run", def, QA_CVAR_FLAG_ARCHIVE);
  else qa_cvar_registry_add_flags(cvars, "cl_run", QA_CVAR_FLAG_ARCHIVE);
  static const char *const ex[] = {"cl_run 1", "set cl_run 0"};
  static const char *const allowed[] = {"0", "1"};
  qa_command_documentation doc = {"Always run for this player; the speed key reverses run and walk. Uses the Always run menu preference.",
                                  "cl_run [0|1]", ex, 2, allowed, 2};
  qa_cvar_registry_document(cvars, "cl_run", doc);
}
static char *validate_run(void *ctx, const char *value) {
  (void)ctx;
  return (strcmp(value, "0") == 0 || strcmp(value, "1") == 0) ? NULL : qa_input_dup("Always run must be 0 or 1");
}
static void changed_noop(void *ctx, const char *value) { (void)ctx; (void)value; }
static bool run_read(void *ctx) { return qa_cvar_registry_variable_value((qa_cvar_registry *)ctx, "cl_run") != 0; }
static void run_write(void *ctx, bool value) { cvar_set_((qa_cvar_registry *)ctx, "cl_run", value ? "1" : "0", false); }
int64_t qa_app_bind_run_cvar(qa_cvar_registry *cvars, qa_input_command_builder *builder) {
  bool stored = cvar_exists_(cvars, "cl_run");
  qa_app_register_run_cvar(cvars, qa_input_command_builder_dialect(builder));
  bool always = qa_input_command_builder_tuning(builder).always_run;
  if (!stored) cvar_set_(cvars, "cl_run", always ? "1" : "0", false);
  char *current = qa_cvar_registry_variable_string(cvars, "cl_run");
  if (validate_run(NULL, current ? current : "") != NULL) cvar_set_(cvars, "cl_run", always ? "1" : "0", true);
  free(current);
  qa_cvar_value_binding b = {NULL, validate_run, changed_noop};
  int64_t handle = qa_cvar_registry_bind_value(cvars, "cl_run", b);
  qa_input_command_builder_bind_always_run(builder, run_read, run_write, cvars);
  return handle;
}
static char *finite_validate(void *ctx, const char *value) {
  (void)ctx;
  char *trimmed = qa_input_js_trim(value);
  bool empty = trimmed == NULL || trimmed[0] == '\0';
  free(trimmed);
  return (!empty && isfinite(qa_input_js_parse_number(value))) ? NULL : qa_input_dup("Expected a finite number");
}
static char *validate_use01(void *ctx, const char *value) {
  (void)ctx;
  return (strcmp(value, "0") == 0 || strcmp(value, "1") == 0) ? NULL : qa_input_dup("Use 0 or 1");
}
static char *gamma_validate(void *ctx, const char *value) {
  char *f = finite_validate(ctx, value);
  if (f) return f;
  double n = qa_input_js_parse_number(value);
  return (n >= 0.5 && n <= 3) ? NULL : qa_input_dup("Brightness must be between 0.5 and 3");
}
static char *gamma_read(void *ctx, const char *value) {
  (void)ctx;
  return qa_input_js_number(1.0 / qa_input_js_parse_number(value));
}
static bool gamma_write(void *ctx, const char *value, char **out_value, char **out_message) {
  char *f = finite_validate(ctx, value);
  double n = qa_input_js_parse_number(value);
  if (f != NULL || n < 1.0 / 3.0 || n > 2) {
    free(f);
    *out_message = qa_input_dup("Gamma must be between 1/3 and 2");
    return false;
  }
  *out_value = qa_input_js_number(1.0 / n);
  return true;
}
void qa_app_register_shared_client_settings(qa_cvar_registry *cvars, qa_audio_output_format output_format, qa_app_shared_audio_settings_host host) {
  qa_app_register_render_settings(cvars);
  qa_register_input_device_cvars(cvars);
  if (host.register_audio_output_cvars) host.register_audio_output_cvars(host.ctx, cvars, output_format);
  if (host.register_music_settings) host.register_music_settings(host.ctx, cvars);
  cvar_register_(cvars, "s_geometryAcoustics", "0", QA_CVAR_FLAG_ARCHIVE);
  qa_cvar_value_binding geo = {NULL, validate_use01, changed_noop};
  qa_cvar_registry_bind_value(cvars, "s_geometryAcoustics", geo);
  { static const char *const ex[] = {"s_geometryAcoustics 1", "s_geometryAcoustics 0"};
    static const char *const allowed[] = {"0", "1"};
    qa_command_documentation doc = {"Enable shared geometry sound obstruction. Off preserves native attenuation; this is a functional A3D replacement, not native numerical emulation.",
                                    "s_geometryAcoustics <0|1>", ex, 2, allowed, 2};
    qa_cvar_registry_document(cvars, "s_geometryAcoustics", doc); }
  cvar_register_(cvars, "r_saveFontData", "0", QA_CVAR_FLAG_NONE);
  { static const char *const ex[] = {"r_saveFontData 1"};
    qa_command_documentation doc = {"Export generated Q3 font atlases and DAT records to this content's user directory.",
                                    "r_saveFontData <0|1>", ex, 1, NULL, 0};
    qa_cvar_registry_document(cvars, "r_saveFontData", doc); }
  bool q3 = qa_cvar_registry_dialect(cvars) == QA_COMMAND_DIALECT_Q3;
  cvar_register_(cvars, "volume", q3 ? "0.8" : "0.7", QA_CVAR_FLAG_ARCHIVE);
  cvar_register_(cvars, "bgmvolume", q3 ? "0.25" : "1", QA_CVAR_FLAG_ARCHIVE);
  qa_cvar_value_binding fin = {NULL, finite_validate, changed_noop};
  qa_cvar_registry_bind_value(cvars, "volume", fin);
  qa_cvar_registry_bind_value(cvars, "bgmvolume", fin);
  qa_cvar_value_binding gam = {NULL, gamma_validate, changed_noop};
  qa_cvar_registry_bind_value(cvars, "r_gamma", gam);
  qa_cvar_alias_conversion conv; memset(&conv, 0, sizeof conv); conv.read = gamma_read; conv.write = gamma_write;
  { static const char *const ex[] = {"gamma 0.5"};
    qa_cvar_alias a; memset(&a, 0, sizeof a); a.name = "gamma"; a.target = "r_gamma"; a.conversion = conv;
    a.documentation = (qa_command_documentation){"Quake brightness convention: lower values brighten. Alias of r_gamma with gamma = 1 / r_gamma.", "gamma <1/3..2>", ex, 1, NULL, 0};
    qa_cvar_registry_register_alias(cvars, a); }
  { static const char *const ex[] = {"vid_gamma 0.5"};
    qa_cvar_alias a; memset(&a, 0, sizeof a); a.name = "vid_gamma"; a.target = "r_gamma"; a.conversion = conv;
    a.documentation = (qa_command_documentation){"Quake II brightness convention: lower values brighten. Alias of r_gamma with vid_gamma = 1 / r_gamma.", "vid_gamma <1/3..2>", ex, 1, NULL, 0};
    qa_cvar_registry_register_alias(cvars, a); }
  qa_cvar_alias_conversion ident; memset(&ident, 0, sizeof ident); ident.is_identity = true;
  struct { const char *name, *target, *summary, *usage, *example; } aliases[3] = {
      {"s_volume", "volume", "Alias of volume. Changes the same effects gain and audio menu preference.", "s_volume <0..1>", "s_volume 0.7"},
      {"s_musicvolume", "bgmvolume", "Alias of bgmvolume. Changes the same music gain and audio menu preference.", "s_musicvolume <0..1>", "s_musicvolume 0.5"},
      {"ogg_volume", "bgmvolume", "Quake II music volume. Alias of bgmvolume and the shared music gain.", "ogg_volume <0..1>", "ogg_volume 0.5"}};
  for (int i = 0; i < 3; i++) {
    qa_cvar_alias a; memset(&a, 0, sizeof a);
    a.name = aliases[i].name; a.target = aliases[i].target; a.conversion = ident;
    const char *const *ex = malloc(sizeof(char *)); /* leaked: registry borrows docs for its lifetime */
    if (ex) { ((const char **)ex)[0] = aliases[i].example; }
    a.documentation = (qa_command_documentation){aliases[i].summary, aliases[i].usage, ex, 1, NULL, 0};
    qa_cvar_registry_register_alias(cvars, a);
  }
  { static const char *const ex[] = {"volume 0.7", "volume 0"};
    qa_command_documentation doc = {"Effects volume shared with the audio menu. Output clamps to 0 through 1.", "volume <0..1>", ex, 2, NULL, 0};
    qa_cvar_registry_document(cvars, "volume", doc); }
  { static const char *const ex[] = {"bgmvolume 0.5", "bgmvolume 0"};
    qa_command_documentation doc = {"Music volume shared with the audio menu. Output clamps to 0 through 1.", "bgmvolume <0..1>", ex, 2, NULL, 0};
    qa_cvar_registry_document(cvars, "bgmvolume", doc); }
}
void qa_app_apply_audio_output_settings(qa_cvar_registry *cvars, qa_app_audio_output_target audio, qa_app_shared_audio_settings_host host) {
  qa_audio_output_format desired = host.read_audio_output_cvars(host.ctx, cvars);
  qa_audio_output_format current = audio.output_format(audio.ctx);
  if (desired.sample_rate != current.sample_rate || desired.sample_bits != current.sample_bits || desired.channels != current.channels)
    audio.select_output_format(audio.ctx, desired);
}
