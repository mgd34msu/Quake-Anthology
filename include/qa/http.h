#ifndef QA_HTTP_H
#define QA_HTTP_H
#include "qa/common.h"

typedef struct qa_http qa_http;
typedef uint64_t qa_http_request_id;
typedef struct qa_http_header { const char *name, *value; } qa_http_header;
typedef struct qa_http_response {
    unsigned status;
    const char *content_type;
    uint64_t bytes;
    uint64_t range_first, range_last, range_total;
    bool has_range;
} qa_http_response;
typedef struct qa_http_callbacks {
    void *context;
    /* Header notification occurs for each HTTP response (including redirects).
     * Strings/bytes borrow this call. False stops the transfer with the supplied
     * error. Callbacks may cancel requests, but must defer submit/pump/destroy. */
    bool (*headers)(void *, qa_http_request_id, const qa_http_response *, qa_error *);
    bool (*body)(void *, qa_http_request_id, const qa_http_response *, qa_bytes, qa_error *);
    void (*complete)(void *, qa_http_request_id, const qa_http_response *, const qa_error *);
} qa_http_callbacks;
typedef struct qa_http_request {
    const char *url, *method;
    const qa_http_header *headers;
    size_t header_count;
    qa_bytes body;
    uint32_t timeout_ms, connect_timeout_ms;
    uint64_t maximum_response_bytes; /* Zero permits arbitrary download length. */
    uint32_t maximum_redirects; /* Zero rejects automatic redirects. */
    qa_http_callbacks callbacks;
} qa_http_request;
/* Native libcurl multi owner. No worker thread or feature-private HTTP client.
 * Calls require one owner thread. submit copies URL/headers/body before return;
 * callbacks run only in pump. TLS verification is always enabled. */
bool qa_http_create(qa_http **, qa_error *);
bool qa_http_destroy(qa_http *, qa_error *);
bool qa_http_submit(qa_http *, const qa_http_request *, qa_http_request_id *, qa_error *);
/* Nonblocking progress; caller's ordinary event loop chooses its next wakeup. */
bool qa_http_pump(qa_http *, qa_error *);
bool qa_http_next_timeout(const qa_http *, uint32_t *milliseconds, bool *pending, qa_error *);
/* Idempotent; canceled requests receive no later consumer callbacks. Cancel is
 * legal inside a callback, including for that callback's current request. */
void qa_http_cancel(qa_http *, qa_http_request_id);
size_t qa_http_pending(const qa_http *);
/* Read-only retirement admission; pending transfers may still be canceled. */
bool qa_http_callbacks_idle(const qa_http *);
#endif
