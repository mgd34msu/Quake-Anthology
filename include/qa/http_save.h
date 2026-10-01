#ifndef QA_HTTP_SAVE_H
#define QA_HTTP_SAVE_H
#include "qa/http.h"

/* Capture retains request recipes and the exact received continuation. Restore
 * constructs detached records and invokes no network or consumer callback.
 * Each live record requires its actual candidate consumer binding before a
 * matching native multi/easy handle continuation can transfer at publication. */
bool qa_http_create_empty(qa_http **, qa_error *);
bool qa_http_checkpoint_ready(const qa_http *, qa_error *);
bool qa_http_checkpoint(const qa_http *, qa_buffer *, qa_error *);
bool qa_http_restore(qa_http *, qa_bytes, qa_error *);
bool qa_http_restore_callbacks(qa_http *, qa_http_request_id,
                               const qa_http_callbacks *, qa_error *);
typedef struct qa_http_continuation_view {
    const char *url, *method;
    qa_bytes request_body;
    size_t header_count;
    uint32_t timeout_ms, connect_timeout_ms, maximum_redirects;
    uint64_t maximum_response_bytes, response_body_start;
    qa_http_response response;
    qa_error failure;
    bool canceled;
} qa_http_continuation_view;
/* Pure borrowed observations of the actual request, available for detached
 * records too. Every pointer expires at the next mutation of its HTTP owner. */
bool qa_http_continuation_read(const qa_http *, qa_http_request_id,
                               qa_http_continuation_view *, qa_error *);
bool qa_http_continuation_header(const qa_http *, qa_http_request_id, size_t,
                                 const char **line, qa_error *);
/* Readiness must be followed by publication without pumping, canceling,
 * submitting, rebinding or changing either owner in between. */
bool qa_http_handoff_ready(const qa_http *active, const qa_http *candidate, qa_error *);
void qa_http_handoff_publish(qa_http *active, qa_http *candidate);
#endif
