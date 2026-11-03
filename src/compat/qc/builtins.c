/* Port of src/compat/qc/builtins.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/builtins.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"

typedef struct { int64_t n; const char *name; } HN;
static const HN host_numbers[] = {
  {2,"setorigin"},{3,"setmodel"},{4,"setsize"},{6,"break"},{8,"sound"},{11,"objerror"},
  {14,"spawn"},{15,"remove"},{16,"traceline"},{17,"checkclient"},{19,"precache_sound"},{20,"precache_model"},
  {21,"stuffcmd"},{22,"findradius"},{23,"bprint"},{24,"sprint"},{25,"dprint"},{28,"coredump"},{31,"eprint"},
  {32,"walkmove"},{34,"droptofloor"},{35,"lightstyle"},{40,"checkbottom"},{41,"pointcontents"},{44,"aim"},
  {45,"cvar"},{46,"localcmd"},{48,"particle"},{49,"changeyaw"},{52,"WriteByte"},{53,"WriteChar"},
  {54,"WriteShort"},{55,"WriteLong"},{56,"WriteCoord"},{57,"WriteAngle"},{58,"WriteString"},{59,"WriteEntity"},
  {67,"movetogoal"},{68,"precache_file"},{69,"makestatic"},{70,"changelevel"},{72,"cvar_set"},
  {73,"centerprint"},{74,"ambientsound"},{75,"precache_model"},{76,"precache_sound"},{77,"precache_file"},{78,"setspawnparms"},
};
static const char *const rerelease_names[] = {
  "ex_bprint","ex_sprint","ex_centerprint","ex_finaleFinished","ex_localsound",
  "ex_draw_point","ex_draw_line","ex_draw_arrow","ex_draw_ray","ex_draw_circle","ex_draw_bounds",
  "ex_draw_worldtext","ex_draw_sphere","ex_draw_cylinder","ex_bot_movetopoint","ex_bot_followentity",
  "ex_CheckPlayerEXFlags","ex_walkpathtogoal","ex_prompt","ex_promptchoice","ex_clearprompt",
};
qa_compat_qc_host_kind qa_compat_qc_host_kind_for_program(const qa_compat_qc_program *p) {
  if (p->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD) return "quakeworld";
  for (size_t i = 0; i < p->function_count; i++) if (p->functions[i].named_builtin && strcmp(p->functions[i].name, "ex_centerprint") == 0) return "rerelease";
  return "netquake";
}
size_t qa_compat_qc_host_requirements(qa_compat_qc_host_kind kind, qa_compat_qc_builtin_requirement *out, size_t cap) {
  size_t n = 0;
#define PUSH(has, num, nm) do { if (out && n < cap) out[n] = (qa_compat_qc_builtin_requirement){has, num, nm}; n++; } while (0)
  bool qw = strcmp(kind, "quakeworld") == 0;
  for (size_t i = 0; i < sizeof host_numbers / sizeof *host_numbers; i++) {
    if (qw && host_numbers[i].n == 48) continue;
    PUSH(true, host_numbers[i].n, host_numbers[i].name);
  }
  if (qw) { PUSH(true, 79, "logfrag"); PUSH(true, 80, "infokey"); PUSH(true, 82, "multicast"); }
  if (strcmp(kind, "rerelease") == 0) {
    PUSH(true, 401, "setcolor");
    for (size_t i = 0; i < sizeof rerelease_names / sizeof *rerelease_names; i++) PUSH(false, 0, rerelease_names[i]);
  }
#undef PUSH
  return n;
}
typedef struct { qa_compat_qc_builtin_services s; } State;
static void free_cleanup(void *arg) { free(arg); }
static double vlen(qa_compat_qc_machine *m, qa_vec3 v) {
  const qa_numeric_operations *n = m->numeric; void *c = n->ctx;
  return n->square_root(c, n->add(c, n->add(c, n->multiply(c, v.x, v.x), n->multiply(c, v.y, v.y)), n->multiply(c, v.z, v.z)));
}
static double yaw_of(qa_vec3 v) {
  if (v.x == 0 && v.y == 0) return 0;
  double angle = trunc(atan2(v.y, v.x) * 180 / 3.141592653589793);
  return angle < 0 ? angle + 360 : angle;
}
/* Number.prototype.toFixed(1): exact value, ties to the larger magnitude (printf ties to even);
 * -0 prints unsigned; NaN, infinities and |x| >= 1e21 print as String(x). */
/* Number.prototype.toFixed(1). */
static void to_fixed_1(double v, char *out, size_t capacity) { qa_js_to_fixed(v, 1, out, capacity); }
static void format_float(double v, char out[64]) { /* TS value.toFixed(1).padStart(5," ") */
  char raw[64]; to_fixed_1(v, raw, sizeof raw);
  size_t n = strlen(raw); if (n >= 5) { strcpy(out, raw); return; }
  memset(out, ' ', 5 - n); strcpy(out + (5 - n), raw);
}
static void b_makevectors(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx;
  qa_vec3 a = qa_compat_qc_machine_arg_vector(m, 0); const qa_numeric_operations *n = m->numeric; void *c = n->ctx;
  const double PI = 3.141592653589793;
  double y = a.y * PI / 180, p = a.x * PI / 180, r = a.z * PI / 180;
  double sy = n->store(c, sin(y)), cy = n->store(c, cos(y)), sp = n->store(c, sin(p)), cp = n->store(c, cos(p)), sr = n->store(c, sin(r)), cr = n->store(c, cos(r));
  qa_compat_qc_words_set_vector(&m->globals, qa_compat_qc_machine_global_offset(m, "v_forward"), (qa_vec3){n->multiply(c,cp,cy), n->multiply(c,cp,sy), -sp});
  qa_compat_qc_words_set_vector(&m->globals, qa_compat_qc_machine_global_offset(m, "v_right"), (qa_vec3){
    n->add(c, n->multiply(c, n->multiply(c,-sr,sp), cy), n->multiply(c,cr,sy)),
    n->subtract(c, n->multiply(c, n->multiply(c,-sr,sp), sy), n->multiply(c,cr,cy)), n->multiply(c,-sr,cp)});
  qa_compat_qc_words_set_vector(&m->globals, qa_compat_qc_machine_global_offset(m, "v_up"), (qa_vec3){
    n->add(c, n->multiply(c, n->multiply(c,cr,sp), cy), n->multiply(c,sr,sy)),
    n->subtract(c, n->multiply(c, n->multiply(c,cr,sp), sy), n->multiply(c,sr,cy)), n->multiply(c,cr,cp)});
}
static void b_random(void *ctx, qa_compat_qc_machine *m) {
  const qa_random_source *r = ((State *)ctx)->s.random;
  qa_compat_qc_machine_return_float(m, (double)(qa_int32(r->next_integer(r->ctx)) & 0x7fff) / 0x7fff);
}
static void b_normalize(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx;
  qa_vec3 v = qa_compat_qc_machine_arg_vector(m, 0); double mag = vlen(m, v);
  double inv = mag == 0 ? 0 : m->numeric->divide(m->numeric->ctx, 1, mag); const qa_numeric_operations *n = m->numeric; void *c = n->ctx;
  qa_compat_qc_machine_return_vector(m, (qa_vec3){n->multiply(c,v.x,inv), n->multiply(c,v.y,inv), n->multiply(c,v.z,inv)});
}
static void b_error(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx;
  char *s = qa_compat_qc_machine_var_string(m, 0); qa_cleanup scope; qa_cleanup_push(&scope, free_cleanup, s);
  qa_compat_qc_machine_fail(m, "%s", s ? s : "");
}
static void b_vlen(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; qa_compat_qc_machine_return_float(m, vlen(m, qa_compat_qc_machine_arg_vector(m, 0))); }
static void b_vectoyaw(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; qa_compat_qc_machine_return_float(m, yaw_of(qa_compat_qc_machine_arg_vector(m, 0))); }
static void b_ftos(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx;
  double v = qa_compat_qc_machine_arg_float(m, 0); char buf[64];
  if (v == trunc(v)) qa_net_js_number(trunc(v), buf, sizeof buf); else format_float(v, buf);
  qa_compat_qc_machine_return_int(m, (double)qa_compat_qc_strings_set_engine(&m->strings, "pr_string_temp", buf, -1));
}
static void b_vtos(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx;
  qa_vec3 v = qa_compat_qc_machine_arg_vector(m, 0); char x[64], y[64], z[64], buf[256];
  format_float(v.x, x); format_float(v.y, y); format_float(v.z, z);
  snprintf(buf, sizeof buf, "'%s %s %s'", x, y, z);
  qa_compat_qc_machine_return_int(m, (double)qa_compat_qc_strings_set_engine(&m->strings, "pr_string_temp", buf, -1));
}
/* PF_objerror reports the failing QuakeC function and message; break, coredump and eprint only print diagnostics in the
 * original, so a host without a console ignores them. */
static void b_objerror(void *_ctx, qa_compat_qc_machine *m) {
  (void)_ctx;
  const int64_t current = qa_compat_qc_machine_get_current_function(m);
  const char *name = current >= 0 && (size_t)current < m->program->function_count && m->program->functions[current].name != NULL
      ? m->program->functions[current].name : "?";
  char *message = qa_compat_qc_machine_var_string(m, 0);
  qa_compat_qc_machine_fail(m, "OBJECT ERROR in %s: %s", name, message != NULL ? message : "");
}
static void b_diagnostic(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; (void)m; }
static void b_traceon(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; m->trace_enabled = true; }
static void b_traceoff(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; m->trace_enabled = false; }
static void b_rint(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; double v = qa_compat_qc_machine_arg_float(m, 0); qa_compat_qc_machine_return_float(m, trunc(v > 0 ? v + 0.5 : v - 0.5)); }
static void b_floor(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; qa_compat_qc_machine_return_float(m, floor(qa_compat_qc_machine_arg_float(m, 0))); }
static void b_ceil(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; qa_compat_qc_machine_return_float(m, ceil(qa_compat_qc_machine_arg_float(m, 0))); }
static void b_fabs(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; qa_compat_qc_machine_return_float(m, fabs(qa_compat_qc_machine_arg_float(m, 0))); }
static void b_vectoangles(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx;
  qa_vec3 v = qa_compat_qc_machine_arg_vector(m, 0); double pitch;
  if (v.x == 0 && v.y == 0) pitch = v.z > 0 ? 90 : 270;
  else { double fwd = sqrt(v.x * v.x + v.y * v.y); pitch = trunc(atan2(v.z, fwd) * 180 / 3.141592653589793); if (pitch < 0) pitch += 360; }
  qa_compat_qc_machine_return_vector(m, (qa_vec3){pitch, yaw_of(v), 0});
}
static void b_find(void *ctx, qa_compat_qc_machine *m) {
  State *s = ctx; if (s->s.prepare_entities) s->s.prepare_entities(s->s.ctx);
  int64_t start = qa_compat_qc_entity_memory_slot(m->entities, qa_compat_qc_machine_arg_int(m, 0));
  int64_t field = qa_compat_qc_machine_arg_int(m, 1);
  char *match = strdup(qa_compat_qc_machine_arg_string(m, 2));
  if (!match) qa_range_error("find allocation failed");
  qa_cleanup scope; qa_cleanup_push(&scope, free_cleanup, match);
  const qa_compat_qc_definition *def = NULL;
  for (size_t i = 0; i < m->program->field_count; i++) if (m->program->fields[i].offset == field && strcmp(m->program->fields[i].type, "string") == 0) { def = &m->program->fields[i]; break; }
  if (!def) qa_compat_qc_machine_fail(m, "find requires a source string field");
  int64_t result = 0;
  for (int64_t slot = start + 1; slot < m->entities->count; slot++) {
    if (s->s.is_free_entity(s->s.ctx, slot)) continue;
    int64_t ref = qa_compat_qc_entity_memory_reference(m->entities, slot);
    int32_t text = qa_compat_qc_machine_entity_int(m, ref, def->name);
    if (strcmp(qa_compat_qc_strings_get(&m->strings, text), match) == 0) { result = ref; break; }
  }
  qa_cleanup_pop(&scope); free(match);
  qa_compat_qc_machine_return_int(m, (double)result);
}
static void b_nextent(void *ctx, qa_compat_qc_machine *m) {
  State *s = ctx; if (s->s.prepare_entities) s->s.prepare_entities(s->s.ctx);
  for (int64_t slot = qa_compat_qc_entity_memory_slot(m->entities, qa_compat_qc_machine_arg_int(m, 0)) + 1; slot < m->entities->count; slot++)
    if (!s->s.is_free_entity(s->s.ctx, slot)) { qa_compat_qc_machine_return_int(m, (double)qa_compat_qc_entity_memory_reference(m->entities, slot)); return; }
  qa_compat_qc_machine_return_int(m, 0);
}
static void b_atof(void *_ctx, qa_compat_qc_machine *m) { (void)_ctx; qa_compat_qc_machine_return_float(m, qa_native_atof(qa_compat_qc_machine_arg_string(m, 0))); }
static void b_checkext(void *ctx, qa_compat_qc_machine *m) {
  State *s = ctx; const char *name = qa_compat_qc_machine_arg_string(m, 0); bool has = false;
  if (s->s.has_extensions) for (size_t i = 0; i < s->s.extension_count; i++) if (strcmp(s->s.extensions[i], name) == 0) has = true;
  qa_compat_qc_machine_return_float(m, has ? 1 : 0);
}
static void state_free(void *p) { free(p); }
bool qa_compat_qc_create_builtins(qa_compat_qc_builtin_services services, qa_compat_qc_builtin_registry *out) {
  State *state = malloc(sizeof *state); if (!state) return false; state->s = services;
  out->state = state; out->state_destroy = state_free;
#define SETN(n, fn) do { if (!qa_compat_qc_registry_set_numbered(out, n, fn)) return false; } while (0)
  SETN(1, ((qa_compat_qc_builtin){NULL, b_makevectors}));
  if (services.random) SETN(7, ((qa_compat_qc_builtin){state, b_random}));
  SETN(9, ((qa_compat_qc_builtin){NULL, b_normalize})); SETN(10, ((qa_compat_qc_builtin){NULL, b_error})); SETN(12, ((qa_compat_qc_builtin){NULL, b_vlen})); SETN(13, ((qa_compat_qc_builtin){NULL, b_vectoyaw}));
  SETN(11, ((qa_compat_qc_builtin){NULL, b_objerror}));
  SETN(6, ((qa_compat_qc_builtin){NULL, b_diagnostic})); SETN(28, ((qa_compat_qc_builtin){NULL, b_diagnostic})); SETN(31, ((qa_compat_qc_builtin){NULL, b_diagnostic}));
  SETN(26, ((qa_compat_qc_builtin){NULL, b_ftos})); SETN(27, ((qa_compat_qc_builtin){NULL, b_vtos})); SETN(29, ((qa_compat_qc_builtin){NULL, b_traceon})); SETN(30, ((qa_compat_qc_builtin){NULL, b_traceoff}));
  SETN(36, ((qa_compat_qc_builtin){NULL, b_rint})); SETN(37, ((qa_compat_qc_builtin){NULL, b_floor})); SETN(38, ((qa_compat_qc_builtin){NULL, b_ceil})); SETN(43, ((qa_compat_qc_builtin){NULL, b_fabs})); SETN(51, ((qa_compat_qc_builtin){NULL, b_vectoangles}));
  if (services.is_free_entity) { SETN(18, ((qa_compat_qc_builtin){state, b_find})); SETN(47, ((qa_compat_qc_builtin){state, b_nextent})); }
  if (strcmp(services.kind, "quakeworld") == 0) SETN(81, ((qa_compat_qc_builtin){NULL, b_atof}));
  SETN(99, ((qa_compat_qc_builtin){state, b_checkext}));
#undef SETN
  qa_compat_qc_builtin_requirement reqs[96]; size_t n = qa_compat_qc_host_requirements(services.kind, reqs, 96);
  if (n > 96) n = 96;
  for (size_t i = 0; i < n; i++) {
    qa_compat_qc_builtin fn = {NULL, NULL};
    for (size_t j = 0; j < services.host_count; j++) if (strcmp(services.host[j].name, reqs[i].name) == 0) { fn = services.host[j].fn; break; }
    if (!fn.fn) continue;
    bool ok = reqs[i].has_number ? qa_compat_qc_registry_set_numbered(out, reqs[i].number, fn) : qa_compat_qc_registry_set_named(out, reqs[i].name, fn);
    if (!ok) return false;
  }
  return true;
}
