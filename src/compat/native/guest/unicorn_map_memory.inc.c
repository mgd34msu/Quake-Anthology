/* Text-include at the end of pinned qemu/softmmu/memory.c, x86_64-softmmu only.
 * This seam owns page-aligned private RAM on the lower owner's sole CPU. */
#include "unicorn_map_internal.h"

static FlatView *qa_ram_view(struct uc_struct *uc, MemoryRegion *added,
    MemoryRegion *changed, uint32_t permissions, bool remove)
{
    FlatView *prior = address_space_to_flatview(&uc->address_space_memory);
    size_t count = (size_t)prior->nr + (added ? 1 : 0);
    if (remove) {
        if (!count) return NULL;
        --count;
    }
    if (count > UINT_MAX || count > SIZE_MAX / sizeof(FlatRange) ||
        count > SIZE_MAX / sizeof(MemoryRegionSection)) return NULL;
    FlatView *view = calloc(1, sizeof(*view));
    if (!view) return NULL;
    view->ref = 1; view->root = uc->system_memory;
    view->ranges = count ? calloc(count, sizeof(*view->ranges)) : NULL;
    MemoryRegionSection *sections = count ? calloc(count, sizeof(*sections)) : NULL;
    if (count && (!view->ranges || !sections)) {
        free(sections); free(view->ranges); free(view); return NULL;
    }
    view->nr_allocated = (unsigned)count;
    size_t at = 0; bool found = !changed;
    for (unsigned i = 0; i < prior->nr; ++i) {
        FlatRange range = prior->ranges[i];
        if (range.mr == changed) {
            if (found) { free(sections); flatview_unref(view); return NULL; }
            found = true;
            if (remove) continue;
            range.readonly = !(permissions & UC_PROT_WRITE);
        }
        if (added && added->addr < int128_get64(range.addr.start)) {
            view->ranges[at++] = (FlatRange){.mr = added, .readonly = added->readonly,
                .addr = addrrange_make(int128_make64(added->addr), added->size)};
            added = NULL;
        }
        if (at >= count) { free(sections); flatview_unref(view); return NULL; }
        view->ranges[at++] = range;
    }
    if (added) view->ranges[at++] = (FlatRange){.mr = added, .readonly = added->readonly,
        .addr = addrrange_make(int128_make64(added->addr), added->size)};
    if (!found || at != count) { free(sections); flatview_unref(view); return NULL; }
    view->nr = (unsigned)count;
    for (size_t i = 0; i < count; ++i) sections[i] = section_from_flat_range(&view->ranges[i], view);
    view->dispatch = qa_unicorn_x86_dispatch(uc, view, sections, count);
    free(sections);
    if (!view->dispatch) { flatview_unref(view); return NULL; }
    return view;
}

static void qa_ram_commit(struct uc_struct *uc, FlatView *view)
{
    AddressSpace *space = &uc->address_space_memory;
    FlatView *prior = address_space_to_flatview(space);
    space->current_map = view;
    /* The sole TCG listener reloads its actual dispatcher and flushes TLBs.
     * No global topology/hash allocation occurs at the commit boundary. */
    MEMORY_LISTENER_CALL_GLOBAL(uc, commit, Forward);
    uc->tb_flush(uc);
    flatview_unref(prior);
}

MemoryRegion *qa_unicorn_x86_map_ptr(struct uc_struct *uc, hwaddr base, size_t bytes,
    uint32_t permissions, void *data)
{
    MemoryRegion *ram = calloc(1, sizeof(*ram));
    if (!ram) return NULL;
    memory_region_init(uc, ram, bytes);
    ram->ram = true; ram->terminates = true;
    ram->destructor = memory_region_destructor_ram;
    ram->ram_block = qa_unicorn_x86_ram_ptr(uc, bytes, data, ram);
    if (!ram->ram_block) { free(ram); return NULL; }
    ram->perms = permissions; ram->addr = base; ram->end = base + bytes;
    ram->readonly = !(permissions & UC_PROT_WRITE);
    FlatView *view = qa_ram_view(uc, ram, NULL, 0, false);
    if (!view) { qemu_ram_free(uc, ram->ram_block); free(ram); return NULL; }
    /* All candidate allocations succeeded. Attach actual RAM without invoking
     * the dependency's allocating topology transaction a second time. */
    ram->container = uc->system_memory;
    QTAILQ_INSERT_TAIL(&uc->system_memory->subregions, ram, subregions_link);
    qa_ram_commit(uc, view);
    return ram;
}

uc_err qa_unicorn_x86_change(struct uc_struct *uc, MemoryRegion *ram,
    uint32_t permissions, bool remove)
{
    if (!ram || !ram->ram || ram->container != uc->system_memory ||
        !QTAILQ_EMPTY(&ram->subregions)) return UC_ERR_ARG;
    FlatView *view = qa_ram_view(uc, NULL, ram, permissions, remove);
    if (!view) return UC_ERR_NOMEM;
    if (!remove) {
        ram->perms = permissions;
        ram->readonly = !(permissions & UC_PROT_WRITE);
    }
    qa_ram_commit(uc, view);
    if (remove) {
        memory_region_remove_subregion(uc->system_memory, ram);
        ram->destructor(ram); free(ram);
    }
    return UC_ERR_OK;
}

void qa_unicorn_x86_release(struct uc_struct *uc)
{
    /* Retire dispatches before any RAM metadata they reference. Cached initial
     * views retain their normal references; NULL hash destruction is supported
     * by the selected GLib implementation in the subsequent uc_close. */
    FlatView *prior = address_space_to_flatview(&uc->address_space_memory);
    flatview_ref(uc->empty_view);
    uc->address_space_memory.current_map = uc->empty_view;
    MEMORY_LISTENER_CALL_GLOBAL(uc, commit, Forward);
    g_hash_table_unref(uc->flat_views); uc->flat_views = NULL;
    flatview_unref(prior);
    MemoryRegion *ram, *next;
    QTAILQ_FOREACH_SAFE(ram, &uc->system_memory->subregions, subregions_link, next) {
        memory_region_remove_subregion(uc->system_memory, ram);
        ram->destructor(ram); free(ram);
    }
    uc->memory_region_update_pending = false;
    uc->mapped_block_count = 0;
}
