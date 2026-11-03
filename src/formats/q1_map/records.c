/* Port of src/formats/q1-map/records.ts. GPL-2.0-or-later. */
#include "qa/formats/q1_map/records.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdlib.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

qa_vec3 qa_q1_read_vec3(qa_binary_reader *reader) {
  qa_vec3 v; /* TS reads x, y, z in order; C leaves initializer order open */
  v.x = qa_binary_reader_finite_f32(reader);
  v.y = qa_binary_reader_finite_f32(reader);
  v.z = qa_binary_reader_finite_f32(reader);
  return v;
}

qa_vec4 qa_q1_read_vec4(qa_binary_reader *reader) {
  qa_vec4 v;
  v.x = qa_binary_reader_finite_f32(reader);
  v.y = qa_binary_reader_finite_f32(reader);
  v.z = qa_binary_reader_finite_f32(reader);
  v.w = qa_binary_reader_finite_f32(reader);
  return v;
}

static qa_vec3 short_vec3(qa_binary_reader *reader) {
  qa_vec3 v;
  v.x = qa_binary_reader_i16(reader);
  v.y = qa_binary_reader_i16(reader);
  v.z = qa_binary_reader_i16(reader);
  return v;
}

qa_bounds qa_q1_read_bounds(qa_binary_reader *reader, bool floats) {
  qa_bounds b;
  if (floats) { b.min = qa_q1_read_vec3(reader); b.max = qa_q1_read_vec3(reader); }
  else { b.min = short_vec3(reader); b.max = short_vec3(reader); }
  return b;
}

void qa_q1_check_index(double value, double count, const char *source) {
  if (value != trunc(value) || value < 0 || value >= count) {
    qa_binary_error(source, 0, "index %s outside %s records", QA_JS_NUMBER(value), QA_JS_NUMBER(count));
  }
}

void qa_q1_check_range(qa_index_range value, double count, const char *source) {
  if (value.first != trunc(value.first) || value.count != trunc(value.count) ||
      value.first < 0 || value.count < 0 || value.first > count - value.count) {
    qa_binary_error(source, 0, "range %s+%s outside %s records", QA_JS_NUMBER(value.first), QA_JS_NUMBER(value.count), QA_JS_NUMBER(count));
  }
}

bool qa_q1_optional_offset(double value, double *out) {
  if (value == -1 || value == 4294967295.0) return false;
  *out = value;
  return true;
}

static size_t record_count(qa_binary_reader *reader, size_t stride) {
  if (reader->length % stride != 0) {
    qa_binary_error(reader->source, 0, "lump length %zu is not a multiple of %zu", reader->length, stride);
  }
  return reader->length / stride;
}

qa_bsp_plane *qa_q1_read_planes(qa_binary_reader *reader, size_t *out_count) {
  size_t count = record_count(reader, 20);
  qa_bsp_plane *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    qa_vec3 normal = qa_q1_read_vec3(reader);
    double distance = qa_binary_reader_finite_f32(reader);
    double type = qa_binary_reader_i32(reader);
    result[i].plane.normal = normal;
    result[i].plane.distance = distance;
    result[i].type = type;
    result[i].signbits = (normal.x < 0 ? 1 : 0) | (normal.y < 0 ? 2 : 0) | (normal.z < 0 ? 4 : 0);
  }
  *out_count = count;
  return result;
}

static qa_bsp_child node_child(qa_binary_reader *reader, qa_q1_bsp_format format, double node_count) {
  qa_bsp_child child;
  if (format == QA_Q1_FORMAT_BSP29) {
    double value = qa_binary_reader_u16(reader);
    if (value < node_count) { child.is_node = true; child.index = value; }
    else { child.is_node = false; child.index = 65535 - value; }
    return child;
  }
  int32_t value = qa_binary_reader_i32(reader);
  if (value >= 0) { child.is_node = true; child.index = value; }
  else { child.is_node = false; child.index = -1 - (double)value; }
  return child;
}

qa_bsp_node *qa_q1_read_nodes(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count) {
  size_t stride = format == QA_Q1_FORMAT_BSP29 ? 24 : format == QA_Q1_FORMAT_2PSB ? 32 : 44;
  size_t count = record_count(reader, stride);
  qa_bsp_node *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    result[i].plane = qa_binary_reader_i32(reader);
    result[i].children[0] = node_child(reader, format, (double)count);
    result[i].children[1] = node_child(reader, format, (double)count);
    result[i].bounds = qa_q1_read_bounds(reader, format == QA_Q1_FORMAT_BSP2);
    if (format == QA_Q1_FORMAT_BSP29) {
      result[i].faces.first = qa_binary_reader_u16(reader);
      result[i].faces.count = qa_binary_reader_u16(reader);
    } else {
      result[i].faces.first = qa_binary_reader_u32(reader);
      result[i].faces.count = qa_binary_reader_u32(reader);
    }
  }
  *out_count = count;
  return result;
}

static qa_q1_clip_child clip_child(qa_binary_reader *reader, qa_q1_bsp_format format, double count) {
  double value = format == QA_Q1_FORMAT_BSP29 ? qa_binary_reader_u16(reader) : qa_binary_reader_i32(reader);
  if (format == QA_Q1_FORMAT_BSP29 && value >= count) value -= 65536;
  qa_q1_clip_child child;
  if (value < 0) { child.is_clipnode = false; child.value = value; }
  else { child.is_clipnode = true; child.value = value; }
  return child;
}

qa_q1_clip_node *qa_q1_read_clipnodes(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count) {
  size_t stride = format == QA_Q1_FORMAT_BSP29 ? 8 : 12;
  size_t count = record_count(reader, stride);
  qa_q1_clip_node *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    result[i].plane = qa_binary_reader_i32(reader);
    result[i].children[0] = clip_child(reader, format, (double)count);
    result[i].children[1] = clip_child(reader, format, (double)count);
  }
  *out_count = count;
  return result;
}

qa_q1_leaf *qa_q1_read_leaves(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count) {
  size_t stride = format == QA_Q1_FORMAT_BSP29 ? 28 : format == QA_Q1_FORMAT_2PSB ? 32 : 44;
  size_t count = record_count(reader, stride);
  qa_q1_leaf *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    result[i].contents = qa_binary_reader_i32(reader);
    result[i].has_visibility_offset = qa_q1_optional_offset(qa_binary_reader_i32(reader), &result[i].visibility_offset);
    result[i].bounds = qa_q1_read_bounds(reader, format == QA_Q1_FORMAT_BSP2);
    if (format == QA_Q1_FORMAT_BSP29) {
      result[i].faces.first = qa_binary_reader_u16(reader);
      result[i].faces.count = qa_binary_reader_u16(reader);
    } else {
      result[i].faces.first = qa_binary_reader_u32(reader);
      result[i].faces.count = qa_binary_reader_u32(reader);
    }
    for (int j = 0; j < 4; j++) result[i].ambient_sound[j] = qa_binary_reader_u8(reader);
  }
  *out_count = count;
  return result;
}

qa_bsp_edge *qa_q1_read_edges(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count) {
  size_t stride = format == QA_Q1_FORMAT_BSP29 ? 4 : 8;
  size_t count = record_count(reader, stride);
  qa_bsp_edge *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    if (format == QA_Q1_FORMAT_BSP29) {
      result[i].vertices[0] = qa_binary_reader_u16(reader);
      result[i].vertices[1] = qa_binary_reader_u16(reader);
    } else {
      result[i].vertices[0] = qa_binary_reader_u32(reader);
      result[i].vertices[1] = qa_binary_reader_u32(reader);
    }
  }
  *out_count = count;
  return result;
}

qa_bsp_face *qa_q1_read_faces(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count) {
  size_t stride = format == QA_Q1_FORMAT_BSP29 ? 20 : 28;
  size_t count = record_count(reader, stride);
  qa_bsp_face *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    result[i].plane = format == QA_Q1_FORMAT_BSP29 ? qa_binary_reader_u16(reader) : qa_binary_reader_u32(reader);
    double side = format == QA_Q1_FORMAT_BSP29 ? qa_binary_reader_u16(reader) : qa_binary_reader_u32(reader);
    if (side != 0 && side != 1) qa_binary_error(reader->source, (int64_t)reader->cursor, "invalid face side %s", QA_JS_NUMBER(side));
    result[i].edges.first = qa_binary_reader_i32(reader);
    result[i].edges.count = format == QA_Q1_FORMAT_BSP29 ? qa_binary_reader_u16(reader) : qa_binary_reader_u32(reader);
    result[i].texture_info = format == QA_Q1_FORMAT_BSP29 ? qa_binary_reader_u16(reader) : qa_binary_reader_u32(reader);
    double *styles = xmalloc(4 * sizeof *styles);
    for (int j = 0; j < 4; j++) styles[j] = qa_binary_reader_u8(reader);
    result[i].styles.values = styles;
    result[i].styles.count = 4;
    result[i].back = side == 1;
    result[i].has_lighting_offset = qa_q1_optional_offset(qa_binary_reader_i32(reader), &result[i].lighting_offset);
  }
  *out_count = count;
  return result;
}

qa_q1_texture_info *qa_q1_read_texture_info(qa_binary_reader *reader, size_t *out_count) {
  size_t count = record_count(reader, 40);
  qa_q1_texture_info *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    result[i].projection.s = qa_q1_read_vec4(reader);
    result[i].projection.t = qa_q1_read_vec4(reader);
    result[i].texture = qa_binary_reader_i32(reader);
    result[i].flags = qa_binary_reader_i32(reader);
  }
  *out_count = count;
  return result;
}

qa_q1_world_model *qa_q1_read_models(qa_binary_reader *reader, size_t *out_count) {
  size_t count = record_count(reader, 64);
  qa_q1_world_model *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    result[i].bounds = qa_q1_read_bounds(reader, true);
    result[i].origin = qa_q1_read_vec3(reader);
    double *headnodes = xmalloc(4 * sizeof *headnodes);
    for (int j = 0; j < 4; j++) headnodes[j] = qa_binary_reader_i32(reader);
    result[i].headnodes.values = headnodes;
    result[i].headnodes.count = 4;
    result[i].visible_leaves = qa_binary_reader_i32(reader);
    result[i].faces.first = qa_binary_reader_i32(reader);
    result[i].faces.count = qa_binary_reader_i32(reader);
  }
  *out_count = count;
  return result;
}

qa_vec3 *qa_q1_read_vertices(qa_binary_reader *reader, size_t *out_count) {
  size_t count = record_count(reader, 12);
  qa_vec3 *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) result[i] = qa_q1_read_vec3(reader);
  *out_count = count;
  return result;
}

double *qa_q1_read_surface_edges(qa_binary_reader *reader, size_t *out_count) {
  size_t count = record_count(reader, 4);
  double *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) result[i] = qa_binary_reader_i32(reader);
  *out_count = count;
  return result;
}

double *qa_q1_read_leaf_faces(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count) {
  size_t stride = format == QA_Q1_FORMAT_BSP29 ? 2 : 4;
  size_t count = record_count(reader, stride);
  double *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    result[i] = format == QA_Q1_FORMAT_BSP29 ? qa_binary_reader_u16(reader) : qa_binary_reader_u32(reader);
  }
  *out_count = count;
  return result;
}
