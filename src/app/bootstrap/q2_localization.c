/* Port of src/app/bootstrap/q2-localization.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_localization.h"
#include <stdlib.h>
#include <string.h>
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out != NULL) memcpy(out, text, len + 1);
  return out;
}
char *qa_app_q2_localized_text(qa_localization_table *catalog, const char *text,
    const char *const *args, size_t arg_count) {
  bool dollar = text[0] == '$';
  size_t key_len = strlen(text);
  if (dollar) while (key_len > 0 && (text[key_len - 1] == '\r' || text[key_len - 1] == '\n')) key_len--;
  const char *suffix = text + key_len;
  const qa_loc_string *entry = NULL;
  if (dollar) {
    char *key = malloc(key_len); /* key without '$', NUL takes its place */
    if (key == NULL) return NULL;
    memcpy(key, text + 1, key_len - 1); key[key_len - 1] = '\0';
    entry = qa_localization_table_find(catalog, key);
    free(key);
  }
  if (dollar && entry == NULL) return dup_text(text);
  if (entry == NULL) {
    if (arg_count == 0) return dup_text(text);
    return qa_localization_table_localize(catalog, text, args, arg_count, true, 1024, NULL);
  }
  /* Assemble format slices with substituted arguments + suffix. */
  size_t capacity = strlen(entry->format) + strlen(suffix) + 1;
  for (size_t i = 0; i < entry->argument_count; i++) {
    int index = entry->arguments[i].arg_index;
    if (index < 0 || (size_t)index >= arg_count || args[index] == NULL) return dup_text(text);
    capacity += strlen(args[index]) + 64;
  }
  char *result = malloc(capacity);
  if (result == NULL) return NULL;
  size_t at = 0, start = 0;
  for (size_t i = 0; i < entry->argument_count; i++) {
    const qa_loc_arg *slot = &entry->arguments[i];
    const char *argument = args[slot->arg_index];
    char *localized = NULL;
    const char *value = argument;
    if (argument[0] == '$' && qa_localization_table_find(catalog, argument + 1) != NULL) {
      localized = qa_localization_table_localize(catalog, argument, NULL, 0, true, 1024, NULL);
      if (localized != NULL) value = localized;
    }
    size_t slice = slot->start - start;
    memcpy(result + at, entry->format + start, slice); at += slice;
    size_t vlen = strlen(value);
    if (at + vlen + 1 > capacity) {
      capacity = (at + vlen + 1) * 2;
      char *next = realloc(result, capacity);
      if (next == NULL) { free(localized); free(result); return NULL; }
      result = next;
    }
    memcpy(result + at, value, vlen); at += vlen;
    free(localized);
    start = slot->end;
  }
  size_t tail = strlen(entry->format + start);
  size_t slen = strlen(suffix);
  if (at + tail + slen + 1 > capacity) {
    char *next = realloc(result, at + tail + slen + 1);
    if (next == NULL) { free(result); return NULL; }
    result = next;
  }
  memcpy(result + at, entry->format + start, tail); at += tail;
  memcpy(result + at, suffix, slen); at += slen;
  result[at] = '\0';
  return result;
}
