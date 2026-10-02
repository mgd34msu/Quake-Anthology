#ifndef QA_NATIVE_GUEST_RUNTIME_IMPORT_H
#define QA_NATIVE_GUEST_RUNTIME_IMPORT_H

#include "abi.h"

typedef struct guest_runtime_imports guest_runtime_imports;
typedef enum guest_runtime_symbol_kind {
    GUEST_RUNTIME_SYMBOL_NAME, GUEST_RUNTIME_SYMBOL_ORDINAL
} guest_runtime_symbol_kind;
typedef struct guest_runtime_import_key {
    uint64_t scope;
    const char *library;
    guest_runtime_symbol_kind kind;
    const char *name;
    uint32_t ordinal;
    const char *version; /* NULL is an unversioned symbol, distinct from "". */
} guest_runtime_import_key;

typedef bool (*guest_runtime_import_fn)(void *, qa_native_guest *,
    const qa_native_value *, size_t, qa_native_value *, qa_error *);
/* Aggregate result storage must remain owned by the service context through
 * the subsequent actual ABI return. Decoded arguments borrow this dispatch. */
/* Decode fixed arguments first. The actual API/format descriptor supplies
 * promoted extras; these layouts borrow selector storage until invoke returns. */
typedef bool (*guest_runtime_variadic_fn)(void *, const qa_native_guest *,
    const qa_native_value *, size_t, const guest_abi_layout **, size_t *, qa_error *);
typedef struct guest_runtime_function {
    uint64_t id, address;
    guest_abi_signature signature;
    uint64_t variadic_id;
    guest_runtime_variadic_fn variadic;
    guest_runtime_import_fn invoke;
    void *context;
} guest_runtime_function;
typedef bool (*guest_runtime_function_resolve_fn)(void *, uint64_t,
    guest_runtime_function *, qa_error *);

typedef enum guest_runtime_import_kind {
    GUEST_RUNTIME_IMPORT_FUNCTION, GUEST_RUNTIME_IMPORT_DATA,
    GUEST_RUNTIME_IMPORT_UNRESOLVED
} guest_runtime_import_kind;
typedef struct guest_runtime_import_view {
    guest_runtime_import_key key;
    guest_runtime_import_kind kind;
    uint64_t id, address, bytes, reached, failed;
    bool bound;
    qa_status last_failure;
    const char *last_failure_detail; /* NULL until a failed actual invocation. */
} guest_runtime_import_view;

bool guest_runtime_imports_create(qa_native_guest *, guest_runtime_imports **, qa_error *);
/* Own real RX pages filled with INT3. Function slots occupy 16 aligned bytes.
 * Physical addresses are supplied by the runtime's genuine address allocator. */
bool guest_runtime_imports_traps(guest_runtime_imports *, uint64_t, size_t, qa_error *);
/* Declarations may append at idle or within a genuine stopped import callback.
 * Lazy methods use already owned trap slots; retirement still requires idle. */
bool guest_runtime_imports_function(guest_runtime_imports *,
    const guest_runtime_import_key *, const guest_runtime_function *, qa_error *);
/* Missing strong imports have no invented ABI. This binds a real terminal
 * entry trap which fails before argument decode or any CPU/RAM change. */
bool guest_runtime_imports_unresolved(guest_runtime_imports *,
    const guest_runtime_import_key *, uint64_t, uint64_t, qa_error *);
/* Initial data publication requires readable storage. A later genuine guarded
 * reservation may lose read permission; capture/attach qualify its mapped span. */
bool guest_runtime_imports_data(guest_runtime_imports *,
    const guest_runtime_import_key *, uint64_t, size_t, qa_error *);
bool guest_runtime_imports_alias(guest_runtime_imports *,
    const guest_runtime_import_key *, uint64_t, qa_error *);
bool guest_runtime_imports_unbind(guest_runtime_imports *, uint64_t, qa_error *);
bool guest_runtime_imports_find(const guest_runtime_imports *,
    const guest_runtime_import_key *, guest_runtime_import_view *, qa_error *);
size_t guest_runtime_imports_count(const guest_runtime_imports *);
bool guest_runtime_imports_at(const guest_runtime_imports *, size_t,
    guest_runtime_import_view *, qa_error *);
bool guest_runtime_imports_idle(const guest_runtime_imports *);
/* Retire before lower guest destruction. Rejected retirement retains the owner.
 * abandon is legal only AFTER successful lower destruction, including failures. */
bool guest_runtime_imports_destroy(guest_runtime_imports **, qa_error *);
void guest_runtime_imports_abandon(guest_runtime_imports **);
bool guest_runtime_imports_checkpoint(const guest_runtime_imports *, qa_buffer *, qa_error *);
/* Complete detached decode; resolves saved immutable descriptors, never installs
 * services or allocates trap RAM. Pass callback as lower restore's resolver,
 * then attach to the same completed lower candidate before any execution.
 * A rejected lower close retains callback contexts: keep this owner until that
 * candidate is destroyed, then abandon it. Detached destroy refuses leases. */
bool guest_runtime_imports_decode(qa_bytes, const qa_native_target *,
    guest_runtime_function_resolve_fn, void *, guest_runtime_imports **, qa_error *);
bool guest_runtime_imports_callback(void *, uint64_t, uint64_t,
    qa_native_guest_callback *, qa_error *);
bool guest_runtime_imports_attach(guest_runtime_imports *, qa_native_guest *, qa_error *);

#endif
