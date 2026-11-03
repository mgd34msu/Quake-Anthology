/* Port of src/text/localization-key.ts. GPL-2.0-or-later. */
#include "qa/text/localization_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#define RS '\x1e'
#define US '\x1f'
static const char TAG[] = "\x1eQLK";
#define TAG_LENGTH (sizeof TAG - 1)
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static char *copy_range(const char *from, size_t length) {
  char *out = xmalloc(length + 1);
  memcpy(out, from, length);
  out[length] = '\0';
  return out;
}
/* TS clean: separators inside a field would corrupt the layout, so they become spaces. */
static void append_clean(char *out, size_t *at, const char *value) {
  for (const char *c = value; *c; c++) out[(*at)++] = *c == RS || *c == US ? ' ' : *c;
}
char *qa_encode_localization_key(const char *key, const char *const *args, size_t arg_count) {
  char version[24];
  snprintf(version, sizeof version, "%d", QA_LOCALIZATION_KEY_VERSION);
  size_t length = TAG_LENGTH + strlen(version) + 1 + strlen(key);
  for (size_t i = 0; i < arg_count; i++) length += 1 + strlen(args[i]);
  char *text = xmalloc(length + 1);
  size_t at = 0;
  memcpy(text, TAG, TAG_LENGTH); at += TAG_LENGTH;
  memcpy(text + at, version, strlen(version)); at += strlen(version);
  text[at++] = US;
  append_clean(text, &at, key);
  for (size_t i = 0; i < arg_count; i++) {
    text[at++] = US;
    append_clean(text, &at, args[i]);
  }
  text[at] = '\0';
  return text;
}
bool qa_is_localization_key_text(const char *text) {
  return strncmp(text, TAG, TAG_LENGTH) == 0;
}
/* text.slice(TAG.length).split(US): the fields as ranges into text. */
typedef struct field { const char *at; size_t length; } field;
static field *split_fields(const char *text, size_t *out_count) {
  const char *rest = text + TAG_LENGTH;
  size_t count = 1;
  for (const char *c = rest; *c; c++) if (*c == US) count++;
  field *fields = xmalloc(count * sizeof *fields);
  size_t index = 0;
  const char *start = rest;
  for (const char *c = rest;; c++)
    if (*c == US || *c == '\0') {
      fields[index].at = start;
      fields[index].length = (size_t)(c - start);
      index++;
      if (*c == '\0') break;
      start = c + 1;
    }
  *out_count = count;
  return fields;
}
bool qa_decode_localization_key(const char *text, qa_localization_key_text *out) {
  memset(out, 0, sizeof *out);
  if (!qa_is_localization_key_text(text)) return false;
  size_t count = 0;
  field *fields = split_fields(text, &count);
  char version[24];
  snprintf(version, sizeof version, "%d", QA_LOCALIZATION_KEY_VERSION);
  bool known = fields[0].length == strlen(version) && memcmp(fields[0].at, version, fields[0].length) == 0;
  if (!known || count < 2) { free(fields); return false; }
  out->key = copy_range(fields[1].at, fields[1].length);
  out->arg_count = count - 2;
  out->args = xmalloc(out->arg_count * sizeof *out->args);
  for (size_t i = 0; i < out->arg_count; i++) out->args[i] = copy_range(fields[i + 2].at, fields[i + 2].length);
  free(fields);
  return true;
}
void qa_localization_key_text_release(qa_localization_key_text *text) {
  free(text->key);
  for (size_t i = 0; i < text->arg_count; i++) free(text->args[i]);
  free(text->args);
  memset(text, 0, sizeof *text);
}
char *qa_plain_localization_text(const char *text) {
  if (!qa_is_localization_key_text(text)) return copy_range(text, strlen(text));
  size_t count = 0;
  field *fields = split_fields(text, &count);
  /* fields.slice(1).join(" ") */
  size_t length = 0;
  for (size_t i = 1; i < count; i++) length += fields[i].length + (i > 1);
  char *out = xmalloc(length + 1);
  size_t at = 0;
  for (size_t i = 1; i < count; i++) {
    if (i > 1) out[at++] = ' ';
    memcpy(out + at, fields[i].at, fields[i].length);
    at += fields[i].length;
  }
  out[at] = '\0';
  free(fields);
  return out;
}
