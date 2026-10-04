#ifndef QA_APPLICATION_UNIFIED_OUTPUT_JSON_H
#define QA_APPLICATION_UNIFIED_OUTPUT_JSON_H

#include "qa/network_unified.h"
#include "qa/world.h"

typedef struct application_unified_json {
    qa_buffer bytes;
    size_t capacity;
} application_unified_json;

bool application_unified_json_append(application_unified_json *, qa_bytes, qa_error *);
bool application_unified_json_text(application_unified_json *, const char *, qa_error *);
bool application_unified_json_string(application_unified_json *, const char *, qa_error *);
bool application_unified_json_percent_encoded(application_unified_json *, const char *, qa_error *);
bool application_unified_json_number(application_unified_json *, double, qa_error *);
bool application_unified_json_natural(application_unified_json *, uint64_t, qa_error *);
bool application_unified_json_actor(application_unified_json *, qa_actor_id, qa_error *);
bool application_unified_json_vector(application_unified_json *, qa_vec3, qa_error *);
bool application_unified_json_bounds(application_unified_json *, qa_bounds, qa_error *);
bool application_unified_json_body(application_unified_json *, const qa_body_state *, qa_error *);
bool application_unified_json_document(application_unified_json *, const qa_unified_document *, qa_error *);
void application_unified_json_dispose(application_unified_json *);

#endif
