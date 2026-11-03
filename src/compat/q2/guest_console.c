/* Port of src/compat/q2/guest-console.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/guest_console.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static const char *const allowed[] = {"map", "gamemap", "kick", "say", "echo", "exec", "wait"};

static void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n == 0 ? 1 : n);
  if (q == NULL) qa_range_error("guest console allocation failed");
  return q;
}
/* JS String.prototype.trim whitespace in the latin1 range. */
static bool js_space(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13) || c == 0xa0; }

typedef struct { char *bytes; size_t length, capacity; } buffer;
static void put(buffer *b, const char *bytes, size_t length) {
  if (b->length + length + 1 > b->capacity) {
    b->capacity = (b->length + length + 1) * 2;
    b->bytes = xrealloc(b->bytes, b->capacity);
  }
  memcpy(b->bytes + b->length, bytes, length);
  b->length += length;
  b->bytes[b->length] = '\0';
}

static void classify(const char *command, size_t length, buffer *kept, qa_compat_q2_guest_console_filter *out) {
  size_t start = 0, end = length;
  while (start < end && js_space((unsigned char)command[start])) start++;
  while (end > start && js_space((unsigned char)command[end - 1])) end--;
  /* command.trim().split(/\s+/, 1)[0].toLowerCase() */
  size_t name_end = start;
  while (name_end < end && !js_space((unsigned char)command[name_end])) name_end++;
  if (name_end == start) return;
  char *name = malloc(name_end - start + 1);
  if (name == NULL) qa_range_error("guest console allocation failed");
  for (size_t i = start; i < name_end; i++) {
    unsigned char c = (unsigned char)command[i];
    name[i - start] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
  }
  name[name_end - start] = '\0';
  bool permitted = false;
  for (size_t i = 0; i < sizeof allowed / sizeof *allowed; i++) permitted = permitted || strcmp(name, allowed[i]) == 0;
  if (permitted) {
    if (kept->length != 0) put(kept, "\n", 1);
    put(kept, command + start, end - start);
    free(name);
  } else {
    out->refused = xrealloc(out->refused, (out->refused_count + 1) * sizeof *out->refused);
    out->refused[out->refused_count++] = name;
  }
}

qa_compat_q2_guest_console_filter qa_compat_q2_filter_guest_console(const char *text) {
  qa_compat_q2_guest_console_filter out = {NULL, NULL, 0};
  buffer kept = {NULL, 0, 0};
  put(&kept, "", 0);
  size_t current = 0;
  bool quoted = false;
  const size_t length = strlen(text);
  for (size_t i = 0; i < length; i++) {
    const char character = text[i];
    if (character == '"') quoted = !quoted;
    if ((character == ';' && !quoted) || character == '\n') {
      classify(text + current, i - current, &kept, &out);
      current = i + 1;
      quoted = false;
    }
  }
  classify(text + current, length - current, &kept, &out);
  if (kept.length != 0) put(&kept, "\n", 1);
  out.text = kept.bytes;
  return out;
}

char *qa_compat_q2_guest_console_refused_list(const qa_compat_q2_guest_console_filter *filter) {
  buffer list = {NULL, 0, 0};
  put(&list, "", 0);
  for (size_t i = 0; i < filter->refused_count; i++) {
    if (i != 0) put(&list, ", ", 2);
    put(&list, filter->refused[i], strlen(filter->refused[i]));
  }
  return list.bytes;
}

void qa_compat_q2_guest_console_filter_free(qa_compat_q2_guest_console_filter *filter) {
  free(filter->text);
  for (size_t i = 0; i < filter->refused_count; i++) free(filter->refused[i]);
  free(filter->refused);
  filter->text = NULL;
  filter->refused = NULL;
  filter->refused_count = 0;
}
