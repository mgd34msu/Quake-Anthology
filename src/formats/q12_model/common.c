/* Port of src/formats/q12-model/common.ts. GPL-2.0-or-later. */
#include "qa/formats/q12_model/common.h"

#include <math.h>
#include <stdlib.h>

#include "qa/foundation/support.h"

#define FR(value) ((double)(float)(value))

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

noreturn void qa_q12_fail(qa_binary_reader *reader, const char *message) {
  qa_binary_error(reader->source, (int64_t)reader->cursor, "%s", message);
}

double qa_q12_count(qa_binary_reader *reader, const char *label, double minimum) {
  double value = qa_binary_reader_i32(reader);
  if (value < minimum) {
    qa_binary_error(reader->source, (int64_t)reader->cursor, "%s %g is below %g", label, value, minimum);
  }
  return value;
}

void qa_q12_version(qa_binary_reader *reader, double expected) {
  double actual = qa_binary_reader_i32(reader);
  if (actual != expected) {
    qa_binary_error(reader->source, (int64_t)reader->cursor, "version %g, expected %g", actual, expected);
  }
}

qa_vec3 qa_q12_vector(qa_binary_reader *reader) {
  qa_vec3 v = {qa_binary_reader_finite_f32(reader), qa_binary_reader_finite_f32(reader),
               qa_binary_reader_finite_f32(reader)};
  return v;
}

const char *qa_q12_sync_type(qa_binary_reader *reader) {
  int32_t value = qa_binary_reader_i32(reader);
  if (value == 0) return "synchronized";
  if (value == 1) return "random";
  qa_binary_error(reader->source, (int64_t)reader->cursor, "invalid synchronization type %d", (int)value);
}

bool qa_q12_group_type(qa_binary_reader *reader) {
  int32_t value = qa_binary_reader_i32(reader);
  if (value == 0) return false;
  if (value == 1) return true;
  qa_binary_error(reader->source, (int64_t)reader->cursor, "invalid frame/skin type %d", (int)value);
}

double *qa_q12_intervals(qa_binary_reader *reader, double frame_count, size_t *out_count) {
  (void)qa_binary_reader_data_view(reader, (int64_t)reader->cursor, (int64_t)(frame_count * 4));
  size_t count = (size_t)frame_count;
  double *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    double endpoint = qa_binary_reader_finite_f32(reader);
    if (endpoint <= 0) qa_q12_fail(reader, "group interval must be positive");
    result[i] = endpoint;
  }
  *out_count = count;
  return result;
}

qa_timed_frames qa_q12_read_timed(qa_binary_reader *reader, qa_q12_read_frame_fn read_frame, void *ctx) {
  qa_timed_frames result;
  if (!qa_q12_group_type(reader)) {
    result.is_group = false;
    result.single_frame = read_frame(ctx);
    result.frames = NULL;
    result.frame_count = 0;
    return result;
  }
  double count = qa_q12_count(reader, "group frames", 1);
  size_t endpoint_count = 0;
  double *endpoints = qa_q12_intervals(reader, count, &endpoint_count);
  qa_timed_frame_entry *entries = xmalloc(endpoint_count * sizeof *entries);
  for (size_t i = 0; i < endpoint_count; i++) {
    entries[i].interval_seconds = endpoints[i];
    entries[i].frame = read_frame(ctx);
  }
  free(endpoints);
  result.is_group = true;
  result.single_frame = NULL;
  result.frames = entries;
  result.frame_count = endpoint_count;
  return result;
}

qa_vec3 qa_q12_expand_position(qa_vec3 position, qa_vec3 scale, qa_vec3 translate) {
  qa_vec3 v = {FR(FR(position.x * scale.x) + translate.x), FR(FR(position.y * scale.y) + translate.y),
               FR(FR(position.z * scale.z) + translate.z)};
  return v;
}

qa_packed_alias_vertex qa_q12_packed_vertex(qa_binary_reader *reader) {
  qa_packed_alias_vertex vertex;
  vertex.position[0] = qa_binary_reader_u8(reader);
  vertex.position[1] = qa_binary_reader_u8(reader);
  vertex.position[2] = qa_binary_reader_u8(reader);
  vertex.normal_index = qa_binary_reader_u8(reader);
  return vertex;
}

qa_vec3 qa_q12_packed_position(qa_packed_alias_vertex vertex, qa_vec3 scale, qa_vec3 translate) {
  qa_vec3 position = {vertex.position[0], vertex.position[1], vertex.position[2]};
  return qa_q12_expand_position(position, scale, translate);
}

qa_bounds qa_q12_bounds_from_points(const qa_vec3 *points, size_t count) {
  if (count == 0) qa_range_error("Cannot bound an empty model");
  qa_bounds bounds = {points[0], points[0]};
  for (size_t i = 0; i < count; i++) {
    if (points[i].x < bounds.min.x) bounds.min.x = points[i].x;
    if (points[i].y < bounds.min.y) bounds.min.y = points[i].y;
    if (points[i].z < bounds.min.z) bounds.min.z = points[i].z;
    if (points[i].x > bounds.max.x) bounds.max.x = points[i].x;
    if (points[i].y > bounds.max.y) bounds.max.y = points[i].y;
    if (points[i].z > bounds.max.z) bounds.max.z = points[i].z;
  }
  return bounds;
}

qa_bounds qa_q12_union_bounds(const qa_bounds *bounds, size_t count) {
  qa_vec3 *points = xmalloc(count * 2 * sizeof *points);
  for (size_t i = 0; i < count; i++) {
    points[i * 2] = bounds[i].min;
    points[i * 2 + 1] = bounds[i].max;
  }
  qa_bounds result = qa_q12_bounds_from_points(points, count * 2);
  free(points);
  return result;
}

double qa_q12_index(qa_binary_reader *reader, double value, double limit, const char *label) {
  if (value < 0 || value >= limit) {
    qa_binary_error(reader->source, (int64_t)reader->cursor, "%s index %g outside 0..%g", label, value,
                    limit - 1);
  }
  return value;
}

size_t qa_q12_at(double index, size_t count, const char *label) {
  if (index != trunc(index) || index < 0 || index >= (double)count) {
    qa_range_error("Missing %s %g", label, index);
  }
  return (size_t)index;
}
