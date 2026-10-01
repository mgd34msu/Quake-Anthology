#include "pe_memory.h"
#include <stdlib.h>
#include <string.h>

struct guest_pe_memory {
    guest_pe_memory_view view;
    qa_native_guest *guest;
    uint64_t *mappings;
    bool complete;
};

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
        a->preferred_base == b->preferred_base && a->image_bytes == b->image_bytes &&
        qa_sha256_equal(&a->digest, &b->digest);
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
    if (!qa_native_guest_destroy(&memory->guest, error)) return false;
    free(memory->mappings); free(memory); *owner = NULL;
    return true;
}

bool guest_pe_memory_open(const guest_pe *pe, const qa_native_guest_options *options,
    guest_pe_memory **out, qa_error *error)
{
    const guest_pe_view *image = guest_pe_describe(pe);
    if (!image || !options || !out || *out || !image_equal(&image->image, &options->image))
        return fail(error, QA_ERROR_ARGUMENT, 0, "PE memory requires its actual image identity and empty output");
    guest_pe_memory *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, 0, "allocating PE memory owner");
    uint8_t *pages = NULL; qa_buffer initial = {0};
    bool okay = prepare(image, owner, &pages, &initial, error);
    size_t page_count = owner->view.bytes / QA_NATIVE_GUEST_PAGE, runs = 0;
    if (okay && owner->view.bytes > options->maximum_backing_bytes)
        okay = fail(error, QA_ERROR_ARGUMENT, image->base, "PE page backing exceeds admitted guest storage");
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
    if (okay) okay = qa_native_guest_create(options, &owner->guest, error);
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
        if (okay) owner->mappings[owner->view.mapping_count++] = mapping.id;
        at = end;
    }
    free(pages);
    qa_buffer_free(&initial);
    if (!okay) {
        /* No partial detach: the fresh candidate owns every installed run.
         * Retain its CPU and backing on a real close failure for cleanup retry. */
        qa_error cleanup = {0};
        if (!guest_pe_memory_close(&owner, &cleanup)) {
            if (error) *error = cleanup;
            *out = owner;
        }
        return false;
    }
    owner->complete = true; *out = owner;
    return true;
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
