/* Port of src/materials/q2-lightgrid.ts. GPL-2.0-or-later. */
#include <math.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/materials/q2_lightgrid.h"
#include "qa/materials/shader_types.h"

static float fr(double v) { return (float)v; }
static int32_t imul(double a, double b) {
  return (int32_t)(qa_mat_to_uint32(a) * qa_mat_to_uint32(b));
}

bool qa_mat_lookup_q2_lightgrid(qa_q2_lightgrid grid, qa_vec3 point,
    const qa_q2_lightgrid_sample **out, size_t *out_count) {
  qa_q2_lightgrid_child child = grid.root;
  while (child.kind != QA_Q2_LIGHTGRID_OCCLUDED) {
    if (child.kind == QA_Q2_LIGHTGRID_LEAF) {
      int64_t leaf_index = (int64_t)child.index;
      if (leaf_index < 0 || (size_t)leaf_index >= grid.leaf_count) return false;
      qa_q2_lightgrid_leaf leaf = grid.leaves[leaf_index];
      uint32_t x = qa_mat_to_uint32(point.x - leaf.min.x);
      uint32_t y = qa_mat_to_uint32(point.y - leaf.min.y);
      uint32_t z = qa_mat_to_uint32(point.z - leaf.min.z);
      uint32_t inner = (uint32_t)imul(leaf.size.y, (double)z) + y;
      uint32_t index = (uint32_t)imul(leaf.size.x, (double)inner) + x;
      if ((double)index >= leaf.point_count) return false;
      double start_d = leaf.first_sample + (double)index * grid.style_count;
      /* Array.slice truncates toward zero and clamps to length. */
      int64_t start = (int64_t)trunc(start_d);
      int64_t end = (int64_t)trunc(start_d + grid.style_count);
      if (start < 0) start = 0;
      if (end < start) end = start;
      if ((size_t)start > grid.sample_count) start = (int64_t)grid.sample_count;
      if ((size_t)end > grid.sample_count) end = (int64_t)grid.sample_count;
      *out = grid.samples + start;
      *out_count = (size_t)(end - start);
      return true;
    }
    int64_t node_index = (int64_t)child.index;
    if (node_index < 0 || (size_t)node_index >= grid.node_count) return false;
    qa_q2_lightgrid_node node = grid.nodes[node_index];
    int index = (point.x >= node.point.x ? 4 : 0) |
                (point.y >= node.point.y ? 2 : 0) |
                (point.z >= node.point.z ? 1 : 0);
    child = node.children[index];
  }
  return false;
}

qa_vec3 qa_adjust_q2_lighting(qa_vec3 color,
                              qa_q2_lighting_adjustment adjustment) {
  double r = fr(fr(color.x + adjustment.add) * adjustment.modulate);
  double g = fr(fr(color.y + adjustment.add) * adjustment.modulate);
  double b = fr(fr(color.z + adjustment.add) * adjustment.modulate);
  r = r > 0 ? r : 0; g = g > 0 ? g : 0; b = b > 0 ? b : 0;
  qa_vec3 rgb = {r, g, b};
  if (adjustment.saturation == 1) return qa_scale3(rgb, fr(1.0 / 255));
  double luminance = fr(fr(fr(r * fr(0.2126)) + fr(g * fr(0.7152))) +
                        fr(b * fr(0.0722)));
  qa_vec3 adjusted = {luminance + fr(fr(r - luminance) * adjustment.saturation),
      luminance + fr(fr(g - luminance) * adjustment.saturation),
      luminance + fr(fr(b - luminance) * adjustment.saturation)};
  return qa_scale3(adjusted, fr(1.0 / 255));
}

static qa_vec3 interpolate(qa_vec3 a, qa_vec3 b, double fraction) {
  return qa_add3(qa_scale3(a, fr(1 - fraction)), qa_scale3(b, fraction));
}

bool qa_q2_light_grid_point(qa_q2_lightgrid grid, qa_vec3 position,
    const qa_q2_light_style *styles, size_t style_count,
    const qa_q2_lighting_adjustment *adjustment, qa_vec3 *out) {
  qa_q2_lighting_adjustment adj = {0, 1, 1};
  if (adjustment != NULL) adj = *adjustment;
  if (grid.leaf_count == 0) return false;
  qa_vec3 point = {(position.x - grid.min.x) * grid.scale.x,
                   (position.y - grid.min.y) * grid.scale.y,
                   (position.z - grid.min.z) * grid.scale.z};
  uint32_t bx = qa_mat_to_uint32(trunc(point.x));
  uint32_t by = qa_mat_to_uint32(trunc(point.y));
  uint32_t bz = qa_mat_to_uint32(trunc(point.z));
  qa_vec3 corners[8];
  bool valid_corner[8];
  qa_vec3 average = {0, 0, 0};
  int count = 0;
  for (int index = 0; index < 8; index++) {
    qa_vec3 at = {(double)(uint32_t)(bx + (uint32_t)(index & 1)),
                  (double)(uint32_t)(by + (uint32_t)((index >> 1) & 1)),
                  (double)(uint32_t)(bz + (uint32_t)((index >> 2) & 1))};
    const qa_q2_lightgrid_sample *samples = NULL;
    size_t sample_count = 0;
    qa_vec3 color = {0, 0, 0};
    bool valid = false;
    if (qa_mat_lookup_q2_lightgrid(grid, at, &samples, &sample_count)) {
      for (size_t s = 0; s < sample_count; s++) {
        if (samples[s].style == 255) break;
        int64_t si = (int64_t)samples[s].style;
        if (si < 0 || (size_t)si >= style_count) break;
        color = qa_add3(color, qa_scale3(samples[s].rgb, styles[si].rgb.x));
        valid = true;
      }
    }
    valid_corner[index] = valid;
    corners[index] = color;
    if (valid) { count++; average = qa_add3(average, color); }
  }
  if (count == 0) return false;
  average = qa_scale3(average, fr(1.0 / count));
  qa_vec3 values[8];
  for (int i = 0; i < 8; i++) values[i] = valid_corner[i] ? corners[i] : average;
  double fx = fr(point.x - (double)bx), fy = fr(point.y - (double)by),
         fz = fr(point.z - (double)bz);
  qa_vec3 bottom = interpolate(interpolate(values[0], values[1], fx),
                               interpolate(values[2], values[3], fx), fy);
  qa_vec3 top = interpolate(interpolate(values[4], values[5], fx),
                            interpolate(values[6], values[7], fx), fy);
  *out = qa_adjust_q2_lighting(interpolate(bottom, top, fz), adj);
  return true;
}
