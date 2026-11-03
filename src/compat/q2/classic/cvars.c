/* Port of src/compat/q2/classic/cvars.ts: native cvar_t is a stable guest
 * view of the engine's existing registry. The value and latched strings are
 * ReusableGuestString cells (qa/compat/q2/guest_strings.h). TS compares registry
 * snapshot objects by identity to skip unchanged cvars; C snapshots are
 * copies, so an unchanged snapshot is one whose fields are equal.
 * GPL-2.0-or-later. */
#include "qa/compat/q2/classic/cvars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/guest_strings.h"

typedef struct entry { char *key; qa_guest_address address; } entry_t;
typedef struct guest_cvar {
  char *name; qa_guest_address address;
  qa_compat_q2_reusable_guest_string text, latched_text;
  bool applied; qa_cvar_snapshot snapshot; /* the last snapshot written (TS #applied) */
} guest_cvar_t;
struct qa_compat_q2_classic_cvars {
  qa_compat_q2_memory *memory; qa_cvar_registry *registry;
  entry_t *strings; size_t string_count, string_cap;
  guest_cvar_t **records; size_t record_count, record_cap;
  guest_cvar_t **chain; size_t chain_count, chain_cap;
};
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, s, n); return p; }
static void *grow(void *items, size_t *cap, size_t count, size_t size) {
  if (count < *cap) return items;
  size_t next = *cap == 0 ? 16 : *cap * 2; void *g = realloc(items, next * size); if (g == NULL) qa_range_error("Out of memory");
  *cap = next; return g;
}
qa_compat_q2_classic_cvars *qa_compat_q2_classic_cvars_create(qa_compat_q2_memory *memory, qa_cvar_registry *registry) {
  qa_compat_q2_classic_cvars *c = calloc(1, sizeof *c); if (!c) return NULL;
  c->memory = memory; c->registry = registry; return c;
}
static void snapshot_free(guest_cvar_t *record) { if (record->applied) qa_cvar_read_free(&record->snapshot); record->applied = false; }
void qa_compat_q2_classic_cvars_destroy(qa_compat_q2_classic_cvars *c) {
  if (!c) return;
  for (size_t i = 0; i < c->string_count; i++) free(c->strings[i].key);
  for (size_t i = 0; i < c->record_count; i++) {
    guest_cvar_t *r = c->records[i];
    free(r->name); qa_compat_q2_reusable_guest_string_destroy(&r->text); qa_compat_q2_reusable_guest_string_destroy(&r->latched_text); snapshot_free(r); free(r);
  }
  free(c->strings); free(c->records); free(c->chain); free(c);
}
qa_compat_q2_memory *qa_compat_q2_classic_cvars_get_memory(const qa_compat_q2_classic_cvars *c) { return c->memory; }
qa_cvar_registry *qa_compat_q2_classic_cvars_get_registry(const qa_compat_q2_classic_cvars *c) { return c->registry; }
qa_guest_address qa_compat_q2_classic_cvars_string(qa_compat_q2_classic_cvars *c, const char *value) {
  for (size_t i = 0; i < c->string_count; i++) if (strcmp(c->strings[i].key, value) == 0) return c->strings[i].address;
  qa_guest_address a = qa_compat_q2_allocate_classic_string(c->memory, value);
  c->strings = grow(c->strings, &c->string_cap, c->string_count, sizeof *c->strings);
  c->strings[c->string_count++] = (entry_t){xstrdup(value), a};
  return a;
}
static char *label_of(const char *name, const char *suffix) {
  size_t n = strlen(name) + strlen(suffix) + 16; char *label = malloc(n); if (label == NULL) qa_range_error("Out of memory");
  snprintf(label, n, "API 3 cvar %s%s", name, suffix); return label;
}
static guest_cvar_t *find_record(qa_compat_q2_classic_cvars *c, const char *name) {
  for (size_t i = 0; i < c->record_count; i++) if (strcmp(c->records[i]->name, name) == 0) return c->records[i];
  return NULL;
}
static guest_cvar_t *update(qa_compat_q2_classic_cvars *c, const qa_cvar_snapshot *state) {
  guest_cvar_t *rec = find_record(c, state->name);
  if (rec == NULL) {
    char *label = label_of(state->name, "");
    qa_guest_address address = c->memory->allocate(c->memory->ctx, 28, false, 0, label); free(label);
    rec = calloc(1, sizeof *rec); if (rec == NULL) qa_range_error("Out of memory");
    rec->name = xstrdup(state->name); rec->address = address;
    char *value_label = label_of(state->name, " value"), *latched_label = label_of(state->name, " latched");
    qa_compat_q2_reusable_guest_string_init(&rec->text, c->memory, value_label); qa_compat_q2_reusable_guest_string_init(&rec->latched_text, c->memory, latched_label);
    free(value_label); free(latched_label);
    c->records = grow(c->records, &c->record_cap, c->record_count, sizeof *c->records); c->records[c->record_count++] = rec;
  }
  qa_compat_q2_memory *m = c->memory; qa_guest_address base = rec->address;
  qa_compat_q2_mem_write_pointer(m, base, true, qa_compat_q2_classic_cvars_string(c, state->name));
  /* Private cvars (rcon passwords) exist for the guest but never reveal their contents. */
  bool hidden = (state->flags & QA_Q2_CVAR_FLAG_PRIVATE) != 0;
  qa_guest_address text = qa_compat_q2_reusable_guest_string_set_text(&rec->text, hidden ? "" : state->value);
  qa_compat_q2_mem_write_pointer(m, qa_compat_q2_mem_offset(m, base, 4), true, text);
  if (state->latched_value == NULL || hidden) qa_compat_q2_mem_write_pointer(m, qa_compat_q2_mem_offset(m, base, 8), false, base);
  else { qa_guest_address latched = qa_compat_q2_reusable_guest_string_set_text(&rec->latched_text, state->latched_value); qa_compat_q2_mem_write_pointer(m, qa_compat_q2_mem_offset(m, base, 8), true, latched); }
  qa_compat_q2_mem_write_i32(m, qa_compat_q2_mem_offset(m, base, 12), state->flags);
  qa_compat_q2_mem_write_i32(m, qa_compat_q2_mem_offset(m, base, 16), state->modified ? 1 : 0);
  qa_compat_q2_mem_write_f32(m, qa_compat_q2_mem_offset(m, base, 20), hidden ? 0 : state->numeric_value);
  return rec;
}
bool qa_compat_q2_classic_cvars_pointer(qa_compat_q2_classic_cvars *c, const char *name, qa_guest_address *out) {
  qa_cvar_snapshot state; memset(&state, 0, sizeof state);
  if (!qa_cvar_registry_find(c->registry, name, &state)) return false;
  qa_compat_q2_classic_cvars_refresh(c);
  guest_cvar_t *rec = find_record(c, state.name);
  qa_cvar_read_free(&state);
  if (rec == NULL) return false;
  if (out) *out = rec->address;
  return true;
}
static bool text_equal(const char *a, const char *b) { return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0); }
static bool same_snapshot(const qa_cvar_snapshot *a, const qa_cvar_snapshot *b) {
  return text_equal(a->name, b->name) && text_equal(a->value, b->value) && text_equal(a->reset_value, b->reset_value) && text_equal(a->latched_value, b->latched_value)
      && a->flags == b->flags && a->modified == b->modified && a->modification_count == b->modification_count && a->numeric_value == b->numeric_value
      && a->integer_value == b->integer_value;
}
/* Only cvars whose snapshot changed are rewritten; the chain is relinked when membership or order changes. */
void qa_compat_q2_classic_cvars_refresh(qa_compat_q2_classic_cvars *c) {
  size_t n = 0; qa_cvar_snapshot *states = qa_cvar_registry_snapshots(c->registry, 0, &n);
  bool relink = n != c->chain_count;
  while (c->chain_cap < n) c->chain = grow(c->chain, &c->chain_cap, c->chain_cap, sizeof *c->chain);
  for (size_t i = 0; i < n; i++) {
    guest_cvar_t *record = find_record(c, states[i].name);
    if (record == NULL || !record->applied || !same_snapshot(&record->snapshot, &states[i])) {
      record = update(c, &states[i]);
      snapshot_free(record); record->snapshot = states[i]; record->applied = true; memset(&states[i], 0, sizeof states[i]);
    }
    if (i >= c->chain_count || c->chain[i] != record) { c->chain[i] = record; relink = true; }
  }
  c->chain_count = n;
  if (relink) for (size_t i = 0; i < n; i++) {
    qa_guest_address next = qa_compat_q2_mem_offset(c->memory, c->chain[i]->address, 24);
    if (i + 1 < n) qa_compat_q2_mem_write_pointer(c->memory, next, true, c->chain[i + 1]->address);
    else qa_compat_q2_mem_write_pointer(c->memory, next, false, next);
  }
  for (size_t i = 0; i < n; i++) qa_cvar_read_free(&states[i]);
  free(states);
}
