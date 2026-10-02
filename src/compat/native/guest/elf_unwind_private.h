#ifndef QA_NATIVE_GUEST_ELF_UNWIND_PRIVATE_H
#define QA_NATIVE_GUEST_ELF_UNWIND_PRIVATE_H

#include "internal.h"
#include "elf_unwind.h"

typedef struct unwind_block {
    uint8_t *data;
    size_t bytes, source;
    uint64_t address;
    bool debug, fallback;
} unwind_block;
struct guest_elf_unwind {
    const guest_elf *artifact;
    unwind_block *blocks;
    size_t block_count, block_capacity, owned_bytes, maximum;
    guest_elf_unwind_region *regions;
    size_t count, capacity;
    qa_buffer header;
    uint64_t header_address;
    size_t header_source;
};

bool guest_elf_unwind_index(guest_elf_unwind *, unwind_block *, const qa_native_guest *,
    const guest_elf_unwind_region *, size_t, size_t *, qa_error *);
bool guest_elf_unwind_source(const guest_elf_unwind *, const unwind_block *, qa_error *);

#endif
