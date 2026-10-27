/* Port of src/formats/q2-map/reader.ts. GPL-2.0-or-later. */
#include "qa/formats/q2_map/reader.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}

static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *copy = xmalloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}

static qa_vec3 vector(qa_binary_reader *reader) {
  qa_vec3 v = {qa_binary_reader_finite_f32(reader), qa_binary_reader_finite_f32(reader),
               qa_binary_reader_finite_f32(reader)};
  return v;
}

static qa_vec3 short_vector(qa_binary_reader *reader) {
  qa_vec3 v = {qa_binary_reader_i16(reader), qa_binary_reader_i16(reader), qa_binary_reader_i16(reader)};
  return v;
}

static qa_vec4 projection(qa_binary_reader *reader) {
  qa_vec4 v = {qa_binary_reader_finite_f32(reader), qa_binary_reader_finite_f32(reader),
               qa_binary_reader_finite_f32(reader), qa_binary_reader_finite_f32(reader)};
  return v;
}

static qa_bounds read_bounds(qa_binary_reader *reader, bool extended) {
  qa_bounds bounds;
  if (extended) { bounds.min = vector(reader); bounds.max = vector(reader); }
  else { bounds.min = short_vector(reader); bounds.max = short_vector(reader); }
  return bounds;
}

static double read_index(qa_binary_reader *reader, bool extended) {
  return extended ? qa_binary_reader_u32(reader) : qa_binary_reader_u16(reader);
}

static double nullable_index(qa_binary_reader *reader, bool extended) {
  double value = read_index(reader, extended);
  return value == (extended ? 4294967295.0 : 65535.0) ? -1 : value;
}

static qa_index_range read_range(qa_binary_reader *reader, bool extended) {
  qa_index_range range = {read_index(reader, extended), read_index(reader, extended)};
  return range;
}

static qa_bsp_child read_child_value(double value) {
  qa_bsp_child child;
  if (value < 0) { child.is_node = false; child.index = -1 - value; }
  else { child.is_node = true; child.index = value; }
  return child;
}

static qa_binary_reader lump_records(qa_binary_reader *reader, qa_q2_lump lump, int64_t stride,
                                     size_t *out_count) {
  qa_binary_reader section;
  if (!qa_binary_reader_records(reader, (int64_t)lump.offset, (int64_t)lump.length, stride, &section))
    qa_range_error("Out of memory");
  *out_count = stride > 0 ? (size_t)((int64_t)lump.length / stride) : 0;
  return section;
}

static qa_q2_visibility *read_visibility(qa_binary_reader *reader) {
  if (reader->length == 0) return NULL;
  qa_binary_reader copy_section;
  if (!qa_binary_reader_section(reader, 0, (int64_t)reader->length, &copy_section))
    qa_range_error("Out of memory");
  uint8_t *compressed = qa_binary_reader_bytes(&copy_section, (int64_t)reader->length);
  qa_binary_reader_destroy(&copy_section);
  uint32_t count = qa_binary_reader_u32(reader);
  qa_binary_reader records;
  if (!qa_binary_reader_records(reader, (int64_t)reader->cursor, (int64_t)count * 8, 8, &records))
    qa_range_error("Out of memory");
  qa_binary_reader_destroy(&records);
  qa_q2_visibility_cluster *clusters = xmalloc(count * sizeof *clusters);
  for (uint32_t i = 0; i < count; i++) {
    double pvs_offset = qa_binary_reader_i32(reader);
    double phs_offset = qa_binary_reader_i32(reader);
    double offsets[2] = {pvs_offset, phs_offset};
    for (int j = 0; j < 2; j++) {
      if (offsets[j] != -1 && (offsets[j] < 4 + (double)count * 8 || offsets[j] >= (double)reader->length)) {
        qa_binary_error(reader->source, (int64_t)reader->cursor - 8,
                        "visibility offset is outside compressed data");
      }
    }
    clusters[i].pvs_offset = pvs_offset;
    clusters[i].phs_offset = phs_offset;
  }
  qa_q2_visibility *result = xmalloc(sizeof *result);
  result->clusters = clusters;
  result->cluster_count = count;
  result->compressed.bytes = compressed;
  result->compressed.length = reader->length;
  return result;
}

static void validate_q2_bsp(const qa_q2_bsp *map);

void qa_read_q2_bsp(const uint8_t *bytes, size_t length, const char *source, qa_q2_bsp *out) {
  memset(out, 0, sizeof *out);
  const char *name = source != NULL ? source : "<q2-map>";
  out->source = xstrdup(name);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) qa_range_error("Out of memory");
  char *magic = qa_binary_reader_fixed_byte_string(&reader, 4);
  if (magic == NULL || (strcmp(magic, "IBSP") != 0 && strcmp(magic, "QBSP") != 0)) {
    qa_binary_error(name, 0, "unsupported Q2 map identifier %s", magic != NULL ? magic : "");
  }
  bool extended = strcmp(magic, "QBSP") == 0;
  free(magic);
  uint32_t version = qa_binary_reader_u32(&reader);
  if (version != 38) qa_binary_error(name, 4, "unsupported Q2 BSP version %u", (unsigned)version);
  out->format = extended ? "qbsp" : "ibsp38";
  out->version = 38;
  out->lumps = xmalloc(19 * sizeof *out->lumps);
  out->lump_count = 19;
  double end_of_lumps = 160;
  for (int number = 0; number < 19; number++) {
    double offset = qa_binary_reader_u32(&reader);
    double lump_length = qa_binary_reader_u32(&reader);
    if (number == 0 && offset < (double)reader.length && lump_length > (double)reader.length - offset) {
      lump_length = (double)reader.length - offset;
      out->diagnostics.values = xrealloc(out->diagnostics.values,
                                         (out->diagnostics.count + 1) * sizeof(char *));
      out->diagnostics.values[out->diagnostics.count++] = xstrdup("Clamped entity lump to the end of the file");
    }
    qa_binary_reader checked;
    if (!qa_binary_reader_section(&reader, (int64_t)offset, (int64_t)lump_length, &checked))
      qa_range_error("Out of memory");
    qa_binary_reader_destroy(&checked);
    out->lumps[number].offset = offset;
    out->lumps[number].length = lump_length;
    if (offset + lump_length > end_of_lumps) end_of_lumps = offset + lump_length;
  }
#define LUMP(n) (out->lumps[n])
  /* Entities */
  {
    qa_binary_reader section;
    if (!qa_binary_reader_section(&reader, (int64_t)LUMP(0).offset, (int64_t)LUMP(0).length, &section))
      qa_range_error("Out of memory");
    out->entity_bytes = qa_binary_reader_bytes(&section, (int64_t)LUMP(0).length);
    out->entity_byte_length = (size_t)LUMP(0).length;
    qa_binary_reader_destroy(&section);
    char entity_source[1024];
    snprintf(entity_source, sizeof entity_source, "%s:entities", name);
    qa_binary_reader entity_reader;
    if (!qa_binary_reader_init(&entity_reader, out->entity_bytes, out->entity_byte_length, entity_source))
      qa_range_error("Out of memory");
    out->entities = qa_binary_reader_fixed_byte_string(&entity_reader, (int64_t)out->entity_byte_length);
    qa_binary_reader_destroy(&entity_reader);
  }
  size_t count = 0;
  qa_binary_reader row;
  row = lump_records(&reader, LUMP(1), 20, &count);
  out->planes = xmalloc(count * sizeof *out->planes);
  out->plane_count = count;
  for (size_t i = 0; i < count; i++) {
    qa_vec3 normal = vector(&row);
    out->planes[i].plane.normal = normal;
    out->planes[i].plane.distance = qa_binary_reader_finite_f32(&row);
    out->planes[i].type = qa_binary_reader_i32(&row);
    out->planes[i].signbits = (normal.x < 0 ? 1 : 0) | (normal.y < 0 ? 2 : 0) | (normal.z < 0 ? 4 : 0);
  }
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(2), 12, &count);
  out->vertices = xmalloc(count * sizeof *out->vertices);
  out->vertex_count = count;
  for (size_t i = 0; i < count; i++) out->vertices[i] = vector(&row);
  qa_binary_reader_destroy(&row);
  {
    qa_binary_reader section;
    if (!qa_binary_reader_section(&reader, (int64_t)LUMP(3).offset, (int64_t)LUMP(3).length, &section))
      qa_range_error("Out of memory");
    out->visibility = read_visibility(&section);
    qa_binary_reader_destroy(&section);
  }
  row = lump_records(&reader, LUMP(4), extended ? 44 : 28, &count);
  out->nodes = xmalloc(count * sizeof *out->nodes);
  out->node_count = count;
  for (size_t i = 0; i < count; i++) {
    out->nodes[i].plane = qa_binary_reader_u32(&row);
    out->nodes[i].children[0] = read_child_value(qa_binary_reader_i32(&row));
    out->nodes[i].children[1] = read_child_value(qa_binary_reader_i32(&row));
    out->nodes[i].bounds = read_bounds(&row, extended);
    out->nodes[i].faces = read_range(&row, extended);
  }
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(5), 76, &count);
  out->texture_info = xmalloc(count * sizeof *out->texture_info);
  out->texture_info_count = count;
  for (size_t i = 0; i < count; i++) {
    out->texture_info[i].projection.s = projection(&row);
    out->texture_info[i].projection.t = projection(&row);
    out->texture_info[i].flags = qa_binary_reader_i32(&row);
    out->texture_info[i].value = qa_binary_reader_i32(&row);
    out->texture_info[i].name = qa_binary_reader_fixed_byte_string(&row, 32);
    out->texture_info[i].next = qa_binary_reader_i32(&row);
  }
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(6), extended ? 28 : 20, &count);
  out->faces = xmalloc(count * sizeof *out->faces);
  out->face_count = count;
  for (size_t i = 0; i < count; i++) {
    out->faces[i].plane = read_index(&row, extended);
    out->faces[i].draw_flags = read_index(&row, extended);
    out->faces[i].edges.first = qa_binary_reader_u32(&row);
    out->faces[i].edges.count = read_index(&row, extended);
    out->faces[i].texture_info = read_index(&row, extended);
    for (int j = 0; j < 4; j++) out->faces[i].styles[j] = qa_binary_reader_u8(&row);
    out->faces[i].lighting_offset = qa_binary_reader_i32(&row);
  }
  qa_binary_reader_destroy(&row);
  {
    qa_binary_reader section;
    if (!qa_binary_reader_section(&reader, (int64_t)LUMP(7).offset, (int64_t)LUMP(7).length, &section))
      qa_range_error("Out of memory");
    out->lighting = qa_binary_reader_bytes(&section, (int64_t)LUMP(7).length);
    out->lighting_length = (size_t)LUMP(7).length;
    qa_binary_reader_destroy(&section);
  }
  row = lump_records(&reader, LUMP(8), extended ? 52 : 28, &count);
  out->leaves = xmalloc(count * sizeof *out->leaves);
  out->leaf_count = count;
  for (size_t i = 0; i < count; i++) {
    out->leaves[i].contents = qa_binary_reader_i32(&row);
    out->leaves[i].cluster = nullable_index(&row, extended);
    out->leaves[i].area = read_index(&row, extended);
    out->leaves[i].bounds = read_bounds(&row, extended);
    out->leaves[i].faces = read_range(&row, extended);
    out->leaves[i].brushes = read_range(&row, extended);
  }
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(9), extended ? 4 : 2, &count);
  out->leaf_faces = xmalloc(count * sizeof *out->leaf_faces);
  out->leaf_face_count = count;
  for (size_t i = 0; i < count; i++) out->leaf_faces[i] = read_index(&row, extended);
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(10), extended ? 4 : 2, &count);
  out->leaf_brushes = xmalloc(count * sizeof *out->leaf_brushes);
  out->leaf_brush_count = count;
  for (size_t i = 0; i < count; i++) out->leaf_brushes[i] = read_index(&row, extended);
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(11), extended ? 8 : 4, &count);
  out->edges = xmalloc(count * sizeof *out->edges);
  out->edge_count = count;
  for (size_t i = 0; i < count; i++) {
    out->edges[i].vertices[0] = read_index(&row, extended);
    out->edges[i].vertices[1] = read_index(&row, extended);
  }
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(12), 4, &count);
  out->surface_edges = xmalloc(count * sizeof *out->surface_edges);
  out->surface_edge_count = count;
  for (size_t i = 0; i < count; i++) out->surface_edges[i] = qa_binary_reader_i32(&row);
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(13), 48, &count);
  out->models = xmalloc(count * sizeof *out->models);
  out->model_count = count;
  for (size_t i = 0; i < count; i++) {
    out->models[i].bounds = read_bounds(&row, true);
    out->models[i].origin = vector(&row);
    out->models[i].headnode = qa_binary_reader_i32(&row);
    out->models[i].faces = read_range(&row, true);
  }
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(14), 12, &count);
  out->brushes = xmalloc(count * sizeof *out->brushes);
  out->brush_count = count;
  for (size_t i = 0; i < count; i++) {
    out->brushes[i].sides = read_range(&row, true);
    out->brushes[i].contents = qa_binary_reader_i32(&row);
  }
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(15), extended ? 8 : 4, &count);
  out->brush_sides = xmalloc(count * sizeof *out->brush_sides);
  out->brush_side_count = count;
  for (size_t i = 0; i < count; i++) {
    out->brush_sides[i].plane = read_index(&row, extended);
    out->brush_sides[i].texture_info = nullable_index(&row, extended);
  }
  qa_binary_reader_destroy(&row);
  {
    qa_binary_reader section;
    if (!qa_binary_reader_section(&reader, (int64_t)LUMP(16).offset, (int64_t)LUMP(16).length, &section))
      qa_range_error("Out of memory");
    out->pop = qa_binary_reader_bytes(&section, (int64_t)LUMP(16).length);
    out->pop_length = (size_t)LUMP(16).length;
    qa_binary_reader_destroy(&section);
  }
  row = lump_records(&reader, LUMP(17), 8, &count);
  out->areas = xmalloc(count * sizeof *out->areas);
  out->area_count = count;
  for (size_t i = 0; i < count; i++) {
    double portal_count = qa_binary_reader_u32(&row);
    out->areas[i].portals.first = qa_binary_reader_u32(&row);
    out->areas[i].portals.count = portal_count;
  }
  qa_binary_reader_destroy(&row);
  row = lump_records(&reader, LUMP(18), 8, &count);
  out->area_portals = xmalloc(count * sizeof *out->area_portals);
  out->area_portal_count = count;
  for (size_t i = 0; i < count; i++) {
    out->area_portals[i].portal = qa_binary_reader_u32(&row);
    out->area_portals[i].other_area = qa_binary_reader_u32(&row);
  }
  qa_binary_reader_destroy(&row);
#undef LUMP
  out->has_bspx = qa_read_q2_bspx(&reader, end_of_lumps, &out->bspx);
  qa_binary_reader_destroy(&reader);
  validate_q2_bsp(out);
}

static const qa_q2_bsp *validate_map;

static void validate_fail(const char *message) {
  qa_binary_error(validate_map->source, 0, "%s", message);
}

static void reference(double value, double count, const char *label) {
  if (value < 0 || value >= count) {
    char message[256];
    snprintf(message, sizeof message, "Invalid %s index %g for %g records", label, value, count);
    validate_fail(message);
  }
}

static void span(qa_index_range value, double count, const char *label) {
  if (value.first < 0 || value.count < 0 || value.first > count - value.count) {
    char message[256];
    snprintf(message, sizeof message, "Invalid %s range", label);
    validate_fail(message);
  }
}

static void node_reference(qa_bsp_child value) {
  reference(value.index, value.is_node ? (double)validate_map->node_count : (double)validate_map->leaf_count,
            value.is_node ? "node" : "leaf");
}

typedef struct {
  qa_bsp_child child;
  bool leaving;
} pending_node;

static void validate_q2_bsp(const qa_q2_bsp *map) {
  validate_map = map;
  if (map->model_count == 0 || map->node_count == 0 || map->leaf_count == 0)
    validate_fail("Map must contain models, nodes and leaves");
  if (map->leaves[0].contents != 1) validate_fail("Map leaf 0 is not CONTENTS_SOLID");
  for (size_t i = 0; i < map->edge_count; i++)
    for (int j = 0; j < 2; j++) reference(map->edges[i].vertices[j], (double)map->vertex_count, "vertex");
  for (size_t i = 0; i < map->surface_edge_count; i++)
    reference(fabs(map->surface_edges[i]), (double)map->edge_count, "surface edge");
  for (size_t i = 0; i < map->face_count; i++) {
    const qa_q2_raw_face *face = &map->faces[i];
    reference(face->plane, (double)map->plane_count, "face plane");
    reference(face->texture_info, (double)map->texture_info_count, "face texture");
    span(face->edges, (double)map->surface_edge_count, "face edges");
    if (face->edges.count < 3) validate_fail("Face has fewer than three edges");
    if (face->lighting_offset < -1 ||
        (map->lighting_length > 0 && face->lighting_offset >= (double)map->lighting_length))
      validate_fail("Invalid face lighting offset");
  }
  for (size_t i = 0; i < map->texture_info_count; i++) {
    if (map->texture_info[i].next > 0)
      reference(map->texture_info[i].next, (double)map->texture_info_count, "animated texture");
  }
  for (size_t i = 0; i < map->brush_count; i++)
    span(map->brushes[i].sides, (double)map->brush_side_count, "brush sides");
  for (size_t i = 0; i < map->brush_side_count; i++) {
    reference(map->brush_sides[i].plane, (double)map->plane_count, "brush plane");
    if (map->brush_sides[i].texture_info != -1)
      reference(map->brush_sides[i].texture_info, (double)map->texture_info_count, "brush texture");
  }
  for (size_t i = 0; i < map->leaf_brush_count; i++)
    reference(map->leaf_brushes[i], (double)map->brush_count, "leaf brush");
  for (size_t i = 0; i < map->leaf_face_count; i++)
    reference(map->leaf_faces[i], (double)map->face_count, "leaf face");
  for (size_t i = 0; i < map->leaf_count; i++) {
    const qa_q2_raw_leaf *leaf = &map->leaves[i];
    span(leaf->faces, (double)map->leaf_face_count, "leaf faces");
    span(leaf->brushes, (double)map->leaf_brush_count, "leaf brushes");
    reference(leaf->area, (double)map->area_count, "leaf area");
    if (map->visibility != NULL && leaf->cluster != -1)
      reference(leaf->cluster, (double)map->visibility->cluster_count, "leaf cluster");
  }
  for (size_t i = 0; i < map->node_count; i++) {
    reference(map->nodes[i].plane, (double)map->plane_count, "node plane");
    span(map->nodes[i].faces, (double)map->face_count, "node faces");
    for (int j = 0; j < 2; j++) node_reference(map->nodes[i].children[j]);
  }
  for (size_t i = 0; i < map->model_count; i++) {
    node_reference(read_child_value(map->models[i].headnode));
    span(map->models[i].faces, (double)map->face_count, "model faces");
  }
  for (size_t i = 0; i < map->area_count; i++)
    span(map->areas[i].portals, (double)map->area_portal_count, "area portals");
  for (size_t i = 0; i < map->area_portal_count; i++) {
    reference(map->area_portals[i].other_area, (double)map->area_count, "portal area");
    reference(map->area_portals[i].portal, (double)map->area_portal_count, "portal");
  }
  uint8_t *complete = xmalloc(map->node_count);
  uint8_t *active = xmalloc(map->node_count);
  memset(complete, 0, map->node_count);
  memset(active, 0, map->node_count);
  for (size_t m = 0; m < map->model_count; m++) {
    pending_node *pending = NULL;
    size_t pending_count = 0, pending_capacity = 0;
#define PUSH(item)                                                     \
  do {                                                                 \
    if (pending_count == pending_capacity) {                           \
      pending_capacity = pending_capacity ? pending_capacity * 2 : 16; \
      pending = xrealloc(pending, pending_capacity * sizeof *pending); \
    }                                                                  \
    pending[pending_count++] = (item);                                 \
  } while (0)
    pending_node root = {read_child_value(map->models[m].headnode), false};
    PUSH(root);
    while (pending_count > 0) {
      pending_node item = pending[--pending_count];
      if (!item.child.is_node) continue;
      size_t number = (size_t)item.child.index;
      if (item.leaving) { active[number] = 0; complete[number] = 1; continue; }
      if (active[number]) validate_fail("Cycle in BSP nodes");
      if (complete[number]) continue;
      active[number] = 1;
      pending_node leaving = {item.child, true};
      PUSH(leaving);
      pending_node second = {map->nodes[number].children[1], false};
      pending_node first = {map->nodes[number].children[0], false};
      PUSH(second);
      PUSH(first);
    }
    free(pending);
#undef PUSH
  }
  free(complete);
  free(active);
}

void qa_q2_bsp_destroy(qa_q2_bsp *map) {
  if (map == NULL) return;
  free(map->source);
  free(map->lumps);
  free(map->entities);
  free(map->entity_bytes);
  free(map->planes);
  free(map->vertices);
  free(map->edges);
  free(map->surface_edges);
  free(map->nodes);
  free(map->leaves);
  free(map->leaf_faces);
  free(map->leaf_brushes);
  for (size_t i = 0; i < map->texture_info_count; i++) free(map->texture_info[i].name);
  free(map->texture_info);
  free(map->faces);
  free(map->brushes);
  free(map->brush_sides);
  free(map->models);
  free(map->areas);
  free(map->area_portals);
  if (map->visibility != NULL) {
    free((void *)map->visibility->clusters);
    free((void *)map->visibility->compressed.bytes);
    free(map->visibility);
  }
  free(map->lighting);
  free(map->pop);
  if (map->has_bspx) qa_q2_bspx_directory_destroy(&map->bspx);
  qa_q2_string_list_destroy(&map->diagnostics);
  memset(map, 0, sizeof *map);
}

uint8_t *qa_decompress_q2_visibility(const qa_q2_visibility *visibility, double cluster, const char *kind,
                                     double cluster_count, size_t *out_length) {
  double count = visibility != NULL ? (double)visibility->cluster_count : cluster_count;
  size_t length = (size_t)ceil(count / 8);
  uint8_t *row = xmalloc(length);
  memset(row, 0, length);
  *out_length = length;
  if (cluster == -1) return row;
  if (visibility == NULL) {
    memset(row, 255, length);
    return row;
  }
  if (cluster != trunc(cluster) || cluster < 0 || cluster >= (double)visibility->cluster_count) {
    qa_range_error("Invalid Q2 visibility cluster %g", cluster);
  }
  const qa_q2_visibility_cluster *entry = &visibility->clusters[(size_t)cluster];
  double offset = strcmp(kind, "pvs") == 0 ? entry->pvs_offset : entry->phs_offset;
  if (offset == -1) {
    memset(row, 255, length);
    return row;
  }
  char source[128];
  snprintf(source, sizeof source, "Q2 %s cluster %g", kind, cluster);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)visibility->compressed.bytes,
                             visibility->compressed.length, source))
    qa_range_error("Out of memory");
  qa_binary_reader_seek(&reader, (int64_t)offset);
  size_t output = 0;
  while (output < length) {
    uint8_t value = qa_binary_reader_u8(&reader);
    if (value != 0) { row[output++] = value; continue; }
    uint8_t run = qa_binary_reader_u8(&reader);
    if (run == 0 || run > length - output) {
      qa_binary_error(reader.source, (int64_t)reader.cursor - 1, "invalid visibility zero run");
    }
    output += run;
  }
  qa_binary_reader_destroy(&reader);
  return row;
}
