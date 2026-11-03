/* Port of src/compat/qc/machine.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/machine.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

bool qa_compat_qc_registry_set_numbered(qa_compat_qc_builtin_registry *r, int64_t n, qa_compat_qc_builtin fn) {
  for (size_t i = 0; i < r->numbered_count; i++) if (r->numbered[i].number == n) { r->numbered[i].fn = fn; return true; }
  if (r->numbered_count == r->numbered_capacity) { size_t c = r->numbered_capacity ? r->numbered_capacity * 2 : 16; void *p = realloc(r->numbered, c * sizeof *r->numbered); if (!p) return false; r->numbered = p; r->numbered_capacity = c; }
  r->numbered[r->numbered_count++] = (qa_compat_qc_numbered_builtin){n, fn}; return true;
}
bool qa_compat_qc_registry_set_named(qa_compat_qc_builtin_registry *r, const char *name, qa_compat_qc_builtin fn) {
  for (size_t i = 0; i < r->named_count; i++) if (strcmp(r->named[i].name, name) == 0) { r->named[i].fn = fn; return true; }
  if (r->named_count == r->named_capacity) { size_t c = r->named_capacity ? r->named_capacity * 2 : 8; void *p = realloc(r->named, c * sizeof *r->named); if (!p) return false; r->named = p; r->named_capacity = c; }
  r->named[r->named_count++] = (qa_compat_qc_named_builtin){name, fn}; return true;
}
qa_compat_qc_builtin qa_compat_qc_registry_get_numbered(const qa_compat_qc_builtin_registry *r, int64_t n) {
  if (!r) { return (qa_compat_qc_builtin){NULL, NULL}; } for (size_t i = 0; i < r->numbered_count; i++) { if (r->numbered[i].number == n) return r->numbered[i].fn; } return (qa_compat_qc_builtin){NULL, NULL};
}
qa_compat_qc_builtin qa_compat_qc_registry_get_named(const qa_compat_qc_builtin_registry *r, const char *name) {
  if (!r) { return (qa_compat_qc_builtin){NULL, NULL}; } for (size_t i = 0; i < r->named_count; i++) { if (strcmp(r->named[i].name, name) == 0) return r->named[i].fn; } return (qa_compat_qc_builtin){NULL, NULL};
}
void qa_compat_qc_registry_destroy(qa_compat_qc_builtin_registry *r) { if (r->state_destroy) { r->state_destroy(r->state); } free(r->numbered); free(r->named); memset(r, 0, sizeof *r); }
void qa_compat_qc_machine_snapshot_destroy(qa_compat_qc_machine_snapshot *s) { free(s->globals); free(s->entities); free(s->strings); free(s->profiling); memset(s, 0, sizeof *s); }

noreturn void qa_compat_qc_machine_fail(qa_compat_qc_machine *m, const char *format, ...) {
  char msg[QA_CAUGHT_MESSAGE_CAPACITY]; va_list ap; va_start(ap, format); vsnprintf(msg, sizeof msg, format, ap); va_end(ap);
  qa_raise("QcRuntimeError", 0, 0, "QuakeC %lld:%lld: %s", (long long)m->function_index, (long long)m->statement, msg);
}
static bool in_boundary(qa_compat_qc_machine *m, int64_t idx) { for (size_t i = 0; i < m->boundary_function_count; i++) { if (m->boundary_functions[i] == idx) return true; } return false; }
static const qa_compat_qc_inline_region *region_at(qa_compat_qc_machine *m, int64_t entry) {
  for (size_t i = 0; i < m->inline_region_count; i++) { if (m->inline_regions[i].entry == entry) return &m->inline_regions[i]; } return NULL;
}
static void machine_destroy_cleanup(void *arg) { qa_compat_qc_machine_destroy(arg); }
bool qa_compat_qc_machine_init(qa_compat_qc_machine *m, qa_compat_qc_machine_options o) {
  memset(m, 0, sizeof *m); m->options = o; m->program = o.program; m->entities = o.entities; m->numeric = o.numeric;
  qa_cleanup scope; qa_cleanup_push(&scope, machine_destroy_cleanup, m);
  if (o.entities->layout.field_words != o.program->entity_field_words) qa_compat_qc_program_error("progs.dat", "entity field layout disagrees with program");
  m->globals_bytes = malloc(o.program->initial_globals_length ? o.program->initial_globals_length : 1);
  if (!m->globals_bytes) goto oom;
  memcpy(m->globals_bytes, o.program->initial_globals, o.program->initial_globals_length);
  qa_compat_qc_words_init(&m->globals, m->globals_bytes, o.program->initial_globals_length);
  if (!qa_compat_qc_strings_init(&m->strings, o.program->strings, o.program->string_length, o.program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD)) goto oom;
  m->profiling = calloc(o.program->function_count ? o.program->function_count : 1, sizeof(double));
  if (!m->profiling) goto oom;
  if (o.function_boundary && o.function_boundary->function_count) {
    m->boundary_functions = malloc(o.function_boundary->function_count * sizeof(int64_t)); if (!m->boundary_functions) goto oom;
    memcpy(m->boundary_functions, o.function_boundary->functions, o.function_boundary->function_count * sizeof(int64_t));
    m->boundary_function_count = o.function_boundary->function_count;
    for (size_t i = 0; i < m->boundary_function_count; i++) {
      const qa_compat_qc_function *fn = qa_compat_qc_program_function_at(o.program, m->boundary_functions[i]);
      if (fn->named_builtin || fn->first_statement < 0) qa_compat_qc_program_error("progs.dat", "function boundary requires an interpreted function");
    }
  }
  if (o.inline_boundary) for (size_t ri = 0; ri < o.inline_boundary->region_count; ri++) {
    qa_compat_qc_inline_region region = o.inline_boundary->regions[ri];
    const qa_compat_qc_function *fn = qa_compat_qc_program_function_at(o.program, region.function_index);
    int64_t end = (int64_t)o.program->statement_count;
    for (size_t i = 0; i < o.program->function_count; i++) if (o.program->functions[i].first_statement > fn->first_statement && o.program->functions[i].first_statement < end) end = o.program->functions[i].first_statement;
    if (fn->named_builtin || fn->first_statement <= 0 || region.entry < fn->first_statement || region.exit <= region.entry || region.exit >= end || region_at(m, region.entry))
      qa_compat_qc_program_error("progs.dat", "invalid inline source region");
    if (region.has_standalone) {
      int64_t sw = region.standalone_saved; bool global = region.standalone_scope && strcmp(region.standalone_scope, "global") == 0;
      bool ok;
      if (global) {
        bool is_float = false; for (size_t i = 0; i < o.program->global_count; i++) if (o.program->globals[i].offset == sw && strcmp(o.program->globals[i].type, "float") == 0) is_float = true;
        ok = sw >= 28 && sw * 4 + 4 <= (int64_t)m->globals.byte_length && !(sw >= fn->parameter_start && sw < fn->parameter_start + fn->local_words) && is_float;
      } else ok = sw >= fn->parameter_start && sw < fn->parameter_start + fn->local_words;
      if (!ok) qa_compat_qc_program_error("progs.dat", "invalid standalone inline result");
    }
    qa_compat_qc_inline_region *nr = realloc(m->inline_regions, (m->inline_region_count + 1) * sizeof *nr); if (!nr) goto oom;
    m->inline_regions = nr; m->inline_regions[m->inline_region_count++] = region;
  }
  m->statement_limit = o.statement_limit ? o.statement_limit : 100000;
  m->stack_limit = o.stack_limit ? o.stack_limit : 32;
  m->local_stack_limit = o.local_stack_words ? o.local_stack_words : 2048;
  if (m->statement_limit <= 0 || m->stack_limit <= 0 || m->local_stack_limit <= 0) qa_range_error("Invalid QuakeC execution limits");
  qa_cleanup_pop(&scope);
  return true;
oom: qa_cleanup_pop(&scope); qa_compat_qc_machine_destroy(m); return false;
}
void qa_compat_qc_machine_destroy(qa_compat_qc_machine *m) {
  for (size_t i = 0; i < m->frame_count; i++) free(m->frames[i].locals);
  free(m->frames); free(m->profiling); free(m->globals_bytes); free(m->boundary_functions); free(m->inline_regions);
  for (size_t i = 0; i < m->continuation_count; i++) free(m->continuations[i]);
  free(m->continuations);
  qa_compat_qc_strings_destroy(&m->strings); memset(m, 0, sizeof *m);
}
int64_t qa_compat_qc_machine_get_argc(const qa_compat_qc_machine *m) { return m->argument_count; }
int64_t qa_compat_qc_machine_get_current_statement(const qa_compat_qc_machine *m) { return m->statement; }
int64_t qa_compat_qc_machine_get_current_function(const qa_compat_qc_machine *m) { return m->function_index; }
int64_t qa_compat_qc_machine_get_depth(const qa_compat_qc_machine *m) { return (int64_t)m->frame_count; }
int64_t qa_compat_qc_machine_global_offset(qa_compat_qc_machine *m, const char *name) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_global_named(m->program, name);
  if (!d) { qa_compat_qc_machine_fail(m, "missing global %s", name); } return d->offset;
}
bool qa_compat_qc_machine_global_offset_or_null(const qa_compat_qc_machine *m, const char *name, int64_t *out) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_global_named(m->program, name); if (!d) return false; *out = d->offset; return true;
}
int64_t qa_compat_qc_machine_field_offset(qa_compat_qc_machine *m, const char *name) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(m->program, name);
  if (!d) { qa_compat_qc_machine_fail(m, "missing entity field %s", name); } return d->offset;
}
static qa_compat_qc_words *entity_fields(qa_compat_qc_machine *m, int64_t ref, const char *field, int64_t words) {
  if (m->options.validate_entity_access) m->options.validate_entity_access(m->options.ctx, ref, qa_compat_qc_machine_field_offset(m, field), words, "read");
  return qa_compat_qc_entity_memory_from_reference(m->entities, ref);
}
double qa_compat_qc_machine_entity_float(qa_compat_qc_machine *m, int64_t r, const char *f) { const qa_compat_qc_words * w = entity_fields(m, r, f, 1); int64_t word = qa_compat_qc_machine_field_offset(m, f); return qa_compat_qc_words_float(w, word); }
int32_t qa_compat_qc_machine_entity_int(qa_compat_qc_machine *m, int64_t r, const char *f) { const qa_compat_qc_words * w = entity_fields(m, r, f, 1); int64_t word = qa_compat_qc_machine_field_offset(m, f); return qa_compat_qc_words_int(w, word); }
qa_vec3 qa_compat_qc_machine_entity_vector(qa_compat_qc_machine *m, int64_t r, const char *f) { const qa_compat_qc_words * w = entity_fields(m, r, f, 3); int64_t word = qa_compat_qc_machine_field_offset(m, f); return qa_compat_qc_words_vector(w, word); }
static void store_entity(qa_compat_qc_machine *m, int64_t ref, const char *field, int64_t words, int mode, double num, qa_vec3 vec) {
  int64_t word = qa_compat_qc_machine_field_offset(m, field);
  qa_compat_qc_words *fields = qa_compat_qc_entity_memory_from_reference(m->entities, ref);
  if (m->options.validate_entity_access) m->options.validate_entity_access(m->options.ctx, ref, word, words, "write");
  uint8_t before[12]; bool obs = m->options.observe_entity_store != NULL;
  if (obs) memcpy(before, fields->bytes + word * 4, (size_t)words * 4);
  if (mode == 0) qa_compat_qc_words_set_float(fields, word, num);
  else if (mode == 1) qa_compat_qc_words_set_int(fields, word, num);
  else qa_compat_qc_words_set_vector(fields, word, vec);
  if (obs) m->options.observe_entity_store(m->options.ctx, (qa_compat_qc_entity_store_observation){m->function_index, m->statement, ref, word, before, (size_t)words * 4, fields->bytes + word * 4, (size_t)words * 4});
}
void qa_compat_qc_machine_set_entity_float(qa_compat_qc_machine *m, int64_t r, const char *f, double v) { store_entity(m, r, f, 1, 0, v, (qa_vec3){0,0,0}); }
void qa_compat_qc_machine_set_entity_int(qa_compat_qc_machine *m, int64_t r, const char *f, double v) { store_entity(m, r, f, 1, 1, v, (qa_vec3){0,0,0}); }
void qa_compat_qc_machine_set_entity_vector(qa_compat_qc_machine *m, int64_t r, const char *f, qa_vec3 v) { store_entity(m, r, f, 3, 2, 0, v); }
static int64_t param_off(qa_compat_qc_machine *m, int64_t i) { if (i < 0 || i >= 8) qa_compat_qc_machine_fail(m, "invalid parameter %lld", (long long)i); return 4 + i * 3; }
double qa_compat_qc_machine_arg_float(qa_compat_qc_machine *m, int64_t i) { return qa_compat_qc_words_float(&m->globals, param_off(m, i)); }
int32_t qa_compat_qc_machine_arg_int(qa_compat_qc_machine *m, int64_t i) { return qa_compat_qc_words_int(&m->globals, param_off(m, i)); }
qa_vec3 qa_compat_qc_machine_arg_vector(qa_compat_qc_machine *m, int64_t i) { return qa_compat_qc_words_vector(&m->globals, param_off(m, i)); }
const char *qa_compat_qc_machine_arg_string(qa_compat_qc_machine *m, int64_t i) { return qa_compat_qc_strings_get(&m->strings, qa_compat_qc_machine_arg_int(m, i)); }
void qa_compat_qc_machine_return_float(qa_compat_qc_machine *m, double v) { qa_compat_qc_words_set_float(&m->globals, 1, v); }
void qa_compat_qc_machine_return_int(qa_compat_qc_machine *m, double v) { qa_compat_qc_words_set_int(&m->globals, 1, v); }
void qa_compat_qc_machine_return_vector(qa_compat_qc_machine *m, qa_vec3 v) { qa_compat_qc_words_set_vector(&m->globals, 1, v); }
char *qa_compat_qc_machine_var_string(qa_compat_qc_machine *m, int64_t first) {
  size_t cap = 64, len = 0; char *r = malloc(cap); if (!r) return NULL; r[0] = 0;
  for (int64_t i = first; i < m->argument_count; i++) {
    const char *s = qa_compat_qc_machine_arg_string(m, i); size_t n = strlen(s);
    if (len + n + 1 > cap) { cap = (len + n + 1) * 2; char *g = realloc(r, cap); if (!g) { free(r); return NULL; } r = g; }
    memcpy(r + len, s, n); len += n; r[len] = 0;
  }
  return r;
}
qa_compat_qc_words *qa_compat_qc_machine_self(qa_compat_qc_machine *m) {
  return qa_compat_qc_entity_memory_from_reference(m->entities, qa_compat_qc_words_int(&m->globals, qa_compat_qc_machine_global_offset(m, "self")));
}
size_t qa_compat_qc_machine_missing_builtins(qa_compat_qc_machine *m, qa_compat_qc_missing_builtin *out, size_t cap) {
  size_t n = 0;
  for (size_t i = 0; i < m->program->function_count; i++) { const qa_compat_qc_function *fn = &m->program->functions[i];
    qa_compat_qc_missing_builtin e; bool miss = false;
    if (fn->first_statement < 0 && !qa_compat_qc_registry_get_numbered(m->options.builtins, -fn->first_statement).fn) { e = (qa_compat_qc_missing_builtin){fn->index, fn->name, true, -fn->first_statement}; miss = true; }
    else if (fn->named_builtin && !qa_compat_qc_registry_get_named(m->options.builtins, fn->name).fn) { e = (qa_compat_qc_missing_builtin){fn->index, fn->name, false, 0}; miss = true; }
    if (miss) { if (out && n < cap) out[n] = e; n++; }
  }
  return n;
}
static qa_compat_qc_builtin get_builtin(qa_compat_qc_machine *m, const qa_compat_qc_function *fn) {
  if (!fn->named_builtin && fn->first_statement >= 0) return (qa_compat_qc_builtin){NULL, NULL};
  qa_compat_qc_builtin b = fn->named_builtin ? qa_compat_qc_registry_get_named(m->options.builtins, fn->name) : qa_compat_qc_registry_get_numbered(m->options.builtins, -fn->first_statement);
  if (!b.fn) { if (fn->named_builtin) qa_compat_qc_machine_fail(m, "unbound builtin %s (named)", fn->name); else qa_compat_qc_machine_fail(m, "unbound builtin %s (%lld)", fn->name, (long long)-fn->first_statement); }
  return b;
}
/* try/finally around the builtin: a throw (including a cancellation) passes through here. */
static void builtin_finally(void *arg) { ((qa_compat_qc_machine *)arg)->builtin_depth--; }
static void call_builtin(qa_compat_qc_machine *m, qa_compat_qc_builtin b) {
  qa_cleanup scope; m->builtin_depth++; qa_cleanup_push(&scope, builtin_finally, m);
  b.fn(b.ctx, m);
  qa_cleanup_pop(&scope); m->builtin_depth--;
}
static void enter(qa_compat_qc_machine *m, const qa_compat_qc_function *fn) {
  if ((int64_t)m->frame_count + 1 >= m->stack_limit) qa_compat_qc_machine_fail(m, "stack overflow");
  if (m->local_words + fn->local_words > m->local_stack_limit) qa_compat_qc_machine_fail(m, "locals stack overflow");
  if (m->frame_count == m->frame_capacity) { size_t c = m->frame_capacity ? m->frame_capacity * 2 : 8; void *p = realloc(m->frames, c * sizeof *m->frames); if (!p) qa_compat_qc_machine_fail(m, "stack overflow"); m->frames = p; m->frame_capacity = c; }
  size_t begin = (size_t)fn->parameter_start * 4, ln = (size_t)fn->local_words * 4;
  uint8_t *locals = malloc(ln ? ln : 1); if (!locals) qa_compat_qc_machine_fail(m, "locals stack overflow");
  memcpy(locals, m->globals.bytes + begin, ln);
  m->frames[m->frame_count++] = (qa_compat_qc_frame){m->statement, m->function_index, locals, ln};
  m->local_words += fn->local_words;
  int64_t dest = fn->parameter_start;
  for (size_t i = 0; i < fn->parameter_size_count; i++) { int64_t sz = fn->parameter_sizes[i]; qa_compat_qc_words_copy_words(&m->globals, &m->globals, 4 + (int64_t)i * 3, dest, sz); dest += sz; }
  m->function_index = fn->index; m->statement = fn->first_statement - 1;
}
static void leave(qa_compat_qc_machine *m) {
  const qa_compat_qc_function *fn = qa_compat_qc_program_function_at(m->program, m->function_index);
  if (m->frame_count == 0) qa_compat_qc_machine_fail(m, "stack underflow");
  qa_compat_qc_frame fr = m->frames[--m->frame_count];
  memcpy(m->globals.bytes + (size_t)fn->parameter_start * 4, fr.locals, fr.locals_length); free(fr.locals);
  m->local_words -= fn->local_words; m->function_index = fr.function_index; m->statement = fr.statement;
}
typedef struct { uint8_t words[108]; int64_t argument_count; bool valid; } Staging;
static Staging capture_staging(qa_compat_qc_machine *m) {
  Staging s; memset(&s, 0, sizeof s);
  if (m->options.observe_call || m->boundary_function_count) { memcpy(s.words, m->globals.bytes + 4, 108); s.argument_count = m->argument_count; s.valid = true; }
  return s;
}
static void restore_staging(qa_compat_qc_machine *m, Staging s) { if (s.valid) { memcpy(m->globals.bytes + 4, s.words, 108); m->argument_count = s.argument_count; } }
/* stop_frame is a frame index (TS Frame identity survives realloc), -1 = none. */
static void run_statements(qa_compat_qc_machine *m, int64_t exit_depth, int64_t *budget, const qa_compat_qc_inline_region *stop_region, int64_t stop_frame);
typedef struct { qa_compat_qc_machine *m; int64_t depth; } Unwind;
static void unwind_frames(void *arg) { Unwind *u = arg; while ((int64_t)u->m->frame_count > u->depth) leave(u->m); }
static void run_function(qa_compat_qc_machine *m, const qa_compat_qc_function *fn, int64_t *budget) {
  int64_t exit_depth = (int64_t)m->frame_count;
  qa_compat_qc_builtin b = get_builtin(m, fn);
  if (b.fn) { call_builtin(m, b); return; }
  enter(m, fn);
  Unwind unwind = {m, exit_depth}; qa_cleanup scope; qa_cleanup_push(&scope, unwind_frames, &unwind);
  run_statements(m, exit_depth, budget, NULL, -1);
  qa_cleanup_pop(&scope);
}
/* A TS continuation is a closure: it stays callable, and fails by name, after its boundary
 * returned. Records therefore outlive the boundary and are retained per machine; the oldest
 * inactive record is reused only once QA_CONTINUATION_RETAINED newer ones exist. */
enum { QA_CONTINUATION_RETAINED = 64 };
struct qa_compat_qc_continuation {
  qa_compat_qc_machine *m;
  bool active, called, executing, has_failure, has_completed;
  /* function boundary */
  const qa_compat_qc_function *fn; Staging staging, completed; int64_t cancel_words[3];
  /* inline boundary */
  const qa_compat_qc_inline_region *region; int64_t exit_depth, frame;
  int64_t *budget;
  qa_caught_error failure;
};
typedef struct qa_compat_qc_continuation Cont;
static Cont *continuation_acquire(qa_compat_qc_machine *m) {
  size_t n = m->continuation_count;
  if (n >= QA_CONTINUATION_RETAINED) for (size_t i = 0; i < n; i++) {
    size_t at = (m->continuation_next + i) % n; Cont *c = m->continuations[at];
    if (c->active) continue;
    m->continuation_next = (at + 1) % n;
    c->called = c->executing = c->has_failure = c->has_completed = false; c->active = true;
    return c;
  }
  Cont **grown = realloc(m->continuations, (n + 1) * sizeof *grown);
  Cont *c = calloc(1, sizeof *c);
  if (!grown || !c) { free(c); qa_range_error("QuakeC continuation allocation failed"); }
  m->continuations = grown; grown[n] = c; m->continuation_count = n + 1;
  c->m = m; c->active = true;
  return c;
}
/* Function boundary execution plumbing */
static const char QC_FUNCTION_CANCELLATION[] = "QcFunctionCancellation";
static void set_return_words(qa_compat_qc_machine *m, const int64_t w[3]) {
  for (int i = 0; i < 3; i++) { if (w[i] < -2147483648LL || w[i] > 4294967295LL) qa_compat_qc_machine_fail(m, "replacement return is not a QC word"); qa_compat_qc_words_set_int(&m->globals, 1 + i, (double)(int32_t)(uint32_t)w[i]); }
}
static void capture_completed(Cont *e) {
  qa_compat_qc_machine *m = e->m;
  e->has_completed = true; e->completed.valid = true; memcpy(e->completed.words, m->globals.bytes + 4, 12); e->completed.argument_count = m->argument_count;
}
static void run_source(void *ctx) { Cont *e = ctx; run_function(e->m, e->fn, e->budget); }
typedef struct { Cont *e; const qa_compat_qc_builtin *prepare; const int64_t *words; } Finish;
static void finish_body(void *ctx) {
  Finish *f = ctx; Cont *e = f->e; qa_compat_qc_machine *m = e->m;
  if (!e->active || e->called) qa_compat_qc_machine_fail(m, "function continuation must execute once inside its boundary");
  e->called = true; restore_staging(m, e->staging);
  if (f->words) set_return_words(m, f->words);
  else {
    if (f->prepare && f->prepare->fn) f->prepare->fn(f->prepare->ctx, m);
    e->executing = true;
    qa_caught_error caught;
    bool completed = qa_try(run_source, e, &caught);
    e->executing = false;
    if (!completed) {
      if (caught.name != QC_FUNCTION_CANCELLATION || caught.detail != (long long)(intptr_t)e) qa_rethrow(&caught);
      set_return_words(m, e->cancel_words);
    }
  }
  capture_completed(e);
}
/* A host may swallow a continuation's throw; the boundary rethrows it after run returns. */
static void finish(Cont *e, const qa_compat_qc_builtin *prepare, const int64_t *words) {
  Finish f = {e, prepare, words};
  if (!qa_try(finish_body, &f, &e->failure)) { e->has_failure = true; qa_rethrow(&e->failure); }
}
static void exec_run(void *ctx, const qa_compat_qc_builtin *prepare) { finish(ctx, prepare, NULL); }
static void exec_skip(void *ctx, const int64_t w[3]) { finish(ctx, NULL, w); }
static noreturn void exec_cancel(void *ctx, const int64_t w[3]) {
  Cont *e = ctx;
  if (!e->active || !e->executing) qa_compat_qc_machine_fail(e->m, "function cancellation requires its live source execution");
  memcpy(e->cancel_words, w, sizeof e->cancel_words);
  qa_raise(QC_FUNCTION_CANCELLATION, 0, (long long)(intptr_t)e, "function cancellation");
}
static void boundary_finally(void *arg) {
  Cont *e = arg; qa_compat_qc_machine *m = e->m;
  e->active = false; m->boundary_depth--;
  /* Host confirmation may reenter QC after the callee returned. Preserve its actual return, not guest state. */
  if (e->has_completed) { memcpy(m->globals.bytes + 4, e->completed.words, 12); m->argument_count = e->completed.argument_count; }
}
static void invoke_function(qa_compat_qc_machine *m, const qa_compat_qc_function *fn, qa_compat_qc_call_site call, int64_t *budget, Staging staging) {
  const qa_compat_qc_function_boundary *b = m->options.function_boundary;
  if (!b || !in_boundary(m, fn->index)) { run_function(m, fn, budget); return; }
  Cont *e = continuation_acquire(m);
  e->fn = fn; e->budget = budget; e->staging = staging;
  qa_compat_qc_function_execution exec = {e, exec_run, exec_skip, exec_cancel};
  m->boundary_depth++;
  qa_cleanup scope; qa_cleanup_push(&scope, boundary_finally, e);
  b->run(b->ctx, call, exec);
  if (e->has_failure) qa_rethrow(&e->failure);
  if (!e->called) qa_compat_qc_machine_fail(m, "function boundary omitted source execution");
  qa_cleanup_pop(&scope);
  boundary_finally(e);
}
void qa_compat_qc_machine_execute(qa_compat_qc_machine *m, int64_t function_index, int64_t argument_count) {
  if (argument_count < 0 || argument_count > 8) qa_compat_qc_machine_fail(m, "invalid argument count");
  const qa_compat_qc_function *fn = qa_compat_qc_program_function_at(m->program, function_index);
  m->argument_count = argument_count; m->trace_enabled = false;
  qa_compat_qc_call_site call = {function_index, m->function_index, m->statement};
  Staging staging = capture_staging(m);
  if (m->options.observe_call) m->options.observe_call(m->options.ctx, call);
  restore_staging(m, staging);
  int64_t budget = m->statement_limit;
  invoke_function(m, fn, call, &budget, staging);
}
typedef struct { qa_compat_qc_machine *m; int64_t depth, saved; bool has_prev; int32_t prev; int64_t prev_args; } RegionScope;
static void region_finally(void *arg) {
  RegionScope *r = arg;
  while ((int64_t)r->m->frame_count > r->depth) leave(r->m);
  if (r->has_prev) qa_compat_qc_words_set_int(&r->m->globals, r->saved, r->prev);
  r->m->argument_count = r->prev_args;
}
double qa_compat_qc_machine_execute_region(qa_compat_qc_machine *m, qa_compat_qc_inline_region region, int64_t argument_count) {
  const qa_compat_qc_inline_region *admitted = region_at(m, region.entry);
  const qa_compat_qc_function *fn = qa_compat_qc_program_function_at(m->program, region.function_index);
  if (!admitted || admitted->function_index != region.function_index || admitted->exit != region.exit || !admitted->has_standalone)
    qa_compat_qc_machine_fail(m, "standalone inline execution requires an admitted source region");
  int64_t saved = admitted->standalone_saved; bool global = admitted->standalone_scope && strcmp(admitted->standalone_scope, "global") == 0;
  if (argument_count != (int64_t)fn->parameter_size_count) qa_compat_qc_machine_fail(m, "invalid standalone inline argument count");
  bool has_prev = global; int32_t prev = has_prev ? qa_compat_qc_words_int(&m->globals, saved) : 0;
  int64_t depth = (int64_t)m->frame_count, prev_args = m->argument_count;
  m->argument_count = argument_count; enter(m, fn);
  if (m->frame_count == 0) qa_compat_qc_machine_fail(m, "standalone inline execution has no frame");
  m->statement = region.entry - 1;
  int64_t budget = m->statement_limit;
  RegionScope rs = {m, depth, saved, has_prev, prev, prev_args};
  qa_cleanup scope; qa_cleanup_push(&scope, region_finally, &rs);
  run_statements(m, depth, &budget, admitted, depth);
  double result = qa_compat_qc_words_float(&m->globals, saved);
  qa_cleanup_pop(&scope);
  region_finally(&rs);
  return result;
}
static double byte_compare(const char *l, const char *r) {
  size_t ll = strlen(l), lr = strlen(r), mn = ll < lr ? ll : lr;
  for (size_t i = 0; i <= mn; i++) { int d = (i < ll ? (unsigned char)l[i] : 0) - (i < lr ? (unsigned char)r[i] : 0); if (d) return d; }
  return 0;
}
/* Inline continuation plumbing */
typedef struct { Cont *e; bool skip; } Decide;
static void decide_body(void *ctx) {
  Decide *d = ctx; Cont *e = d->e; qa_compat_qc_machine *m = e->m;
  if (!e->active || e->called) qa_compat_qc_machine_fail(m, "inline continuation must execute once inside its boundary");
  if (m->frame_count == 0 || (int64_t)m->frame_count - 1 != e->frame || m->function_index != e->region->function_index) qa_compat_qc_machine_fail(m, "inline continuation belongs to another source frame");
  if (d->skip && !e->region->replaceable) qa_compat_qc_machine_fail(m, "inline source region is not admitted for replacement");
  e->called = true;
  if (d->skip) m->statement = e->region->exit - 1; else run_statements(m, e->exit_depth, e->budget, e->region, e->frame);
}
static void inline_decide(Cont *e, bool skip) {
  Decide d = {e, skip};
  if (!qa_try(decide_body, &d, &e->failure)) { e->has_failure = true; qa_rethrow(&e->failure); }
}
static void inline_run(void *ctx) { inline_decide(ctx, false); }
static void inline_skip(void *ctx) { inline_decide(ctx, true); }
static void inline_finally(void *arg) { ((Cont *)arg)->active = false; }
static void run_statements(qa_compat_qc_machine *m, int64_t exit_depth, int64_t *budget, const qa_compat_qc_inline_region *stop_region, int64_t stop_frame) {
  const qa_compat_qc_inline_boundary *boundary = m->inline_region_count ? m->options.inline_boundary : NULL;
  while ((int64_t)m->frame_count > exit_depth) {
    if (stop_region && m->frame_count && (int64_t)m->frame_count - 1 == stop_frame) {
      if (m->statement + 1 == stop_region->exit) return;
      if (m->statement + 1 < stop_region->entry || m->statement + 1 > stop_region->exit) qa_compat_qc_machine_fail(m, "inline source region escaped its continuation");
    }
    m->statement++;
    if (m->statement < 0 || m->statement >= (int64_t)m->program->statement_count) qa_compat_qc_machine_fail(m, "statement outside program");
    qa_compat_qc_statement st = m->program->statements[m->statement];
    const qa_compat_qc_inline_region *region = boundary ? region_at(m, m->statement) : NULL;
    if (boundary && region && region->function_index == m->function_index && !(stop_region && stop_frame == (int64_t)m->frame_count - 1 && stop_region->entry == m->statement)) {
      if (m->frame_count == 0) qa_compat_qc_machine_fail(m, "inline source region has no frame");
      Cont *e = continuation_acquire(m);
      e->region = region; e->exit_depth = exit_depth; e->budget = budget; e->frame = (int64_t)m->frame_count - 1;
      m->statement--;
      qa_compat_qc_inline_continuation cont = {e, inline_run, inline_skip};
      qa_cleanup scope; qa_cleanup_push(&scope, inline_finally, e);
      boundary->run(boundary->ctx, *region, cont);
      if (e->has_failure) qa_rethrow(&e->failure);
      if (!e->called) qa_compat_qc_machine_fail(m, "inline boundary omitted source execution");
      qa_cleanup_pop(&scope);
      e->active = false;
      continue;
    }
    if (--*budget == 0) qa_compat_qc_machine_fail(m, "runaway loop error");
    if (m->function_index >= 0 && m->function_index < (int64_t)m->program->function_count) m->profiling[m->function_index] += 1;
    if (m->trace_enabled && m->options.trace) m->options.trace(m->options.ctx, m);
    qa_compat_qc_words *g = &m->globals; const qa_numeric_operations *n = m->numeric; void *nc = n->ctx;
    int64_t a = st.a, b = st.b, c = st.c;
    switch (st.opcode) {
      case QA_QC_OP_ADD_F: qa_compat_qc_words_set_float(g, c, n->add(nc, qa_compat_qc_words_float(g,a), qa_compat_qc_words_float(g,b))); break;
      case QA_QC_OP_SUB_F: qa_compat_qc_words_set_float(g, c, n->subtract(nc, qa_compat_qc_words_float(g,a), qa_compat_qc_words_float(g,b))); break;
      case QA_QC_OP_MUL_F: qa_compat_qc_words_set_float(g, c, n->multiply(nc, qa_compat_qc_words_float(g,a), qa_compat_qc_words_float(g,b))); break;
      case QA_QC_OP_DIV_F: qa_compat_qc_words_set_float(g, c, n->divide(nc, qa_compat_qc_words_float(g,a), qa_compat_qc_words_float(g,b))); break;
      case QA_QC_OP_ADD_V: for (int i=0;i<3;i++) qa_compat_qc_words_set_float(g,c+i,n->add(nc,qa_compat_qc_words_float(g,a+i),qa_compat_qc_words_float(g,b+i))); break;
      case QA_QC_OP_SUB_V: for (int i=0;i<3;i++) qa_compat_qc_words_set_float(g,c+i,n->subtract(nc,qa_compat_qc_words_float(g,a+i),qa_compat_qc_words_float(g,b+i))); break;
      case QA_QC_OP_MUL_V: qa_compat_qc_words_set_float(g,c,n->add(nc,n->add(nc,n->multiply(nc,qa_compat_qc_words_float(g,a),qa_compat_qc_words_float(g,b)),n->multiply(nc,qa_compat_qc_words_float(g,a+1),qa_compat_qc_words_float(g,b+1))),n->multiply(nc,qa_compat_qc_words_float(g,a+2),qa_compat_qc_words_float(g,b+2)))); break;
      case QA_QC_OP_MUL_FV: for (int i=0;i<3;i++) qa_compat_qc_words_set_float(g,c+i,n->multiply(nc,qa_compat_qc_words_float(g,a),qa_compat_qc_words_float(g,b+i))); break;
      case QA_QC_OP_MUL_VF: for (int i=0;i<3;i++) qa_compat_qc_words_set_float(g,c+i,n->multiply(nc,qa_compat_qc_words_float(g,b),qa_compat_qc_words_float(g,a+i))); break;
      case QA_QC_OP_EQ_F: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)==qa_compat_qc_words_float(g,b)?1:0); break;
      case QA_QC_OP_NE_F: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)!=qa_compat_qc_words_float(g,b)?1:0); break;
      case QA_QC_OP_EQ_V: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)==qa_compat_qc_words_float(g,b)&&qa_compat_qc_words_float(g,a+1)==qa_compat_qc_words_float(g,b+1)&&qa_compat_qc_words_float(g,a+2)==qa_compat_qc_words_float(g,b+2)?1:0); break;
      case QA_QC_OP_NE_V: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)!=qa_compat_qc_words_float(g,b)||qa_compat_qc_words_float(g,a+1)!=qa_compat_qc_words_float(g,b+1)||qa_compat_qc_words_float(g,a+2)!=qa_compat_qc_words_float(g,b+2)?1:0); break;
      case QA_QC_OP_EQ_S: { const char *l=qa_compat_qc_strings_get(&m->strings,qa_compat_qc_words_int(g,a)); const char *r=qa_compat_qc_strings_get(&m->strings,qa_compat_qc_words_int(g,b)); qa_compat_qc_words_set_float(g,c,strcmp(l,r)==0?1:0); break; }
      case QA_QC_OP_NE_S: { const char *l=qa_compat_qc_strings_get(&m->strings,qa_compat_qc_words_int(g,a)); const char *r=qa_compat_qc_strings_get(&m->strings,qa_compat_qc_words_int(g,b)); qa_compat_qc_words_set_float(g,c,byte_compare(l,r)); break; }
      case QA_QC_OP_EQ_E: case QA_QC_OP_EQ_FN: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_int(g,a)==qa_compat_qc_words_int(g,b)?1:0); break;
      case QA_QC_OP_NE_E: case QA_QC_OP_NE_FN: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_int(g,a)!=qa_compat_qc_words_int(g,b)?1:0); break;
      case QA_QC_OP_LE: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)<=qa_compat_qc_words_float(g,b)?1:0); break;
      case QA_QC_OP_GE: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)>=qa_compat_qc_words_float(g,b)?1:0); break;
      case QA_QC_OP_LT: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)<qa_compat_qc_words_float(g,b)?1:0); break;
      case QA_QC_OP_GT: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)>qa_compat_qc_words_float(g,b)?1:0); break;
      case QA_QC_OP_NOT_F: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)==0?1:0); break;
      case QA_QC_OP_NOT_V: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)==0&&qa_compat_qc_words_float(g,a+1)==0&&qa_compat_qc_words_float(g,a+2)==0?1:0); break;
      case QA_QC_OP_NOT_S: { int32_t v=qa_compat_qc_words_int(g,a); qa_compat_qc_words_set_float(g,c,v==0||strcmp(qa_compat_qc_strings_get(&m->strings,v),"")==0?1:0); break; }
      case QA_QC_OP_NOT_ENT: qa_compat_qc_words_set_float(g,c,qa_compat_qc_entity_memory_slot(m->entities,qa_compat_qc_words_int(g,a))==0?1:0); break;
      case QA_QC_OP_NOT_FN: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_int(g,a)==0?1:0); break;
      case QA_QC_OP_AND: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)!=0&&qa_compat_qc_words_float(g,b)!=0?1:0); break;
      case QA_QC_OP_OR: qa_compat_qc_words_set_float(g,c,qa_compat_qc_words_float(g,a)!=0||qa_compat_qc_words_float(g,b)!=0?1:0); break;
      case QA_QC_OP_BITAND: qa_compat_qc_words_set_float(g,c,(double)((int32_t)n->to_int32(nc,qa_compat_qc_words_float(g,a)) & (int32_t)n->to_int32(nc,qa_compat_qc_words_float(g,b)))); break;
      case QA_QC_OP_BITOR: qa_compat_qc_words_set_float(g,c,(double)((int32_t)n->to_int32(nc,qa_compat_qc_words_float(g,a)) | (int32_t)n->to_int32(nc,qa_compat_qc_words_float(g,b)))); break;
      case QA_QC_OP_STORE_F: case QA_QC_OP_STORE_S: case QA_QC_OP_STORE_ENT: case QA_QC_OP_STORE_FLD: case QA_QC_OP_STORE_FN: qa_compat_qc_words_copy_words(g,g,a,b,1); break;
      case QA_QC_OP_STORE_V: qa_compat_qc_words_copy_words(g,g,a,b,3); break;
      case QA_QC_OP_LOAD_F: case QA_QC_OP_LOAD_S: case QA_QC_OP_LOAD_ENT: case QA_QC_OP_LOAD_FLD: case QA_QC_OP_LOAD_FN:
        if (m->options.validate_entity_access) m->options.validate_entity_access(m->options.ctx,qa_compat_qc_words_int(g,a),qa_compat_qc_words_int(g,b),1,"read");
        qa_compat_qc_words_copy_words(g,qa_compat_qc_entity_memory_from_reference(m->entities,qa_compat_qc_words_int(g,a)),qa_compat_qc_words_int(g,b),c,1); break;
      case QA_QC_OP_LOAD_V:
        if (m->options.validate_entity_access) m->options.validate_entity_access(m->options.ctx,qa_compat_qc_words_int(g,a),qa_compat_qc_words_int(g,b),3,"read");
        qa_compat_qc_words_copy_words(g,qa_compat_qc_entity_memory_from_reference(m->entities,qa_compat_qc_words_int(g,a)),qa_compat_qc_words_int(g,b),c,3); break;
      case QA_QC_OP_ADDRESS:
        if (qa_compat_qc_entity_memory_slot(m->entities,qa_compat_qc_words_int(g,a))==0 && (!m->options.server_active || m->options.server_active(m->options.ctx))) qa_compat_qc_machine_fail(m,"assignment to world entity");
        qa_compat_qc_words_set_int(g,c,(double)qa_compat_qc_entity_memory_pointer(m->entities,qa_compat_qc_words_int(g,a),qa_compat_qc_words_int(g,b))); break;
      case QA_QC_OP_STOREP_F: case QA_QC_OP_STOREP_S: case QA_QC_OP_STOREP_ENT: case QA_QC_OP_STOREP_FLD: case QA_QC_OP_STOREP_FN: case QA_QC_OP_STOREP_V: {
        int64_t words = st.opcode==QA_QC_OP_STOREP_V?3:1;
        qa_compat_qc_resolved_pointer dest = qa_compat_qc_entity_memory_resolve_pointer(m->entities,qa_compat_qc_words_int(g,b),words);
        int64_t ref = qa_compat_qc_words_int(g,b) - m->entities->layout.variables_offset_bytes - dest.word*4;
        if (m->options.validate_entity_access) m->options.validate_entity_access(m->options.ctx,ref,dest.word,words,"write");
        uint8_t before[12]; bool obs=m->options.observe_entity_store!=NULL; if(obs) memcpy(before,dest.fields->bytes+dest.word*4,(size_t)words*4);
        qa_compat_qc_words_copy_words(dest.fields,g,a,dest.word,words);
        if(obs) m->options.observe_entity_store(m->options.ctx,(qa_compat_qc_entity_store_observation){m->function_index,m->statement,ref,dest.word,before,(size_t)words*4,dest.fields->bytes+dest.word*4,(size_t)words*4});
        break; }
      case QA_QC_OP_IF: if (qa_compat_qc_words_int(g,a)!=0) m->statement += qa_compat_qc_signed_branch(b)-1; break;
      case QA_QC_OP_IFNOT: if (qa_compat_qc_words_int(g,a)==0) m->statement += qa_compat_qc_signed_branch(b)-1; break;
      case QA_QC_OP_GOTO: m->statement += qa_compat_qc_signed_branch(a)-1; break;
      case QA_QC_OP_CALL0: case QA_QC_OP_CALL1: case QA_QC_OP_CALL2: case QA_QC_OP_CALL3: case QA_QC_OP_CALL4:
      case QA_QC_OP_CALL5: case QA_QC_OP_CALL6: case QA_QC_OP_CALL7: case QA_QC_OP_CALL8: {
        m->argument_count = st.opcode - QA_QC_OP_CALL0;
        const qa_compat_qc_function *called = qa_compat_qc_program_function_at(m->program, qa_compat_qc_words_int(g,a));
        qa_compat_qc_call_site call = {called->index, m->function_index, m->statement};
        Staging staging = capture_staging(m);
        if (m->options.observe_call) m->options.observe_call(m->options.ctx, call);
        restore_staging(m, staging);
        if (in_boundary(m, called->index)) invoke_function(m, called, call, budget, staging);
        else { qa_compat_qc_builtin cb = get_builtin(m, called); if (!cb.fn) enter(m, called); else call_builtin(m, cb); }
        break; }
      case QA_QC_OP_STATE: {
        qa_compat_qc_words *self = qa_compat_qc_machine_self(m);
        if (m->options.validate_entity_access) {
          int64_t reference = qa_compat_qc_words_int(g, qa_compat_qc_machine_global_offset(m, "self"));
          struct { const char *name; double v; int is_int; } fs[3] = {{"nextthink", n->add(nc, qa_compat_qc_words_float(g, qa_compat_qc_machine_global_offset(m, "time")), 0.1), 0}, {"frame", qa_compat_qc_words_float(g, a), 0}, {"think", qa_compat_qc_words_int(g, b), 1}};
          for (int i = 0; i < 3; i++) {
            int64_t word = qa_compat_qc_machine_field_offset(m, fs[i].name);
            m->options.validate_entity_access(m->options.ctx, reference, word, 1, "write");
            uint8_t before[4]; bool obs = m->options.observe_entity_store != NULL; if (obs) memcpy(before, self->bytes + word * 4, 4);
            if (fs[i].is_int) qa_compat_qc_words_set_int(self, word, fs[i].v); else qa_compat_qc_words_set_float(self, word, fs[i].v);
            if (obs) m->options.observe_entity_store(m->options.ctx, (qa_compat_qc_entity_store_observation){m->function_index, m->statement, reference, word, before, 4, self->bytes + word * 4, 4});
          }
          break;
        }
        int64_t nextthink = qa_compat_qc_machine_field_offset(m, "nextthink");
        qa_compat_qc_words_set_float(self, nextthink, n->add(nc, qa_compat_qc_words_float(g, qa_compat_qc_machine_global_offset(m, "time")), 0.1));
        qa_compat_qc_words_set_float(self, qa_compat_qc_machine_field_offset(m, "frame"), qa_compat_qc_words_float(g, a));
        qa_compat_qc_words_set_int(self, qa_compat_qc_machine_field_offset(m, "think"), qa_compat_qc_words_int(g, b));
        break; }
      case QA_QC_OP_DONE: case QA_QC_OP_RETURN:
        if (stop_region && m->frame_count && (int64_t)m->frame_count-1==stop_frame) qa_compat_qc_machine_fail(m,"inline source region returned before its continuation");
        qa_compat_qc_words_copy_return_words(g,g,a,1,3); leave(m); break;
      default: qa_compat_qc_machine_fail(m,"unsupported opcode %d",(int)st.opcode);
    }
  }
}
bool qa_compat_qc_machine_get_snapshot(qa_compat_qc_machine *m, qa_compat_qc_machine_snapshot *out) {
  memset(out,0,sizeof *out);
  if (m->frame_count || m->builtin_depth || m->boundary_depth) qa_compat_qc_machine_fail(m,"save requires an idle callback boundary");
  out->globals_length=m->globals.byte_length; out->globals=malloc(out->globals_length?out->globals_length:1); if(!out->globals) return false; memcpy(out->globals,m->globals.bytes,out->globals_length);
  out->entities_length=m->entities->byte_length; out->entities=malloc(out->entities_length?out->entities_length:1); if(!out->entities){qa_compat_qc_machine_snapshot_destroy(out);return false;} memcpy(out->entities,m->entities->bytes,out->entities_length);
  out->strings=qa_compat_qc_strings_snapshot(&m->strings,&out->strings_length); if(!out->strings){qa_compat_qc_machine_snapshot_destroy(out);return false;}
  out->profiling_count=m->program->function_count; out->profiling=malloc((out->profiling_count?out->profiling_count:1)*sizeof(double)); if(!out->profiling){qa_compat_qc_machine_snapshot_destroy(out);return false;}
  memcpy(out->profiling,m->profiling,out->profiling_count*sizeof(double));
  out->entity_count=m->entities->count; out->statement=m->statement; out->function_index=m->function_index; out->argument_count=m->argument_count; out->trace_enabled=m->trace_enabled;
  return true;
}
void qa_compat_qc_machine_validate_snapshot(qa_compat_qc_machine *m, const qa_compat_qc_machine_snapshot *s) {
  if (m->frame_count || m->builtin_depth || m->boundary_depth) qa_compat_qc_machine_fail(m,"restore requires an idle callback boundary");
  if (s->globals_length!=m->globals.byte_length || s->profiling_count!=m->program->function_count || s->function_index!=0) qa_compat_qc_machine_fail(m,"incompatible machine checkpoint");
  if (s->entities_length!=m->entities->byte_length || s->entity_count<1 || s->entity_count>m->entities->capacity) qa_compat_qc_machine_fail(m,"incompatible machine checkpoint");
}
bool qa_compat_qc_machine_restore(qa_compat_qc_machine *m, const qa_compat_qc_machine_snapshot *s) {
  qa_compat_qc_machine_validate_snapshot(m, s);
  if (!qa_compat_qc_strings_restore(&m->strings,s->strings,s->strings_length)) return false;
  qa_compat_qc_entity_memory_restore(m->entities,s->entities,s->entities_length,s->entity_count);
  memcpy(m->globals.bytes,s->globals,s->globals_length);
  m->statement=s->statement; m->function_index=0; m->argument_count=s->argument_count; m->trace_enabled=s->trace_enabled;
  for (size_t i=0;i<s->profiling_count;i++) m->profiling[i]=s->profiling[i];
  return true;
}

