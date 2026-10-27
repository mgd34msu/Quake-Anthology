/* Port of src/render/scene/debug-shapes.ts. GPL-2.0-or-later. */
#include "qa/render/scene/debug_shapes.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/view.h"
void qa_scene_debug_batches_destroy(qa_scene_debug_batches *d) {
  if (!d) return;
  if (d->vertices) for (size_t i = 0; i < d->count; i++) free(d->vertices[i]);
  if (d->indices) for (size_t i = 0; i < d->count; i++) free(d->indices[i]);
  free(d->vertices); free(d->indices); free(d->batches); memset(d, 0, sizeof *d);
}
typedef struct accum { qa_render_vertex *v; size_t nv, cv; double *idx; size_t ni, ci; } accum;
static void push_batch(qa_scene_debug_batches *out, size_t *cap, accum *a, bool depth_test,
                       qa_renderer_image image, double line_width) {
  if (!a->v || a->nv == 0) return;
  if (out->count == *cap) {
    *cap = *cap ? *cap * 2 : 8;
    out->batches = realloc(out->batches, *cap * sizeof *out->batches);
    out->vertices = realloc(out->vertices, *cap * sizeof *out->vertices);
    out->indices = realloc(out->indices, *cap * sizeof *out->indices);
    if (!out->batches || !out->vertices || !out->indices) qa_range_error("Allocation failed");
  }
  size_t k = out->count++;
  out->vertices[k] = a->v; out->indices[k] = a->idx;
  qa_draw_batch *b = &out->batches[k]; memset(b, 0, sizeof *b);
  b->primitive_is_lines = true; b->line_width = line_width;
  b->texturing_is_pair = false; b->lighting.kind = "vertex";
  b->vertices = a->v; b->vertex_count = a->nv;
  b->indices.values = a->idx; b->indices.count = a->ni;
  b->texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; b->texture.image = image;
  b->state.blend.source = "src-alpha"; b->state.blend.destination = "one-minus-src-alpha";
  b->state.depth_test = depth_test ? "less-equal" : "always";
  b->state.depth_write = false; b->state.alpha_test = "none"; b->state.cull = "none";
  b->state.depth_range[0] = 0; b->state.depth_range[1] = 1; b->state.polygon_offset = NULL;
  a->v = NULL; a->idx = NULL; a->nv = a->cv = a->ni = a->ci = 0;
}
qa_draw_batch *qa_prepare_debug_shapes(const qa_debug_line *lines, size_t count, qa_scene_camera camera,
                                       qa_renderer_image white_image, double line_width,
                                       qa_scene_debug_batches *out, size_t *out_count) {
  if (!isfinite(line_width) || line_width <= 0) qa_range_error("Invalid debug line width");
  memset(out, 0, sizeof *out);
  size_t cap = 0; accum a; memset(&a, 0, sizeof a);
  bool depth = false; int have_depth = 0;
  qa_scene_view_projector project = qa_scene_create_view_projector(camera);
  for (size_t i = 0; i < count; i++) {
    if (!have_depth || depth != lines[i].depth_test) push_batch(out, &cap, &a, depth, white_image, line_width);
    depth = lines[i].depth_test; have_depth = 1;
    qa_vec3 pts[2] = { lines[i].start, lines[i].end };
    for (int k = 0; k < 2; k++) {
      if (a.ni == a.ci) { a.ci = a.ci ? a.ci * 2 : 16; a.idx = realloc(a.idx, a.ci * sizeof *a.idx); if (!a.idx) qa_range_error("Allocation failed"); }
      a.idx[a.ni++] = (double)a.nv;
      if (a.nv == a.cv) { a.cv = a.cv ? a.cv * 2 : 16; a.v = realloc(a.v, a.cv * sizeof *a.v); if (!a.v) qa_range_error("Allocation failed"); }
      a.v[a.nv].position = qa_scene_projector_project(project, pts[k]);
      a.v[a.nv].color = lines[i].color;
      a.v[a.nv].tex_coord = qa_math_vec2(0, 0);
      a.nv++;
    }
  }
  push_batch(out, &cap, &a, depth, white_image, line_width);
  if (out_count) *out_count = out->count;
  return out->batches;
}
