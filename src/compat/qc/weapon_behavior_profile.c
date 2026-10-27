/* Port of src/compat/qc/weapon-behavior-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/weapon_behavior_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const qa_compat_qc_weapon_field_spec qa_compat_qc_weapon_behavior_fields[12] = {
  {"origin","vector"},{"velocity","vector"},{"angles","vector"},{"mins","vector"},{"maxs","vector"},{"v_angle","vector"},
  {"health","float"},{"solid","float"},{"nextthink","float"},{"classname","string"},{"netname","string"},{"think","function"}};
const qa_compat_qc_weapon_field_spec qa_compat_qc_weapon_behavior_globals[2] = {{"self","entity"},{"other","entity"}};
static const qa_compat_qc_weapon_field_spec opt_fields[3] = {{"model","string"},{"modelindex","float"},{"chain","entity"}};
static const qa_compat_qc_weapon_field_spec opt_globals[11] = {
  {"time","float"},{"trace_endpos","vector"},{"trace_plane_normal","vector"},{"trace_ent","entity"},
  {"deathmatch","float"},{"coop","float"},{"trace_fraction","float"},{"trace_allsolid","float"},{"trace_startsolid","float"},
  {"trace_inwater","float"},{"trace_inopen","float"}};
/* NOTE: TS also lists trace_plane_dist in optionalGlobals; included below via extra check. */
static char *err(const char *fmt, const char *name, const char *type) { size_t n = strlen(fmt) + strlen(name) + strlen(type) + 8; char *r = malloc(n); if (r) snprintf(r, n, fmt, name, type); return r; }
char *qa_compat_qc_weapon_behavior_capability_error(const qa_compat_qc_program *p) {
  for (int i = 0; i < 12; i++) { const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(p, qa_compat_qc_weapon_behavior_fields[i].name);
    if (!f || strcmp(f->type, qa_compat_qc_weapon_behavior_fields[i].type)) return err("QuakeC trajectory adapter requires entity field %s of type %s", qa_compat_qc_weapon_behavior_fields[i].name, qa_compat_qc_weapon_behavior_fields[i].type); }
  for (int i = 0; i < 2; i++) { const qa_compat_qc_definition *g = qa_compat_qc_program_global_named(p, qa_compat_qc_weapon_behavior_globals[i].name);
    if (!g || strcmp(g->type, qa_compat_qc_weapon_behavior_globals[i].type)) return err("QuakeC trajectory adapter requires global %s of type %s", qa_compat_qc_weapon_behavior_globals[i].name, qa_compat_qc_weapon_behavior_globals[i].type); }
  for (int i = 0; i < 3; i++) { const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(p, opt_fields[i].name);
    if (f && strcmp(f->type, opt_fields[i].type)) return err("QuakeC trajectory adapter requires declared entity field %s to have type %s", opt_fields[i].name, opt_fields[i].type); }
  for (int i = 0; i < 11; i++) { const qa_compat_qc_definition *g = qa_compat_qc_program_global_named(p, opt_globals[i].name);
    if (g && strcmp(g->type, opt_globals[i].type)) return err("QuakeC trajectory adapter requires declared global %s to have type %s", opt_globals[i].name, opt_globals[i].type); }
  { const qa_compat_qc_definition *g = qa_compat_qc_program_global_named(p, "trace_plane_dist");
    if (g && strcmp(g->type, "float")) return err("QuakeC trajectory adapter requires declared global %s to have type %s", "trace_plane_dist", "float"); }
  return NULL;
}
