#ifndef QA_HTTP_SAVE_H
#define QA_HTTP_SAVE_H
#include "qa/http.h"

/* Native transfers cannot survive a process checkpoint. Admission requires
 * the actual multi owner to contain no transfer, including canceled nodes.
 * Restore changes only the request sequence on an empty native owner. Empty
 * construction rejects requests and pumping until a full restore succeeds. */
bool qa_http_create_empty(qa_http **, qa_error *);
bool qa_http_checkpoint_ready(const qa_http *, qa_error *);
bool qa_http_checkpoint(const qa_http *, qa_buffer *, qa_error *);
bool qa_http_restore(qa_http *, qa_bytes, qa_error *);
#endif
