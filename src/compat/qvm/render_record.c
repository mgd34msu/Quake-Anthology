/* See include/qa/compat/qvm/render_record.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/render_record.h"

#include <string.h>

#include "qa/core/binary/index.h"

static void require_bytes(const qa_qvm_writable_view *view, int64_t bytes, const char *source) {
  if (view->length < bytes)
    qa_binary_error(source, 0, "record requires %lld bytes, received %lld",
                    (long long)bytes, (long long)view->length);
}

static qa_vec3 read_vector(const qa_qvm_writable_view *view, int64_t offset) {
  qa_vec3 result = {(double)qa_qvm_writable_view_get_float32(view, offset, true),
                    (double)qa_qvm_writable_view_get_float32(view, offset + 4, true),
                    (double)qa_qvm_writable_view_get_float32(view, offset + 8, true)};
  return result;
}

static qa_axis read_axis(const qa_qvm_writable_view *view, int64_t offset) {
  qa_axis result;
  result.vectors[0] = read_vector(view, offset);
  result.vectors[1] = read_vector(view, offset + 12);
  result.vectors[2] = read_vector(view, offset + 24);
  return result;
}

static qa_vec4 read_color(const qa_qvm_writable_view *view, int64_t offset) {
  qa_vec4 result = {qa_qvm_writable_view_get_uint8(view, offset),
                    qa_qvm_writable_view_get_uint8(view, offset + 1),
                    qa_qvm_writable_view_get_uint8(view, offset + 2),
                    qa_qvm_writable_view_get_uint8(view, offset + 3)};
  return result;
}

void qa_qvm_read_ref_entity(const qa_qvm_writable_view *view, qa_qvm_source_ref_entity_record *out) {
  require_bytes(view, QA_QVM_REF_ENTITY_BYTES, "QVM refEntity_t");
  int32_t type = qa_qvm_writable_view_get_int32(view, 0, true);
  if (type < 0 || type > 7)
    qa_binary_error("QVM refEntity_t", 0, "unsupported entity type %d", (int)type);
  out->render_flags = qa_qvm_writable_view_get_int32(view, 4, true);
  out->model = qa_qvm_writable_view_get_int32(view, 8, true);
  out->lighting_origin = read_vector(view, 12);
  out->shadow_plane = qa_qvm_writable_view_get_float32(view, 24, true);
  out->axis = read_axis(view, 28);
  out->non_normalized_axes = qa_qvm_writable_view_get_int32(view, 64, true) != 0;
  out->origin = read_vector(view, 68);
  out->frame = qa_qvm_writable_view_get_int32(view, 80, true);
  out->old_origin = read_vector(view, 84);
  out->old_frame = qa_qvm_writable_view_get_int32(view, 96, true);
  out->back_lerp = qa_qvm_writable_view_get_float32(view, 100, true);
  out->skin_num = qa_qvm_writable_view_get_int32(view, 104, true);
  out->custom_skin = qa_qvm_writable_view_get_int32(view, 108, true);
  out->custom_shader = qa_qvm_writable_view_get_int32(view, 112, true);
  out->shader_rgba = read_color(view, 116);
  out->shader_tex_coord.x = (double)qa_qvm_writable_view_get_float32(view, 120, true);
  out->shader_tex_coord.y = (double)qa_qvm_writable_view_get_float32(view, 124, true);
  out->shader_time = qa_qvm_writable_view_get_float32(view, 128, true);
  out->radius = qa_qvm_writable_view_get_float32(view, 132, true);
  out->rotation = qa_qvm_writable_view_get_float32(view, 136, true);
  static const char *const kinds[] = {"model", "poly", "sprite", "beam",
                                      "rail-core", "rail-rings", "lightning", "portal-surface"};
  out->kind = kinds[type];
}

void qa_qvm_read_refdef(const qa_qvm_writable_view *view, qa_qvm_refdef *out) {
  require_bytes(view, QA_QVM_REFDEF_BYTES, "QVM refdef_t");
  out->x = qa_qvm_writable_view_get_int32(view, 0, true);
  out->y = qa_qvm_writable_view_get_int32(view, 4, true);
  out->width = qa_qvm_writable_view_get_int32(view, 8, true);
  out->height = qa_qvm_writable_view_get_int32(view, 12, true);
  out->fov_x = qa_qvm_writable_view_get_float32(view, 16, true);
  out->fov_y = qa_qvm_writable_view_get_float32(view, 20, true);
  out->view_origin = read_vector(view, 24);
  out->view_axis = read_axis(view, 36);
  out->time = qa_qvm_writable_view_get_int32(view, 72, true);
  out->render_flags = qa_qvm_writable_view_get_int32(view, 76, true);
  memcpy(out->area_mask, view->bytes + view->start + 80, 32);
  for (int row = 0; row < 8; row++) {
    int64_t offset = 112 + row * 32;
    bool has_nul = false;
    for (int i = 0; i < 32; i++) {
      out->text[row][i] = qa_qvm_writable_view_get_uint8(view, offset + i);
      if (out->text[row][i] == 0) has_nul = true;
    }
    if (!has_nul)
      qa_binary_error("QVM refdef_t", offset, "render text row has no NUL within 32 bytes");
  }
}

void qa_qvm_read_poly_vertices(const qa_qvm_writable_view *view, int64_t count, qa_qvm_ref_poly_vertex *out) {
  if (count < 0 || count > view->length / QA_QVM_POLY_VERTEX_BYTES)
    qa_binary_error("QVM polyVert_t", 0, "invalid vertex count %lld for %lld bytes",
                    (long long)count, (long long)view->length);
  for (int64_t i = 0; i < count; i++) {
    int64_t offset = i * QA_QVM_POLY_VERTEX_BYTES;
    out[i].position = read_vector(view, offset);
    out[i].tex_coord.x = (double)qa_qvm_writable_view_get_float32(view, offset + 12, true);
    out[i].tex_coord.y = (double)qa_qvm_writable_view_get_float32(view, offset + 16, true);
    out[i].color = read_color(view, offset + 20);
  }
}

static void write_vector(qa_qvm_writable_view *view, int64_t offset, qa_vec3 value) {
  qa_qvm_writable_view_set_float32(view, offset, value.x, true);
  qa_qvm_writable_view_set_float32(view, offset + 4, value.y, true);
  qa_qvm_writable_view_set_float32(view, offset + 8, value.z, true);
}

void qa_qvm_write_orientation(qa_qvm_writable_view *view, qa_vec3 origin, const qa_axis *axes) {
  require_bytes(view, QA_QVM_ORIENTATION_BYTES, "QVM orientation_t");
  write_vector(view, 0, origin);
  write_vector(view, 12, axes->vectors[0]);
  write_vector(view, 24, axes->vectors[1]);
  write_vector(view, 36, axes->vectors[2]);
}
