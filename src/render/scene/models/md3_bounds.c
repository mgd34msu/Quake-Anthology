/* Port of src/render/scene/models/md3-bounds.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/md3_bounds.h"

#include <math.h>
#include <stdlib.h>

#include "qa/core/math.h"
#include "qa/render/scene/models/transform.h"

static double fr(double value) { return (double)(float)value; }

static bool finite_bounds(qa_bounds b) {
  return isfinite(b.min.x) && isfinite(b.min.y) && isfinite(b.min.z) &&
         isfinite(b.max.x) && isfinite(b.max.y) && isfinite(b.max.z) &&
         b.min.x <= b.max.x && b.min.y <= b.max.y && b.min.z <= b.max.z;
}

typedef struct envelope { qa_bounds bounds; size_t *counts; } envelope;

static bool frame_envelope(const qa_q3_mesh_model *model, double index,
                           envelope *out) {
  out->counts = calloc(model->surface_count ? model->surface_count : 1,
                       sizeof *out->counts);
  if (out->counts == NULL) return false;
  qa_bounds bounds = qa_empty_bounds();
  for (size_t s = 0; s < model->surface_count; s++) {
    const qa_q3_mesh_surface *surface = &model->surfaces[s];
    if (!(index >= 0) || trunc(index) != index ||
        (size_t)index >= surface->frame_count) {
      free(out->counts); out->counts = NULL; return false;
    }
    const qa_model_vertex *vertices = surface->frame_vertices[(size_t)index];
    size_t count = surface->frame_vertex_counts[(size_t)index];
    if (count > surface->texture_coordinate_count) {
      free(out->counts); out->counts = NULL; return false;
    }
    out->counts[s] = count;
    for (size_t i = 0; i < count; i++) {
      qa_vec3 point = vertices[i].position;
      if (!isfinite(point.x) || !isfinite(point.y) || !isfinite(point.z)) {
        free(out->counts); out->counts = NULL; return false;
      }
      bounds = qa_add_point_to_bounds(bounds, point);
    }
  }
  if (!finite_bounds(bounds)) {
    free(out->counts); out->counts = NULL; return false;
  }
  out->bounds = bounds;
  return true;
}

static double component(double old_v, double next, double old_scale,
                        double new_scale) {
  return fr(fr(fr(old_v * 64) * old_scale) + fr(fr(next * 64) * new_scale));
}

bool qa_md3_world_envelope(const qa_q3_mesh_model *model, double frame,
                           double previous_frame, double back_lerp,
                           qa_model_transform transform, qa_bounds *out) {
  envelope current = {0}, previous = {0};
  if (!frame_envelope(model, frame, &current) || !isfinite(back_lerp))
    return false;
  qa_bounds bounds = current.bounds;
  if (back_lerp != 0) {
    if (!frame_envelope(model, previous_frame, &previous)) {
      free(current.counts);
      return false;
    }
    bool differ = false;
    for (size_t i = 0; i < model->surface_count; i++)
      if (current.counts[i] != previous.counts[i]) differ = true;
    if (differ) { free(current.counts); free(previous.counts); return false; }
    double back = fr(back_lerp), front = fr(1 - back);
    double old_scale = fr((1.0 / 64) * back), new_scale = fr((1.0 / 64) * front);
    free(current.counts);
    if (!isfinite(old_scale) || !isfinite(new_scale)) {
      free(previous.counts);
      return false;
    }
    qa_vec3 old_min = old_scale < 0 ? previous.bounds.max : previous.bounds.min;
    qa_vec3 old_max = old_scale < 0 ? previous.bounds.min : previous.bounds.max;
    qa_vec3 new_min = new_scale < 0 ? current.bounds.max : current.bounds.min;
    qa_vec3 new_max = new_scale < 0 ? current.bounds.min : current.bounds.max;
    bounds = (qa_bounds){
        {component(old_min.x, new_min.x, old_scale, new_scale),
         component(old_min.y, new_min.y, old_scale, new_scale),
         component(old_min.z, new_min.z, old_scale, new_scale)},
        {component(old_max.x, new_max.x, old_scale, new_scale),
         component(old_max.y, new_max.y, old_scale, new_scale),
         component(old_max.z, new_max.z, old_scale, new_scale)}};
    free(previous.counts);
  } else {
    free(current.counts);
  }
  if (!finite_bounds(bounds)) return false;
  qa_bounds world = qa_model_world_bounds(transform, bounds);
  if (!finite_bounds(world)) return false;
  *out = world;
  return true;
}
