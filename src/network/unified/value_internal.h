#ifndef QA_UNIFIED_VALUE_INTERNAL_H
#define QA_UNIFIED_VALUE_INTERNAL_H

#include "qa/network_unified.h"
#include "qa/network_unified_session.h"
#include "qa/network_unified_frame_pool.h"

typedef struct qa_unified_builder { uint8_t *data; size_t size, capacity, maximum; const qa_strings *strings, *baseline_strings; } qa_unified_builder;
bool qa_unified_append(qa_unified_builder *, const void *, size_t, qa_error *);
bool qa_unified_number_text(double, char out[32], qa_error *);
bool qa_unified_canonical(const qa_json_document *, qa_json_id, qa_unified_builder *, unsigned, qa_error *);
bool qa_unified_tag_check(const qa_json_document *, qa_json_id, unsigned, qa_error *);
bool qa_unified_schema_check(qa_unified_document_kind, const qa_json_document *, qa_error *);
bool qa_unified_frame_write(const qa_unified_document *, const qa_unified_document *,
    uint32_t baseline_sequence, size_t maximum_bytes, qa_unified_builder *, qa_error *);
bool qa_unified_frame_baseline(qa_bytes, uint32_t *, qa_error *);
bool qa_unified_frame_decode(qa_bytes, const qa_unified_document *, uint32_t baseline_sequence,
    qa_unified_frame_pool *, qa_strings *, qa_unified_document **, qa_error *);
bool qa_unified_document_create_inputs(qa_unified_input_batch **, qa_unified_document **, qa_error *);
size_t qa_unified_document_memory(const qa_unified_document *);
bool qa_unified_document_equal(const qa_unified_document *, const qa_unified_document *);
bool qa_unified_inputs_check(qa_unified_input_batch *, size_t *, qa_error *);
bool qa_unified_inputs_copy(uint32_t, const qa_unified_input *, size_t, qa_unified_input_batch *, qa_error *);
bool qa_unified_inputs_write(const qa_unified_input_batch *, size_t, qa_unified_builder *, qa_error *);

#endif
