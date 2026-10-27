/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/structure.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
const qa_structure_field_definition *qa_find_structure_field(const qa_structure_field_definition *f,
                                                             size_t n, const char *name) {
  for (size_t i = 0; i < n; i++)
    if (strcmp(f[i].name, name) == 0)
      return &f[i];
  return NULL;
}
bool qa_write_indent_bots_a(qa_structure_write_fn w, void *c, int64_t indent) {
  for (int64_t i = 0; i < indent; i++)
    if (w(c, "  ") < 0)
      return false;
  return true;
}
bool qa_write_float_bots_a(qa_structure_write_fn w, void *c, double v) {
  char b[64];
  snprintf(b, sizeof b, "%g", (double)(float)v);
  return w(c, b) >= 0;
}
bool qa_write_struct_with_indent_bots_a(qa_structure_write_fn w, void *c,
                                        const qa_structure_definition *d, const uint8_t *bytes,
                                        size_t len, int64_t indent) {
  (void)bytes;
  (void)len;
  if (!qa_write_indent_bots_a(w, c, indent))
    return false;
  if (w(c, "{\n") < 0)
    return false;
  for (size_t i = 0; i < d->field_count; i++) {
    if (!qa_write_indent_bots_a(w, c, indent + 1))
      return false;
    if (w(c, d->fields[i].name) < 0)
      return false;
    if (w(c, "\n") < 0)
      return false;
  }
  if (!qa_write_indent_bots_a(w, c, indent))
    return false;
  return w(c, "}\n") >= 0;
}
bool qa_write_structure_bots_a(qa_structure_write_fn w, void *c, const qa_structure_definition *d,
                               const uint8_t *b, size_t n) {
  return qa_write_struct_with_indent_bots_a(w, c, d, b, n, 0);
}
