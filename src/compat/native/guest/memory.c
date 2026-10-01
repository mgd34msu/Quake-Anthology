#include "internal.h"

bool guest_fail(qa_error *error, qa_status code, uint64_t address, const char *message)
{
    qa_error_set(error, code, (size_t)address, "%s", message);
    return false;
}

bool guest_uc(qa_native_guest *guest, uc_err code, qa_error *error)
{
    if (code == UC_ERR_OK) return true;
    guest->failed = true;
    qa_error_set(error, code == UC_ERR_NOMEM ? QA_ERROR_MEMORY : QA_ERROR_FORMAT,
        (size_t)code, "native guest CPU: %s", uc_strerror(code));
    return false;
}

bool guest_grow(void **owner, size_t *capacity, size_t count, size_t stride, qa_error *error)
{
    if (count <= *capacity) return true;
    if (count > SIZE_MAX / stride)
        return guest_fail(error, QA_ERROR_MEMORY, 0, "native guest table size overflows");
    size_t next = *capacity && *capacity <= SIZE_MAX / 2 ? *capacity * 2 : count;
    if (next < count || next > SIZE_MAX / stride) next = count;
    void *grown = realloc(*owner, next * stride);
    if (!grown) return guest_fail(error, QA_ERROR_MEMORY, 0, "growing native guest owner table");
    *owner = grown; *capacity = next;
    return true;
}

bool guest_ready(const qa_native_guest *guest, qa_error *error)
{
    return (guest && !guest->failed && !guest->stepping) ||
        guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest owner is terminal or CPU is running");
}

bool guest_mutable(const qa_native_guest *guest, qa_error *error)
{
    return guest_ready(guest, error) && ((!guest->restoring && !guest->faulting) ||
        guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest candidate is restoring or draining a fault"));
}

bool qa_native_guest_idle(const qa_native_guest *guest)
{
    return guest && !guest->run && !guest->callback_depth && !guest->publication_depth &&
        !guest->stepping && !guest->restoring && !guest->failed && !guest->faulting;
}

bool qa_native_guest_create(const qa_native_guest_options *options, qa_native_guest **out,
    qa_error *error)
{
    if (!options || !out || *out || !options->maximum_backing_bytes ||
        !options->allocation_base || options->allocation_base % QA_NATIVE_GUEST_PAGE ||
        !((options->image.target.arch == QA_NATIVE_ARCH_I386 && options->image.target.pointer_bytes == 4 &&
            (options->image.target.abi == QA_NATIVE_ABI_CDECL_I386 ||
             options->image.target.abi == QA_NATIVE_ABI_SYSTEM_V_I386)) ||
          (options->image.target.arch == QA_NATIVE_ARCH_X86_64 && options->image.target.pointer_bytes == 8 &&
            (options->image.target.abi == QA_NATIVE_ABI_MICROSOFT_X64 ||
             options->image.target.abi == QA_NATIVE_ABI_SYSTEM_V_X64))) ||
        (options->image.target.pointer_bytes == 4 && options->allocation_base > UINT32_MAX) ||
        !((options->image.target.os == QA_NATIVE_OS_WINDOWS &&
            ((options->image.format == QA_NATIVE_IMAGE_PE32 && options->image.target.abi == QA_NATIVE_ABI_CDECL_I386) ||
             (options->image.format == QA_NATIVE_IMAGE_PE32_PLUS && options->image.target.abi == QA_NATIVE_ABI_MICROSOFT_X64))) ||
          (options->image.target.os == QA_NATIVE_OS_LINUX &&
            ((options->image.format == QA_NATIVE_IMAGE_ELF32 && options->image.target.abi == QA_NATIVE_ABI_SYSTEM_V_I386) ||
             (options->image.format == QA_NATIVE_IMAGE_ELF64 && options->image.target.abi == QA_NATIVE_ABI_SYSTEM_V_X64)))))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest requires an actual x86 ABI and memory bounds");
    qa_native_guest *guest = calloc(1, sizeof(*guest));
    if (!guest) return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating native guest owner");
    guest->options = *options;
    guest->next_mapping = guest->next_backing = 1;
    guest->allocation_cursor = options->allocation_base;
    if (!guest_cpu_open(guest, error)) {
        if (guest->cpu && uc_close(guest->cpu) != UC_ERR_OK) {
            guest->failed = true;
            *out = guest;
            return false;
        }
        free(guest);
        return false;
    }
    *out = guest;
    return true;
}

bool qa_native_guest_destroy(qa_native_guest **owner, qa_error *error)
{
    if (!owner) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest owner is required");
    qa_native_guest *guest = *owner;
    if (!guest) return true;
    if (guest->run || guest->callback_depth || guest->publication_depth || guest->stepping)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest destruction requires drained execution");
    uc_err code = uc_close(guest->cpu);
    if (code != UC_ERR_OK) return guest_uc(guest, code, error);
    for (size_t i = 0; i < guest->backing_count; ++i) free(guest->backings[i].data);
    free(guest->backings); free(guest->mappings); free(guest->allocations); free(guest->callbacks);
    free(guest); *owner = NULL;
    return true;
}

guest_backing *guest_backing_at(const qa_native_guest *guest, uint64_t id)
{
    for (size_t i = 0; i < guest->backing_count; ++i)
        if (guest->backings[i].id == id) return &guest->backings[i];
    return NULL;
}

qa_native_guest_mapping *guest_mapping(const qa_native_guest *guest, uint64_t address)
{
    for (size_t i = 0; i < guest->mapping_count; ++i) {
        qa_native_guest_mapping *mapping = &guest->mappings[i];
        if (address >= mapping->base && address - mapping->base < mapping->bytes) return mapping;
    }
    return NULL;
}

static bool address_range(const qa_native_guest *guest, uint64_t base, uint64_t bytes,
    qa_error *error)
{
    uint64_t limit = guest->options.image.target.pointer_bytes == 4 ? UINT64_C(0x100000000) : UINT64_MAX;
    return (base && base < limit && bytes <= limit - base) ||
        guest_fail(error, QA_ERROR_ARGUMENT, base, "native guest address extent exceeds its ABI");
}

bool guest_range(const qa_native_guest *guest, uint64_t base, size_t bytes,
    uint32_t permissions, qa_error *error)
{
    if (!address_range(guest, base, bytes, error)) return false;
    size_t offset = 0;
    while (offset < bytes) {
        qa_native_guest_mapping *mapping = guest_mapping(guest, base + offset);
        if (!mapping || (mapping->permissions & permissions) != permissions)
            return guest_fail(error, QA_ERROR_ARGUMENT, base + offset, "native guest memory is unavailable for this access");
        uint64_t available = mapping->bytes - (base + offset - mapping->base);
        size_t amount = available > bytes - offset ? bytes - offset : (size_t)available;
        offset += amount;
    }
    return true;
}

bool guest_install_mapping(qa_native_guest *guest, const qa_native_guest_mapping *mapping,
    qa_error *error)
{
    guest_backing *backing = guest_backing_at(guest, mapping->backing);
    if (!mapping->id || !mapping->bytes || mapping->base % QA_NATIVE_GUEST_PAGE ||
        mapping->bytes % QA_NATIVE_GUEST_PAGE || mapping->backing_offset % QA_NATIVE_GUEST_PAGE ||
        mapping->permissions > 7 || !backing || mapping->backing_offset > backing->bytes ||
        mapping->bytes > backing->bytes - mapping->backing_offset ||
        !address_range(guest, mapping->base, mapping->bytes, error))
        return guest_fail(error, QA_ERROR_FORMAT, mapping->base, "native guest mapping has invalid bounds or backing");
    for (size_t i = 0; i < guest->mapping_count; ++i) {
        const qa_native_guest_mapping *prior = &guest->mappings[i];
        if (prior->id == mapping->id || (mapping->base < prior->base + prior->bytes &&
            prior->base < mapping->base + mapping->bytes))
            return guest_fail(error, QA_ERROR_FORMAT, mapping->base, "native guest mappings overlap or reuse identity");
    }
    if (!guest_grow((void **)&guest->mappings, &guest->mapping_capacity,
        guest->mapping_count + 1, sizeof(*guest->mappings), error)) return false;
    if (!guest_uc(guest, uc_mem_map_ptr(guest->cpu, mapping->base, mapping->bytes,
        mapping->permissions, backing->data + mapping->backing_offset), error)) return false;
    guest->mappings[guest->mapping_count++] = *mapping;
    ++backing->references;
    return true;
}

bool qa_native_guest_map(qa_native_guest *guest, uint64_t base, size_t bytes,
    uint32_t permissions, qa_bytes initial, qa_native_guest_mapping *out, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!out || (!initial.data && initial.size) || initial.size > bytes || !bytes ||
        bytes % QA_NATIVE_GUEST_PAGE || base % QA_NATIVE_GUEST_PAGE || permissions > 7 ||
        bytes > guest->options.maximum_backing_bytes - guest->backing_bytes ||
        guest->next_mapping == UINT64_MAX || guest->next_backing == UINT64_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "native guest mapping exceeds its admitted storage");
    if (!guest_grow((void **)&guest->backings, &guest->backing_capacity,
        guest->backing_count + 1, sizeof(*guest->backings), error)) return false;
    uint8_t *data = calloc(1, bytes);
    if (!data) return guest_fail(error, QA_ERROR_MEMORY, base, "allocating native guest backing");
    if (initial.size) memcpy(data, initial.data, initial.size);
    uint64_t backing_id = guest->next_backing;
    guest->backings[guest->backing_count++] = (guest_backing){backing_id, data, bytes, 0};
    qa_native_guest_mapping mapping = {guest->next_mapping, base, bytes, backing_id, 0, permissions};
    if (!guest_install_mapping(guest, &mapping, error)) {
        --guest->backing_count; free(data);
        return false;
    }
    guest->backing_bytes += bytes;
    ++guest->next_mapping; ++guest->next_backing;
    *out = mapping;
    return true;
}

bool qa_native_guest_alias(qa_native_guest *guest, uint64_t base, size_t bytes,
    uint32_t permissions, uint64_t backing, size_t offset, qa_native_guest_mapping *out,
    qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!out || guest->next_mapping == UINT64_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "native guest alias requires an available mapping identity");
    qa_native_guest_mapping mapping = {guest->next_mapping, base, bytes, backing, offset, permissions};
    if (!guest_install_mapping(guest, &mapping, error)) return false;
    ++guest->next_mapping; *out = mapping;
    return true;
}

bool qa_native_guest_unmap(qa_native_guest *guest, uint64_t id, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    size_t index = 0;
    while (index < guest->mapping_count && guest->mappings[index].id != id) ++index;
    if (index == guest->mapping_count)
        return guest_fail(error, QA_ERROR_NOT_FOUND, id, "native guest mapping is absent");
    qa_native_guest_mapping mapping = guest->mappings[index];
    for (size_t i = 0; i < guest->callback_count; ++i)
        if (guest->callbacks[i].address >= mapping.base && guest->callbacks[i].address - mapping.base < mapping.bytes)
            return guest_fail(error, QA_ERROR_ARGUMENT, id, "retire native guest callbacks before unmapping their traps");
    for (size_t i = 0; i < guest->allocation_count; ++i)
        if (guest->allocations[i].mapping == id)
            return guest_fail(error, QA_ERROR_ARGUMENT, id, "free the real allocation before unmapping its storage");
    if (!guest_uc(guest, uc_mem_unmap(guest->cpu, mapping.base, mapping.bytes), error)) return false;
    memmove(guest->mappings + index, guest->mappings + index + 1,
        (--guest->mapping_count - index) * sizeof(*guest->mappings));
    guest_backing *backing = guest_backing_at(guest, mapping.backing);
    if (!--backing->references) {
        size_t backing_index = (size_t)(backing - guest->backings);
        guest->backing_bytes -= backing->bytes; free(backing->data);
        memmove(backing, backing + 1, (--guest->backing_count - backing_index) * sizeof(*backing));
    }
    return true;
}

bool qa_native_guest_protect(qa_native_guest *guest, uint64_t id, uint32_t permissions, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    for (size_t i = 0; i < guest->mapping_count; ++i) if (guest->mappings[i].id == id) {
        qa_native_guest_mapping *mapping = &guest->mappings[i];
        if (permissions > 7) return guest_fail(error, QA_ERROR_ARGUMENT, id, "invalid native guest permissions");
        if (!(permissions & QA_NATIVE_GUEST_EXECUTE))
            for (size_t j = 0; j < guest->callback_count; ++j)
                if (guest->callbacks[j].address >= mapping->base &&
                    guest->callbacks[j].address - mapping->base < mapping->bytes)
                    return guest_fail(error, QA_ERROR_ARGUMENT, id, "bound native guest trap requires executable storage");
        if (!guest_uc(guest, uc_mem_protect(guest->cpu, mapping->base, mapping->bytes, permissions), error)) return false;
        mapping->permissions = permissions;
        return true;
    }
    return guest_fail(error, QA_ERROR_NOT_FOUND, id, "native guest mapping is absent");
}

size_t qa_native_guest_mapping_count(const qa_native_guest *guest)
{ return guest ? guest->mapping_count : 0; }

bool qa_native_guest_mapping_at(const qa_native_guest *guest, size_t index,
    qa_native_guest_mapping *out, qa_error *error)
{
    if (!guest || !out || index >= guest->mapping_count)
        return guest_fail(error, QA_ERROR_ARGUMENT, index, "native guest mapping index is invalid");
    *out = guest->mappings[index];
    return true;
}

bool qa_native_guest_read(const qa_native_guest *guest, uint64_t address, void *out,
    size_t bytes, qa_error *error)
{
    if (!guest_ready(guest, error)) return false;
    if (!out && bytes) return guest_fail(error, QA_ERROR_ARGUMENT, address, "native guest read output is required");
    if (!guest_range(guest, address, bytes, QA_NATIVE_GUEST_READ, error)) return false;
    size_t offset = 0;
    while (offset < bytes) {
        qa_native_guest_mapping mapping = *guest_mapping(guest, address + offset);
        size_t displacement = (size_t)(address + offset - mapping.base);
        size_t amount = bytes - offset;
        if (amount > mapping.bytes - displacement) amount = (size_t)mapping.bytes - displacement;
        guest_backing *backing = guest_backing_at(guest, mapping.backing);
        memcpy((uint8_t *)out + offset, backing->data + mapping.backing_offset + displacement, amount);
        offset += amount;
    }
    return true;
}

bool guest_publish(qa_native_guest *guest, const qa_native_guest_commit *commit, qa_error *error)
{
    if (!guest_uc(guest, uc_ctl_flush_tb(guest->cpu), error)) return false;
    if (!guest->observe) return true;
    if (guest->publication_depth == UINT_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, commit->address, "native guest publication depth exhausted");
    ++guest->publication_depth;
    bool okay = guest->observe(guest->observe_context, guest, commit, error);
    --guest->publication_depth;
    if (okay && guest->failed)
        okay = guest_fail(error, QA_ERROR_ARGUMENT, commit->address,
            "native guest observer cannot recover a terminal owner");
    if (!okay) guest->failed = true;
    return okay;
}

bool qa_native_guest_write(qa_native_guest *guest, uint64_t address, qa_bytes bytes,
    qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!bytes.data && bytes.size) return guest_fail(error, QA_ERROR_ARGUMENT, address, "native guest write bytes are required");
    if (!guest_range(guest, address, bytes.size, QA_NATIVE_GUEST_WRITE, error)) return false;
    /* Input may be an alias of source storage. Keep it through effectful observers. */
    uint8_t *copy = bytes.size ? malloc(bytes.size) : NULL;
    if (bytes.size && !copy) return guest_fail(error, QA_ERROR_MEMORY, address, "retaining native guest write input");
    if (bytes.size) memcpy(copy, bytes.data, bytes.size);
    size_t offset = 0; bool okay = true;
    while (okay && offset < bytes.size) {
        if (!guest_range(guest, address + offset, bytes.size - offset, QA_NATIVE_GUEST_WRITE, error)) { okay = false; break; }
        qa_native_guest_mapping mapping = *guest_mapping(guest, address + offset);
        size_t displacement = (size_t)(address + offset - mapping.base), amount = bytes.size - offset;
        if (amount > mapping.bytes - displacement) amount = (size_t)mapping.bytes - displacement;
        guest_backing *backing = guest_backing_at(guest, mapping.backing);
        memcpy(backing->data + mapping.backing_offset + displacement, copy + offset, amount);
        qa_native_guest_commit commit = {address + offset, mapping.backing, mapping.backing_offset + displacement, amount};
        offset += amount;
        okay = guest_publish(guest, &commit, error);
    }
    free(copy);
    return okay;
}

bool qa_native_guest_allocate(qa_native_guest *guest, size_t bytes, int32_t tag,
    uint64_t *out, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    size_t actual = bytes ? bytes : 1;
    if (!out || actual > SIZE_MAX - (QA_NATIVE_GUEST_PAGE - 1))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest allocation size overflows");
    size_t rounded = (actual + QA_NATIVE_GUEST_PAGE - 1) & ~(size_t)(QA_NATIVE_GUEST_PAGE - 1);
    uint64_t address = guest->allocation_cursor;
    for (;;) {
        if (!address_range(guest, address, rounded, error)) return false;
        uint64_t next = address;
        for (size_t i = 0; i < guest->mapping_count; ++i) {
            qa_native_guest_mapping *mapping = &guest->mappings[i];
            if (address < mapping->base + mapping->bytes && mapping->base < address + rounded &&
                next < mapping->base + mapping->bytes) next = mapping->base + mapping->bytes;
        }
        if (next == address) break;
        address = next;
    }
    if (!guest_grow((void **)&guest->allocations, &guest->allocation_capacity,
        guest->allocation_count + 1, sizeof(*guest->allocations), error)) return false;
    qa_native_guest_mapping mapping;
    if (!qa_native_guest_map(guest, address, rounded, QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE,
        (qa_bytes){0}, &mapping, error)) return false;
    guest->allocations[guest->allocation_count++] = (guest_allocation){address, mapping.id, actual, tag};
    guest->allocation_cursor = address + rounded;
    *out = address;
    return true;
}

bool qa_native_guest_free(qa_native_guest *guest, uint64_t address, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!address) return true;
    size_t index = 0;
    while (index < guest->allocation_count && guest->allocations[index].address != address) ++index;
    if (index == guest->allocation_count)
        return guest_fail(error, QA_ERROR_ARGUMENT, address, "native guest free requires the real allocation base");
    guest_allocation allocation = guest->allocations[index];
    /* Retain the record if removing its storage is rejected. */
    memmove(guest->allocations + index, guest->allocations + index + 1,
        (--guest->allocation_count - index) * sizeof(*guest->allocations));
    if (!qa_native_guest_unmap(guest, allocation.mapping, error)) {
        memmove(guest->allocations + index + 1, guest->allocations + index,
            (guest->allocation_count - index) * sizeof(*guest->allocations));
        guest->allocations[index] = allocation; ++guest->allocation_count;
        return false;
    }
    return true;
}

bool qa_native_guest_allocation(const qa_native_guest *guest, uint64_t address,
    qa_native_allocation_info *out, qa_error *error)
{
    if (!guest || !out) return guest_fail(error, QA_ERROR_ARGUMENT, address, "native guest allocation output is required");
    for (size_t i = 0; i < guest->allocation_count; ++i) {
        const guest_allocation *allocation = &guest->allocations[i];
        if (address >= allocation->address && address - allocation->address < allocation->bytes) {
            *out = (qa_native_allocation_info){allocation->address, allocation->bytes, allocation->tag};
            return true;
        }
    }
    return guest_fail(error, QA_ERROR_NOT_FOUND, address, "native guest address has no allocator owner");
}
