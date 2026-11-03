/* Port of src/app/bootstrap/simulation/quakeworld-cvars.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakeworld_cvars.h"
void qa_app_sim_register_quakeworld_engine_cvars(qa_cvar_registry *cvars) {
  static const struct { const char *name; const char *value; } entries[] = {
    {"sv_phs", "1"}, {"sv_stopspeed", "100"}, {"sv_spectatormaxspeed", "500"},
    {"sv_accelerate", "10"}, {"sv_airaccelerate", "0.7"}, {"sv_wateraccelerate", "10"},
    {"sv_friction", "4"}, {"sv_waterfriction", "4"}, {"password", ""},
    {"spectator_password", ""}, {"sv_highchars", "1"},
  };
  for (size_t i = 0; i < sizeof entries / sizeof entries[0]; i++) {
    qa_cvar_read found;
    bool exists = qa_cvar_registry_find(cvars, entries[i].name, &found);
    qa_cvar_read_free(&found);
    if (!exists) {
      qa_cvar_snapshot registered;
      qa_cvar_registry_register(cvars, entries[i].name, entries[i].value, 0, &registered);
      qa_cvar_read_free(&registered);
    }
  }
  qa_cvar_snapshot registered;
  qa_cvar_registry_register(cvars, "maxspectators", "8", QA_CVAR_FLAG_SERVER_INFO, &registered);
  qa_cvar_read_free(&registered);
}
