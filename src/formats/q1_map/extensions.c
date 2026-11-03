/* Port of src/formats/q1-map/extensions.ts. GPL-2.0-or-later. */
#include "qa/formats/q1_map/extensions.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/q1_map/records.h"
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

static void section_or_die(const qa_binary_reader *reader, int64_t offset, int64_t length,
                           qa_binary_reader *out) {
  if (!qa_binary_reader_section(reader, offset, length, out)) qa_range_error("Out of memory");
}

static qa_binary_reader make_reader(const uint8_t *data, size_t length, const char *source) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)data, length, source)) qa_range_error("Out of memory");
  return reader;
}

qa_q1_bspx_lump *qa_q1_read_bspx(qa_binary_reader *reader, const qa_q1_lump *lumps,
                                 size_t lump_count, size_t *out_count) {
  *out_count = 0;
  double end = 124;
  bool overlaps_header = false;
  for (size_t i = 0; i < lump_count; i++) {
    double lump_end = lumps[i].offset + lumps[i].length;
    if (lump_end > end) end = lump_end;
    if (lumps[i].length > 0 && lumps[i].offset < 132 && lump_end > 124) overlaps_header = true;
  }
  double candidates[2] = {ceil(end / 4) * 4, 124};
  size_t candidate_count = overlaps_header ? 1 : 2;
  for (size_t c = 0; c < candidate_count; c++) {
    double offset = candidates[c];
    if (offset > (double)reader->length - 8) continue;
    qa_binary_reader magic;
    section_or_die(reader, (int64_t)offset, 4, &magic);
    char *magic_text = qa_binary_reader_fixed_byte_string(&magic, 4);
    qa_binary_reader_destroy(&magic);
    bool is_bspx = magic_text != NULL && strcmp(magic_text, "BSPX") == 0;
    free(magic_text);
    if (!is_bspx) continue;
    qa_binary_reader directory;
    section_or_die(reader, (int64_t)offset + 4, (int64_t)reader->length - (int64_t)offset - 4, &directory);
    uint32_t count = qa_binary_reader_u32(&directory);
    if ((double)count > (double)qa_binary_reader_remaining(&directory) / 32) {
      qa_binary_error(reader->source, (int64_t)offset, "invalid BSPX count %u", (unsigned)count);
    }
    qa_q1_bspx_lump *result = xmalloc(count * sizeof *result);
    for (uint32_t i = 0; i < count; i++) {
      result[i].name = qa_binary_reader_fixed_byte_string(&directory, 24);
      result[i].offset = qa_binary_reader_u32(&directory);
      result[i].length = qa_binary_reader_u32(&directory);
      qa_binary_reader section;
      section_or_die(reader, (int64_t)result[i].offset, (int64_t)result[i].length, &section);
      result[i].data = qa_binary_reader_bytes(&section, (int64_t)result[i].length);
      result[i].data_length = (size_t)result[i].length;
      qa_binary_reader_destroy(&section);
    }
    qa_binary_reader_destroy(&directory);
    *out_count = count;
    return result;
  }
  return NULL;
}

bool qa_q1_bspx_data(const qa_q1_bspx_lump *lumps, size_t lump_count, const char *name,
                     const uint8_t **out_data, size_t *out_length) {
  for (size_t i = 0; i < lump_count; i++) {
    if (strcmp(lumps[i].name, name) == 0) {
      *out_data = lumps[i].data;
      *out_length = lumps[i].data_length;
      return true;
    }
  }
  return false;
}

bool qa_q1_read_decoupled_lightmaps(bool has_data, const uint8_t *data, size_t data_length,
                                    double face_count, qa_decoupled_lightmap **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (!has_data) return false;
  qa_binary_reader reader = make_reader(data, data_length, "BSPX DECOUPLED_LM");
  if ((double)data_length != face_count * 40) {
    qa_binary_error(reader.source, 0, "record count does not match faces");
  }
  size_t count = data_length / 40;
  qa_decoupled_lightmap *result = xmalloc(count * sizeof *result);
  for (size_t i = 0; i < count; i++) {
    result[i].width = qa_binary_reader_u16(&reader);
    result[i].height = qa_binary_reader_u16(&reader);
    result[i].has_lighting_offset = qa_q1_optional_offset(qa_binary_reader_u32(&reader), &result[i].lighting_offset);
    qa_vec4 s = qa_q1_read_vec4(&reader);
    qa_vec4 t = qa_q1_read_vec4(&reader);
    result[i].axes[0].x = s.x; result[i].axes[0].y = s.y; result[i].axes[0].z = s.z;
    result[i].axes[1].x = t.x; result[i].axes[1].y = t.y; result[i].axes[1].z = t.z;
    result[i].offset.x = s.w;
    result[i].offset.y = t.w;
  }
  qa_binary_reader_destroy(&reader);
  *out = result;
  *out_count = count;
  return true;
}

bool qa_q1_read_brush_list(bool has_data, const uint8_t *data, size_t data_length,
                           double model_count, qa_q1_brush_list_model **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (!has_data) return false;
  qa_binary_reader reader = make_reader(data, data_length, "BSPX BRUSHLIST");
  qa_q1_brush_list_model *models = NULL;
  size_t count = 0, capacity = 0;
  while (qa_binary_reader_remaining(&reader) > 0) {
    uint32_t version = qa_binary_reader_u32(&reader);
    if (version != 1) {
      qa_q1_brush_list_destroy(models, count);
      qa_binary_reader_destroy(&reader);
      return false;
    }
    double model = qa_binary_reader_u32(&reader);
    qa_q1_check_index(model, model_count, reader.source);
    uint32_t brush_count = qa_binary_reader_u32(&reader);
    uint32_t expected_planes = qa_binary_reader_u32(&reader);
    double remaining = (double)qa_binary_reader_remaining(&reader);
    if ((double)brush_count > remaining / 28 || (double)expected_planes > (remaining - (double)brush_count * 28) / 16) {
      qa_binary_error(reader.source, (int64_t)reader.cursor, "brushes or planes exceed lump");
    }
    qa_q1_brush_list_brush *brushes = xmalloc(brush_count * sizeof *brushes);
    double plane_total = 0;
    for (uint32_t i = 0; i < brush_count; i++) {
      brushes[i].bounds = qa_q1_read_bounds(&reader, true);
      brushes[i].contents = qa_binary_reader_i16(&reader);
      uint16_t plane_count = qa_binary_reader_u16(&reader);
      plane_total += plane_count;
      if (plane_total > expected_planes) {
        qa_binary_error(reader.source, (int64_t)reader.cursor, "brush plane count exceeds model total");
      }
      qa_plane *planes = xmalloc(plane_count * sizeof *planes);
      for (uint16_t j = 0; j < plane_count; j++) {
        planes[j].normal = qa_q1_read_vec3(&reader);
        planes[j].distance = qa_binary_reader_finite_f32(&reader);
      }
      brushes[i].planes = planes;
      brushes[i].plane_count = plane_count;
    }
    if (plane_total != expected_planes) {
      qa_binary_error(reader.source, (int64_t)reader.cursor, "brush plane count differs from model total");
    }
    if (count == capacity) {
      capacity = capacity ? capacity * 2 : 4;
      models = xrealloc(models, capacity * sizeof *models);
    }
    models[count].model = model;
    models[count].brushes = brushes;
    models[count].brush_count = brush_count;
    count++;
  }
  qa_binary_reader_destroy(&reader);
  *out = models;
  *out_count = count;
  return true;
}

static bool per_face_styles(bool has_data, const uint8_t *data, size_t data_length, double face_count,
                            bool wide, qa_q1_style_row **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (!has_data) return false;
  qa_binary_reader reader = make_reader(data, data_length, wide ? "BSPX LMSTYLE16" : "BSPX LMSTYLE");
  double width = wide ? 2 : 1;
  if (face_count == 0) {
    if (data_length > 0) qa_binary_error(reader.source, 0, "styles with no faces");
    qa_binary_reader_destroy(&reader);
    *out = xmalloc(0);
    return true;
  }
  if (fmod((double)data_length, width * face_count) != 0) {
    qa_binary_error(reader.source, 0, "style count does not match faces");
  }
  size_t per_face = (size_t)((double)data_length / (width * face_count));
  size_t faces = (size_t)face_count;
  qa_q1_style_row *result = xmalloc(faces * sizeof *result);
  for (size_t i = 0; i < faces; i++) {
    result[i].values = xmalloc(per_face * sizeof *result[i].values);
    result[i].count = per_face;
    for (size_t j = 0; j < per_face; j++) {
      result[i].values[j] = wide ? qa_binary_reader_u16(&reader) : qa_binary_reader_u8(&reader);
    }
  }
  qa_binary_reader_destroy(&reader);
  *out = result;
  *out_count = faces;
  return true;
}

static qa_q1_face_normals *read_face_normals(bool has_data, const uint8_t *data, size_t data_length,
                                             double corner_count) {
  if (!has_data) return NULL;
  qa_binary_reader reader = make_reader(data, data_length, "BSPX FACENORMALS");
  uint32_t count = qa_binary_reader_u32(&reader);
  qa_binary_reader section;
  section_or_die(&reader, (int64_t)reader.cursor, (int64_t)count * 12, &section);
  qa_q1_face_normals *result = xmalloc(sizeof *result);
  result->vectors = xmalloc(count * sizeof *result->vectors);
  result->vector_count = count;
  for (uint32_t i = 0; i < count; i++) result->vectors[i] = qa_q1_read_vec3(&section);
  qa_binary_reader_destroy(&section);
  qa_binary_reader_skip(&reader, (int64_t)count * 12);
  size_t corners = (size_t)corner_count;
  /* TS grows the list one corner at a time and the reads fail at the end of
   * the lump, so never reserve more corners than the lump can hold. */
  size_t room = qa_binary_reader_remaining(&reader) / 12;
  result->indices = xmalloc((corners < room ? corners : room) * sizeof *result->indices);
  result->index_count = corners;
  for (size_t i = 0; i < corners; i++) {
    double normal = qa_binary_reader_u32(&reader);
    double tangent = qa_binary_reader_u32(&reader);
    double bitangent = qa_binary_reader_u32(&reader);
    qa_q1_check_index(normal, count, reader.source);
    qa_q1_check_index(tangent, count, reader.source);
    qa_q1_check_index(bitangent, count, reader.source);
    result->indices[i][0] = normal;
    result->indices[i][1] = tangent;
    result->indices[i][2] = bitangent;
  }
  qa_binary_reader_destroy(&reader);
  return result;
}

static bool exact_lump(const qa_q1_bspx_lump *lumps, size_t lump_count, const char *name, double length,
                       const uint8_t **out_data, size_t *out_length) {
  if (!qa_q1_bspx_data(lumps, lump_count, name, out_data, out_length)) return false;
  if ((double)*out_length != length) {
    char source[64];
    snprintf(source, sizeof source, "BSPX %s", name);
    qa_binary_error(source, 0, "expected %s bytes, got %zu", QA_JS_NUMBER(length), *out_length);
  }
  return true;
}

static uint8_t *copy_bytes(const uint8_t *data, size_t length) {
  uint8_t *copy = xmalloc(length);
  if (length > 0) memcpy(copy, data, length);
  return copy;
}

void qa_q1_read_bspx_metadata(const qa_q1_bspx_lump *lumps, size_t lump_count, double face_count,
                              double vertex_count, double corner_count, qa_q1_bspx_metadata *out) {
  memset(out, 0, sizeof *out);
  const uint8_t *data = NULL;
  size_t length = 0;
  /* TS checks LMOFFSET and VERTEXNORMALS before building the record, so
   * their length errors win over LMSHIFT's and the styles'. */
  const uint8_t *offsets_data = NULL, *normals_data = NULL;
  size_t offsets_length = 0, normals_length = 0;
  bool has_offsets = exact_lump(lumps, lump_count, "LMOFFSET", face_count * 4, &offsets_data, &offsets_length);
  bool has_normals = exact_lump(lumps, lump_count, "VERTEXNORMALS", vertex_count * 12, &normals_data, &normals_length);
  if (exact_lump(lumps, lump_count, "LMSHIFT", face_count, &data, &length)) {
    out->lightmap_shifts = copy_bytes(data, length);
    out->lightmap_shift_count = length;
  }
  if (has_offsets) {
    data = offsets_data;
    length = offsets_length;
    qa_binary_reader reader = make_reader(data, length, "BSPX LMOFFSET");
    size_t count = length / 4;
    out->lightmap_offsets = xmalloc(count * sizeof *out->lightmap_offsets);
    out->lightmap_offset_count = count;
    for (size_t i = 0; i < count; i++) {
      out->lightmap_offsets[i].has_value = qa_q1_optional_offset(qa_binary_reader_u32(&reader),
                                                                 &out->lightmap_offsets[i].value);
    }
    qa_binary_reader_destroy(&reader);
  }
  bool has = qa_q1_bspx_data(lumps, lump_count, "LMSTYLE", &data, &length);
  per_face_styles(has, data, length, face_count, false, &out->lightmap_styles, &out->lightmap_style_count);
  has = qa_q1_bspx_data(lumps, lump_count, "LMSTYLE16", &data, &length);
  per_face_styles(has, data, length, face_count, true, &out->lightmap_styles16, &out->lightmap_style16_count);
  if (qa_q1_bspx_data(lumps, lump_count, "RGBLIGHTING", &data, &length)) {
    out->rgb_lighting = copy_bytes(data, length);
    out->rgb_lighting_length = length;
  }
  if (qa_q1_bspx_data(lumps, lump_count, "LIGHTING_E5BGR9", &data, &length)) {
    qa_binary_reader reader = make_reader(data, length, "BSPX LIGHTING_E5BGR9");
    if (length % 4 != 0) qa_binary_error(reader.source, 0, "lump length %zu is not a multiple of 4", length);
    size_t count = length / 4;
    out->hdr_lighting = xmalloc(count * sizeof *out->hdr_lighting);
    out->hdr_lighting_count = count;
    for (size_t i = 0; i < count; i++) out->hdr_lighting[i] = qa_binary_reader_u32(&reader);
    qa_binary_reader_destroy(&reader);
  }
  if (qa_q1_bspx_data(lumps, lump_count, "LIGHTINGDIR", &data, &length)) {
    out->lighting_directions = copy_bytes(data, length);
    out->lighting_direction_length = length;
  }
  if (has_normals) {
    data = normals_data;
    length = normals_length;
    qa_binary_reader reader = make_reader(data, length, "BSPX VERTEXNORMALS");
    size_t count = length / 12;
    out->vertex_normals = xmalloc(count * sizeof *out->vertex_normals);
    out->vertex_normal_count = count;
    for (size_t i = 0; i < count; i++) out->vertex_normals[i] = qa_q1_read_vec3(&reader);
    qa_binary_reader_destroy(&reader);
  }
  has = qa_q1_bspx_data(lumps, lump_count, "FACENORMALS", &data, &length);
  out->face_normals = read_face_normals(has, data, length, corner_count);
}

void qa_q1_bspx_metadata_destroy(qa_q1_bspx_metadata *metadata) {
  if (metadata == NULL) return;
  free(metadata->lightmap_shifts);
  free(metadata->lightmap_offsets);
  for (size_t i = 0; i < metadata->lightmap_style_count; i++) free(metadata->lightmap_styles[i].values);
  free(metadata->lightmap_styles);
  for (size_t i = 0; i < metadata->lightmap_style16_count; i++) free(metadata->lightmap_styles16[i].values);
  free(metadata->lightmap_styles16);
  free(metadata->rgb_lighting);
  free(metadata->hdr_lighting);
  free(metadata->lighting_directions);
  free(metadata->vertex_normals);
  if (metadata->face_normals != NULL) {
    free(metadata->face_normals->vectors);
    free(metadata->face_normals->indices);
    free(metadata->face_normals);
  }
  memset(metadata, 0, sizeof *metadata);
}

void qa_q1_bspx_destroy(qa_q1_bspx_lump *lumps, size_t count) {
  if (lumps == NULL) return;
  for (size_t i = 0; i < count; i++) {
    free(lumps[i].name);
    free(lumps[i].data);
  }
  free(lumps);
}

void qa_q1_brush_list_destroy(qa_q1_brush_list_model *models, size_t count) {
  if (models == NULL) return;
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < models[i].brush_count; j++) free((void *)models[i].brushes[j].planes);
    free((void *)models[i].brushes);
  }
  free(models);
}
