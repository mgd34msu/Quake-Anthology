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
    return guest_fail(error, QA_ERROR_MEMORY, 0, "Linux descriptor namespace exhausted");
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
static bool io(qa_native_sysv_program *owner, const uint64_t a[6], bool write,
    bool positional, int64_t *result, qa_error *error)
{
    program_file *entry = file(owner, a[0]);
    if (!entry || !(entry->capability.mode & (write ? 2u : 1u))) { *result = -K_EBADF; return true; }
    size_t count = a[2] > UINT32_C(0x7ffff000) ? UINT32_C(0x7ffff000) : (size_t)a[2];
    if (!user_range(owner, a[1], count, write ? QA_NATIVE_GUEST_READ : QA_NATIVE_GUEST_WRITE)) { *result = -K_EFAULT; return true; }
    if (positional && a[3] > INT64_MAX) { *result = -K_EINVAL; return true; }
    uint8_t *data = count ? malloc(count) : NULL;
    if (count && !data) { *result = -K_ENOMEM; return true; }
    uint64_t offset = positional ? a[3] : entry->offset;
    if (offset > INT64_MAX || count > (uint64_t)INT64_MAX - offset) { free(data); *result = -K_EINVAL; return true; }
    size_t done = 0; qa_error actual = {0}; bool okay;
    if (write) {
        okay = qa_native_guest_read(owner->guest, a[1], data, count, error);
        if (!okay) { free(data); return false; }
        if (entry->flags & 1024u) {
            if (!entry->capability.size(entry->capability.context, &offset, &actual)) { *result = failure(owner, &actual); free(data); return true; }
        }
        okay = entry->capability.write(entry->capability.context, offset, (qa_bytes){data, count}, &done, &actual);
    } else okay = entry->capability.read(entry->capability.context, offset, data, count, &done, &actual);
    if (done > count) { free(data); return guest_fail(error, QA_ERROR_FORMAT, a[0], "Linux file capability reported impossible completion"); }
    if (!positional) entry->offset = offset + done;
    if (!write && done && !qa_native_guest_write(owner->guest, a[1], (qa_bytes){data, done}, error)) { free(data); return false; }
    *result = done || okay ? (int64_t)done : failure(owner, &actual); free(data); return true;
}
static bool open_file(qa_native_sysv_program *owner, uint64_t path, uint64_t flags,
    int64_t *result, qa_error *error)
{
    char name[4096];
    if (!string_read(owner, path, name)) { *result = -K_EFAULT; return true; }
    if ((flags & 3u) == 3u || flags & ~(UINT64_C(3) | 64u | 128u | 512u | 1024u | 32768u | 524288u)) {
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
        return guest_fail(error, QA_ERROR_FORMAT, number, "Linux open acquired an incomplete actual capability");
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
    qa_buffer snapshot = {0};
    if (!(a[3] & 32u)) {
        program_file *entry = file(owner, a[4]);
        if (!entry || !(entry->capability.mode & 1u)) { *result = -K_EBADF; return true; }
        uint64_t size; qa_error actual = {0};
        if (!entry->capability.size(entry->capability.context, &size, &actual)) { *result = failure(owner, &actual); return true; }
        if (size > SIZE_MAX || size > owner->options.guest.maximum_backing_bytes) { *result = -K_ENOMEM; return true; }
        snapshot.size = (size_t)size; snapshot.data = size ? malloc((size_t)size) : NULL;
        if (size && !snapshot.data) { *result = -K_ENOMEM; return true; }
        size_t offset = 0;
        while (offset < snapshot.size) {
            size_t done = 0;
            bool okay = entry->capability.read(entry->capability.context, offset, snapshot.data + offset,
                snapshot.size - offset, &done, &actual);
            if (done > snapshot.size - offset) { qa_buffer_free(&snapshot); return guest_fail(error, QA_ERROR_FORMAT, a[4], "Linux mapping read reported impossible completion"); }
            offset += done;
            if (!okay || !done) { *result = okay ? -K_EIO : failure(owner, &actual); qa_buffer_free(&snapshot); return true; }
        }
    }
    qa_error actual = {0}; bool okay = true;
    if (fixed && !(a[3] & 1048576u)) okay = remove_pages(owner, base, bytes, &actual);
    qa_native_guest_mapping mapped;
    uint32_t rights = (uint32_t)a[2];
    if (owner->options.read_implies_execute && (rights & QA_NATIVE_GUEST_READ)) rights |= QA_NATIVE_GUEST_EXECUTE;
    if (okay) okay = a[3] & 32u ? qa_native_guest_map(owner->guest, base, bytes, rights, (qa_bytes){0}, &mapped, &actual) :
        qa_native_guest_map_file(owner->guest, base, bytes, rights, (qa_bytes){snapshot.data, snapshot.size}, a[5], &mapped, &actual);
    qa_buffer_free(&snapshot);
    if (!okay && owner->guest->failed) { if (error) *error = actual; return false; }
    if (okay && !fixed) owner->mapping_cursor = base + bytes;
    *result = okay ? (int64_t)base : framework_failure(&actual); return true;
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
    uint64_t total = 0; bool okay = true; int64_t current = 0;
    for (size_t i = 0; okay && i < count && total < UINT32_C(0x7ffff000); ++i) {
        uint64_t amount = qa_load_u64le(vectors + i * 16 + 8);
        if (amount > (uint64_t)INT64_MAX - total) { current = -K_EINVAL; break; }
        if (amount > UINT32_C(0x7ffff000) - total) amount = UINT32_C(0x7ffff000) - total;
        uint64_t arguments[6] = {a[0], qa_load_u64le(vectors + i * 16), amount, 0, 0, 0};
        okay = io(owner, arguments, write, false, &current, error);
        if (!okay || current < 0) break;
        total += (uint64_t)current;
        if ((uint64_t)current < amount) break;
    }
    free(vectors); *result = total ? (int64_t)total : current; return okay;
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
        if (next > old) changed = qa_native_guest_map(guest, old, (size_t)(next - old),
            QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, (qa_bytes){0}, &mapping, &actual);
        else if (next < old) changed = remove_pages(owner, next, (size_t)(old - next), &actual);
        if (!changed && guest->failed) { if (error) *error = actual; return false; }
        if (changed) owner->current_break = a[0];
        result = (int64_t)owner->current_break; break;
    }
    case 39: result = (int64_t)owner->status.process_id; break;
    case 186: result = (int64_t)owner->status.thread_id; break;
    case 60: case 231: {
        if (owner->status.clear_child_tid && user_range(owner, owner->status.clear_child_tid, 4, QA_NATIVE_GUEST_WRITE)) {
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
        if (!owner->options.services.clock(owner->options.services.context, clock, &seconds, &nanos, &actual)) { result = failure(owner, &actual); break; }
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
