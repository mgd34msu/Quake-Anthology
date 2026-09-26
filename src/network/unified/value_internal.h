#ifndef QA_UNIFIED_VALUE_INTERNAL_H
#define QA_UNIFIED_VALUE_INTERNAL_H

#include "qa/network_unified.h"

typedef struct qa_unified_builder { uint8_t *data; size_t size, capacity, maximum; } qa_unified_builder;
bool qa_unified_append(qa_unified_builder *, const void *, size_t, qa_error *);
bool qa_unified_number_text(double, char out[32], qa_error *);
bool qa_unified_canonical(const qa_json_document *, qa_json_id, qa_unified_builder *, unsigned, qa_error *);
bool qa_unified_tag_check(const qa_json_document *, qa_json_id, unsigned, qa_error *);
bool qa_unified_schema_check(qa_unified_document_kind, const qa_json_document *, qa_error *);

#endif
