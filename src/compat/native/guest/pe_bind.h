#ifndef QA_NATIVE_GUEST_PE_BIND_H
#define QA_NATIVE_GUEST_PE_BIND_H

#include "pe.h"

typedef struct guest_pe_resolution {
    uint64_t address, module_handle;
    /* The actual delay-load lifecycle chooses whether to publish its handle.
     * Eager IAT preparation does not imply a delay-helper notification. */
    bool publish_module_handle;
} guest_pe_resolution;
typedef bool (*guest_pe_import_resolve_fn)(void *, const guest_pe_view *,
    const guest_pe_import *, guest_pe_resolution *, qa_error *);
typedef bool (*guest_pe_library_fn)(void *, const char *, const guest_pe *,
    const guest_pe **, qa_error *);

/* Resolve every actual provider before touching IAT or page rights. The
 * resolver owns library policy, callbacks and reference counts. Binding never
 * calls an initializer. Failure after mutation retains terminal CPU/RAM and
 * requires whole-process destruction, including rejected protection restore. */
bool guest_pe_bind_imports(const guest_pe *, qa_native_guest *,
    guest_pe_import_resolve_fn, void *, qa_error *);
/* Cold restore retains the actual saved IAT bytes. Source code can replace an
 * IAT target or its page rights, so this fresh binder is not a cold validator. */
/* Forwarders use caller-owned actual library inventory; they never load code.
 * Result addresses are checked against the final image's real owned metadata. */
bool guest_pe_resolve_export(const guest_pe *, const qa_native_guest *, const char *, uint32_t, bool,
    guest_pe_library_fn, void *, const guest_pe **, uint64_t *, qa_error *);

#endif
