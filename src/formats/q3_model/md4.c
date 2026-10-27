/* Port of src/formats/q3-model/md4.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_model/md4.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/formats/q3_model/text.h"
#include "qa/foundation/support.h"

#define FR(value) ((double)(float)(value))

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *copy = xmalloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}

static void check_range(qa_binary_reader *reader, double offset, double length, double start, double end,
                        int64_t field) {
  if (offset != trunc(offset) || length != trunc(length) || offset > 9007199254740991.0 ||
      length > 9007199254740991.0 || offset < start || length < 0 || offset > end - length) {
    qa_binary_error(reader->source, field, "MD4 range %g+%g exceeds %g..%g", offset, length, start, end);
  }
}

static double read_count(qa_binary_reader *reader, double maximum) {
  size_t offset = reader->cursor;
  double value = qa_binary_reader_i32(reader);
  if (value < 0 || value > maximum) {
    qa_binary_error(reader->source, (int64_t)offset, "MD4 count %g outside 0..%g", value, maximum);
  }
  return value;
}

static double read_index(qa_binary_reader *reader, double maximum, const char *label) {
  size_t offset = reader->cursor;
  double value = qa_binary_reader_i32(reader);
  if (value < 0 || value >= maximum) {
    qa_binary_error(reader->source, (int64_t)offset, "%s %g outside 0..%g", label, value, maximum - 1);
  }
  return value;
}

static char *c_string(qa_binary_reader *reader) {
  uint8_t *bytes = qa_binary_reader_bytes(reader, 64);
  size_t end = 0;
  while (end < 64 && bytes[end] != 0) end++;
  char *result = xmalloc(end + 1);
  for (size_t i = 0; i < end; i++) result[i] = (char)bytes[i];
  result[end] = '\0';
  free(bytes);
  return result;
}

static qa_vec3 read_vec3(qa_binary_reader *reader) {
  qa_vec3 v = {qa_binary_reader_finite_f32(reader), qa_binary_reader_finite_f32(reader),
               qa_binary_reader_finite_f32(reader)};
  return v;
}

static qa_vec4 read_row(qa_binary_reader *reader) {
  qa_vec4 v = {qa_binary_reader_finite_f32(reader), qa_binary_reader_finite_f32(reader),
               qa_binary_reader_finite_f32(reader), qa_binary_reader_finite_f32(reader)};
  return v;
}

static double parse_surface(qa_binary_reader *reader, double source_offset, double lod_end, double num_bones,
                            qa_md4_surface *out) {
  memset(out, 0, sizeof *out);
  check_range(reader, source_offset, 168, 100, lod_end, (int64_t)source_offset);
  qa_binary_reader_seek(reader, (int64_t)source_offset);
  (void)qa_binary_reader_i32(reader);
  char *name = c_string(reader);
  for (char *p = name; *p != '\0'; p++) *p = (char)tolower((unsigned char)*p);
  out->name = name;
  out->shader = c_string(reader);
  (void)qa_binary_reader_i32(reader);
  double ofs_header = qa_binary_reader_i32(reader);
  if (source_offset + ofs_header != 0) {
    qa_binary_error(reader->source, (int64_t)source_offset + 136,
                    "MD4 surface does not point back to its model header");
  }
  double num_verts = read_count(reader, 1000);
  double ofs_verts = qa_binary_reader_i32(reader);
  double num_triangles = read_count(reader, 2000);
  double ofs_triangles = qa_binary_reader_i32(reader);
  double num_bone_references = read_count(reader, 2147483647.0);
  double ofs_bone_references = qa_binary_reader_i32(reader);
  double ofs_end = qa_binary_reader_i32(reader);
  check_range(reader, source_offset, ofs_end, source_offset, lod_end, (int64_t)source_offset + 164);
  if (ofs_end < 168) qa_binary_error(reader->source, (int64_t)source_offset + 164,
                                     "MD4 surface end precedes its header");
  double end = source_offset + ofs_end;
  if (num_verts > 0) check_range(reader, ofs_verts, num_verts * 24, 168, ofs_end, (int64_t)source_offset + 144);
  if (num_triangles > 0)
    check_range(reader, ofs_triangles, num_triangles * 12, 168, ofs_end, (int64_t)source_offset + 152);
  if (num_bone_references > 0)
    check_range(reader, ofs_bone_references, num_bone_references * 4, 168, ofs_end,
                (int64_t)source_offset + 160);
  qa_md4_triangle *triangles = xmalloc((size_t)num_triangles * sizeof *triangles);
  if (num_triangles > 0) qa_binary_reader_seek(reader, (int64_t)(source_offset + ofs_triangles));
  for (size_t i = 0; i < (size_t)num_triangles; i++) {
    for (int j = 0; j < 3; j++) triangles[i].indices[j] = read_index(reader, num_verts, "triangle vertex");
  }
  out->triangles = triangles;
  out->triangle_count = (size_t)num_triangles;
  double *bone_references = xmalloc((size_t)num_bone_references * sizeof *bone_references);
  if (num_bone_references > 0) qa_binary_reader_seek(reader, (int64_t)(source_offset + ofs_bone_references));
  for (size_t i = 0; i < (size_t)num_bone_references; i++)
    bone_references[i] = read_index(reader, num_bones, "bone reference");
  out->bone_references.values = bone_references;
  out->bone_references.count = (size_t)num_bone_references;
  qa_md4_vertex *vertices = xmalloc((size_t)num_verts * sizeof *vertices);
  if (num_verts > 0) qa_binary_reader_seek(reader, (int64_t)(source_offset + ofs_verts));
  for (size_t i = 0; i < (size_t)num_verts; i++) {
    check_range(reader, (double)reader->cursor, 24, source_offset + 168, end, (int64_t)reader->cursor);
    vertices[i].normal = read_vec3(reader);
    vertices[i].tex_coords.x = qa_binary_reader_finite_f32(reader);
    vertices[i].tex_coords.y = qa_binary_reader_finite_f32(reader);
    double num_weights = read_count(reader, 2147483647.0);
    check_range(reader, (double)reader->cursor, num_weights * 20, source_offset + 168, end,
                (int64_t)reader->cursor - 4);
    qa_md4_weight *weights = xmalloc((size_t)num_weights * sizeof *weights);
    for (size_t w = 0; w < (size_t)num_weights; w++) {
      weights[w].bone_index = read_index(reader, num_bones, "weight bone");
      weights[w].bone_weight = qa_binary_reader_finite_f32(reader);
      weights[w].offset = read_vec3(reader);
    }
    vertices[i].weights = weights;
    vertices[i].weight_count = (size_t)num_weights;
  }
  out->vertices = vertices;
  out->vertex_count = (size_t)num_verts;
  return end;
}

void qa_parse_md4(const uint8_t *bytes, size_t length, const char *source, qa_decoded_md4_model *out) {
  memset(out, 0, sizeof *out);
  const char *name = source != NULL ? source : "<md4>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) qa_range_error("Out of memory");
  check_range(&reader, 0, 100, 0, (double)reader.length, 0);
  if (qa_binary_reader_u32(&reader) != 0x34504449u) qa_binary_error(name, 0, "expected IDP4 model magic");
  double version = qa_binary_reader_i32(&reader);
  if (version != 1) qa_binary_error(name, 4, "unsupported MD4 version %g", version);
  qa_md4_model *model = &out->model;
  model->version = version;
  model->name = c_string(&reader);
  double num_frames = read_count(&reader, 2147483647.0);
  if (num_frames == 0) qa_binary_error(name, 72, "MD4 model has no frames");
  double num_bones = read_count(&reader, 128);
  model->num_bones = num_bones;
  double ofs_bone_names = qa_binary_reader_i32(&reader);
  double ofs_frames = qa_binary_reader_i32(&reader);
  double num_lods = read_count(&reader, 2147483647.0);
  double ofs_lods = qa_binary_reader_i32(&reader);
  double ofs_end = qa_binary_reader_i32(&reader);
  check_range(&reader, 0, ofs_end, 0, (double)reader.length, 96);
  if (ofs_end < 100) qa_binary_error(name, 96, "MD4 model end precedes its header");
  model->byte_length = ofs_end;
  double frame_size = 40 + num_bones * 48;
  check_range(&reader, ofs_frames, num_frames * frame_size, 100, ofs_end, 84);
  if (num_lods > 0) check_range(&reader, ofs_lods, num_lods * 12, 100, ofs_end, 92);
  qa_binary_reader_seek(&reader, (int64_t)ofs_frames);
  qa_md4_frame *frames = xmalloc((size_t)num_frames * sizeof *frames);
  for (size_t i = 0; i < (size_t)num_frames; i++) {
    frames[i].bounds.min = read_vec3(&reader);
    frames[i].bounds.max = read_vec3(&reader);
    frames[i].local_origin = read_vec3(&reader);
    frames[i].radius = qa_binary_reader_finite_f32(&reader);
    qa_md4_bone *bones = xmalloc((size_t)num_bones * sizeof *bones);
    for (size_t b = 0; b < (size_t)num_bones; b++) {
      for (int r = 0; r < 3; r++) bones[b].matrix[r] = read_row(&reader);
    }
    frames[i].bones = bones;
    frames[i].bone_count = (size_t)num_bones;
  }
  model->frames = frames;
  model->frame_count = (size_t)num_frames;
  qa_md4_lod *lods = xmalloc((size_t)num_lods * sizeof *lods);
  double lod_offset = ofs_lods;
  for (size_t lod = 0; lod < (size_t)num_lods; lod++) {
    check_range(&reader, lod_offset, 12, 100, ofs_end, (int64_t)lod_offset);
    qa_binary_reader_seek(&reader, (int64_t)lod_offset);
    double num_surfaces = read_count(&reader, 2147483647.0);
    double ofs_surfaces = qa_binary_reader_i32(&reader);
    double lod_length = qa_binary_reader_i32(&reader);
    check_range(&reader, lod_offset, lod_length, lod_offset, ofs_end, (int64_t)lod_offset + 8);
    if (lod_length < 12) qa_binary_error(name, (int64_t)lod_offset + 8, "MD4 LOD end precedes its header");
    if (num_surfaces > 0)
      check_range(&reader, ofs_surfaces, num_surfaces * 168, 12, lod_length, (int64_t)lod_offset + 4);
    qa_md4_surface *surfaces = xmalloc((size_t)num_surfaces * sizeof *surfaces);
    double surface_offset = lod_offset + ofs_surfaces;
    for (size_t s = 0; s < (size_t)num_surfaces; s++) {
      surface_offset = parse_surface(&reader, surface_offset, lod_offset + lod_length, num_bones,
                                     &surfaces[s]);
    }
    lods[lod].surfaces = surfaces;
    lods[lod].surface_count = (size_t)num_surfaces;
    lod_offset += lod_length;
  }
  model->lods = lods;
  model->lod_count = (size_t)num_lods;
  if (ofs_bone_names >= 100 && ofs_bone_names <= ofs_end - num_bones * 64) {
    qa_binary_reader_seek(&reader, (int64_t)ofs_bone_names);
    out->bone_names = xmalloc((size_t)num_bones * sizeof *out->bone_names);
    out->bone_name_count = (size_t)num_bones;
    for (size_t i = 0; i < out->bone_name_count; i++) out->bone_names[i] = c_string(&reader);
  }
  out->source = xstrdup(name);
  out->bytes_length = (size_t)ofs_end;
  out->bytes = xmalloc(out->bytes_length);
  memcpy(out->bytes, bytes, out->bytes_length);
  qa_binary_reader_destroy(&reader);
}

void qa_decoded_md4_model_destroy(qa_decoded_md4_model *model) {
  if (model == NULL) return;
  qa_md4_model *inner = &model->model;
  free((void *)inner->name);
  for (size_t i = 0; i < inner->frame_count; i++) free((void *)inner->frames[i].bones);
  free((void *)inner->frames);
  for (size_t i = 0; i < inner->lod_count; i++) {
    for (size_t s = 0; s < inner->lods[i].surface_count; s++) {
      const qa_md4_surface *surface = &inner->lods[i].surfaces[s];
      free((void *)surface->name);
      free((void *)surface->shader);
      for (size_t v = 0; v < surface->vertex_count; v++) free((void *)surface->vertices[v].weights);
      free((void *)surface->vertices);
      free((void *)surface->triangles);
      free((void *)surface->bone_references.values);
    }
    free((void *)inner->lods[i].surfaces);
  }
  free((void *)inner->lods);
  free(model->source);
  free(model->bytes);
  for (size_t i = 0; i < model->bone_name_count; i++) free(model->bone_names[i]);
  free(model->bone_names);
  memset(model, 0, sizeof *model);
}

qa_model_vertex *qa_skin_md4_surface(const qa_md4_model *model, const qa_md4_surface *surface, double frame,
                                     double previous_frame, double back_lerp, size_t *out_count) {
  if (frame != trunc(frame) || frame < 0 || previous_frame != trunc(previous_frame) || previous_frame < 0 ||
      !isfinite(back_lerp)) {
    qa_range_error("Invalid MD4 frame selection");
  }
  const qa_md4_frame *current = &model->frames[qa_q3_model_at(frame, model->frame_count, "MD4 frame")];
  double back = frame == previous_frame ? 0 : FR(back_lerp);
  const qa_md4_frame *old = back == 0 ? current
                                      : &model->frames[qa_q3_model_at(previous_frame, model->frame_count,
                                                                     "MD4 previous frame")];
  double front = FR(1 - back);
  size_t bone_count = current->bone_count;
  qa_md4_bone *bones = xmalloc(bone_count * sizeof *bones);
  for (size_t i = 0; i < bone_count; i++) {
    const qa_md4_bone *before = &old->bones[qa_q3_model_at((double)i, old->bone_count, "MD4 bone")];
    for (int r = 0; r < 3; r++) {
      qa_vec4 a = current->bones[i].matrix[r], b = before->matrix[r];
      qa_vec4 *row = &bones[i].matrix[r];
      if (back == 0) {
        *row = a;
      } else {
        row->x = FR(FR(front * a.x) + FR(back * b.x));
        row->y = FR(FR(front * a.y) + FR(back * b.y));
        row->z = FR(FR(front * a.z) + FR(back * b.z));
        row->w = FR(FR(front * a.w) + FR(back * b.w));
      }
    }
  }
  qa_model_vertex *result = xmalloc(surface->vertex_count * sizeof *result);
  for (size_t v = 0; v < surface->vertex_count; v++) {
    const qa_md4_vertex *vertex = &surface->vertices[v];
    double x = 0, y = 0, z = 0, nx = 0, ny = 0, nz = 0;
    for (size_t w = 0; w < vertex->weight_count; w++) {
      const qa_md4_weight *weight = &vertex->weights[w];
      const qa_md4_bone *bone = &bones[qa_q3_model_at(weight->bone_index, bone_count, "MD4 weight bone")];
      qa_vec4 a = bone->matrix[0], b = bone->matrix[1], c = bone->matrix[2];
      qa_vec3 off = weight->offset, nor = vertex->normal;
      double da_off = FR(FR(FR(a.x * off.x) + FR(a.y * off.y)) + FR(a.z * off.z));
      double db_off = FR(FR(FR(b.x * off.x) + FR(b.y * off.y)) + FR(b.z * off.z));
      double dc_off = FR(FR(FR(c.x * off.x) + FR(c.y * off.y)) + FR(c.z * off.z));
      double da_nor = FR(FR(FR(a.x * nor.x) + FR(a.y * nor.y)) + FR(a.z * nor.z));
      double db_nor = FR(FR(FR(b.x * nor.x) + FR(b.y * nor.y)) + FR(b.z * nor.z));
      double dc_nor = FR(FR(FR(c.x * nor.x) + FR(c.y * nor.y)) + FR(c.z * nor.z));
      x = FR(x + FR(weight->bone_weight * FR(da_off + a.w)));
      y = FR(y + FR(weight->bone_weight * FR(db_off + b.w)));
      z = FR(z + FR(weight->bone_weight * FR(dc_off + c.w)));
      nx = FR(nx + FR(weight->bone_weight * da_nor));
      ny = FR(ny + FR(weight->bone_weight * db_nor));
      nz = FR(nz + FR(weight->bone_weight * dc_nor));
    }
    result[v].position.x = x;
    result[v].position.y = y;
    result[v].position.z = z;
    result[v].normal.x = nx;
    result[v].normal.y = ny;
    result[v].normal.z = nz;
  }
  free(bones);
  *out_count = surface->vertex_count;
  return result;
}
