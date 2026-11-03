/* Port of src/formats/q3-model/md3.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_model/md3.h"
#include "qa/network/common/js_number.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/core/math.h"
#include "qa/core/renderer_math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js_case.h"

#define FR(value) ((double)(float)(value))
#define MD3_IDENT 0x33504449u
#define HEADER_SIZE 108
#define XYZ_SCALE (1.0 / 64.0)

/* Zeroed, so a caught BinaryError can release a partly parsed model. */
static void *xmalloc(size_t size) {
  void *p = calloc(size == 0 ? 1 : size, 1);
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

static char *c_string(qa_binary_reader *reader, int64_t length) {
  uint8_t *bytes = qa_binary_reader_bytes(reader, length);
  size_t count = length > 0 ? (size_t)length : 0;
  size_t end = 0;
  while (end < count && bytes[end] != 0) end++;
  char *result = xmalloc(end + 1);
  for (size_t i = 0; i < end; i++) result[i] = (char)bytes[i];
  result[end] = '\0';
  free(bytes);
  return result;
}

static qa_vec3 read_vec3(qa_binary_reader *reader) {
  qa_vec3 v;
  v.x = qa_binary_reader_finite_f32(reader);
  v.y = qa_binary_reader_finite_f32(reader);
  v.z = qa_binary_reader_finite_f32(reader);
  return v;
}

static double check_count(double value, double maximum, const char *source, int64_t offset,
                          const char *name) {
  if (value < 0 || value > maximum) {
    qa_binary_error(source, offset, "%s count %s outside 0..%s", name, QA_JS_NUMBER(value), QA_JS_NUMBER(maximum));
  }
  return value;
}

static double byte_length(double record_count, double stride, const char *source, int64_t offset,
                          const char *name) {
  double length = record_count * stride;
  if (length != trunc(length) || length < 0 || length > 9007199254740991.0) {
    qa_binary_error(source, offset, "%s byte length is not safe", name);
  }
  return length;
}

static void check_range(double offset, double length, double start, double end, const char *source,
                        int64_t field_offset, const char *name) {
  if (offset < start || length < 0 || offset > end - length) {
    qa_binary_error(source, field_offset, "%s range %s+%s exceeds %s..%s", name, QA_JS_NUMBER(offset), QA_JS_NUMBER(length), QA_JS_NUMBER(start), QA_JS_NUMBER(end));
  }
}

qa_vec3 qa_decode_md3_normal(double packed) {
  if (packed != trunc(packed) || packed < 0 || packed > 0xffff) {
    qa_range_error("packed MD3 normal %s outside uint16", QA_JS_NUMBER(packed));
  }
  unsigned value = (unsigned)packed;
  double latitude = ((value >> 8) & 0xff) * (1024 / 256);
  double longitude = (value & 0xff) * (1024 / 256);
  double x = FR((double)qa_renderer_sine(latitude + 1024 / 4) * (double)qa_renderer_sine(longitude));
  double y = FR((double)qa_renderer_sine(latitude) * (double)qa_renderer_sine(longitude));
  double z = (double)qa_renderer_sine(longitude + 1024 / 4);
  qa_vec3 normal = {x, y, z};
  return normal;
}

static char *surface_name(char *name) {
  size_t length = strlen(name);
  /* TS name.toLowerCase() over a byte (Latin-1) string. */
  for (size_t i = 0; i < length; i++) name[i] = (char)qa_js_latin1_lower((unsigned char)name[i]);
  if (length > 2 && name[length - 2] == '_') name[length - 2] = '\0';
  return name;
}

static double parse_surface(qa_binary_reader *reader, const char *source, double surface_offset,
                            double model_end, double model_frame_count, qa_md3_surface *out) {
  memset(out, 0, sizeof *out);
  check_range(surface_offset, HEADER_SIZE, HEADER_SIZE, model_end, source, (int64_t)surface_offset,
              "surface header");
  qa_binary_reader_seek(reader, (int64_t)surface_offset);
  if (qa_binary_reader_u32(reader) != MD3_IDENT)
    qa_binary_error(source, (int64_t)surface_offset, "expected IDP3 surface magic");
  out->name = surface_name(c_string(reader, 64));
  out->flags = qa_binary_reader_i32(reader);
  double frame_count = check_count(qa_binary_reader_i32(reader), 1024, source, (int64_t)surface_offset + 72,
                                   "surface frame");
  double shader_count = check_count(qa_binary_reader_i32(reader), 256, source, (int64_t)surface_offset + 76,
                                    "surface shader");
  double vertex_count = check_count(qa_binary_reader_i32(reader), 4096, source, (int64_t)surface_offset + 80,
                                    "surface vertex");
  double triangle_count = check_count(qa_binary_reader_i32(reader), 8192, source,
                                      (int64_t)surface_offset + 84, "surface triangle");
  double triangles_offset = qa_binary_reader_i32(reader);
  double shaders_offset = qa_binary_reader_i32(reader);
  double tex_coords_offset = qa_binary_reader_i32(reader);
  double vertices_offset = qa_binary_reader_i32(reader);
  double surface_length = qa_binary_reader_i32(reader);
  if (frame_count != model_frame_count) {
    qa_binary_error(source, (int64_t)surface_offset + 72, "surface has %s frames, model has %s", QA_JS_NUMBER(frame_count),
                    QA_JS_NUMBER(model_frame_count));
  }
  if (surface_length < HEADER_SIZE || surface_offset > model_end - surface_length) {
    qa_binary_error(source, (int64_t)surface_offset + 104, "surface end %s exceeds model end %s",
                    QA_JS_NUMBER(surface_length), QA_JS_NUMBER(model_end));
  }
  double shader_bytes = byte_length(shader_count, 68, source, (int64_t)surface_offset + 76, "shaders");
  double triangle_bytes = byte_length(triangle_count, 12, source, (int64_t)surface_offset + 84, "triangles");
  double tex_coord_bytes = byte_length(vertex_count, 8, source, (int64_t)surface_offset + 80,
                                       "texture coordinates");
  double vertex_records = byte_length(frame_count, vertex_count, source, (int64_t)surface_offset + 72,
                                      "vertex frames");
  double vertex_bytes = byte_length(vertex_records, 8, source, (int64_t)surface_offset + 80, "vertices");
  check_range(shaders_offset, shader_bytes, HEADER_SIZE, surface_length, source,
              (int64_t)surface_offset + 92, "shaders");
  check_range(triangles_offset, triangle_bytes, HEADER_SIZE, surface_length, source,
              (int64_t)surface_offset + 88, "triangles");
  check_range(tex_coords_offset, tex_coord_bytes, HEADER_SIZE, surface_length, source,
              (int64_t)surface_offset + 96, "texture coordinates");
  check_range(vertices_offset, vertex_bytes, HEADER_SIZE, surface_length, source,
              (int64_t)surface_offset + 100, "vertices");
  qa_binary_reader_seek(reader, (int64_t)(surface_offset + shaders_offset));
  out->shaders = xmalloc((size_t)shader_count * sizeof *out->shaders);
  out->shader_count = (size_t)shader_count;
  for (size_t i = 0; i < out->shader_count; i++) {
    out->shaders[i].name = c_string(reader, 64);
    out->shaders[i].index = qa_binary_reader_i32(reader);
  }
  qa_binary_reader_seek(reader, (int64_t)(surface_offset + triangles_offset));
  out->triangles = xmalloc((size_t)triangle_count * sizeof *out->triangles);
  out->triangle_count = (size_t)triangle_count;
  for (size_t i = 0; i < out->triangle_count; i++) {
    double triangle_offset = surface_offset + triangles_offset + (double)i * 12;
    for (int j = 0; j < 3; j++) {
      double vertex = qa_binary_reader_i32(reader);
      if (vertex < 0 || vertex >= vertex_count) {
        qa_binary_error(source, (int64_t)triangle_offset, "triangle vertex %s outside 0..%s", QA_JS_NUMBER(vertex),
                        QA_JS_NUMBER(vertex_count - 1));
      }
      out->triangles[i].indices[j] = vertex;
    }
  }
  qa_binary_reader_seek(reader, (int64_t)(surface_offset + tex_coords_offset));
  out->tex_coords = xmalloc((size_t)vertex_count * sizeof *out->tex_coords);
  out->tex_coord_count = (size_t)vertex_count;
  for (size_t i = 0; i < out->tex_coord_count; i++) {
    out->tex_coords[i].x = qa_binary_reader_finite_f32(reader);
    out->tex_coords[i].y = qa_binary_reader_finite_f32(reader);
  }
  qa_binary_reader_seek(reader, (int64_t)(surface_offset + vertices_offset));
  out->frame_vertices = xmalloc((size_t)frame_count * sizeof *out->frame_vertices);
  out->frame_vertex_counts = xmalloc((size_t)frame_count * sizeof *out->frame_vertex_counts);
  out->frame_count = (size_t)frame_count;
  for (size_t frame = 0; frame < out->frame_count; frame++) {
    qa_model_vertex *vertices = xmalloc((size_t)vertex_count * sizeof *vertices);
    for (size_t v = 0; v < (size_t)vertex_count; v++) {
      vertices[v].position.x = FR(qa_binary_reader_i16(reader) * XYZ_SCALE);
      vertices[v].position.y = FR(qa_binary_reader_i16(reader) * XYZ_SCALE);
      vertices[v].position.z = FR(qa_binary_reader_i16(reader) * XYZ_SCALE);
      vertices[v].normal = qa_decode_md3_normal(qa_binary_reader_u16(reader));
    }
    out->frame_vertices[frame] = vertices;
    out->frame_vertex_counts[frame] = (size_t)vertex_count;
  }
  return surface_offset + surface_length;
}

typedef struct md3_parse_state {
  qa_binary_reader reader;
  qa_decoded_md3_model *out;
} md3_parse_state;

static void md3_parse_release(void *raw) {
  md3_parse_state *state = raw;
  qa_binary_reader_destroy(&state->reader);
  qa_decoded_md3_model_destroy(state->out);
}

void qa_parse_md3(const uint8_t *data, size_t data_length, const char *source, qa_decoded_md3_model *out) {
  memset(out, 0, sizeof *out);
  const char *name = source != NULL ? source : "<md3>";
  md3_parse_state state;
  state.out = out;
  if (!qa_binary_reader_init(&state.reader, (uint8_t *)data, data_length, name)) qa_range_error("Out of memory");
  /* parseMd3 callers (loadMd3Lods, model loaders) catch BinaryError. */
  qa_cleanup node;
  qa_cleanup_push(&node, md3_parse_release, &state);
  qa_binary_reader reader = state.reader;
  if (qa_binary_reader_u32(&reader) != MD3_IDENT) qa_binary_error(name, 0, "expected IDP3 magic");
  if (qa_binary_reader_i32(&reader) != 15) qa_binary_error(name, 4, "expected MD3 version 15");
  qa_md3_model *model = &out->model;
  model->name = c_string(&reader, 64);
  model->flags = qa_binary_reader_i32(&reader);
  double frame_count = check_count(qa_binary_reader_i32(&reader), 1024, name, 76, "frame");
  double tag_count = check_count(qa_binary_reader_i32(&reader), 16, name, 80, "tag");
  double surface_count = check_count(qa_binary_reader_i32(&reader), 32, name, 84, "surface");
  double skin_count = qa_binary_reader_i32(&reader);
  double frames_offset = qa_binary_reader_i32(&reader);
  double tags_offset = qa_binary_reader_i32(&reader);
  double surfaces_offset = qa_binary_reader_i32(&reader);
  double model_end = qa_binary_reader_i32(&reader);
  if (frame_count < 1) qa_binary_error(name, 76, "MD3 has no frames");
  if (skin_count < 0) qa_binary_error(name, 88, "negative skin count %s", QA_JS_NUMBER(skin_count));
  model->skin_count = skin_count;
  if (model_end < HEADER_SIZE || model_end > (double)data_length) {
    qa_binary_error(name, 104, "model end %s outside %d..%zu", QA_JS_NUMBER(model_end), HEADER_SIZE, data_length);
  }
  double frame_bytes = byte_length(frame_count, 56, name, 76, "frames");
  double tag_records = byte_length(frame_count, tag_count, name, 80, "tags");
  double tag_bytes = byte_length(tag_records, 112, name, 80, "tags");
  check_range(frames_offset, frame_bytes, HEADER_SIZE, model_end, name, 92, "frames");
  check_range(tags_offset, tag_bytes, HEADER_SIZE, model_end, name, 96, "tags");
  check_range(surfaces_offset, 0, HEADER_SIZE, model_end, name, 100, "surfaces");
  qa_binary_reader_seek(&reader, (int64_t)frames_offset);
  model->frames = xmalloc((size_t)frame_count * sizeof *model->frames);
  model->frame_count = (size_t)frame_count;
  for (size_t i = 0; i < model->frame_count; i++) {
    model->frames[i].bounds.min = read_vec3(&reader);
    model->frames[i].bounds.max = read_vec3(&reader);
    model->frames[i].origin = read_vec3(&reader);
    model->frames[i].radius = qa_binary_reader_finite_f32(&reader);
    model->frames[i].name = c_string(&reader, 16);
  }
  qa_binary_reader_seek(&reader, (int64_t)tags_offset);
  model->tag_rows = xmalloc((size_t)frame_count * sizeof *model->tag_rows);
  model->tag_row_counts = xmalloc((size_t)frame_count * sizeof *model->tag_row_counts);
  model->tag_row_count = (size_t)frame_count;
  for (size_t frame = 0; frame < model->tag_row_count; frame++) {
    qa_md3_tag *tags = xmalloc((size_t)tag_count * sizeof *tags);
    model->tag_rows[frame] = tags;
    model->tag_row_counts[frame] = (size_t)tag_count;
    for (size_t i = 0; i < (size_t)tag_count; i++) {
      tags[i].name = c_string(&reader, 64);
      tags[i].origin = read_vec3(&reader);
      for (int j = 0; j < 3; j++) tags[i].axes.vectors[j] = read_vec3(&reader);
    }
    model->tag_rows[frame] = tags;
    model->tag_row_counts[frame] = (size_t)tag_count;
  }
  model->surfaces = xmalloc((size_t)surface_count * sizeof *model->surfaces);
  model->surface_count = (size_t)surface_count;
  double surface_offset = surfaces_offset;
  for (size_t i = 0; i < model->surface_count; i++) {
    surface_offset = parse_surface(&reader, name, surface_offset, model_end, frame_count,
                                   &model->surfaces[i]);
  }
  if (surface_offset != model_end) {
    qa_binary_error(name, (int64_t)surface_offset, "surface chain ends at %s, model ends at %s",
                    QA_JS_NUMBER(surface_offset), QA_JS_NUMBER(model_end));
  }
  out->source = xstrdup(name);
  out->byte_length = (size_t)model_end; /* TS no longer keeps a byte copy. */
  qa_cleanup_pop(&node);
  qa_binary_reader_destroy(&state.reader);
}

void qa_md3_model_destroy(qa_md3_model *model) {
  if (model == NULL) return;
  free(model->name);
  for (size_t i = 0; i < model->frame_count; i++) free(model->frames[i].name);
  free(model->frames);
  for (size_t i = 0; i < model->tag_row_count; i++) {
    for (size_t j = 0; j < model->tag_row_counts[i]; j++) free(model->tag_rows[i][j].name);
    free(model->tag_rows[i]);
  }
  free(model->tag_rows);
  free(model->tag_row_counts);
  for (size_t i = 0; i < model->surface_count; i++) {
    qa_md3_surface *surface = &model->surfaces[i];
    free(surface->name);
    for (size_t j = 0; j < surface->shader_count; j++) free(surface->shaders[j].name);
    free(surface->shaders);
    free(surface->triangles);
    free(surface->tex_coords);
    for (size_t j = 0; j < surface->frame_count; j++) free(surface->frame_vertices[j]);
    free(surface->frame_vertices);
    free(surface->frame_vertex_counts);
  }
  free(model->surfaces);
  memset(model, 0, sizeof *model);
}

void qa_decoded_md3_model_destroy(qa_decoded_md3_model *model) {
  if (model == NULL) return;
  qa_md3_model_destroy(&model->model);
  free(model->source);
  memset(model, 0, sizeof *model);
}

static const qa_model_vertex *frame_at(const qa_md3_surface *surface, double index, const char *name,
                                       size_t *out_count) {
  if (index != trunc(index) || index < 0 || index >= (double)surface->frame_count) {
    qa_range_error("%s MD3 frame %s outside 0..%s", name, QA_JS_NUMBER(index), QA_JS_NUMBER((double)surface->frame_count - 1));
  }
  *out_count = surface->frame_vertex_counts[(size_t)index];
  return surface->frame_vertices[(size_t)index];
}

const qa_model_vertex *qa_md3_interpolate_surface(const qa_md3_surface *surface, double frame,
                                                  double old_frame, double back_lerp, bool *out_owned,
                                                  size_t *out_count) {
  if (!isfinite(back_lerp)) qa_range_error("MD3 backlerp must be finite");
  size_t count = 0;
  const qa_model_vertex *current = frame_at(surface, frame, "current", &count);
  double old_normal_scale = FR(back_lerp);
  *out_count = count;
  if (old_normal_scale == 0 || frame == old_frame) {
    *out_owned = false;
    return current;
  }
  size_t previous_count = 0;
  const qa_model_vertex *previous = frame_at(surface, old_frame, "old", &previous_count);
  *out_owned = true;
  return qa_md3_interpolate_frames(current, previous, count, back_lerp);
}

qa_model_vertex *qa_md3_interpolate_frames(const qa_model_vertex *current, const qa_model_vertex *previous,
                                           size_t count, double back_lerp) {
  double old_normal_scale = FR(back_lerp);
  double new_normal_scale = FR(1 - old_normal_scale);
  double old_xyz_scale = FR(XYZ_SCALE * old_normal_scale);
  double new_xyz_scale = FR(XYZ_SCALE * new_normal_scale);
  qa_model_vertex *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    const qa_model_vertex *cur = &current[i];
    const qa_model_vertex *prev = &previous[i];
    double position[3], normal[3];
    const double old_pos[3] = {prev->position.x, prev->position.y, prev->position.z};
    const double new_pos[3] = {cur->position.x, cur->position.y, cur->position.z};
    const double old_nor[3] = {prev->normal.x, prev->normal.y, prev->normal.z};
    const double new_nor[3] = {cur->normal.x, cur->normal.y, cur->normal.z};
    for (int j = 0; j < 3; j++) {
      double old_xyz = FR(old_pos[j] * 64);
      double new_xyz = FR(new_pos[j] * 64);
      position[j] = FR(FR(old_xyz * old_xyz_scale) + FR(new_xyz * new_xyz_scale));
      normal[j] = FR(FR(old_nor[j] * old_normal_scale) + FR(new_nor[j] * new_normal_scale));
    }
    result[i].position.x = position[0];
    result[i].position.y = position[1];
    result[i].position.z = position[2];
    qa_vec3 blended = {normal[0], normal[1], normal[2]};
    result[i].normal = qa_normalize_fast3(blended);
  }
  return result;
}

static const qa_md3_tag *tag_at(const qa_md3_model *model, double frame, const char *name) {
  if (frame != trunc(frame) || frame < 0) qa_range_error("MD3 tag frame %s must be non-negative", QA_JS_NUMBER(frame));
  if (model->tag_row_count == 0) return NULL;
  double clamped = frame;
  if (clamped > (double)model->frame_count - 1) clamped = (double)model->frame_count - 1;
  if (clamped < 0 || clamped >= (double)model->tag_row_count) return NULL;
  size_t row = (size_t)clamped;
  for (size_t i = 0; i < model->tag_row_counts[row]; i++) {
    if (strcmp(model->tag_rows[row][i].name, name) == 0) return &model->tag_rows[row][i];
  }
  return NULL;
}

bool qa_md3_lerp_tag(const qa_md3_model *model, const char *name, double start_frame, double end_frame,
                     double fraction, qa_md3_tag *out) {
  const qa_md3_tag *start = tag_at(model, start_frame, name);
  const qa_md3_tag *end = tag_at(model, end_frame, name);
  if (start == NULL || end == NULL) return false;
  qa_md3_interpolate_tags(start, end, name, fraction, out);
  return true;
}

void qa_md3_interpolate_tags(const qa_md3_tag *start, const qa_md3_tag *end, const char *name,
                             double fraction, qa_md3_tag *out) {
  if (!isfinite(fraction)) qa_range_error("MD3 tag fraction must be finite");
  double front_lerp = FR(fraction);
  double back_lerp = FR(1 - front_lerp);
  out->name = xstrdup(name);
  qa_vec3 from[4] = {start->origin, start->axes.vectors[0], start->axes.vectors[1], start->axes.vectors[2]};
  qa_vec3 to[4] = {end->origin, end->axes.vectors[0], end->axes.vectors[1], end->axes.vectors[2]};
  qa_vec3 mixed[4];
  for (int i = 0; i < 4; i++) {
    mixed[i].x = FR(FR(from[i].x * back_lerp) + FR(to[i].x * front_lerp));
    mixed[i].y = FR(FR(from[i].y * back_lerp) + FR(to[i].y * front_lerp));
    mixed[i].z = FR(FR(from[i].z * back_lerp) + FR(to[i].z * front_lerp));
  }
  out->origin = mixed[0];
  for (int i = 0; i < 3; i++) out->axes.vectors[i] = qa_normalize3(mixed[i + 1]);
}

/* SkinTokenizer (CommaParse) port. */
static void skin_skip_comma(qa_md3_skin_iterator *iterator) {
  if (iterator->offset < iterator->length && iterator->text[iterator->offset] == ',') iterator->offset++;
}

static char *skin_next_token(qa_md3_skin_iterator *iterator) {
  for (;;) {
    while (iterator->offset < iterator->length) {
      unsigned char character = (unsigned char)iterator->text[iterator->offset];
      if (character > 32 && character < 128) break;
      iterator->offset++;
    }
    if (iterator->offset + 1 < iterator->length && iterator->text[iterator->offset] == '/' &&
        iterator->text[iterator->offset + 1] == '/') {
      while (iterator->offset < iterator->length && iterator->text[iterator->offset] != '\n')
        iterator->offset++;
      continue;
    }
    if (iterator->offset + 1 < iterator->length && iterator->text[iterator->offset] == '/' &&
        iterator->text[iterator->offset + 1] == '*') {
      iterator->offset += 2;
      /* An unterminated comment runs to the end of the text (TS startsWith). */
      while (iterator->offset < iterator->length &&
             !(iterator->offset + 1 < iterator->length && iterator->text[iterator->offset] == '*' &&
               iterator->text[iterator->offset + 1] == '/'))
        iterator->offset++;
      if (iterator->offset < iterator->length) iterator->offset += 2;
      continue;
    }
    break;
  }
  if (iterator->offset >= iterator->length) return NULL;
  char first = iterator->text[iterator->offset];
  char *result = xmalloc(1);
  size_t length = 0, capacity = 1;
  result[0] = '\0';
  if (first == '"') {
    iterator->offset++;
    while (iterator->offset < iterator->length) {
      char character = iterator->text[iterator->offset];
      if (character == '"') break;
      if (length + 1 >= capacity) { capacity *= 2; result = xrealloc(result, capacity); }
      result[length++] = character;
      result[length] = '\0';
      iterator->offset++;
    }
    if (iterator->offset < iterator->length && iterator->text[iterator->offset] == '"') iterator->offset++;
    if (length >= 1024) qa_range_error("CommaParse quoted token exceeds its source allocation");
    return result;
  }
  if (length + 1 >= capacity) { capacity *= 2; result = xrealloc(result, capacity); }
  result[length++] = first;
  result[length] = '\0';
  iterator->offset++;
  while (iterator->offset < iterator->length) {
    char character = iterator->text[iterator->offset];
    unsigned char code = (unsigned char)character;
    if (character == ',' || code <= 32 || code >= 128) break;
    if (length < 1024) {
      if (length + 1 >= capacity) { capacity *= 2; result = xrealloc(result, capacity); }
      result[length++] = character;
      result[length] = '\0';
    }
    iterator->offset++;
  }
  if (length == 1024) result[0] = '\0';
  return result;
}

void qa_md3_skin_iterator_init(qa_md3_skin_iterator *iterator, const char *text) {
  iterator->text = text;
  iterator->length = strlen(text);
  iterator->offset = 0;
  /* The TS rejects UTF-16 code units above 255; C byte text cannot. */
}

bool qa_md3_skin_iterator_next(qa_md3_skin_iterator *iterator, qa_skin_surface *out) {
  for (;;) {
    char *name = skin_next_token(iterator);
    if (name == NULL || name[0] == '\0') {
      free(name);
      return false;
    }
    skin_skip_comma(iterator);
    if (strstr(name, "tag_") != NULL) {
      free(name);
      continue;
    }
    char *shader = skin_next_token(iterator);
    if (shader == NULL) shader = xstrdup("");
    if (strlen(name) > 63) name[63] = '\0';
    for (char *p = name; *p != '\0'; p++) *p = (char)tolower((unsigned char)*p);
    out->name = name;
    out->shader = shader;
    return true;
  }
}

qa_skin_surface *qa_parse_skin(const char *text, size_t *out_count) {
  qa_md3_skin_iterator iterator;
  qa_md3_skin_iterator_init(&iterator, text);
  qa_skin_surface *result = NULL;
  size_t count = 0, capacity = 0;
  qa_skin_surface entry;
  while (qa_md3_skin_iterator_next(&iterator, &entry)) {
    if (count == capacity) {
      capacity = capacity ? capacity * 2 : 8;
      result = xrealloc(result, capacity * sizeof *result);
    }
    result[count++] = entry;
  }
  *out_count = count;
  return result;
}

void qa_skin_surfaces_destroy(qa_skin_surface *surfaces, size_t count) {
  if (surfaces == NULL) return;
  for (size_t i = 0; i < count; i++) {
    free(surfaces[i].name);
    free(surfaces[i].shader);
  }
  free(surfaces);
}
