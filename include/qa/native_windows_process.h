#ifndef QA_NATIVE_WINDOWS_PROCESS_H
#define QA_NATIVE_WINDOWS_PROCESS_H

#include "qa/native_guest.h"

typedef struct qa_native_windows_process qa_native_windows_process;
/* All runtime service IDs, including dynamically unresolved imports, occupy
 * [this value, UINT64_MAX]. Source SDK callbacks use the separate lower range. */
uint64_t qa_native_windows_process_callback_minimum(void);
typedef struct qa_native_windows_artifact {
    uint64_t id, load_base;
    qa_native_image_info image;
    qa_bytes bytes;
    size_t maximum_image_bytes;
    const char *path;
} qa_native_windows_artifact;
typedef struct qa_native_windows_calendar {
    int32_t year, month, weekday, day, hour, minute, second, millisecond;
    int32_t year_day, daylight, timezone_minutes;
} qa_native_windows_calendar;
typedef struct qa_native_windows_file {
    uint64_t id;
    uint32_t mode;
    bool (*read)(void *, uint64_t, void *, size_t, size_t *, qa_error *);
    bool (*write)(void *, uint64_t, qa_bytes, size_t *, qa_error *);
    bool (*size)(void *, uint64_t *, qa_error *);
    bool (*truncate)(void *, uint64_t, qa_error *);
    bool (*flush)(void *, qa_error *);
    bool (*close)(void *, qa_error *);
    void *context;
} qa_native_windows_file;
typedef struct qa_native_windows_stream {
    uint64_t id;
    bool (*read)(void *, void *, size_t, size_t *, qa_error *);
    bool (*write)(void *, qa_bytes, qa_error *);
    void *context;
} qa_native_windows_stream;
/* The enclosing resource graph supplies actual entropy, clock and file
 * authority. No default host clock, filesystem or standard stream is invented.
 * opened=true transfers close ownership even when open_file returns false.
 * resolve_file borrows an existing durable capability without opening it. */
typedef struct qa_native_windows_capabilities {
    uint64_t id;
    bool (*entropy)(void *, void *, size_t, qa_error *);
    bool (*milliseconds)(void *, int64_t *, qa_error *);
    bool (*performance)(void *, int64_t *, qa_error *);
    int64_t performance_frequency;
    bool (*calendar)(void *, int64_t, bool, qa_native_windows_calendar *, qa_error *);
    bool (*open_file)(void *, const char *, uint32_t, uint32_t,
        qa_native_windows_file *, bool *, qa_error *);
    bool (*resolve_file)(void *, uint64_t, qa_native_windows_file *, qa_error *);
    qa_native_windows_stream streams[3];
    bool (*current)(void *, qa_error *);
    void *context;
} qa_native_windows_capabilities;
typedef struct qa_native_windows_process_options {
    qa_native_guest_options guest;
    const qa_native_windows_artifact *artifacts;
    size_t artifact_count;
    uint64_t primary_image; /* Selected actual source image, independent of artifact order. */
    /* Full page-aligned stack extent. HOST calls have budget zero; emulated
     * calls require a positive instruction budget. Backend identity does not
     * admit an artifact's instruction/syscall profile. */
    size_t stack_bytes, instruction_budget;
    uint32_t process_id, thread_id;
    bool has_process_id, has_thread_id;
    const uint16_t *command_line, *environment;
    size_t command_line_units, environment_units;
    qa_native_windows_capabilities capabilities;
} qa_native_windows_process_options;

/* Artifact order is retained. The complete graph is admitted before any image
 * binds imports, including later providers and dependency cycles. This
 * constructor attaches real PE pages and prepares IAT/TLS/CFG runtime state;
 * source TLS callbacks and DLL entry points run only through explicit lifecycle
 * operations. Failure retains the complete partial owner for checked disposal. */
bool qa_native_windows_process_create(const qa_native_windows_process_options *,
    qa_native_windows_process **, qa_error *);
bool qa_native_windows_process_idle(const qa_native_windows_process *);
qa_native_guest *qa_native_windows_process_guest(const qa_native_windows_process *);
/* Copies the actual retained inert descriptor; bytes/path borrow the owner. */
bool qa_native_windows_process_artifact_read(const qa_native_windows_process *, uint64_t,
    qa_native_windows_artifact *, qa_error *);
bool qa_native_windows_process_initialize(qa_native_windows_process *, uint64_t, qa_error *);
/* Genuine DLL detach, new source image backing/imports/CFG and process attach.
 * The actual process, capability graph and static TLS slot stay owned here. */
bool qa_native_windows_process_reload(qa_native_windows_process *, uint64_t, qa_error *);
bool qa_native_windows_process_finalize(qa_native_windows_process *, uint64_t, qa_error *);
bool qa_native_windows_process_finalize_all(qa_native_windows_process *, qa_error *);
/* Registers an already acquired capability for the exact source fopen name.
 * Close/removal refuse a capability still referenced by a live CRT FILE. */
bool qa_native_windows_process_file_add(qa_native_windows_process *, const char *, uint32_t,
    const qa_native_windows_file *, qa_error *);
bool qa_native_windows_process_file_close(qa_native_windows_process *, uint64_t, qa_error *);
bool qa_native_windows_process_file_remove(qa_native_windows_process *, uint64_t, qa_error *);
/* Fixed stock/POD native signature; i386 uses its declared stock C convention.
 * API-specific stdcall/fastcall/thiscall bridges require their typed ABI plan. */
bool qa_native_windows_process_invoke(qa_native_windows_process *, uint64_t,
    const qa_native_signature *, const qa_native_value *, size_t,
    qa_native_value *, qa_error *);
bool qa_native_windows_process_invoke_original(qa_native_windows_process *, uint64_t, uint64_t,
    const qa_native_signature *, const qa_native_value *, size_t, qa_native_value *, qa_error *);
bool qa_native_windows_process_export(qa_native_windows_process *,
    const char *, const char *, uint64_t *, qa_error *);
/* No source finalizer is called. A refused file or lower CPU close retains
 * every remaining callback context and owned artifact for a checked retry. */
bool qa_native_windows_process_dispose(qa_native_windows_process **, qa_error *);

#endif
