#ifndef QA_NATIVE_GUEST_ELF_UNWIND_H
#define QA_NATIVE_GUEST_ELF_UNWIND_H

#include "elf.h"

typedef struct guest_elf_unwind guest_elf_unwind;
typedef enum guest_elf_unwind_format {
    GUEST_ELF_EH_FRAME, GUEST_ELF_DEBUG_FRAME
} guest_elf_unwind_format;
typedef struct guest_elf_unwind_region {
    uint64_t first, end, metadata_address;
    size_t cie_offset, fde_offset;
    guest_elf_unwind_format format;
    qa_bytes metadata;
} guest_elf_unwind_region;

/* Index the actual relocated allocated frame bytes, original nonallocated
 * debug bytes and stripped GNU EH header source. Full instruction/augmentation
 * bytes remain owned. This does not execute a personality or unwind a CPU.
 * The retained artifact outlives the metadata owner. */
bool guest_elf_unwind_open(const guest_elf *, const qa_native_guest *, size_t,
    guest_elf_unwind **, qa_error *);
void guest_elf_unwind_close(guest_elf_unwind **);
size_t guest_elf_unwind_count(const guest_elf_unwind *);
bool guest_elf_unwind_at(const guest_elf_unwind *, size_t,
    guest_elf_unwind_region *, qa_error *);
const guest_elf_unwind_region *guest_elf_unwind_find(const guest_elf_unwind *, uint64_t);
/* Preserve the indexed historical indirect-pointer receipts and full source
 * metadata snapshots. Cold decode checks actual artifact/provenance and parses
 * the retained bytes; it never rereads mutable guest pointer slots or executes
 * a personality, initializer, relocation or source unwinder. */
bool guest_elf_unwind_checkpoint(const guest_elf_unwind *, qa_buffer *, qa_error *);
bool guest_elf_unwind_restore(const guest_elf *, const qa_native_guest *, qa_bytes,
    size_t, guest_elf_unwind **, qa_error *);

#endif
