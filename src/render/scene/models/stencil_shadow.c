/* Port of src/render/scene/models/stencil-shadow.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/stencil_shadow.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

#define MAX_EDGE_DEFS 32
#define SHADOW_EXTRUSION 512

/* Storage sized once, like id's static edgeDefs/numEdgeDefs/facing tables. */
static int32_t edge_counts[QA_STENCIL_SHADOW_MAX_VERTEXES];
static int32_t edge_targets[QA_STENCIL_SHADOW_MAX_VERTEXES * MAX_EDGE_DEFS];
static uint8_t edge_facing[QA_STENCIL_SHADOW_MAX_VERTEXES * MAX_EDGE_DEFS];

static double f(double value) { return (double)(float)value; }
static void add_edge(size_t from, int32_t to, uint8_t facing) {
  int32_t used = edge_counts[from];
  if (used == MAX_EDGE_DEFS) return;
  edge_targets[from * MAX_EDGE_DEFS + (size_t)used] = to;
  edge_facing[from * MAX_EDGE_DEFS + (size_t)used] = facing;
  edge_counts[from] = used + 1;
}
/* positions[index] is undefined in TS unless index is an integer within the array. */
static bool vertex_index(const double *indices, size_t at, size_t count, size_t *out) {
  double value = indices[at];
  if (!(value >= 0 && value < (double)count && value == floor(value))) return false;
  *out = (size_t)value;
  return true;
}
bool qa_make_stencil_shadow_volume(const qa_vec3 *positions, size_t count, const double *indices, size_t index_count,
                              qa_vec3 light_dir, qa_stencil_shadow_volume *out) {
  memset(out, 0, sizeof *out);
  if (count >= QA_STENCIL_SHADOW_MAX_VERTEXES || index_count % 3 != 0) return false;
  memset(edge_counts, 0, count * sizeof *edge_counts);
  size_t triangles = index_count / 3;
  for (size_t triangle = 0; triangle < triangles; triangle++) {
    size_t i1, i2, i3;
    if (!vertex_index(indices, triangle * 3, count, &i1) || !vertex_index(indices, triangle * 3 + 1, count, &i2) ||
        !vertex_index(indices, triangle * 3 + 2, count, &i3))
      return false;
    qa_vec3 v1 = positions[i1], v2 = positions[i2], v3 = positions[i3];
    double d1x = f(v2.x - v1.x), d1y = f(v2.y - v1.y), d1z = f(v2.z - v1.z);
    double d2x = f(v3.x - v1.x), d2y = f(v3.y - v1.y), d2z = f(v3.z - v1.z);
    double nx = f(f(d1y * d2z) - f(d1z * d2y)), ny = f(f(d1z * d2x) - f(d1x * d2z)), nz = f(f(d1x * d2y) - f(d1y * d2x));
    uint8_t facing = f(f(f(nx * light_dir.x) + f(ny * light_dir.y)) + f(nz * light_dir.z)) > 0 ? 1 : 0;
    add_edge(i1, (int32_t)i2, facing);
    add_edge(i2, (int32_t)i3, facing);
    add_edge(i3, (int32_t)i1, facing);
  }
  qa_vec3 *volume = malloc((count > 0 ? count * 2 : 1) * sizeof *volume);
  if (volume == NULL) qa_range_error("Out of memory");
  if (count > 0) memcpy(volume, positions, count * sizeof *volume);
  for (size_t i = 0; i < count; i++) {
    qa_vec3 p = positions[i];
    volume[count + i].x = f(p.x + f(-SHADOW_EXTRUSION * light_dir.x));
    volume[count + i].y = f(p.y + f(-SHADOW_EXTRUSION * light_dir.y));
    volume[count + i].z = f(p.z + f(-SHADOW_EXTRUSION * light_dir.z));
  }
  size_t capacity = 0, n = 0;
  double *result = NULL;
  for (size_t vertex = 0; vertex < count; vertex++) {
    for (int32_t edge = 0; edge < edge_counts[vertex]; edge++) {
      if (edge_facing[vertex * MAX_EDGE_DEFS + (size_t)edge] == 0) continue;
      size_t other = (size_t)edge_targets[vertex * MAX_EDGE_DEFS + (size_t)edge];
      int front_hits = 0;
      for (int32_t candidate = 0; candidate < edge_counts[other]; candidate++)
        if (edge_targets[other * MAX_EDGE_DEFS + (size_t)candidate] == (int32_t)vertex && edge_facing[other * MAX_EDGE_DEFS + (size_t)candidate] == 1)
          front_hits++;
      /* A front-facing edge that no other front-facing triangle shares is on the silhouette. */
      if (front_hits != 0) continue;
      if (n + 6 > capacity) {
        capacity = capacity ? capacity * 2 : 24;
        double *grown = realloc(result, capacity * sizeof *grown);
        if (grown == NULL) { free(result); free(volume); qa_range_error("Out of memory"); }
        result = grown;
      }
      double v = (double)vertex, o = (double)other, c = (double)count;
      result[n++] = v; result[n++] = c + v; result[n++] = o;
      result[n++] = o; result[n++] = c + v; result[n++] = c + o;
    }
  }
  out->positions = volume; out->position_count = count * 2;
  out->indices = result; out->index_count = n;
  return true;
}
void qa_stencil_shadow_volume_destroy(qa_stencil_shadow_volume *volume) {
  free(volume->positions); free(volume->indices);
  memset(volume, 0, sizeof *volume);
}
