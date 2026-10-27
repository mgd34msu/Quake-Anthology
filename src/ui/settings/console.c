/* Port of src/ui/settings/console.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/console.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

typedef struct console_owner {
  qa_ui_setting_binding items[1];
} console_owner;
static void console_teardown(void *v) {
  console_owner *o = v;
  qa_ui_cvar_setting_binding_dispose(&o->items[0]);
  free(o);
}

qa_ui_setting_bindings qa_ui_bind_console_settings(
    qa_cvar_registry *registry) {
  static const qa_ui_choice choices[5] = {
    { "0", "Auto" }, { "1", "1x" }, { "2", "2x" }, { "3", "3x" },
    { "4", "4x" }
  };
  console_owner *o = calloc(1, sizeof *o);
  if (o == NULL) qa_range_error("Out of memory");
  qa_ui_cvar_setting_spec spec;
  memset(&spec, 0, sizeof spec);
  spec.name = "con_scale";
  spec.label = "Console text size";
  spec.category = "accessibility";
  spec.kind = "choice";
  spec.choices = choices;
  spec.choice_count = 5;
  if (!qa_ui_bind_cvar_setting(registry, &spec, NULL, &o->items[0]))
    qa_range_error("Out of memory");
  return qa_ui_setting_bindings_own(o->items, 1, o, console_teardown);
}
