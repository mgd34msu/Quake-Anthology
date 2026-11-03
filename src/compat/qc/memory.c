/* Port of src/compat/qc/memory.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/memory.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qc/program.h"
#include "qa/core/binary/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *r = malloc(n); if (r) memcpy(r, s, n); return r; }
void qa_compat_qc_words_init(qa_compat_qc_words *w, uint8_t *bytes, size_t n) {
  if (n % 4) { qa_range_error("QC storage must contain whole words"); } w->bytes = bytes; w->byte_length = n;
}
int64_t qa_compat_qc_words_length(const qa_compat_qc_words *w) { return (int64_t)(w->byte_length / 4); }
static size_t woff(const qa_compat_qc_words *w, int64_t word) {
  if (word < 0 || word >= qa_compat_qc_words_length(w)) qa_compat_qc_program_error("progs.dat", "word %lld outside %lld-word storage", (long long)word, (long long)qa_compat_qc_words_length(w));
  return (size_t)word * 4;
}
int32_t qa_compat_qc_words_int(const qa_compat_qc_words *w, int64_t word) { int32_t v; memcpy(&v, w->bytes + woff(w, word), 4); return v; }
double qa_compat_qc_words_float(const qa_compat_qc_words *w, int64_t word) { float v; memcpy(&v, w->bytes + woff(w, word), 4); return v; }
void qa_compat_qc_words_set_int(qa_compat_qc_words *w, int64_t word, double value) { int32_t v = qa_int32(value); memcpy(w->bytes + woff(w, word), &v, 4); }
/* DataView.setFloat32 stores every NaN as the canonical 0x7fc00000 (the oracle engine purifies
 * NaN); x86 arithmetic would otherwise leave the sign-set default NaN in the word. */
void qa_compat_qc_words_set_float(qa_compat_qc_words *w, int64_t word, double value) {
  size_t at = woff(w, word);
  if (isnan(value)) { static const uint32_t canonical = 0x7fc00000u; memcpy(w->bytes + at, &canonical, 4); return; }
  float v = qa_float32(value); memcpy(w->bytes + at, &v, 4);
}
qa_vec3 qa_compat_qc_words_vector(const qa_compat_qc_words *w, int64_t word) {
  return (qa_vec3){qa_compat_qc_words_float(w, word), qa_compat_qc_words_float(w, word + 1), qa_compat_qc_words_float(w, word + 2)};
}
void qa_compat_qc_words_set_vector(qa_compat_qc_words *w, int64_t word, qa_vec3 v) {
  qa_compat_qc_words_set_float(w, word, v.x); qa_compat_qc_words_set_float(w, word + 1, v.y); qa_compat_qc_words_set_float(w, word + 2, v.z);
}
void qa_compat_qc_words_copy_words(qa_compat_qc_words *d, const qa_compat_qc_words *f, int64_t s, int64_t dst, int64_t n) {
  for (int64_t i = 0; i < n; i++) qa_compat_qc_words_set_int(d, dst + i, qa_compat_qc_words_int(f, s + i));
}
/* OP_RETURN copies three words from any operand; id's globals continue past the table, so absent words read as zero. */
void qa_compat_qc_words_copy_return_words(qa_compat_qc_words *d, const qa_compat_qc_words *f, int64_t s, int64_t dst, int64_t n) {
  for (int64_t i = 0; i < n; i++) qa_compat_qc_words_set_int(d, dst + i, s + i < qa_compat_qc_words_length(f) ? qa_compat_qc_words_int(f, s + i) : 0);
}

static void check_slot(qa_compat_qc_entity_memory *m, int64_t slot) {
  if (slot < 0 || slot >= m->count) qa_compat_qc_program_error("progs.dat", "invalid entity slot %lld", (long long)slot);
}
bool qa_compat_qc_entity_memory_init(qa_compat_qc_entity_memory *m, qa_compat_qc_entity_layout l, int64_t cap, int64_t count) {
  memset(m, 0, sizeof *m);
  if (cap < 1 || count < 1 || count > cap || l.stride_bytes <= 0 || l.stride_bytes % 4 || l.variables_offset_bytes < 0 || l.variables_offset_bytes % 4
      || l.field_words <= 0 || l.variables_offset_bytes + l.field_words * 4 > l.stride_bytes || cap * l.stride_bytes > 0x7fffffff)
    qa_range_error("Invalid QC entity layout/capacity");
  m->layout = l; m->capacity = cap; m->count = count; m->byte_length = (size_t)(cap * l.stride_bytes);
  m->bytes = calloc(1, m->byte_length); m->fields = calloc((size_t)cap, sizeof *m->fields); m->fields_ready = calloc((size_t)cap, sizeof *m->fields_ready);
  if (!m->bytes || !m->fields || !m->fields_ready) { qa_compat_qc_entity_memory_destroy(m); return false; }
  return true;
}
void qa_compat_qc_entity_memory_destroy(qa_compat_qc_entity_memory *m) { free(m->bytes); free(m->fields); free(m->fields_ready); memset(m, 0, sizeof *m); }
int64_t qa_compat_qc_entity_memory_get_count(const qa_compat_qc_entity_memory *m) { return m->count; }
void qa_compat_qc_entity_memory_set_count(qa_compat_qc_entity_memory *m, int64_t c) {
  if (c < 1 || c > m->capacity) { qa_range_error("Invalid QC entity count"); } m->count = c;
}
int64_t qa_compat_qc_entity_memory_reference(qa_compat_qc_entity_memory *m, int64_t slot) { check_slot(m, slot); return slot * m->layout.stride_bytes; }
int64_t qa_compat_qc_entity_memory_slot(qa_compat_qc_entity_memory *m, int64_t ref) {
  if (ref < 0 || ref % m->layout.stride_bytes) qa_compat_qc_program_error("progs.dat", "invalid entity reference %lld", (long long)ref);
  int64_t slot = ref / m->layout.stride_bytes; check_slot(m, slot); return slot;
}
qa_compat_qc_words *qa_compat_qc_entity_memory_at(qa_compat_qc_entity_memory *m, int64_t slot) {
  check_slot(m, slot);
  if (!m->fields_ready[slot]) {
    size_t off = (size_t)(slot * m->layout.stride_bytes + m->layout.variables_offset_bytes);
    qa_compat_qc_words_init(&m->fields[slot], m->bytes + off, (size_t)m->layout.field_words * 4); m->fields_ready[slot] = true;
  }
  return &m->fields[slot];
}
qa_compat_qc_words *qa_compat_qc_entity_memory_from_reference(qa_compat_qc_entity_memory *m, int64_t r) { return qa_compat_qc_entity_memory_at(m, qa_compat_qc_entity_memory_slot(m, r)); }
int64_t qa_compat_qc_entity_memory_pointer(qa_compat_qc_entity_memory *m, int64_t ref, int64_t field) {
  qa_compat_qc_entity_memory_slot(m, ref);
  if (field < 0 || field >= m->layout.field_words) qa_compat_qc_program_error("progs.dat", "invalid field offset %lld", (long long)field);
  return ref + m->layout.variables_offset_bytes + field * 4;
}
qa_compat_qc_resolved_pointer qa_compat_qc_entity_memory_resolve_pointer(qa_compat_qc_entity_memory *m, int64_t ptr, int64_t words) {
  if (ptr < 0 || ptr % 4) qa_compat_qc_program_error("progs.dat", "invalid entity pointer %lld", (long long)ptr);
  int64_t slot = ptr / m->layout.stride_bytes, word = (ptr % m->layout.stride_bytes - m->layout.variables_offset_bytes) / 4;
  if (word < 0 || word + words > m->layout.field_words) qa_compat_qc_program_error("progs.dat", "pointer %lld does not address entity variables", (long long)ptr);
  return (qa_compat_qc_resolved_pointer){qa_compat_qc_entity_memory_at(m, slot), word};
}
void qa_compat_qc_entity_memory_restore(qa_compat_qc_entity_memory *m, const uint8_t *b, size_t n, int64_t count) {
  if (n != m->byte_length) qa_compat_qc_program_error("progs.dat", "entity checkpoint capacity mismatch");
  qa_compat_qc_entity_memory_set_count(m, count); memcpy(m->bytes, b, n);
}

/* QcStrings */
bool qa_compat_qc_strings_init(qa_compat_qc_strings *s, const uint8_t *prog, size_t n, bool qw) {
  memset(s, 0, sizeof *s); s->quakeworld = qw; s->arena_length = n > 4096 ? n : 4096;
  /* new Uint8Array: zero-filled. Reserved engine buffers reach snapshots whole. */
  s->arena = calloc(s->arena_length, 1); if (!s->arena) return false;
  memcpy(s->arena, prog, n); s->used = n; return true;
}
void qa_compat_qc_strings_destroy(qa_compat_qc_strings *s) {
  for (size_t i = 0; i < s->engine_count; i++) free(s->engines[i].name);
  free(s->engines); free(s->arena);
  for (size_t i = 0; i < QA_COMPAT_QC_STRING_RING; i++) free(s->get_ring[i]);
  for (size_t i = 0; i < s->interned_capacity; i++) free(s->interned[i].text);
  free(s->interned);
  memset(s, 0, sizeof *s);
}
static int64_t reserve(qa_compat_qc_strings *s, size_t len) {
  size_t off = s->used, req = off + len;
  if (req > 0x7fffffff) qa_compat_qc_program_error("progs.dat", "string arena overflow");
  if (req > s->arena_length) {
    size_t nl = req > s->arena_length * 2 ? req : s->arena_length * 2;
    uint8_t *g = realloc(s->arena, nl); if (!g) qa_range_error("string arena allocation failed");
    memset(g + s->arena_length, 0, nl - s->arena_length); /* grown Uint8Array is zero-filled */
    s->arena = g; s->arena_length = nl;
  }
  s->used = req; return (int64_t)off;
}
static void encode(qa_compat_qc_strings *s, int64_t off, int64_t cap, const char *text) {
  size_t n = strlen(text);
  if (n >= (size_t)cap) qa_compat_qc_program_error("progs.dat", "engine string exceeds its buffer");
  for (size_t i = 0; i < n; i++) { unsigned char b = (unsigned char)text[i]; if (!b) qa_compat_qc_program_error("progs.dat", "QC strings require non-null byte characters"); s->arena[off + (int64_t)i] = b; }
  s->arena[off + (int64_t)n] = 0;
}
int64_t qa_compat_qc_strings_allocate(qa_compat_qc_strings *s, const char *text) {
  int64_t off = reserve(s, strlen(text) + 1); encode(s, off, (int64_t)strlen(text) + 1, text); return off;
}
/* Map<string, number>: open addressing, keys owned. */
static uint64_t text_hash(const char *text) { uint64_t h = 1469598103934665603ull; for (; *text; text++) h = (h ^ (uint8_t)*text) * 1099511628211ull; return h; }
static void interned_clear(qa_compat_qc_strings *s) {
  for (size_t i = 0; i < s->interned_capacity; i++) { free(s->interned[i].text); s->interned[i].text = NULL; }
  s->interned_count = 0;
}
int64_t qa_compat_qc_strings_intern(qa_compat_qc_strings *s, const char *text) {
  if (s->interned_capacity) {
    for (size_t at = text_hash(text) & (s->interned_capacity - 1);; at = (at + 1) & (s->interned_capacity - 1)) {
      if (!s->interned[at].text) break;
      if (strcmp(s->interned[at].text, text) == 0) return s->interned[at].offset;
    }
  }
  int64_t offset = qa_compat_qc_strings_allocate(s, text);
  if ((s->interned_count + 1) * 2 > s->interned_capacity) {
    size_t capacity = s->interned_capacity ? s->interned_capacity * 2 : 64;
    qa_compat_qc_interned_string *grown = calloc(capacity, sizeof *grown);
    if (!grown) qa_range_error("string allocation failed");
    for (size_t i = 0; i < s->interned_capacity; i++) if (s->interned[i].text) {
      size_t at = text_hash(s->interned[i].text) & (capacity - 1);
      while (grown[at].text) at = (at + 1) & (capacity - 1);
      grown[at] = s->interned[i];
    }
    free(s->interned); s->interned = grown; s->interned_capacity = capacity;
  }
  char *key = xstrdup(text); if (!key) qa_range_error("string allocation failed");
  size_t at = text_hash(text) & (s->interned_capacity - 1);
  while (s->interned[at].text) at = (at + 1) & (s->interned_capacity - 1);
  s->interned[at] = (qa_compat_qc_interned_string){key, offset}; s->interned_count++;
  return offset;
}
int64_t qa_compat_qc_strings_set_engine(qa_compat_qc_strings *s, const char *name, const char *text, int64_t capacity) {
  if (capacity < 0) capacity = 128;
  size_t idx = s->engine_count; bool found = false;
  for (size_t i = 0; i < s->engine_count; i++) if (strcmp(s->engines[i].name, name) == 0) { idx = i; found = true; break; }
  if (found && strlen(text) >= (size_t)s->engines[idx].capacity) {
    /* A longer value moves to a larger buffer; the old bytes stay valid for aliases taken earlier. */
    int64_t grown = (int64_t)strlen(text) + 1, doubled = s->engines[idx].capacity * 2;
    s->engines[idx].capacity = grown > doubled ? grown : doubled;
    s->engines[idx].offset = reserve(s, (size_t)s->engines[idx].capacity);
  }
  if (!found) {
    if (capacity < 1) qa_range_error("Invalid engine string capacity");
    if (s->quakeworld && s->engine_count >= 1023) qa_compat_qc_program_error("progs.dat", "MAX_PRSTR");
    if (s->engine_count == s->engine_capacity) { s->engine_capacity = s->engine_capacity ? s->engine_capacity * 2 : 8; s->engines = realloc(s->engines, s->engine_capacity * sizeof *s->engines); if (!s->engines) qa_range_error("engine string allocation failed"); }
    char *dup = xstrdup(name); if (!dup) qa_range_error("engine string allocation failed");
    s->engines[s->engine_count] = (qa_compat_qc_engine_string){dup, reserve(s, (size_t)capacity), capacity};
    idx = s->engine_count++;
  }
  encode(s, s->engines[idx].offset, s->engines[idx].capacity, text);
  return s->quakeworld ? -(int64_t)idx - 1 : s->engines[idx].offset;
}
const char *qa_compat_qc_strings_get(qa_compat_qc_strings *s, int64_t ref) {
  int64_t off = ref;
  if (ref < 0) {
    if (!s->quakeworld || -ref - 1 >= (int64_t)s->engine_count) qa_compat_qc_program_error("progs.dat", "invalid engine string %lld", (long long)ref);
    off = s->engines[-ref - 1].offset;
  }
  if (off < 0 || (uint64_t)off >= s->used) qa_compat_qc_program_error("progs.dat", "invalid string offset %lld", (long long)off);
  const uint8_t *end = memchr(s->arena + off, 0, s->used - (size_t)off);
  if (!end) qa_compat_qc_program_error("progs.dat", "unterminated string at %lld", (long long)off);
  size_t need = (size_t)(end - (s->arena + off)) + 1, at = s->get_ring_next;
  if (s->get_ring_capacity[at] < need) {
    char *grown = realloc(s->get_ring[at], need);
    if (!grown) qa_range_error("string allocation failed");
    s->get_ring[at] = grown; s->get_ring_capacity[at] = need;
  }
  memcpy(s->get_ring[at], s->arena + off, need);
  s->get_ring_next = (at + 1) % QA_COMPAT_QC_STRING_RING;
  return s->get_ring[at];
}
uint8_t *qa_compat_qc_strings_snapshot(qa_compat_qc_strings *s, size_t *out_len) {
  size_t total = 16 + s->used; for (size_t i = 0; i < s->engine_count; i++) total += 12 + strlen(s->engines[i].name);
  qa_binary_writer w; if (!qa_binary_writer_init(&w, (int64_t)total)) return NULL;
  qa_binary_writer_u32(&w, (int64_t)0x31534351); qa_binary_writer_u32(&w, s->quakeworld ? 1 : 0);
  qa_binary_writer_u32(&w, (int64_t)s->used); qa_binary_writer_bytes(&w, s->arena, s->used);
  qa_binary_writer_u32(&w, (int64_t)s->engine_count);
  for (size_t i = 0; i < s->engine_count; i++) {
    qa_binary_writer_u32(&w, s->engines[i].offset); qa_binary_writer_u32(&w, s->engines[i].capacity);
    size_t n = strlen(s->engines[i].name); qa_binary_writer_u32(&w, (int64_t)n);
    for (size_t j = 0; j < n; j++) qa_binary_writer_u8(&w, (unsigned char)s->engines[i].name[j]);
  }
  uint8_t *r = qa_binary_writer_finish(&w, out_len); qa_binary_writer_destroy(&w); return r;
}
typedef struct { qa_binary_reader reader; uint8_t *arena; qa_compat_qc_engine_string *entries; uint32_t count; } RestoreScope;
static void restore_scope_free(void *arg) {
  RestoreScope *r = arg;
  for (uint32_t i = 0; i < r->count; i++) free(r->entries[i].name);
  free(r->entries); free(r->arena); qa_binary_reader_destroy(&r->reader);
}
bool qa_compat_qc_strings_restore(qa_compat_qc_strings *s, const uint8_t *bytes, size_t length) {
  RestoreScope r; memset(&r, 0, sizeof r);
  if (!qa_binary_reader_init(&r.reader, (uint8_t *)bytes, length, "QC strings checkpoint")) return false;
  qa_cleanup scope; qa_cleanup_push(&scope, restore_scope_free, &r);
  if (qa_binary_reader_u32(&r.reader) != 0x31534351u || qa_binary_reader_u32(&r.reader) != (s->quakeworld ? 1u : 0u)) qa_compat_qc_program_error("progs.dat", "incompatible string checkpoint");
  uint32_t arena_len = qa_binary_reader_u32(&r.reader);
  r.arena = qa_binary_reader_bytes(&r.reader, arena_len);
  uint32_t count = qa_binary_reader_u32(&r.reader);
  size_t capacity = 0;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t off = qa_binary_reader_u32(&r.reader), cap = qa_binary_reader_u32(&r.reader);
    char *name = qa_binary_reader_fixed_byte_string(&r.reader, qa_binary_reader_u32(&r.reader));
    if (r.count == capacity) {
      capacity = capacity ? capacity * 2 : 8;
      qa_compat_qc_engine_string *grown = realloc(r.entries, capacity * sizeof *grown);
      if (!grown) { free(name); qa_range_error("string allocation failed"); }
      r.entries = grown;
    }
    r.entries[r.count++] = (qa_compat_qc_engine_string){name, off, cap};
    if (cap < 1 || (int64_t)off + cap > arena_len) qa_compat_qc_program_error("progs.dat", "invalid engine string checkpoint");
  }
  if (qa_binary_reader_remaining(&r.reader)) qa_compat_qc_program_error("progs.dat", "trailing string checkpoint bytes");
  qa_cleanup_pop(&scope);
  qa_binary_reader_destroy(&r.reader);
  for (size_t i = 0; i < s->engine_count; i++) free(s->engines[i].name);
  free(s->engines); free(s->arena);
  s->arena = r.arena; s->arena_length = arena_len; s->used = arena_len; s->engines = r.entries; s->engine_count = r.count; s->engine_capacity = capacity;
  interned_clear(s);
  return true;
}
