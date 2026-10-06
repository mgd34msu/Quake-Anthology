#include "pe_memory.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct guest_pe_memory {
    guest_pe_memory_view view;
    qa_native_guest *guest;
    uint64_t *mappings;
    size_t attached_bytes;
    bool complete, borrowed;
};

static bool attachment(const guest_pe_memory *owner, uint64_t id, uint64_t base,
    uint64_t backing_id, uint64_t bytes, qa_error *error)
{
    qa_native_guest *guest = owner->guest;
    guest_backing *backing = guest_backing_at(guest, backing_id);
    qa_native_guest_mapping *anchor = guest_mapping(guest, base);
    if (!bytes || bytes > UINT64_MAX - base || bytes % QA_NATIVE_GUEST_PAGE || !backing || backing->file || backing->bytes != bytes ||
        !anchor || anchor->id != id || anchor->base != base || anchor->backing != backing_id || anchor->backing_offset)
        return guest_fail(error, QA_ERROR_FORMAT, base, "PE attachment has no actual original backing anchor");
    size_t offset = 0;
    while (offset < bytes) {
        qa_native_guest_mapping *mapping = guest_mapping(guest, base + offset);
        if (!mapping || mapping->base != base + offset || mapping->backing != backing_id ||
            mapping->backing_offset != offset || mapping->bytes > bytes - offset)
            return guest_fail(error, QA_ERROR_FORMAT, base + offset, "PE attachment fragments differ from their actual retained backing");
        offset += (size_t)mapping->bytes;
    }
    return true;
}

static bool fail(qa_error *error, qa_status code, uint64_t address, const char *message)
{
    qa_error_set(error, code, (size_t)address, "%s", message);
    return false;
}

static bool image_equal(const qa_native_image_info *a, const qa_native_image_info *b)
{
    return a->format == b->format && a->target.os == b->target.os &&
        a->target.arch == b->target.arch && a->target.abi == b->target.abi &&
        a->target.pointer_bytes == b->target.pointer_bytes &&
        a->preferred_base == b->preferred_base && a->image_bytes == b->image_bytes;
}

static bool page_bytes(size_t bytes, size_t *out, qa_error *error)
{
    if (bytes > SIZE_MAX - (QA_NATIVE_GUEST_PAGE - 1))
        return fail(error, QA_ERROR_FORMAT, bytes, "PE page extent overflows");
    *out = (bytes + QA_NATIVE_GUEST_PAGE - 1) & ~(size_t)(QA_NATIVE_GUEST_PAGE - 1);
    return true;
}

static bool mark(uint8_t *pages, size_t bytes, size_t start, size_t length,
    uint32_t permissions, qa_error *error)
{
    if (start > bytes || length > bytes - start || permissions > 7)
        return fail(error, QA_ERROR_FORMAT, start, "PE protection extent exceeds the image");
    if (!length) return true;
    size_t end;
    if (!page_bytes(start + length, &end, error)) return false;
    for (size_t at = start / QA_NATIVE_GUEST_PAGE; at < end / QA_NATIVE_GUEST_PAGE; ++at)
        pages[at] = (uint8_t)permissions;
    return true;
}

static bool prepare(const guest_pe_view *pe, guest_pe_memory *owner, uint8_t **out,
    qa_buffer *initial, qa_error *error)
{
    size_t bytes;
    if (!page_bytes(pe->bytes.size, &bytes, error)) return false;
    if (!bytes || bytes > UINT64_MAX - pe->base ||
        (pe->image.target.pointer_bytes == 4 && pe->base + bytes > UINT64_C(0x100000000)))
        return fail(error, QA_ERROR_FORMAT, pe->base, "PE page mapping exceeds its ABI address space");
    uint8_t *pages = calloc(bytes / QA_NATIVE_GUEST_PAGE, 1);
    if (!pages) return fail(error, QA_ERROR_MEMORY, pe->base, "allocating PE page protections");
    uint8_t *data = calloc(1, bytes);
    if (!data) {
        free(pages);
        return fail(error, QA_ERROR_MEMORY, pe->base, "allocating PE live image bytes");
    }
    owner->view.image = pe->image; owner->view.base = pe->base; owner->view.bytes = bytes;
    owner->view.flat = pe->section_alignment < QA_NATIVE_GUEST_PAGE;
    bool okay = true;
    if (owner->view.flat) {
        /* PE Format's low-alignment file/RVA restriction and Wine 11.0's
         * map_image_into_view flat branch: original file pages, RWX, no
         * relocation. A section sharing a page has no separate protection. */
        if (pe->base != pe->image.preferred_base || pe->file_alignment != pe->section_alignment)
            okay = fail(error, QA_ERROR_FORMAT, pe->base, "flat PE mapping requires its original base and alignment");
        for (size_t i = 0; okay && i < pe->section_count; ++i)
            if (pe->sections[i].rva != pe->sections[i].file_offset)
                okay = fail(error, QA_ERROR_FORMAT, pe->sections[i].rva,
                    "flat PE section file position differs from its RVA");
        size_t copied = pe->artifact.size < bytes ? pe->artifact.size : bytes;
        if (okay) okay = mark(pages, bytes, 0, copied,
            QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE | QA_NATIVE_GUEST_EXECUTE, error);
        if (okay) memcpy(data, pe->artifact.data, copied);
    } else {
        /* Ordinary sections are page-aligned and do not share pages. Header
         * padding to SectionAlignment is readable; unclaimed pages stay mapped
         * with no access. The live section extent follows nonzero VirtualSize;
         * raw padding outside that rounded extent is not loaded. */
        size_t alignment = pe->section_alignment;
        size_t header = ((size_t)pe->header_bytes + alignment - 1) & ~(alignment - 1);
        okay = mark(pages, bytes, 0, header, QA_NATIVE_GUEST_READ, error);
        if (okay) memcpy(data, pe->bytes.data, pe->header_bytes);
        for (size_t i = 0; okay && i < pe->section_count; ++i) {
            const guest_pe_section *section = &pe->sections[i];
            uint64_t actual = section->virtual_bytes ? section->virtual_bytes : section->file_bytes;
            uint64_t length = (actual + alignment - 1) & ~(uint64_t)(alignment - 1);
            if (length > section->bytes)
                okay = fail(error, QA_ERROR_FORMAT, section->rva, "PE live section exceeds inert source storage");
            else okay = mark(pages, bytes, section->rva, (size_t)length, section->permissions, error);
            if (okay && length) memcpy(data + section->rva, pe->bytes.data + section->rva, (size_t)length);
        }
    }
    if (!okay) { free(data); free(pages); return false; }
    *initial = (qa_buffer){data, bytes};
    *out = pages;
    return true;
}

bool guest_pe_memory_close(guest_pe_memory **owner, qa_error *error)
{
    if (!owner) return fail(error, QA_ERROR_ARGUMENT, 0, "PE memory owner is required");
    if (!*owner) return true;
    guest_pe_memory *memory = *owner;
    if (memory->borrowed) {
        if (memory->attached_bytes && !qa_native_guest_unmap_range(memory->guest,
            memory->view.base, memory->attached_bytes, error)) return false;
    } else if (!qa_native_guest_destroy(&memory->guest, error)) return false;
    free(memory->mappings); free(memory); *owner = NULL;
    return true;
}

static bool install(const guest_pe *pe, const qa_native_guest_options *options,
    qa_native_guest *borrowed, guest_pe_memory **out, qa_error *error)
{
    const guest_pe_view *image = guest_pe_describe(pe);
    bool compatible = image && options && (borrowed ?
        image->image.target.os == options->image.target.os &&
        image->image.target.arch == options->image.target.arch &&
        image->image.target.abi == options->image.target.abi &&
        image->image.target.pointer_bytes == options->image.target.pointer_bytes :
        image_equal(&image->image, &options->image));
    if (!compatible || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, 0, "PE memory requires its actual image identity and empty output");
    if (borrowed && !guest_mutable(borrowed, error)) return false;
    guest_pe_memory *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, 0, "allocating PE memory owner");
    owner->borrowed = borrowed != NULL; owner->guest = borrowed;
    uint8_t *pages = NULL; qa_buffer initial = {0};
    bool okay = prepare(image, owner, &pages, &initial, error);
    size_t page_count = owner->view.bytes / QA_NATIVE_GUEST_PAGE, runs = 0;
    if (okay && owner->view.bytes > options->maximum_backing_bytes)
        okay = fail(error, QA_ERROR_ARGUMENT, image->base, "PE page backing exceeds admitted guest storage");
    if (okay && borrowed) {
        if (owner->view.bytes > options->maximum_backing_bytes - borrowed->backing_bytes)
            okay = fail(error, QA_ERROR_MEMORY, image->base, "additional PE pages exceed actual remaining process backing");
        for (size_t i = 0; okay && i < borrowed->mapping_count; ++i) {
            const qa_native_guest_mapping *mapping = &borrowed->mappings[i];
            if (image->base < mapping->base + mapping->bytes &&
                mapping->base < image->base + owner->view.bytes)
                okay = fail(error, QA_ERROR_ARGUMENT, image->base, "additional PE image overlaps actual process storage");
        }
    }
    if (okay) {
        for (size_t at = 0; at < page_count; ++at)
            if (!at || pages[at] != pages[at - 1]) ++runs;
        if (runs > SIZE_MAX / sizeof(*owner->mappings))
            okay = fail(error, QA_ERROR_MEMORY, runs, "PE mapping registry extent overflows");
        else {
            owner->mappings = malloc(runs * sizeof(*owner->mappings));
            if (!owner->mappings) okay = fail(error, QA_ERROR_MEMORY, runs, "allocating PE mapping registry");
            owner->view.mappings = owner->mappings;
        }
    }
    if (okay && !borrowed) okay = qa_native_guest_create(options, &owner->guest, error);
    for (size_t at = 0; okay && at < page_count; ) {
        size_t end = at + 1;
        while (end < page_count && pages[end] == pages[at]) ++end;
        size_t offset = at * QA_NATIVE_GUEST_PAGE, length = (end - at) * QA_NATIVE_GUEST_PAGE;
        size_t copied = offset < initial.size ? initial.size - offset : 0;
        if (copied > length) copied = length;
        qa_bytes source = {copied ? initial.data + offset : NULL, copied};
        qa_native_guest_mapping mapping;
        okay = qa_native_guest_map(owner->guest, image->base + offset, length, pages[at],
            source, &mapping, error);
        if (okay) {
            owner->mappings[owner->view.mapping_count++] = mapping.id;
            owner->attached_bytes += length;
        }
        at = end;
    }
    free(pages);
    qa_buffer_free(&initial);
    if (!okay) {
        /* A primary owns the whole candidate. A borrowed attachment may undo
         * only its actual installed prefix before initialization. Dependency
         * faults retain the guest and backing for real whole-process close. */
        qa_error cleanup = {0};
        bool closed = true;
        if (borrowed) {
            if (borrowed->failed) closed = false;
            else if (owner->attached_bytes) closed = qa_native_guest_unmap_range(borrowed,
                image->base, owner->attached_bytes, &cleanup);
            if (closed) { free(owner->mappings); free(owner); owner = NULL; }
        } else closed = guest_pe_memory_close(&owner, &cleanup);
        if (!closed) {
            if (error && cleanup.code != QA_OK) *error = cleanup;
            *out = owner;
        }
        return false;
    }
    owner->complete = true; *out = owner;
    return true;
}

bool guest_pe_memory_open(const guest_pe *pe, const qa_native_guest_options *options,
    guest_pe_memory **out, qa_error *error)
{ return install(pe, options, NULL, out, error); }

bool guest_pe_memory_attach(const guest_pe *pe, qa_native_guest *guest,
    guest_pe_memory **out, qa_error *error)
{
    if (!guest) return fail(error, QA_ERROR_ARGUMENT, 0, "additional PE image needs its actual process guest");
    return install(pe, &guest->options, guest, out, error);
}

void guest_pe_memory_abandon(guest_pe_memory **owner)
{
    if (!owner || !*owner || !(*owner)->borrowed) return;
    free((*owner)->mappings); free(*owner); *owner = NULL;
}

bool guest_pe_memory_checkpoint(const guest_pe_memory *memory, qa_buffer *out, qa_error *error)
{
    if (!memory || !memory->complete || !qa_native_guest_idle(memory->guest) || !out || out->data || out->size ||
        memory->view.mapping_count > (SIZE_MAX - 76) / 32)
        return fail(error, QA_ERROR_ARGUMENT, 0, "PE attachment checkpoint requires complete idle ownership and empty output");
    size_t bytes = 76 + memory->view.mapping_count * 32;
    uint8_t *data = calloc(1, bytes);
    if (!data) return fail(error, QA_ERROR_MEMORY, 0, "owning PE attachment checkpoint");
    const qa_native_image_info *image = &memory->view.image;
    memcpy(data, "QAPM", 4);
    qa_store_u32le(data + 4, image->format); qa_store_u32le(data + 8, image->target.os);
    qa_store_u32le(data + 12, image->target.arch); qa_store_u32le(data + 16, image->target.abi);
    data[20] = image->target.pointer_bytes; data[21] = memory->view.flat; data[22] = memory->borrowed;
    qa_store_u64le(data + 28, image->preferred_base); qa_store_u64le(data + 36, image->image_bytes);
    qa_store_u64le(data + 44, memory->view.base); qa_store_u64le(data + 52, memory->view.bytes);
    qa_store_u64le(data + 60, memory->attached_bytes); qa_store_u64le(data + 68, memory->view.mapping_count);
    uint64_t base = memory->view.base; bool okay = true;
    for (size_t i = 0; okay && i < memory->view.mapping_count; ++i) {
        qa_native_guest_mapping *mapping = guest_mapping(memory->guest, base);
        guest_backing *backing = mapping ? guest_backing_at(memory->guest, mapping->backing) : NULL;
        if (!mapping || !backing || backing->bytes > memory->view.bytes - (base - memory->view.base)) {
            okay = fail(error, QA_ERROR_FORMAT, base, "PE attachment initial backing exceeds its actual owned span"); break;
        }
        uint64_t id = memory->mappings[i], length = backing->bytes;
        okay = attachment(memory, id, base, mapping->backing, length, error);
        uint8_t *record = data + 76 + i * 32;
        qa_store_u64le(record, id); qa_store_u64le(record + 8, base);
        qa_store_u64le(record + 16, mapping->backing); qa_store_u64le(record + 24, length);
        base += length;
    }
    if (okay && (base - memory->view.base != memory->view.bytes || memory->attached_bytes != memory->view.bytes))
        okay = fail(error, QA_ERROR_FORMAT, base, "PE attachment initial backings do not cover the complete image");
    if (!okay) { free(data); return false; }
    *out = (qa_buffer){data, bytes}; return true;
}

static bool memory_record(const guest_pe *pe, qa_bytes encoded,
    guest_pe_memory_view *view, bool *borrowed, qa_error *error)
{
    const guest_pe_view *image = guest_pe_describe(pe);
    if (!image || !view || !borrowed ||
        !encoded.data || encoded.size < 76 || memcmp(encoded.data, "QAPM", 4))
        return fail(error, QA_ERROR_ARGUMENT, 0, "PE cold attachment requires its actual image, idle lower guest and typed record");
    const uint8_t *data = encoded.data; qa_native_image_info saved = {0};
    saved.format = (qa_native_image_format)qa_load_u32le(data + 4);
    saved.target.os = (qa_native_os)qa_load_u32le(data + 8);
    saved.target.arch = (qa_native_arch)qa_load_u32le(data + 12);
    saved.target.abi = (qa_native_abi)qa_load_u32le(data + 16); saved.target.pointer_bytes = data[20];
    saved.preferred_base = qa_load_u64le(data + 28); saved.image_bytes = qa_load_u64le(data + 36);
    uint64_t base = qa_load_u64le(data + 44), bytes = qa_load_u64le(data + 52);
    uint64_t attached = qa_load_u64le(data + 60), count = qa_load_u64le(data + 68);
    size_t actual_bytes = 0;
    if (!image_equal(&saved, &image->image) || base != image->base ||
        !page_bytes(image->bytes.size, &actual_bytes, error) || bytes != actual_bytes || attached != bytes ||
        data[21] != (image->section_alignment < QA_NATIVE_GUEST_PAGE) || data[22] > 1 ||
        memcmp(data + 23, "\0\0\0\0\0", 5) ||
        !count || count > SIZE_MAX / sizeof(uint64_t) || count > (encoded.size - 76) / 32 ||
        encoded.size - 76 != count * 32)
        return fail(error, QA_ERROR_FORMAT, base, "PE cold attachment differs from its actual source identity or ownership");
    *view = (guest_pe_memory_view){saved, base, actual_bytes, data[21] != 0, NULL, (size_t)count};
    *borrowed = data[22] != 0;
    uint64_t address = base;
    for (size_t i = 0; i < (size_t)count; ++i) {
        const uint8_t *row = data + 76 + i * 32;
        uint64_t id = qa_load_u64le(row), start = qa_load_u64le(row + 8);
        uint64_t backing = qa_load_u64le(row + 16), length = qa_load_u64le(row + 24);
        if (!id || !backing || start != address || !length || length % QA_NATIVE_GUEST_PAGE ||
            length > bytes - (address - base))
            return fail(error, QA_ERROR_FORMAT, start, "PE cold attachment has invalid backing bounds");
        for (size_t j = 0; j < i; ++j) {
            const uint8_t *prior = data + 76 + j * 32;
            if (qa_load_u64le(prior) == id || qa_load_u64le(prior + 16) == backing)
                return fail(error, QA_ERROR_FORMAT, start, "PE cold attachment repeats its backing identity");
        }
        address += length;
    }
    return address - base == bytes ||
        fail(error, QA_ERROR_FORMAT, address, "PE cold attachment omits image backing");
}

bool guest_pe_memory_pristine(const guest_pe *pe, qa_bytes encoded, uint64_t backing,
    size_t extent, bool *matched, qa_bytes *out, qa_error *error)
{
    guest_pe_memory_view view = {0}; bool borrowed = false;
    if (!matched || !out || !memory_record(pe, encoded, &view, &borrowed, error)) return false;
    *matched = false; *out = (qa_bytes){0};
    const guest_pe_view *image = guest_pe_describe(pe);
    for (size_t i = 0; i < view.mapping_count; ++i) {
        const uint8_t *row = encoded.data + 76 + i * 32;
        if (qa_load_u64le(row + 16) != backing) continue;
        if (qa_load_u64le(row + 24) != extent)
            return fail(error, QA_ERROR_FORMAT, backing, "PE backing differs from its saved attachment extent");
        size_t offset = (size_t)(qa_load_u64le(row + 8) - view.base);
        qa_bytes bytes = view.flat ? image->artifact : image->bytes;
        if (offset < bytes.size) {
            size_t amount = bytes.size - offset;
            if (amount > extent) amount = extent;
            *out = (qa_bytes){bytes.data + offset, amount};
        }
        *matched = true;
    }
    return true;
}

bool guest_pe_memory_adopt(const guest_pe *pe, qa_native_guest *guest, qa_bytes encoded,
    bool primary, guest_pe_memory **out, qa_error *error)
{
    guest_pe_memory_view view = {0}; bool borrowed = false;
    if (!guest || !qa_native_guest_idle(guest) || !out || *out ||
        !memory_record(pe, encoded, &view, &borrowed, error)) return false;
    const guest_pe_view *image = guest_pe_describe(pe);
    if (borrowed != !primary || image->image.target.os != guest->options.image.target.os ||
        image->image.target.arch != guest->options.image.target.arch ||
        image->image.target.abi != guest->options.image.target.abi ||
        image->image.target.pointer_bytes != guest->options.image.target.pointer_bytes ||
        (primary && !image_equal(&image->image, &guest->options.image)))
        return fail(error, QA_ERROR_FORMAT, view.base, "PE attachment differs from its actual process authority");
    const uint8_t *data = encoded.data;
    qa_native_image_info saved = view.image;
    uint64_t base = view.base, count = view.mapping_count;
    size_t actual_bytes = view.bytes;
    guest_pe_memory *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, base, "owning PE cold attachment");
    owner->mappings = malloc((size_t)count * sizeof(*owner->mappings));
    if (!owner->mappings) { free(owner); return fail(error, QA_ERROR_MEMORY, base, "owning PE cold attachment anchors"); }
    owner->guest = guest; owner->borrowed = !primary; owner->attached_bytes = actual_bytes;
    owner->view = (guest_pe_memory_view){saved, base, actual_bytes, data[21] != 0, owner->mappings, (size_t)count};
    bool okay = true;
    for (size_t i = 0; okay && i < (size_t)count; ++i) {
        const uint8_t *record = data + 76 + i * 32;
        uint64_t id = qa_load_u64le(record), start = qa_load_u64le(record + 8);
        uint64_t backing = qa_load_u64le(record + 16), length = qa_load_u64le(record + 24);
        okay = attachment(owner, id, start, backing, length, error);
        if (okay) owner->mappings[i] = id;
    }
    if (!okay) { free(owner->mappings); free(owner); return false; }
    owner->complete = true; *out = owner; return true;
}

qa_native_guest *guest_pe_memory_guest(guest_pe_memory *owner)
{ return owner && owner->complete ? owner->guest : NULL; }

const guest_pe_memory_view *guest_pe_memory_describe(const guest_pe_memory *owner)
{ return owner ? &owner->view : NULL; }

bool guest_pe_memory_mapping_at(const guest_pe_memory *owner, size_t index,
    qa_native_guest_mapping *out, qa_error *error)
{
    if (!owner || !owner->complete || !out || index >= owner->view.mapping_count)
        return fail(error, QA_ERROR_ARGUMENT, index, "PE image mapping index is invalid");
    uint64_t id = owner->mappings[index];
    size_t count = qa_native_guest_mapping_count(owner->guest);
    for (size_t i = 0; i < count; ++i) {
        qa_native_guest_mapping current;
        if (!qa_native_guest_mapping_at(owner->guest, i, &current, error)) return false;
        if (current.id == id) { *out = current; return true; }
    }
    return fail(error, QA_ERROR_NOT_FOUND, id, "PE image mapping was removed from its lower owner");
}
