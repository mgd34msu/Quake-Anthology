/* Port of src/compat/qc/mod-environment.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_environment.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef qa_compat_qc_mod_environment E;
static bool actor_eq(qa_actor_id a, qa_actor_id b){return a.slot==b.slot&&a.generation==b.generation&&a.token==b.token&&a.session.token==b.session.token;}
static void env_print(void *ctx, const char *text) { E *e = ctx; if (e->services->engine && e->services->engine->print) e->services->engine->print(e->services->engine->ctx, text); (void)e; }
/* print fn signature check: qa_command_print_fn */
static void b_cvar(void *ctx, qa_compat_qc_machine *m) { E *e=ctx; qa_compat_qc_machine_return_float(m, qa_cvar_registry_variable_value(e->cvars, qa_compat_qc_machine_arg_string(m,0))); }
static void b_cvar_set(void *ctx, qa_compat_qc_machine *m) {
  E *e=ctx; const char *n=qa_compat_qc_machine_arg_string(m,0); char name[256]; snprintf(name,sizeof name,"%s",n);
  qa_cvar_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set(e->cvars, name, qa_compat_qc_machine_arg_string(m,1), false, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void b_dprint(void *ctx, qa_compat_qc_machine *m) {
  E *e=ctx; if (qa_cvar_registry_variable_value(e->cvars, "developer") != 0 && e->services->engine && e->services->engine->print) {
    char *t = qa_compat_qc_machine_var_string(m, 0); e->services->engine->print(e->services->engine->ctx, t ? t : ""); free(t);
  }
}
static void b_checkclient(void *ctx, qa_compat_qc_machine *m) {
  E *e=ctx;
  if (!e->visibility) { qa_compat_qc_machine_fail(m, "Mod checkclient requires destination client visibility"); }
  int64_t self = qa_compat_qc_words_int(&m->globals, qa_compat_qc_machine_global_offset(m, "self"));
  qa_actor_id actor;
  bool has = qa_q1_client_visibility_check(e->visibility, (qa_q1_client_eye){qa_compat_qc_machine_entity_vector(m,self,"origin"), qa_compat_qc_machine_entity_vector(m,self,"view_ofs")},
    qa_compat_qc_words_float(&m->globals, qa_compat_qc_machine_global_offset(m, "time")), *m->numeric, &actor);
  qa_compat_qc_machine_return_int(m, (double)e->reference(e->reference_ctx, has ? &actor : NULL));
}
static qa_q1_visibility_client vis_client(void *ctx, int64_t slot) { E *e = ctx; return e->clients->at(e->clients->ctx, slot); }
bool qa_compat_qc_mod_environment_init(E *e, qa_mod_host_services *services, qa_compat_qc_program *program, qa_mod_callback_declaration declaration, void *ref_ctx, int64_t (*reference)(void *, const qa_actor_id *), const qa_compat_qc_mod_environment_clients *clients) {
  memset(e, 0, sizeof *e); e->services = services; e->reference_ctx = ref_ctx; e->reference = reference; e->has_reference = reference != NULL; e->clients = clients; e->self_ref = e;
  const qa_session_mod_engine_services *engine = services->engine;
  bool has_env = engine && engine->has_environment;
  qa_cvar_registry_options opts; memset(&opts, 0, sizeof opts);
  opts.dialect = program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? QA_COMMAND_DIALECT_Q1_QUAKEWORLD : QA_COMMAND_DIALECT_Q1_NETQUAKE;
  opts.context.session = qa_session_actor_registry_session(services->actors);
  opts.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  opts.print = (qa_command_print_fn)env_print; opts.print_ctx = e;
  e->cvars = qa_cvar_registry_create(&opts);
  if (!e->cvars) return false;
  char buf[64];
  typedef struct { const char *name; const char *value; } Def; Def defaults[15];
  int di = 0;
  char skill[32], maxc[32], grav[32];
  snprintf(skill,sizeof skill,"%g", has_env ? engine->environment_skill : 1);
  /* TS String(number): integers print without fraction; %g matches for these magnitudes */
  snprintf(maxc,sizeof maxc,"%g", declaration.clients ? declaration.clients->maximum : has_env ? engine->environment_max_clients : 1);
  snprintf(grav,sizeof grav,"%g", has_env && engine->environment_gravity ? engine->environment_gravity : 800);
  const char *mode = has_env ? engine->environment_mode : NULL;
  defaults[di++] = (Def){"skill", skill};
  defaults[di++] = (Def){"maxclients", maxc};
  defaults[di++] = (Def){"coop", mode && strcmp(mode,"coop")==0 ? "1" : "0"};
  defaults[di++] = (Def){"deathmatch", mode && strcmp(mode,"deathmatch")==0 ? "1" : "0"};
  defaults[di++] = (Def){"teamplay", "0"};
  defaults[di++] = (Def){"sv_gravity", grav};
  defaults[di++] = (Def){"sv_aim", program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? "2" : "0.93"};
  defaults[di++] = (Def){"sv_maxspeed", "320"};
  defaults[di++] = (Def){"registered", "1"};
  defaults[di++] = (Def){"developer", "0"};
  defaults[di++] = (Def){"sv_cheats", "0"};
  defaults[di++] = (Def){"samelevel", "0"};
  defaults[di++] = (Def){"timelimit", "0"};
  defaults[di++] = (Def){"fraglimit", "0"};
  defaults[di++] = (Def){"gamecfg", "0"};
  (void)buf;
  /* qa_cvar_registry_register/set/find require a non-NULL out snapshot. */
  for (int i = 0; i < di; i++) { qa_cvar_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot); if (qa_cvar_registry_register(e->cvars, defaults[i].name, defaults[i].value, 0, &snapshot)) qa_cvar_read_free(&snapshot); }
  if (program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD) { qa_cvar_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot); if (qa_cvar_registry_register(e->cvars, "sv_phs", "1", 0, &snapshot)) qa_cvar_read_free(&snapshot); }
  for (size_t i = 0; i < declaration.cvar_count; i++) {
    qa_cvar_read variable; memset(&variable, 0, sizeof variable);
    bool found = qa_cvar_registry_find(e->cvars, declaration.cvars[i].name, &variable);
    if (found) qa_cvar_read_free(&variable);
    qa_cvar_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
    if (!found) { if (qa_cvar_registry_register(e->cvars, declaration.cvars[i].name, declaration.cvars[i].value, 0, &snapshot)) qa_cvar_read_free(&snapshot); }
    else if (qa_cvar_registry_set(e->cvars, declaration.cvars[i].name, declaration.cvars[i].value, true, &snapshot)) qa_cvar_read_free(&snapshot);
  }
  e->visibility = clients ? qa_q1_client_visibility_create((int64_t)clients->maximum, e, vis_client, clients->visibility) : NULL;
  if (clients && !e->visibility) { qa_cvar_registry_destroy(e->cvars); return false; }
  e->host_count = 4;
  e->host_entries[0]=(qa_compat_qc_host_binding){"cvar",{e,b_cvar}};
  e->host_entries[1]=(qa_compat_qc_host_binding){"cvar_set",{e,b_cvar_set}};
  e->host_entries[2]=(qa_compat_qc_host_binding){"dprint",{e,b_dprint}};
  e->host_entries[3]=(qa_compat_qc_host_binding){"checkclient",{e,b_checkclient}};
  return true;
}
void qa_compat_qc_mod_environment_destroy(E *e) {
  if (e->visibility) qa_q1_client_visibility_destroy(e->visibility);
  if (e->cvars) qa_cvar_registry_destroy(e->cvars);
  memset(e, 0, sizeof *e);
}
void qa_compat_qc_mod_environment_initialize_globals(E *e, qa_compat_qc_machine *m) {
  static const char *const names[4] = {"skill","deathmatch","coop","teamplay"};
  for (int i = 0; i < 4; i++) { const qa_compat_qc_definition *d = qa_compat_qc_program_global_named(m->program, names[i]);
    if (d && strcmp(d->type,"float")==0) qa_compat_qc_words_set_float(&m->globals, d->offset, qa_cvar_registry_variable_value(e->cvars, names[i])); }
  const qa_compat_qc_definition *time_d = qa_compat_qc_program_global_named(m->program, "time");
  qa_source_time now = e->services->time(e->services->ctx);
  if (time_d && strcmp(time_d->type,"float")==0) qa_compat_qc_words_set_float(&m->globals, time_d->offset, now.kind==QA_SOURCE_TIME_SECONDS?now.value:now.value/1000);
  const qa_compat_qc_definition *mapname = qa_compat_qc_program_global_named(m->program, "mapname");
  const char *map = e->services->engine ? (const char *)e->services->engine->presentation_map : NULL;
  if (mapname && strcmp(mapname->type,"string")==0 && map) {
    char *clean = strdup(map);
    const char *start = strncmp(clean, "maps/", 5)==0 ? clean+5 : clean;
    char *copy = strdup(start);
    size_t n = strlen(copy); if (n>=4 && strcmp(copy+n-4,".bsp")==0) copy[n-4]=0;
    qa_compat_qc_words_set_int(&m->globals, mapname->offset, (double)qa_compat_qc_strings_allocate(&m->strings, copy));
    free(clean); free(copy);
  }
}
bool qa_compat_qc_mod_environment_client(E *e, qa_actor_id actor, qa_q1_visibility_client *out) {
  if (!e->clients) qa_range_error("Mod client field requires destination client semantics");
  for (int64_t slot = 1; slot <= (int64_t)e->clients->maximum; slot++) {
    qa_q1_visibility_client c = e->clients->at(e->clients->ctx, slot);
    if (c.has_actor && actor_eq(c.actor, actor)) { *out = c; return true; }
  }
  return false;
}
