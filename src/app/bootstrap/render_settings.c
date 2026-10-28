/* Port of src/app/bootstrap/render-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/render_settings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/input/support.h"

static void cvar_register_(qa_cvar_registry *r, const char *name, const char *def, double flags) {
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (qa_cvar_registry_register(r, name, def, flags, &out)) qa_cvar_read_free(&out);
}

static char *validate_shadows(void *ctx, const char *value) {
  (void)ctx;
  char *trimmed = qa_input_js_trim(value);
  bool empty = trimmed == NULL || trimmed[0] == '\0';
  free(trimmed);
  double n = qa_input_js_parse_number(value);
  return (!empty && isfinite(n)) ? NULL : qa_input_dup("Shadows require a finite number");
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
}
