/*
 * Port of src/compat/qvm/render-record.ts. Mirrored types (TS owners
 * src/content/q3/presentation/ref-entity.ts and refdef.ts, not yet
 * landed): qa_qvm_source_ref_entity_record mirrors
 * SourceRefEntityRecord (kind keeps exact TS strings), qa_qvm_refdef
 * mirrors Refdef (areaMask copied to caller storage; text rows keep
 * all 32 bytes including bytes after NUL), qa_qvm_ref_poly_vertex
 * mirrors RefPolyVertex. Reads fill caller out parameters.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_RENDER_RECORD_H
#define QA_COMPAT_QVM_RENDER_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/memory_writes.h"
#include "qa/contracts/math.h"

#define QA_QVM_REF_ENTITY_BYTES 140
#define QA_QVM_REFDEF_BYTES 368
#define QA_QVM_POLY_VERTEX_BYTES 24
#define QA_QVM_ORIENTATION_BYTES 48

typedef struct qa_qvm_source_ref_entity_record {
  const char *kind; /* model|poly|sprite|beam|rail-core|rail-rings|lightning|portal-surface */
  int32_t render_flags;
  int32_t model;
  qa_vec3 lighting_origin;
  float shadow_plane;
  qa_axis axis;
  bool non_normalized_axes;
  qa_vec3 origin;
  int32_t frame;
  qa_vec3 old_origin;
  int32_t old_frame;
  float back_lerp;
  int32_t skin_num;
  int32_t custom_skin;
  int32_t custom_shader;
  qa_vec4 shader_rgba; /* byte channels as numbers, as in the TS */
  qa_vec2 shader_tex_coord;
  float shader_time;
  float radius;
  float rotation;
} qa_qvm_source_ref_entity_record;

typedef struct qa_qvm_refdef {
  int32_t x, y, width, height;
  float fov_x, fov_y;
  qa_vec3 view_origin;
  qa_axis view_axis;
  int32_t time;
  int32_t render_flags;
  uint8_t area_mask[32];
  uint8_t text[8][32]; /* complete rows, bytes after NUL retained */
} qa_qvm_refdef;

typedef struct qa_qvm_ref_poly_vertex {
  qa_vec3 position;
  qa_vec2 tex_coord;
  qa_vec4 color;
} qa_qvm_ref_poly_vertex;

void qa_qvm_read_ref_entity(const qa_qvm_writable_view *view, qa_qvm_source_ref_entity_record *out);
void qa_qvm_read_refdef(const qa_qvm_writable_view *view, qa_qvm_refdef *out);
/* Fills out[0..count); count validated against the view extent. */
void qa_qvm_read_poly_vertices(const qa_qvm_writable_view *view, int64_t count, qa_qvm_ref_poly_vertex *out);
void qa_qvm_write_orientation(qa_qvm_writable_view *view, qa_vec3 origin, const qa_axis *axes);

#endif
