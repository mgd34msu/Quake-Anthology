/*
 * Port of src/compat/qvm/trace-record.ts. The view is a raw byte span
 * (writable-view setters publish identically when the caller passes a
 * QvmWritableView-backed span through qa_qvm_write_trace_view).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_TRACE_RECORD_H
#define QA_COMPAT_QVM_TRACE_RECORD_H

#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory_writes.h"
#include "qa/world/collision/q3/world.h"

#define QA_QVM_TRACE_BYTES 56

/* TS QvmTraceRecord extends SourceTraceResult. */
typedef struct qa_qvm_trace_record {
  qa_source_trace_result result;
  int32_t entity_num;
} qa_qvm_trace_record;

void qa_qvm_write_trace(uint8_t *bytes, size_t length, const qa_qvm_trace_record *value);
void qa_qvm_write_trace_view(qa_qvm_writable_view *view, const qa_qvm_trace_record *value);

#endif
