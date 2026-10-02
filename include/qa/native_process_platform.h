#ifndef QA_NATIVE_PROCESS_PLATFORM_H
#define QA_NATIVE_PROCESS_PLATFORM_H

#include "qa/native_windows_process.h"
#include "qa/native_sysv_process.h"

typedef struct qa_native_process_platform qa_native_process_platform;
/* The enclosing resource graph issues these durable identities. Fresh creation
 * duplicates the actual standard descriptors; it does not open replacement
 * files or derive a stream from a source library name. */
typedef struct qa_native_process_platform_options {
    uint64_t id, standard_ids[3];
} qa_native_process_platform_options;
bool qa_native_process_platform_create(const qa_native_process_platform_options *,
    qa_native_process_platform **, qa_error *);
/* Pure independent logical stream graph over the same genuinely held native
 * objects. A later source close cannot invalidate this retained capture. */
bool qa_native_process_platform_capture(const qa_native_process_platform *,
    qa_native_process_platform **, qa_error *);
bool qa_native_process_platform_current(const qa_native_process_platform *, qa_error *);
/* Pure retained-owner fence for detached capability rebinding. */
bool qa_native_process_platform_retained(const qa_native_process_platform *, qa_error *);
bool qa_native_process_platform_entropy(void *, void *, size_t, qa_error *);
bool qa_native_process_platform_milliseconds(void *, int64_t *, qa_error *);
bool qa_native_process_platform_seconds(void *, int64_t *, qa_error *);
bool qa_native_process_platform_performance(void *, int64_t *, qa_error *);
int64_t qa_native_process_platform_frequency(const qa_native_process_platform *);
bool qa_native_process_platform_calendar(void *, int64_t, bool,
    qa_native_windows_calendar *, qa_error *);
bool qa_native_process_platform_windows_streams(qa_native_process_platform *,
    qa_native_windows_stream[3], qa_error *);
bool qa_native_process_platform_sysv_files(qa_native_process_platform *,
    qa_native_sysv_file[3], bool *terminal, qa_error *);
/* Detached continuations borrow this retained external owner. Validate actual
 * identities before adoption; no descriptor duplication or clock/entropy/I/O
 * occurs on this path. */
bool qa_native_process_platform_checkpoint(const qa_native_process_platform *, qa_buffer *, qa_error *);
bool qa_native_process_platform_validate(const qa_native_process_platform *, qa_bytes, qa_error *);
void qa_native_process_platform_retain(qa_native_process_platform *);
/* Actual descriptor close is checked. Successful closes remain consumed even
 * when another close fails; callback contexts survive for the retry. */
bool qa_native_process_platform_release(qa_native_process_platform **, qa_error *);

#endif
