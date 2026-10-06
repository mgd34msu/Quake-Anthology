#ifndef QA_NATIVE_GUEST_WINDOWS_RUNTIME_H
#define QA_NATIVE_GUEST_WINDOWS_RUNTIME_H

#include "pe.h"
#include "runtime_import.h"
#include "runtime_resource.h"
#include "qa/native_windows_locale.h"

typedef struct guest_windows guest_windows;
typedef struct guest_windows_kernel guest_windows_kernel;
typedef struct guest_windows_crt guest_windows_crt;
typedef struct guest_windows_msvc guest_windows_msvc;

#define GUEST_WINDOWS_CALLBACK_MINIMUM UINT64_C(0x57494e0000000000)

typedef struct guest_windows_calendar {
    int32_t year, month, weekday, day, hour, minute, second, millisecond;
    int32_t year_day, daylight, timezone_minutes;
} guest_windows_calendar;
/* Borrowed process streams have no Windows-owned close or seek operation. */
typedef struct guest_windows_stream_capability {
    uint64_t id;
    bool (*read)(void *, void *, size_t, size_t *, qa_error *);
    bool (*write)(void *, qa_bytes, qa_error *);
    void *context;
} guest_windows_stream_capability;
/* These are the genuine process capabilities. A cold owner borrows the same
 * capabilities after all records decode; it never reopens an existing file. */
typedef struct guest_windows_capabilities {
    uint64_t id;
    qa_native_windows_locale_profile locale;
    /* A input units are literal bytes; W units are UTF-16. Counts exclude an
     * automatically consumed terminator. A source failure returns true with
     * result=0 and its real Win32 error; capability failure returns false. */
    bool (*compare_string)(void *, uint32_t, uint32_t, bool, const uint16_t *, size_t,
        const uint16_t *, size_t, int32_t *, uint32_t *, qa_error *);
    bool (*entropy)(void *, void *, size_t, qa_error *);
    bool (*milliseconds)(void *, int64_t *, qa_error *);
    bool (*performance)(void *, int64_t *, qa_error *);
    int64_t performance_frequency;
    bool (*calendar)(void *, int64_t, bool, guest_windows_calendar *, qa_error *);
    /* opened=true transfers this actual capability's close ownership, even
     * when the callback reports failure after opening it. */
    bool (*open_file)(void *, const char *, uint32_t, uint32_t,
        guest_runtime_file_capability *, bool *, qa_error *);
    guest_runtime_file_resolve_fn resolve_file;
    guest_windows_stream_capability standard_input, standard_output, standard_error;
    void *context;
} guest_windows_capabilities;
typedef struct guest_windows_options {
    qa_native_guest *guest; /* Borrowed; the enclosing image owner destroys it. */
    guest_windows_capabilities capabilities;
    uint32_t process_id, thread_id;
    bool has_process_id, has_thread_id;
    const uint16_t *command_line; size_t command_line_units;
    /* Ordered UTF-16 key=value strings separated by NUL, without the final
     * extra NUL. NULL/zero is the actual empty process environment. */
    const uint16_t *environment; size_t environment_units;
    uint64_t stack_base; size_t stack_bytes;
    /* Positive for emulated calls; exactly zero for qualified hardware. */
    size_t instruction_budget;
    uint64_t primary_image; /* Actual source artifact ID, independent of graph order. */
} guest_windows_options;
typedef struct guest_windows_image {
    uint64_t id;
    const guest_pe *image; /* Actual inert artifact retained by outer loader. */
    const char *path;
} guest_windows_image;
typedef bool (*guest_windows_image_resolve_fn)(void *, uint64_t,
    uint64_t, guest_windows_image *, qa_error *);

/* A failed fresh construction retains any partial owner in the output. Check
 * file retirement while the lower guest is retained, then destroy that guest
 * before abandoning a terminal partial owner. */
bool guest_windows_create(const guest_windows_options *, guest_windows **, qa_error *);
/* Fresh admission owns the complete ordered immutable graph before any image
 * binds imports or publishes TLS/CFG state. It performs no guest writes. */
bool guest_windows_inventory(guest_windows *, const guest_windows_image *, size_t, qa_error *);
/* Fresh prepare binds the actual attached image, allocates its static TLS and
 * installs CFG targets. It never invokes a CRT initializer list separately.
 * A failure after mutation requires whole lower teardown; no Init replay. */
bool guest_windows_prepare(guest_windows *, const guest_windows_image *, qa_error *);
bool guest_windows_initialize(guest_windows *, uint64_t, size_t, qa_error *);
bool guest_windows_finalize(guest_windows *, uint64_t, size_t, qa_error *);
/* Drop the finalized module's preparation/CFG receipt. Its process-owned TLS
 * slot is retained and repopulated by the actual next prepare. */
bool guest_windows_reload_begin(guest_windows *, uint64_t, qa_error *);
bool guest_windows_idle(const guest_windows *);
bool guest_windows_destroy(guest_windows **, qa_error *);
/* Retires all real file close ownership while the lower owner is still retained
 * and stopped, including a terminal CPU. Refusals retain their exact capability
 * for retry. A failure-only pending open never becomes a healthy checkpoint. */
bool guest_windows_close_files(guest_windows *, qa_error *);
/* Only after checked file retirement and successful lower guest destruction,
 * including terminal failures. A provisional cold candidate owns no files. */
void guest_windows_abandon(guest_windows **);
guest_runtime_imports *guest_windows_imports(guest_windows *);
guest_runtime_resources *guest_windows_resources(guest_windows *);
bool guest_windows_resolve(guest_windows *, const char *, const char *, uint64_t *, qa_error *);
bool guest_windows_coverage_count(const guest_windows *, size_t *, qa_error *);
bool guest_windows_coverage_at(const guest_windows *, size_t,
    guest_runtime_import_view *, bool *, qa_error *);
bool guest_windows_checkpoint(const guest_windows *, qa_buffer *, qa_error *);
/* Decode all runtime/import/resource records without a lower guest, constructor,
 * allocation or source call. Use callback for the lower guest cold resolver;
 * attach validates actual RAM/CPU/image/capability addresses before adoption. */
bool guest_windows_decode(qa_bytes, guest_windows_image_resolve_fn, void *,
    guest_windows **, qa_error *);
bool guest_windows_callback(void *, uint64_t, uint64_t,
    qa_native_guest_callback *, qa_error *);
bool guest_windows_attach(guest_windows *, qa_native_guest *,
    const guest_windows_capabilities *, qa_error *);
/* Failed attach retains contexts leased by the lower restore candidate. Destroy
 * that candidate before abandoning this owner; do not retry fresh preparation. */
/* Final no-I/O publication transfers actual file close ownership. A previous
 * runtime remains inert after transfer; its guest lifetime is caller-owned. */
bool guest_windows_adopt(guest_windows *, guest_windows *, qa_error *);
bool guest_windows_discard(guest_windows **, qa_error *);

/* Shared implementation records. Every pointer below has one runtime owner;
 * addresses are genuine guest identities and are serialized as typed fields. */
typedef struct windows_service {
    guest_windows *owner;
    guest_runtime_function function;
    guest_abi_layout parameters[16];
    uint32_t operation;
    uint8_t family;
    bool supported;
    char *library, *name;
} windows_service;
typedef struct windows_heap_allocation { uint64_t address, heap, requested; } windows_heap_allocation;
typedef struct windows_library { char *name; uint64_t handle, references; } windows_library;
typedef struct windows_image_record {
    uint64_t id, base, tls_block, references, preferred_base, image_bytes;
    uint32_t tls_index;
    char *path;
    const guest_pe *image;
    bool prepared, initialized;
} windows_image_record;
struct guest_windows {
    qa_native_guest *guest;
    qa_native_target target;
    qa_native_guest_backend execution;
    guest_windows_capabilities capabilities;
    uint64_t capability_id, stream_ids[3], teb, peb, static_tls, return_trap, primary_image;
    uint64_t trap_cursor, trap_end;
    uint64_t stack_base, stack_bytes;
    uint32_t process_id, thread_id, static_tls_count;
    size_t instruction_budget;
    guest_runtime_imports *imports;
    guest_runtime_resources *resources;
    windows_service **services; size_t service_count, service_capacity;
    char **requested; size_t requested_count, requested_capacity;
    windows_library *libraries; size_t library_count, library_capacity;
    windows_image_record *images; size_t image_count, image_capacity;
    windows_heap_allocation *allocations; size_t allocation_count, allocation_capacity;
    uint64_t *cfg_targets; size_t cfg_count, cfg_capacity;
    uint64_t *prepared_ids, *initialized_ids;
    size_t prepared_count, prepared_capacity, initialized_count, initialized_capacity;
    uint64_t cfg_check, cfg_dispatch;
    guest_windows_kernel *kernel;
    guest_windows_crt *crt;
    guest_windows_msvc *msvc;
    unsigned busy;
    bool constructed, detached, publication_pending, retired, retiring, files_closed;
};
bool windows_read(guest_windows *, uint64_t, size_t, uint64_t *, qa_error *);
bool windows_write(guest_windows *, uint64_t, size_t, uint64_t, qa_error *);
bool windows_zero(guest_windows *, uint64_t, size_t, qa_error *);
bool windows_string(guest_windows *, uint64_t, bool, uint16_t **, size_t *, qa_error *);
bool windows_store_string(guest_windows *, const uint16_t *, size_t, bool, uint64_t *, qa_error *);
bool windows_storage(guest_windows *, size_t, uint64_t *, qa_error *);
bool windows_allocate(guest_windows *, size_t, uint64_t, uint64_t *, qa_error *);
bool windows_free(guest_windows *, uint64_t, uint64_t, qa_error *);
bool windows_allocation_size(guest_windows *, uint64_t, uint64_t, uint64_t *);
bool windows_last_error(guest_windows *, uint32_t, qa_error *);
bool windows_invoke(guest_windows *, uint64_t, const qa_native_value_type *, size_t,
    qa_native_value_type, const qa_native_value *, bool, qa_native_value *, qa_error *);
bool windows_service_add(guest_windows *, uint64_t, uint8_t, uint32_t,
    const char *, const char *, const qa_native_value_type *, size_t,
    qa_native_value_type, guest_abi_convention, bool, qa_error *);
bool windows_library_handle(guest_windows *, const char *, uint64_t *, qa_error *);
bool windows_load_library(guest_windows *, const char *, uint64_t *, qa_error *);
bool windows_free_library(guest_windows *, uint64_t, bool *, qa_error *);
bool windows_destroy_heap(guest_windows *, uint64_t, qa_error *);
const char *windows_library_name(const guest_windows *, uint64_t);
const windows_image_record *windows_image_at(const guest_windows *, uint64_t);
bool windows_validate_storage(guest_windows *, uint64_t, size_t, int32_t, qa_error *);

#endif
