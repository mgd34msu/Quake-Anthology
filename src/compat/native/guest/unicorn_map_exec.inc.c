/* Text-include at the end of pinned qemu/exec.c, in x86_64-softmmu only. */
#include "unicorn_map_internal.h"

RAMBlock *qa_unicorn_x86_ram_ptr(struct uc_struct *uc, ram_addr_t bytes,
    void *data, MemoryRegion *region)
{
    if (!bytes || !data) return NULL;
    /* Actual RAM offsets are host-width identities. Find a free bitmap-aligned
     * extent without the original allocator's overflow/abort path. */
    ram_addr_t offset = 0;
    ram_addr_t alignment = (ram_addr_t)BITS_PER_LONG << TARGET_PAGE_BITS;
    RAMBlock *block;
    for (;;) {
        if (bytes > RAM_ADDR_MAX - offset) return NULL;
        ram_addr_t next = offset;
        RAMBLOCK_FOREACH(block) {
            if (block->max_length > RAM_ADDR_MAX - block->offset) return NULL;
            ram_addr_t end = block->offset + block->max_length;
            if (offset < end && block->offset < offset + bytes && next < end) next = end;
        }
        if (next == offset) break;
        if (next > RAM_ADDR_MAX - (alignment - 1)) return NULL;
        offset = (next + alignment - 1) & ~(alignment - 1);
    }
    RAMBlock *ram = calloc(1, sizeof(*ram));
    if (!ram) return NULL;
    ram->mr = region; ram->used_length = ram->max_length = bytes;
    ram->page_size = uc->qemu_real_host_page_size; ram->host = data;
    ram->flags = RAM_PREALLOC; ram->offset = offset;
    /* Retain the actual dependency's descending-length registry and last-block
     * bookkeeping. PREALLOC leaves the backing with the lower C owner. */
    RAMBlock *last = NULL;
    RAMBLOCK_FOREACH(block) {
        last = block;
        if (block->max_length < bytes) break;
    }
    if (block) QLIST_INSERT_BEFORE_RCU(block, ram, next);
    else if (last) {
        QLIST_INSERT_AFTER_RCU(last, ram, next);
        uc->ram_list.last_block = ram;
    } else {
        QLIST_INSERT_HEAD_RCU(&uc->ram_list.blocks, ram, next);
        uc->ram_list.last_block = ram;
    }
    uc->ram_list.mru_block = NULL;
    uc->invalid_addr = 0; uc->invalid_error = UC_ERR_OK;
    cpu_physical_memory_set_dirty_range(offset, bytes, DIRTY_CLIENTS_ALL);
    return ram;
}

static PhysPageEntry *qa_page_entry(AddressSpaceDispatch *dispatch, uint32_t parent, unsigned slot)
{
    return parent == PHYS_MAP_NODE_NIL ? &dispatch->phys_map : &dispatch->map.nodes[parent][slot];
}

static bool qa_page_node(AddressSpaceDispatch *dispatch, bool leaf, uint32_t *out)
{
    PhysPageMap *map = &dispatch->map;
    if (map->nodes_nb == PHYS_MAP_NODE_NIL) return false;
    if (map->nodes_nb == map->nodes_nb_alloc) {
        size_t next = map->nodes_nb_alloc ? (size_t)map->nodes_nb_alloc * 2 : 16;
        if (next > PHYS_MAP_NODE_NIL) next = PHYS_MAP_NODE_NIL;
        if (next > SIZE_MAX / sizeof(*map->nodes)) return false;
        Node *nodes = realloc(map->nodes, next * sizeof(*nodes));
        if (!nodes) return false;
        map->nodes = nodes; map->nodes_nb_alloc = (unsigned)next;
    }
    uint32_t index = map->nodes_nb++;
    PhysPageEntry value = {.skip = leaf ? 0 : 1,
        .ptr = leaf ? PHYS_SECTION_UNASSIGNED : PHYS_MAP_NODE_NIL};
    for (unsigned i = 0; i < P_L2_SIZE; ++i) map->nodes[index][i] = value;
    *out = index;
    return true;
}

static bool qa_page_set(AddressSpaceDispatch *dispatch, uint32_t parent, unsigned slot,
    hwaddr *index, uint64_t *pages, uint16_t section, int level)
{
    PhysPageEntry entry = *qa_page_entry(dispatch, parent, slot);
    if (entry.skip && entry.ptr == PHYS_MAP_NODE_NIL) {
        uint32_t node;
        if (!qa_page_node(dispatch, level == 0, &node)) return false;
        /* Growing nodes can move every child pointer. Retain indices only. */
        qa_page_entry(dispatch, parent, slot)->ptr = node;
        entry.ptr = node;
    }
    uint32_t node = entry.ptr;
    unsigned child = (unsigned)((*index >> (level * P_L2_BITS)) & (P_L2_SIZE - 1));
    hwaddr step = (hwaddr)1 << (level * P_L2_BITS);
    while (*pages && child < P_L2_SIZE) {
        if (!(*index & (step - 1)) && *pages >= step) {
            dispatch->map.nodes[node][child] = (PhysPageEntry){.skip = 0, .ptr = section};
            *index += step; *pages -= step;
        } else if (level <= 0 || !qa_page_set(dispatch, node, child, index, pages, section, level - 1)) {
            return false;
        }
        ++child;
    }
    return true;
}

AddressSpaceDispatch *qa_unicorn_x86_dispatch(struct uc_struct *uc, FlatView *view,
    const MemoryRegionSection *sections, size_t count)
{
    /* The actual IOTLB encodes section indices in TARGET_PAGE_BITS. */
    if (count >= TARGET_PAGE_SIZE || count + 1 > SIZE_MAX / sizeof(*sections)) return NULL;
    AddressSpaceDispatch *dispatch = calloc(1, sizeof(*dispatch));
    if (!dispatch) return NULL;
    dispatch->uc = uc;
    dispatch->phys_map = (PhysPageEntry){.ptr = PHYS_MAP_NODE_NIL, .skip = 1};
    dispatch->map.sections = calloc(count + 1, sizeof(*sections));
    if (!dispatch->map.sections) { free(dispatch); return NULL; }
    dispatch->map.sections_nb_alloc = (unsigned)count + 1;
    dispatch->map.sections_nb = (unsigned)count + 1;
    dispatch->map.sections[PHYS_SECTION_UNASSIGNED] = (MemoryRegionSection){
        .mr = &uc->io_mem_unassigned, .fv = view, .size = int128_2_64()};
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        MemoryRegionSection section = sections[i];
        uint64_t bytes = int128_get64(section.size);
        if (!bytes || section.offset_within_address_space & ~TARGET_PAGE_MASK ||
            bytes & ~TARGET_PAGE_MASK || !section.mr->ram || section.mr->subpage) {
            okay = false; break;
        }
        section.fv = view;
        dispatch->map.sections[i + 1] = section;
        hwaddr index = section.offset_within_address_space >> TARGET_PAGE_BITS;
        uint64_t pages = bytes >> TARGET_PAGE_BITS;
        okay = qa_page_set(dispatch, PHYS_MAP_NODE_NIL, 0, &index, &pages,
            (uint16_t)(i + 1), P_L2_LEVELS - 1) && !pages;
    }
    if (!okay) {
        free(dispatch->map.sections); free(dispatch->map.nodes); free(dispatch);
        return NULL;
    }
    address_space_dispatch_compact(dispatch);
    return dispatch;
}
