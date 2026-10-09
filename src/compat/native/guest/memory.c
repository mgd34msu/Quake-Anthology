#include "internal.h"
#include "unicorn_state.h"

typedef struct guest_heap_chunk {
    uint64_t base, backing;
    uint8_t *data;
    size_t bytes, live;
} guest_heap_chunk;
typedef struct guest_heap_span {
    uint64_t address;
    size_t bytes, chunk;
} guest_heap_span;
struct guest_heap {
    guest_heap_chunk *chunks;
    size_t chunk_count, chunk_capacity;
    guest_heap_span *spans;
    size_t span_count, span_capacity;
    bool dirty;
};
static void heap_destroy(struct guest_heap *);

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

bool qa_native_guest_terminal(const qa_native_guest *guest)
{ return guest && guest->failed; }

bool qa_native_guest_idle(const qa_native_guest *guest)
{
    return guest && !guest->run && !guest->callback_depth && !guest->publication_depth &&
        !guest->stopped_write_calls && !guest->stopped_write_bindings && !guest->recovery &&
        !guest->stepping && !guest->restoring && !guest->failed && !guest->faulting;
}

bool guest_create(const qa_native_guest_options *options, bool fresh,
    qa_native_guest **out, qa_error *error)
{
    if (!options || !out || *out || !options->maximum_backing_bytes ||
        (options->backend != QA_NATIVE_GUEST_EMULATED && options->backend != QA_NATIVE_GUEST_HOST_X86_64) ||
        (options->backend == QA_NATIVE_GUEST_HOST_X86_64 &&
            (!options->host_executable || !*options->host_executable || !options->profile_guard ||
             options->image.target.arch != QA_NATIVE_ARCH_X86_64)) ||
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
    if (!guest_cpu_open(guest, fresh, error)) {
        guest->options.host_executable = NULL;
        guest->options.profile_guard = NULL;
        if (guest->child && !guest_host_child_destroy(&guest->child, error)) {
            guest->failed = true; *out = guest; return false;
        }
        if (guest->cpu && uc_close(guest->cpu) != UC_ERR_OK) {
            guest->failed = true;
            *out = guest;
            return false;
        }
        free(guest);
        return false;
    }
    guest->options.host_executable = NULL;
    guest->options.profile_guard = NULL;
    *out = guest;
    return true;
}

bool qa_native_guest_create(const qa_native_guest_options *options,
    qa_native_guest **out, qa_error *error)
{ return guest_create(options, true, out, error); }

bool qa_native_guest_destroy(qa_native_guest **owner, qa_error *error)
{
    if (!owner) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest owner is required");
    qa_native_guest *guest = *owner;
    if (!guest) return true;
    if (guest->run || guest->callback_depth || guest->publication_depth || guest->stepping ||
        guest->stopped_write_calls || guest->stopped_write_bindings || guest->recovery)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest destruction requires drained execution");
    if (guest->memory_retiring) guest->memory_retiring(guest->memory_context, 0, 0);
    if (guest->child) {
        if (!guest_host_child_destroy(&guest->child, error)) { guest->failed = true; return false; }
    } else if (guest->cpu) {
        uc_err code = qa_unicorn_memory_release(guest->cpu);
        if (code == UC_ERR_OK) code = uc_close(guest->cpu);
        if (code != UC_ERR_OK) return guest_uc(guest, code, error);
    }
    for (size_t i = 0; i < guest->backing_count; ++i)
        if (!guest->backings[i].child_owned) free(guest->backings[i].data);
    heap_destroy(guest->heap);
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
        if (permissions && !guest_file_access(guest, mapping, base + offset, amount, error)) return false;
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
    if (!guest_backend_map(guest, mapping, error)) return false;
    guest->mappings[guest->mapping_count++] = *mapping;
    ++backing->references;
    guest_heap_changed(guest, mapping->base, (size_t)mapping->bytes);
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
    guest->backings[guest->backing_count++] = (guest_backing){.id = backing_id, .data = data, .bytes = bytes};
    qa_native_guest_mapping mapping = {guest->next_mapping, base, bytes, backing_id, 0, permissions};
    if (!guest_install_mapping(guest, &mapping, error)) {
        if (guest->failed) guest->backing_bytes += bytes;
        else { --guest->backing_count; free(data); }
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
    for (size_t i = 0; i < guest->allocation_count; ++i) {
        const guest_allocation *allocation = &guest->allocations[i];
        size_t rounded = guest_allocation_extent(allocation);
        if (mapping.base >= allocation->address + rounded ||
            allocation->address >= mapping.base + mapping.bytes) continue;
        if (!guest_allocation_storage(guest, allocation, NULL, NULL, error)) return false;
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "free the real allocation before unmapping its storage");
    }
    if (guest->memory_retiring)
        guest->memory_retiring(guest->memory_context, mapping.base, (size_t)mapping.bytes);
    if (!guest_backend_change(guest, &mapping, 0, true, error)) return false;
    guest_heap_changed(guest, mapping.base, (size_t)mapping.bytes);
    memmove(guest->mappings + index, guest->mappings + index + 1,
        (--guest->mapping_count - index) * sizeof(*guest->mappings));
    guest_backing *backing = guest_backing_at(guest, mapping.backing);
    if (!--backing->references) {
        size_t backing_index = (size_t)(backing - guest->backings);
        if (!guest_backing_retire(guest, backing, error)) return false;
        guest->backing_bytes -= backing->bytes;
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
        if (mapping->permissions != permissions && guest->memory_retiring)
            guest->memory_retiring(guest->memory_context, mapping->base, (size_t)mapping->bytes);
        if (!guest_backend_change(guest, mapping, permissions, false, error)) return false;
        mapping->permissions = permissions;
        guest_heap_changed(guest, mapping->base, (size_t)mapping->bytes);
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

bool guest_callback_cancelled(const qa_native_guest *guest,const qa_error *error)
{
    if(!guest||guest->failed||!error)return false;
    for(const guest_callback_recovery *r=guest->recovery;r;r=r->previous)
        if(r->cancelled&&!r->restored&&!r->resolved&&error->code==r->failure.code&&
            error->offset==r->failure.offset&&!strcmp(error->message,r->failure.message))return true;
    return false;
}
bool guest_call_prepared(const qa_native_guest *guest)
{
    const guest_callback_recovery *r=guest?guest->recovery:NULL;
    return r&&r->invocation&&!r->resolved&&!r->restored&&!r->cancelled&&
        !guest->run&&!guest->callback_depth&&!guest->publication_depth&&!guest->stopped_write_calls&&
        !guest->failed&&!guest->faulting&&!guest->stepping&&!guest->restoring;
}
bool guest_callback_failure(qa_native_guest *guest,const qa_error *error)
{
    if(!guest||!error||guest->failed||guest->faulting||guest->stepping)return false;
    if(guest_callback_cancelled(guest,error))return true;
    for(guest_callback_recovery *r=guest->recovery;r;r=r->previous) {
        if(!r->invocation||r->restored||r->resolved||r->cancelled)continue;
        if(!r->accepts(r->context,error))continue;
        if(guest->failed||guest->faulting||guest->stepping)return false;
        r->failure=*error;r->cancelled=true;return true;
    }
    return false;
}
bool guest_publish(qa_native_guest *guest, const qa_native_guest_commit *commit, qa_error *error)
{
    if (guest->options.backend == QA_NATIVE_GUEST_EMULATED &&
        !guest_uc(guest, uc_ctl_flush_tb(guest->cpu), error)) return false;
    if (!guest->observe) return true;
    if (guest->publication_depth == UINT_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, commit->address, "native guest publication depth exhausted");
    ++guest->publication_depth;
    bool okay = guest->observe(guest->observe_context, guest, commit, error);
    --guest->publication_depth;
    if (okay && guest->failed)
        okay = guest_fail(error, QA_ERROR_ARGUMENT, commit->address,
            "native guest observer cannot recover a terminal owner");
    if (!okay && !guest_callback_failure(guest,error)) guest->failed = true;
    return okay;
}

bool qa_native_guest_write(qa_native_guest *guest, uint64_t address, qa_bytes bytes,
    qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!bytes.data && bytes.size) return guest_fail(error, QA_ERROR_ARGUMENT, address, "native guest write bytes are required");
    if (!guest_range(guest, address, bytes.size, QA_NATIVE_GUEST_WRITE, error)) return false;
    qa_native_guest_cpu cpu = {0};
    if (guest->observe && !qa_native_guest_cpu_read(guest, &cpu, error)) return false;
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
        qa_native_guest_commit commit = {address + offset, mapping.backing, mapping.backing_offset + displacement,
            amount, cpu.instruction, offset + amount == bytes.size};
        offset += amount;
        okay = guest_publish(guest, &commit, error);
    }
    free(copy);
    return okay;
}

bool qa_native_guest_allocate_aligned(qa_native_guest *guest, size_t bytes, size_t alignment,
    uint32_t permissions, int32_t tag, uint64_t *out, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    size_t actual = bytes ? bytes : 1;
    if (!out || !alignment || (alignment & (alignment - 1)) || permissions > 7 ||
        actual > SIZE_MAX - (QA_NATIVE_GUEST_PAGE - 1))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest allocation needs power-of-two alignment, actual permissions and bounded size");
    size_t rounded = (actual + QA_NATIVE_GUEST_PAGE - 1) & ~(size_t)(QA_NATIVE_GUEST_PAGE - 1);
    if (alignment < QA_NATIVE_GUEST_PAGE) alignment = QA_NATIVE_GUEST_PAGE;
    uint64_t address = guest->allocation_cursor;
    for (;;) {
        if (address > UINT64_MAX - (alignment - 1))
            return guest_fail(error, QA_ERROR_MEMORY, address, "native guest allocation alignment exceeds its address domain");
        address = (address + alignment - 1) & ~((uint64_t)alignment - 1);
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
    if (!qa_native_guest_map(guest, address, rounded, permissions,
        (qa_bytes){0}, &mapping, error)) return false;
    guest->allocations[guest->allocation_count++] = (guest_allocation){address, mapping.id, actual, tag};
    guest->allocation_cursor = address + rounded;
    *out = address;
    return true;
}

static void heap_destroy(struct guest_heap *heap)
{
    if (!heap) return;
    free(heap->chunks); free(heap->spans); free(heap);
}

static bool heap_open(qa_native_guest *guest, qa_error *error)
{
    if (guest->heap) return true;
    guest->heap = calloc(1, sizeof(*guest->heap));
    return guest->heap || guest_fail(error, QA_ERROR_MEMORY, 0, "allocating native byte heap metadata");
}

void guest_heap_changed(qa_native_guest *guest, uint64_t base, size_t bytes)
{
    struct guest_heap *heap = guest->heap;
    if (!heap) return;
    for (size_t i = 0; i < heap->chunk_count; ++i) {
        const guest_heap_chunk *chunk = heap->chunks + i;
        if (chunk->backing && base < chunk->base + chunk->bytes && chunk->base < base + bytes) {
            heap->dirty = true; return;
        }
    }
}

static int span_compare(const void *left, const void *right)
{
    uint64_t a = ((const guest_heap_span *)left)->address;
    uint64_t b = ((const guest_heap_span *)right)->address;
    return (a > b) - (a < b);
}

static bool heap_rebuild(qa_native_guest *guest, qa_error *error)
{
    struct guest_heap *heap = guest->heap;
    heap->span_count = 0;
    for (size_t c = 0; c < heap->chunk_count; ++c) {
        const guest_heap_chunk *chunk = heap->chunks + c;
        if (!chunk->backing) continue;
        for (size_t i = 0; i < guest->mapping_count; ++i) {
            const qa_native_guest_mapping *mapping = guest->mappings + i;
            if (mapping->backing != chunk->backing || mapping->base < chunk->base ||
                mapping->base - chunk->base != mapping->backing_offset ||
                (mapping->permissions & (QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE)) !=
                    (QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE)) continue;
            if (!guest_grow((void **)&heap->spans, &heap->span_capacity,
                heap->span_count + 1, sizeof(*heap->spans), error)) return false;
            heap->spans[heap->span_count++] = (guest_heap_span){mapping->base, (size_t)mapping->bytes, c};
        }
    }
    for (size_t a = 0; a < guest->allocation_count; ++a) {
        const guest_allocation *allocation = guest->allocations + a;
        if (allocation->mapping) continue;
        uint64_t first = allocation->address, last = first + guest_allocation_extent(allocation);
        for (size_t i = 0; i < heap->span_count; ++i) {
            guest_heap_span span = heap->spans[i];
            uint64_t end = span.address + span.bytes;
            if (first >= end || last <= span.address) continue;
            if (first > span.address && last < end) {
                if (!guest_grow((void **)&heap->spans, &heap->span_capacity,
                    heap->span_count + 1, sizeof(*heap->spans), error)) return false;
                heap->spans[i].bytes = (size_t)(first - span.address);
                heap->spans[heap->span_count++] = (guest_heap_span){last, (size_t)(end - last), span.chunk};
            } else if (first > span.address) heap->spans[i].bytes = (size_t)(first - span.address);
            else if (last < end) {
                heap->spans[i].address = last; heap->spans[i].bytes = (size_t)(end - last);
            } else {
                heap->spans[i] = heap->spans[--heap->span_count]; --i;
            }
        }
    }
    if (heap->span_count > 1) qsort(heap->spans, heap->span_count, sizeof(*heap->spans), span_compare);
    size_t count = 0;
    for (size_t i = 0; i < heap->span_count; ++i) {
        guest_heap_span span = heap->spans[i];
        if (count && heap->spans[count - 1].chunk == span.chunk &&
            heap->spans[count - 1].address + heap->spans[count - 1].bytes == span.address)
            heap->spans[count - 1].bytes += span.bytes;
        else heap->spans[count++] = span;
    }
    heap->span_count = count; heap->dirty = false;
    return true;
}

bool guest_heap_restore(qa_native_guest *guest, qa_error *error)
{
    for (size_t i = 0; i < guest->allocation_count; ++i) {
        const guest_allocation *allocation = guest->allocations + i;
        if (allocation->mapping) continue;
        if (!heap_open(guest, error)) return false;
        qa_native_guest_mapping *mapping = guest_mapping(guest, allocation->address);
        guest_backing *backing = guest_backing_at(guest, mapping->backing);
        if (mapping->base < mapping->backing_offset)
            return guest_fail(error, QA_ERROR_FORMAT, allocation->address, "native byte heap has an invalid root address");
        uint64_t base = mapping->base - mapping->backing_offset;
        if (base < guest->options.allocation_base || base > guest->allocation_cursor ||
            backing->bytes > guest->allocation_cursor - base)
            return guest_fail(error, QA_ERROR_FORMAT, base, "native byte heap backing exceeds its actual allocator cursor");
        struct guest_heap *heap = guest->heap; size_t c = 0;
        while (c < heap->chunk_count && heap->chunks[c].backing != backing->id) ++c;
        if (c < heap->chunk_count && heap->chunks[c].base != base)
            return guest_fail(error, QA_ERROR_FORMAT, allocation->address, "native byte heap allocation uses a different backing alias");
        if (c == heap->chunk_count) {
            if (!guest_grow((void **)&heap->chunks, &heap->chunk_capacity,
                c + 1, sizeof(*heap->chunks), error)) return false;
            heap->chunks[heap->chunk_count++] = (guest_heap_chunk){base, backing->id, backing->data, backing->bytes, 0};
        }
        ++heap->chunks[c].live;
    }
    return !guest->heap || heap_rebuild(guest, error);
}

bool qa_native_guest_allocate(qa_native_guest *guest, size_t bytes, int32_t tag,
    uint64_t *out, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    size_t actual = bytes ? bytes : 1;
    if (!out || actual > SIZE_MAX - 15)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native byte allocation size overflows");
    if (actual > 32768) return qa_native_guest_allocate_aligned(guest, actual, QA_NATIVE_GUEST_PAGE,
        QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, tag, out, error);
    if (!heap_open(guest, error)) return false;
    struct guest_heap *heap = guest->heap;
    if (heap->dirty && !heap_rebuild(guest, error)) return false;
    if (!guest_grow((void **)&guest->allocations, &guest->allocation_capacity,
        guest->allocation_count + 1, sizeof(*guest->allocations), error)) return false;
    size_t rounded = (actual + 15) & ~(size_t)15;
    for (size_t i = 0; i < heap->span_count; ++i) {
        guest_heap_span *span = heap->spans + i;
        if (span->bytes < rounded) continue;
        guest_heap_chunk *chunk = heap->chunks + span->chunk;
        uint64_t address = span->address;
        memset(chunk->data + (size_t)(address - chunk->base), 0, rounded);
        guest->allocations[guest->allocation_count++] = (guest_allocation){address, 0, actual, tag};
        ++chunk->live; span->address += rounded; span->bytes -= rounded;
        if (!span->bytes) {
            memmove(span, span + 1, (heap->span_count - i - 1) * sizeof(*span)); --heap->span_count;
        }
        *out = address; return true;
    }
    size_t c = 0;
    while (c < heap->chunk_count && heap->chunks[c].backing) ++c;
    if (!guest_grow((void **)&heap->chunks, &heap->chunk_capacity,
        c + 1, sizeof(*heap->chunks), error) ||
        !guest_grow((void **)&heap->spans, &heap->span_capacity,
            heap->span_count + 1, sizeof(*heap->spans), error)) return false;
    size_t available = guest->options.maximum_backing_bytes - guest->backing_bytes;
    size_t size = available < 65536 ? available & ~(size_t)(QA_NATIVE_GUEST_PAGE - 1) : 65536;
    if (size < rounded)
        return guest_fail(error, QA_ERROR_MEMORY, 0, "native byte heap exceeds its admitted backing storage");
    uint64_t address = 0;
    if (!qa_native_guest_allocate_aligned(guest, size, QA_NATIVE_GUEST_PAGE,
        QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, tag, &address, error)) return false;
    qa_native_guest_mapping *mapping = guest_mapping(guest, address);
    guest_backing *backing = guest_backing_at(guest, mapping->backing);
    heap->chunks[c] = (guest_heap_chunk){address, backing->id, backing->data, size, 1};
    if (c == heap->chunk_count) ++heap->chunk_count;
    guest->allocations[guest->allocation_count - 1] = (guest_allocation){address, 0, actual, tag};
    if (rounded < size) heap->spans[heap->span_count++] = (guest_heap_span){address + rounded, size - rounded, c};
    *out = address; return true;
}

static void heap_span_free(struct guest_heap *heap, guest_heap_span span)
{
    size_t i = 0;
    while (i < heap->span_count && heap->spans[i].address < span.address) ++i;
    memmove(heap->spans + i + 1, heap->spans + i, (heap->span_count - i) * sizeof(*heap->spans));
    heap->spans[i] = span; ++heap->span_count;
    if (i && heap->spans[i - 1].chunk == span.chunk &&
        heap->spans[i - 1].address + heap->spans[i - 1].bytes == span.address) {
        heap->spans[i - 1].bytes += span.bytes;
        memmove(heap->spans + i, heap->spans + i + 1, (--heap->span_count - i) * sizeof(*heap->spans)); --i;
    }
    if (i + 1 < heap->span_count && heap->spans[i + 1].chunk == span.chunk &&
        heap->spans[i].address + heap->spans[i].bytes == heap->spans[i + 1].address) {
        heap->spans[i].bytes += heap->spans[i + 1].bytes;
        memmove(heap->spans + i + 1, heap->spans + i + 2, (heap->span_count - i - 2) * sizeof(*heap->spans));
        --heap->span_count;
    }
}

static bool heap_chunk_free(qa_native_guest *guest, size_t c, qa_error *error)
{
    struct guest_heap *heap = guest->heap;
    guest_heap_chunk chunk = heap->chunks[c];
    for (size_t i = 0; i < guest->mapping_count; ) {
        qa_native_guest_mapping mapping = guest->mappings[i];
        if (mapping.backing == chunk.backing && mapping.base >= chunk.base &&
            mapping.base - chunk.base == mapping.backing_offset) {
            if (!qa_native_guest_unmap(guest, mapping.id, error)) return false;
        } else ++i;
    }
    size_t count = 0;
    for (size_t i = 0; i < heap->span_count; ++i)
        if (heap->spans[i].chunk != c) heap->spans[count++] = heap->spans[i];
    heap->span_count = count; heap->chunks[c] = (guest_heap_chunk){0};
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
    size_t rounded = 0;
    if (!guest_allocation_storage(guest, &allocation, NULL, &rounded, error)) return false;
    size_t c = 0;
    if (!allocation.mapping) {
        struct guest_heap *heap = guest->heap;
        if (heap->dirty && !heap_rebuild(guest, error)) return false;
        while (c < heap->chunk_count && (!heap->chunks[c].backing || address < heap->chunks[c].base ||
            address - heap->chunks[c].base >= heap->chunks[c].bytes)) ++c;
        if (c == heap->chunk_count)
            return guest_fail(error, QA_ERROR_FORMAT, address, "native byte allocation lost its heap chunk");
        uint64_t base = heap->chunks[c].live == 1 ? heap->chunks[c].base : address;
        size_t length = heap->chunks[c].live == 1 ? heap->chunks[c].bytes : rounded;
        for (size_t i = 0; i < guest->callback_count; ++i)
            if (guest->callbacks[i].address >= base && guest->callbacks[i].address - base < length)
                return guest_fail(error, QA_ERROR_ARGUMENT, address, "retire the actual callback before freeing its storage");
        if (heap->chunks[c].live != 1 && !guest_grow((void **)&heap->spans, &heap->span_capacity,
            heap->span_count + 1, sizeof(*heap->spans), error)) return false;
    }
    /* Retain the record if removing its storage is rejected. */
    if (guest->memory_retiring)
        guest->memory_retiring(guest->memory_context, allocation.address, rounded);
    memmove(guest->allocations + index, guest->allocations + index + 1,
        (--guest->allocation_count - index) * sizeof(*guest->allocations));
    bool okay = allocation.mapping ? qa_native_guest_unmap_range(guest, allocation.address, rounded, error) :
        (guest->heap->chunks[c].live != 1 || heap_chunk_free(guest, c, error));
    if (!okay) {
        memmove(guest->allocations + index + 1, guest->allocations + index,
            (guest->allocation_count - index) * sizeof(*guest->allocations));
        guest->allocations[index] = allocation; ++guest->allocation_count;
        return false;
    }
    if (!allocation.mapping && guest->heap->chunks[c].backing) {
        --guest->heap->chunks[c].live;
        size_t offset = 0;
        while (offset < rounded) {
            const qa_native_guest_mapping *mapping = guest_mapping(guest, address + offset);
            if ((mapping->permissions & (QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE)) !=
                (QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE)) break;
            size_t available = (size_t)(mapping->bytes - (address + offset - mapping->base));
            offset += available < rounded - offset ? available : rounded - offset;
        }
        if (offset == rounded) heap_span_free(guest->heap, (guest_heap_span){address, rounded, c});
        else guest->heap->dirty = true;
    }
    return true;
}

bool guest_allocation_transfer(qa_native_guest *guest, uint64_t address,
    guest_allocation *out, uint64_t *backing, qa_error *error)
{
    if (!out || !backing || !guest_mutable(guest, error)) return false;
    size_t index = 0;
    while (index < guest->allocation_count && guest->allocations[index].address != address) ++index;
    if (index == guest->allocation_count)
        return guest_fail(error, QA_ERROR_ARGUMENT, address, "VM ownership transfer requires the actual allocation base");
    guest_allocation allocation = guest->allocations[index];
    if (!allocation.mapping)
        return guest_fail(error, QA_ERROR_ARGUMENT, address, "VM ownership transfer requires a page allocation");
    uint64_t owned = 0;
    if (!guest_allocation_storage(guest, &allocation, &owned, NULL, error)) return false;
    /* Only the allocator claim moves. Actual RAM, aliases, permissions and
     * the monotonic allocation cursor remain with the same lower owner. */
    memmove(guest->allocations + index, guest->allocations + index + 1,
        (--guest->allocation_count - index) * sizeof(*guest->allocations));
    *out = allocation; *backing = owned;
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
