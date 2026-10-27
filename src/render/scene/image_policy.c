/* Port of src/render/scene/image-policy.ts. GPL-2.0-or-later. */
#include "qa/render/scene/image_policy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/common_parse.h"
static const qa_scene_image_format FMTS[6] = { "png", "jpg", "tga", "jpeg", "bmp", "gif" };
static const struct { qa_scene_image_usage usage; int bit; } BITS[5] = { {"skin",1},{"sprite",2},{"wall",4},{"picture",8},{"sky",16} };
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
const qa_scene_image_format *qa_parse_image_formats(const char *value, size_t *out_count) {
  qa_scene_image_format *result = NULL; size_t n = 0, cap = 0;
  qa_legacy_parse_state st = { value, strlen(value), 0 };
  while (st.index < st.length) {
    size_t start = st.index; char *tok = qa_parse_q2_token(&st, 1024);
    if (!tok) break;
    if (st.index == start) { free(tok); break; }
    for (char *p = tok; *p; p++) *p = lower(*p);
    qa_scene_image_format exact = NULL;
    for (int i = 0; i < 6; i++) if (!strcmp(tok, FMTS[i])) exact = FMTS[i];
    if (exact) { int seen = 0; for (size_t i = 0; i < n; i++) if (result[i] == exact) seen = 1;
      if (!seen) { if (n == cap) { cap = cap ? cap * 2 : 8; result = realloc(result, cap * sizeof *result); } result[n++] = exact; } free(tok); continue; }
    for (const char *p = tok; *p; p++) for (int i = 0; i < 6; i++) if (FMTS[i][0] == *p) {
      int seen = 0; for (size_t k = 0; k < n; k++) if (result[k] == FMTS[i]) seen = 1;
      if (!seen) { if (n == cap) { cap = cap ? cap * 2 : 8; result = realloc(result, cap * sizeof *result); } result[n++] = FMTS[i]; } break;
    }
    free(tok);
  }
  if (out_count) *out_count = n;
  return result;
}
void qa_scene_image_policy_owned_destroy(qa_scene_image_policy_owned *o) { if (!o) return; free(o->usages); free(o->formats); o->usages = NULL; o->formats = NULL; }
bool qa_snapshot_image_policy(qa_scene_image_policy p, qa_scene_image_policy_owned *out) {
  out->usages = NULL; out->formats = NULL;
  if (p.override_usage_count) { out->usages = malloc(p.override_usage_count * sizeof *out->usages); if (!out->usages) return false; memcpy(out->usages, p.override_usages, p.override_usage_count * sizeof *out->usages); }
  if (p.has_formats && p.format_count) { out->formats = malloc(p.format_count * sizeof *out->formats); if (!out->formats) { free(out->usages); return false; } memcpy(out->formats, p.formats, p.format_count * sizeof *out->formats); }
  out->policy = p; out->policy.override_usages = out->usages; out->policy.formats = out->formats;
  return true;
}
static int32_t i32d(double v) { if (!isfinite(v)) return 0; double m = fmod(trunc(v), 4294967296.0); if (m < 0) m += 4294967296.0; if (m >= 2147483648.0) m -= 4294967296.0; return (int32_t)m; }
bool qa_image_policy_from_controls(qa_scene_image_controls c, qa_scene_image_policy_owned *out) {
  qa_scene_image_usage usages[5]; size_t nu = 0;
  int mask = i32d(c.override_mask);
  for (int i = 0; i < 5; i++) if (mask & BITS[i].bit) usages[nu++] = BITS[i].usage;
  /* TS trim().toLowerCase() === "source" (ASCII for format names). */
  const char *f = c.formats; while (*f == ' ' || *f == '\t' || *f == '\n' || *f == '\r') f++;
  size_t len = strlen(f); while (len && (f[len-1]==' '||f[len-1]=='\t'||f[len-1]=='\n'||f[len-1]=='\r')) len--;
  int is_source = len == 6; if (is_source) { const char *w = "source"; for (int i = 0; i < 6; i++) if (lower(f[i]) != w[i]) is_source = 0; }
  qa_scene_image_policy p = { 0 }; p.override_level = c.override_level; p.override_usages = usages; p.override_usage_count = nu;
  size_t nf = 0; const qa_scene_image_format *fmts = NULL;
  if (!is_source) { p.has_formats = true; fmts = qa_parse_image_formats(c.formats, &nf); p.formats = fmts; p.format_count = nf; }
  bool ok = qa_snapshot_image_policy(p, out);
  free((void *)fmts);
  return ok;
}
qa_scene_image_policy qa_default_image_policy(void) {
  static const qa_scene_image_usage u[5] = { "skin", "sprite", "wall", "picture", "sky" };
  qa_scene_image_policy p = { 1, u, 5, true, FMTS, 6 };
  return p;
}
