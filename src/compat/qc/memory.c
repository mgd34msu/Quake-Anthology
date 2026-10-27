/* Port of src/compat/qc/memory.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/memory.h"
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
void qa_compat_qc_words_set_float(qa_compat_qc_words *w, int64_t word, double value) { float v = qa_float32(value); memcpy(w->bytes + woff(w, word), &v, 4); }
qa_vec3 qa_compat_qc_words_vector(const qa_compat_qc_words *w, int64_t word) {
  return (qa_vec3){qa_compat_qc_words_float(w, word), qa_compat_qc_words_float(w, word + 1), qa_compat_qc_words_float(w, word + 2)};
}
void qa_compat_qc_words_set_vector(qa_compat_qc_words *w, int64_t word, qa_vec3 v) {
  qa_compat_qc_words_set_float(w, word, v.x); qa_compat_qc_words_set_float(w, word + 1, v.y); qa_compat_qc_words_set_float(w, word + 2, v.z);
}
void qa_compat_qc_words_copy_words(qa_compat_qc_words *d, const qa_compat_qc_words *f, int64_t s, int64_t dst, int64_t n) {
  for (int64_t i = 0; i < n; i++) qa_compat_qc_words_set_int(d, dst + i, qa_compat_qc_words_int(f, s + i));
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
  s->arena = malloc(s->arena_length); if (!s->arena) return false;
  memcpy(s->arena, prog, n); s->used = n; return true;
}
void qa_compat_qc_strings_destroy(qa_compat_qc_strings *s) {
  for (size_t i = 0; i < s->engine_count; i++) free(s->engines[i].name);
  free(s->engines); free(s->arena); free(s->get_buffer); memset(s, 0, sizeof *s);
}
static int64_t reserve(qa_compat_qc_strings *s, size_t len) {
  size_t off = s->used, req = off + len;
  if (req > 0x7fffffff) qa_compat_qc_program_error("progs.dat", "string arena overflow");
  if (req > s->arena_length) {
    size_t nl = req > s->arena_length * 2 ? req : s->arena_length * 2;
    uint8_t *g = realloc(s->arena, nl); if (!g) qa_range_error("string arena allocation failed");
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
int64_t qa_compat_qc_strings_set_engine(qa_compat_qc_strings *s, const char *name, const char *text, int64_t capacity) {
  if (capacity < 0) capacity = 128;
  size_t idx = s->engine_count; bool found = false;
  for (size_t i = 0; i < s->engine_count; i++) if (strcmp(s->engines[i].name, name) == 0) { idx = i; found = true; break; }
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
  char *t = qa_compat_qc_byte_string(s->arena, s->used, off);
  if (!t) qa_range_error("string allocation failed");
  free(s->get_buffer); s->get_buffer = t; return t;
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
bool qa_compat_qc_strings_restore(qa_compat_qc_strings *s, const uint8_t *bytes, size_t length) {
  qa_binary_reader r; if (!qa_binary_reader_init(&r, (uint8_t *)bytes, length, "QC strings checkpoint")) return false;
  if (qa_binary_reader_u32(&r) != 0x31534351u || qa_binary_reader_u32(&r) != (s->quakeworld ? 1u : 0u)) qa_compat_qc_program_error("progs.dat", "incompatible string checkpoint");
  uint32_t arena_len = qa_binary_reader_u32(&r);
  uint8_t *arena = qa_binary_reader_bytes(&r, arena_len); if (!arena && arena_len) { qa_binary_reader_destroy(&r); return false; }
  uint32_t count = qa_binary_reader_u32(&r);
  qa_compat_qc_engine_string *entries = NULL; if (count) entries = calloc(count, sizeof *entries);
  if (count && !entries) { free(arena); qa_binary_reader_destroy(&r); return false; }
  for (uint32_t i = 0; i < count; i++) {
    uint32_t off = qa_binary_reader_u32(&r), cap = qa_binary_reader_u32(&r);
    char *name = qa_binary_reader_fixed_byte_string(&r, qa_binary_reader_u32(&r));
    if (!name) { free(arena); qa_binary_reader_destroy(&r); return false; }
    if (cap < 1 || (int64_t)off + cap > arena_len) qa_compat_qc_program_error("progs.dat", "invalid engine string checkpoint");
    entries[i] = (qa_compat_qc_engine_string){name, off, cap};
  }
  if (qa_binary_reader_remaining(&r)) qa_compat_qc_program_error("progs.dat", "trailing string checkpoint bytes");
  qa_binary_reader_destroy(&r);
  for (size_t i = 0; i < s->engine_count; i++) free(s->engines[i].name);
  free(s->engines); free(s->arena);
  s->arena = arena; s->arena_length = arena_len; s->used = arena_len; s->engines = entries; s->engine_count = count; s->engine_capacity = count;
  return true;
}
