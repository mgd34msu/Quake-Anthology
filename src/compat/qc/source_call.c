/* Port of src/compat/qc/source-call.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/source_call.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
qa_compat_qc_value_type qa_compat_qc_source_value_type(qa_mod_callback_value v) {
  if (strcmp(v.kind, "input") != 0) return v.kind;
  const char *n = v.name;
  if (!strcmp(n,"self")||!strcmp(n,"other")||!strcmp(n,"activator")||!strcmp(n,"attacker")||!strcmp(n,"inflictor")) return "entity";
  if (!strcmp(n,"point")||!strcmp(n,"direction")||!strcmp(n,"normal")||!strcmp(n,"view-angles")) return "vector";
  if (!strcmp(n,"item")) return "string";
  return "float";
}
void qa_compat_qc_validate_source_call(const qa_compat_qc_program *p, qa_mod_source_call call, const qa_mod_callback_input *avail, size_t avail_n, const char *label) {
  for (int pass = 0; pass < 2; pass++) {
    size_t n = pass ? call.global_count : call.argument_count;
    for (size_t i = 0; i < n; i++) {
      qa_mod_callback_value v = pass ? call.globals[i].value : call.arguments[i];
      if (strcmp(v.kind, "input") == 0) { bool has = false; for (size_t j = 0; j < avail_n; j++) if (strcmp(avail[j], v.name) == 0) has = true;
        if (!has) qa_range_error("Mod %s cannot read %s", label, v.name); }
    }
  }
  const qa_compat_qc_function *fn = qa_compat_qc_program_function_named(p, call.function);
  bool bad = fn->index == 0 || fn->first_statement <= 0 || fn->parameter_size_count != call.argument_count;
  if (!bad) for (size_t i = 0; i < fn->parameter_size_count; i++)
    if (fn->parameter_sizes[i] != (strcmp(qa_compat_qc_source_value_type(call.arguments[i]), "vector") == 0 ? 3 : 1)) bad = true;
  if (bad) qa_range_error("Mod callback %s has an incompatible source signature", call.function);
  for (size_t i = 0; i < call.global_count; i++) {
    bool dup = false; for (size_t j = 0; j < i; j++) if (strcmp(call.globals[j].name, call.globals[i].name) == 0) dup = true;
    const qa_compat_qc_definition *g = qa_compat_qc_program_global_named(p, call.globals[i].name);
    if (dup || !g || strcmp(g->type, qa_compat_qc_source_value_type(call.globals[i].value)) != 0)
      qa_range_error("Mod callback global %s is duplicated or has an incompatible type", call.globals[i].name);
  }
}
void qa_compat_qc_write_source_value(qa_compat_qc_machine *m, qa_compat_qc_words *w, int64_t off, qa_mod_runtime_value v, qa_compat_qc_actor_reference ref) {
  if (strcmp(v.kind,"float")==0) { if (!isfinite((double)qa_float32(v.number_value))) qa_range_error("Mod callback number exceeds binary32 range"); qa_compat_qc_words_set_float(w, off, v.number_value); }
  else if (strcmp(v.kind,"vector")==0) {
    if (!isfinite((double)qa_float32(v.vector_value.x))||!isfinite((double)qa_float32(v.vector_value.y))||!isfinite((double)qa_float32(v.vector_value.z))) qa_range_error("Mod callback vector exceeds binary32 range");
    qa_compat_qc_words_set_vector(w, off, v.vector_value);
  }
  else if (strcmp(v.kind,"string")==0) {
    size_t vn = strlen(v.string_value); char *name = malloc(vn + 11); if (!name) qa_range_error("source value allocation failed");
    memcpy(name, "mod-value:", 10); memcpy(name + 10, v.string_value, vn + 1);
    int64_t cap = (int64_t)vn + 1; if (cap < 128) cap = 128;
    qa_compat_qc_words_set_int(w, off, (double)qa_compat_qc_strings_set_engine(&m->strings, name, v.string_value, cap));
    free(name);
  }
  else if (strcmp(v.kind,"actor")==0) qa_compat_qc_words_set_int(w, off, (double)ref.fn(ref.ctx, v.actor_value));
}
static qa_mod_runtime_value resolve(qa_mod_callback_value v, const qa_compat_qc_mod_input_entry *in, size_t n, const char *fn) {
  if (strcmp(v.kind, "input") != 0) {
    qa_mod_runtime_value r; memset(&r,0,sizeof r); r.kind=v.kind; r.number_value=v.number_value; r.string_value=v.string_value; r.vector_value=v.vector_value; return r;
  }
  for (size_t i=0;i<n;i++) if (strcmp(in[i].name, v.name)==0) return in[i].value;
  qa_range_error("Gameplay callback %s has no %s input", fn, v.name);
}
typedef struct { size_t off; uint8_t bytes[12]; size_t len; } SavedGlobal;
typedef struct {
  qa_compat_qc_machine *m; qa_mod_runtime_value *args, *gvals; const qa_compat_qc_definition **gdefs; SavedGlobal *saved;
  size_t global_count; bool staged; uint8_t staging[108];
} SourceScope;
static void source_finally(void *arg) {
  SourceScope *s = arg;
  if (s->staged) {
    memcpy(s->m->globals.bytes + 4, s->staging, 108);
    for (size_t i = 0; i < s->global_count; i++) memcpy(s->m->globals.bytes + s->saved[i].off, s->saved[i].bytes, s->saved[i].len);
  }
  free(s->args); free(s->gvals); free(s->gdefs); free(s->saved);
}
double qa_compat_qc_with_source_call(qa_compat_qc_machine *m, qa_mod_source_call call, const qa_compat_qc_mod_input_entry *inputs, size_t n, qa_compat_qc_actor_reference ref, qa_compat_qc_source_execute exec) {
  size_t gc = call.global_count;
  SourceScope s; memset(&s, 0, sizeof s); s.m = m; s.global_count = gc;
  s.args = malloc((call.argument_count?call.argument_count:1)*sizeof *s.args);
  s.gvals = malloc((gc?gc:1)*sizeof *s.gvals);
  s.gdefs = malloc((gc?gc:1)*sizeof *s.gdefs);
  s.saved = malloc((gc?gc:1)*sizeof *s.saved);
  if(!s.args||!s.gvals||!s.gdefs||!s.saved) { free(s.args); free(s.gvals); free(s.gdefs); free(s.saved); qa_range_error("source call allocation failed"); }
  qa_cleanup scope; qa_cleanup_push(&scope, source_finally, &s);
  for (size_t i=0;i<call.argument_count;i++) s.args[i]=resolve(call.arguments[i],inputs,n,call.function);
  for (size_t i=0;i<gc;i++){ s.gdefs[i]=qa_compat_qc_program_global_named(m->program,call.globals[i].name); s.gvals[i]=resolve(call.globals[i].value,inputs,n,call.function); }
  memcpy(s.staging, m->globals.bytes+4, 108);
  for (size_t i=0;i<gc;i++){ if(!s.gdefs[i]) qa_range_error("Missing validated callback global");
    s.saved[i].off=(size_t)s.gdefs[i]->offset*4; s.saved[i].len=(strcmp(s.gdefs[i]->type,"vector")==0?3:1)*4; memcpy(s.saved[i].bytes,m->globals.bytes+s.saved[i].off,s.saved[i].len); }
  s.staged = true; /* TS try begins here: earlier throws leave the staging untouched. */
  for (size_t i=0;i<call.argument_count;i++) qa_compat_qc_write_source_value(m,&m->globals,4+(int64_t)i*3,s.args[i],ref);
  for (size_t i=0;i<gc;i++) qa_compat_qc_write_source_value(m,&m->globals,s.gdefs[i]->offset,s.gvals[i],ref);
  double result = exec.fn(exec.ctx, (int64_t)call.argument_count);
  qa_cleanup_pop(&scope);
  source_finally(&s);
  return result;
}
