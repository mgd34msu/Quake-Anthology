/* Port of src/formats/q12-model/mdl.ts. GPL-2.0-or-later. */
#include "qa/formats/q12_model/mdl.h"

#include <stdlib.h>
#include <string.h>

#include "qa/formats/q12_model/animation.h"
#include "qa/formats/q12_model/common.h"
#include "qa/formats/q12_model/normals.h"
#include "qa/foundation/support.h"

/* Zeroed, so a caught BinaryError can release a partly read model. */
static void *xmalloc(size_t size) {
  void *p = calloc(size == 0 ? 1 : size, 1);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static void read_frame(qa_binary_reader *reader, double vertex_count, qa_vec3 scale, qa_vec3 translate,
                       qa_q1_alias_frame *out) {
  qa_binary_reader_check_number(reader, (double)reader->cursor, 24 + vertex_count * 4);
  out->compressed_bounds[0] = qa_q12_packed_vertex(reader);
  out->compressed_bounds[1] = qa_q12_packed_vertex(reader);
  memset(&out->frame, 0, sizeof out->frame);
  out->frame.name = qa_binary_reader_fixed_byte_string(reader, 16);
  size_t count = (size_t)vertex_count;
  qa_packed_alias_vertex *compressed = xmalloc(count * sizeof *compressed);
  out->compressed_vertices = compressed;
  out->compressed_vertex_count = count;
  qa_model_vertex *vertices = xmalloc(count * sizeof *vertices);
  out->frame.vertices = vertices;
  out->frame.vertex_count = count;
  for (size_t i = 0; i < count; i++) {
    qa_packed_alias_vertex packed = qa_q12_packed_vertex(reader);
    qa_q12_index(reader, packed.normal_index, 162, "normal");
    compressed[i] = packed;
    vertices[i].position = qa_q12_packed_position(packed, scale, translate);
    vertices[i].normal = qa_decode_alias_normal(packed.normal_index);
  }
  out->frame.vertices = vertices;
  out->frame.vertex_count = count;
  qa_vec3 points[2] = {qa_q12_packed_position(out->compressed_bounds[0], scale, translate),
                       qa_q12_packed_position(out->compressed_bounds[1], scale, translate)};
  out->frame.bounds = qa_q12_bounds_from_points(points, 2);
  out->frame.origin.x = 0;
  out->frame.origin.y = 0;
  out->frame.origin.z = 0;
}

static void read_frame_set(qa_binary_reader *reader, double vertex_count, qa_vec3 scale, qa_vec3 translate,
                           qa_q1_alias_frame_set *out) {
  memset(out, 0, sizeof *out);
  if (!qa_q12_group_type(reader)) {
    qa_q1_alias_frame *frame = xmalloc(sizeof *frame);
    out->is_group = false;
    out->single_frame = frame;
    read_frame(reader, vertex_count, scale, translate, frame);
    return;
  }
  double frame_count = qa_q12_count(reader, "group frames", 1);
  out->compressed_bounds[0] = qa_q12_packed_vertex(reader);
  out->compressed_bounds[1] = qa_q12_packed_vertex(reader);
  size_t endpoint_count = 0;
  double *endpoints = qa_q12_intervals(reader, frame_count, &endpoint_count);
  qa_binary_reader_check_number(reader, (double)reader->cursor, frame_count * (24 + vertex_count * 4));
  qa_vec3 points[2] = {qa_q12_packed_position(out->compressed_bounds[0], scale, translate),
                       qa_q12_packed_position(out->compressed_bounds[1], scale, translate)};
  out->bounds = qa_q12_bounds_from_points(points, 2);
  qa_timed_frame_entry *entries = xmalloc(endpoint_count * sizeof *entries);
  out->is_group = true;
  out->frames = entries;
  out->frame_count = endpoint_count;
  qa_cleanup endpoints_node;
  qa_cleanup_push(&endpoints_node, free, endpoints);
  for (size_t i = 0; i < endpoint_count; i++) {
    qa_q1_alias_frame *frame = xmalloc(sizeof *frame);
    entries[i].interval_seconds = endpoints[i];
    entries[i].frame = frame;
    read_frame(reader, vertex_count, scale, translate, frame);
  }
  qa_cleanup_pop(&endpoints_node);
  free(endpoints);
}

typedef struct {
  qa_binary_reader *reader;
  int64_t length;
} skin_ctx;

static void *read_skin(void *ctx) {
  skin_ctx *skin = ctx;
  uint8_t *bytes = qa_binary_reader_bytes(skin->reader, skin->length);
  qa_byte_view *view = xmalloc(sizeof *view);
  view->bytes = bytes;
  view->length = (size_t)skin->length;
  return view;
}

typedef struct mdl_parse_state {
  qa_binary_reader reader;
  qa_q1_alias_model *out;
} mdl_parse_state;

static void mdl_parse_release(void *raw) {
  mdl_parse_state *state = raw;
  qa_binary_reader_destroy(&state->reader);
  qa_q1_alias_model_destroy(state->out);
}

void qa_parse_mdl(const uint8_t *data, size_t data_length, const char *source, qa_q1_alias_model *out) {
  memset(out, 0, sizeof *out);
  mdl_parse_state state;
  state.out = out;
  if (!qa_binary_reader_init(&state.reader, (uint8_t *)data, data_length, source != NULL ? source : "<mdl>"))
    qa_range_error("Out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, mdl_parse_release, &state);
#define reader state.reader
  qa_binary_reader_expect_magic(&reader, "IDPO");
  qa_q12_version(&reader, 6);
  out->scale = qa_q12_vector(&reader);
  out->scale_origin = qa_q12_vector(&reader);
  out->bounding_radius = qa_binary_reader_finite_f32(&reader);
  out->eye_position = qa_q12_vector(&reader);
  double skin_count = qa_q12_count(&reader, "skins", 1);
  out->skin_width = qa_q12_count(&reader, "skin width", 1);
  out->skin_height = qa_q12_count(&reader, "skin height", 1);
  double vertex_count = qa_q12_count(&reader, "vertices", 1);
  double triangle_count = qa_q12_count(&reader, "triangles", 1);
  double frame_count = qa_q12_count(&reader, "frames", 1);
  out->sync = qa_q12_sync_type(&reader);
  out->flags = qa_binary_reader_i32(&reader);
  out->size = qa_binary_reader_finite_f32(&reader);
  if (out->bounding_radius < 0) qa_q12_fail(&reader, "negative model radius");
  qa_binary_reader_check_number(&reader, (double)reader.cursor, skin_count * (4 + out->skin_width * out->skin_height));
  qa_timed_frames *skins = xmalloc((size_t)skin_count * sizeof *skins);
  out->skins = skins;
  out->skin_count = (size_t)skin_count;
  skin_ctx ctx = {&reader, (int64_t)(out->skin_width * out->skin_height)};
  for (size_t i = 0; i < (size_t)skin_count; i++) qa_q12_read_timed(&reader, read_skin, &ctx, &skins[i]);
  qa_binary_reader_check_number(&reader, (double)reader.cursor, vertex_count * 12 + triangle_count * 16);
  qa_alias_tex_coord *coordinates = xmalloc((size_t)vertex_count * sizeof *coordinates);
  out->texture_coordinates = coordinates;
  for (size_t i = 0; i < (size_t)vertex_count; i++) {
    coordinates[i].on_seam = qa_binary_reader_i32(&reader) != 0;
    coordinates[i].s = qa_binary_reader_i32(&reader);
    coordinates[i].t = qa_binary_reader_i32(&reader);
  }
  out->texture_coordinates = coordinates;
  out->texture_coordinate_count = (size_t)vertex_count;
  qa_q1_alias_triangle *triangles = xmalloc((size_t)triangle_count * sizeof *triangles);
  out->triangles = triangles;
  for (size_t i = 0; i < (size_t)triangle_count; i++) {
    triangles[i].front = qa_binary_reader_i32(&reader) != 0;
    for (int j = 0; j < 3; j++)
      triangles[i].vertices[j] = qa_q12_index(&reader, qa_binary_reader_i32(&reader), vertex_count, "vertex");
  }
  out->triangles = triangles;
  out->triangle_count = (size_t)triangle_count;
  qa_binary_reader_check_number(&reader, (double)reader.cursor, frame_count * (4 + 24 + vertex_count * 4));
  qa_q1_alias_frame_set *frames = xmalloc((size_t)frame_count * sizeof *frames);
  out->frames = frames;
  out->frame_count = (size_t)frame_count;
  for (size_t i = 0; i < (size_t)frame_count; i++)
    read_frame_set(&reader, vertex_count, out->scale, out->scale_origin, &frames[i]);
  qa_bounds *all = xmalloc((size_t)frame_count * sizeof *all);
  for (size_t i = 0; i < (size_t)frame_count; i++)
    all[i] = frames[i].is_group ? frames[i].bounds : frames[i].single_frame->frame.bounds;
  out->bounds = qa_q12_union_bounds(all, (size_t)frame_count);
  free(all);
#undef reader
  qa_cleanup_pop(&node);
  qa_binary_reader_destroy(&state.reader);
}

static void destroy_frame(qa_q1_alias_frame *frame) {
  if (frame == NULL) return;
  /* The interpolation PoseLru keys by frame address (TS WeakMap identity): forget it before the address is freed. */
  qa_alias_interpolation_forget(&frame->frame);
  free((void *)frame->frame.name);
  free((void *)frame->frame.vertices);
  free((void *)frame->compressed_vertices);
  free(frame);
}

static void destroy_timed_bytes(qa_timed_frames *frames) {
  if (!frames->is_group) {
    qa_byte_view *view = (qa_byte_view *)frames->single_frame;
    if (view != NULL) { free((void *)view->bytes); free(view); }
    return;
  }
  for (size_t i = 0; i < frames->frame_count; i++) {
    qa_byte_view *view = (qa_byte_view *)frames->frames[i].frame;
    if (view != NULL) { free((void *)view->bytes); free(view); }
  }
  free((void *)frames->frames);
}

void qa_q1_alias_model_destroy(qa_q1_alias_model *model) {
  if (model == NULL) return;
  for (size_t i = 0; i < model->skin_count; i++) destroy_timed_bytes((qa_timed_frames *)&model->skins[i]);
  free((void *)model->skins);
  free((void *)model->texture_coordinates);
  free((void *)model->triangles);
  for (size_t i = 0; i < model->frame_count; i++) {
    qa_q1_alias_frame_set *set = (qa_q1_alias_frame_set *)&model->frames[i];
    if (!set->is_group) {
      destroy_frame((qa_q1_alias_frame *)set->single_frame);
    } else {
      for (size_t j = 0; j < set->frame_count; j++)
        destroy_frame((qa_q1_alias_frame *)set->frames[j].frame);
      free((void *)set->frames);
    }
  }
  free((void *)model->frames);
  memset(model, 0, sizeof *model);
}
