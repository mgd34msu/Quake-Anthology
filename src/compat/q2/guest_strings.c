/* Port of src/compat/q2/guest-strings.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/guest_strings.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

static char *dup_bytes(const char *text, size_t length) {
  char *p = malloc(length + 1); if (p == NULL) qa_range_error("Out of memory");
  memcpy(p, text, length); p[length] = '\0'; return p;
}
void qa_compat_q2_reusable_guest_string_init(qa_compat_q2_reusable_guest_string *s, const qa_compat_q2_memory *memory, const char *label) {
  memset(s, 0, sizeof *s); s->memory = memory; s->label = dup_bytes(label, strlen(label));
}
void qa_compat_q2_reusable_guest_string_destroy(qa_compat_q2_reusable_guest_string *s) { free(s->label); free(s->text); memset(s, 0, sizeof *s); }
qa_guest_address qa_compat_q2_reusable_guest_string_set(qa_compat_q2_reusable_guest_string *s, const uint8_t *bytes, size_t length) {
  free(s->text); s->text = NULL;
  if (!s->has_address || (double)length + 1 > s->capacity) {
    /* The outgrown block stays mapped: a guest may still hold the old pointer, and growth is rare. */
    s->address = qa_compat_q2_allocate_pooled_native_memory(s->memory, (double)length + 1, s->label); s->has_address = true;
    s->capacity = qa_compat_q2_native_allocation_bytes((double)length + 1);
  }
  uint8_t *terminated = malloc(length + 1); if (terminated == NULL) qa_range_error("Out of memory");
  if (length != 0) memcpy(terminated, bytes, length);
  terminated[length] = 0;
  s->memory->write(s->memory->ctx, s->address, terminated, length + 1); free(terminated);
  return s->address;
}
qa_guest_address qa_compat_q2_reusable_guest_string_set_text(qa_compat_q2_reusable_guest_string *s, const char *text) {
  if (s->has_address && s->text != NULL && strcmp(s->text, text) == 0) return s->address;
  qa_guest_address address = qa_compat_q2_reusable_guest_string_set(s, (const uint8_t *)text, strlen(text));
  s->text = dup_bytes(text, strlen(text));
  return address;
}

typedef struct block { qa_guest_address address; double bytes; } block;
typedef struct entry { char *text; qa_guest_address address; } entry;
struct qa_compat_q2_command_guest_strings {
  const qa_compat_q2_memory *memory; char *label;
  bool has_key; char *key; size_t key_length;
  entry *strings; size_t string_count, string_cap;
  block *blocks; size_t block_count, block_cap;
};
qa_compat_q2_command_guest_strings *qa_compat_q2_command_guest_strings_create(const qa_compat_q2_memory *memory, const char *label) {
  qa_compat_q2_command_guest_strings *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->memory = memory; s->label = dup_bytes(label, strlen(label)); return s;
}
static void clear_strings(qa_compat_q2_command_guest_strings *s) { for (size_t i = 0; i < s->string_count; i++) free(s->strings[i].text); s->string_count = 0; }
void qa_compat_q2_command_guest_strings_destroy(qa_compat_q2_command_guest_strings *s) {
  if (s == NULL) return;
  clear_strings(s); free(s->strings); free(s->blocks); free(s->key); free(s->label); free(s);
}
qa_guest_address qa_compat_q2_command_guest_strings_get(qa_compat_q2_command_guest_strings *s, const char *command, size_t command_length, const char *text) {
  if (!s->has_key || s->key_length != command_length || memcmp(s->key, command, command_length) != 0) {
    for (size_t i = 0; i < s->block_count; i++) s->memory->unmap(s->memory->ctx, s->blocks[i].address, s->blocks[i].bytes);
    s->block_count = 0; clear_strings(s);
    free(s->key); s->key = dup_bytes(command, command_length); s->key_length = command_length; s->has_key = true;
  }
  for (size_t i = 0; i < s->string_count; i++) if (strcmp(s->strings[i].text, text) == 0) return s->strings[i].address;
  size_t length = strlen(text);
  qa_guest_address address = qa_compat_q2_allocate_pooled_native_memory(s->memory, (double)length + 1, s->label);
  s->memory->write(s->memory->ctx, address, (const uint8_t *)text, length + 1);
  if (s->block_count == s->block_cap) { s->block_cap = s->block_cap ? s->block_cap * 2 : 8; s->blocks = realloc(s->blocks, s->block_cap * sizeof *s->blocks); if (s->blocks == NULL) qa_range_error("Out of memory"); }
  s->blocks[s->block_count++] = (block){address, qa_compat_q2_native_allocation_bytes((double)length + 1)};
  if (s->string_count == s->string_cap) { s->string_cap = s->string_cap ? s->string_cap * 2 : 8; s->strings = realloc(s->strings, s->string_cap * sizeof *s->strings); if (s->strings == NULL) qa_range_error("Out of memory"); }
  s->strings[s->string_count++] = (entry){dup_bytes(text, length), address};
  return address;
}
