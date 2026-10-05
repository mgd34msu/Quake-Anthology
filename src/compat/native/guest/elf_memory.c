#include "internal.h"
#include "elf_memory.h"

typedef struct elf_page {
    uint64_t file_offset;
    uint32_t permissions;
    uint8_t kind; /* 0 unmapped, 1 private file, 2 anonymous. */
    bool segment;
} elf_page;
typedef struct elf_extent { uint64_t id, base, backing; size_t bytes; bool kernel_changed; } elf_extent;
struct guest_elf_memory {
    guest_elf_memory_view view;
    qa_native_guest *guest;
    elf_page *pages;
    size_t page_count, installed;
    elf_extent *extents;
    size_t extent_count;
    const guest_elf *image;
    guest_elf_memory_options options;
    bool complete, writing, finished;
};

static uint64_t down(uint64_t value)
{ return value & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1); }
static uint64_t up(uint64_t value)
{ return (value + QA_NATIVE_GUEST_PAGE - 1) & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1); }

static uint32_t rights(uint32_t flags, bool read_implies_execute)
{
    uint32_t value = (flags & 4 ? QA_NATIVE_GUEST_READ : 0) |
        (flags & 2 ? QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE : 0) |
        (flags & 1 ? QA_NATIVE_GUEST_EXECUTE : 0);
    if (read_implies_execute && (flags & 4)) value |= QA_NATIVE_GUEST_EXECUTE;
    return value;
}

static bool page_span(guest_elf_memory *owner, uint64_t begin, uint64_t end,
    uint64_t first, uint8_t kind, uint32_t permissions, uint64_t file_offset,
    bool segment, qa_error *error)
{
    if (begin < first || end < begin || end - first > owner->view.bytes ||
        begin % QA_NATIVE_GUEST_PAGE || end % QA_NATIVE_GUEST_PAGE ||
        (kind == 1 && end - begin > UINT64_MAX - file_offset))
        return guest_fail(error, QA_ERROR_FORMAT, begin, "ELF live page plan exceeds its actual source extent");
    for (uint64_t at = begin; at < end; at += QA_NATIVE_GUEST_PAGE) {
        elf_page *page = &owner->pages[(size_t)((at - first) / QA_NATIVE_GUEST_PAGE)];
        *page = (elf_page){file_offset + at - begin, permissions, kind, segment};
    }
    return true;
}

static bool prepare(guest_elf_memory *owner, const guest_elf_memory_options *options,
    bool fresh, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(owner->image);
    const guest_elf_segment *first_load = NULL, *last_load = NULL, *prior = NULL;
    bool holes = false;
    uint64_t first = image->first, end = image->end;
    for (size_t i = 0; i < image->segment_count; ++i) {
        const guest_elf_segment *segment = &image->segments[i];
        if (segment->type != 1) continue;
        if ((segment->flags & ~7u) || segment->offset < segment->address % QA_NATIVE_GUEST_PAGE ||
            segment->address + segment->memory_bytes > UINT64_MAX - (QA_NATIVE_GUEST_PAGE - 1))
            return guest_fail(error, QA_ERROR_FORMAT, segment->address, "ELF live segment cannot preserve its actual page or processor rights");
        if (prior && segment->address < prior->address)
            return guest_fail(error, QA_ERROR_FORMAT, segment->address, "ELF PT_LOAD records violate source address ordering");
        if (prior && up(prior->address + prior->file_bytes) != down(segment->address)) holes = true;
        if (!first_load) first_load = segment;
        last_load = prior = segment;
    }
    if (!first_load || !last_load)
        return guest_fail(error, QA_ERROR_FORMAT, 0, "ELF live image lacks its actual load commands");
    bool initial_file = image->role == GUEST_ELF_LIBRARY && !image->executable;
    if (initial_file && first > down(first_load->address)) first = down(first_load->address);
    if (initial_file && end < up(last_load->address + last_load->memory_bytes))
        end = up(last_load->address + last_load->memory_bytes);
    if (end <= first || end - first > SIZE_MAX || first > UINT64_MAX - image->bias ||
        end > UINT64_MAX - image->bias ||
        (image->image.target.pointer_bytes == 4 && end + image->bias > UINT64_C(0x100000000)))
        return guest_fail(error, QA_ERROR_MEMORY, first, "ELF live page inventory exceeds its actual address domain");
    owner->view = (guest_elf_memory_view){image->image, image->role,
        image->bias + first, (size_t)(end - first)};
    owner->page_count = owner->view.bytes / QA_NATIVE_GUEST_PAGE;
    if (owner->page_count > SIZE_MAX / sizeof(*owner->pages) ||
        owner->page_count > SIZE_MAX / sizeof(*owner->extents))
        return guest_fail(error, QA_ERROR_MEMORY, first, "ELF live page inventory allocation overflows");
    owner->pages = calloc(owner->page_count, sizeof(*owner->pages));
    owner->extents = calloc(owner->page_count, sizeof(*owner->extents));
    if (!owner->pages || !owner->extents)
        return guest_fail(error, QA_ERROR_MEMORY, first, "owning ELF live page inventory");
    if (initial_file) {
        uint64_t limit = up(last_load->address + last_load->memory_bytes);
        if (!page_span(owner, down(first_load->address), limit, first, 1,
            rights(first_load->flags, options->read_implies_execute), down(first_load->offset), false, error)) return false;
        if (holes) {
            uint64_t begin = up(first_load->address + first_load->file_bytes);
            uint64_t finish = down(last_load->address);
            if (finish < begin)
                return guest_fail(error, QA_ERROR_FORMAT, begin, "ELF library load commands fail the actual glibc gap protection extent");
            for (uint64_t at = begin; at < finish; at += QA_NATIVE_GUEST_PAGE)
                owner->pages[(size_t)((at - first) / QA_NATIVE_GUEST_PAGE)].permissions = 0;
        }
    }
    for (size_t i = 0; i < image->segment_count; ++i) {
        const guest_elf_segment *segment = &image->segments[i];
        if (segment->type != 1) continue;
        uint64_t begin = down(segment->address);
        uint64_t file_end = up(segment->address + segment->file_bytes);
        uint64_t allocated_end = up(segment->address + segment->memory_bytes);
        uint32_t permissions = rights(segment->flags, options->read_implies_execute);
        bool map_file = image->role == GUEST_ELF_LIBRARY ? file_end > begin : segment->file_bytes != 0;
        if (map_file && !page_span(owner, begin, file_end, first, 1, permissions,
            down(segment->offset), true, error)) return false;
        if (segment->memory_bytes > segment->file_bytes) {
            uint64_t zero = image->role == GUEST_ELF_PROGRAM && !segment->file_bytes ? begin : file_end;
            uint32_t anonymous = image->role == GUEST_ELF_LIBRARY ? permissions :
                options->anonymous_permissions | (permissions & QA_NATIVE_GUEST_EXECUTE);
            if (zero < allocated_end && !page_span(owner, zero, allocated_end, first,
                2, anonymous, 0, true, error)) return false;
        }
    }
    size_t total = 0;
    for (size_t i = 0; i < owner->page_count; ++i) {
        if (!owner->pages[i].kind) continue;
        if (total > SIZE_MAX - QA_NATIVE_GUEST_PAGE)
            return guest_fail(error, QA_ERROR_MEMORY, first, "ELF live backing accounting overflows");
        total += QA_NATIVE_GUEST_PAGE;
        uint64_t base = owner->view.base + i * (uint64_t)QA_NATIVE_GUEST_PAGE;
        for (size_t j = 0; fresh && j < owner->guest->mapping_count; ++j) {
            const qa_native_guest_mapping *mapping = &owner->guest->mappings[j];
            if (base < mapping->base + mapping->bytes && mapping->base < base + QA_NATIVE_GUEST_PAGE)
                return guest_fail(error, QA_ERROR_ARGUMENT, base, "ELF live image overlaps actual retained process storage");
        }
    }
    return !fresh || total <= owner->guest->options.maximum_backing_bytes - owner->guest->backing_bytes ||
        guest_fail(error, QA_ERROR_MEMORY, first, "ELF live image exceeds remaining real process backing");
}

static bool same_run(const elf_page *first, const elf_page *next, size_t bytes)
{
    return first->kind == next->kind && first->permissions == next->permissions &&
        first->segment == next->segment &&
        (first->kind != 1 || first->file_offset + bytes == next->file_offset);
}

bool guest_elf_memory_attach(const guest_elf *elf, qa_native_guest *guest,
    const guest_elf_memory_options *options, guest_elf_memory **out, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(elf);
    if (!image || !options || options->anonymous_permissions > 7 || !out || *out ||
        !qa_native_guest_idle(guest) || image->image.target.os != QA_NATIVE_OS_LINUX ||
        image->image.target.os != guest->options.image.target.os ||
        image->image.target.arch != guest->options.image.target.arch ||
        image->image.target.abi != guest->options.image.target.abi ||
        image->image.target.pointer_bytes != guest->options.image.target.pointer_bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF live attachment requires its actual idle process, policy and empty output");
    guest_elf_memory *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning ELF live attachment");
    owner->guest = guest; owner->image = elf; owner->options = *options;
    bool okay = prepare(owner, options, true, error);
    for (size_t i = 0; okay && i < owner->page_count;) {
        const elf_page *page = owner->pages + i;
        if (!page->kind) { ++i; continue; }
        size_t count = 1;
        while (i + count < owner->page_count && same_run(page, page + count,
            count * QA_NATIVE_GUEST_PAGE)) ++count;
        size_t bytes = count * QA_NATIVE_GUEST_PAGE;
        uint64_t base = owner->view.base + i * (uint64_t)QA_NATIVE_GUEST_PAGE;
        uint64_t address = base - image->bias;
        qa_bytes initial = {0};
        if (page->segment) okay = guest_elf_range(elf, address, bytes, &initial, error);
        qa_native_guest_mapping mapping = {0};
        if (okay) okay = page->kind == 1 ?
            qa_native_guest_map_file(guest, base, bytes, page->permissions,
                image->artifact, page->file_offset, &mapping, error) :
            qa_native_guest_map(guest, base, bytes, page->permissions, initial, &mapping, error);
        if (!okay) break;
        owner->extents[owner->extent_count++] = (elf_extent){mapping.id, base, mapping.backing, bytes, false};
        owner->installed += bytes;
        if (page->kind == 1 && page->segment) {
            /* This is fresh page construction, before publication or source
             * execution, just like PE initial pages. Keep the file provenance
             * while retaining the genuine role's private partial BSS contents.
             * Inaccessible EOF pages are never copied or exposed as RAM. */
            guest_backing *backing = guest_backing_at(guest, mapping.backing);
            size_t amount = (size_t)backing->source.accessible_bytes;
            if (amount) memcpy(backing->data, initial.data, amount);
        }
        i += count;
    }
    if (!okay) {
        if (owner->installed || guest->failed) {
            guest->failed = true; *out = owner;
        } else { guest_elf_memory_abandon(&owner); }
        return false;
    }
    owner->complete = true; *out = owner; return true;
}

static bool qualify(guest_elf_memory *owner, qa_error *error)
{
    return (owner && owner->complete && qa_native_guest_idle(owner->guest)) ||
        guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF live attachment requires complete idle ownership");
}

bool guest_elf_memory_write_begin(guest_elf_memory *owner, qa_error *error)
{
    if (!qualify(owner, error)) return false;
    if (owner->writing || owner->finished)
        return guest_fail(error, QA_ERROR_ARGUMENT, owner->view.base, "ELF fresh relocation write lease has already advanced");
    owner->writing = true;
    for (size_t i = 0; i < owner->page_count; ++i) {
        const elf_page *page = owner->pages + i;
        if (!page->kind || !page->segment) continue;
        if (!qa_native_guest_protect_range(owner->guest,
            owner->view.base + i * (uint64_t)QA_NATIVE_GUEST_PAGE, QA_NATIVE_GUEST_PAGE,
            page->permissions | QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, error)) {
            owner->guest->failed = true; return false;
        }
    }
    return true;
}

bool guest_elf_memory_finish(guest_elf_memory *owner, qa_error *error)
{
    if (!qualify(owner, error)) return false;
    if (!owner->writing || owner->finished)
        return guest_fail(error, QA_ERROR_ARGUMENT, owner->view.base, "ELF final rights require the actual fresh relocation lease");
    bool okay = true;
    for (size_t i = 0; okay && i < owner->page_count; ++i) {
        const elf_page *page = owner->pages + i;
        if (!page->kind || !page->segment) continue;
        okay = qa_native_guest_protect_range(owner->guest,
            owner->view.base + i * (uint64_t)QA_NATIVE_GUEST_PAGE,
            QA_NATIVE_GUEST_PAGE, page->permissions, error);
    }
    const guest_elf_view *image = guest_elf_describe(owner->image);
    for (size_t i = 0; okay && i < image->segment_count; ++i) {
        const guest_elf_segment *segment = &image->segments[i];
        if (segment->type != UINT32_C(0x6474e552)) continue;
        if (segment->address > UINT64_MAX - segment->memory_bytes) {
            okay = guest_fail(error, QA_ERROR_FORMAT, segment->address, "ELF RELRO address extent overflows"); break;
        }
        uint64_t begin = down(segment->address), end = down(segment->address + segment->memory_bytes);
        uint64_t first = owner->view.base - image->bias;
        if (begin < first || end < begin || end - first > owner->view.bytes) {
            okay = guest_fail(error, QA_ERROR_FORMAT, begin, "ELF RELRO exceeds its actual image pages"); break;
        }
        for (uint64_t at = begin; okay && at < end; at += QA_NATIVE_GUEST_PAGE) {
            const elf_page *page = &owner->pages[(size_t)((at - first) / QA_NATIVE_GUEST_PAGE)];
            if (!page->kind || !page->segment)
                okay = guest_fail(error, QA_ERROR_FORMAT, at, "ELF RELRO crosses a non-segment page");
        }
        if (okay && end > begin) okay = qa_native_guest_protect_range(owner->guest,
            image->bias + begin, (size_t)(end - begin),
            rights(4, owner->options.read_implies_execute), error);
    }
    if (!okay) { owner->guest->failed = true; return false; }
    owner->writing = false; owner->finished = true; return true;
}

bool guest_elf_memory_program_ready(const guest_elf_memory *owner, qa_error *error)
{
    return (owner && owner->complete && !owner->writing && !owner->finished &&
        owner->view.role == GUEST_ELF_PROGRAM && qa_native_guest_idle(owner->guest)) ||
        guest_fail(error, QA_ERROR_ARGUMENT, 0,
            "ELF program startup requires its fresh raw idle attachment");
}

bool guest_elf_memory_program_seal(guest_elf_memory *owner, qa_error *error)
{
    if (!guest_elf_memory_program_ready(owner, error)) return false;
    owner->finished = true;
    return true;
}
bool guest_elf_memory_program_changed(guest_elf_memory *owner, uint64_t base,
    size_t bytes, qa_error *error)
{
    if (!owner || !owner->complete || !owner->finished || owner->writing ||
        owner->view.role != GUEST_ELF_PROGRAM || !bytes || base > UINT64_MAX - bytes ||
        !guest_mutable(owner->guest, error))
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "ELF kernel mutation receipt requires its actual sealed program owner");
    for (size_t i = 0; i < owner->extent_count; ++i) {
        elf_extent *extent = owner->extents + i;
        if (base < extent->base + extent->bytes && extent->base < base + bytes)
            extent->kernel_changed = true;
    }
    return true;
}

static bool retained(const guest_elf_memory *owner, const elf_extent *extent,
    const elf_page *page, qa_error *error)
{
    guest_backing *backing = guest_backing_at(owner->guest, extent->backing);
    qa_native_guest_mapping *anchor = guest_mapping(owner->guest, extent->base);
    if (!extent->bytes || !extent->id || extent->id >= owner->guest->next_mapping ||
        !extent->backing || extent->backing >= owner->guest->next_backing ||
        (extent->kernel_changed && owner->view.role != GUEST_ELF_PROGRAM) ||
        (!extent->kernel_changed && (!anchor || anchor->id != extent->id ||
         anchor->base != extent->base || anchor->backing != extent->backing || anchor->backing_offset)))
        return guest_fail(error, QA_ERROR_FORMAT, extent->base, "ELF attachment lost its actual original backing anchor");
    if (!backing && extent->kernel_changed) return true;
    if (!backing || backing->bytes != extent->bytes || backing->file != (page->kind == 1))
        return guest_fail(error, QA_ERROR_FORMAT, extent->base, "ELF attachment backing differs from its historical source extent");
    const guest_elf_view *image = guest_elf_describe(owner->image);
    if (backing->file && (backing->source.bytes != image->artifact.size ||
        backing->source.offset != page->file_offset ||
        !qa_sha256_equal(&backing->source.digest, &image->image.digest)))
        return guest_fail(error, QA_ERROR_FORMAT, extent->base, "ELF attachment file provenance differs from its retained artifact");
    if (extent->kernel_changed) return true;
    size_t offset = 0;
    while (offset < extent->bytes) {
        qa_native_guest_mapping *mapping = guest_mapping(owner->guest, extent->base + offset);
        if (!mapping || mapping->base != extent->base + offset ||
            mapping->backing != extent->backing || mapping->backing_offset != offset ||
            mapping->bytes > extent->bytes - offset)
            return guest_fail(error, QA_ERROR_FORMAT, extent->base + offset,
                "ELF attachment fragments differ from their actual owned backing");
        offset += (size_t)mapping->bytes;
    }
    return true;
}

enum { ELF_MEMORY_HEADER = 116, ELF_MEMORY_ROW = 40 };

static void identity_write(uint8_t *data, const guest_elf_memory *owner)
{
    const qa_native_image_info *image = &owner->view.image;
    memcpy(data, "QALM", 4);
    qa_store_u32le(data + 4, image->format); qa_store_u32le(data + 8, image->target.os);
    qa_store_u32le(data + 12, image->target.arch); qa_store_u32le(data + 16, image->target.abi);
    data[20] = image->target.pointer_bytes; data[21] = (uint8_t)owner->view.role;
    data[22] = owner->options.read_implies_execute;
    qa_store_u32le(data + 24, owner->options.anonymous_permissions);
    qa_store_u64le(data + 28, image->preferred_base); qa_store_u64le(data + 36, image->image_bytes);
    memcpy(data + 44, image->digest.bytes, 32);
    qa_store_u64le(data + 76, owner->view.base); qa_store_u64le(data + 84, owner->view.bytes);
    qa_store_u64le(data + 92, guest_elf_describe(owner->image)->bias);
    qa_store_u64le(data + 100, owner->installed); qa_store_u64le(data + 108, owner->extent_count);
}

bool guest_elf_memory_checkpoint(const guest_elf_memory *owner, qa_buffer *out, qa_error *error)
{
    if (!owner || !owner->complete || !owner->finished || owner->writing ||
        !qa_native_guest_idle(owner->guest) || !out || out->data || out->size ||
        owner->extent_count > (SIZE_MAX - ELF_MEMORY_HEADER) / ELF_MEMORY_ROW)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF attachment capture requires its complete idle finalized owner");
    size_t bytes = ELF_MEMORY_HEADER + owner->extent_count * ELF_MEMORY_ROW;
    uint8_t *data = calloc(1, bytes);
    if (!data) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning ELF attachment checkpoint");
    identity_write(data, owner);
    bool okay = true;
    for (size_t i = 0; okay && i < owner->extent_count; ++i) {
        const elf_extent *extent = &owner->extents[i];
        size_t page = (size_t)((extent->base - owner->view.base) / QA_NATIVE_GUEST_PAGE);
        okay = retained(owner, extent, owner->pages + page, error);
        uint8_t *row = data + ELF_MEMORY_HEADER + i * ELF_MEMORY_ROW;
        qa_store_u64le(row, extent->id); qa_store_u64le(row + 8, extent->base);
        qa_store_u64le(row + 16, extent->backing); qa_store_u64le(row + 24, extent->bytes);
        row[32] = extent->kernel_changed;
    }
    if (!okay) { free(data); return false; }
    *out = (qa_buffer){data, bytes}; return true;
}

bool guest_elf_memory_restore_prepare(const guest_elf *elf,
    qa_bytes encoded, guest_elf_memory **out, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(elf);
    if (!image || !out || *out || !encoded.data ||
        encoded.size < ELF_MEMORY_HEADER || memcmp(encoded.data, "QALM", 4) ||
        encoded.data[22] > 1 || encoded.data[23] ||
        qa_load_u32le(encoded.data + 24) > 7)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF cold attachment requires its actual artifact, process and typed ownership record");
    guest_elf_memory *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning ELF cold attachment");
    owner->image = elf;
    owner->options = (guest_elf_memory_options){qa_load_u32le(encoded.data + 24), encoded.data[22] != 0};
    bool okay = prepare(owner, &owner->options, false, error);
    uint64_t rows = qa_load_u64le(encoded.data + 108);
    size_t stride = ELF_MEMORY_ROW;
    if (okay && (rows > owner->page_count || rows > (encoded.size - ELF_MEMORY_HEADER) / stride ||
        encoded.size - ELF_MEMORY_HEADER != rows * stride))
        okay = guest_fail(error, QA_ERROR_FORMAT, 0, "ELF cold attachment has an invalid backing row extent");
    for (size_t page = 0, row = 0; okay && page < owner->page_count;) {
        const elf_page *planned = owner->pages + page;
        if (!planned->kind) { ++page; continue; }
        size_t count = 1;
        while (page + count < owner->page_count && same_run(planned, planned + count,
            count * QA_NATIVE_GUEST_PAGE)) ++count;
        if (row >= rows) { okay = guest_fail(error, QA_ERROR_FORMAT, page, "ELF cold attachment omits an actual image backing"); break; }
        const uint8_t *data = encoded.data + ELF_MEMORY_HEADER + row * stride;
        elf_extent extent = {qa_load_u64le(data), qa_load_u64le(data + 8),
            qa_load_u64le(data + 16), count * QA_NATIVE_GUEST_PAGE, data[32] != 0};
        if (data[32] > 1 || data[33] || data[34] || data[35] || qa_load_u32le(data + 36))
            okay = guest_fail(error, QA_ERROR_FORMAT, extent.base, "ELF cold kernel mutation receipt is invalid");
        if (!extent.id || !extent.backing || extent.base != owner->view.base + page * (uint64_t)QA_NATIVE_GUEST_PAGE ||
            qa_load_u64le(data + 24) != extent.bytes)
            okay = guest_fail(error, QA_ERROR_FORMAT, extent.base, "ELF cold attachment differs from its actual source mapping extent");
        for (size_t i = 0; okay && i < row; ++i)
            if (owner->extents[i].id == extent.id || owner->extents[i].backing == extent.backing)
                okay = guest_fail(error, QA_ERROR_FORMAT, extent.base, "ELF cold attachment repeats an original backing owner");
        if (okay) {
            owner->extents[owner->extent_count++] = extent; owner->installed += extent.bytes;
            page += count; ++row;
        }
    }
    uint8_t expected[ELF_MEMORY_HEADER] = {0};
    if (okay) {
        identity_write(expected, owner);
        if (memcmp(expected, encoded.data, ELF_MEMORY_HEADER))
            okay = guest_fail(error, QA_ERROR_FORMAT, owner->view.base, "ELF cold attachment source identity or policy differs");
    }
    if (!okay) { guest_elf_memory_abandon(&owner); return false; }
    owner->complete = owner->finished = true; *out = owner; return true;
}


bool guest_elf_memory_pristine(const guest_elf_memory *owner, uint64_t backing,
    size_t extent, const qa_native_guest_file *file, bool *matched, qa_bytes *out,
    qa_error *error)
{
    if (!owner || !owner->complete || !owner->finished || !matched || !out)
        return guest_fail(error, QA_ERROR_ARGUMENT, backing, "ELF pristine read requires its actual prepared attachment");
    const guest_elf *elf = owner->image;
    *matched = false; *out = (qa_bytes){0};
    const guest_elf_view *image = guest_elf_describe(elf);
    bool okay = true;
    for (size_t i = 0; okay && i < owner->extent_count; ++i) {
        const elf_extent *row = owner->extents + i;
        if (row->backing != backing) continue;
        size_t page_index = (size_t)((row->base - owner->view.base) / QA_NATIVE_GUEST_PAGE);
        const elf_page *page = owner->pages + page_index;
        if (row->bytes != extent || (page->kind == 1) != (file != NULL)) {
            okay = guest_fail(error, QA_ERROR_FORMAT, backing, "ELF backing differs from its actual attachment kind or extent");
            break;
        }
        if (file && (file->capability || !qa_sha256_equal(&file->digest, &image->image.digest) ||
            file->bytes != image->artifact.size || file->offset != page->file_offset)) {
            okay = guest_fail(error, QA_ERROR_FORMAT, backing, "ELF private file baseline differs from its actual installed artifact");
            break;
        }
        if (page->segment) {
            okay = guest_elf_range(elf, row->base - image->bias, extent, out, error);
            if (okay && file && out->size > file->accessible_bytes)
                out->size = (size_t)file->accessible_bytes;
        } else if (page->file_offset < image->artifact.size) {
            size_t bytes = image->artifact.size - (size_t)page->file_offset;
            if (bytes > extent) bytes = extent;
            *out = (qa_bytes){image->artifact.data + (size_t)page->file_offset, bytes};
        }
        *matched = okay;
    }
    return okay;
}

bool guest_elf_memory_restore_attach(guest_elf_memory *owner, qa_native_guest *guest,
    qa_error *error)
{
    if (!owner || !owner->complete || !owner->finished || owner->guest ||
        !qa_native_guest_idle(guest))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF adoption requires prepared metadata and its restored guest");
    const qa_native_image_info *image = &owner->view.image;
    const qa_native_image_info *actual = &guest->options.image;
    if (image->target.os != actual->target.os || image->target.arch != actual->target.arch ||
        image->target.abi != actual->target.abi || image->target.pointer_bytes != actual->target.pointer_bytes)
        return guest_fail(error, QA_ERROR_FORMAT, 0, "ELF attachment differs from its actual process ABI");
    owner->guest = guest;
    for (size_t i = 0; i < owner->extent_count; ++i) {
        const elf_extent *extent = owner->extents + i;
        size_t page = (size_t)((extent->base - owner->view.base) / QA_NATIVE_GUEST_PAGE);
        if (!retained(owner, extent, owner->pages + page, error)) return false;
    }
    return true;
}

void guest_elf_memory_abandon(guest_elf_memory **owner)
{
    if (!owner || !*owner) return;
    free((*owner)->pages); free((*owner)->extents); free(*owner); *owner = NULL;
}

bool guest_elf_memory_close(guest_elf_memory **owner, qa_error *error)
{
    if (!owner) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF attachment owner is required");
    if (!*owner) return true;
    guest_elf_memory *memory = *owner;
    if (!qa_native_guest_idle(memory->guest))
        return guest_fail(error, QA_ERROR_ARGUMENT, memory->view.base, "ELF attachment retirement requires idle process ownership; terminal pages require whole-process teardown");
    for (size_t i = memory->extent_count; i; --i) {
        elf_extent extent = memory->extents[i - 1];
        if (extent.kernel_changed)
            return guest_fail(error, QA_ERROR_ARGUMENT, extent.base, "kernel-mutated ELF pages require actual whole-process retirement");
        if (!qa_native_guest_unmap_range(memory->guest, extent.base, extent.bytes, error)) return false;
        memory->complete = false;
        --memory->extent_count;
    }
    guest_elf_memory_abandon(owner); return true;
}

qa_native_guest *guest_elf_memory_guest(guest_elf_memory *owner)
{ return owner && owner->complete ? owner->guest : NULL; }
const guest_elf_memory_view *guest_elf_memory_describe(const guest_elf_memory *owner)
{ return owner ? &owner->view : NULL; }
