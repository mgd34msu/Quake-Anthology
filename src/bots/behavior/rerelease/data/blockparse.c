/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/data/blockparse.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
static char *dupn(const char *s, size_t n) { char *p = malloc(n + 1); if (!p) return NULL; memcpy(p, s, n); p[n] = 0; return p; }
static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
typedef struct { char **v; size_t n, cap; } SVec;
typedef struct { qa_block_parse_error *v; size_t n, cap; } EVec;
static bool spush(SVec *s, char *v) { if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 8; s->v = realloc(s->v, s->cap * sizeof *s->v); if (!s->v) return false; } s->v[s->n++] = v; return true; }
static bool epush(EVec *e, const char *msg, int64_t line) { if (e->n == e->cap) { e->cap = e->cap ? e->cap * 2 : 8; e->v = realloc(e->v, e->cap * sizeof *e->v); if (!e->v) return false; } e->v[e->n].message = dupn(msg, strlen(msg)); e->v[e->n].line = line; e->n++; return true; }
static void sfree(SVec *s) { for (size_t i = 0; i < s->n; i++) free(s->v[i]); free(s->v); s->v = NULL; s->n = s->cap = 0; }
static SVec tokenize(const char *line, size_t len, int64_t line_no, EVec *errors) {
  SVec toks = {0}; size_t i = 0;
  while (i < len) { while (i < len && is_space(line[i])) i++; if (i >= len) break;
    if (line[i] == '"') { i++; size_t st = i; while (i < len && line[i] != '"') i++; char *t = dupn(line + st, i - st); spush(&toks, t); if (i < len) i++; else epush(errors, "unclosed quoted string", line_no); }
    else if (line[i] == '{' || line[i] == '}') { spush(&toks, dupn(line + i, 1)); i++; }
    else { size_t st = i; while (i < len && !is_space(line[i]) && line[i] != '{' && line[i] != '}') i++; spush(&toks, dupn(line + st, i - st)); } }
  return toks;
}
static size_t stripped_len(const char *line, size_t len) { bool q = false; for (size_t i = 0; i < len; i++) { if (line[i] == '"') q = !q; else if (!q && line[i] == '/' && i + 1 < len && line[i + 1] == '/') return i; } return len; }
void qa_block_parse_result_destroy(qa_block_parse_result *r) {
  if (!r) return;
  for (size_t i = 0; i < r->block_count; i++) { qa_block *b = &r->blocks[i]; for (size_t j = 0; j < b->header_count; j++) free(b->header[j]); free(b->header);
    for (size_t j = 0; j < b->field_count; j++) { free(b->fields[j].key); for (size_t k = 0; k < b->fields[j].value_count; k++) free(b->fields[j].values[k]); free(b->fields[j].values); } free(b->fields); }
  free(r->blocks); for (size_t i = 0; i < r->error_count; i++) free(r->errors[i].message); free(r->errors); memset(r, 0, sizeof *r);
}
bool qa_parse_blocks(qa_block_parse_result *out, const char *text) {
  memset(out, 0, sizeof *out); EVec errors = {0}; qa_block *blocks = NULL; size_t bn = 0, bc = 0;
  SVec pending = {0}; bool in_block = false; qa_block cur; memset(&cur, 0, sizeof cur);
  size_t total = strlen(text), pos = 0; int64_t line_no = 0, total_lines = 1; for (size_t i = 0; i < total; i++) if (text[i] == '\n') total_lines++;
  while (pos <= total) { size_t st = pos; while (pos < total && text[pos] != '\n') pos++; size_t len = stripped_len(text + st, pos - st); line_no++;
    SVec toks = tokenize(text + st, len, line_no, &errors);
    if (pos < total) pos++;
    if (toks.n == 0) { sfree(&toks); if (pos > total) break; if (st == total) break; continue; }
    if (!in_block) {
      bool has_open = false, has_close = false; size_t first_open = toks.n;
      for (size_t i = 0; i < toks.n; i++) { if (!strcmp(toks.v[i], "{")) { has_open = true; if (first_open == toks.n) first_open = i; } if (!strcmp(toks.v[i], "}")) has_close = true; }
      if (toks.n && !strcmp(toks.v[toks.n - 1], "{") && first_open == toks.n - 1 && !has_close) {
        memset(&cur, 0, sizeof cur); SVec hdr = {0};
        for (size_t i = 0; i < pending.n; i++) spush(&hdr, pending.v[i]);
        free(pending.v); pending = (SVec){0};
        for (size_t i = 0; i + 1 < toks.n; i++) spush(&hdr, toks.v[i]);
        if (toks.n) free(toks.v[toks.n - 1]);
        free(toks.v);
        cur.header = hdr.v; cur.header_count = hdr.n; cur.line = line_no; in_block = true;
      } else if (has_open || has_close) { char buf[64]; snprintf(buf, sizeof buf, "unexpected \"%s\" on header line", has_open ? "{" : "}"); epush(&errors, buf, line_no); sfree(&toks); }
      else { for (size_t i = 0; i < toks.n; i++) spush(&pending, toks.v[i]); free(toks.v); }
    } else {
      if (toks.n == 1 && !strcmp(toks.v[0], "}")) { sfree(&toks); if (bn == bc) { bc = bc ? bc * 2 : 8; blocks = realloc(blocks, bc * sizeof *blocks); } blocks[bn++] = cur; memset(&cur, 0, sizeof cur); in_block = false; }
      else { qa_block_field f; f.key = toks.v[0]; f.values = toks.n > 1 ? toks.v + 1 : NULL; f.value_count = toks.n - 1; f.line = line_no;
        /* values array must be separately allocated: copy pointers */
        if (f.value_count) { char **vs = malloc(f.value_count * sizeof *vs); memcpy(vs, toks.v + 1, f.value_count * sizeof *vs); f.values = vs; }
        free(toks.v);
        int depth = 0; for (size_t i = 0; i < f.value_count; i++) { if (!strcmp(f.values[i], "{")) depth++; else if (!strcmp(f.values[i], "}")) depth--; if (depth < 0) break; }
        if (!strcmp(f.key, "{") || !strcmp(f.key, "}") || depth != 0) { char buf[128]; snprintf(buf, sizeof buf, "unbalanced field braces in block opened at line %lld", (long long)cur.line); epush(&errors, buf, line_no); }
        cur.fields = realloc(cur.fields, (cur.field_count + 1) * sizeof *cur.fields); cur.fields[cur.field_count++] = f; }
    }
    if (st == total) break;
  }
  if (in_block) { epush(&errors, "unclosed block", cur.line);
    /* TS drops unclosed block from blocks; free it */ for (size_t j = 0; j < cur.header_count; j++) free(cur.header[j]); free(cur.header);
    for (size_t j = 0; j < cur.field_count; j++) { free(cur.fields[j].key); for (size_t k = 0; k < cur.fields[j].value_count; k++) free(cur.fields[j].values[k]); free(cur.fields[j].values); } free(cur.fields); }
  if (pending.n) epush(&errors, "header has no block", total_lines);
  sfree(&pending);
  out->blocks = blocks; out->block_count = bn; out->errors = errors.v; out->error_count = errors.n; return true;
}
bool qa_field_string(const char *const *v, size_t n, const char **out) { if (n != 1) return false; *out = v[0]; return true; }
bool qa_field_number(const char *const *v, size_t n, double *out) { if (n != 1 || !v[0][0]) return false;
  double x = qa_js_string_to_number(v[0]); /* Number(values[0]); strtod took "0x1p3" and "-0x10", missed "0b11" */
  if (!isfinite(x)) return false;
  *out = x; return true; }
bool qa_field_bool(const char *const *v, size_t n, bool *out) { if (n != 1) return false; if (!strcmp(v[0], "true")) { *out = true; return true; } if (!strcmp(v[0], "false")) { *out = false; return true; } return false; }
const char **qa_field_flag_list(const char *const *v, size_t n, size_t *out_count) { const char **r = malloc((n ? n : 1) * sizeof *r); size_t c = 0; for (size_t i = 0; i < n; i++) if (strcmp(v[i], "|")) r[c++] = v[i]; *out_count = c; return r; }
