/* Port of src/content/q2/foundation/fields.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/fields.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_parse.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

const qa_vec3 QA_Q2_ZERO = {0, 0, 0};

double qa_q2_number_field(const qa_q2_spawn_fields *f, const char *key, double fallback) {
  const char *v = qa_q2_spawn_get(f, key);
  return v == NULL ? fallback : qa_native_atof(v);
}
double qa_q2_integer_field(const qa_q2_spawn_fields *f, const char *key, double fallback) {
  const char *v = qa_q2_spawn_get(f, key);
  return v == NULL ? fallback : (double)qa_native_atoi(v);
}
qa_vec3 qa_q2_vector_field(const qa_q2_spawn_fields *f, const char *key) {
  const char *raw = qa_q2_spawn_get(f, key);
  if (raw == NULL) raw = "";
  /* TS: trim().split(/\s+/) then atof of first three (missing -> "0"). */
  double out[3] = {0, 0, 0};
  const char *p = raw;
  int i = 0;
  while (i < 3) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) break;
    const char *start = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t n = (size_t)(p - start);
    char buf[128];
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, start, n); buf[n] = '\0';
    out[i++] = qa_native_atof(buf);
  }
  return (qa_vec3){out[0], out[1], out[2]};
}

static char *entity_string(const char *value) {
  size_t n = strlen(value);
  char *out = malloc(n + 1);
  if (!out) return NULL;
  size_t o = 0;
  for (size_t i = 0; i < n; i++) {
    if (value[i] == '\\' && i + 1 < n) {
      /* TS /(.)/gs: any escaped char (incl newline) consumed. */
      out[o++] = value[i + 1] == 'n' ? '\n' : '\\';
      i++;
    } else out[o++] = value[i];
  }
  out[o] = '\0';
  return out;
}

/* Returns malloc'd token or NULL for EOF. *oom set on allocation failure. */
static char *entity_token(qa_legacy_parse_state *st, double limit, bool *is_eof) {
  for (;;) {
    while (st->index < st->length && st->data[st->index] > 0 && (unsigned char)st->data[st->index] <= 32) st->index++;
    if (st->index >= st->length || st->data[st->index] == 0) { *is_eof = true; return NULL; }
    if (st->data[st->index] != '/' || st->index + 1 >= st->length || st->data[st->index + 1] != '/') break;
    while (st->index < st->length && st->data[st->index] != 0 && st->data[st->index] != '\n') st->index++;
  }
  *is_eof = false;
  return qa_parse_q2_token(st, limit);
}

bool qa_q2_parse_entities(const char *source, qa_q2_edition edition, qa_q2_spawn_fields **out, size_t *out_count) {
  qa_legacy_parse_state st = {.data = source, .length = strlen(source), .index = 0};
  qa_q2_spawn_fields *list = NULL;
  size_t count = 0, cap = 0;
  double limit = edition == QA_Q2_EDITION_CLASSIC ? 128 : 512;
  for (;;) {
    bool eof = false;
    char *tok = entity_token(&st, limit, &eof);
    if (eof) break;
    if (!tok) goto oom;
    if (strcmp(tok, "{") != 0) { free(tok); qa_range_error("Q2 entity %zu: expected opening brace", count); }
    free(tok);
    qa_q2_spawn_fields fields = {.ordinal = (double)count, .classname = ""};
    for (;;) {
      bool keof = false;
      char *key = entity_token(&st, limit, &keof);
      if (keof) { qa_q2_spawn_fields_free(&fields); qa_range_error("Q2 entity %zu: EOF without closing brace", count); }
      if (!key) { qa_q2_spawn_fields_free(&fields); goto oom_list; }
      if (strcmp(key, "}") == 0) { free(key); break; }
      bool veof = false;
      char *val = entity_token(&st, limit, &veof);
      if (veof || (val && strcmp(val, "}") == 0)) {
        free(val); qa_q2_spawn_fields_free(&fields);
        qa_range_error("Q2 entity %zu: missing value for %s", count, key);
      }
      if (!val) { free(key); qa_q2_spawn_fields_free(&fields); goto oom_list; }
      char *unesc = entity_string(val);
      free(val);
      if (!unesc || !qa_q2_spawn_set(&fields, key, unesc)) { free(key); free(unesc); qa_q2_spawn_fields_free(&fields); goto oom_list; }
      free(key); free(unesc);
    }
    const char *cn = qa_q2_spawn_get(&fields, "classname");
    fields.classname = cn ? cn : "";
    if (count == cap) {
      size_t ncap = cap ? cap * 2 : 16;
      qa_q2_spawn_fields *next = realloc(list, ncap * sizeof *next);
      if (!next) { qa_q2_spawn_fields_free(&fields); goto oom_list; }
      list = next; cap = ncap;
    }
    list[count++] = fields;
  }
  *out = list; *out_count = count;
  return true;
oom_list:
  for (size_t i = 0; i < count; i++) qa_q2_spawn_fields_free(&list[i]);
  free(list);
oom:
  return false;
}

void qa_q2_entities_free(qa_q2_spawn_fields *e, size_t n) {
  for (size_t i = 0; i < n; i++) qa_q2_spawn_fields_free(&e[i]);
  free(e);
}

bool qa_q2_inhibit_spawn(const qa_q2_spawn_fields *f, qa_q2_game_options o) {
  if (strcmp(f->classname, "worldspawn") == 0) return false;
  int64_t flags = (int64_t)qa_q2_integer_field(f, "spawnflags", 0);
  if (o.mode == QA_Q2_MODE_DEATHMATCH) return (flags & 2048) != 0;
  if (o.edition == QA_Q2_EDITION_RERELEASE) {
    if (o.mode == QA_Q2_MODE_COOP && (flags & 4096) != 0) return true;
    if (o.mode != QA_Q2_MODE_COOP && (flags & 16384) != 0) return true;
  }
  return (flags & (o.skill == 0 ? 256 : o.skill == 1 ? 512 : 1024)) != 0;
}

qa_vec3 qa_q2_movedir(qa_vec3 a) {
  if (a.x == 0 && a.y == -1 && a.z == 0) return (qa_vec3){0, 0, 1};
  if (a.x == 0 && a.y == -2 && a.z == 0) return (qa_vec3){0, 0, -1};
  return qa_math_angle_vectors_rule((qa_vec3){a.x, a.y, 0}, QA_ANGLE_RULE_NATIVE).forward;
}
