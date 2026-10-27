/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/bsp_entities.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void qa_aas_bsp_entities_init(qa_aas_bsp_entities *e) { memset(e, 0, sizeof *e); }
void qa_aas_bsp_entities_destroy(qa_aas_bsp_entities *e) {
  free(e->text);
  e->text = NULL;
}
bool qa_aas_bsp_entities_load(qa_aas_bsp_entities *e, const char *t) {
  free(e->text);
  size_t n = strlen(t) + 1;
  e->text = malloc(n);
  if (!e->text)
    return false;
  memcpy(e->text, t, n);
  e->length = n - 1;
  int64_t count = 0;
  for (const char *p = t; *p; p++)
    if (*p == '{')
      count++;
  if (count > QA_BSP_MAX_ENTITIES)
    count = QA_BSP_MAX_ENTITIES;
  e->entity_count = count;
  return true;
}
/* Find "key" "value" inside entity-th { } block (source epair scan). */
static bool find_value(qa_aas_bsp_entities *e, int64_t ent, const char *key, char *buf, size_t bl) {
  if (!e->text || ent < 0)
    return false;
  if (ent < INT32_MIN || ent > INT32_MAX)
    qa_range_error("BSP entity numbers must be signed 32-bit integers");
  const char *p = e->text;
  int64_t cur = -1;
  while (*p) {
    if (*p == '{') {
      cur++;
      p++;
      if (cur == ent)
        break;
      continue;
    }
    p++;
  }
  if (cur != ent)
    return false;
  int depth = 1;
  while (*p && depth > 0) {
    if (*p == '{')
      depth++;
    else if (*p == '}') {
      depth--;
      if (!depth)
        break;
    } else if (*p == '"') {
      char k[129];
      size_t ki = 0;
      p++;
      while (*p && *p != '"' && ki < 128)
        k[ki++] = *p++;
      k[ki] = '\0';
      if (*p == '"')
        p++;
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
      if (*p != '"')
        continue;
      p++;
      char v[512];
      size_t vi = 0;
      while (*p && *p != '"' && vi < 511)
        v[vi++] = *p++;
      v[vi] = '\0';
      if (*p == '"')
        p++;
      if (strcmp(k, key) == 0) {
        snprintf(buf, bl, "%s", v);
        return true;
      }
      continue;
    }
    p++;
  }
  return false;
}
const char *qa_aas_bsp_entities_get_string(qa_aas_bsp_entities *e, int64_t en, const char *k,
                                           char *b, size_t bl) {
  return find_value(e, en, k, b, bl) ? b : NULL;
}
qa_aas_bsp_number_result qa_aas_bsp_entities_get_float(qa_aas_bsp_entities *e, int64_t en,
                                                       const char *k) {
  char b[512];
  if (!find_value(e, en, k, b, sizeof b))
    return (qa_aas_bsp_number_result){false, 0};
  return (qa_aas_bsp_number_result){true, qa_native_atof(b)};
}
qa_aas_bsp_number_result qa_aas_bsp_entities_get_int(qa_aas_bsp_entities *e, int64_t en,
                                                     const char *k) {
  char b[512];
  if (!find_value(e, en, k, b, sizeof b))
    return (qa_aas_bsp_number_result){false, 0};
  return (qa_aas_bsp_number_result){true, qa_native_atoi(b)};
}
qa_aas_bsp_vector_result qa_aas_bsp_entities_get_vector(qa_aas_bsp_entities *e, int64_t en,
                                                        const char *k) {
  char b[512];
  if (!find_value(e, en, k, b, sizeof b))
    return (qa_aas_bsp_vector_result){false, {0, 0, 0}};
  double x = 0, y = 0, z = 0;
  char *p = b;
  x = qa_native_atof(p);
  while (*p && *p != ' ')
    p++;
  if (*p)
    p++;
  y = qa_native_atof(p);
  while (*p && *p != ' ')
    p++;
  if (*p)
    p++;
  z = qa_native_atof(p);
  return (qa_aas_bsp_vector_result){true, {x, y, z}};
}
int64_t qa_aas_bsp_entities_get_count(const qa_aas_bsp_entities *e) { return e->entity_count; }
