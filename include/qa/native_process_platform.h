#ifndef QA_NATIVE_PROCESS_PLATFORM_H
#define QA_NATIVE_PROCESS_PLATFORM_H

#include "qa/native_windows_process.h"
#include "qa/native_sysv_process.h"
#include "qa/filesystem.h"

typedef struct qa_native_process_platform qa_native_process_platform;
/* The enclosing resource graph issues these durable identities. Fresh creation
 * duplicates the actual standard descriptors; it does not open replacement
 * files or derive a stream from a source library name. */
typedef struct qa_native_process_platform_options {
    uint64_t id, standard_ids[3];
} qa_native_process_platform_options;
typedef struct qa_native_process_linux_identity {
    uint32_t uid, effective_uid, gid, effective_gid;
    int64_t clock_ticks;
    char system[256], node[256], release[256], version[256], machine[256], domain[256];
} qa_native_process_linux_identity;
bool qa_native_process_platform_create(const qa_native_process_platform_options *,
    qa_native_process_platform **, qa_error *);
/* Pure independent logical stream graph over the same genuinely held native
 * objects. A later source close cannot invalidate this retained capture. */
bool qa_native_process_platform_capture(const qa_native_process_platform *,
    qa_native_process_platform **, qa_error *);
bool qa_native_process_platform_current(const qa_native_process_platform *, qa_error *);
bool qa_native_process_platform_native_error_read(const qa_native_process_platform *, qa_fs_native_error *, uint64_t *);
/* Pure retained-owner fence for detached capability rebinding. */
bool qa_native_process_platform_retained(const qa_native_process_platform *, qa_error *);
bool qa_native_process_platform_entropy(void *, void *, size_t, qa_error *);
bool qa_native_process_platform_milliseconds(void *, int64_t *, qa_error *);
bool qa_native_process_platform_seconds(void *, int64_t *, qa_error *);
bool qa_native_process_platform_performance(void *, int64_t *, qa_error *);
int64_t qa_native_process_platform_frequency(const qa_native_process_platform *);
/* Pure read of the fresh owner's acquired formatting locale. Unknown POSIX
 * mappings retain an unavailable default, never an assumed English locale. */
bool qa_native_process_platform_locale_read(const qa_native_process_platform *,
    qa_native_windows_locale_profile *, qa_error *);
/* Genuine named Windows NLS comparison. The retained formatting profile and
 * sort version qualify the provider; other hosts have no substitute. */
bool qa_native_process_platform_compare_string(void *, uint32_t, uint32_t, bool,
    const uint16_t *, size_t, const uint16_t *, size_t, int32_t *, uint32_t *, qa_error *);
bool qa_native_process_platform_calendar(void *, int64_t, bool,
    qa_native_windows_calendar *, qa_error *);
/* Actual Linux security credentials/kernel personality and owned stream
 * metadata. Source process IDs are owned by the kernel/physical child, not
 * borrowed from this controller. These calls perform real native observation. */
bool qa_native_process_platform_linux_identity_read(qa_native_process_platform *,
    qa_native_process_linux_identity *, qa_error *);
/* Actual Linux global clocks at full native precision. Source task CPU clocks
 * require that task's retained owner and are not sampled from the controller. */
bool qa_native_process_platform_linux_clock_read(qa_native_process_platform *,
    int32_t, int64_t *, int32_t *, qa_error *);
bool qa_native_process_platform_file_status(qa_native_process_platform *, uint64_t,
    qa_fs_posix_status *, qa_error *);
bool qa_native_process_platform_descriptor_status(qa_native_process_platform *, uint64_t,
    qa_fs_posix_descriptor_status *, qa_error *);
bool qa_native_process_platform_descriptor_flags(qa_native_process_platform *, uint64_t,
    bool append, bool nonblocking, qa_error *);
bool qa_native_process_platform_windows_streams(qa_native_process_platform *,
    qa_native_windows_stream[3], qa_error *);
bool qa_native_process_platform_sysv_files(qa_native_process_platform *,
    qa_native_sysv_file[3], bool *terminal, qa_error *);
/* PROGRAM uses positional access for genuinely seekable standard objects.
 * Library/CRT standard streams retain their sequential native callbacks. */
bool qa_native_process_platform_program_files(qa_native_process_platform *,
    qa_native_sysv_file[3], qa_error *);
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
