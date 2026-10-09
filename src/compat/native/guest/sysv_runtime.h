#ifndef QA_NATIVE_GUEST_SYSV_RUNTIME_H
#define QA_NATIVE_GUEST_SYSV_RUNTIME_H

#include "runtime_import.h"
#include "runtime_resource.h"
#include "qa/platform_services.h"

typedef struct guest_sysv_runtime guest_sysv_runtime;
typedef struct guest_sysv_load guest_sysv_load;
/* Standard streams are already acquired, borrowed external capabilities whose
 * owners outlive this runtime. Completion is reported even when a callback
 * fails after transferring an actual prefix. */
typedef struct guest_sysv_stream {
    uint64_t id;
    bool (*read)(void *, void *, size_t, size_t *, qa_error *);
    bool (*write)(void *, qa_bytes, size_t *, qa_error *);
    bool (*flush)(void *, qa_error *);
    void *context;
} guest_sysv_stream;
typedef struct guest_sysv_bindings {
    uint64_t clock_id;
    bool (*time)(void *, int64_t *, qa_error *);
    bool (*random)(void *, void *, size_t, uint32_t, size_t *, int32_t *, qa_error *);
    bool (*clock)(void *, int32_t, int64_t *, int32_t *, qa_error *);
    bool (*calendar)(void *, int64_t, bool, qa_platform_calendar_fields *, qa_error *);
    bool (*getcwd)(void *, qa_buffer *, qa_error *);
    guest_sysv_stream streams[3];
    /* Borrowed process-owned registry of actually opened file capabilities. */
    guest_runtime_resources *resources;
    /* Opens under acquired directory authority and registers the returned
     * handle in resources before returning. opened retains cleanup on error. */
    bool (*open_file)(void *, const char *, uint32_t, uint32_t,
        uint64_t *, bool *, qa_error *);
    bool (*open_temporary_file)(void *, uint64_t *, bool *, qa_error *);
    bool output_is_terminal;
    bool (*current)(void *, const qa_native_guest *, qa_error *);
    void *context;
} guest_sysv_bindings;
typedef struct guest_sysv_options {
    uint64_t scope, first_function, trap_base, return_trap;
    /* Positive instruction budget for EMULATED; exactly zero for HOST.
     * Runtime retains the actual lower backend as immutable identity. */
    size_t trap_bytes, instruction_budget;
    const char *const *argv, *const *environment;
    size_t argc, environment_count;
    guest_sysv_bindings bindings;
} guest_sysv_options;
typedef enum guest_sysv_lifecycle {
    GUEST_SYSV_LOADED, GUEST_SYSV_INITIALIZING, GUEST_SYSV_INITIALIZED,
    GUEST_SYSV_FINALIZING, GUEST_SYSV_FINALIZED, GUEST_SYSV_FAILED
} guest_sysv_lifecycle;
typedef struct guest_sysv_export {
    const char *name, *version;
    uint64_t address, bytes, tls_offset;
    bool tls;
} guest_sysv_export;
/* Supplied by the actual ELF loader AFTER relocation. Runtime copies these
 * ordered records; it never reinterprets an unrelocated ELF symbol or array. */
typedef struct guest_sysv_provider {
    uint64_t id;
    qa_native_image_info image;
    const char *soname;
    const guest_sysv_export *exports;
    size_t export_count;
    const char *const *needed;
    size_t needed_count;
    const uint64_t *preinitializers, *initializers, *finalizers;
    size_t preinitializer_count, initializer_count, finalizer_count;
} guest_sysv_provider;
typedef struct guest_sysv_tls {
    uint64_t module_id, address, bytes;
    int64_t thread_pointer_offset;
} guest_sysv_tls;
typedef struct guest_sysv_provider_tls {
    guest_sysv_tls block;
    uint64_t prior_used, prior_module;
} guest_sysv_provider_tls;
typedef struct guest_sysv_symbol {
    bool present, host, tls, size_known;
    uint64_t provider, address, bytes, offset;
    guest_sysv_tls block;
} guest_sysv_symbol;
typedef struct guest_sysv_trace {
    uint64_t provider, target;
    bool finalize;
} guest_sysv_trace;

/* Fresh construction installs services and allocates process state exactly
 * once. Failure may retain an owner until the whole lower guest is retired. */
bool guest_sysv_create(qa_native_guest *, const guest_sysv_options *,
    guest_sysv_runtime **, qa_error *);
bool guest_sysv_idle(const guest_sysv_runtime *);
/* Unbind before destroying the lower guest. abandon releases host records only
 * and is legal after successful lower destruction, including failed owners. */
bool guest_sysv_retire(guest_sysv_runtime *, qa_error *);
void guest_sysv_abandon(guest_sysv_runtime **);
guest_runtime_imports *guest_sysv_imports(guest_sysv_runtime *);
/* Borrowed identity only; this does not qualify execution or admission. */
qa_native_guest *guest_sysv_guest(const guest_sysv_runtime *);
/* Pure retained FILE ownership, including a partially completed close. */
bool guest_sysv_file_in_use(const guest_sysv_runtime *, uint64_t handle);
/* Pure retained identity, including detached or failed owners. The borrowed
 * immutable record remains valid until provider inventory changes or abandon;
 * it neither qualifies current execution nor performs symbol resolution. */
const guest_sysv_provider *guest_sysv_provider_read(const guest_sysv_runtime *, uint64_t provider);
/* Pure committed identity, including detached/failed owners. Copies the
 * actual block and preceding committed used/module cursors. Requested TLS
 * alignment is not retained here: the loader qualifies its actual PT_TLS
 * against these receipts without recreating or inspecting mutable TLS RAM. */
bool guest_sysv_provider_tls_read(const guest_sysv_runtime *, uint64_t,
    guest_sysv_provider_tls *);
uint64_t guest_sysv_thread_pointer(const guest_sysv_runtime *);

/* Begin holds the load lease and reserves the actual next TLS identity and
 * negative TP offset. Commit copies the relocated template, publishes DTV and
 * ordered provider together. Abort retains reached RAM effects, never replays
 * loading. An image without PT_TLS does not consume a TLS module identity. */
bool guest_sysv_load_begin(guest_sysv_runtime *, uint64_t provider,
    bool has_tls, uint64_t bytes, uint64_t alignment,
    guest_sysv_load **, guest_sysv_tls *, qa_error *);
/* Replace a finalized module inside the same runtime. Its original static TLS
 * identity stays allocated; the fresh commit installs its real template again. */
bool guest_sysv_reload_begin(guest_sysv_runtime *, uint64_t provider,
    guest_sysv_load **, guest_sysv_tls *, qa_error *);
bool guest_sysv_finalize_image_destructors(guest_sysv_runtime *, uint64_t provider,
    uint64_t base, uint64_t bytes, qa_error *);
bool guest_sysv_load_commit(guest_sysv_load **,
    const guest_sysv_provider *, qa_bytes tls_template, qa_error *);
bool guest_sysv_load_abort(guest_sysv_load **, qa_error *);
/* Exact name/version equality, including NULL versus empty version. Requester
 * is excluded from ordinary lookup; TLS lookup includes every real provider.
 * Missing weak/local definitions remain absent. Missing strong names receive
 * an exact unsupported import; reserved RTTI pages become inaccessible when
 * the load lease ends. */
bool guest_sysv_resolve(guest_sysv_runtime *, uint64_t requester,
    const char *library, const char *name, const char *version,
    bool weak, bool local_definition, bool tls,
    guest_sysv_symbol *, qa_error *);
/* proposed == 0 is a pure lookup; absence returns 0 without registration. */
bool guest_sysv_unique(guest_sysv_runtime *, const char *, uint64_t proposed,
    uint64_t *, qa_error *);
bool guest_sysv_tls_address(guest_sysv_runtime *, uint64_t module,
    uint64_t offset, uint64_t *, qa_error *);
bool guest_sysv_initialize(guest_sysv_runtime *, uint64_t, size_t, qa_error *);
bool guest_sysv_finalize(guest_sysv_runtime *, uint64_t, size_t, qa_error *);
bool guest_sysv_destructor(guest_sysv_runtime *, uint64_t target,
    uint64_t argument, uint64_t dso, qa_error *);
bool guest_sysv_finalize_destructors(guest_sysv_runtime *, uint64_t, qa_error *);
bool guest_sysv_finalize_all(guest_sysv_runtime *, size_t, qa_error *);
size_t guest_sysv_trace_count(const guest_sysv_runtime *);
bool guest_sysv_trace_at(const guest_sysv_runtime *, size_t,
    guest_sysv_trace *, qa_error *);

/* Complete detached runtime decode precedes lower restore, so saved callback
 * IDs resolve against the decoded immutable descriptors. attach validates the
 * retained allocations, callback registry and external capability identities
 * while preserving the saved CPU, without invoking
 * services, constructors, stream I/O, or allocation. */
bool guest_sysv_checkpoint(const guest_sysv_runtime *, qa_buffer *, qa_error *);
bool guest_sysv_decode(qa_bytes, const qa_native_target *,
    const guest_sysv_bindings *, guest_sysv_runtime **, qa_error *);
bool guest_sysv_callback(void *, uint64_t, uint64_t,
    qa_native_guest_callback *, qa_error *);
bool guest_sysv_attach(guest_sysv_runtime *, qa_native_guest *, qa_error *);

#endif
