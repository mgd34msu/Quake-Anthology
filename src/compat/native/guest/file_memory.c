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

bool guest_file_prepare(qa_native_guest *guest, uint64_t base, size_t bytes,
    uint32_t permissions, const qa_source_save_memory_source *file, size_t file_bytes,
    uint64_t offset, uint64_t capability, guest_backing *out, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!file || !out || out->data || (!file->read &&
        (file->bytes.size != file_bytes || (!file->bytes.data && file_bytes))) || !bytes ||
        bytes % QA_NATIVE_GUEST_PAGE || base % QA_NATIVE_GUEST_PAGE ||
        offset % QA_NATIVE_GUEST_PAGE || permissions > 7 ||
        bytes > guest->options.maximum_backing_bytes ||
        guest->next_mapping == UINT64_MAX || guest->next_backing == UINT64_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "native private file view requires bounded aligned pages");
    uint8_t *data = calloc(1, bytes);
    if (!data) return guest_fail(error, QA_ERROR_MEMORY, base, "owning native private file pages");
    uint64_t available = offset < file_bytes ? file_bytes - offset : 0;
    size_t copied = available > bytes ? bytes : (size_t)available;
    qa_sha256_digest digest;
    if (file->read) {
        uint8_t scratch[65536]; qa_sha256_context hash;
        qa_sha256_init(&hash);
        for (size_t position = 0; position < file_bytes;) {
            size_t amount = file_bytes - position;
            if (amount > sizeof(scratch)) amount = sizeof(scratch);
            if (!file->read(file->context, position, scratch, amount, error)) {
                free(data); return false;
            }
            qa_sha256_update(&hash, (qa_bytes){scratch, amount});
            uint64_t first = position > offset ? position : offset;
            uint64_t end = position + amount;
            if (end > offset && end - offset > copied) end = offset + copied;
            if (first < end) memcpy(data + (size_t)(first - offset),
                scratch + (size_t)(first - position), (size_t)(end - first));
            position += amount;
        }
        qa_sha256_final(&hash, &digest);
    } else {
        if (copied) memcpy(data, file->bytes.data + (size_t)offset, copied);
        qa_sha256(file->bytes, &digest);
    }
    size_t admitted = copied;
    if (admitted % QA_NATIVE_GUEST_PAGE)
        admitted += QA_NATIVE_GUEST_PAGE - admitted % QA_NATIVE_GUEST_PAGE;
    *out = (guest_backing){.data = data, .bytes = bytes, .file = true,
        .source = {.digest = digest, .bytes = file_bytes, .offset = offset,
            .accessible_bytes = admitted, .capability = capability}};
    return true;
}

bool guest_file_publish(qa_native_guest *guest, uint64_t base, uint32_t permissions,
    guest_backing *prepared, qa_native_guest_mapping *out, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!prepared || !prepared->data || !out ||
        prepared->bytes > guest->options.maximum_backing_bytes - guest->backing_bytes ||
        guest->next_mapping == UINT64_MAX || guest->next_backing == UINT64_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "native file publication requires its prepared backing");
    if (!guest_grow((void **)&guest->backings, &guest->backing_capacity,
        guest->backing_count + 1, sizeof(*guest->backings), error)) return false;
    prepared->id = guest->next_backing;
    guest_backing backing = *prepared;
    guest->backings[guest->backing_count++] = backing;
    guest->backing_bytes += backing.bytes;
    *prepared = (guest_backing){0};
    qa_native_guest_mapping mapping = {guest->next_mapping, base, backing.bytes,
        backing.id, 0, permissions};
    if (!guest_install_mapping(guest, &mapping, error)) {
        /* A prefix may already belong to the real CPU. Keep its buffer until
         * whole terminal CPU destruction completes. */
        if (!guest->failed) {
            --guest->backing_count; guest->backing_bytes -= backing.bytes; free(backing.data);
        }
        return false;
    }
    ++guest->next_mapping; ++guest->next_backing;
    *out = mapping;
    return true;
}

bool qa_native_guest_map_file_source(qa_native_guest *guest, uint64_t base, size_t bytes,
    uint32_t permissions, const qa_source_save_memory_source *file, size_t file_bytes,
    uint64_t offset, uint64_t capability, qa_native_guest_mapping *out, qa_error *error)
{
    if (!out || !guest || bytes > guest->options.maximum_backing_bytes - guest->backing_bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "native file view requires its actual output and remaining backing budget");
    guest_backing prepared = {0};
    bool okay = guest_file_prepare(guest, base, bytes, permissions, file, file_bytes,
        offset, capability, &prepared, error) &&
        guest_file_publish(guest, base, permissions, &prepared, out, error);
    free(prepared.data);
    return okay;
}

bool qa_native_guest_map_file(qa_native_guest *guest, uint64_t base, size_t bytes,
    uint32_t permissions, qa_bytes file, uint64_t offset,
    qa_native_guest_mapping *out, qa_error *error)
{
    const qa_source_save_memory_source source = {.bytes = file};
    return qa_native_guest_map_file_source(guest, base, bytes, permissions, &source,
        file.size, offset, 0, out, error);
}
