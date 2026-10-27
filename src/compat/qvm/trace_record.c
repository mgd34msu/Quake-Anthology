/* See include/qa/compat/qvm/trace_record.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/trace_record.h"

#include "qa/core/binary/index.h"

void qa_qvm_write_trace_view(qa_qvm_writable_view *view, const qa_qvm_trace_record *value) {
  if (view->length < QA_QVM_TRACE_BYTES)
    qa_binary_error("QVM trace_t", 0, "record requires %d bytes, received %lld",
                    QA_QVM_TRACE_BYTES, (long long)view->length);
  const qa_source_trace_result *r = &value->result;
  qa_qvm_writable_view_set_int32(view, 0, r->all_solid ? 1 : 0, true);
  qa_qvm_writable_view_set_int32(view, 4, r->start_solid ? 1 : 0, true);
  qa_qvm_writable_view_set_float32(view, 8, r->fraction, true);
  qa_qvm_writable_view_set_float32(view, 12, r->end.x, true);
  qa_qvm_writable_view_set_float32(view, 16, r->end.y, true);
  qa_qvm_writable_view_set_float32(view, 20, r->end.z, true);
  qa_qvm_writable_view_set_float32(view, 24, r->plane.plane.normal.x, true);
  qa_qvm_writable_view_set_float32(view, 28, r->plane.plane.normal.y, true);
  qa_qvm_writable_view_set_float32(view, 32, r->plane.plane.normal.z, true);
  qa_qvm_writable_view_set_float32(view, 36, r->plane.plane.distance, true);
  qa_qvm_writable_view_set_uint8(view, 40, (uint32_t)r->plane.type);
  qa_qvm_writable_view_set_uint8(view, 41, (uint32_t)r->plane.signbits);
  qa_qvm_writable_view_set_uint16(view, 42, 0, true);
  qa_qvm_writable_view_set_int32(view, 44, (int32_t)r->surface_flags, true);
  qa_qvm_writable_view_set_int32(view, 48, (int32_t)r->contents, true);
  qa_qvm_writable_view_set_int32(view, 52, value->entity_num, true);
}

void qa_qvm_write_trace(uint8_t *bytes, size_t length, const qa_qvm_trace_record *value) {
  qa_qvm_memory_writes writes;
  qa_qvm_memory_writes_init(&writes, bytes, length, NULL, NULL);
  qa_qvm_writable_view view;
  qa_qvm_writable_view_init(&view, bytes, length, 0, (int64_t)length, &writes);
  qa_qvm_write_trace_view(&view, value);
  qa_qvm_memory_writes_destroy(&writes);
}
