#ifndef QA_NATIVE_GUEST_PE_H
#define QA_NATIVE_GUEST_PE_H

#include "qa/native_guest.h"

typedef struct guest_pe guest_pe;
typedef struct guest_pe_directory { uint32_t rva, bytes; } guest_pe_directory;
typedef struct guest_pe_section {
    char name[9];
    uint32_t rva, bytes, virtual_bytes, file_offset, file_bytes, flags, permissions;
} guest_pe_section;
typedef struct guest_pe_import {
    const char *library, *name; /* Borrowed from the owned inert image. */
    uint16_t ordinal, hint;
    uint32_t slot, descriptor, module_handle, bound_slots, unload_slots;
    bool by_ordinal, delayed;
} guest_pe_import;
typedef struct guest_pe_export {
    const char *name, *forwarder;
    uint32_t ordinal, rva;
} guest_pe_export;
typedef struct guest_pe_tls {
    bool present;
    uint32_t index, zero_bytes, alignment;
    qa_bytes initialized;
    const uint64_t *callbacks;
    size_t callback_count;
} guest_pe_tls;
typedef struct guest_pe_unwind {
    uint32_t begin, end, information, handler, handler_data;
    uint8_t version, flags;
    size_t chained; /* SIZE_MAX when this record has no chain. */
    qa_bytes metadata;
} guest_pe_unwind;
typedef struct guest_pe_view {
    qa_native_image_info image;
    uint64_t base;
    uint32_t entry, header_bytes, section_alignment, file_alignment;
    uint16_t characteristics, dll_characteristics;
    guest_pe_directory directories[16];
    const guest_pe_section *sections;
    size_t section_count;
    const guest_pe_import *imports;
    size_t import_count;
    const guest_pe_export *exports;
    size_t export_count;
    guest_pe_tls tls;
    const guest_pe_unwind *unwind;
    const size_t *functions; /* Top-level .pdata entries index unwind records. */
    size_t unwind_count, function_count;
    qa_bytes load_configuration;
    uint32_t cookie, guard_check, guard_dispatch, guard_flags;
    qa_bytes artifact, bytes;
} guest_pe_view;

/* Reinspect the actual artifact, own its file and virtual bytes, apply genuine
 * relocation entries, and decode lifecycle/import metadata. This object is
 * inert: imports are not bound, TLS is not allocated, and no initializer runs.
 * The enclosing runtime must own these services before attaching execution. */
bool guest_pe_open(qa_bytes, const qa_native_image_info *, uint64_t, size_t,
    guest_pe **, qa_error *);
void guest_pe_close(guest_pe **);
const guest_pe_view *guest_pe_describe(const guest_pe *);
bool guest_pe_range(const guest_pe *, uint32_t, size_t, qa_bytes *, qa_error *);

#endif
