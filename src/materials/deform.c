/* Port of src/materials/deform.ts. GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/core/renderer_math.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/materials/deform.h"
#include "qa/materials/shader_types.h"

static float fr(double v) { return (float)v; }
static bool eq(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}

static qa_material_vertex vertex_at(qa_deform_geometry mesh, int64_t index) {
  if (index < 0 || (size_t)index >= mesh.vertex_count)
    qa_range_error("deformation index %lld outside %zu", (long long)index,
                   mesh.vertex_count);
  return mesh.vertices[index];
}
/* Linux rand(): Park-Miller seed expansion, additive recurrence. */
struct linux_random {
  uint32_t state[31];
  int front, rear;
};
static uint32_t linux_next(struct linux_random *r) {
  uint32_t value = r->state[r->front] + r->state[r->rear];
  r->state[r->front] = value;
  r->front = (r->front + 1) % 31;
  r->rear = (r->rear + 1) % 31;
  return value >> 1;
}
static void linux_random_init(struct linux_random *r, int64_t seed) {
  r->state[0] = seed == 0 ? 1u : (uint32_t)seed;
  for (int i = 1; i < 31; i++)
    /* TS computes in JS numbers: 16807 * state % 2147483647. State values
     * after the first are below the modulus; the seed here is 1001. */
    r->state[i] = (uint32_t)((16807ull * r->state[i - 1]) % 2147483647ull);
  r->front = 3;
  r->rear = 0;
  for (int i = 0; i < 310; i++) (void)linux_next(r);
}

void qa_renderer_noise_init(qa_renderer_noise *noise) {
  struct linux_random random;
  linux_random_init(&random, 1001);
  for (int i = 0; i < 256; i++) {
    double value = fr(fr((double)linux_next(&random)) / fr(2147483647.0));
    noise->values[i] = fr(value * 2 - 1);
    noise->permutation[i] =
        qa_mat_to_int32(fr(fr(fr((double)linux_next(&random)) /
                            fr(2147483647.0)) * 255.0)) & 255;
  }
}

/* perm(index & 255): the sums are JS doubles and `&` applies ToInt32 (NaN to 0). */
static double lattice(const qa_renderer_noise *noise, double ix, double iy,
                      double iz, double it, int dx, int dy,
                      int dz, int dt) {
  int p1 = noise->permutation[qa_js_to_int32(it + dt) & 255];
  int p2 = noise->permutation[qa_js_to_int32(iz + dz + p1) & 255];
  int p3 = noise->permutation[qa_js_to_int32(iy + dy + p2) & 255];
  int p4 = noise->permutation[qa_js_to_int32(ix + dx + p3) & 255];
  return noise->values[p4];
}
static double lerp_f(double a, double b, double amount) {
  return fr(fr(a * fr(1 - amount)) + fr(b * amount));
}

double qa_renderer_noise_sample(const qa_renderer_noise *noise, double x,
                                double y, double z, double time) {
  double ix = floor(x), iy = floor(y), iz = floor(z), it = floor(time);
  double fx = fr(x - ix), fy = fr(y - iy);
  double fz = fr(z - iz), ft = fr(time - it);
  double corners[2][2][2][2];
  for (int dt = 0; dt < 2; dt++)
    for (int dz = 0; dz < 2; dz++)
      for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++)
          corners[dt][dz][dy][dx] = lattice(noise, ix, iy, iz, it, dx, dy, dz, dt);
  double planes[2][2];
  for (int dt = 0; dt < 2; dt++)
    for (int dz = 0; dz < 2; dz++)
      planes[dt][dz] = lerp_f(
          lerp_f(corners[dt][dz][0][0], corners[dt][dz][0][1], fx),
          lerp_f(corners[dt][dz][1][0], corners[dt][dz][1][1], fx), fy);
  return lerp_f(lerp_f(planes[0][0], planes[0][1], fz),
                lerp_f(planes[1][0], planes[1][1], fz), ft);
}

static qa_vec3 local_direction(qa_vec3 direction, qa_deform_view view) {
  if (view.entity_axis == NULL) return direction;
  qa_vec3 out = {qa_dot3(direction, view.entity_axis->vectors[0]),
                 qa_dot3(direction, view.entity_axis->vectors[1]),
                 qa_dot3(direction, view.entity_axis->vectors[2])};
  return out;
}

/* Builds a replaced geometry inside tess from caller arrays. */
static void replace_with(qa_material_deform_state *tess,
                         const qa_material_vertex *vertices, size_t vcount,
                         const double *indices, size_t icount) {
  qa_material_geometry geometry = {vertices, vcount, indices, icount};
  qa_material_deform_state_replace(tess, geometry);
}

static void projection_shadow(qa_material_deform_state *tess,
                              qa_deform_geometry mesh,
                              qa_projection_shadow_context context) {
  qa_vec3 ground = {context.axis.vectors[0].z, context.axis.vectors[1].z,
                    context.axis.vectors[2].z};
  ground = qa_math_vec3(ground.x, ground.y, ground.z);
  double ground_dist = fr(context.origin.z - context.shadow_plane);
  qa_vec3 light_dir = context.light_dir;
  double d = qa_dot3(light_dir, ground);
  if (d < 0.5) {
    light_dir = qa_math_vec3(light_dir.x + ground.x * (0.5 - d),
                             light_dir.y + ground.y * (0.5 - d),
                             light_dir.z + ground.z * (0.5 - d));
    d = qa_dot3(light_dir, ground);
  }
  qa_vec3 light = qa_scale3(light_dir, fr(1.0 / d));
  qa_material_vertex *vertices = malloc(
      (mesh.vertex_count > 0 ? mesh.vertex_count : 1) * sizeof *vertices);
  if (vertices == NULL) qa_range_error("deform allocation failed");
  for (size_t i = 0; i < mesh.vertex_count; i++) {
    vertices[i] = mesh.vertices[i];
    double height = fr(qa_dot3(vertices[i].position, ground) + ground_dist);
    vertices[i].position = qa_sub3(vertices[i].position,
                                   qa_scale3(light, height));
  }
  replace_with(tess, vertices, mesh.vertex_count, mesh.indices,
               mesh.index_count);
  free(vertices);
}

static void sprite_geometry(qa_material_deform_state *tess,
                            qa_deform_geometry mesh, qa_deform_view view) {
  /* tr_shade_calc.c only warns about an odd vertex or index count and then
     processes whole quads; a trailing partial quad is dropped. */
  size_t quad_vertices = mesh.vertex_count - mesh.vertex_count % 4;
  qa_material_vertex *vertices = malloc(
      (quad_vertices > 0 ? quad_vertices : 1) * sizeof *vertices);
  double *indices = malloc(
      (quad_vertices > 0 ? quad_vertices / 4 * 6 : 1) * sizeof *indices);
  if (vertices == NULL || indices == NULL)
    qa_range_error("deform allocation failed");
  qa_vec3 left_dir = local_direction(view.axis.vectors[1], view);
  qa_vec3 up_dir = local_direction(view.axis.vectors[2], view);
  double axis_scale = 1;
  if (view.non_normalized_axis != NULL) {
    double length = qa_length3(*view.non_normalized_axis);
    axis_scale = length == 0 ? 0 : fr(1.0 / length);
  }
  size_t out_v = 0, out_i = 0;
  static const qa_vec2 coords[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (size_t start = 0; start < quad_vertices; start += 4) {
    qa_material_vertex first = vertex_at(mesh, (int64_t)start);
    qa_vec3 center = qa_scale3(
        qa_add3(qa_add3(first.position, vertex_at(mesh, (int64_t)start + 1).position),
                qa_add3(vertex_at(mesh, (int64_t)start + 2).position,
                        vertex_at(mesh, (int64_t)start + 3).position)),
        0.25);
    qa_vec3 delta = qa_sub3(first.position, center);
    double radius = fr(sqrt(qa_dot3(delta, delta)) * 0.707);
    qa_vec3 left = qa_scale3(qa_scale3(left_dir, view.mirror ? -radius : radius),
                             axis_scale);
    qa_vec3 up = qa_scale3(qa_scale3(up_dir, radius), axis_scale);
    qa_vec3 normal = qa_sub3((qa_vec3){0, 0, 0}, view.axis.vectors[0]);
    qa_vec3 positions[4] = {
        qa_add3(qa_add3(center, left), up), qa_add3(qa_sub3(center, left), up),
        qa_sub3(qa_sub3(center, left), up), qa_sub3(qa_add3(center, left), up)};
    for (int corner = 0; corner < 4; corner++) {
      vertices[out_v].position = positions[corner];
      vertices[out_v].normal = normal;
      vertices[out_v].tex_coord = coords[corner];
      vertices[out_v].lightmap_coord = coords[corner];
      vertices[out_v].color = first.color;
      out_v++;
    }
    double quad_indices[6] = {(double)start, (double)start + 1,
        (double)start + 3, (double)start + 3, (double)start + 1,
        (double)start + 2};
    memcpy(indices + out_i, quad_indices, sizeof quad_indices);
    out_i += 6;
  }
  replace_with(tess, vertices, out_v, indices, out_i);
  free(vertices);
  free(indices);
}

static void text_geometry(qa_material_deform_state *tess, double index_d,
                          qa_deform_view view) {
  if (!(index_d >= 0 && index_d == floor(index_d) && index_d < (double)tess->render_text_count))
    qa_range_error("deformation index %s outside %zu", QA_JS_NUMBER(index_d),
                   tess->render_text_count);
  size_t index = (size_t)index_d;
  const char *text = tess->render_text[index];
  qa_material_vertex quad[4];
  qa_material_deform_state_text_quad(tess, quad);
  qa_vec3 width = qa_cross3(quad[0].normal, qa_math_vec3(0, 0, -1));
  qa_vec3 mid = {0, 0, 0};
  double bottom = 999999, top = -999999;
  for (int i = 0; i < 4; i++) {
    mid = qa_add3(quad[i].position, mid);
    if (quad[i].position.z < bottom) bottom = quad[i].position.z;
    if (quad[i].position.z > top) top = quad[i].position.z;
  }
  qa_vec3 height = qa_math_vec3(0, 0, fr(top - bottom) * 0.5);
  width = qa_scale3(width, fr(height.z * -0.75));
  /* C strings end at the first NUL, matching the TS length rule. */
  size_t length = strlen(text);
  qa_vec3 origin = qa_add3(qa_scale3(mid, 0.25),
                           qa_scale3(width, (double)length - 1));
  qa_vec3 normal = qa_sub3((qa_vec3){0, 0, 0}, view.axis.vectors[0]);
  qa_material_deform_state_reset(tess);
  for (size_t character = 0; character < length; character++) {
    int ch = (unsigned char)text[character] & 255;
    if (ch != 32) {
      double s = (ch & 15) * 0.0625, t = (ch >> 4) * 0.0625;
      qa_vec3 positions[4] = {
          qa_add3(qa_add3(origin, width), height),
          qa_add3(qa_sub3(origin, width), height),
          qa_sub3(qa_sub3(origin, width), height),
          qa_sub3(qa_add3(origin, width), height)};
      qa_vec2 coords[4] = {{s, t}, {s + 0.0625, t},
                           {s + 0.0625, t + 0.0625}, {s, t + 0.0625}};
      qa_material_vertex stamp[4];
      for (int corner = 0; corner < 4; corner++) {
        stamp[corner].position = positions[corner];
        stamp[corner].normal = normal;
        stamp[corner].tex_coord = coords[corner];
        stamp[corner].lightmap_coord = coords[corner];
        stamp[corner].color = (qa_vec4){255, 255, 255, 255};
      }
      double stamp_indices[6] = {0, 1, 3, 3, 1, 2};
      qa_material_geometry geometry = {stamp, 4, stamp_indices, 6};
      qa_material_deform_state_append_stamp(tess, geometry);
    }
    origin = qa_add3(origin, qa_scale3(width, -2));
  }
}

static void pivot_geometry(qa_material_deform_state *tess,
                           qa_deform_geometry mesh, qa_deform_view view) {
  qa_vec3 forward = local_direction(view.axis.vectors[0], view);
  qa_material_vertex *vertices = malloc(
      (mesh.vertex_count > 0 ? mesh.vertex_count : 1) * sizeof *vertices);
  if (vertices == NULL) qa_range_error("deform allocation failed");
  if (mesh.vertex_count > 0)
    memcpy(vertices, mesh.vertices, mesh.vertex_count * sizeof *vertices);
  qa_deform_geometry work = {vertices, mesh.vertex_count, mesh.indices,
                             mesh.index_count};
  static const int edges[6][2] = {{0, 1}, {0, 2}, {0, 3},
                                  {1, 2}, {1, 3}, {2, 3}};
  for (size_t start = 0; start + 4 <= work.vertex_count; start += 4) {
    int shortest = 0, second = 0;
    double first_length = 999999, second_length = 999999;
    for (int e = 0; e < 6; e++) {
      qa_vec3 delta = qa_sub3(
          vertex_at(work, (int64_t)start + edges[e][0]).position,
          vertex_at(work, (int64_t)start + edges[e][1]).position);
      double length = qa_dot3(delta, delta);
      if (length < first_length) {
        second = shortest; second_length = first_length;
        shortest = e; first_length = length;
      } else if (length < second_length) {
        second = e; second_length = length;
      }
    }
    qa_vec3 first_mid = qa_scale3(
        qa_add3(vertex_at(work, (int64_t)start + edges[shortest][0]).position,
                vertex_at(work, (int64_t)start + edges[shortest][1]).position),
        0.5);
    qa_vec3 second_mid = qa_scale3(
        qa_add3(vertex_at(work, (int64_t)start + edges[second][0]).position,
                vertex_at(work, (int64_t)start + edges[second][1]).position),
        0.5);
    qa_vec3 minor = qa_normalize3(
        qa_cross3(qa_sub3(second_mid, first_mid), forward));
    int selected[2] = {shortest, second};
    qa_vec3 mids[2] = {first_mid, second_mid};
    double lengths[2] = {first_length, second_length};
    for (int s = 0; s < 2; s++) {
      int a = edges[selected[s]][0], b = edges[selected[s]][1];
      bool follows = false;
      for (int k = 0; k < 5; k++) {
        /* Plain indexing: past the end reads undefined, which never matches. */
        size_t base = start / 4 * 6 + (size_t)k;
        if (base + 1 < mesh.index_count &&
            mesh.indices[base] == (double)start + a &&
            mesh.indices[base + 1] == (double)start + b)
          follows = true;
      }
      double radius = sqrt(lengths[s]) * 0.5 * (follows ? -1 : 1);
      vertices[start + a].position =
          qa_add3(mids[s], qa_scale3(minor, radius));
      vertices[start + b].position =
          qa_add3(mids[s], qa_scale3(minor, -radius));
    }
  }
  replace_with(tess, vertices, mesh.vertex_count, mesh.indices,
               mesh.index_count);
  free(vertices);
}

qa_deform_geometry qa_deform_geometry_apply(
    qa_material_deform_state *tess, const qa_vertex_deformation *deforms,
    size_t deform_count, qa_deform_view view, double time,
    const qa_renderer_noise *noise,
    const qa_projection_shadow_context *projection) {
  for (size_t di = 0; di < deform_count; di++) {
    qa_vertex_deformation deform = deforms[di];
    qa_deform_geometry result = qa_material_deform_state_snapshot(tess);
    if (eq(deform.kind, "none")) continue;
    if (eq(deform.kind, "text")) {
      text_geometry(tess, deform.index, view);
      continue;
    }
    if (eq(deform.kind, "autosprite")) {
      sprite_geometry(tess, result, view);
      continue;
    }
    if (eq(deform.kind, "autosprite2")) {
      pivot_geometry(tess, result, view);
      continue;
    }
    if (eq(deform.kind, "projectionshadow")) {
      if (projection == NULL) continue;
      projection_shadow(tess, result, *projection);
      continue;
    }
    if ((eq(deform.kind, "move") || eq(deform.kind, "wave")) &&
        (eq(deform.wave.kind, "none") || eq(deform.wave.kind, "noise"))) {
      if (tess->material_name == NULL)
        qa_range_error("Invalid deformation waveform requires its begun source material");
      int func = eq(deform.wave.kind, "none") ? QA_MAT_WAVE_NONE
                                              : QA_MAT_WAVE_NOISE;
      char message[512];
      snprintf(message, sizeof message,
               "TableForFunc called with invalid function '%d' in shader '%s'\n",
               func, tess->material_name);
      qa_common_error(QA_COMMON_ERROR_DROP, message);
    }
    qa_material_vertex *vertices = malloc(
        (result.vertex_count > 0 ? result.vertex_count : 1) * sizeof *vertices);
    if (vertices == NULL) qa_range_error("deform allocation failed");
    if (eq(deform.kind, "move")) {
      qa_vec3 offset = qa_scale3(deform.direction,
                                 qa_mat_evaluate_waveform(deform.wave, time));
      for (size_t i = 0; i < result.vertex_count; i++) {
        vertices[i] = result.vertices[i];
        vertices[i].position = qa_add3(vertices[i].position, offset);
      }
    } else if (eq(deform.kind, "wave")) {
      bool constant = deform.wave.frequency == 0;
      double constant_scale = constant
          ? qa_mat_evaluate_waveform(deform.wave, time) : 0;
      for (size_t i = 0; i < result.vertex_count; i++) {
        vertices[i] = result.vertices[i];
        double scale;
        if (constant) {
          scale = constant_scale;
        } else {
          double sum = fr(fr(vertices[i].position.x + vertices[i].position.y) +
                          vertices[i].position.z);
          qa_waveform wave = deform.wave;
          wave.phase = fr(deform.wave.phase + fr(sum * deform.spread));
          scale = qa_mat_evaluate_waveform(wave, time);
        }
        vertices[i].position = qa_add3(vertices[i].position,
                                       qa_scale3(vertices[i].normal, scale));
      }
    } else {
      for (size_t i = 0; i < result.vertex_count; i++) {
        vertices[i] = result.vertices[i];
        qa_vec3 position = vertices[i].position;
        qa_vec3 normal = vertices[i].normal;
        if (eq(deform.kind, "normal")) {
          double scale = fr(0.98);
          double x = fr(position.x * scale), y = fr(position.y * scale);
          double z = fr(position.z * scale), t = fr(time * deform.frequency);
          normal = qa_normalize_fast3((qa_vec3){
              fr(normal.x + fr(deform.amplitude *
                  qa_renderer_noise_sample(noise, x, y, z, t))),
              fr(normal.y + fr(deform.amplitude *
                  qa_renderer_noise_sample(noise, fr(100 + x), y, z, t))),
              fr(normal.z + fr(deform.amplitude *
                  qa_renderer_noise_sample(noise, fr(200 + x), y, z, t)))});
        } else if (eq(deform.kind, "bulge")) {
          double now = fr(fr(fr(tess->refdef_time) * deform.speed) * fr(0.001));
          double phase = fr(fr(vertices[i].tex_coord.x * deform.width) + now);
          int32_t index = qa_mat_to_int32(fr(fr(1024 / (acos(-1.0) * 2)) * phase)) & 1023;
          position = qa_add3(position,
              qa_scale3(normal, fr(qa_renderer_sine(index) * deform.height)));
        }
        vertices[i].position = position;
        vertices[i].normal = normal;
      }
    }
    replace_with(tess, vertices, result.vertex_count, result.indices,
                 result.index_count);
    free(vertices);
  }
  return qa_material_deform_state_snapshot(tess);
}
