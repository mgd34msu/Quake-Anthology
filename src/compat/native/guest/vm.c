#include "internal.h"
#include "unicorn_state.h"

bool guest_allocation_storage(const qa_native_guest *guest, const guest_allocation *allocation,
    uint64_t *backing, size_t *bytes, qa_error *error)
{
    if (!allocation->bytes || allocation->bytes > SIZE_MAX - (QA_NATIVE_GUEST_PAGE - 1))
        return guest_fail(error, QA_ERROR_FORMAT, allocation->address, "native allocation page extent overflows");
    size_t rounded = (allocation->bytes + QA_NATIVE_GUEST_PAGE - 1) & ~(size_t)(QA_NATIVE_GUEST_PAGE - 1);
    qa_native_guest_mapping *first = guest_mapping(guest, allocation->address);
    if (!first || first->id != allocation->mapping || first->base != allocation->address ||
        first->backing_offset || rounded > UINT64_MAX - allocation->address)
        return guest_fail(error, QA_ERROR_FORMAT, allocation->address, "native allocation has no actual mapping anchor");
    guest_backing *owned = guest_backing_at(guest, first->backing);
    if (!owned || owned->file || owned->bytes != rounded)
        return guest_fail(error, QA_ERROR_FORMAT, allocation->address, "native allocation differs from its actual owned backing size");
    size_t offset = 0;
    while (offset < rounded) {
        qa_native_guest_mapping *mapping = guest_mapping(guest, allocation->address + offset);
        if (!mapping || mapping->base != allocation->address + offset ||
            mapping->backing != first->backing || mapping->backing_offset != offset ||
            mapping->bytes > rounded - offset)
            return guest_fail(error, QA_ERROR_FORMAT, allocation->address + offset,
                "native allocation fragments do not cover their actual backing extent");
        offset += (size_t)mapping->bytes;
    }
    if (backing) *backing = first->backing;
    if (bytes) *bytes = rounded;
    return true;
}

static bool overlaps(uint64_t base, uint64_t bytes, uint64_t other, uint64_t length)
{ return base < other + length && other < base + bytes; }

static bool append(qa_native_guest_mapping *records, size_t *count, uint64_t *next,
    const qa_native_guest_mapping *source, uint64_t base, uint64_t bytes,
    uint32_t permissions, bool *first, qa_error *error)
{
    if (!bytes) return true;
    if (!*first && *next == UINT64_MAX)
        return guest_fail(error, QA_ERROR_MEMORY, base, "native mapping fragment identities are exhausted");
    qa_native_guest_mapping record = *source;
    record.id = *first ? source->id : (*next)++;
    record.base = base; record.bytes = bytes;
    record.backing_offset += base - source->base; record.permissions = permissions;
    records[(*count)++] = record; *first = false;
    return true;
}

static bool change(qa_native_guest *guest, uint64_t base, size_t bytes,
    uint32_t permissions, bool remove, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!bytes || base % QA_NATIVE_GUEST_PAGE || bytes % QA_NATIVE_GUEST_PAGE || permissions > 7)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "native mapping range needs whole pages and actual permissions");
    if (!guest_range(guest, base, bytes, 0, error)) return false;
    uint64_t end = base + bytes;
    for (size_t i = 0; i < guest->callback_count; ++i) {
        uint64_t address = guest->callbacks[i].address;
        if (address >= base && address < end && (remove || !(permissions & QA_NATIVE_GUEST_EXECUTE)))
            return guest_fail(error, QA_ERROR_ARGUMENT, address, "retire the actual callback before changing its executable trap");
    }
    if (remove) for (size_t i = 0; i < guest->allocation_count; ++i) {
        const guest_allocation *allocation = &guest->allocations[i];
        size_t rounded = (allocation->bytes + QA_NATIVE_GUEST_PAGE - 1) & ~(size_t)(QA_NATIVE_GUEST_PAGE - 1);
        if (!overlaps(base, bytes, allocation->address, rounded)) continue;
        if (!guest_allocation_storage(guest, allocation, NULL, NULL, error)) return false;
        return guest_fail(error, QA_ERROR_ARGUMENT, allocation->address, "free actual allocator storage before removing its pages");
    }
    size_t affected = 0;
    for (size_t i = 0; i < guest->mapping_count; ++i)
        if (overlaps(base, bytes, guest->mappings[i].base, guest->mappings[i].bytes)) ++affected;
    if (affected > (SIZE_MAX - guest->mapping_count) / 2)
        return guest_fail(error, QA_ERROR_MEMORY, base, "native mapping fragment table overflows");
    size_t capacity = guest->mapping_count + affected * 2;
    if (capacity > SIZE_MAX / sizeof(*guest->mappings))
        return guest_fail(error, QA_ERROR_MEMORY, base, "native mapping fragment bytes overflow");
    qa_native_guest_mapping *records = malloc(capacity * sizeof(*records));
    if (!records) return guest_fail(error, QA_ERROR_MEMORY, base, "owning native mapping replacement records");
    size_t count = 0; uint64_t next = guest->next_mapping; bool okay = true;
    for (size_t i = 0; okay && i < guest->mapping_count; ++i) {
        const qa_native_guest_mapping *source = &guest->mappings[i];
        uint64_t limit = source->base + source->bytes;
        if (!overlaps(base, bytes, source->base, source->bytes)) { records[count++] = *source; continue; }
        uint64_t begin = base > source->base ? base : source->base;
        uint64_t finish = end < limit ? end : limit; bool first = true;
        okay = append(records, &count, &next, source, source->base, begin - source->base,
            source->permissions, &first, error);
        if (okay && !remove) okay = append(records, &count, &next, source, begin,
            finish - begin, permissions, &first, error);
        if (okay) okay = append(records, &count, &next, source, finish, limit - finish,
            source->permissions, &first, error);
    }
    /* Every allocation and identity is prepared before topology changes.
     * Checked dependency transactions own their real topology on failure; a
     * partially replaced guest is terminal and retains all backing for close. */
    for (size_t i = 0; okay && i < guest->mapping_count; ++i) {
        const qa_native_guest_mapping *mapping = &guest->mappings[i];
        if (overlaps(base, bytes, mapping->base, mapping->bytes))
            okay = guest_backend_change(guest, mapping, 0, true, error);
    }
    size_t cursor = 0;
    for (size_t i = 0; okay && i < guest->mapping_count; ++i) {
        const qa_native_guest_mapping *source = &guest->mappings[i];
        if (!overlaps(base, bytes, source->base, source->bytes)) { ++cursor; continue; }
        uint64_t limit = source->base + source->bytes;
        while (okay && cursor < count && records[cursor].base >= source->base && records[cursor].base < limit)
            okay = guest_backend_map(guest, &records[cursor++], error);
    }
    if (!okay) { free(records); return false; }
    cursor = 0;
    for (size_t i = 0; i < guest->mapping_count; ++i) {
        const qa_native_guest_mapping *source = &guest->mappings[i];
        if (!overlaps(base, bytes, source->base, source->bytes)) { ++cursor; continue; }
        size_t first = cursor; uint64_t limit = source->base + source->bytes;
        while (cursor < count && records[cursor].base >= source->base && records[cursor].base < limit) ++cursor;
        guest_backing *backing = guest_backing_at(guest, source->backing);
        --backing->references; backing->references += cursor - first;
    }
    free(guest->mappings); guest->mappings = records;
    guest->mapping_count = count; guest->mapping_capacity = capacity; guest->next_mapping = next;
    for (size_t i = 0; i < guest->backing_count; ) {
        guest_backing *backing = &guest->backings[i];
        if (backing->references) { ++i; continue; }
        if (!guest_backing_retire(guest, backing, error)) return false;
        guest->backing_bytes -= backing->bytes;
        memmove(backing, backing + 1, (--guest->backing_count - i) * sizeof(*backing));
    }
    return true;
}

bool qa_native_guest_protect_range(qa_native_guest *guest, uint64_t base, size_t bytes,
    uint32_t permissions, qa_error *error)
{ return change(guest, base, bytes, permissions, false, error); }

bool qa_native_guest_unmap_range(qa_native_guest *guest, uint64_t base, size_t bytes, qa_error *error)
{ return change(guest, base, bytes, 0, true, error); }
