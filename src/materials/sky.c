/* Port of src/materials/sky.ts. GPL-2.0-or-later. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/materials/sky.h"

static float fr(double v) { return (float)v; }

const char *const qa_sky_face_suffixes[6] = {"rt", "lf", "bk", "ft", "up", "dn"};

static const qa_vec3 clip_planes[6] = {{1, 1, 0}, {1, -1, 0}, {0, -1, 1},
                                       {0, 1, 1}, {1, 0, 1}, {-1, 0, 1}};

qa_vec3 qa_sky_vector(double face, double s, double t, double size) {
  size = fr(size);
  double horizontal = fr(fr(s) * size), vertical = fr(fr(t) * size);
  qa_vec3 out;
  switch ((int)face) {
    case 0: out = (qa_vec3){size, -horizontal, vertical}; break;
    case 1: out = (qa_vec3){-size, horizontal, vertical}; break;
    case 2: out = (qa_vec3){horizontal, size, vertical}; break;
    case 3: out = (qa_vec3){-horizontal, -size, vertical}; break;
    case 4: out = (qa_vec3){-vertical, -horizontal, size}; break;
    case 5: out = (qa_vec3){vertical, -horizontal, -size}; break;
    default: qa_range_error("sky face must be 0..5");
  }
  return out;
}

static void project_polygon(const qa_vec3 *points, size_t count,
                            qa_sky_face_bounds *bounds) {
  qa_vec3 sum = {0, 0, 0};
  for (size_t i = 0; i < count; i++) sum = qa_add3(sum, points[i]);
  double x = fabs(sum.x), y = fabs(sum.y), z = fabs(sum.z);
  int face = (x > y && x > z) ? (sum.x < 0 ? 1 : 0)
           : (y > z && y > x) ? (sum.y < 0 ? 3 : 2)
                              : (sum.z < 0 ? 5 : 4);
  qa_sky_face_bounds *range = &bounds[face];
  for (size_t i = 0; i < count; i++) {
    qa_vec3 point = points[i];
    double divisor, s, t;
    switch (face) {
      case 0: divisor = point.x; s = -point.y; t = point.z; break;
      case 1: divisor = -point.x; s = point.y; t = point.z; break;
      case 2: divisor = point.y; s = point.x; t = point.z; break;
      case 3: divisor = -point.y; s = -point.x; t = point.z; break;
      case 4: divisor = point.z; s = -point.y; t = -point.x; break;
      default: divisor = -point.z; s = -point.y; t = point.x; break;
    }
    if (divisor < 0.001) continue;
    s = fr(s / divisor);
    t = fr(t / divisor);
    if (s < range->min_s) range->min_s = s;
    if (t < range->min_t) range->min_t = t;
    if (s > range->max_s) range->max_s = s;
    if (t > range->max_t) range->max_t = t;
  }
}

static void clip_polygon(const qa_vec3 *points, size_t count, int stage,
                         qa_sky_face_bounds *bounds) {
  if (count > 62) qa_range_error("sky polygon exceeds source clip vertex limit");
  if (stage == 6) {
    project_polygon(points, count, bounds);
    return;
  }
  qa_vec3 normal = clip_planes[stage];
  double distances[62];
  int sides[62];
  bool has_front = false, has_back = false;
  for (size_t i = 0; i < count; i++) {
    distances[i] = qa_dot3(points[i], normal);
    sides[i] = distances[i] > fr(0.1) ? 1 : distances[i] < -fr(0.1) ? -1 : 0;
    if (sides[i] == 1) has_front = true;
    if (sides[i] == -1) has_back = true;
  }
  if (!has_front || !has_back) {
    clip_polygon(points, count, stage + 1, bounds);
    return;
  }
  qa_vec3 front[64], back[64];
  size_t front_count = 0, back_count = 0;
  for (size_t i = 0; i < count; i++) {
    size_t next = (i + 1) % count;
    if (sides[i] >= 0) front[front_count++] = points[i];
    if (sides[i] <= 0) back[back_count++] = points[i];
    if (sides[i] == 0 || sides[next] == 0 || sides[next] == sides[i]) continue;
    double fraction = fr(distances[i] / fr(distances[i] - distances[next]));
    qa_vec3 intersection = qa_add3(
        points[i], qa_scale3(qa_sub3(points[next], points[i]), fraction));
    front[front_count++] = intersection;
    back[back_count++] = intersection;
  }
  clip_polygon(front, front_count, stage + 1, bounds);
  clip_polygon(back, back_count, stage + 1, bounds);
}

qa_vec2 qa_cloud_tex_coord(double face, double s, double t, double height) {
  height = fr(height);
  if (!isfinite(height))
    qa_range_error("sky cloud height must be finite float32");
  qa_vec3 direction = qa_sky_vector(face, s, t, 1024.0 / 1.75);
  const double radius = 4096;
  double xx = fr(direction.x * direction.x), yy = fr(direction.y * direction.y);
  double zz = fr(direction.z * direction.z), hh = fr(height * height);
  double discriminant = fr(zz * fr(radius * radius));
  discriminant = fr(discriminant + fr(fr(fr(2 * xx) * radius) * height));
  discriminant = fr(discriminant + fr(xx * hh));
  discriminant = fr(discriminant + fr(fr(fr(2 * yy) * radius) * height));
  discriminant = fr(discriminant + fr(yy * hh));
  discriminant = fr(discriminant + fr(fr(fr(2 * zz) * radius) * height));
  discriminant = fr(discriminant + fr(zz * hh));
  double inverse = fr(1.0 / fr(2 * qa_dot3(direction, direction)));
  double p = fr(inverse * (fr(fr(-2 * direction.z) * radius) +
                           2 * sqrt(discriminant)));
  qa_vec3 hit = {fr(direction.x * p), fr(direction.y * p),
                 fr(fr(direction.z * p) + radius)};
  qa_vec3 intersection = qa_normalize3(hit);
  qa_vec2 out = {fr(acos(intersection.x)), fr(acos(intersection.y))};
  return out;
}

void qa_sky_builder_init(qa_sky_builder *builder) {
  memset(builder->cloud_coords, 0, sizeof builder->cloud_coords);
  builder->bounds_count = 0;
}

void qa_sky_builder_initialize_cloud_coordinates(qa_sky_builder *builder,
                                                 double height) {
  if (!isfinite(fr(height)))
    qa_range_error("sky cloud height must be finite float32");
  for (int face = 0; face < 6; face++)
    for (int index = 0; index < 81; index++)
      builder->cloud_coords[face][index] = qa_cloud_tex_coord(
          face, (index % 9 - 4) / 4.0, (index / 9 - 4) / 4.0, height);
}

static qa_vec3 vertex_at(const qa_deform_geometry *mesh, double index_d) {
  int64_t index = (int64_t)index_d;
  if (index < 0 || (size_t)index >= mesh->vertex_count)
    qa_range_error("sky index %lld outside %zu", (long long)index,
                   mesh->vertex_count);
  return mesh->vertices[index].position;
}

void qa_sky_builder_clip(qa_sky_builder *builder,
                         const qa_deform_geometry *meshes, size_t mesh_count,
                         qa_vec3 origin) {
  for (int face = 0; face < 6; face++)
    builder->bounds[face] = (qa_sky_face_bounds){9999, 9999, -9999, -9999};
  builder->bounds_count = 6;
  for (size_t m = 0; m < mesh_count; m++) {
    const qa_deform_geometry *mesh = &meshes[m];
    for (size_t index = 0; index < mesh->index_count; index += 3) {
      qa_vec3 points[3];
      for (int offset = 0; offset < 3; offset++) {
        if (index + (size_t)offset >= mesh->index_count)
          qa_range_error("sky index %zu outside %zu", index + (size_t)offset,
                         mesh->index_count);
        points[offset] = qa_sub3(
            vertex_at(mesh, mesh->indices[index + (size_t)offset]), origin);
      }
      clip_polygon(points, 3, 0, builder->bounds);
    }
  }
}

static double clamp4(double value) {
  return value < -4 ? -4 : value > 4 ? 4 : value;
}

qa_sky_geometry qa_sky_builder_build(qa_sky_builder *builder, qa_vec3 origin,
                                     double far) {
  qa_sky_geometry result;
  memset(&result, 0, sizeof result);
  size_t face_capacity = 0, cloud_vertex_count = 0, cloud_index_count = 0;
  qa_material_vertex *cloud_vertices = NULL;
  double *cloud_indices = NULL;
  for (int face = 0; face < builder->bounds_count; face++) {
    qa_sky_face_bounds b = builder->bounds[face];
    double raw_min_s = floor(b.min_s * 4), raw_min_t = floor(b.min_t * 4);
    double raw_max_s = ceil(b.max_s * 4), raw_max_t = ceil(b.max_t * 4);
    if (raw_min_s >= raw_max_s || raw_min_t >= raw_max_t) continue;
    int min_s = (int)clamp4(raw_min_s), min_t = (int)clamp4(raw_min_t);
    int max_s = (int)clamp4(raw_max_s), max_t = (int)clamp4(raw_max_t);
    int width = max_s - min_s + 1;
    size_t vertex_count = (size_t)width * (size_t)(max_t - min_t + 1);
    size_t index_count = (size_t)(max_t - min_t) * (size_t)(max_s - min_s) * 6;
    qa_material_vertex *vertices = malloc(vertex_count * sizeof *vertices);
    double *indices = index_count > 0 ? malloc(index_count * sizeof *indices) : NULL;
    if (vertices == NULL || (index_count > 0 && indices == NULL))
      qa_range_error("sky allocation failed");
    size_t vi = 0;
    for (int t = min_t; t <= max_t; t++) {
      for (int s = min_s; s <= max_s; s++, vi++) {
        qa_vec3 direction = qa_sky_vector(face, s / 4.0, t / 4.0, far / 1.75);
        vertices[vi].position = qa_add3(direction, origin);
        vertices[vi].normal = qa_normalize3(qa_scale3(direction, -1));
        vertices[vi].tex_coord = (qa_vec2){(s / 4.0 + 1) / 2, 1 - (t / 4.0 + 1) / 2};
        vertices[vi].lightmap_coord = (qa_vec2){0, 0};
        vertices[vi].color = (qa_vec4){255, 255, 255, 255};
      }
    }
    size_t strip_count = (size_t)(max_t - min_t);
    qa_sky_index_strip *strips = strip_count > 0
        ? malloc(strip_count * sizeof *strips) : NULL;
    if (strip_count > 0 && strips == NULL) qa_range_error("sky allocation failed");
    size_t ii = 0;
    for (int t = 0; t < max_t - min_t; t++) {
      size_t strip_len = (size_t)width * 2;
      strips[t].indices = malloc(strip_len * sizeof *strips[t].indices);
      if (strips[t].indices == NULL) qa_range_error("sky allocation failed");
      strips[t].index_count = strip_len;
      for (int s = 0; s < width; s++) {
        strips[t].indices[s * 2] = s + t * width;
        strips[t].indices[s * 2 + 1] = s + (t + 1) * width;
      }
      for (int s = 0; s < max_s - min_s; s++) {
        int index = s + t * width;
        indices[ii++] = index;
        indices[ii++] = index + width;
        indices[ii++] = index + 1;
        indices[ii++] = index + width;
        indices[ii++] = index + width + 1;
        indices[ii++] = index + 1;
      }
    }
    if (result.face_count == face_capacity) {
      face_capacity = face_capacity == 0 ? 4 : face_capacity * 2;
      result.box = realloc(result.box, face_capacity * sizeof *result.box);
      if (result.box == NULL) qa_range_error("sky allocation failed");
    }
    qa_sky_face *entry = &result.box[result.face_count++];
    entry->face = face;
    entry->geometry.vertices = vertices;
    entry->geometry.vertex_count = vertex_count;
    entry->geometry.indices = indices;
    entry->geometry.index_count = index_count;
    entry->strips = strips;
    entry->strip_count = strip_count;
    if (face == 5) continue;
    size_t cloud_start = cloud_vertex_count;
    cloud_vertices = realloc(cloud_vertices,
        (cloud_vertex_count + vertex_count) * sizeof *cloud_vertices);
    cloud_indices = realloc(cloud_indices,
        (cloud_index_count + index_count) * sizeof *cloud_indices);
    if (cloud_vertices == NULL || (index_count > 0 && cloud_indices == NULL))
      qa_range_error("sky allocation failed");
    for (int t = min_t; t <= max_t; t++) {
      for (int s = min_s; s <= max_s; s++) {
        qa_material_vertex vertex = vertices[s - min_s + (t - min_t) * width];
        vertex.tex_coord = builder->cloud_coords[face][(s + 4) + (t + 4) * 9];
        cloud_vertices[cloud_vertex_count++] = vertex;
      }
    }
    for (size_t i = 0; i < index_count; i++)
      cloud_indices[cloud_index_count++] = (double)cloud_start + indices[i];
  }
  result.clouds.vertices = cloud_vertices;
  result.clouds.vertex_count = cloud_vertex_count;
  result.clouds.indices = cloud_indices;
  result.clouds.index_count = cloud_index_count;
  return result;
}

void qa_sky_geometry_destroy(qa_sky_geometry *geometry) {
  for (size_t i = 0; i < geometry->face_count; i++) {
    free((void *)geometry->box[i].geometry.vertices);
    free((void *)geometry->box[i].geometry.indices);
    for (size_t s = 0; s < geometry->box[i].strip_count; s++)
      free(geometry->box[i].strips[s].indices);
    free(geometry->box[i].strips);
  }
  free(geometry->box);
  free((void *)geometry->clouds.vertices);
  free((void *)geometry->clouds.indices);
  memset(geometry, 0, sizeof *geometry);
}
