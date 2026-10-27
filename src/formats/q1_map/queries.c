/* Port of src/formats/q1-map/queries.ts. GPL-2.0-or-later. */
#include "qa/formats/q1_map/queries.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/formats/q1_map/records.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static noreturn void plain_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

uint8_t *qa_q1_decompress_pvs(const uint8_t *data, size_t data_length, bool has_offset,
                              double offset, double visible_leaves, size_t *out_length) {
  if (visible_leaves != trunc(visible_leaves) || visible_leaves < 0 ||
      visible_leaves > 9007199254740991.0) {
    qa_range_error("Invalid visible leaf count");
  }
  size_t length = (size_t)ceil(visible_leaves / 8);
  uint8_t *output = xmalloc(length);
  memset(output, 0, length);
  *out_length = length;
  if (!has_offset || data_length == 0) {
    memset(output, 255, length);
    return output;
  }
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)data, data_length, "Quake PVS"))
    qa_range_error("Out of memory");
  qa_binary_reader_seek(&reader, (int64_t)offset);
  size_t position = 0;
  while (position < length) {
    uint8_t value = qa_binary_reader_u8(&reader);
    if (value != 0) { output[position++] = value; continue; }
    uint8_t count = qa_binary_reader_u8(&reader);
    if (count == 0 || count > length - position) {
      qa_binary_error(reader.source, (int64_t)reader.cursor - 1, "invalid zero run %u", (unsigned)count);
    }
    position += count;
  }
  qa_binary_reader_destroy(&reader);
  return output;
}

uint8_t *qa_q1_leaf_pvs(const qa_q1_world_geometry *map, double leaf_index, size_t *out_length) {
  qa_q1_check_index(leaf_index, (double)map->leaf_count, "Quake PVS leaf");
  const qa_q1_leaf *leaf = &map->leaves[(size_t)leaf_index];
  double count = map->model_count > 0 ? map->models[0].visible_leaves
                                       : (double)map->leaf_count - 1 > 0 ? (double)map->leaf_count - 1 : 0;
  bool has_offset = leaf_index != 0 && leaf->has_visibility_offset;
  return qa_q1_decompress_pvs(map->visibility.bytes, map->visibility.length, has_offset,
                              leaf->visibility_offset, count, out_length);
}

double qa_q1_find_leaf(const qa_q1_world_geometry *map, qa_vec3 point, double model_index) {
  qa_q1_check_index(model_index, (double)map->model_count, "Quake model");
  const qa_q1_world_model *model = &map->models[(size_t)model_index];
  if (model->headnodes.count == 0) qa_range_error("Missing Quake model root");
  double next = model->headnodes.values[0];
  for (size_t visited = 0; visited <= map->node_count; visited++) {
    if (next < 0) {
      double leaf = -1 - next;
      qa_q1_check_index(leaf, (double)map->leaf_count, "Quake leaf");
      return leaf;
    }
    if (next != trunc(next) || next >= (double)map->node_count)
      qa_range_error("Missing Quake node %g", next);
    const qa_bsp_node *node = &map->nodes[(size_t)next];
    if (node->plane != trunc(node->plane) || node->plane < 0 || node->plane >= (double)map->plane_count)
      qa_range_error("Missing Quake plane %g", node->plane);
    const qa_bsp_plane *plane = &map->planes[(size_t)node->plane];
    /* Contract BspPlane nests Plane + type/signbits, mirroring TS fields. */
    double distance = point.x * plane->plane.normal.x + point.y * plane->plane.normal.y +
                      point.z * plane->plane.normal.z - plane->plane.distance;
    qa_bsp_child child = node->children[distance > 0 ? 0 : 1];
    if (!child.is_node) return child.index;
    next = child.index;
  }
  plain_error("Cycle in Quake BSP nodes");
}

qa_vec3 *qa_q1_face_vertices(const qa_q1_world_geometry *map, double face_index, size_t *out_count) {
  qa_q1_check_index(face_index, (double)map->face_count, "Quake face");
  const qa_bsp_face *face = &map->faces[(size_t)face_index];
  size_t count = face->edges.count > 0 ? (size_t)face->edges.count : 0;
  qa_vec3 *vertices = xmalloc(count * sizeof *vertices);
  for (size_t i = 0; i < count; i++) {
    double at = face->edges.first + (double)i;
    if (at != trunc(at) || at < 0 || at >= (double)map->surface_edges.count)
      qa_range_error("Missing Quake surface edge");
    double edge_index = map->surface_edges.values[(size_t)at];
    double edge_at = fabs(edge_index);
    if (edge_at != trunc(edge_at) || edge_at >= (double)map->edge_count)
      qa_range_error("Missing Quake edge");
    const qa_bsp_edge *edge = &map->edges[(size_t)edge_at];
    double vertex_at = edge->vertices[edge_index >= 0 ? 0 : 1];
    if (vertex_at != trunc(vertex_at) || vertex_at < 0 || vertex_at >= (double)map->vertex_count)
      qa_range_error("Missing Quake vertex");
    vertices[i] = map->vertices[(size_t)vertex_at];
  }
  *out_count = count;
  return vertices;
}

double qa_q1_face_at_contact(const qa_q1_world_geometry *map, const double *candidates,
                             size_t candidate_count, qa_vec3 point, qa_plane plane, bool *out_found) {
  qa_vec3 normal = plane.normal;
  double distance = (point.x * normal.x + point.y * normal.y + point.z * normal.z - plane.distance) /
                    (normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
  qa_vec3 projected = {point.x - normal.x * distance, point.y - normal.y * distance,
                       point.z - normal.z * distance};
  for (size_t c = 0; c < candidate_count; c++) {
    size_t count = 0;
    qa_vec3 *vertices = qa_q1_face_vertices(map, candidates[c], &count);
    bool positive = false, negative = false;
    for (size_t i = 0; i < count; i++) {
      qa_vec3 a = vertices[i], b = vertices[(i + 1) % count];
      qa_vec3 edge = {b.x - a.x, b.y - a.y, b.z - a.z};
      qa_vec3 off = {projected.x - a.x, projected.y - a.y, projected.z - a.z};
      double side = (edge.y * off.z - edge.z * off.y) * normal.x +
                    (edge.z * off.x - edge.x * off.z) * normal.y +
                    (edge.x * off.y - edge.y * off.x) * normal.z;
      positive = positive || side > 0;
      negative = negative || side < 0;
      if (positive && negative) break;
    }
    free(vertices);
    if (count >= 3 && !(positive && negative)) {
      *out_found = true;
      return candidates[c];
    }
  }
  *out_found = false;
  return 0;
}
