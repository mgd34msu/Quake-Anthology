/* Port of src/compat/qc/save.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/save.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/cvars/numbers.h"

static char *xd(const char *s) { size_t n = strlen(s) + 1; char *r = malloc(n); if (r) memcpy(r, s, n); return r; }
void qa_compat_qc_text_pairs_free(qa_compat_qc_text_pairs *p) {
  for (size_t i = 0; i < p->count; i++) { free(p->items[i].key); free(p->items[i].value); }
  free(p->items); p->items = NULL; p->count = 0;
}
static bool push(qa_compat_qc_text_pairs *p, const char *k, const char *v) {
  qa_compat_qc_text_pair *n = realloc(p->items, (p->count + 1) * sizeof *n); if (!n) return false; p->items = n;
  p->items[p->count].key = xd(k); p->items[p->count].value = xd(v);
  if (!p->items[p->count].key || !p->items[p->count].value) return false;
  p->count++; return true;
}
static char *new_string(const char *text) {
  size_t n = strlen(text); char *r = malloc(n + 1); if (!r) return NULL; size_t j = 0;
  for (size_t i = 0; i < n; i++) {
    if (text[i] == '\\') { i++; r[j++] = (i < n && text[i] == 'n') ? '\n' : '\\'; }
    else r[j++] = text[i];
  }
  r[j] = 0; return r;
}
void qa_compat_qc_parse_value(qa_compat_qc_machine *m, qa_compat_qc_words *w, const qa_compat_qc_definition *d, const char *text) {
  int64_t o = d->offset; const char *t = d->type;
  if (strcmp(t, "string") == 0) { char *s = new_string(text); qa_compat_qc_words_set_int(w, o, (double)qa_compat_qc_strings_allocate(&m->strings, s ? s : "")); free(s); }
  else if (strcmp(t, "float") == 0) qa_compat_qc_words_set_float(w, o, qa_quake_atof(text));
  else if (strcmp(t, "vector") == 0) {
    const char *start = text;
    for (int c = 0; c < 3; c++) { const char *sp = strchr(start, ' '); size_t len = sp ? (size_t)(sp - start) : strlen(start);
      char buf[128]; if (len >= sizeof buf) len = sizeof buf - 1; memcpy(buf, start, len); buf[len] = 0;
      qa_compat_qc_words_set_float(w, o + c, qa_quake_atof(buf)); start = sp ? sp + 1 : start + strlen(start); }
  }
  else if (strcmp(t, "entity") == 0) qa_compat_qc_words_set_int(w, o, (double)qa_compat_qc_entity_memory_reference(m->entities, (int64_t)trunc(qa_quake_atof(text))));
  else if (strcmp(t, "function") == 0) qa_compat_qc_words_set_int(w, o, (double)qa_compat_qc_program_function_named(m->program, text)->index);
  else if (strcmp(t, "field") == 0) {
    const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(m->program, text);
    if (!f) qa_compat_qc_machine_fail(m, "cannot find field %s", text);
    qa_compat_qc_words_set_int(w, o, qa_compat_qc_words_int(&m->globals, f->offset));
  }
  else if (strcmp(t, "void") == 0) { /* nothing */ }
  else qa_compat_qc_machine_fail(m, "text parsing is unavailable for QC type %lld (%s); restore raw state instead", (long long)d->native_type, d->name);
}
bool qa_compat_qc_apply_entity_pairs(qa_compat_qc_machine *m, int64_t slot, const qa_compat_qc_text_pair *pairs, size_t count, qa_compat_qc_entity_parse_result_compat *out) {
  memset(out, 0, sizeof *out); out->empty = count == 0;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(m->entities, slot);
  if (slot != 0) memset(w->bytes, 0, w->byte_length);
  for (size_t i = 0; i < count; i++) {
    bool angle = strcmp(pairs[i].key, "angle") == 0;
    char *key = xd(angle ? "angles" : strcmp(pairs[i].key, "light") == 0 ? "light_lev" : pairs[i].key);
    if (!key) return false;
    if (m->program->api.kind == QA_QUAKEC_API_Q1_NETQUAKE) { size_t n = strlen(key); while (n && key[n - 1] == ' ') key[--n] = 0; }
    if (key[0] == '_') { free(key); continue; }
    if (!push(&out->retained, key, pairs[i].value)) { free(key); return false; }
    const qa_compat_qc_definition *def = qa_compat_qc_program_field_named(m->program, key);
    if (!def) { if (!push(&out->unknown, key, pairs[i].value)) { free(key); return false; } free(key); continue; }
    if (angle) { char buf[256]; snprintf(buf, sizeof buf, "0 %s 0", pairs[i].value); qa_compat_qc_parse_value(m, w, def, buf); }
    else qa_compat_qc_parse_value(m, w, def, pairs[i].value);
    free(key);
  }
  return true;
}
bool qa_compat_qc_apply_global_pairs(qa_compat_qc_machine *m, const qa_compat_qc_text_pair *pairs, size_t count, qa_compat_qc_text_pairs *out) {
  memset(out, 0, sizeof *out);
  for (size_t i = 0; i < count; i++) {
    const qa_compat_qc_definition *def = qa_compat_qc_program_global_named(m->program, pairs[i].key);
    if (!def) { if (!push(out, pairs[i].key, pairs[i].value)) return false; }
    else qa_compat_qc_parse_value(m, &m->globals, def, pairs[i].value);
  }
  return true;
}
static char *saved_value(qa_compat_qc_machine *m, qa_compat_qc_words *w, const qa_compat_qc_definition *d) {
  int64_t o = d->offset; const char *t = d->type;
  if (strcmp(t, "string") == 0) return xd(qa_compat_qc_strings_get(&m->strings, qa_compat_qc_words_int(w, o)));
  if (strcmp(t, "float") == 0) return qa_cvar_value_text(qa_compat_qc_words_float(w, o), false);
  if (strcmp(t, "vector") == 0) {
    char *a = qa_cvar_value_text(qa_compat_qc_words_float(w, o), false), *b = qa_cvar_value_text(qa_compat_qc_words_float(w, o + 1), false), *c = qa_cvar_value_text(qa_compat_qc_words_float(w, o + 2), false);
    if (!a || !b || !c) { free(a); free(b); free(c); return NULL; }
    size_t n = strlen(a) + strlen(b) + strlen(c) + 3; char *r = malloc(n); if (r) snprintf(r, n, "%s %s %s", a, b, c); free(a); free(b); free(c); return r;
  }
  if (strcmp(t, "entity") == 0) { char buf[32]; snprintf(buf, sizeof buf, "%lld", (long long)qa_compat_qc_entity_memory_slot(m->entities, qa_compat_qc_words_int(w, o))); return xd(buf); }
  if (strcmp(t, "function") == 0) return xd(qa_compat_qc_program_function_at(m->program, qa_compat_qc_words_int(w, o))->name);
  if (strcmp(t, "field") == 0) {
    for (size_t i = 0; i < m->program->field_count; i++) if (m->program->fields[i].offset == qa_compat_qc_words_int(w, o)) return xd(m->program->fields[i].name);
    qa_compat_qc_machine_fail(m, "cannot save field offset %d", qa_compat_qc_words_int(w, o));
  }
  if (strcmp(t, "void") == 0) return xd("void");
  qa_compat_qc_machine_fail(m, "text saving is unavailable for QC type %lld (%s); save raw state instead", (long long)d->native_type, d->name);
}
bool qa_compat_qc_save_global_pairs(qa_compat_qc_machine *m, qa_compat_qc_text_pairs *out) {
  memset(out, 0, sizeof *out);
  for (size_t i = 0; i < m->program->global_count; i++) { const qa_compat_qc_definition *d = &m->program->globals[i];
    if (!d->save) continue;
    if (strcmp(d->type, "string") && strcmp(d->type, "float") && strcmp(d->type, "entity")) continue;
    char *v = saved_value(m, &m->globals, d); if (!v) return false;
    bool ok = push(out, d->name, v); free(v); if (!ok) return false;
  }
  return true;
}
bool qa_compat_qc_save_entity_pairs(qa_compat_qc_machine *m, int64_t slot, bool is_free, qa_compat_qc_text_pairs *out) {
  memset(out, 0, sizeof *out);
  if (is_free) return true;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(m->entities, slot);
  for (size_t i = 1; i < m->program->field_count; i++) { const qa_compat_qc_definition *d = &m->program->fields[i];
    size_t nl = strlen(d->name); if (nl >= 2 && d->name[nl - 2] == '_') continue;
    int64_t cnt = strcmp(d->type, "vector") == 0 ? 3 : 1; bool nz = false;
    for (int64_t c = 0; c < cnt; c++) if (qa_compat_qc_words_int(w, d->offset + c) != 0) nz = true;
    if (!nz) continue;
    char *v = saved_value(m, w, d); if (!v) return false;
    bool ok = push(out, d->name, v); free(v); if (!ok) return false;
  }
  return true;
}
