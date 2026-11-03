/* Port of src/render/scene/particles/primitives.ts. GPL-2.0-or-later. */
#include "qa/render/scene/particles/primitives.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/models/transform.h"

const qa_rail_settings qa_default_rail_settings = {6, 16, 32};

static double fr(double value) { return (double)(float)value; }
static bool is_int32(double value) {
  return trunc(value) == value && value >= -2147483648.0 && value <= 2147483647.0;
}

void qa_entity_geometry_destroy(qa_entity_geometry *geometry) {
  free(geometry->vertices);
  free(geometry->indices);
  memset(geometry, 0, sizeof *geometry);
}
void qa_particle_draw_batch_destroy(qa_particle_draw_batch *batch) {
  free(batch->owned_vertices);
  free(batch->owned_indices);
  memset(batch, 0, sizeof *batch);
}

typedef struct builder {
  qa_material_vertex *vertices; size_t count, capacity;
  double *indices; size_t icount, icapacity;
} builder;
static void push_vertex(builder *b, qa_material_vertex vertex) {
  if (b->count == b->capacity) {
    b->capacity = b->capacity ? b->capacity * 2 : 16;
    b->vertices = realloc(b->vertices, b->capacity * sizeof *b->vertices);
  }
  if (b->vertices != NULL) b->vertices[b->count++] = vertex;
}
static void push_indices(builder *b, const double *values, size_t count) {
  if (b->icount + count > b->icapacity) {
    b->icapacity = (b->icount + count) * 2 + 16;
    b->indices = realloc(b->indices, b->icapacity * sizeof *b->indices);
  }
  if (b->indices != NULL) {
    memcpy(b->indices + b->icount, values, count * sizeof *values);
    b->icount += count;
  }
}
static qa_material_vertex rail_vertex(qa_vec3 position, double s, double t,
                                      qa_vec4 rgba, bool dim) {
  qa_material_vertex vertex = {0};
  vertex.position = position;
  vertex.normal = qa_math_vec3(0, 0, 0);
  vertex.tex_coord = (qa_vec2){s, t};
  vertex.lightmap_coord = (qa_vec2){0, 0};
  vertex.color = (qa_vec4){dim ? trunc(rgba.x * 0.25) : rgba.x,
                           dim ? trunc(rgba.y * 0.25) : rgba.y,
                           dim ? trunc(rgba.z * 0.25) : rgba.z, 0};
  return vertex;
}

qa_entity_geometry qa_rail_geometry(qa_rail_pose entity, qa_vec3 view_origin,
                                    const qa_rail_settings *settings) {
  if (settings == NULL) settings = &qa_default_rail_settings;
  builder b = {0};
  bool lightning = strcmp(entity.kind, "lightning") == 0;
  qa_vec3 start = lightning ? entity.origin : entity.old_origin;
  qa_vec3 end = lightning ? entity.old_origin : entity.origin;
  qa_vec3 delta = qa_sub3(end, start);
  qa_vec3 direction = qa_normalize3(delta);
  double length = trunc(qa_length3(delta));
  /* The source's int conversion is undefined past int32, so such a beam is not drawn. */
  if (!is_int32(length)) return (qa_entity_geometry){b.vertices, b.count, b.indices, b.icount};
  if (strcmp(entity.kind, "rail-rings") != 0) {
    qa_vec3 right = qa_normalize3(qa_cross3(
        qa_normalize3(qa_sub3(start, view_origin)),
        qa_normalize3(qa_sub3(end, view_origin))));
    int iterations = lightning ? 4 : 1;
    for (int index = 0; index < iterations; index++) {
      double width = lightning ? 8 : settings->core_width;
      if (!is_int32(width)) {
        if (lightning) right = qa_rotate_point_around_vector(direction, right, 45);
        continue;
      }
      double base = (double)b.count;
      qa_vec3 offset = qa_scale3(right, fr(width));
      double t = fr(fr(length) / 256);
      push_vertex(&b, rail_vertex(qa_add3(start, offset), 0, 0, entity.shader_rgba, true));
      push_vertex(&b, rail_vertex(qa_sub3(start, offset), 0, 1, entity.shader_rgba, false));
      push_vertex(&b, rail_vertex(qa_add3(end, offset), t, 0, entity.shader_rgba, false));
      push_vertex(&b, rail_vertex(qa_sub3(end, offset), t, 1, entity.shader_rgba, false));
      double idx[6] = {base, base + 1, base + 2, base + 2, base + 1, base + 3};
      push_indices(&b, idx, 6);
      if (lightning)
        right = qa_rotate_point_around_vector(direction, right, 45);
    }
    return (qa_entity_geometry){b.vertices, b.count, b.indices, b.icount};
  }
  qa_vec3 seed = qa_math_vec3(direction.z, -direction.x, direction.y);
  qa_vec3 right = qa_normalize3(
      qa_sub3(seed, qa_scale3(direction, qa_dot3(seed, direction))));
  qa_vec3 up = qa_cross3(right, direction);
  double segments =
      trunc(fr(fr(length) / fr(settings->segment_length)));
  if (!is_int32(segments)) return (qa_entity_geometry){b.vertices, b.count, b.indices, b.icount};
  if (segments <= 0) segments = 1;
  qa_vec3 step = qa_math_vec3(direction.x * fr(settings->segment_length),
                              direction.y * fr(settings->segment_length),
                              direction.z * fr(settings->segment_length));
  double width = settings->ring_width;
  if (!is_int32(width) || segments > 1000000) return (qa_entity_geometry){b.vertices, b.count, b.indices, b.icount};
  if (segments > 1) segments--;
  qa_vec3 positions[4];
  for (int index = 0; index < 4; index++) {
    double angle = (45 + index * 90) * acos(-1.0) / 180;
    double c = fr(cos(angle)), s = fr(sin(angle));
    qa_vec3 offset = qa_scale3(
        qa_scale3(qa_add3(qa_scale3(right, c), qa_scale3(up, s)), 0.25),
        fr(width));
    qa_vec3 position = qa_add3(start, offset);
    positions[index] = segments > 1 ? qa_add3(position, step) : position;
  }
  for (double segment = 0; segment < segments; segment++) {
    double base = (double)b.count;
    for (int index = 0; index < 4; index++) {
      push_vertex(&b, rail_vertex(positions[index], index < 2 ? 1 : 0,
                                  (index != 0 && index != 3) ? 1 : 0,
                                  entity.shader_rgba, false));
      positions[index] = qa_add3(positions[index], step);
    }
    double idx[6] = {base, base + 1, base + 3, base + 3, base + 1, base + 2};
    push_indices(&b, idx, 6);
  }
  return (qa_entity_geometry){b.vertices, b.count, b.indices, b.icount};
}

qa_entity_geometry qa_poly_geometry(const qa_poly_input_vertex *vertices,
                                    size_t vertex_count) {
  qa_entity_geometry out = {0};
  out.vertices = calloc(vertex_count ? vertex_count : 1, sizeof *out.vertices);
  if (out.vertices == NULL) return out;
  for (size_t i = 0; i < vertex_count; i++) {
    out.vertices[i].position = vertices[i].position;
    out.vertices[i].tex_coord = vertices[i].tex_coord;
    out.vertices[i].color = vertices[i].color;
    out.vertices[i].normal = qa_math_vec3(0, 0, 0);
    out.vertices[i].lightmap_coord = (qa_vec2){0, 0};
  }
  out.vertex_count = vertex_count;
  size_t triangles = vertex_count >= 2 ? vertex_count - 2 : 0;
  if (triangles > 0) {
    out.indices = malloc(triangles * 3 * sizeof *out.indices);
    if (out.indices != NULL) {
      for (size_t i = 0; i < triangles; i++) {
        out.indices[i * 3] = 0;
        out.indices[i * 3 + 1] = (double)(i + 1);
        out.indices[i * 3 + 2] = (double)(i + 2);
      }
      out.index_count = triangles * 3;
    }
  }
  return out;
}

static qa_vec3 sprite_corner(qa_vec3 origin, qa_vec3 left, qa_vec3 up,
                             double l, double u) {
  return qa_math_vec3(fr(origin.x + l * left.x) + u * up.x,
                      fr(origin.y + l * left.y) + u * up.y,
                      fr(origin.z + l * left.z) + u * up.z);
}

qa_entity_geometry qa_sprite_geometry(qa_sprite_pose entity, qa_axis view_axis,
                                      bool mirror) {
  qa_entity_geometry out = {0};
  double radius = fr(entity.radius);
  qa_vec3 left, up;
  if (entity.rotation == 0) {
    left = qa_scale3(view_axis.vectors[1], radius);
    up = qa_scale3(view_axis.vectors[2], radius);
  } else {
    double angle = fr(acos(-1.0) * entity.rotation / 180);
    double sine = fr(sin(angle)), cosine = fr(cos(angle));
    left = qa_add3(qa_scale3(view_axis.vectors[1], fr(cosine * radius)),
                   qa_scale3(view_axis.vectors[2], fr(-sine * radius)));
    up = qa_add3(qa_scale3(view_axis.vectors[2], fr(cosine * radius)),
                 qa_scale3(view_axis.vectors[1], fr(sine * radius)));
  }
  if (mirror) left = qa_scale3(left, -1);
  out.vertices = calloc(4, sizeof *out.vertices);
  out.indices = malloc(6 * sizeof *out.indices);
  if (out.vertices == NULL || out.indices == NULL) {
    qa_entity_geometry_destroy(&out);
    return out;
  }
  const double ls[4] = {1, -1, -1, 1}, us[4] = {1, 1, -1, -1};
  const double ss[4] = {0, 1, 1, 0}, ts[4] = {0, 0, 1, 1};
  for (int i = 0; i < 4; i++) {
    out.vertices[i].position = sprite_corner(entity.origin, left, up, ls[i], us[i]);
    out.vertices[i].normal = qa_scale3(view_axis.vectors[0], -1);
    out.vertices[i].tex_coord = (qa_vec2){ss[i], ts[i]};
    out.vertices[i].lightmap_coord = (qa_vec2){ss[i], ts[i]};
    out.vertices[i].color = entity.shader_rgba;
  }
  out.vertex_count = 4;
  const double indices[6] = {0, 1, 3, 3, 1, 2};
  memcpy(out.indices, indices, sizeof indices);
  out.index_count = 6;
  return out;
}

static qa_draw_batch base_batch(qa_renderer_image white_image,
                                qa_render_state state) {
  qa_draw_batch batch = {0};
  batch.lighting.kind = "vertex";
  batch.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
  batch.texture.image = white_image;
  batch.state = state;
  return batch;
}

qa_particle_draw_batch qa_beam_batch(qa_beam_pose entity,
                                     qa_particle_project_fn project,
                                     void *project_ctx, qa_render_state previous,
                                     qa_renderer_image white_image) {
  qa_particle_draw_batch out = {0};
  qa_vec3 direction = qa_sub3(entity.old_origin, entity.origin);
  if (qa_length3(direction) == 0) {
    out.batch = base_batch(white_image, previous);
    return out;
  }
  qa_vec3 normalized = qa_normalize3(direction);
  qa_vec3 perpendicular = qa_scale3(qa_perpendicular_vector(normalized), 4);
  out.owned_vertices = calloc(12, sizeof *out.owned_vertices);
  out.owned_indices = malloc(36 * sizeof *out.owned_indices);
  if (out.owned_vertices == NULL || out.owned_indices == NULL) {
    qa_particle_draw_batch_destroy(&out);
    out.batch = base_batch(white_image, previous);
    return out;
  }
  size_t vi = 0;
  for (int index = 0; index < 6; index++) {
    qa_vec3 start =
        qa_rotate_point_around_vector(normalized, perpendicular, index * 60);
    qa_vec3 positions[2] = {start, qa_add3(start, direction)};
    for (int k = 0; k < 2; k++) {
      out.owned_vertices[vi].position = project(project_ctx, positions[k]);
      out.owned_vertices[vi].tex_coord = (qa_vec2){0, 0};
      out.owned_vertices[vi].color = (qa_vec4){1, 0, 0, 1};
      vi++;
    }
  }
  size_t ii = 0;
  for (int index = 0; index < 12; index++) {
    if (index % 2 == 0) {
      out.owned_indices[ii++] = index % 12;
      out.owned_indices[ii++] = (index + 1) % 12;
      out.owned_indices[ii++] = (index + 2) % 12;
    } else {
      out.owned_indices[ii++] = (index + 1) % 12;
      out.owned_indices[ii++] = index % 12;
      out.owned_indices[ii++] = (index + 2) % 12;
    }
  }
  qa_render_state state = previous;
  state.blend = (qa_blend_state){"one", "one"};
  state.depth_test = "less-equal";
  state.depth_write = false;
  state.alpha_test = "none";
  out.batch = base_batch(white_image, state);
  out.batch.vertices = out.owned_vertices;
  out.batch.vertex_count = 12;
  out.batch.indices.values = out.owned_indices;
  out.batch.indices.count = 36;
  return out;
}

qa_particle_draw_batch qa_default_model_batch(
    qa_model_transform entity, qa_particle_project_fn project,
    void *project_ctx, qa_render_state state, qa_renderer_image white_image) {
  qa_particle_draw_batch out = {0};
  out.owned_vertices = calloc(6, sizeof *out.owned_vertices);
  out.owned_indices = malloc(6 * sizeof *out.owned_indices);
  out.batch = base_batch(white_image, state);
  out.batch.primitive_is_lines = true;
  out.batch.line_width = 3;
  if (out.owned_vertices == NULL || out.owned_indices == NULL) {
    qa_particle_draw_batch_destroy(&out);
    out.batch = base_batch(white_image, state);
    out.batch.primitive_is_lines = true;
    out.batch.line_width = 3;
    return out;
  }
  const qa_vec4 colors[3] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
  size_t vi = 0;
  for (int index = 0; index < 3; index++) {
    qa_vec3 locals[2] = {qa_math_vec3(0, 0, 0),
                         qa_math_vec3(index == 0 ? 16 : 0, index == 1 ? 16 : 0,
                                      index == 2 ? 16 : 0)};
    for (int k = 0; k < 2; k++) {
      out.owned_vertices[vi].position =
          project(project_ctx, qa_model_world_point(entity, locals[k]));
      out.owned_vertices[vi].color = colors[index];
      out.owned_vertices[vi].tex_coord = (qa_vec2){0, 0};
      vi++;
    }
  }
  for (int i = 0; i < 6; i++) out.owned_indices[i] = i;
  out.batch.vertices = out.owned_vertices;
  out.batch.vertex_count = 6;
  out.batch.indices.values = out.owned_indices;
  out.batch.indices.count = 6;
  return out;
}
