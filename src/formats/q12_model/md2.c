/* Port of src/formats/q12-model/md2.ts. GPL-2.0-or-later. */
#include "qa/formats/q12_model/md2.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/q12_model/common.h"
#include "qa/formats/q12_model/normals.h"
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

static qa_md2_command *read_commands(qa_binary_reader *reader, double vertex_count, size_t *out_count) {
  qa_md2_command *result = NULL;
  size_t count = 0, capacity = 0;
  *out_count = 0;
  if (reader->length == 0) return NULL;
  while (qa_binary_reader_remaining(reader) > 0) {
    int32_t command = qa_binary_reader_i32(reader);
    if (command == 0) {
      *out_count = count;
      return result;
    }
    double vertices = fabs((double)command);
    if (vertices < 3) qa_q12_fail(reader, "GL strip/fan must have at least three vertices");
    (void)qa_binary_reader_data_view(reader, (int64_t)reader->cursor, (int64_t)(vertices * 12));
    qa_md2_command entry;
    entry.kind = command > 0 ? "strip" : "fan";
    entry.vertex_count = (size_t)vertices;
    entry.vertices = xmalloc(entry.vertex_count * sizeof *entry.vertices);
    for (size_t i = 0; i < entry.vertex_count; i++) {
      entry.vertices[i].tex_coord.x = qa_binary_reader_finite_f32(reader);
      entry.vertices[i].tex_coord.y = qa_binary_reader_finite_f32(reader);
      entry.vertices[i].vertex = qa_q12_index(reader, qa_binary_reader_i32(reader), vertex_count, "GL vertex");
    }
    if (count == capacity) {
      capacity = capacity ? capacity * 2 : 8;
      result = xrealloc(result, capacity * sizeof *result);
    }
    result[count++] = entry;
  }
  qa_q12_fail(reader, "GL command list lacks terminator");
}

qa_md2_command *qa_decode_md2_commands(const qa_q2_alias_model *model, size_t *out_count) {
  if (model->frame_count == 0) qa_range_error("MD2 has no frames");
  size_t length = model->gl_command_count * 4;
  uint8_t *data = xmalloc(length);
  for (size_t i = 0; i < model->gl_command_count; i++) {
    uint32_t word = (uint32_t)model->gl_commands[i];
    data[i * 4] = (uint8_t)word;
    data[i * 4 + 1] = (uint8_t)(word >> 8);
    data[i * 4 + 2] = (uint8_t)(word >> 16);
    data[i * 4 + 3] = (uint8_t)(word >> 24);
  }
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, data, length, "<md2 GL commands>")) qa_range_error("Out of memory");
  qa_md2_command *result = read_commands(&reader, (double)model->frames[0].frame.vertex_count, out_count);
  qa_binary_reader_destroy(&reader);
  free(data);
  return result;
}

void qa_md2_commands_destroy(qa_md2_command *commands, size_t count) {
  if (commands == NULL) return;
  for (size_t i = 0; i < count; i++) free(commands[i].vertices);
  free(commands);
}

static void md2_section(qa_binary_reader *reader, double offset, double count, double stride, double end,
                        qa_binary_reader *out) {
  double size = count * stride;
  if (size != trunc(size) || size > 9007199254740991.0 || offset < 68 || offset > end - size) {
    qa_binary_error(reader->source, (int64_t)reader->cursor, "MD2 section %g+%g exceeds header/model bounds",
                    offset, size);
  }
  if (!qa_binary_reader_section(reader, (int64_t)offset, (int64_t)size, out)) qa_range_error("Out of memory");
}

void qa_parse_md2(const uint8_t *data, size_t data_length, const char *source, qa_q2_alias_model *out) {
  memset(out, 0, sizeof *out);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)data, data_length, source != NULL ? source : "<md2>"))
    qa_range_error("Out of memory");
  qa_binary_reader_expect_magic(&reader, "IDP2");
  qa_q12_version(&reader, 8);
  out->skin_width = qa_q12_count(&reader, "skin width", 1);
  out->skin_height = qa_q12_count(&reader, "skin height", 1);
  double frame_size = qa_q12_count(&reader, "frame size", 1);
  double skin_count = qa_q12_count(&reader, "skins", 0);
  double vertex_count = qa_q12_count(&reader, "vertices", 1);
  double coordinate_count = qa_q12_count(&reader, "texture coordinates", 1);
  double triangle_count = qa_q12_count(&reader, "triangles", 1);
  double command_count = qa_q12_count(&reader, "GL command words", 0);
  double frame_count = qa_q12_count(&reader, "frames", 1);
  double skin_offset = qa_binary_reader_i32(&reader);
  double coordinate_offset = qa_binary_reader_i32(&reader);
  double triangle_offset = qa_binary_reader_i32(&reader);
  double frame_offset = qa_binary_reader_i32(&reader);
  double command_offset = qa_binary_reader_i32(&reader);
  double end = qa_binary_reader_i32(&reader);
  if (end < 68 || end > (double)reader.length) {
    qa_binary_error(reader.source, (int64_t)reader.cursor, "invalid model end %g", end);
  }
  if (frame_size < 40 + vertex_count * 4) qa_q12_fail(&reader, "frame size is smaller than its vertices");
  qa_binary_reader skin_reader, coordinate_reader, triangle_reader, frame_reader, command_reader;
  md2_section(&reader, skin_offset, skin_count, 64, end, &skin_reader);
  md2_section(&reader, coordinate_offset, coordinate_count, 4, end, &coordinate_reader);
  md2_section(&reader, triangle_offset, triangle_count, 12, end, &triangle_reader);
  md2_section(&reader, frame_offset, frame_count, frame_size, end, &frame_reader);
  md2_section(&reader, command_offset, command_count, 4, end, &command_reader);
  const char **skins = xmalloc((size_t)skin_count * sizeof *skins);
  for (size_t i = 0; i < (size_t)skin_count; i++) skins[i] = qa_binary_reader_fixed_byte_string(&skin_reader, 64);
  out->skins.values = skins;
  out->skins.count = (size_t)skin_count;
  qa_vec2 *coordinates = xmalloc((size_t)coordinate_count * sizeof *coordinates);
  for (size_t i = 0; i < (size_t)coordinate_count; i++) {
    coordinates[i].x = qa_binary_reader_i16(&coordinate_reader);
    coordinates[i].y = qa_binary_reader_i16(&coordinate_reader);
  }
  out->texture_coordinates = coordinates;
  out->texture_coordinate_count = (size_t)coordinate_count;
  qa_q2_alias_triangle *triangles = xmalloc((size_t)triangle_count * sizeof *triangles);
  for (size_t i = 0; i < (size_t)triangle_count; i++) {
    for (int j = 0; j < 3; j++)
      triangles[i].vertices[j] = qa_q12_index(&triangle_reader, qa_binary_reader_u16(&triangle_reader),
                                              vertex_count, "vertex");
    for (int j = 0; j < 3; j++)
      triangles[i].tex_coords[j] = qa_q12_index(&triangle_reader, qa_binary_reader_u16(&triangle_reader),
                                                coordinate_count, "texture coordinate");
  }
  out->triangles = triangles;
  out->triangle_count = (size_t)triangle_count;
  qa_q2_alias_frame *frames = xmalloc((size_t)frame_count * sizeof *frames);
  for (size_t i = 0; i < (size_t)frame_count; i++) {
    qa_binary_reader_seek(&frame_reader, (int64_t)((double)i * frame_size));
    memset(&frames[i], 0, sizeof frames[i]);
    frames[i].scale = qa_q12_vector(&frame_reader);
    frames[i].translation = qa_q12_vector(&frame_reader);
    frames[i].frame.name = qa_binary_reader_fixed_byte_string(&frame_reader, 16);
    size_t count = (size_t)vertex_count;
    qa_model_vertex *vertices = xmalloc(count * sizeof *vertices);
    qa_packed_alias_vertex *compressed = xmalloc(count * sizeof *compressed);
    qa_vec3 *points = xmalloc(count * sizeof *points);
    for (size_t v = 0; v < count; v++) {
      qa_packed_alias_vertex packed = qa_q12_packed_vertex(&frame_reader);
      qa_q12_index(&frame_reader, packed.normal_index, 162, "normal");
      compressed[v] = packed;
      vertices[v].position = qa_q12_packed_position(packed, frames[i].scale, frames[i].translation);
      vertices[v].normal = qa_decode_alias_normal(packed.normal_index);
      points[v] = vertices[v].position;
    }
    frames[i].frame.vertices = vertices;
    frames[i].frame.vertex_count = count;
    frames[i].compressed_vertices = compressed;
    frames[i].compressed_vertex_count = count;
    frames[i].frame.bounds = qa_q12_bounds_from_points(points, count);
    free(points);
  }
  out->frames = frames;
  out->frame_count = (size_t)frame_count;
  size_t validated = 0;
  qa_md2_command *validated_commands = read_commands(&command_reader, vertex_count, &validated);
  qa_md2_commands_destroy(validated_commands, validated);
  qa_binary_reader_seek(&command_reader, 0);
  int32_t *commands = xmalloc((size_t)command_count * sizeof *commands);
  for (size_t i = 0; i < (size_t)command_count; i++) commands[i] = qa_binary_reader_i32(&command_reader);
  out->gl_commands = commands;
  out->gl_command_count = (size_t)command_count;
  qa_bounds *all = xmalloc((size_t)frame_count * sizeof *all);
  for (size_t i = 0; i < (size_t)frame_count; i++) all[i] = frames[i].frame.bounds;
  out->bounds = qa_q12_union_bounds(all, (size_t)frame_count);
  free(all);
  qa_binary_reader_destroy(&skin_reader);
  qa_binary_reader_destroy(&coordinate_reader);
  qa_binary_reader_destroy(&triangle_reader);
  qa_binary_reader_destroy(&frame_reader);
  qa_binary_reader_destroy(&command_reader);
  qa_binary_reader_destroy(&reader);
}

void qa_q2_alias_model_destroy(qa_q2_alias_model *model) {
  if (model == NULL) return;
  for (size_t i = 0; i < model->skins.count; i++) free((void *)model->skins.values[i]);
  free((void *)model->skins.values);
  free((void *)model->texture_coordinates);
  free((void *)model->triangles);
  for (size_t i = 0; i < model->frame_count; i++) {
    free((void *)model->frames[i].frame.name);
    free((void *)model->frames[i].frame.vertices);
    free((void *)model->frames[i].compressed_vertices);
  }
  free((void *)model->frames);
  free((void *)model->gl_commands);
  memset(model, 0, sizeof *model);
}
