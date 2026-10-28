/* Port of src/content/q3/presentation/marks.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/marks.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
typedef struct stored {
  double time; const qa_q3_pres_scene_shader *shader; bool alpha_fade; qa_vec4 color;
  qa_q3_pres_ref_poly_vertex *vertices; size_t vertex_count;
} stored;
struct qa_q3_pres_impact_mark_system {
  qa_q3_pres_bsp_mark_projector *projector;
  qa_q3_pres_impact_mark_options options;
  stored *active; size_t count, capacity; /* newest first (TS unshift) */
};
static float fv(double v, const char *name) {
  float r = (float)v;
  if (!isfinite(r)) qa_range_error("%s must be finite float32", name);
  return r;
}
static qa_vec3 pos3(qa_vec3 v) { return (qa_vec3){fv(v.x, "mark coordinate"), fv(v.y, "mark coordinate"), fv(v.z, "mark coordinate")}; }
static double unit_color(double v) {
  float r = fv(v, "mark color");
  if (r < 0 || r > 1) qa_range_error("mark colors must be in [0, 1]");
  return r;
}
static double byte_of(double v) { return trunc((float)v) < 0 ? (double)((int64_t)trunc((float)v) & 255) : (double)((uint64_t)trunc((float)v) & 255); }
static double now_of(qa_q3_pres_impact_mark_system *s) {
  double t = s->options.clock(s->options.ctx);
  if (t != trunc(t) || t < -2147483648.0 || t > 2147483647.0)
    qa_range_error("mark clock must be signed 32-bit milliseconds");
  return t;
}
static bool grow_polys(qa_q3_pres_ref_poly **arr, size_t *cap, size_t need) {
  if (*cap >= need) return true;
  size_t nc = *cap ? *cap * 2 : 8;
  while (nc < need) nc *= 2;
  qa_q3_pres_ref_poly *np = realloc(*arr, nc * sizeof *np);
  if (!np) return false;
  *arr = np;
  *cap = nc;
  return true;
}
static bool make_poly(const qa_q3_pres_scene_shader *shader, const qa_q3_pres_ref_poly_vertex *verts,
                      size_t n, qa_q3_pres_ref_poly *out) {
  out->shader = shader;
  out->vertex_count = n;
  out->vertices = n ? malloc(n * sizeof *verts) : NULL;
  if (n && !out->vertices) return false;
  if (n) memcpy(out->vertices, verts, n * sizeof *verts);
  return true;
}
qa_q3_pres_impact_mark_system *qa_q3_pres_impact_mark_system_create(
    qa_q3_pres_bsp_mark_projector *projector, qa_q3_pres_impact_mark_options options) {
  qa_q3_pres_impact_mark_system *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->projector = projector;
  s->options = options;
  return s;
}
void qa_q3_pres_impact_mark_system_reset(qa_q3_pres_impact_mark_system *s) {
  for (size_t i = 0; i < s->count; i++) free(s->active[i].vertices);
  s->count = 0;
}
void qa_q3_pres_impact_mark_system_destroy(qa_q3_pres_impact_mark_system *s) {
  if (!s) return;
  qa_q3_pres_impact_mark_system_reset(s);
  free(s->active);
  free(s);
}
double qa_q3_pres_impact_mark_system_active_mark_count(qa_q3_pres_impact_mark_system *s) { return (double)s->count; }
bool qa_q3_pres_impact_mark_system_impact_mark(qa_q3_pres_impact_mark_system *s,
    qa_q3_pres_impact_mark_request request, qa_q3_pres_ref_poly **out_polys, size_t *out_count) {
  *out_polys = NULL;
  *out_count = 0;
  if (!s->options.enabled(s->options.ctx)) return true;
  double radius = fv(request.radius, "mark radius");
  if (radius <= 0) qa_range_error("CG_ImpactMark called with <= 0 radius");
  qa_vec3 origin = pos3(request.origin), direction = pos3(request.direction);
  if (qa_dot3(direction, direction) == 0) return true;
  qa_vec3 normal = qa_normalize3_or_zero(direction);
  qa_vec3 axis2 = qa_qvm_rotate_point_around_vector(normal, qa_perpendicular_vector(normal),
                                                     fv(request.orientation, "mark orientation"));
  qa_vec3 axis1 = qa_cross3(normal, axis2);
  double scale = (float)(0.5 / radius);
  /* TS point() staged binary32 arithmetic. */
  qa_vec3 corners[4];
  const double signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
  for (int i = 0; i < 4; i++) {
    double a = signs[i][0], b = signs[i][1];
    corners[i] = (qa_vec3){
        (float)(origin.x + (float)(a * (float)(radius * axis1.x))) + (float)(b * (float)(radius * axis2.x)),
        (float)(origin.y + (float)(a * (float)(radius * axis1.y))) + (float)(b * (float)(radius * axis2.y)),
        (float)(origin.z + (float)(a * (float)(radius * axis1.z))) + (float)(b * (float)(radius * axis2.z))};
  }
  qa_q3_pres_mark_fragments fragments;
  qa_q3_pres_mark_projection query = {corners, 4, qa_scale3(direction, -20),
                                      QA_Q3_PRES_MAX_MARK_POINTS, QA_Q3_PRES_MAX_MARK_FRAGMENTS};
  if (!qa_q3_pres_bsp_mark_projector_mark_fragments(s->projector, query, &fragments)) return false;
  qa_vec4 color = {unit_color(request.color.x), unit_color(request.color.y), unit_color(request.color.z),
                   unit_color(request.color.w)};
  qa_vec4 modulate = {byte_of(color.x * 255), byte_of(color.y * 255), byte_of(color.z * 255), byte_of(color.w * 255)};
  qa_q3_pres_ref_poly *temporary = NULL;
  size_t temp_count = 0, temp_cap = 0;
  for (size_t fi = 0; fi < fragments.fragment_count; fi++) {
    qa_q3_pres_mark_fragment frag = fragments.fragments[fi];
    size_t n = (size_t)fmin(frag.point_count, QA_Q3_PRES_MAX_MARK_POLY_VERTICES);
    qa_q3_pres_ref_poly_vertex verts[QA_Q3_PRES_MAX_MARK_POLY_VERTICES];
    for (size_t k = 0; k < n; k++) {
      qa_vec3 point = fragments.points[(size_t)frag.first_point + k];
      qa_vec3 delta = qa_sub3(point, origin);
      verts[k].position = point;
      verts[k].tex_coord = (qa_vec2){0.5 + (float)(qa_dot3(delta, axis1) * scale),
                                     0.5 + (float)(qa_dot3(delta, axis2) * scale)};
      verts[k].color = modulate;
    }
    if (request.temporary) {
      if (!grow_polys(&temporary, &temp_cap, temp_count + 1) ||
          !make_poly(request.shader, verts, n, &temporary[temp_count])) goto oom;
      temp_count++;
      continue;
    }
    if (s->count == QA_Q3_PRES_MAX_MARK_POLYS) {
      double oldest = s->active[s->count - 1].time;
      while (s->count && s->active[s->count - 1].time == oldest) {
        s->count--;
        free(s->active[s->count].vertices);
      }
    }
    if (s->count == s->capacity) {
      size_t nc = s->capacity ? s->capacity * 2 : 16;
      stored *np = realloc(s->active, nc * sizeof *np);
      if (!np) goto oom;
      s->active = np;
      s->capacity = nc;
    }
    memmove(s->active + 1, s->active, s->count * sizeof *s->active);
    stored *entry = &s->active[0];
    entry->time = now_of(s);
    entry->shader = request.shader;
    entry->alpha_fade = request.alpha_fade;
    entry->color = color;
    entry->vertex_count = n;
    entry->vertices = n ? malloc(n * sizeof *verts) : NULL;
    if (n && !entry->vertices) {
      memmove(s->active, s->active + 1, s->count * sizeof *s->active);
      goto oom;
    }
    if (n) memcpy(entry->vertices, verts, n * sizeof *verts);
    s->count++;
  }
  qa_q3_pres_mark_fragments_destroy(&fragments);
  *out_polys = temporary;
  *out_count = temp_count;
  return true;
oom:
  for (size_t i = 0; i < temp_count; i++) qa_q3_pres_ref_poly_destroy(&temporary[i]);
  free(temporary);
  qa_q3_pres_mark_fragments_destroy(&fragments);
  return false;
}
static void fade_rgb(stored *mark, double fade) {
  double r = byte_of(mark->color.x * fade), g = byte_of(mark->color.y * fade), b = byte_of(mark->color.z * fade);
  for (size_t i = 0; i < mark->vertex_count; i++) {
    mark->vertices[i].color.x = r;
    mark->vertices[i].color.y = g;
    mark->vertices[i].color.z = b;
  }
}
bool qa_q3_pres_impact_mark_system_add_marks(qa_q3_pres_impact_mark_system *s,
    qa_q3_pres_ref_poly **out_polys, size_t *out_count) {
  *out_polys = NULL;
  *out_count = 0;
  if (!s->options.enabled(s->options.ctx)) return true;
  double now = now_of(s);
  qa_q3_pres_ref_poly *output = NULL;
  size_t count = 0, cap = 0;
  for (size_t index = 0; index < s->count;) {
    stored *mark = &s->active[index];
    double expires = (double)qa_int32(mark->time + QA_Q3_PRES_MARK_TOTAL_TIME);
    if (now > expires) {
      free(mark->vertices);
      memmove(s->active + index, s->active + index + 1, (s->count - index - 1) * sizeof *s->active);
      s->count--;
      continue;
    }
    if (mark->shader == s->options.energy_shader(s->options.ctx)) {
      double age = (float)qa_int32(now - mark->time);
      double fade = trunc((float)(450 - (float)(450 * (float)(age / 3000))));
      if (fade < 255 && mark->vertex_count && mark->vertices[0].color.x != 0)
        fade_rgb(mark, fmax(0, fade));
    }
    double remaining = (double)qa_int32(expires - now);
    if (remaining < QA_Q3_PRES_MARK_FADE_TIME) {
      double fade = trunc((double)((int32_t)(255 * (int32_t)remaining)) / QA_Q3_PRES_MARK_FADE_TIME);
      if (mark->alpha_fade) {
        for (size_t k = 0; k < mark->vertex_count; k++)
          mark->vertices[k].color.w = (double)((int64_t)fade & 255);
      } else fade_rgb(mark, fade);
    }
    if (!grow_polys(&output, &cap, count + 1) || !make_poly(mark->shader, mark->vertices, mark->vertex_count, &output[count])) {
      for (size_t i = 0; i < count; i++) qa_q3_pres_ref_poly_destroy(&output[i]);
      free(output);
      return false;
    }
    count++;
    index++;
  }
  *out_polys = output;
  *out_count = count;
  return true;
}
static bool view_impact(void *ctx, qa_q3_pres_impact_mark_request request,
                        qa_q3_pres_ref_poly **out, size_t *count) {
  return qa_q3_pres_impact_mark_system_impact_mark(ctx, request, out, count);
}
qa_q3_pres_impact_mark_system_view qa_q3_pres_impact_mark_system_as_view(qa_q3_pres_impact_mark_system *s) {
  qa_q3_pres_impact_mark_system_view view = {s, view_impact};
  return view;
}
