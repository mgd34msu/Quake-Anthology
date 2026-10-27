/* Port of src/materials/dlight.ts. GPL-2.0-or-later. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/materials/dlight.h"

static float fr(double v) { return (float)v; }
static bool eq(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}
static int32_t bit_for(size_t index) {
  /* JS 1 << index masks the shift count to 5 bits. */
  return (int32_t)(1u << (index & 31));
}

qa_q3_dynamic_light *qa_transform_dlights(const qa_q3_dynamic_light *lights,
    size_t light_count, qa_vec3 origin, qa_axis axis) {
  qa_q3_dynamic_light *out = malloc(
      (light_count > 0 ? light_count : 1) * sizeof *out);
  if (out == NULL) qa_range_error("dlight allocation failed");
  for (size_t i = 0; i < light_count; i++) {
    out[i] = lights[i];
    qa_vec3 relative = qa_sub3(lights[i].origin, origin);
    out[i].origin = qa_math_vec3(qa_dot3(relative, axis.vectors[0]),
                                 qa_dot3(relative, axis.vectors[1]),
                                 qa_dot3(relative, axis.vectors[2]));
  }
  return out;
}

qa_dlight_mask_pair qa_split_dlight_mask(const qa_q3_dynamic_light *lights,
    size_t light_count, int32_t mask, qa_plane plane) {
  qa_dlight_mask_pair out = {0, 0};
  if (mask == 0) return out;
  for (size_t i = 0; i < light_count; i++) {
    int32_t bit = bit_for(i);
    if ((mask & bit) == 0) continue;
    double distance = fr(qa_dot3(lights[i].origin, plane.normal) -
                         plane.distance);
    if (distance > -lights[i].radius) out.front |= bit;
    if (distance < lights[i].radius) out.back |= bit;
  }
  return out;
}

int32_t qa_face_dlight_mask(const qa_q3_dynamic_light *lights,
    size_t light_count, int32_t mask, qa_plane plane) {
  for (size_t i = 0; i < light_count; i++) {
    int32_t bit = bit_for(i);
    if ((mask & bit) == 0) continue;
    double distance = fr(qa_dot3(lights[i].origin, plane.normal) -
                         plane.distance);
    if (distance < -lights[i].radius || distance > lights[i].radius)
      mask &= ~bit;
  }
  return mask;
}

int32_t qa_grid_dlight_mask(const qa_q3_dynamic_light *lights,
    size_t light_count, int32_t mask, qa_bounds bounds) {
  for (size_t i = 0; i < light_count; i++) {
    int32_t bit = bit_for(i);
    if ((mask & bit) == 0) continue;
    qa_vec3 origin = lights[i].origin;
    double radius = lights[i].radius;
    if (fr(origin.x - radius) > bounds.max.x ||
        fr(origin.x + radius) < bounds.min.x ||
        fr(origin.y - radius) > bounds.max.y ||
        fr(origin.y + radius) < bounds.min.y ||
        fr(origin.z - radius) > bounds.max.z ||
        fr(origin.z + radius) < bounds.min.z)
      mask &= ~bit;
  }
  return mask;
}

int32_t qa_bmodel_dlight_mask(const qa_q3_dynamic_light *lights,
    size_t light_count, qa_bounds bounds) {
  int32_t mask = 0;
  for (size_t i = 0; i < light_count; i++) {
    qa_vec3 origin = lights[i].origin;
    double radius = lights[i].radius;
    if (fr(origin.x - bounds.max.x) > radius ||
        fr(bounds.min.x - origin.x) > radius ||
        fr(origin.y - bounds.max.y) > radius ||
        fr(bounds.min.y - origin.y) > radius ||
        fr(origin.z - bounds.max.z) > radius ||
        fr(bounds.min.z - origin.z) > radius)
      continue;
    mask |= bit_for(i);
  }
  return mask;
}

bool qa_receives_projected_dlights(const qa_mat_compiled_material *material) {
  if (material->finished.sort > 3) return false;
  const char *iterator = material->finished.iterator.kind;
  if (!eq(iterator, "generic") && !eq(iterator, "sky")) return true;
  qa_string_view_list parms = material->registered.definition.surface_parms;
  for (size_t i = 0; i < parms.count; i++)
    if (eq(parms.values[i], "nodlight") || eq(parms.values[i], "sky"))
      return false;
  return true;
}

qa_draw_batch *qa_project_dlight_texture(qa_deform_geometry geometry,
    int32_t mask, const qa_q3_dynamic_light *lights, size_t light_count,
    qa_renderer_image image, qa_mat_project_fn project, const char *cull,
    size_t *out_count) {
  qa_draw_batch *batches = NULL;
  size_t count = 0, capacity = 0;
  for (size_t li = 0; li < light_count; li++) {
    if ((mask & bit_for(li)) == 0) continue;
    qa_q3_dynamic_light light = lights[li];
    double radius = light.radius;
    double scale = fr(1.0 / radius);
    qa_vec3 color = qa_math_vec3(light.color.x * 255, light.color.y * 255,
                                 light.color.z * 255);
    size_t vcount = geometry.vertex_count;
    int *clip_bits = malloc((vcount > 0 ? vcount : 1) * sizeof *clip_bits);
    qa_render_vertex *vertices = malloc(
        (vcount > 0 ? vcount : 1) * sizeof *vertices);
    if (clip_bits == NULL || vertices == NULL)
      qa_range_error("dlight allocation failed");
    for (size_t vi = 0; vi < vcount; vi++) {
      qa_vec3 distance = qa_sub3(light.origin, geometry.vertices[vi].position);
      qa_vec2 tex = {fr(0.5 + fr(distance.x * scale)),
                     fr(0.5 + fr(distance.y * scale))};
      int clip = (tex.x < 0 ? 1 : tex.x > 1 ? 2 : 0) |
                 (tex.y < 0 ? 4 : tex.y > 1 ? 8 : 0);
      double modulate;
      if (distance.z > radius) {
        clip |= 16; modulate = 0;
      } else if (distance.z < -radius) {
        clip |= 32; modulate = 0;
      } else {
        double height = fabs(distance.z);
        modulate = height < fr(radius * 0.5)
            ? 1 : fr(fr(2.0 * fr(radius - height)) * scale);
      }
      clip_bits[vi] = clip;
      double comps[3] = {color.x, color.y, color.z};
      double bytes[3];
      for (int c = 0; c < 3; c++)
        bytes[c] = (double)(qa_mat_to_int32(fr(comps[c] * modulate)) & 255) / 255;
      vertices[vi].position = project.project(project.ctx,
                                              geometry.vertices[vi].position);
      vertices[vi].tex_coord = tex;
      vertices[vi].color = (qa_vec4){bytes[0], bytes[1], bytes[2], 1};
    }
    double *indices = malloc(
        (geometry.index_count > 0 ? geometry.index_count : 1) *
        sizeof *indices);
    if (indices == NULL) qa_range_error("dlight allocation failed");
    size_t index_count = 0;
    for (size_t offset = 0; offset < geometry.index_count; offset += 3) {
      if (offset + 2 >= geometry.index_count)
        qa_range_error("dlight triangle indices are incomplete");
      double a = geometry.indices[offset], b = geometry.indices[offset + 1],
             c = geometry.indices[offset + 2];
      int64_t ai = (int64_t)a, bi = (int64_t)b, ci = (int64_t)c;
      if (ai < 0 || (size_t)ai >= vcount || bi < 0 || (size_t)bi >= vcount ||
          ci < 0 || (size_t)ci >= vcount)
        qa_range_error("ProjectDlightTexture: triangle has no active clipBits entry; inactive source scratch is indeterminate");
      if ((clip_bits[ai] & clip_bits[bi] & clip_bits[ci]) == 0) {
        indices[index_count++] = a;
        indices[index_count++] = b;
        indices[index_count++] = c;
      }
    }
    free(clip_bits);
    if (index_count == 0) {
      free(vertices);
      free(indices);
      continue;
    }
    if (count == capacity) {
      capacity = capacity == 0 ? 4 : capacity * 2;
      batches = realloc(batches, capacity * sizeof *batches);
      if (batches == NULL) qa_range_error("dlight allocation failed");
    }
    qa_draw_batch *batch = &batches[count++];
    memset(batch, 0, sizeof *batch);
    batch->indices.values = indices;
    batch->indices.count = index_count;
    batch->texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
    batch->texture.image = image;
    batch->state.blend.source = light.additive ? "one" : "dst-color";
    batch->state.blend.destination = "one";
    batch->state.depth_test = "equal";
    batch->state.depth_write = false;
    batch->state.alpha_test = "none";
    batch->state.cull = cull;
    batch->state.depth_range[0] = 0;
    batch->state.depth_range[1] = 1;
    batch->state.polygon_offset = NULL;
    batch->lighting.kind = "vertex";
    batch->vertices = vertices;
    batch->vertex_count = vcount;
  }
  *out_count = count;
  return batches;
}
