/* Port of src/render/scene/models/sprites.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/sprites.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"

void qa_model_owned_geometry_destroy(qa_model_owned_geometry *geometry) {
  free(geometry->vertices);
  free(geometry->indices);
  memset(geometry, 0, sizeof *geometry);
}
qa_material_geometry qa_model_owned_geometry_view(
    const qa_model_owned_geometry *geometry) {
  return (qa_material_geometry){geometry->vertices, geometry->vertex_count,
                                geometry->indices, geometry->index_count};
}

bool qa_q1_sprite_axes(const qa_q1_sprite_model *model,
                       qa_model_transform transform, qa_scene_camera camera,
                       double roll, qa_axis *out) {
  qa_vec3 right = qa_scale3(camera.axis.vectors[1], -1);
  qa_vec3 up = camera.axis.vectors[2];
  switch (model->orientation) {
    case 0: {
      qa_vec3 direction =
          qa_normalize3(qa_sub3(transform.origin, camera.origin));
      if (fabs(direction.z) > 0.999848) return false;
      qa_vec3 r = qa_normalize3((qa_vec3){direction.y, -direction.x, 0});
      *out = (qa_axis){{{ -r.y, r.x, 0}, qa_scale3(r, -1), {0, 0, 1}}};
      return true;
    }
    case 1: {
      qa_vec3 direction = camera.axis.vectors[0];
      if (fabs(direction.z) > 0.999848) return false;
      qa_vec3 r = qa_normalize3((qa_vec3){direction.y, -direction.x, 0});
      *out = (qa_axis){{{ -r.y, r.x, 0}, qa_scale3(r, -1), {0, 0, 1}}};
      return true;
    }
    case 2: *out = camera.axis; return true;
    case 3: *out = transform.axis; return true;
    case 4: {
      double radians = roll * acos(-1.0) / 180;
      double sine = sin(radians), cosine = cos(radians);
      *out = (qa_axis){{camera.axis.vectors[0],
                        qa_scale3(qa_add3(qa_scale3(right, cosine),
                                          qa_scale3(up, sine)), -1),
                        qa_add3(qa_scale3(right, -sine),
                                qa_scale3(up, cosine))}};
      return true;
    }
    default: return false; /* TS falls off the switch: undefined (see qa_q1_sprite_geometry) */
  }
}

qa_model_owned_geometry qa_sprite_quad(qa_vec3 origin, qa_axis axis,
                                       qa_sprite_extents extents, qa_vec4 color,
                                       bool mirror) {
  qa_model_owned_geometry out = {0};
  out.vertices = calloc(4, sizeof *out.vertices);
  out.indices = malloc(6 * sizeof *out.indices);
  if (out.vertices == NULL || out.indices == NULL) {
    qa_model_owned_geometry_destroy(&out);
    return out;
  }
  qa_vec3 right = qa_scale3(axis.vectors[1], mirror ? 1 : -1);
  qa_vec3 up = axis.vectors[2];
  qa_vec3 normal = qa_scale3(axis.vectors[0], -1);
  const double xs[4] = {extents.left, extents.right, extents.right, extents.left};
  const double ys[4] = {extents.top, extents.top, extents.bottom, extents.bottom};
  const double ss[4] = {0, 1, 1, 0}, ts[4] = {0, 0, 1, 1};
  for (int i = 0; i < 4; i++) {
    qa_material_vertex vertex = {0};
    vertex.position =
        qa_add3(qa_add3(origin, qa_scale3(right, xs[i])), qa_scale3(up, ys[i]));
    vertex.normal = normal;
    vertex.tex_coord = (qa_vec2){ss[i], ts[i]};
    vertex.lightmap_coord = (qa_vec2){ss[i], ts[i]};
    vertex.color = color;
    out.vertices[i] = vertex;
  }
  out.vertex_count = 4;
  const double indices[6] = {0, 1, 3, 3, 1, 2};
  memcpy(out.indices, indices, sizeof indices);
  out.index_count = 6;
  return out;
}

qa_model_owned_geometry qa_q1_sprite_geometry(
    const qa_q1_sprite_model *model, qa_sprite_frame frame,
    qa_model_transform transform, qa_scene_camera camera, qa_vec4 color,
    double roll) {
  qa_model_owned_geometry empty = {0};
  qa_axis axis;
  if (!qa_q1_sprite_axes(model, transform, camera, roll, &axis)) {
    /* q1SpriteAxes falls off its switch for any other orientation and
     * returns undefined, which is not null: TS goes on to read axis[0]. */
    if (model->orientation < 0 || model->orientation > 4)
      qa_raise("TypeError", 0, 0, "undefined is not an object (evaluating 'axis[0]')");
    return empty;
  }
  qa_vec3 origin = qa_add3(transform.origin,
                           qa_scale3(axis.vectors[0], -model->beam_length));
  if (qa_dot3(axis.vectors[0], qa_sub3(camera.origin, origin)) >= 0)
    return empty;
  qa_sprite_extents extents = {
      frame.origin_x * transform.scale.x,
      (frame.origin_x + frame.width) * transform.scale.x,
      frame.origin_y * transform.scale.z,
      (frame.origin_y - frame.height) * transform.scale.z};
  return qa_sprite_quad(origin, axis, extents, color,
                        camera.clip_is_portal && camera.clip_mirror);
}
