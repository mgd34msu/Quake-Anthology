/* Port of src/compat/qc/memory.ts. GPL-2.0-or-later.
 * QcWords borrows its byte storage (entity field views borrow the
 * entity arena). QcStrings.get returns a borrowed buffer valid until
 * the next strings mutation (TS returns a fresh string). */
#ifndef QA_COMPAT_QC_MEMORY_H
#define QA_COMPAT_QC_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"

typedef struct qa_compat_qc_words { uint8_t *bytes; size_t byte_length; } qa_compat_qc_words;
/* bytes length must be a whole number of words (TS RangeError). */
void qa_compat_qc_words_init(qa_compat_qc_words *w, uint8_t *bytes, size_t byte_length);
int64_t qa_compat_qc_words_length(const qa_compat_qc_words *w);
int32_t qa_compat_qc_words_int(const qa_compat_qc_words *w, int64_t word);
double qa_compat_qc_words_float(const qa_compat_qc_words *w, int64_t word);
void qa_compat_qc_words_set_int(qa_compat_qc_words *w, int64_t word, double value);
void qa_compat_qc_words_set_float(qa_compat_qc_words *w, int64_t word, double value);
qa_vec3 qa_compat_qc_words_vector(const qa_compat_qc_words *w, int64_t word);
void qa_compat_qc_words_set_vector(qa_compat_qc_words *w, int64_t word, qa_vec3 value);
void qa_compat_qc_words_copy_words(qa_compat_qc_words *dst, const qa_compat_qc_words *from, int64_t source, int64_t destination, int64_t count);

typedef struct qa_compat_qc_entity_layout { int64_t stride_bytes, variables_offset_bytes, field_words; } qa_compat_qc_entity_layout;
typedef struct qa_compat_qc_entity_memory {
  qa_compat_qc_entity_layout layout; int64_t capacity;
  uint8_t *bytes; size_t byte_length; /* owned */
  qa_compat_qc_words *fields; bool *fields_ready; int64_t count;
} qa_compat_qc_entity_memory;
/* count 0 in options means TS default 1 is NOT applied; pass count>=1. False only on OOM. */
bool qa_compat_qc_entity_memory_init(qa_compat_qc_entity_memory *m, qa_compat_qc_entity_layout layout, int64_t capacity, int64_t count);
void qa_compat_qc_entity_memory_destroy(qa_compat_qc_entity_memory *m);
int64_t qa_compat_qc_entity_memory_get_count(const qa_compat_qc_entity_memory *m);
void qa_compat_qc_entity_memory_set_count(qa_compat_qc_entity_memory *m, int64_t count);
int64_t qa_compat_qc_entity_memory_reference(qa_compat_qc_entity_memory *m, int64_t slot);
int64_t qa_compat_qc_entity_memory_slot(qa_compat_qc_entity_memory *m, int64_t reference);
qa_compat_qc_words *qa_compat_qc_entity_memory_at(qa_compat_qc_entity_memory *m, int64_t slot);
qa_compat_qc_words *qa_compat_qc_entity_memory_from_reference(qa_compat_qc_entity_memory *m, int64_t reference);
int64_t qa_compat_qc_entity_memory_pointer(qa_compat_qc_entity_memory *m, int64_t reference, int64_t field);
typedef struct qa_compat_qc_resolved_pointer { qa_compat_qc_words *fields; int64_t word; } qa_compat_qc_resolved_pointer;
qa_compat_qc_resolved_pointer qa_compat_qc_entity_memory_resolve_pointer(qa_compat_qc_entity_memory *m, int64_t pointer, int64_t words);
void qa_compat_qc_entity_memory_restore(qa_compat_qc_entity_memory *m, const uint8_t *bytes, size_t length, int64_t count);

typedef struct qa_compat_qc_engine_string { char *name; int64_t offset, capacity; } qa_compat_qc_engine_string;
typedef struct qa_compat_qc_strings {
  uint8_t *arena; size_t arena_length, used; /* owned */
  qa_compat_qc_engine_string *engines; size_t engine_count, engine_capacity;
  bool quakeworld; char *get_buffer; size_t get_buffer_length;
} qa_compat_qc_strings;
bool qa_compat_qc_strings_init(qa_compat_qc_strings *s, const uint8_t *program_strings, size_t length, bool quakeworld);
void qa_compat_qc_strings_destroy(qa_compat_qc_strings *s);
int64_t qa_compat_qc_strings_allocate(qa_compat_qc_strings *s, const char *text);
/* capacity<0 = TS default 128. */
int64_t qa_compat_qc_strings_set_engine(qa_compat_qc_strings *s, const char *name, const char *text, int64_t capacity);
/* Borrowed; valid until next mutation. */
const char *qa_compat_qc_strings_get(qa_compat_qc_strings *s, int64_t reference);
/* Malloc'd snapshot (caller frees); NULL on OOM, *out_length set. */
uint8_t *qa_compat_qc_strings_snapshot(qa_compat_qc_strings *s, size_t *out_length);
bool qa_compat_qc_strings_restore(qa_compat_qc_strings *s, const uint8_t *bytes, size_t length);
#endif
