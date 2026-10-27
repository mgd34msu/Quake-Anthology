/* Port of src/compat/q2/classic/cvars.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/cvars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"

typedef struct entry { char *key; qa_guest_address address; } entry_t;
typedef struct guest_cvar { char *name; qa_guest_address address; } guest_cvar_t;
struct qa_compat_q2_classic_cvars {
  qa_compat_q2_memory *memory; qa_cvar_registry *registry;
  entry_t *strings; size_t string_count, string_cap;
  guest_cvar_t *records; size_t record_count, record_cap;
};
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
qa_compat_q2_classic_cvars *qa_compat_q2_classic_cvars_create(qa_compat_q2_memory *memory, qa_cvar_registry *registry) {
  qa_compat_q2_classic_cvars *c = calloc(1, sizeof *c); if (!c) return NULL;
  c->memory = memory; c->registry = registry; return c;
}
void qa_compat_q2_classic_cvars_destroy(qa_compat_q2_classic_cvars *c) {
  if (!c) return;
  for (size_t i = 0; i < c->string_count; i++) free(c->strings[i].key);
  for (size_t i = 0; i < c->record_count; i++) free(c->records[i].name);
  free(c->strings); free(c->records); free(c);
}
qa_compat_q2_memory *qa_compat_q2_classic_cvars_get_memory(const qa_compat_q2_classic_cvars *c) { return c->memory; }
qa_cvar_registry *qa_compat_q2_classic_cvars_get_registry(const qa_compat_q2_classic_cvars *c) { return c->registry; }
qa_guest_address qa_compat_q2_classic_cvars_string(qa_compat_q2_classic_cvars *c, const char *value) {
  for (size_t i = 0; i < c->string_count; i++) if (strcmp(c->strings[i].key, value) == 0) return c->strings[i].address;
  qa_guest_address a = qa_compat_q2_allocate_classic_string(c->memory, value);
  if (c->string_count == c->string_cap) { c->string_cap = c->string_cap ? c->string_cap * 2 : 16; c->strings = realloc(c->strings, c->string_cap * sizeof *c->strings); }
  c->strings[c->string_count++] = (entry_t){xstrdup(value), a};
  return a;
}
static guest_cvar_t *update(qa_compat_q2_classic_cvars *c, const qa_cvar_snapshot *state) {
  guest_cvar_t *rec = NULL;
  for (size_t i = 0; i < c->record_count; i++) if (strcmp(c->records[i].name, state->name) == 0) rec = &c->records[i];
  if (!rec) {
    if (c->record_count == c->record_cap) { c->record_cap = c->record_cap ? c->record_cap * 2 : 16; c->records = realloc(c->records, c->record_cap * sizeof *c->records); }
    rec = &c->records[c->record_count++];
    char label[512]; snprintf(label, sizeof label, "API 3 cvar %s", state->name);
    rec->name = xstrdup(state->name);
    rec->address = c->memory->allocate(c->memory->ctx, 28, false, 0, label);
  }
  qa_guest_address base = rec->address;
  qa_compat_q2_mem_write_pointer(c->memory, base, true, qa_compat_q2_classic_cvars_string(c, state->name));
  qa_compat_q2_mem_write_pointer(c->memory, qa_compat_q2_mem_offset(c->memory, base, 4), true, qa_compat_q2_classic_cvars_string(c, state->value));
  if (state->latched_value) qa_compat_q2_mem_write_pointer(c->memory, qa_compat_q2_mem_offset(c->memory, base, 8), true, qa_compat_q2_classic_cvars_string(c, state->latched_value));
  else qa_compat_q2_mem_write_pointer(c->memory, qa_compat_q2_mem_offset(c->memory, base, 8), false, base);
  qa_compat_q2_mem_write_i32(c->memory, qa_compat_q2_mem_offset(c->memory, base, 12), state->flags);
  qa_compat_q2_mem_write_i32(c->memory, qa_compat_q2_mem_offset(c->memory, base, 16), state->modified ? 1 : 0);
  qa_compat_q2_mem_write_f32(c->memory, qa_compat_q2_mem_offset(c->memory, base, 20), state->numeric_value);
  return rec;
}
bool qa_compat_q2_classic_cvars_pointer(qa_compat_q2_classic_cvars *c, const char *name, qa_guest_address *out) {
  qa_cvar_snapshot state; memset(&state, 0, sizeof state);
  if (!qa_cvar_registry_find(c->registry, name, &state)) return false;
  char *canon = state.name;
  qa_compat_q2_classic_cvars_refresh(c);
  bool found = false;
  for (size_t i = 0; i < c->record_count; i++) if (strcmp(c->records[i].name, canon) == 0) { if (out) *out = c->records[i].address; found = true; }
  qa_cvar_read_free(&state);
  return found;
}
void qa_compat_q2_classic_cvars_refresh(qa_compat_q2_classic_cvars *c) {
  size_t n = 0; qa_cvar_snapshot *snaps = qa_cvar_registry_snapshots(c->registry, 0, &n);
  qa_guest_address *order = malloc((n ? n : 1) * sizeof *order);
  for (size_t i = 0; i < n; i++) order[i] = update(c, &snaps[i])->address;
  for (size_t i = 0; i < n; i++) {
    qa_guest_address next = qa_compat_q2_mem_offset(c->memory, order[i], 24);
    if (i + 1 < n) qa_compat_q2_mem_write_pointer(c->memory, next, true, order[i + 1]);
    else qa_compat_q2_mem_write_pointer(c->memory, next, false, order[i]);
  }
  for (size_t i = 0; i < n; i++) qa_cvar_read_free(&snaps[i]);
  free(snaps); free(order);
}
