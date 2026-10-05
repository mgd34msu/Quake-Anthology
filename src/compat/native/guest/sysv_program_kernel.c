#include "sysv_program_private.h"

enum { K_EPERM = 1, K_ENOENT = 2, K_EIO = 5, K_EBADF = 9, K_ENOMEM = 12,
    K_EACCES = 13, K_EFAULT = 14, K_EEXIST = 17, K_EINVAL = 22, K_EMFILE = 24,
    K_ESPIPE = 29, K_ENOSYS = 38, K_EOPNOTSUPP = 95 };
static int64_t framework_failure(const qa_error *error)
{
    if (!error) return -K_EIO;
    switch (error->code) {
    case QA_ERROR_NOT_FOUND: return -K_ENOENT;
    case QA_ERROR_MEMORY: return -K_ENOMEM;
    case QA_ERROR_ARGUMENT: return -K_EINVAL;
    case QA_ERROR_UNSUPPORTED: return -K_EOPNOTSUPP;
    default: return -K_EIO;
    }
}
static int64_t failure(qa_native_sysv_program *owner, const qa_error *error)
{
    qa_fs_native_error native = {0};
    if (error && error->code == QA_ERROR_IO &&
        owner->options.services.native_error(owner->options.services.context, &native) &&
        native.available && native.platform == 1 && native.code && native.code <= 4095)
        return -(int64_t)native.code;
    return framework_failure(error);
}
static program_descriptor *descriptor(qa_native_sysv_program *owner, uint64_t number)
{
    if (number > INT32_MAX) return NULL;
    for (size_t i = 0; i < owner->descriptor_count; ++i)
        if (owner->descriptors[i].number == (int32_t)number) return owner->descriptors + i;
    return NULL;
}
static program_file *file(qa_native_sysv_program *owner, uint64_t number)
{
    program_descriptor *fd = descriptor(owner, number);
    if (!fd || fd->file >= owner->file_count) return NULL;
    program_file *out = owner->files + fd->file;
    return out->closed || out->closing ? NULL : out;
}
static bool descriptor_room(qa_native_sysv_program *owner, int32_t minimum,
    int32_t *out, qa_error *error)
{
    for (int32_t number = minimum; number < INT32_MAX; ++number)
        if (!descriptor(owner, (uint64_t)number)) {
            if (!guest_grow((void **)&owner->descriptors, &owner->descriptor_capacity,
                owner->descriptor_count + 1, sizeof(*owner->descriptors), error)) return false;
            *out = number; return true;
        }
    guest_fail(error, QA_ERROR_MEMORY, 0, "Linux descriptor namespace exhausted");
    return false;
}
static bool close_description(program_file *entry, qa_error *error)
{
    if (entry->closed) return true;
    entry->closing = true;
    if (!entry->capability.close || !entry->capability.close(entry->capability.context, error)) return false;
    entry->closed = true; entry->references = 0; return true;
}
bool program_files_close(qa_native_sysv_program *owner, qa_error *error)
{
    if (owner->provisional) return true;
    for (size_t i = 0; i < owner->file_count; ++i)
        if (!close_description(owner->files + i, error)) return false;
    return true;
}
static bool user_range(qa_native_sysv_program *owner, uint64_t address, size_t bytes, uint32_t rights)
{
    qa_error ignored = {0};
    return !bytes || guest_range(owner->guest, address, bytes, rights, &ignored);
}
static bool string_read(qa_native_sysv_program *owner, uint64_t address, char data[4096])
{
    qa_error ignored = {0};
    for (size_t i = 0; i < 4096; ++i) {
        if (address > UINT64_MAX - i || !qa_native_guest_read(owner->guest, address + i, data + i, 1, &ignored)) return false;
        if (!data[i]) return true;
    }
    return false;
}
static bool store(qa_native_sysv_program *owner, uint64_t address, const void *data,
    size_t bytes, int64_t *result, qa_error *error)
{
    if (!user_range(owner, address, bytes, QA_NATIVE_GUEST_WRITE)) { *result = -K_EFAULT; return true; }
    return qa_native_guest_write(owner->guest, address, (qa_bytes){data, bytes}, error);
}
static size_t user_prefix(qa_native_sysv_program *owner, uint64_t address, size_t bytes,
    uint32_t rights)
{
    size_t completed = 0;
    while (completed < bytes) {
        qa_native_guest_mapping *mapping = guest_mapping(owner->guest, address + completed);
        if (!mapping || (mapping->permissions & rights) != rights) break;
        uint64_t displacement = address + completed - mapping->base;
        size_t amount = bytes - completed;
        if (amount > mapping->bytes - displacement) amount = (size_t)(mapping->bytes - displacement);
        guest_backing *backing = guest_backing_at(owner->guest, mapping->backing);
        uint64_t position = mapping->backing_offset + displacement;
        if (backing->file) {
            if (position >= backing->source.accessible_bytes) break;
            if (amount > backing->source.accessible_bytes - position)
                amount = (size_t)(backing->source.accessible_bytes - position);
        }
        completed += amount;
    }
    return completed;
}
static bool io(qa_native_sysv_program *owner, const uint64_t a[6], bool write,
    bool positional, int64_t *result, qa_error *error)
{
    if (positional && a[3] > INT64_MAX) { *result = -K_EINVAL; return true; }
    program_file *entry = file(owner, a[0]);
    if (!entry || !(entry->capability.mode & (write ? 2u : 1u))) { *result = -K_EBADF; return true; }
    if (positional && !entry->seekable) { *result = -K_ESPIPE; return true; }
    if (a[1] > UINT64_C(0x0000800000000000) || a[2] > UINT64_C(0x0000800000000000) - a[1]) {
        *result = -K_EFAULT; return true;
    }
    uint64_t offset = positional ? a[3] : entry->offset;
    if (offset > INT64_MAX || a[2] > (uint64_t)INT64_MAX - offset) { *result = -K_EINVAL; return true; }
    size_t requested = a[2] > UINT32_C(0x7ffff000) ? UINT32_C(0x7ffff000) : (size_t)a[2];
    size_t count = user_prefix(owner, a[1], requested, write ? QA_NATIVE_GUEST_READ : QA_NATIVE_GUEST_WRITE);
    if (requested && !count) {
        /* Regular file EOF needs no destination copy. Observe the genuinely
         * held object; never consume a pipe byte to probe an invalid buffer. */
        if (!write) {
            qa_fs_posix_status status; qa_error actual = {0};
            if (!owner->options.services.file_status(owner->options.services.context,
                entry->capability.capability, &status, &actual)) { *result = failure(owner, &actual); return true; }
            if ((status.mode & UINT32_C(0170000)) == UINT32_C(0100000) &&
                status.size >= 0 && offset >= (uint64_t)status.size) { *result = 0; return true; }
        }
        *result = -K_EFAULT; return true;
    }
    uint8_t *data = count ? malloc(count) : NULL;
    if (count && !data) { *result = -K_ENOMEM; return true; }
    size_t done = 0; qa_error actual = {0}; bool okay;
    if (!owner->options.services.descriptor_flags(owner->options.services.context,
        entry->capability.capability, entry->flags, &actual)) {
        *result = failure(owner, &actual); free(data); return true;
    }
    if (write) {
        okay = qa_native_guest_read(owner->guest, a[1], data, count, error);
        if (!okay) { free(data); return false; }
        if (entry->flags & 1024u) {
            if (!entry->capability.size(entry->capability.context, &offset, &actual)) { *result = failure(owner, &actual); free(data); return true; }
            if (offset > INT64_MAX || count > (uint64_t)INT64_MAX - offset) { free(data); *result = -K_EINVAL; return true; }
        }
        okay = entry->capability.write(entry->capability.context, offset, (qa_bytes){data, count}, &done, &actual);
    } else okay = entry->capability.read(entry->capability.context, offset, data, count, &done, &actual);
    if (done > count) { free(data); return guest_fail(error, QA_ERROR_FORMAT, a[0], "Linux file capability reported impossible completion"); }
    if (!positional && entry->seekable) entry->offset = offset + done;
    if (!write && done && !qa_native_guest_write(owner->guest, a[1], (qa_bytes){data, done}, error)) { free(data); return false; }
    *result = done || okay ? (int64_t)done : failure(owner, &actual); free(data); return true;
}
static bool open_file(qa_native_sysv_program *owner, uint64_t path, uint64_t flags,
    int64_t *result, qa_error *error)
{
    char name[4096];
    if (!string_read(owner, path, name)) { *result = -K_EFAULT; return true; }
    if ((flags & 3u) == 3u || flags & ~(UINT64_C(3) | 64u | 128u | 512u | 1024u | 2048u | 32768u | 131072u | 524288u)) {
        *result = -K_EINVAL; return true;
    }
    /* The contained acquisition API does not yet carry a Unix create mode or
     * atomic O_EXCL receipt. Do not silently synthesize those filesystem effects. */
    if (flags & (64u | 128u)) { *result = -K_EOPNOTSUPP; return true; }
    if (!owner->options.services.open_file) { *result = -K_EACCES; return true; }
    uint32_t mode = (flags & 3u) == 0 ? 1u : (flags & 3u) == 1 ? 2u : 3u;
    if ((flags & 512u) && !(mode & 2u)) { *result = -K_EACCES; return true; }
    int32_t number;
    if (!descriptor_room(owner, 0, &number, error) ||
        !guest_grow((void **)&owner->files, &owner->file_capacity, owner->file_count + 1, sizeof(*owner->files), error)) return false;
    program_file *entry = owner->files + owner->file_count;
    *entry = (program_file){.flags = (uint32_t)flags};
    bool opened = false; qa_error actual = {0};
    bool okay = owner->options.services.open_file(owner->options.services.context, name, mode,
        flags & 512u ? QA_FS_TRUNCATE_EXISTING : QA_FS_OPEN_EXISTING, &entry->capability, &opened, &actual);
    if (opened) { ++owner->file_count; entry->references = 1; }
    if (!okay || !opened) {
        if (opened) { entry->closing = true; entry->references = 0; }
        *result = okay ? -K_ENOENT : failure(owner, &actual); return true;
    }
    if (!entry->capability.capability || !entry->capability.close || !entry->capability.size ||
        (entry->capability.mode & mode) != mode || ((mode & 1u) && !entry->capability.read) ||
        ((mode & 2u) && !entry->capability.write))
        return guest_fail(error, QA_ERROR_FORMAT, (uint64_t)number, "Linux open acquired an incomplete actual capability");
    qa_native_sysv_program_descriptor_status status;
    if (!owner->options.services.descriptor_status(owner->options.services.context,
        entry->capability.capability, &status, &actual)) {
        *result = failure(owner, &actual); entry->references = 0;
        qa_error cleanup = {0}; (void)close_description(entry, &cleanup); return true;
    }
    if (status.offset < 0 || (!status.seekable && status.offset))
        return guest_fail(error, QA_ERROR_FORMAT, (uint64_t)number, "Linux opened descriptor has invalid actual position");
    entry->seekable = status.seekable; entry->offset = (uint64_t)status.offset;
    /* Native containment uses safety flags on its private descriptor. The
     * source description retains the actual requested Linux status flags;
     * x64's real forced O_LARGEFILE remains observed from the acquired object. */
    entry->flags = ((uint32_t)flags & ~(UINT32_C(64) | 128u | 512u | 524288u)) |
        (status.flags & 32768u);
    owner->descriptors[owner->descriptor_count++] = (program_descriptor){number, owner->file_count - 1, (flags & 524288u) != 0};
    *result = number; return true;
}
static bool remove_pages(qa_native_sysv_program *owner, uint64_t base, size_t bytes, qa_error *error)
{
    uint64_t end = base + bytes;
    /* Preflight every affected allocator record before any topology change. */
    for (size_t i = 0; i < owner->guest->allocation_count; ++i) {
        const guest_allocation *allocation = owner->guest->allocations + i;
        uint64_t limit = allocation->address + ((allocation->bytes + 4095) & ~(size_t)4095);
        if (base < limit && allocation->address < end)
            return guest_fail(error, QA_ERROR_ARGUMENT, allocation->address, "Linux range intersects retained kernel allocator storage");
    }
    for (;;) {
        qa_native_guest_mapping *found = NULL;
        for (size_t i = 0; i < owner->guest->mapping_count; ++i) {
            qa_native_guest_mapping *row = owner->guest->mappings + i;
            if (base < row->base + row->bytes && row->base < end) { found = row; break; }
        }
        if (!found) return true;
        uint64_t first = base > found->base ? base : found->base;
        uint64_t last = end < found->base + found->bytes ? end : found->base + found->bytes;
        if (!qa_native_guest_unmap_range(owner->guest, first, (size_t)(last - first), error)) return false;
        if (!guest_elf_program_stack_changed(owner->startup, first, (size_t)(last - first), error)) {
            owner->guest->failed = true; return false;
        }
        for (size_t i = 0; i < 2; ++i)
            if (owner->memory[i] && !guest_elf_memory_program_changed(owner->memory[i], first,
                (size_t)(last - first), error)) { owner->guest->failed = true; return false; }
    }
}
static bool hole(qa_native_sysv_program *owner, uint64_t hint, size_t bytes, uint64_t *out)
{
    uint64_t base = hint ? hint & ~UINT64_C(4095) : owner->mapping_cursor;
    if (base < 65536) base = 65536;
    for (;;) {
        if (base > UINT64_C(0x0000800000000000) - bytes) return false;
        uint64_t next = base;
        for (size_t i = 0; i < owner->guest->mapping_count; ++i) {
            const qa_native_guest_mapping *row = owner->guest->mappings + i;
            if (base < row->base + row->bytes && row->base < base + bytes && next < row->base + row->bytes)
                next = row->base + row->bytes;
        }
        if (next == base) { *out = base; return true; }
        base = next;
    }
}
typedef struct mapping_file {
    const qa_native_sysv_file *file;
    bool malformed;
} mapping_file;
static bool mapping_read(void *context, size_t offset, void *out, size_t bytes,
    qa_error *error)
{
    mapping_file *source = context;
    size_t position = 0;
    while (position < bytes) {
        size_t done = 0;
        bool okay = source->file->read(source->file->context, offset + position,
            (uint8_t *)out + position, bytes - position, &done, error);
        if (done > bytes - position) {
            source->malformed = true;
            return guest_fail(error, QA_ERROR_FORMAT, offset,
                "Linux mapping read reported impossible completion");
        }
        position += done;
        if (!okay) return false;
        if (!done) return guest_fail(error, QA_ERROR_IO, offset + position, "Linux mapping read ended before its actual file extent");
    }
    return true;
}

static bool mmap_file(qa_native_sysv_program *owner, const uint64_t a[6], int64_t *result, qa_error *error)
{
    if (!a[1] || a[1] > SIZE_MAX - 4095 || a[2] > 7 || a[5] % 4096 ||
        (a[3] & 15u) != 2u || a[3] & ~(UINT64_C(2) | 16u | 32u | 2048u | 4096u | 8192u | 32768u | 131072u | 1048576u)) {
        *result = -K_EINVAL; return true;
    }
    size_t bytes = ((size_t)a[1] + 4095) & ~(size_t)4095;
    bool fixed = (a[3] & (16u | 1048576u)) != 0;
    uint64_t base = a[0];
    if (fixed && (!base || base % 4096 || base > UINT64_C(0x0000800000000000) - bytes)) { *result = -K_EINVAL; return true; }
    if (!fixed && !hole(owner, base, bytes, &base)) { *result = -K_ENOMEM; return true; }
    if (a[3] & 1048576u)
        for (size_t i = 0; i < owner->guest->mapping_count; ++i) {
            qa_native_guest_mapping *row = owner->guest->mappings + i;
            if (base < row->base + row->bytes && row->base < base + bytes) { *result = -K_EEXIST; return true; }
        }
    qa_error actual = {0}; bool okay = true;
    uint32_t rights = (uint32_t)a[2];
    if (owner->options.read_implies_execute && (rights & QA_NATIVE_GUEST_READ)) rights |= QA_NATIVE_GUEST_EXECUTE;
    guest_backing prepared = {0}; mapping_file file_source = {0};
    if (!(a[3] & 32u)) {
        program_file *entry = file(owner, a[4]);
        if (!entry || !(entry->capability.mode & 1u)) { *result = -K_EBADF; return true; }
        uint64_t size;
        if (!entry->capability.size(entry->capability.context, &size, &actual)) { *result = failure(owner, &actual); return true; }
        if (size > SIZE_MAX || size > owner->options.guest.maximum_backing_bytes) { *result = -K_ENOMEM; return true; }
        file_source.file = &entry->capability;
        const qa_source_save_memory_source source = {.read = mapping_read, .context = &file_source};
        okay = guest_file_prepare(owner->guest, base, bytes, rights, &source,
            (size_t)size, a[5], entry->capability.capability, &prepared, &actual);
    }
    if (okay && fixed && !(a[3] & 1048576u)) okay = remove_pages(owner, base, bytes, &actual);
    qa_native_guest_mapping mapped;
    if (okay) okay = a[3] & 32u ?
        qa_native_guest_map(owner->guest, base, bytes, rights, (qa_bytes){0}, &mapped, &actual) :
        guest_file_publish(owner->guest, base, rights, &prepared, &mapped, &actual);
    free(prepared.data);
    if (file_source.malformed) { if (error) *error = actual; return false; }
    if (!okay && owner->guest->failed) { if (error) *error = actual; return false; }
    if (okay && !fixed) owner->mapping_cursor = base + bytes;
    *result = okay ? (int64_t)base : failure(owner, &actual); return true;
}
static bool stat_file(qa_native_sysv_program *owner, uint64_t number, uint64_t address,
    int64_t *result, qa_error *error)
{
    program_file *entry = file(owner, number); if (!entry) { *result = -K_EBADF; return true; }
    if (!user_range(owner, address, 144, QA_NATIVE_GUEST_WRITE)) { *result = -K_EFAULT; return true; }
    qa_fs_posix_status status; qa_error actual = {0};
    if (!owner->options.services.file_status(owner->options.services.context, entry->capability.capability, &status, &actual)) {
        *result = failure(owner, &actual); return true;
    }
    uint8_t data[144] = {0};
    qa_store_u64le(data, status.device); qa_store_u64le(data + 8, status.inode); qa_store_u64le(data + 16, status.links);
    qa_store_u32le(data + 24, status.mode); qa_store_u32le(data + 28, status.uid); qa_store_u32le(data + 32, status.gid);
    qa_store_u64le(data + 40, status.special_device); qa_store_u64le(data + 48, (uint64_t)status.size);
    qa_store_u64le(data + 56, (uint64_t)status.block_size); qa_store_u64le(data + 64, (uint64_t)status.blocks);
    const qa_fs_timestamp *times[] = {&status.access, &status.modification, &status.change};
    for (size_t i = 0; i < 3; ++i) {
        qa_store_u64le(data + 72 + i * 16, (uint64_t)times[i]->seconds);
        qa_store_u64le(data + 80 + i * 16, times[i]->nanoseconds);
    }
    *result = 0; return store(owner, address, data, sizeof(data), result, error);
}
static int64_t close_fd(qa_native_sysv_program *owner, uint64_t number)
{
    program_descriptor *fd = descriptor(owner, number);
    if (!fd) return -K_EBADF;
    program_file *entry = owner->files + fd->file; qa_error actual = {0};
    size_t index = (size_t)(fd - owner->descriptors);
    memmove(fd, fd + 1, (owner->descriptor_count - index - 1) * sizeof(*fd)); --owner->descriptor_count;
    if (entry->references) --entry->references;
    return entry->references || close_description(entry, &actual) ? 0 : failure(owner, &actual);
}
static bool stat_path(qa_native_sysv_program *owner, uint64_t path, uint64_t destination,
    int64_t *result, qa_error *error)
{
    char name[4096];
    if (!string_read(owner, path, name)) { *result = -K_EFAULT; return true; }
    if (!user_range(owner, destination, 144, QA_NATIVE_GUEST_WRITE)) { *result = -K_EFAULT; return true; }
    if (!owner->options.services.open_file) { *result = -K_EACCES; return true; }
    int32_t number;
    if (!descriptor_room(owner, 0, &number, error) ||
        !guest_grow((void **)&owner->files, &owner->file_capacity, owner->file_count + 1, sizeof(*owner->files), error)) return false;
    program_file *entry = owner->files + owner->file_count;
    *entry = (program_file){0}; bool opened = false; qa_error actual = {0};
    bool acquired = owner->options.services.open_file(owner->options.services.context, name, 0,
        QA_FS_OPEN_EXISTING, &entry->capability, &opened, &actual);
    if (opened) ++owner->file_count;
    if (!acquired || !opened) {
        if (opened) entry->closing = true;
        *result = acquired ? -K_ENOENT : failure(owner, &actual); return true;
    }
    if (!entry->capability.capability || !entry->capability.close || !entry->capability.size)
        return guest_fail(error, QA_ERROR_FORMAT, path, "Linux metadata acquisition lost its actual native owner");
    entry->references = 1;
    owner->descriptors[owner->descriptor_count++] = (program_descriptor){number, owner->file_count - 1, false};
    bool okay = stat_file(owner, (uint64_t)number, destination, result, error);
    /* The internal path lease closes independently of the stat result. A
     * refused native close remains represented by its zero-reference holder. */
    (void)close_fd(owner, (uint64_t)number); return okay;
}
static bool vector_io(qa_native_sysv_program *owner, const uint64_t a[6], bool write,
    int64_t *result, qa_error *error)
{
    if (!file(owner, a[0])) { *result = -K_EBADF; return true; }
    if (a[2] > 1024) { *result = -K_EINVAL; return true; }
    size_t count = (size_t)a[2], bytes = count * 16;
    if (!user_range(owner, a[1], bytes, QA_NATIVE_GUEST_READ)) { *result = -K_EFAULT; return true; }
    uint8_t *vectors = bytes ? malloc(bytes) : NULL;
    if (bytes && !vectors) { *result = -K_ENOMEM; return true; }
    if (bytes && !qa_native_guest_read(owner->guest, a[1], vectors, bytes, error)) { free(vectors); return false; }
    /* Linux imports the whole iovec before touching the file. access_ok tests
     * the user address domain here; page faults occur later during transfer.
     * A single-vector import clamps before access_ok, while the array path
     * qualifies original lengths before replacing them with capped lengths. */
    for (size_t i = 0; i < count; ++i)
        if (qa_load_u64le(vectors + i * 16 + 8) > INT64_MAX) {
            free(vectors); *result = -K_EINVAL; return true;
        }
    uint64_t imported = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t base = qa_load_u64le(vectors + i * 16);
        uint64_t amount = qa_load_u64le(vectors + i * 16 + 8);
        uint64_t available = UINT32_C(0x7ffff000) - imported;
        if (count == 1 && amount > available) amount = available;
        if (base > UINT64_C(0x0000800000000000) ||
            amount > UINT64_C(0x0000800000000000) - base) {
            free(vectors); *result = -K_EFAULT; return true;
        }
        if (amount > available) amount = available;
        qa_store_u64le(vectors + i * 16 + 8, amount); imported += amount;
    }
    uint64_t total = 0; bool okay = true; int64_t current = 0;
    for (size_t i = 0; okay && i < count && total < UINT32_C(0x7ffff000); ++i) {
        uint64_t amount = qa_load_u64le(vectors + i * 16 + 8);
        uint64_t arguments[6] = {a[0], qa_load_u64le(vectors + i * 16), amount, 0, 0, 0};
        okay = io(owner, arguments, write, false, &current, error);
        if (!okay || current < 0) break;
        total += (uint64_t)current;
        if ((uint64_t)current < amount) break;
    }
    free(vectors); *result = total ? (int64_t)total : current; return okay;
}
static bool robust_death(qa_native_sysv_program *owner, uint64_t entry, uint64_t offset,
    bool pi, bool pending, bool *walk, qa_error *error)
{
    uint64_t address = entry + offset;
    if (address % 4 || !user_range(owner, address, 4, QA_NATIVE_GUEST_READ)) {
        *walk = false; return true;
    }
    uint8_t bytes[4];
    if (!qa_native_guest_read(owner->guest, address, bytes, 4, error)) return false;
    uint32_t value = qa_load_u32le(bytes), tid = value & UINT32_C(0x3fffffff);
    /* This kernel owns one source task and currently has no queued futex
     * waiters. The pending unlocked non-PI case has no user-memory effect. */
    if ((pending && !pi && !tid) || tid != owner->status.thread_id) return true;
    if (!user_range(owner, address, 4, QA_NATIVE_GUEST_WRITE)) { *walk = false; return true; }
    qa_store_u32le(bytes, (value & UINT32_C(0x80000000)) | UINT32_C(0x40000000));
    /* No source task can race the stopped kernel continuation; publish the
     * actual OWNER_DIED word through its real committed-memory producer. */
    return qa_native_guest_write(owner->guest, address, (qa_bytes){bytes, 4}, error);
}
static bool robust_exit(qa_native_sysv_program *owner, qa_error *error)
{
    uint64_t head = owner->status.robust_list;
    if (!head || !user_range(owner, head, 24, QA_NATIVE_GUEST_READ)) return true;
    uint8_t bytes[24];
    if (!qa_native_guest_read(owner->guest, head, bytes, sizeof(bytes), error)) return false;
    uint64_t entry = qa_load_u64le(bytes), offset = qa_load_u64le(bytes + 8);
    uint64_t pending = qa_load_u64le(bytes + 16), pending_address = pending & ~UINT64_C(1);
    bool walk = true;
    for (size_t limit = 2048; (entry & ~UINT64_C(1)) != head && limit; --limit) {
        uint64_t address = entry & ~UINT64_C(1), next = 0;
        bool readable = user_range(owner, address, 8, QA_NATIVE_GUEST_READ);
        if (readable) {
            if (!qa_native_guest_read(owner->guest, address, bytes, 8, error)) return false;
            next = qa_load_u64le(bytes);
        }
        if (address != pending_address &&
            !robust_death(owner, address, offset, (entry & 1u) != 0, false, &walk, error)) return false;
        if (!walk || !readable) return true;
        entry = next;
    }
    if (pending_address)
        return robust_death(owner, pending_address, offset, (pending & 1u) != 0, true, &walk, error);
    return true;
}
bool program_syscall(void *context, qa_native_guest *guest, const qa_native_guest_syscall *request,
    qa_native_guest_syscall_result *out, qa_error *error)
{
    qa_native_sysv_program *owner = context;
    if (!owner || guest != owner->guest || !owner->busy || !program_current(owner, error)) return false;
    const uint64_t *a = request->arguments; int64_t result = -K_ENOSYS; bool okay = true;
    switch (request->number) {
    case 0: case 1: case 17: case 18:
        okay = io(owner, a, request->number == 1 || request->number == 18,
            request->number >= 17, &result, error); break;
    case 2: okay = open_file(owner, a[0], a[1], &result, error); break;
    case 4: case 6: okay = stat_path(owner, a[0], a[1], &result, error); break;
    case 262:
        if (a[3] & ~(UINT64_C(0x100) | UINT64_C(0x800) | UINT64_C(0x1000))) { result = -K_EINVAL; break; }
        if (a[3] & UINT64_C(0x1000)) {
            char name[4096];
            if (!string_read(owner, a[1], name)) result = -K_EFAULT;
            else if (!*name) okay = stat_file(owner, a[0], a[2], &result, error);
            else if ((int64_t)a[0] == -100 || name[0] == '/') okay = stat_path(owner, a[1], a[2], &result, error);
            else result = -K_EBADF;
        } else if ((int64_t)a[0] == -100) okay = stat_path(owner, a[1], a[2], &result, error);
        else {
            char name[4096];
            if (!string_read(owner, a[1], name)) result = -K_EFAULT;
            else if (name[0] == '/') okay = stat_path(owner, a[1], a[2], &result, error);
            else result = -K_EBADF;
        }
        break;
    case 257:
        if ((int64_t)a[0] != -100) result = -K_EBADF;
        else okay = open_file(owner, a[1], a[2], &result, error);
        break;
    case 3: result = close_fd(owner, a[0]); break;
    case 5: okay = stat_file(owner, a[0], a[1], &result, error); break;
    case 8: {
        program_file *entry = file(owner, a[0]); if (!entry) { result = -K_EBADF; break; }
        if (!entry->seekable) { result = -K_ESPIPE; break; }
        uint64_t base = 0; qa_error actual = {0};
        if (a[2] == 1) base = entry->offset;
        else if (a[2] == 2) { if (!entry->capability.size(entry->capability.context, &base, &actual)) { result = failure(owner, &actual); break; } }
        else if (a[2] != 0) { result = -K_EINVAL; break; }
        int64_t offset; memcpy(&offset, &a[1], sizeof(offset));
        uint64_t absolute = offset < 0 ? (uint64_t)(-(offset + 1)) + 1 : (uint64_t)offset;
        if (base > INT64_MAX || (offset < 0 ? absolute > base : absolute > (uint64_t)INT64_MAX - base)) { result = -K_EINVAL; break; }
        entry->offset = offset < 0 ? base - absolute : base + absolute; result = (int64_t)entry->offset; break;
    }
    case 9: okay = mmap_file(owner, a, &result, error); break;
    case 19: case 20: okay = vector_io(owner, a, request->number == 20, &result, error); break;
    case 32: case 33: case 292: case 72: {
        program_descriptor *source = descriptor(owner, a[0]);
        if (!source) { result = -K_EBADF; break; }
        if (request->number == 72 && a[1] == 1) { result = source->close_on_exec; break; }
        if (request->number == 72 && a[1] == 2) { source->close_on_exec = (a[2] & 1u) != 0; result = 0; break; }
        if (request->number == 72 && a[1] == 3) { result = owner->files[source->file].flags; break; }
        if (request->number == 72 && a[1] == 4) {
            program_file *entry = owner->files + source->file;
            /* These extra modes require their actual async/direct/credential
             * service owners. Do not report a virtual bit-only success. */
            if (a[2] & (UINT64_C(8192) | UINT64_C(16384) | UINT64_C(262144))) {
                result = -K_EOPNOTSUPP; break;
            }
            uint32_t flags = (entry->flags & ~(UINT32_C(1024) | 2048u)) |
                ((uint32_t)a[2] & (1024u | 2048u));
            qa_error actual = {0};
            if (!owner->options.services.descriptor_flags(owner->options.services.context,
                entry->capability.capability, flags, &actual)) result = failure(owner, &actual);
            else { entry->flags = flags; result = 0; }
            break;
        }
        if (request->number == 72 && a[1] != 0 && a[1] != 1030) { result = -K_EINVAL; break; }
        size_t description = source->file; int32_t number = 0;
        bool exact = request->number == 33 || request->number == 292;
        bool cloexec = request->number == 292 ? (a[2] & 524288u) != 0 : request->number == 72 && a[1] == 1030;
        if ((exact && a[1] > INT32_MAX) || (request->number == 292 && (a[2] & ~UINT64_C(524288))) ||
            (request->number == 72 && a[2] > INT32_MAX)) { result = -K_EINVAL; break; }
        if (exact && a[0] == a[1]) { result = request->number == 292 ? -K_EINVAL : (int64_t)a[0]; break; }
        if (exact) {
            number = (int32_t)a[1];
            if (!guest_grow((void **)&owner->descriptors, &owner->descriptor_capacity,
                owner->descriptor_count + 1, sizeof(*owner->descriptors), error)) { okay = false; break; }
            if (descriptor(owner, (uint64_t)number)) (void)close_fd(owner, (uint64_t)number);
        } else if (!descriptor_room(owner, request->number == 72 ? (int32_t)a[2] : 0, &number, error)) { okay = false; break; }
        ++owner->files[description].references;
        owner->descriptors[owner->descriptor_count++] = (program_descriptor){number, description, cloexec};
        result = number; break;
    }
    case 10: case 11: {
        if (!a[1] || a[0] % 4096 || a[1] > SIZE_MAX - 4095 || a[0] > UINT64_MAX - a[1] ||
            (request->number == 10 && a[2] > 7)) { result = -K_EINVAL; break; }
        size_t bytes = ((size_t)a[1] + 4095) & ~(size_t)4095; qa_error actual = {0};
        if (a[0] > UINT64_C(0x0000800000000000) - bytes) { result = -K_EINVAL; break; }
        uint32_t rights = (uint32_t)a[2];
        if (owner->options.read_implies_execute && (rights & 1u)) rights |= 4u;
        bool changed = request->number == 10 ? qa_native_guest_protect_range(guest, a[0], bytes, rights, &actual) :
            remove_pages(owner, a[0], bytes, &actual);
        if (!changed && guest->failed) { if (error) *error = actual; return false; }
        result = changed ? 0 : framework_failure(&actual); break;
    }
    case 12: {
        result = (int64_t)owner->current_break;
        if (!a[0] || a[0] < owner->break_base || a[0] > UINT64_MAX - 4095) break;
        uint64_t old = (owner->current_break + 4095) & ~UINT64_C(4095), next = (a[0] + 4095) & ~UINT64_C(4095);
        qa_error actual = {0}; bool changed = true; qa_native_guest_mapping mapping;
        uint32_t rights = QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE;
        if (owner->options.read_implies_execute) rights |= QA_NATIVE_GUEST_EXECUTE;
        if (next > old) changed = qa_native_guest_map(guest, old, (size_t)(next - old),
            rights, (qa_bytes){0}, &mapping, &actual);
        else if (next < old) changed = remove_pages(owner, next, (size_t)(old - next), &actual);
        if (!changed && guest->failed) { if (error) *error = actual; return false; }
        if (changed) owner->current_break = a[0];
        result = (int64_t)owner->current_break; break;
    }
    case 39: result = (int64_t)owner->status.process_id; break;
    case 186: result = (int64_t)owner->status.thread_id; break;
    case 60: case 231: {
        okay = robust_exit(owner, error);
        if (okay && owner->status.clear_child_tid && user_range(owner, owner->status.clear_child_tid, 4, QA_NATIVE_GUEST_WRITE)) {
            uint32_t zero = 0; okay = store(owner, owner->status.clear_child_tid, &zero, 4, &result, error);
        }
        if (okay) { owner->status.exit_code = (uint32_t)a[0] & 255u; owner->status.exited = true; out->stop = true; }
        result = 0; break;
    }
    case 63: case 102: case 104: case 107: case 108: {
        qa_native_process_linux_identity identity; qa_error actual = {0};
        if (!owner->options.services.identity(owner->options.services.context, &identity, &actual)) { result = failure(owner, &actual); break; }
        if (request->number == 63) {
            uint8_t data[390] = {0}; const char *parts[] = {identity.system, identity.node, identity.release, identity.version, identity.machine, identity.domain};
            for (size_t i = 0; i < 6; ++i) { size_t n = strlen(parts[i]); if (n > 64) n = 64; memcpy(data + i * 65, parts[i], n); }
            result = 0; okay = store(owner, a[0], data, sizeof(data), &result, error);
        } else result = request->number == 102 ? identity.uid : request->number == 104 ? identity.gid :
            request->number == 107 ? identity.effective_uid : identity.effective_gid;
        break;
    }
    case 158: {
        qa_native_guest_cpu state; okay = qa_native_guest_cpu_read(guest, &state, error); if (!okay) break;
        if (a[0] == 0x1001 || a[0] == 0x1002) {
            if (a[1] >= UINT64_C(0x0000800000000000)) { result = -K_EPERM; break; }
            state.segments[a[0] == 0x1001 ? 5 : 4].base = a[1];
            okay = qa_native_guest_cpu_write(guest, &state, error); result = 0;
        } else if (a[0] == 0x1003 || a[0] == 0x1004) {
            uint8_t data[8]; qa_store_u64le(data, state.segments[a[0] == 0x1003 ? 4 : 5].base);
            result = 0; okay = store(owner, a[1], data, 8, &result, error);
        } else result = -K_EINVAL;
        break;
    }
    case 218: owner->status.clear_child_tid = a[0]; result = (int64_t)owner->status.thread_id; break;
    case 273:
        if (a[1] != 24) result = -K_EINVAL;
        else { owner->status.robust_list = a[0]; result = 0; }
        break;
    case 228: case 96: {
        if (request->number == 228 && !a[1]) { result = -K_EFAULT; break; }
        if (request->number == 96 && a[1]) { result = -K_EOPNOTSUPP; break; }
        int64_t seconds; int32_t nanos; qa_error actual = {0};
        int32_t clock = request->number == 96 ? 0 : (int32_t)a[0];
        bool observed = clock == 2 || clock == 3 ?
            program_clock_read(owner, clock, &seconds, &nanos, &actual) :
            owner->options.services.clock(owner->options.services.context, clock, &seconds, &nanos, &actual);
        if (!observed) {
            result = clock == 2 || clock == 3 ? framework_failure(&actual) : failure(owner, &actual);
            break;
        }
        if (nanos < 0 || nanos >= 1000000000) return guest_fail(error, QA_ERROR_FORMAT, 0, "Linux clock returned invalid nanoseconds");
        uint8_t data[16]; qa_store_u64le(data, (uint64_t)seconds); qa_store_u64le(data + 8, request->number == 96 ? (uint64_t)(nanos / 1000) : (uint64_t)nanos);
        result = 0; uint64_t address = request->number == 96 ? a[0] : a[1];
        if (address) okay = store(owner, address, data, sizeof(data), &result, error);
        break;
    }
    case 318: {
        if (a[2] & ~UINT64_C(7)) { result = -K_EINVAL; break; }
        size_t bytes = a[1] > UINT32_C(0x7ffff000) ? UINT32_C(0x7ffff000) : (size_t)a[1];
        if (!user_range(owner, a[0], bytes, QA_NATIVE_GUEST_WRITE)) { result = -K_EFAULT; break; }
        uint8_t *data = bytes ? malloc(bytes) : NULL; if (bytes && !data) { result = -K_ENOMEM; break; }
        qa_error actual = {0}; bool acquired = owner->options.services.entropy(owner->options.services.context, data, bytes, &actual);
        result = acquired ? (int64_t)bytes : failure(owner, &actual);
        if (acquired) okay = store(owner, a[0], data, bytes, &result, error);
        free(data); break;
    }
    default: break;
    }
    if (okay) okay = program_current(owner, error);
    if (okay) out->value = result;
    return okay;
}
