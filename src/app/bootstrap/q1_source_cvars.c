/* Port of src/app/bootstrap/q1-source-cvars.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q1_source_cvars.h"
#include <stddef.h>
qa_cvar_registry *qa_app_clone_q1_source_cvars(qa_cvar_registry *source,
    qa_command_print_fn print, void *print_ctx) {
  qa_cvar_registry_options options = {0};
  options.dialect = qa_cvar_registry_dialect(source);
  options.context = qa_cvar_registry_context(source);
  options.print = print;
  options.print_ctx = print_ctx;
  qa_cvar_registry *candidate = qa_cvar_registry_create(&options);
  if (candidate == NULL) return NULL;
  qa_save_value *state = qa_cvar_registry_capture_world_transfer_state(source);
  qa_cvar_registry_restore_save_state(candidate, state);
  return candidate;
}
void qa_app_register_q1_bot_controls(qa_cvar_registry *cvars) {
  qa_cvar_read found;
  bool exists = qa_cvar_registry_find(cvars, "bot_minplayers", &found);
  if (exists) qa_cvar_read_free(&found);
  if (!exists || qa_cvar_registry_is_console_created(cvars, "bot_minplayers")) {
    qa_cvar_snapshot snap;
    if (qa_cvar_registry_register(cvars, "bot_minplayers", "0", 0, &snap)) qa_cvar_read_free(&snap);
  }
}
