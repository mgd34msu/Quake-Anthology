/* Port of src/content/q3/base/game/save-callbacks.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/save_callbacks.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
void qa_q3_callback_family_init(qa_q3_callback_family *f) { f->entries = NULL; f->count = 0; f->cap = 0; }
void qa_q3_callback_family_destroy(qa_q3_callback_family *f) {
  for (size_t i = 0; i < f->count; i++) free(f->entries[i].id);
  free(f->entries); f->entries = NULL; f->count = f->cap = 0;
}
static int same_cb(qa_q3_entity_callback a, qa_q3_entity_callback b) { return a.fn == b.fn && a.ctx == b.ctx; }
static char *dupstr(const char *v) { size_t n = strlen(v); char *d = malloc(n + 1); if (d) memcpy(d, v, n + 1); return d; }
qa_q3_entity_callback qa_q3_callback_family_register(qa_q3_callback_family *f, const char *id, qa_q3_entity_callback cb) {
  if (!id || !id[0]) qa_range_error("Q3 callback identity is empty");
  for (size_t i = 0; i < f->count; i++) {
    if (strcmp(f->entries[i].id, id) == 0 && !same_cb(f->entries[i].callback, cb))
      qa_range_error("Duplicate Q3 callback identity %s", id);
    if (same_cb(f->entries[i].callback, cb) && strcmp(f->entries[i].id, id) != 0)
      qa_range_error("Q3 callback has two identities: %s, %s", f->entries[i].id, id);
  }
  for (size_t i = 0; i < f->count; i++) if (strcmp(f->entries[i].id, id) == 0) return cb;
  if (f->count == f->cap) { size_t cap = f->cap ? f->cap * 2 : 8; void *n = realloc(f->entries, cap * sizeof *f->entries); if (!n) qa_range_error("out of memory"); f->entries = n; f->cap = cap; }
  f->entries[f->count].id = dupstr(id); f->entries[f->count].callback = cb; f->count++; return cb;
}
qa_q3_entity_callback qa_q3_callback_family_intern(qa_q3_callback_family *f, const char *id, qa_q3_entity_callback cb) {
  for (size_t i = 0; i < f->count; i++) if (strcmp(f->entries[i].id, id) == 0) return f->entries[i].callback;
  return qa_q3_callback_family_register(f, id, cb);
}
const char *qa_q3_callback_family_capture(qa_q3_callback_family *f, qa_q3_entity_callback cb) {
  if (!cb.fn) return NULL;
  for (size_t i = 0; i < f->count; i++) if (same_cb(f->entries[i].callback, cb)) return f->entries[i].id;
  qa_range_error("Unregistered native Q3 callback cannot be saved");
}
qa_q3_entity_callback qa_q3_callback_family_resolve(qa_q3_callback_family *f, const char *id) {
  qa_q3_entity_callback none = {NULL, NULL}; if (!id) return none;
  for (size_t i = 0; i < f->count; i++) if (strcmp(f->entries[i].id, id) == 0) return f->entries[i].callback;
  qa_range_error("Unknown native Q3 callback %s", id);
}
void qa_q3_callback_catalog_init(qa_q3_callback_catalog *c) {
  c->retained = NULL; c->retained_count = c->retained_cap = 0;
  qa_q3_callback_family_init(&c->think); qa_q3_callback_family_init(&c->reached); qa_q3_callback_family_init(&c->blocked);
  qa_q3_callback_family_init(&c->touch); qa_q3_callback_family_init(&c->use); qa_q3_callback_family_init(&c->pain); qa_q3_callback_family_init(&c->die);
}
void qa_q3_callback_catalog_retain(qa_q3_callback_catalog *c, void *owned) {
  if (c->retained_count == c->retained_cap) {
    size_t cap = c->retained_cap ? c->retained_cap * 2 : 8;
    void **grown = realloc(c->retained, cap * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    c->retained = grown; c->retained_cap = cap;
  }
  c->retained[c->retained_count++] = owned;
}
void qa_q3_callback_catalog_destroy(qa_q3_callback_catalog *c) {
  for (size_t i = 0; i < c->retained_count; i++) free(c->retained[i]);
  free(c->retained); c->retained = NULL; c->retained_count = c->retained_cap = 0;
  qa_q3_callback_family_destroy(&c->think); qa_q3_callback_family_destroy(&c->reached); qa_q3_callback_family_destroy(&c->blocked);
  qa_q3_callback_family_destroy(&c->touch); qa_q3_callback_family_destroy(&c->use); qa_q3_callback_family_destroy(&c->pain); qa_q3_callback_family_destroy(&c->die);
}
