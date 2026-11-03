/* Port of src/render/scene/q2-sky.ts. GPL-2.0-or-later. */
#include "qa/render/scene/q2_sky.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/materials/sky.h"
typedef struct fb { double min_s, min_t, max_s, max_t; } fb;
static const qa_vec3 PLANES[6] = { {1,1,0},{1,-1,0},{0,-1,1},{0,1,1},{1,0,1},{-1,0,1} };
static float fr(double v) { return (float)v; }
static void project_polygon(const qa_vec3 *points, size_t n, fb *bounds) {
  qa_vec3 sum = { 0, 0, 0 };
  for (size_t i = 0; i < n; i++) sum = qa_add3(sum, points[i]);
  double x = fabs(sum.x), y = fabs(sum.y), z = fabs(sum.z);
  int face = (x > y && x > z) ? (sum.x < 0 ? 1 : 0) : (y > z && y > x) ? (sum.y < 0 ? 3 : 2) : (sum.z < 0 ? 5 : 4);
  fb *range = &bounds[face];
  for (size_t i = 0; i < n; i++) {
    qa_vec3 point = points[i]; double divisor, s, t;
    switch (face) {
      case 0: divisor = point.x; s = -point.y; t = point.z; break;
      case 1: divisor = -point.x; s = point.y; t = point.z; break;
      case 2: divisor = point.y; s = point.x; t = point.z; break;
      case 3: divisor = -point.y; s = -point.x; t = point.z; break;
      case 4: divisor = point.z; s = -point.y; t = -point.x; break;
      default: divisor = -point.z; s = -point.y; t = point.x; break;
    }
    if (divisor < 0.001) continue;
    s = fr(s / divisor); t = fr(t / divisor);
    range->min_s = qa_js_min(range->min_s, s); range->min_t = qa_js_min(range->min_t, t); /* Math.min/max keep NaN */
    range->max_s = qa_js_max(range->max_s, s); range->max_t = qa_js_max(range->max_t, t);
  }
}
static void clip_polygon(const qa_vec3 *points, size_t n, int stage, fb *bounds) {
  if (n > 62) qa_range_error("Q2 sky polygon exceeds MAX_CLIP_VERTS");
  if (stage == 6) { project_polygon(points, n, bounds); return; }
  qa_vec3 normal = PLANES[stage];
  double *distances = malloc((n ? n : 1) * sizeof(double));
  int *sides = malloc((n ? n : 1) * sizeof(int));
  if (!distances || !sides) qa_range_error("Allocation failed");
  int has_pos = 0, has_neg = 0;
  for (size_t i = 0; i < n; i++) {
    distances[i] = qa_dot3(points[i], normal);
    sides[i] = distances[i] > 0.1 ? 1 : distances[i] < -0.1 ? -1 : 0;
    if (sides[i] == 1) has_pos = 1;
    if (sides[i] == -1) has_neg = 1;
  }
  if (!has_pos || !has_neg) { free(distances); free(sides); clip_polygon(points, n, stage + 1, bounds); return; }
  qa_vec3 *front = malloc((n * 2 + 2) * sizeof(qa_vec3)), *back = malloc((n * 2 + 2) * sizeof(qa_vec3));
  if (!front || !back) qa_range_error("Allocation failed");
  size_t nf = 0, nb = 0;
  for (size_t i = 0; i < n; i++) {
    size_t next = (i + 1) % n; int side = sides[i], next_side = sides[next];
    if (side >= 0) front[nf++] = points[i];
    if (side <= 0) back[nb++] = points[i];
    if (side == 0 || next_side == 0 || side == next_side) continue;
    double fraction = fr(distances[i] / fr(distances[i] - distances[next]));
    qa_vec3 is = qa_add3(points[i], qa_scale3(qa_sub3(points[next], points[i]), fraction));
    front[nf++] = is; back[nb++] = is;
  }
  free(distances); free(sides);
  clip_polygon(front, nf, stage + 1, bounds); clip_polygon(back, nb, stage + 1, bounds);
  free(front); free(back);
}
void qa_q2_sky_operations_destroy(qa_q2_sky_operations *ops) {
  if (!ops) return;
  free(ops->strips); free(ops->vertices); free(ops->operations); memset(ops, 0, sizeof *ops);
}
static double coord_clamp(double seam, double v) { double c = (v + 1) * 0.5; return c < seam ? seam : (c > 1 - seam ? 1 - seam : c); }
qa_render_operation *qa_q2_sky_sides(qa_material_geometry geometry, qa_vec3 origin, qa_q2_sky_view sky,
                                     double seconds, qa_scene_project_fn project, void *project_ctx,
                                     qa_q2_sky_operations *out, size_t *out_count) {
  memset(out, 0, sizeof *out);
  /* TS plain Error -> qa_range_error (identical observable abort). */
  if (sky.image_count != 6) qa_range_error("Q2 sky requires six registered images");
  fb bounds[6];
  for (int i = 0; i < 6; i++) bounds[i] = (fb){ 9999, 9999, -9999, -9999 };
  for (size_t index = 0; index + 2 < geometry.index_count + ((geometry.index_count % 3) ? 3 : 0) && index < geometry.index_count; index += 3) {
    qa_vec3 tri[3];
    for (int o = 0; o < 3; o++) {
      if (index + (size_t)o >= geometry.index_count) qa_range_error("Q2 sky index %zu outside %zu", index + (size_t)o, geometry.index_count);
      double vi = geometry.indices[index + (size_t)o];
      if (!(vi >= 0 && vi == floor(vi) && vi < (double)geometry.vertex_count))
        qa_range_error("Q2 sky index %s outside %zu", QA_JS_NUMBER(vi), geometry.vertex_count);
      tri[o] = qa_sub3(geometry.vertices[(size_t)vi].position, origin);
    }
    clip_polygon(tri, 3, 0, bounds);
  }
  int any = 0;
  for (int i = 0; i < 6; i++) if (bounds[i].min_s < bounds[i].max_s && bounds[i].min_t < bounds[i].max_t) any = 1;
  if (!any) { if (out_count) *out_count = 0; return NULL; }
  double angle = sky.auto_rotate ? seconds * sky.rotation : sky.rotation;
  qa_vec3 axis = qa_normalize3_or_zero(sky.axis);
  int rotate = angle != 0 && qa_dot3(axis, axis) != 0;
  double seam = sky.rotation != 0 ? 1.0 / 256 : 1.0 / 512;
  out->operations = malloc(6 * sizeof *out->operations);
  out->strips = malloc(6 * sizeof *out->strips);
  out->vertices = malloc(6 * 4 * sizeof *out->vertices);
  if (!out->operations || !out->strips || !out->vertices) qa_range_error("Allocation failed");
  out->strip_capacity = 6;
  size_t nop = 0;
  for (int face = 0; face < 6; face++) {
    fb range = sky.rotation != 0 ? (fb){ -1, -1, 1, 1 } : bounds[face];
    if (!(range.min_s < range.max_s && range.min_t < range.max_t)) continue;
    double ss[4] = { range.min_s, range.min_s, range.max_s, range.max_s };
    double tt[4] = { range.min_t, range.max_t, range.min_t, range.max_t };
    qa_vertex_position_tex_coord *vs = out->vertices + nop * 4;
    for (int k = 0; k < 4; k++) {
      qa_vec3 point = qa_sky_vector(face, ss[k], tt[k], 2300);
      qa_vec3 direction = rotate ? qa_rotate_point_around_vector(axis, point, angle) : point;
      vs[k].position = project(project_ctx, qa_add3(origin, direction));
      vs[k].tex_coord = qa_math_vec2(coord_clamp(seam, ss[k]), 1 - coord_clamp(seam, tt[k]));
    }
    out->strips[nop].vertices = vs; out->strips[nop].vertex_count = 4;
    qa_render_operation *op = &out->operations[nop]; memset(op, 0, sizeof *op);
    op->kind = QA_RENDER_OP_SKY_SIDE; op->image = sky.images[face];
    op->color = qa_math_vec4(1, 1, 1, 1);
    op->strips = &out->strips[nop]; op->strip_count = 1;
    nop++;
  }
  out->count = nop;
  if (out_count) *out_count = nop;
  return out->operations;
}
