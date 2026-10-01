#ifndef QA_UNICORN_MAP_INTERNAL_H
#define QA_UNICORN_MAP_INTERNAL_H

/* Included only inside the pinned x86_64-softmmu memory and exec units, after
 * their real private types. No dependency structure layout is copied here. */
AddressSpaceDispatch *qa_unicorn_x86_dispatch(struct uc_struct *, FlatView *,
    const MemoryRegionSection *, size_t);
RAMBlock *qa_unicorn_x86_ram_ptr(struct uc_struct *, ram_addr_t, void *, MemoryRegion *);
MemoryRegion *qa_unicorn_x86_map_ptr(struct uc_struct *, hwaddr, size_t, uint32_t, void *);
uc_err qa_unicorn_x86_change(struct uc_struct *, MemoryRegion *, uint32_t, bool);
void qa_unicorn_x86_release(struct uc_struct *);

#endif
