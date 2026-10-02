#ifndef QA_NATIVE_GUEST_ELF_H
#define QA_NATIVE_GUEST_ELF_H

#include "qa/native_guest.h"

typedef struct guest_elf guest_elf;
typedef enum guest_elf_role { GUEST_ELF_LIBRARY, GUEST_ELF_PROGRAM } guest_elf_role;
typedef struct guest_elf_segment {
    uint32_t type, flags;
    uint64_t offset, address, file_bytes, memory_bytes, alignment;
} guest_elf_segment;
typedef struct guest_elf_section {
    const char *name;
    uint32_t type, link, info;
    uint64_t flags, address, offset, bytes, alignment, entry_bytes;
} guest_elf_section;
typedef struct guest_elf_dynamic { uint64_t tag, value; } guest_elf_dynamic;
typedef struct guest_elf_version {
    const char *name, *library;
    uint16_t index;
    bool weak;
} guest_elf_version;
typedef struct guest_elf_symbol {
    const char *name;
    uint64_t value, bytes;
    uint32_t section;
    uint8_t binding, type, visibility;
    const guest_elf_version *version;
    bool hidden;
} guest_elf_symbol;
typedef enum guest_elf_relocation_table {
    GUEST_ELF_REL, GUEST_ELF_RELA, GUEST_ELF_PLT_REL, GUEST_ELF_PLT_RELA, GUEST_ELF_RELR
} guest_elf_relocation_table;
typedef struct guest_elf_relocation {
    uint64_t address, addend_bits;
    uint32_t type, symbol;
    guest_elf_relocation_table table;
    bool explicit_addend;
    uint64_t record; /* Actual file-backed virtual record, including overlapping PLT tables. */
} guest_elf_relocation;
typedef struct guest_elf_view {
    qa_native_image_info image;
    uint64_t bias, first, end, entry;
    bool executable;
    guest_elf_role role;
    const guest_elf_segment *segments;
    size_t segment_count;
    const guest_elf_section *sections;
    size_t section_count;
    const guest_elf_dynamic *dynamic;
    size_t dynamic_count;
    const char *const *needed;
    size_t needed_count;
    const char *soname, *rpath, *runpath, *interpreter;
    const guest_elf_version *versions;
    size_t version_count;
    const guest_elf_symbol *symbols;
    size_t symbol_count;
    const guest_elf_relocation *relocations;
    size_t relocation_count;
    const guest_elf_segment *tls;
    qa_bytes artifact, bytes;
} guest_elf_view;

/* Own actual file and role-qualified PT_LOAD page bytes; decode binding/lifecycle metadata
 * without resolving imports, IFUNCs, TLS, or calling source initializers.
 * A later runtime binds relocations before indexing relocated unwind/arrays.
 * Unknown metadata tags/types remain visible for that actual runtime policy.
 * Library pages follow the actual glibc mapper; program pages follow the kernel
 * ELF loader, including their different partial BSS/file-page tails. Role comes
 * from the actual library/program caller. No OS/CPU mapping is attached here. */
bool guest_elf_open(qa_bytes, const qa_native_image_info *, guest_elf_role, uint64_t, size_t,
    guest_elf **, qa_error *);
void guest_elf_close(guest_elf **);
const guest_elf_view *guest_elf_describe(const guest_elf *);
bool guest_elf_file_range(const guest_elf *, uint64_t, size_t, qa_bytes *, qa_error *);
bool guest_elf_range(const guest_elf *, uint64_t, size_t, qa_bytes *, qa_error *);
bool guest_elf_dynamic_value(const guest_elf *, uint64_t, uint64_t *);

#endif
