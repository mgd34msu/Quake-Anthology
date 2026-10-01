#ifndef QA_NATIVE_MODULE_SAVE_H
#define QA_NATIVE_MODULE_SAVE_H

#include "qa/native.h"

/* The content graph owns the immutable artifact bytes. This record preserves
 * the cache's exact module profile, image ABI and opening name, without copying
 * the artifact again. Capture qualifies that actual held artifact against the
 * module's own immutable bytes. No image is mapped or executed by either API. */
bool qa_native_module_checkpoint(const qa_native_module *, qa_bytes artifact,
    qa_buffer *, qa_error *);
bool qa_native_module_restore(qa_bytes state, qa_bytes artifact,
    qa_native_module **, qa_error *);

/* Module cache restoration does not restore an executor's mutable memory,
 * host allocations, callbacks or source initialization state. */

#endif
