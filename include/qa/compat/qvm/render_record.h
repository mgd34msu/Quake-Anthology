/*
 * Port of src/compat/qvm/render-record.ts. Records are the content/q3
 * presentation types: SourceRefEntityRecord (ref_entity.h, model/skin/
 * shader as VM handles), Refdef (refdef.h, eight complete 32-byte rows)
 * and RefPolyVertex. Reads fill caller out parameters.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_RENDER_RECORD_H
#define QA_COMPAT_QVM_RENDER_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/memory_writes.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/refdef.h"
#include "qa/contracts/math.h"

#define QA_QVM_REF_ENTITY_BYTES 140
#define QA_QVM_REFDEF_BYTES 368
#define QA_QVM_POLY_VERTEX_BYTES 24
#define QA_QVM_ORIENTATION_BYTES 48

/* TS readQvmRefEntity: handles stay unresolved (is_handle set). */
void qa_qvm_read_ref_entity(const qa_qvm_writable_view *view, qa_q3_pres_source_ref_entity_record *out);
/* TS readQvmRefdef: owns the mask and the complete byte rows, including
 * bytes after NUL (each row length 32). */
void qa_qvm_read_refdef(const qa_qvm_writable_view *view, qa_q3_pres_refdef *out);
/* Fills out[0..count); count validated against the view extent. */
void qa_qvm_read_poly_vertices(const qa_qvm_writable_view *view, int64_t count, qa_q3_pres_ref_poly_vertex *out);
void qa_qvm_write_orientation(qa_qvm_writable_view *view, qa_vec3 origin, const qa_axis *axes);

#endif
