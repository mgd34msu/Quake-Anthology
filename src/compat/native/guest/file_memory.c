#include "internal.h"
#include "unicorn_state.h"

static size_t accessible(const guest_backing *backing,
    const qa_native_guest_mapping *mapping)
{
    uint64_t end = backing->file ? backing->source.accessible_bytes : backing->bytes;
    if (mapping->backing_offset >= end) return 0;
    uint64_t bytes = end - mapping->backing_offset;
    return (size_t)(bytes < mapping->bytes ? bytes : mapping->bytes);
}

/* The dependency requires each operation to name an entire actual region.
 * A logical file mapping has at most two such regions: accessible pages and
 * EOF pages with no CPU rights. Keep this same division for every change. */
bool guest_backend_map(qa_native_guest *guest,
    const qa_native_guest_mapping *mapping, qa_error *error)
{
    if (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64)
        return guest_native_map(guest, mapping, error);
    guest_backing *backing = guest_backing_at(guest, mapping->backing);
    size_t first = accessible(backing, mapping);
    if (first && !guest_uc(guest, qa_unicorn_memory_map(guest->cpu,
        mapping->base, first, mapping->permissions,
        backing->data + mapping->backing_offset), error)) return false;
    size_t tail = (size_t)mapping->bytes - first;
    return !tail || guest_uc(guest, qa_unicorn_memory_map(guest->cpu,
        mapping->base + first, tail, 0,
        backing->data + mapping->backing_offset + first), error);
}

bool guest_backend_change(qa_native_guest *guest,
    const qa_native_guest_mapping *mapping, uint32_t permissions,
    bool remove, qa_error *error)
{
    if (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64)
        return guest_native_result(guest, guest_host_child_change(guest->child,
            mapping, permissions, remove, error), error);
    guest_backing *backing = guest_backing_at(guest, mapping->backing);
    size_t first = accessible(backing, mapping);
    if (first && !guest_uc(guest, qa_unicorn_memory_change(guest->cpu,
        mapping->base, first, permissions, remove), error)) return false;
    size_t tail = (size_t)mapping->bytes - first;
    return !tail || guest_uc(guest, qa_unicorn_memory_change(guest->cpu,
        mapping->base + first, tail, 0, remove), error);
}

bool guest_file_access(const qa_native_guest *guest,
    const qa_native_guest_mapping *mapping, uint64_t address,
    size_t bytes, qa_error *error)
{
    guest_backing *backing = guest_backing_at(guest, mapping->backing);
    if (!backing->file || !bytes) return true;
    uint64_t offset = mapping->backing_offset + address - mapping->base;
    uint64_t end = backing->source.accessible_bytes;
    if (offset < end && bytes <= end - offset) return true;
    uint64_t fault = offset >= end ? address : address + end - offset;
    return guest_fail(error, QA_ERROR_FORMAT, fault,
        "native guest file mapping access faults beyond EOF");
}

bool qa_native_guest_file_backing(const qa_native_guest *guest, uint64_t id,
    qa_native_guest_file *out, qa_error *error)
{
    if (!guest || !out)
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "native file backing output is required");
    guest_backing *backing = guest_backing_at(guest, id);
    if (!backing || !backing->file)
        return guest_fail(error, QA_ERROR_NOT_FOUND, id, "native backing is not an actual private file view");
    *out = backing->source;
    return true;
}

bool qa_native_guest_last_fault(const qa_native_guest *guest,
    qa_native_guest_fault *out)
{
    if (!guest || !out || !guest->has_memory_fault) return false;
    *out = guest->memory_fault;
    return true;
}

bool qa_native_guest_map_file(qa_native_guest *guest, uint64_t base, size_t bytes,
    uint32_t permissions, qa_bytes file, uint64_t offset,
    qa_native_guest_mapping *out, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!out || (!file.data && file.size) || !bytes ||
        bytes % QA_NATIVE_GUEST_PAGE || base % QA_NATIVE_GUEST_PAGE ||
        offset % QA_NATIVE_GUEST_PAGE || permissions > 7 ||
        bytes > guest->options.maximum_backing_bytes - guest->backing_bytes ||
        guest->next_mapping == UINT64_MAX || guest->next_backing == UINT64_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "native private file view requires bounded aligned pages");
    if (!guest_grow((void **)&guest->backings, &guest->backing_capacity,
        guest->backing_count + 1, sizeof(*guest->backings), error)) return false;
    uint8_t *data = calloc(1, bytes);
    if (!data) return guest_fail(error, QA_ERROR_MEMORY, base, "owning native private file pages");
    uint64_t available = offset < file.size ? file.size - offset : 0;
    size_t copied = available > bytes ? bytes : (size_t)available;
    if (copied) memcpy(data, file.data + (size_t)offset, copied);
    size_t admitted = copied;
    if (admitted % QA_NATIVE_GUEST_PAGE)
        admitted += QA_NATIVE_GUEST_PAGE - admitted % QA_NATIVE_GUEST_PAGE;
    guest_backing backing = {.id = guest->next_backing, .data = data,
        .bytes = bytes, .file = true,
        .source = {.bytes = file.size, .offset = offset, .accessible_bytes = admitted}};
    qa_sha256(file, &backing.source.digest);
    guest->backings[guest->backing_count++] = backing;
    guest->backing_bytes += bytes;
    qa_native_guest_mapping mapping = {guest->next_mapping, base, bytes,
        backing.id, 0, permissions};
    if (!guest_install_mapping(guest, &mapping, error)) {
        /* A prefix may already be registered with the real CPU. Keep every
         * borrowed buffer until whole terminal CPU destruction completes. */
        if (!guest->failed) {
            --guest->backing_count; guest->backing_bytes -= bytes; free(data);
        }
        return false;
    }
    ++guest->next_mapping; ++guest->next_backing;
    *out = mapping;
    return true;
}
