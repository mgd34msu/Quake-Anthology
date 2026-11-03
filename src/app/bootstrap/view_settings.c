/* Port of src/app/bootstrap/view-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/view_settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/persistence/value.h"

static void cvar_set_(qa_cvar_registry *r, const char *name, const char *value, bool force) {
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (qa_cvar_registry_set(r, name, value, force, &out)) qa_cvar_read_free(&out);
}
static void cvar_register_(qa_cvar_registry *r, const char *name, const char *def, double flags) {
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (qa_cvar_registry_register(r, name, def, flags, &out)) qa_cvar_read_free(&out);
}

struct qa_app_view_settings {
  void (*changed)(void *ctx, double value); void *ctx;
  bool has_selected; double selected;
  qa_cvar_registry *cvars; bool explicit; int64_t binding;
  char *label;
};
qa_app_view_settings *qa_app_view_settings_create(void (*changed)(void *ctx, double value), void *ctx) {
  qa_app_view_settings *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->changed = changed; s->ctx = ctx; s->binding = -1;
  return s;
}
void qa_app_view_settings_destroy(qa_app_view_settings *s) { if (s) { free(s->label); free(s); } }
double qa_app_view_settings_field_of_view(const qa_app_view_settings *s) {
  if (s->cvars == NULL) return s->has_selected ? s->selected : 90;
  char *text = qa_cvar_registry_variable_string(s->cvars, "fov");
  double v = qa_input_js_parse_number(text ? text : "");
  free(text);
  return v;
}
bool qa_app_view_settings_override(const qa_app_view_settings *s, double *out) {
  if (!s->explicit) return false;
  *out = qa_app_view_settings_field_of_view(s);
  return true;
}
static char *validate_fov(void *ctx, const char *value) {
  (void)ctx;
  const char *e = qa_app_validate_field_of_view(value);
  return e ? qa_input_dup(e) : NULL;
}
static void fov_changed(void *ctx, const char *value) {
  qa_app_view_settings *s = ctx;
  s->explicit = true;
  s->changed(s->ctx, qa_input_js_parse_number(value));
}
static bool dialect_q1(qa_command_dialect d) { return d == QA_COMMAND_DIALECT_Q1_NETQUAKE || d == QA_COMMAND_DIALECT_Q1_QUAKEWORLD; }
void qa_app_view_settings_bind_cvars(qa_app_view_settings *s, qa_cvar_registry *cvars) {
  if (s->cvars != NULL) qa_range_error("View settings already have a cvar owner");
  qa_cvar_read existing; memset(&existing, 0, sizeof existing);
  bool found = qa_cvar_registry_find(cvars, "fov", &existing);
  bool stored = found && strcmp(existing.value, "90") != 0 && qa_app_validate_field_of_view(existing.value) == NULL;
  if (!dialect_q1(qa_cvar_registry_dialect(cvars)) || !found || strcmp(existing.reset_value, "90") != 0)
    cvar_register_(cvars, "fov", "90", QA_CVAR_FLAG_ARCHIVE);
  if (found) qa_cvar_read_free(&existing);
  char *current = qa_cvar_registry_variable_string(cvars, "fov");
  if (qa_app_validate_field_of_view(current ? current : "") != NULL) cvar_set_(cvars, "fov", "90", true);
  free(current);
  if (!stored && s->has_selected) {
    char *num = qa_input_js_number(s->selected);
    cvar_set_(cvars, "fov", num ? num : "90", false);
    free(num);
  }
  s->cvars = cvars; s->has_selected = false;
  if (stored) {
    char *now = qa_cvar_registry_variable_string(cvars, "fov");
    if (qa_input_js_parse_number(now ? now : "") != 90) s->explicit = true;
    free(now);
  }
  qa_cvar_value_binding b = {s, validate_fov, fov_changed};
  s->binding = qa_cvar_registry_bind_value(cvars, "fov", b);
  static const char *const ex[] = {"fov 120", "set fov 90"};
  static const char *const allowed[] = {"60 through 160"};
  qa_command_documentation doc = {"Shared field-of-view preference in degrees. Game zoom and special cameras retain control.",
                                  "fov [degrees]", ex, 2, allowed, 1};
  qa_cvar_registry_document(cvars, "fov", doc);
}
void qa_app_view_settings_unbind_cvars(qa_app_view_settings *s) {
  if (s->cvars == NULL) return;
  qa_cvar_registry_unbind_value(s->cvars, s->binding);
  s->selected = qa_app_view_settings_field_of_view(s);
  s->has_selected = true;
  s->cvars = NULL; s->binding = -1;
}
void qa_app_view_settings_set_field_of_view(qa_app_view_settings *s, double value) {
  if (!isfinite(value) || value < 60 || value > 160)
    qa_raise("RangeError", 0, 0, "Field of view must be between 60 and 160 degrees");
  s->explicit = true;
  if (s->cvars == NULL) { s->selected = value; s->has_selected = true; s->changed(s->ctx, value); }
  else {
    char *num = qa_input_js_number(value);
    cvar_set_(s->cvars, "fov", num ? num : "90", false);
    free(num);
  }
}
static bool binding_enabled(void *ctx) { (void)ctx; return true; }
static double binding_read(void *ctx) { return qa_app_view_settings_field_of_view((qa_app_view_settings *)ctx); }
static void binding_write(void *ctx, double v) { qa_app_view_settings_set_field_of_view((qa_app_view_settings *)ctx, v); }
qa_ui_setting_binding qa_app_view_settings_binding(qa_app_view_settings *s) {
  char buf[96];
  char *num = qa_input_js_number(qa_app_view_settings_field_of_view(s));
  snprintf(buf, sizeof buf, "Field of view (%s)", num ? num : "90");
  free(num);
  free(s->label); s->label = qa_input_dup(buf);
  qa_ui_setting_binding b; memset(&b, 0, sizeof b);
  b.id = "ui:view:field-of-view"; b.label = s->label; b.category = "display"; b.kind = "slider";
  b.ctx = s; b.enabled = binding_enabled; b.read_num = binding_read; b.write_num = binding_write;
  b.minimum = 60; b.maximum = 160; b.step = 5;
  return b;
}
typedef struct load_call { qa_app_view_settings *settings; const qa_save_value *value; } load_call;
static void load_body(void *ctx) {
  load_call *call = ctx;
  const qa_save_value *value = call->value;
  const qa_save_value *version = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "version") : NULL;
  const qa_save_value *fov = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "fieldOfView") : NULL;
  if (version == NULL || version->kind != QA_SAVE_NUMBER || version->number != 1 || fov == NULL || fov->kind != QA_SAVE_NUMBER)
    qa_raise("Error", 0, 0, "Invalid view preferences");
  qa_app_view_settings_set_field_of_view(call->settings, fov->number);
}
void qa_app_view_settings_load(qa_app_view_settings *s, const qa_config_store *store) {
  qa_app_view_settings_load_warn(s, store, NULL, NULL);
}
void qa_app_view_settings_load_warn(qa_app_view_settings *s, const qa_config_store *store,
    void (*warn)(void *ctx, const char *text), void *warn_ctx) {
  size_t len = 0;
  char *text = qa_config_store_load_text(store, "view.json", &len);
  if (text == NULL) return;
  /* Text that is not JSON keeps the defaults, like absence; parsed values still face validation. */
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, len, true);
  free(text);
  if (value == NULL) return;
  load_call call = {s, value};
  qa_caught_error caught;
  bool ok = qa_try(load_body, &call, &caught);
  qa_save_value_free(value);
  if (!ok && warn != NULL) {
    char message[QA_CAUGHT_MESSAGE_CAPACITY + 32];
    snprintf(message, sizeof message, "Ignoring view.json: %s\n", caught.message);
    warn(warn_ctx, message);
  }
}
void qa_app_view_settings_save(qa_app_view_settings *s, const qa_config_store *store) {
  if (!s->explicit) return;
  char *num = qa_input_js_number(qa_app_view_settings_field_of_view(s));
  char buf[128];
  snprintf(buf, sizeof buf, "{\"version\":1,\"fieldOfView\":%s}\n", num ? num : "90");
  free(num);
  qa_config_store_dump(store, "view.json", buf);
}
