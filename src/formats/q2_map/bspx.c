/* Port of src/formats/q2-map/bspx.ts. GPL-2.0-or-later. */
#include "qa/formats/q2_map/bspx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void list_push(qa_q2_string_list *list, char *value) {
  list->values = xrealloc(list->values, (list->count + 1) * sizeof *list->values);
  list->values[list->count++] = value;
}

void qa_q2_string_list_destroy(qa_q2_string_list *list) {
  if (list == NULL) return;
  for (size_t i = 0; i < list->count; i++) free(list->values[i]);
  free(list->values);
  list->values = NULL;
  list->count = 0;
}

static void section_or_die(const qa_binary_reader *reader, int64_t offset, int64_t length,
                           qa_binary_reader *out) {
  if (!qa_binary_reader_section(reader, offset, length, out)) qa_range_error("Out of memory");
}

static qa_binary_reader lump_reader(const qa_q2_bspx_lump *lump, const char *source) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, lump->bytes, lump->length, source)) qa_range_error("Out of memory");
  return reader;
}

bool qa_read_q2_bspx(qa_binary_reader *reader, double end_of_lumps, qa_q2_bspx_directory *out) {
  memset(out, 0, sizeof *out);
  double offset = ceil(end_of_lumps / 4) * 4;
  if (offset > (double)reader->length - 8) return false;
  qa_binary_reader directory;
  section_or_die(reader, (int64_t)offset, (int64_t)reader->length - (int64_t)offset, &directory);
  char *magic = qa_binary_reader_fixed_byte_string(&directory, 4);
  bool is_bspx = magic != NULL && strcmp(magic, "BSPX") == 0;
  free(magic);
  if (!is_bspx) {
    qa_binary_reader_destroy(&directory);
    return false;
  }
  out->offset = offset;
  uint32_t count = qa_binary_reader_u32(&directory);
  if ((double)count > (double)qa_binary_reader_remaining(&directory) / 32) {
    list_push(&out->diagnostics, xstrdup("Truncated BSPX directory"));
    qa_binary_reader_destroy(&directory);
    return true;
  }
  for (uint32_t i = 0; i < count; i++) {
    char *name = qa_binary_reader_fixed_byte_string(&directory, 24);
    double file_offset = qa_binary_reader_u32(&directory);
    double length = qa_binary_reader_u32(&directory);
    bool duplicate = qa_q2_bspx_find(out, name) != NULL;
    if (length == 0 || file_offset > (double)reader->length - length || duplicate) {
      char message[512];
      snprintf(message, sizeof message, "Ignored empty, out-of-bounds, or duplicate BSPX lump %s", name);
      list_push(&out->diagnostics, xstrdup(message));
      free(name);
      continue;
    }
    qa_binary_reader section;
    section_or_die(reader, (int64_t)file_offset, (int64_t)length, &section);
    qa_q2_bspx_lump lump;
    lump.name = name;
    lump.offset = file_offset;
    lump.bytes = qa_binary_reader_bytes(&section, (int64_t)length);
    lump.length = (size_t)length;
    qa_binary_reader_destroy(&section);
    out->lumps = xrealloc(out->lumps, (out->lump_count + 1) * sizeof *out->lumps);
    out->lumps[out->lump_count++] = lump;
  }
  qa_binary_reader_destroy(&directory);
  return true;
}

const qa_q2_bspx_lump *qa_q2_bspx_find(const qa_q2_bspx_directory *directory, const char *name) {
  if (directory == NULL) return NULL;
  for (size_t i = 0; i < directory->lump_count; i++) {
    if (strcmp(directory->lumps[i].name, name) == 0) return &directory->lumps[i];
  }
  return NULL;
}

void qa_q2_bspx_directory_destroy(qa_q2_bspx_directory *directory) {
  if (directory == NULL) return;
  for (size_t i = 0; i < directory->lump_count; i++) {
    free(directory->lumps[i].name);
    free(directory->lumps[i].bytes);
  }
  free(directory->lumps);
  qa_q2_string_list_destroy(&directory->diagnostics);
  memset(directory, 0, sizeof *directory);
}

static qa_vec3 vector(qa_binary_reader *reader) {
  qa_vec3 v = {qa_binary_reader_finite_f32(reader), qa_binary_reader_finite_f32(reader),
               qa_binary_reader_finite_f32(reader)};
  return v;
}

static qa_vec3 integer_vector(qa_binary_reader *reader) {
  qa_vec3 v = {qa_binary_reader_u32(reader), qa_binary_reader_u32(reader), qa_binary_reader_u32(reader)};
  return v;
}

void qa_read_q2_face_normals(const qa_q2_bspx_lump *lump, double corner_count, qa_q2_face_normals *out) {
  memset(out, 0, sizeof *out);
  qa_binary_reader reader = lump_reader(lump, "BSPX FACENORMALS");
  uint32_t count = qa_binary_reader_u32(&reader);
  qa_binary_reader records;
  if (!qa_binary_reader_records(&reader, (int64_t)reader.cursor,
                                (int64_t)((double)count * 12 + corner_count * 12), 12, &records))
    qa_range_error("Out of memory");
  qa_binary_reader_destroy(&records);
  out->normals = xmalloc(count * sizeof *out->normals);
  out->normal_count = count;
  for (uint32_t i = 0; i < count; i++) out->normals[i] = vector(&reader);
  size_t corners = (size_t)corner_count;
  out->corner_indices = xmalloc(corners * sizeof *out->corner_indices);
  out->corner_count = corners;
  for (size_t i = 0; i < corners; i++) {
    double normal = qa_binary_reader_u32(&reader);
    if (normal >= count) {
      qa_binary_error(reader.source, (int64_t)reader.cursor - 4, "invalid face normal index");
    }
    out->corner_indices[i][0] = normal;
    out->corner_indices[i][1] = qa_binary_reader_u32(&reader);
    out->corner_indices[i][2] = qa_binary_reader_u32(&reader);
  }
  qa_binary_reader_destroy(&reader);
}

void qa_q2_face_normals_destroy(qa_q2_face_normals *normals) {
  if (normals == NULL) return;
  free(normals->normals);
  free(normals->corner_indices);
  memset(normals, 0, sizeof *normals);
}

void qa_read_q2_decoupled_lightmaps(const qa_q2_bspx_lump *lump, double face_count, double lighting_bytes,
                                    qa_q2_decoupled_lightmaps *out) {
  memset(out, 0, sizeof *out);
  qa_binary_reader reader = lump_reader(lump, "BSPX DECOUPLED_LM");
  qa_binary_reader records;
  if (!qa_binary_reader_records(&reader, 0, (int64_t)reader.length, 40, &records))
    qa_range_error("Out of memory");
  qa_binary_reader_destroy(&records);
  if (face_count > (double)reader.length / 40) {
    qa_binary_error(reader.source, 0, "fewer lightmaps than faces");
  }
  size_t faces = (size_t)face_count;
  out->faces = xmalloc(faces * sizeof *out->faces);
  out->face_count = faces;
  for (size_t i = 0; i < faces; i++) {
    out->faces[i].width = qa_binary_reader_u16(&reader);
    out->faces[i].height = qa_binary_reader_u16(&reader);
    uint32_t raw_offset = qa_binary_reader_u32(&reader);
    bool has_offset = raw_offset != 0xffffffffu;
    double lighting_offset = raw_offset;
    if (has_offset && lighting_offset >= lighting_bytes) {
      char message[256];
      snprintf(message, sizeof message, "DECOUPLED_LM face %zu has invalid lighting offset %g", i,
               lighting_offset);
      list_push(&out->diagnostics, xstrdup(message));
      has_offset = false;
    }
    out->faces[i].has_lighting_offset = has_offset;
    out->faces[i].lighting_offset = lighting_offset;
    qa_vec3 s = vector(&reader);
    double x = qa_binary_reader_finite_f32(&reader);
    qa_vec3 t = vector(&reader);
    double y = qa_binary_reader_finite_f32(&reader);
    out->faces[i].axes[0] = s;
    out->faces[i].axes[1] = t;
    out->faces[i].offset.x = x;
    out->faces[i].offset.y = y;
  }
  qa_binary_reader_destroy(&reader);
}

void qa_q2_decoupled_lightmaps_destroy(qa_q2_decoupled_lightmaps *lightmaps) {
  if (lightmaps == NULL) return;
  free(lightmaps->faces);
  qa_q2_string_list_destroy(&lightmaps->diagnostics);
  memset(lightmaps, 0, sizeof *lightmaps);
}

static qa_q2_lightgrid_child read_child(qa_binary_reader *reader) {
  uint32_t value = qa_binary_reader_u32(reader);
  qa_q2_lightgrid_child child;
  if ((value & 0x40000000u) != 0) {
    child.kind = QA_Q2_LIGHTGRID_OCCLUDED;
    child.index = 0;
  } else if ((value & 0x80000000u) != 0) {
    child.kind = QA_Q2_LIGHTGRID_LEAF;
    child.index = value & 0x7fffffffu;
  } else {
    child.kind = QA_Q2_LIGHTGRID_NODE;
    child.index = value;
  }
  return child;
}

void qa_read_q2_lightgrid(const qa_q2_bspx_lump *lump, qa_q2_lightgrid *out) {
  memset(out, 0, sizeof *out);
  qa_binary_reader reader = lump_reader(lump, "BSPX LIGHTGRID_OCTREE");
  out->spacing = vector(&reader);
  if (out->spacing.x <= 0 || out->spacing.y <= 0 || out->spacing.z <= 0) {
    qa_binary_error(reader.source, 0, "lightgrid spacing must be positive");
  }
  out->scale.x = 1 / out->spacing.x;
  out->scale.y = 1 / out->spacing.y;
  out->scale.z = 1 / out->spacing.z;
  out->size = integer_vector(&reader);
  out->min = vector(&reader);
  double style_count = qa_binary_reader_u8(&reader);
  if (style_count < 1 || style_count > 4) {
    qa_binary_error(reader.source, 36, "invalid lightgrid style count");
  }
  out->style_count = style_count;
  out->root = read_child(&reader);
  uint32_t node_count = qa_binary_reader_u32(&reader);
  qa_binary_reader records;
  if (!qa_binary_reader_records(&reader, (int64_t)reader.cursor, (int64_t)node_count * 44, 44, &records))
    qa_range_error("Out of memory");
  qa_binary_reader_destroy(&records);
  qa_q2_lightgrid_node *nodes = xmalloc(node_count * sizeof *nodes);
  for (uint32_t i = 0; i < node_count; i++) {
    nodes[i].point = integer_vector(&reader);
    for (int j = 0; j < 8; j++) nodes[i].children[j] = read_child(&reader);
  }
  out->nodes = nodes;
  out->node_count = node_count;
  uint32_t leaf_count = qa_binary_reader_u32(&reader);
  if ((double)leaf_count > (double)qa_binary_reader_remaining(&reader) / 24) {
    qa_binary_error(reader.source, (int64_t)reader.cursor - 4, "invalid lightgrid leaf count");
  }
  qa_q2_lightgrid_leaf *leaves = xmalloc(leaf_count * sizeof *leaves);
  qa_q2_lightgrid_sample *samples = NULL;
  size_t sample_count = 0, sample_capacity = 0;
  qa_q2_lightgrid_sample occluded = {255, {255, 255, 255}};
  for (uint32_t i = 0; i < leaf_count; i++) {
    qa_vec3 leaf_min = integer_vector(&reader);
    qa_vec3 leaf_size = integer_vector(&reader);
    double point_count = leaf_size.x * leaf_size.y * leaf_size.z;
    if (point_count != trunc(point_count) || point_count < 0 || point_count > 9007199254740991.0 ||
        point_count > (double)qa_binary_reader_remaining(&reader)) {
      qa_binary_error(reader.source, (int64_t)reader.cursor, "invalid lightgrid point count");
    }
    double first_sample = (double)sample_count;
    for (double point = 0; point < point_count; point++) {
      uint8_t count = qa_binary_reader_u8(&reader);
      if (count != 255 && count > style_count) {
        qa_binary_error(reader.source, (int64_t)reader.cursor - 1, "too many sample styles");
      }
      for (double style = 0; style < style_count; style++) {
        qa_q2_lightgrid_sample sample;
        if (count != 255 && style < count) {
          sample.style = qa_binary_reader_u8(&reader);
          sample.rgb.x = qa_binary_reader_u8(&reader);
          sample.rgb.y = qa_binary_reader_u8(&reader);
          sample.rgb.z = qa_binary_reader_u8(&reader);
        } else {
          sample = occluded;
        }
        if (sample_count == sample_capacity) {
          sample_capacity = sample_capacity ? sample_capacity * 2 : 64;
          samples = xrealloc(samples, sample_capacity * sizeof *samples);
        }
        samples[sample_count++] = sample;
      }
    }
    leaves[i].min = leaf_min;
    leaves[i].size = leaf_size;
    leaves[i].first_sample = first_sample;
    leaves[i].point_count = point_count;
  }
  out->leaves = leaves;
  out->leaf_count = leaf_count;
  out->samples = samples;
  out->sample_count = sample_count;
  /* Validate the octree iteratively, as in the TS. */
  qa_q2_lightgrid_child *pending = NULL;
  size_t pending_count = 0, pending_capacity = 0;
  uint8_t *visited = xmalloc(node_count);
  memset(visited, 0, node_count);
  pending = xrealloc(pending, sizeof *pending);
  pending[pending_count++] = out->root;
  pending_capacity = 1;
  while (pending_count > 0 && leaf_count > 0) {
    qa_q2_lightgrid_child next = pending[--pending_count];
    if (next.kind == QA_Q2_LIGHTGRID_OCCLUDED) continue;
    if (next.kind == QA_Q2_LIGHTGRID_LEAF) {
      if (next.index >= leaf_count) qa_binary_error(reader.source, 37, "invalid lightgrid leaf reference");
      continue;
    }
    if (next.index != trunc(next.index) || next.index < 0 || next.index >= node_count ||
        visited[(size_t)next.index]) {
      qa_binary_error(reader.source, 37, "invalid or repeated lightgrid node");
    }
    visited[(size_t)next.index] = 1;
    for (int j = 0; j < 8; j++) {
      if (pending_count == pending_capacity) {
        pending_capacity *= 2;
        pending = xrealloc(pending, pending_capacity * sizeof *pending);
      }
      pending[pending_count++] = nodes[(size_t)next.index].children[j];
    }
  }
  free(pending);
  free(visited);
  qa_binary_reader_destroy(&reader);
}

void qa_q2_lightgrid_destroy(qa_q2_lightgrid *grid) {
  if (grid == NULL) return;
  free((void *)grid->nodes);
  free((void *)grid->leaves);
  free((void *)grid->samples);
  memset(grid, 0, sizeof *grid);
}

bool qa_lookup_q2_lightgrid(const qa_q2_lightgrid *grid, qa_vec3 point,
                            const qa_q2_lightgrid_sample **out, size_t *out_count) {
  if (point.x != trunc(point.x) || point.y != trunc(point.y) || point.z != trunc(point.z)) return false;
  qa_q2_lightgrid_child next = grid->root;
  while (next.kind == QA_Q2_LIGHTGRID_NODE) {
    if (next.index != trunc(next.index) || next.index < 0 || next.index >= (double)grid->node_count)
      return false;
    const qa_q2_lightgrid_node *node = &grid->nodes[(size_t)next.index];
    int octant = (point.x >= node->point.x ? 4 : 0) | (point.y >= node->point.y ? 2 : 0) |
                 (point.z >= node->point.z ? 1 : 0);
    next = node->children[octant];
  }
  if (next.kind == QA_Q2_LIGHTGRID_OCCLUDED) return false;
  if (next.index != trunc(next.index) || next.index < 0 || next.index >= (double)grid->leaf_count)
    return false;
  const qa_q2_lightgrid_leaf *leaf = &grid->leaves[(size_t)next.index];
  double x = point.x - leaf->min.x;
  double y = point.y - leaf->min.y;
  double z = point.z - leaf->min.z;
  if (x < 0 || y < 0 || z < 0 || x >= leaf->size.x || y >= leaf->size.y || z >= leaf->size.z) return false;
  double first = leaf->first_sample + (leaf->size.x * (leaf->size.y * z + y) + x) * grid->style_count;
  if (first < 0 || first > (double)grid->sample_count) return false;
  size_t start = (size_t)first;
  size_t available = grid->sample_count - start;
  size_t wanted = (size_t)grid->style_count;
  *out = grid->samples + start;
  *out_count = available < wanted ? available : wanted;
  return true;
}
