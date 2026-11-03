/* Port of src/render/scene/particles/legacy.ts. GPL-2.0-or-later. */
#include "qa/render/scene/particles/legacy.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"

static double fr(double value) { return (double)(float)value; }

void qa_legacy_particle_batch_destroy(qa_legacy_particle_batch *batch) {
  free(batch->owned_vertices);
  free(batch->owned_indices);
  memset(batch, 0, sizeof *batch);
}
void qa_legacy_particle_image_destroy(qa_legacy_particle_image *image) {
  free(image->pixels);
  memset(image, 0, sizeof *image);
}

typedef struct builder {
  qa_material_vertex *vertices; size_t count, capacity;
  double *indices; size_t icount, icapacity;
} builder;
static bool grow_v(builder *b, size_t extra) {
  if (b->count + extra <= b->capacity) return b->vertices != NULL || b->capacity == 0 ? b->vertices != NULL : false;
  size_t next = (b->count + extra) * 2 + 8;
  qa_material_vertex *grown = realloc(b->vertices, next * sizeof *grown);
  if (grown == NULL) return false;
  b->vertices = grown; b->capacity = next; return true;
}
static bool grow_i(builder *b, size_t extra) {
  if (b->icount + extra <= b->icapacity) return b->indices != NULL;
  size_t next = (b->icount + extra) * 2 + 8;
  double *grown = realloc(b->indices, next * sizeof *grown);
  if (grown == NULL) return false;
  b->indices = grown; b->icapacity = next; return true;
}

qa_model_owned_geometry qa_prepare_particle_geometry(
    const qa_scene_particle *particles, size_t particle_count,
    const qa_particle_preparation_context *context) {
  builder b = {0};
  bool q1 = strcmp(context->indexed_profile, "q1") == 0;
  for (size_t pi = 0; pi < particle_count; pi++) {
    const qa_scene_particle *particle = &particles[pi];
    size_t offset = b.count;
    if (particle->is_rgba) {
      qa_sprite_pose pose = {particle->origin, particle->size,
                             particle->rotation,
                             {particle->color.x * 255, particle->color.y * 255,
                              particle->color.z * 255, particle->color.w * 255}};
      qa_entity_geometry geometry = qa_sprite_geometry(
          pose, context->camera.axis,
          context->camera.clip_is_portal && context->camera.clip_mirror);
      if (!grow_v(&b, geometry.vertex_count) ||
          !grow_i(&b, geometry.index_count)) {
        qa_entity_geometry_destroy(&geometry);
        break;
      }
      memcpy(b.vertices + b.count, geometry.vertices,
             geometry.vertex_count * sizeof *b.vertices);
      b.count += geometry.vertex_count;
      for (size_t i = 0; i < geometry.index_count; i++)
        b.indices[b.icount++] = geometry.indices[i] + (double)offset;
      qa_entity_geometry_destroy(&geometry);
      continue;
    }
    qa_vec3 delta = qa_sub3(particle->origin, context->camera.origin);
    qa_vec3 forward = context->camera.axis.vectors[0];
    double depth = delta.x * forward.x + delta.y * forward.y + delta.z * forward.z;
    double scale = (depth < 20 ? 1 : 1 + depth * 0.004) * particle->size;
    qa_vec3 palette =
        context->palette_color(context->palette_color_ctx, particle->palette_index);
    double alpha = q1 ? 255 : (double)(qa_int32(trunc(particle->alpha * 255)) & 255);
    qa_vec4 color = {palette.x, palette.y, palette.z, alpha};
    qa_vec3 normal = qa_scale3(forward, -1);
    qa_vec3 up = qa_scale3(context->camera.axis.vectors[2], 1.5 * scale);
    qa_vec3 right = qa_scale3(context->camera.axis.vectors[1], -1.5 * scale);
    double uv = q1 ? 0 : 0.0625;
    if (!grow_v(&b, 3) || !grow_i(&b, 3)) break;
    qa_vec3 positions[3] = {particle->origin, qa_add3(particle->origin, up),
                            qa_add3(particle->origin, right)};
    double ss[3] = {uv, 1 + uv, uv}, ts[3] = {uv, uv, 1 + uv};
    for (int i = 0; i < 3; i++) {
      qa_material_vertex vertex = {0};
      vertex.position = positions[i];
      vertex.normal = normal;
      vertex.color = color;
      vertex.tex_coord = (qa_vec2){ss[i], ts[i]};
      vertex.lightmap_coord = (qa_vec2){0, 0};
      b.vertices[b.count++] = vertex;
    }
    b.indices[b.icount++] = (double)offset;
    b.indices[b.icount++] = (double)(offset + 1);
    b.indices[b.icount++] = (double)(offset + 2);
  }
  return (qa_model_owned_geometry){b.vertices, b.count, b.indices, b.icount};
}

static qa_draw_batch particle_batch_shell(qa_renderer_image texture,
                                          qa_render_state state) {
  qa_draw_batch batch = {0};
  batch.lighting.kind = "vertex";
  batch.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
  batch.texture.image = texture;
  batch.state = state;
  return batch;
}

qa_legacy_particle_batch qa_prepare_particle_batch(
    const qa_scene_particle *particles, size_t particle_count,
    const qa_particle_preparation_context *context, qa_renderer_image texture,
    qa_particle_project_fn project, void *project_ctx) {
  qa_model_owned_geometry geometry =
      qa_prepare_particle_geometry(particles, particle_count, context);
  qa_legacy_particle_batch out = {0};
  out.owned_vertices =
      calloc(geometry.vertex_count ? geometry.vertex_count : 1,
             sizeof *out.owned_vertices);
  if (out.owned_vertices != NULL) {
    for (size_t i = 0; i < geometry.vertex_count; i++) {
      qa_material_vertex vertex = geometry.vertices[i];
      out.owned_vertices[i].position = project(project_ctx, vertex.position);
      out.owned_vertices[i].tex_coord = vertex.tex_coord;
      out.owned_vertices[i].color =
          (qa_vec4){vertex.color.x / 255, vertex.color.y / 255,
                    vertex.color.z / 255, vertex.color.w / 255};
    }
  }
  out.owned_indices = geometry.indices; /* ownership transfer */
  qa_render_state state = {0};
  state.blend = (qa_blend_state){"src-alpha", "one-minus-src-alpha"};
  state.depth_test = "less-equal";
  state.depth_write = strcmp(context->indexed_profile, "q1") == 0;
  state.alpha_test = "none";
  state.cull = "none";
  state.depth_range[0] = 0;
  state.depth_range[1] = 1;
  out.batch = particle_batch_shell(texture, state);
  out.batch.vertices = out.owned_vertices;
  out.batch.vertex_count =
      out.owned_vertices != NULL ? geometry.vertex_count : 0;
  out.batch.indices.values = out.owned_indices;
  out.batch.indices.count = geometry.index_count;
  free(geometry.vertices);
  return out;
}

qa_model_owned_geometry qa_q2_beam_geometry(qa_vec3 origin, qa_vec3 old_origin,
                                            double diameter, qa_vec4 color) {
  qa_model_owned_geometry out = {0};
  qa_vec3 delta = qa_sub3(old_origin, origin);
  if (qa_length3(delta) == 0) return out;
  qa_vec3 direction = qa_normalize3(delta);
  qa_vec3 perpendicular =
      qa_scale3(qa_perpendicular_vector(direction), diameter / 2);
  builder b = {0};
  for (int segment = 0; segment < 6; segment++) {
    qa_vec3 normal =
        qa_rotate_point_around_vector(direction, perpendicular, segment * 60);
    qa_vec3 start = qa_add3(origin, normal), end = qa_add3(start, delta);
    qa_vec3 positions[2] = {start, end};
    if (!grow_v(&b, 2) || !grow_i(&b, 6)) break;
    for (int k = 0; k < 2; k++) {
      qa_material_vertex vertex = {0};
      vertex.position = positions[k];
      vertex.normal = qa_normalize3(normal);
      vertex.tex_coord = (qa_vec2){0, 0};
      vertex.lightmap_coord = (qa_vec2){0, 0};
      vertex.color = color;
      b.vertices[b.count++] = vertex;
    }
    double a = segment * 2, next = ((segment + 1) % 6) * 2;
    double idx[6] = {a, a + 1, next, next, a + 1, next + 1};
    memcpy(b.indices + b.icount, idx, sizeof idx);
    b.icount += 6;
  }
  return (qa_model_owned_geometry){b.vertices, b.count, b.indices, b.icount};
}

qa_legacy_particle_batch qa_q2_beam_batch(
    qa_vec3 origin, qa_vec3 old_origin, double diameter, qa_vec4 color,
    qa_particle_project_fn project, void *project_ctx,
    qa_renderer_image white_image, qa_render_state state) {
  qa_model_owned_geometry geometry =
      qa_q2_beam_geometry(origin, old_origin, diameter, color);
  qa_legacy_particle_batch out = {0};
  out.owned_vertices =
      calloc(geometry.vertex_count ? geometry.vertex_count : 1,
             sizeof *out.owned_vertices);
  if (out.owned_vertices != NULL) {
    for (size_t i = 0; i < geometry.vertex_count; i++) {
      out.owned_vertices[i].position =
          project(project_ctx, geometry.vertices[i].position);
      out.owned_vertices[i].tex_coord = geometry.vertices[i].tex_coord;
      out.owned_vertices[i].color =
          (qa_vec4){color.x / 255, color.y / 255, color.z / 255, color.w / 255};
    }
  }
  out.owned_indices = geometry.indices;
  qa_render_state batch_state = state;
  batch_state.depth_write = false;
  batch_state.blend = (qa_blend_state){"src-alpha", "one-minus-src-alpha"};
  batch_state.alpha_test = "none";
  out.batch = particle_batch_shell(white_image, batch_state);
  out.batch.vertices = out.owned_vertices;
  out.batch.vertex_count =
      out.owned_vertices != NULL ? geometry.vertex_count : 0;
  out.batch.indices.values = out.owned_indices;
  out.batch.indices.count = geometry.index_count;
  free(geometry.vertices);
  return out;
}

void qa_legacy_particle_image_make(qa_legacy_particle_image *out, const char *profile) {
  memset(out, 0, sizeof *out);
  static const char *const DOT_Q1[8] = {"01100000", "11110000", "11110000",
                                        "01100000", "00000000", "00000000",
                                        "00000000", "00000000"};
  static const char *const DOT_Q2[8] = {"00000000", "00110000", "01111000",
                                        "01111000", "00110000", "00000000",
                                        "00000000", "00000000"};
  const char *const *dot =
      (profile != NULL && strcmp(profile, "q1") == 0) ? DOT_Q1 : DOT_Q2;
  out->pixels = calloc(8 * 8 * 4, 1);
  if (out->pixels != NULL) {
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        uint8_t *pixel = out->pixels + (y * 8 + x) * 4;
        pixel[0] = pixel[1] = pixel[2] = 255;
        pixel[3] = dot[x][y] == '1' ? 255 : 0;
      }
  }
  out->level = (qa_image_level){8, 8, {out->pixels, 8 * 8 * 4}};
  out->image.kind = QA_RENDER_IMAGE_RGBA8;
  out->image.levels = &out->level;
  out->image.level_count = 1;
  out->image.border_color = (qa_vec4){0, 0, 0, 0};
}

static const double RAMP1[8] = {111, 109, 107, 105, 103, 101, 99, 97};
static const double RAMP2[8] = {111, 110, 109, 108, 107, 106, 104, 102};
static const double RAMP3[8] = {109, 107, 6, 5, 4, 3, 0, 0};
static double ramp_at(const double *ramp, double value, double fallback) {
  double index = trunc(value);
  return (index >= 0 && index < 8) ? ramp[(size_t)index] : fallback;
}
static qa_vec3 scale_velocity(qa_vec3 velocity, double amount, bool z) {
  return (qa_vec3){fr(velocity.x + velocity.x * amount),
                   fr(velocity.y + velocity.y * amount),
                   z ? fr(velocity.z + velocity.z * amount) : velocity.z};
}

qa_q1_particle_state qa_advance_q1_particle(qa_q1_particle_state particle,
                                            double seconds, double gravity) {
  qa_q1_particle_state out = particle;
  out.origin = (qa_vec3){fr(particle.origin.x + particle.velocity.x * seconds),
                         fr(particle.origin.y + particle.velocity.y * seconds),
                         fr(particle.origin.z + particle.velocity.z * seconds)};
  qa_vec3 velocity = particle.velocity;
  double ramp = particle.ramp, die = particle.die, color = particle.color;
  double grav = seconds * gravity * 0.05, dvel = 4 * seconds;
  const char *type = particle.type;
  if (strcmp(type, "static") == 0) {
  } else if (strcmp(type, "fire") == 0) {
    ramp = fr(ramp + seconds * 5);
    if (ramp >= 6) die = -1; else color = ramp_at(RAMP3, ramp, color);
    velocity.z = fr(velocity.z + grav);
  } else if (strcmp(type, "explode") == 0) {
    ramp = fr(ramp + seconds * 10);
    if (ramp >= 8) die = -1; else color = ramp_at(RAMP1, ramp, color);
    velocity = scale_velocity(velocity, dvel, true);
    velocity.z = fr(velocity.z - grav);
  } else if (strcmp(type, "explode2") == 0) {
    ramp = fr(ramp + seconds * 15);
    if (ramp >= 8) die = -1; else color = ramp_at(RAMP2, ramp, color);
    velocity = scale_velocity(velocity, -seconds, true);
    velocity.z = fr(velocity.z - grav);
  } else if (strcmp(type, "blob") == 0) {
    velocity = scale_velocity(velocity, dvel, true);
    velocity.z = fr(velocity.z - grav);
  } else if (strcmp(type, "blob2") == 0) {
    velocity = scale_velocity(velocity, -dvel, false);
    velocity.z = fr(velocity.z - grav);
  } else if (strcmp(type, "gravity") == 0 || strcmp(type, "slow-gravity") == 0) {
    velocity.z = fr(velocity.z - grav);
  }
  out.velocity = velocity;
  out.ramp = ramp;
  out.die = die;
  out.color = color;
  return out;
}

bool qa_sample_q2_particle(qa_q2_particle_state particle, double milliseconds,
                           qa_scene_particle *out) {
  double seconds = particle.alpha_velocity == -10000
                       ? 0
                       : (milliseconds - particle.spawn_milliseconds) * 0.001;
  double alpha = particle.alpha + seconds * particle.alpha_velocity;
  if (alpha <= 0) return false;
  double square = seconds * seconds;
  memset(out, 0, sizeof *out);
  out->is_rgba = false;
  out->palette_index = particle.color;
  out->alpha = qa_js_min(1, alpha);
  out->size = 1;
  out->origin = (qa_vec3){
      fr(particle.origin.x + particle.velocity.x * seconds +
         particle.acceleration.x * square),
      fr(particle.origin.y + particle.velocity.y * seconds +
         particle.acceleration.y * square),
      fr(particle.origin.z + particle.velocity.z * seconds +
         particle.acceleration.z * square)};
  return true;
}
