/* Port of src/app/bootstrap/startup-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_source.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/server_administration.h"
#include "qa/content/q3/base/settings.h"
#include "qa/core/q3_product_policy.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/settings/server/cvars.h"
#include "qa/settings/server/profile.h"
#include "qa/settings/server/q2_owner.h"

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

static bool is_q2(qa_command_dialect d) { return d == QA_COMMAND_DIALECT_Q2_CLASSIC || d == QA_COMMAND_DIALECT_Q2_RERELEASE; }
static void set_or_register(qa_cvar_registry *cvars, const char *name, const char *value) {
  if (!cvar_exists_(cvars, name)) cvar_register_(cvars, name, value, 0);
  else cvar_set_(cvars, name, value, true);
}
static char *num(double v) { char *r = qa_input_js_number(v); if (!r) qa_range_error("Out of memory"); return r; }
qa_cvar_registry *qa_app_create_startup_source(qa_app_startup_source_host host, const qa_app_options *options,
                                               qa_app_startup_source_selection selection, qa_command_dialect dialect,
                                               qa_command_context context, double max_clients,
                                               qa_command_print_fn print, void *print_ctx) {
  qa_cvar_registry_options opts; memset(&opts, 0, sizeof opts);
  opts.dialect = dialect; opts.context = context; opts.print = print; opts.print_ctx = print_ctx;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&opts);
  if (cvars == NULL) return NULL;
  if (is_q2(dialect)) qa_register_q2_server_cvars(cvars, selection.match_provider);
  else if (dialect == QA_COMMAND_DIALECT_Q3) {
    if (options->has_q3_product) {
      cvar_set_(cvars, "com_prereleaseDemo", qa_q3_prerelease_demo(options->q3_product.policy) ? "1" : "0", true);
      cvar_set_(cvars, "com_prereleaseTeamArenaDemo", qa_q3_team_arena_demo(options->q3_product.policy) ? "1" : "0", true);
      cvar_set_(cvars, "fs_restrict", options->q3_product.restriction.kind == QA_Q3_MOUNT_DEMO ? "1" : "0", true);
    }
    if (host.register_q3_product_policy) host.register_q3_product_policy(host.ctx, cvars);
    cvar_register_(cvars, "fs_restrict", "0", QA_CVAR_FLAG_INIT);
    bool missionpack = selection.source_content && strstr(selection.source_content, "missionpack") != NULL;
    size_t count = 0;
    const qa_q3_base_cvar_definition *defs = qa_q3_base_game_cvar_definitions(missionpack ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3, &count);
    for (size_t i = 0; i < count; i++) cvar_register_(cvars, defs[i].name, defs[i].value, defs[i].flags);
  } else {
    const struct { const char *name, *value; } defaults[] = {
        {"skill", "1"}, {"deathmatch", "0"}, {"coop", "0"}, {"teamplay", "0"}, {"sv_cheats", "0"},
        {"sv_aim", dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD ? "2" : "0.93"}, {"developer", "0"},
        {"pausable", "1"}, {"sv_gravity", "800"}, {"sv_maxspeed", "320"}, {"samelevel", "0"},
        {"timelimit", "0"}, {"fraglimit", "0"}, {"gamecfg", "0"}, {"registered", "1"}, {"footsteps", "1"}};
    for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
      cvar_register_(cvars, defaults[i].name, defaults[i].value, 0);
  }
  char *skill = num(options->skill);
  set_or_register(cvars, "skill", skill); free(skill);
  set_or_register(cvars, "deathmatch", strcmp(options->mode, "deathmatch") == 0 ? "1" : "0");
  set_or_register(cvars, "coop", strcmp(options->mode, "coop") == 0 ? "1" : "0");
  bool ctf = selection.match_provider && strcmp(selection.match_provider, "q3:ctf") == 0;
  set_or_register(cvars, "g_gametype",
                  (ctf || (selection.rules && strcmp(selection.rules, "ctf") == 0 && dialect == QA_COMMAND_DIALECT_Q3))
                      ? "4" : strcmp(options->mode, "singleplayer") == 0 ? "2" : "0");
  if (ctf) {
    set_or_register(cvars, "g_teamAutoJoin", "1");
    set_or_register(cvars, "g_teamForceBalance", "1");
  }
  char *clients = num(max_clients);
  set_or_register(cvars, dialect == QA_COMMAND_DIALECT_Q3 ? "sv_maxclients" : "maxclients", clients);
  free(clients);
  if (dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD && host.register_quakeworld_engine_cvars)
    host.register_quakeworld_engine_cvars(host.ctx, cvars);
  if (dialect == QA_COMMAND_DIALECT_Q3 && host.register_q3_server_cvars)
    host.register_q3_server_cvars(host.ctx, cvars, max_clients, options->map);
  if (host.register_frame_time_cvars) host.register_frame_time_cvars(host.ctx, cvars);
  qa_app_register_source_administration_cvars(cvars);
  if (!options->dedicated && options->network_kind && strcmp(options->network_kind, "offline") == 0 && dialect != QA_COMMAND_DIALECT_Q3) {
    cvar_register_(cvars, "sv_autosave", "1", QA_CVAR_FLAG_ARCHIVE);
    cvar_register_(cvars, "sv_autosave_interval", "0", QA_CVAR_FLAG_ARCHIVE);
  }
  cvar_register_(cvars, "qts_weaponBehavior", "", QA_CVAR_FLAG_ARCHIVE);
  return cvars;
}
static void apply_latched_all(qa_cvar_registry *cvars) {
  size_t count = 0;
  qa_cvar_snapshot *snaps = qa_cvar_registry_apply_latched(cvars, NULL, &count);
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&snaps[i]);
  free(snaps);
}
qa_app_startup_rules qa_app_resolve_startup_rules(qa_app_options options, qa_cvar_registry *cvars, double default_capacity,
                                                  const qa_server_setting_definition *const *definitions, size_t definition_count, bool apply_explicit) {
  apply_latched_all(cvars);
  if (apply_explicit && options.explicit_skill) {
    char *skill = num(options.skill);
    cvar_set_(cvars, "skill", skill, true);
    free(skill);
  }
  if (apply_explicit && options.explicit_mode) {
    cvar_set_(cvars, "deathmatch", strcmp(options.mode, "deathmatch") == 0 ? "1" : "0", true);
    cvar_set_(cvars, "coop", strcmp(options.mode, "coop") == 0 ? "1" : "0", true);
    if (qa_cvar_registry_dialect(cvars) == QA_COMMAND_DIALECT_Q3) {
      if (strcmp(options.mode, "singleplayer") == 0) cvar_set_(cvars, "g_gametype", "2", true);
      else if (strcmp(options.mode, "deathmatch") != 0 || true) {
        qa_cvar_snapshot snap; memset(&snap, 0, sizeof snap);
        bool found = qa_cvar_registry_get(cvars, "g_gametype", &snap);
        if (strcmp(options.mode, "deathmatch") != 0 || (found && snap.integer_value == 2))
          cvar_set_(cvars, "g_gametype", "0", true);
        if (found) qa_cvar_read_free(&snap);
      }
    }
  }
  if (apply_explicit && options.server_profile != NULL) {
    qa_cvar_server_owner *owners = calloc(definition_count ? definition_count : 1, sizeof *owners);
    qa_bound_server_setting *targets = calloc(definition_count ? definition_count : 1, sizeof *targets);
    if (!owners || !targets) qa_range_error("Out of memory");
    for (size_t i = 0; i < definition_count; i++) {
      qa_cvar_server_settings_owner_init(&owners[i], cvars, true);
      targets[i].definition = definitions[i];
      targets[i].owner = owners[i].owner;
    }
    qa_apply_server_profile((const qa_server_profile *)options.server_profile, targets, definition_count);
    free(owners); free(targets);
  }
  apply_latched_all(cvars);
  double skill_value = floor(qa_cvar_registry_variable_value(cvars, "skill"));
  if (skill_value < 0) skill_value = 0;
  if (skill_value > 3) skill_value = 3;
  if (skill_value != 0 && skill_value != 1 && skill_value != 2 && skill_value != 3)
    qa_range_error("Invalid source startup difficulty");
  const char *mode;
  if (apply_explicit && options.explicit_mode) mode = options.mode;
  else if (qa_cvar_registry_dialect(cvars) == QA_COMMAND_DIALECT_Q3)
    mode = qa_cvar_registry_variable_value(cvars, "g_gametype") == 2 ? "singleplayer" : "deathmatch";
  else
    mode = qa_cvar_registry_variable_value(cvars, "deathmatch") != 0 ? "deathmatch"
           : qa_cvar_registry_variable_value(cvars, "coop") != 0 ? "coop" : "singleplayer";
  const char *capacity_name = qa_cvar_registry_dialect(cvars) == QA_COMMAND_DIALECT_Q3 ? "sv_maxclients" : "maxclients";
  double requested = (apply_explicit && options.explicit_capacity) ? default_capacity
                     : qa_cvar_registry_variable_value(cvars, capacity_name) > 0 ? qa_cvar_registry_variable_value(cvars, capacity_name) : default_capacity;
  double native_capacity = (is_q2(qa_cvar_registry_dialect(cvars)) && requested <= 1)
                               ? (strcmp(mode, "deathmatch") == 0 ? 8 : strcmp(mode, "coop") == 0 ? 4 : requested)
                               : requested;
  double floor_clients = options.dedicated ? 1 : options.seats;
  double truncated = trunc(native_capacity);
  double max_clients = floor_clients > truncated ? floor_clients : truncated;
  if (max_clients != trunc(max_clients) || max_clients < 1 || max_clients > 64)
    qa_range_error("Source startup client capacity must be between 1 and 64");
  char *clients = num(max_clients);
  cvar_set_(cvars, capacity_name, clients, true);
  free(clients);
  options.skill = skill_value;
  options.mode = mode;
  qa_app_startup_rules result = {options, max_clients};
  return result;
}
