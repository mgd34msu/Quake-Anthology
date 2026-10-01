/* Text-include in pinned uc.c after mem_map_check, x86-only configured build. */
#include "unicorn_state.h"
#include <limits.h>

extern MemoryRegion *qa_unicorn_x86_map_ptr(struct uc_struct *, hwaddr, size_t, uint32_t, void *);
extern uc_err qa_unicorn_x86_change(struct uc_struct *, MemoryRegion *, uint32_t, bool);
extern void qa_unicorn_x86_release(struct uc_struct *);

UNICORN_EXPORT unsigned qa_unicorn_map_revision(void)
{ return QA_UNICORN_MAP_REVISION; }

UNICORN_EXPORT uc_err qa_unicorn_memory_bind(uc_engine *uc)
{
    if (!uc || !uc->init_done || uc->arch != UC_ARCH_X86 || uc->snapshot_level ||
        uc->mapped_block_count || !QTAILQ_EMPTY(&uc->system_memory->subregions)) return UC_ERR_ARG;
    uc->memory_map_ptr = qa_unicorn_x86_map_ptr;
    return UC_ERR_OK;
}

UNICORN_EXPORT bool qa_unicorn_memory_owned(const uc_engine *uc)
{ return uc && uc->init_done && uc->memory_map_ptr == qa_unicorn_x86_map_ptr && !uc->snapshot_level; }

UNICORN_EXPORT uc_err qa_unicorn_memory_map(uc_engine *uc, uint64_t address,
    size_t bytes, uint32_t permissions, void *data)
{
    if (!qa_unicorn_memory_owned(uc) || !data) return UC_ERR_ARG;
    save_jit_state(uc);
    uc_err status = mem_map_check(uc, address, bytes, permissions);
    if (status) { restore_jit_state(uc); return status; }
    if (uc->mapped_block_count >= INT_MAX ||
        (size_t)uc->mapped_block_count + MEM_BLOCK_INCR > SIZE_MAX / sizeof(*uc->mapped_blocks)) {
        restore_jit_state(uc); return UC_ERR_NOMEM;
    }
    if (!(uc->mapped_block_count & (MEM_BLOCK_INCR - 1))) {
        size_t count = (size_t)uc->mapped_block_count + MEM_BLOCK_INCR;
        MemoryRegion **records = realloc(uc->mapped_blocks, count * sizeof(*records));
        if (!records) { restore_jit_state(uc); return UC_ERR_NOMEM; }
        uc->mapped_blocks = records;
    }
    MemoryRegion *ram = qa_unicorn_x86_map_ptr(uc, address, bytes, permissions, data);
    if (!ram) { restore_jit_state(uc); return UC_ERR_NOMEM; }
    int at = bsearch_mapped_blocks(uc, address);
    memmove(uc->mapped_blocks + at + 1, uc->mapped_blocks + at,
        ((size_t)uc->mapped_block_count - (size_t)at) * sizeof(*uc->mapped_blocks));
    uc->mapped_blocks[at] = ram; ++uc->mapped_block_count;
    restore_jit_state(uc);
    return UC_ERR_OK;
}

UNICORN_EXPORT uc_err qa_unicorn_memory_change(uc_engine *uc, uint64_t address,
    size_t bytes, uint32_t permissions, bool remove)
{
    if (!qa_unicorn_memory_owned(uc) || permissions > UC_PROT_ALL) return UC_ERR_ARG;
    int at = bsearch_mapped_blocks(uc, address);
    if (at >= (int)uc->mapped_block_count) return UC_ERR_MAP;
    MemoryRegion *ram = uc->mapped_blocks[at];
    if (ram->addr != address || int128_get64(ram->size) != bytes) return UC_ERR_ARG;
    save_jit_state(uc);
    uc_err status = qa_unicorn_x86_change(uc, ram, permissions, remove);
    if (!status && remove) {
        --uc->mapped_block_count;
        memmove(uc->mapped_blocks + at, uc->mapped_blocks + at + 1,
            ((size_t)uc->mapped_block_count - (size_t)at) * sizeof(*uc->mapped_blocks));
    }
    restore_jit_state(uc);
    return status;
}

UNICORN_EXPORT uc_err qa_unicorn_memory_release(uc_engine *uc)
{
    if (!qa_unicorn_memory_owned(uc)) return UC_ERR_OK;
    save_jit_state(uc);
    qa_unicorn_x86_release(uc);
    restore_jit_state(uc);
    return UC_ERR_OK;
}
