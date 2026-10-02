#ifndef QA_NATIVE_GUEST_PE_MEMORY_H
#define QA_NATIVE_GUEST_PE_MEMORY_H

#include "pe.h"

typedef struct guest_pe_memory guest_pe_memory;
typedef struct guest_pe_memory_view {
    qa_native_image_info image;
    uint64_t base;
    size_t bytes;
    bool flat;
    /* Initial installation anchors. Range protection can add real fragments;
     * enumerate the borrowed lower guest for its complete current inventory. */
    const uint64_t *mappings;
    size_t mapping_count;
} guest_pe_memory_view;

/* Own a fresh lower guest and install the actual PE RAM, including protected
 * gaps. The inert PE may be released after this call. Ordinary images retain
 * relocated bytes within the live section extents and source protections. Low-alignment
 * images use Windows' flat file mapping at the preferred base.
 * No imports, TLS, initializer, ABI stack or enclosing runtime are prepared.
 * A failed open returns a retained owner only if whole-guest destruction fails;
 * that owner exposes no guest and must be passed to close again. */
bool guest_pe_memory_open(const guest_pe *, const qa_native_guest_options *,
    guest_pe_memory **, qa_error *);
/* Additional libraries share the actual process guest. This attachment owns
 * its image pages only. A terminal failure retains an incomplete attachment;
 * destroy the whole lower guest before abandoning that attachment. */
bool guest_pe_memory_attach(const guest_pe *, qa_native_guest *, guest_pe_memory **, qa_error *);
bool guest_pe_memory_close(guest_pe_memory **, qa_error *);
/* Capture/adopt the actual complete image attachment. Cold adoption allocates
 * host records only and validates its retained backing fragments; it never
 * maps RAM, applies relocations, binds imports or invokes source code. The
 * primary flag transfers lower destruction responsibility to this owner. */
bool guest_pe_memory_checkpoint(const guest_pe_memory *, qa_buffer *, qa_error *);
bool guest_pe_memory_adopt(const guest_pe *, qa_native_guest *, qa_bytes, bool,
    guest_pe_memory **, qa_error *);
/* Borrowed attachments only, after successful whole lower destruction. */
void guest_pe_memory_abandon(guest_pe_memory **);
/* Borrowed: destruction remains with this owner. Mapping queries read the
 * current lower records, including later real protection changes. */
qa_native_guest *guest_pe_memory_guest(guest_pe_memory *);
const guest_pe_memory_view *guest_pe_memory_describe(const guest_pe_memory *);
bool guest_pe_memory_mapping_at(const guest_pe_memory *, size_t,
    qa_native_guest_mapping *, qa_error *);

#endif
