/* Port of src/app/bootstrap/q2-damage-blend.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_damage_blend.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/js.h"
static float fr(double v) { return (float)v; }
static double component(double before, double after, double fraction) {
  return fr(before + fr(fr(after - before) * fraction));
}
qa_vec4 qa_app_interpolate_q2_damage_blend(const qa_vec4 *previous, qa_vec4 current, double fraction) {
  if (previous == NULL || previous->w == 0) return current;
  qa_vec4 out = {component(previous->x, current.x, fraction), component(previous->y, current.y, fraction),
                 component(previous->z, current.z, fraction), component(previous->w, current.w, fraction)};
  return out;
}
static double byte_of(double value) { return (double)(qa_js_to_int32(trunc(fr(value * 255))) & 255) / 255.0; } /* Math.trunc(...) & 255 */
static qa_render_vertex vertex(double x, double y, double width, double height, qa_vec4 color) {
  qa_render_vertex v;
  memset(&v, 0, sizeof v);
  v.position.x = 2 * x / width - 1; v.position.y = 1 - 2 * y / height; v.position.z = 0; v.position.w = 1;
  v.tex_coord.x = 0; v.tex_coord.y = 0; v.color = color;
  return v;
}
size_t qa_app_prepare_q2_damage_blend(qa_vec4 blend, qa_rect viewport, qa_renderer_image white,
    double fraction, qa_draw_batch *out, qa_render_vertex *vertices, double *indices) {
  if (blend.w == 0 || viewport.width <= 0 || viewport.height <= 0) return 0;
  qa_vec4 outer = {byte_of(blend.x), byte_of(blend.y), byte_of(blend.z), byte_of(blend.w)};
  qa_vec4 inner = outer; inner.w = 0;
  double width = viewport.width, height = viewport.height;
  vertices[0] = vertex(0, 0, width, height, outer);
  vertices[1] = vertex(width, 0, width, height, outer);
  vertices[2] = vertex(width, height, width, height, outer);
  vertices[3] = vertex(0, height, width, height, outer);
  size_t vertex_count = 4, index_count = 6;
  static const double base_idx[6] = {0, 1, 2, 0, 2, 3};
  memcpy(indices, base_idx, sizeof base_idx);
  if (fraction > 0) {
    double distance = trunc(fr(qa_js_min(width, height) * fr(qa_js_min(fraction, 0.5))));
    vertices[4] = vertex(distance, distance, width, height, inner);
    vertices[5] = vertex(width - distance, distance, width, height, inner);
    vertices[6] = vertex(width - distance, height - distance, width, height, inner);
    vertices[7] = vertex(distance, height - distance, width, height, inner);
    vertex_count = 8;
    static const double idx[24] = {0,5,4, 0,1,5, 1,6,5, 1,2,6, 6,2,3, 6,3,7, 0,7,3, 0,4,7};
    memcpy(indices, idx, sizeof idx);
    index_count = 24;
  }
  memset(out, 0, sizeof *out);
  out->indices.values = indices; out->indices.count = index_count;
  out->texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; out->texture.image = white;
  out->state.blend.source = "src-alpha"; out->state.blend.destination = "one-minus-src-alpha";
  out->state.depth_test = "always"; out->state.depth_write = false;
  out->state.alpha_test = "none"; out->state.cull = "none";
  out->state.depth_range[0] = 0; out->state.depth_range[1] = 1; out->state.polygon_offset = NULL;
  out->lighting.kind = "vertex";
  out->primitive_is_lines = false; out->texturing_is_pair = false;
  out->vertices = vertices; out->vertex_count = vertex_count;
  return 1;
}
