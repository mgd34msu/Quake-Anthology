#ifndef QA_NATIVE_GUEST_ELF_MEMORY_H
#define QA_NATIVE_GUEST_ELF_MEMORY_H

#include "elf.h"

typedef struct guest_elf_memory guest_elf_memory;
typedef struct guest_elf_memory_options {
    /* Actual process policy, supplied by the Linux process owner. Library BSS
     * uses its segment rights; kernel-program anonymous BSS uses these rights. */
    uint32_t anonymous_permissions;
    bool read_implies_execute;
} guest_elf_memory_options;
typedef struct guest_elf_memory_view {
    qa_native_image_info image;
    guest_elf_role role;
    uint64_t base;
    size_t bytes;
} guest_elf_memory_view;

/* Attach fresh final page contents before any source execution. ET_DYN library
 * gaps retain the actual initial file mapping, including protected EOF pages.
 * Program gaps remain unmapped. File tails and anonymous BSS follow the actual
 * role-qualified source; no file fault is replaced by accessible zero RAM.
 * A postmutation failure retains an owner for whole-process teardown. The
 * actual retained inert ELF outlives this owner, including cold adoption. */
bool guest_elf_memory_attach(const guest_elf *, qa_native_guest *,
    const guest_elf_memory_options *, guest_elf_memory **, qa_error *);
/* Fresh relocation write lease, followed by actual final page rights and
 * RELRO. Cold adoption never acquires this lease or rewrites saved rights. */
bool guest_elf_memory_write_begin(guest_elf_memory *, qa_error *);
bool guest_elf_memory_finish(guest_elf_memory *, qa_error *);
/* Kernel program/interpreter attachment: no userspace linker has run yet.
 * Seal raw page ownership for capture without relocation, RELRO or RAM writes. */
bool guest_elf_memory_program_ready(const guest_elf_memory *, qa_error *);
bool guest_elf_memory_program_seal(guest_elf_memory *, qa_error *);
/* Records a successful kernel-owned removal/replacement of raw PROGRAM pages.
 * The original artifact/backing receipt remains historical; the lower current
 * mapping inventory becomes authoritative for surviving aliases and pages. */
bool guest_elf_memory_program_changed(guest_elf_memory *, uint64_t, size_t, qa_error *);
bool guest_elf_memory_checkpoint(const guest_elf_memory *, qa_buffer *, qa_error *);
/* Restore host ownership records only against actual already-restored RAM,
 * backing provenance and source artifact. Saved mutable bytes/rights win. */
bool guest_elf_memory_adopt(const guest_elf *, qa_native_guest *, qa_bytes,
    guest_elf_memory **, qa_error *);
bool guest_elf_memory_close(guest_elf_memory **, qa_error *);
void guest_elf_memory_abandon(guest_elf_memory **);
qa_native_guest *guest_elf_memory_guest(guest_elf_memory *);
const guest_elf_memory_view *guest_elf_memory_describe(const guest_elf_memory *);

#endif
