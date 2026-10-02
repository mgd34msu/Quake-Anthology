#include "internal.h"
#include "windows_crt.h"
#include "qa/filesystem.h"

static uint64_t word(const qa_native_value *v)
{
    switch (v->type) {
    case QA_NATIVE_ADDRESS: return v->as.address;
    case QA_NATIVE_U32: return v->as.u32;
    case QA_NATIVE_I32: return (uint64_t)(int64_t)v->as.i32;
    case QA_NATIVE_I64: return (uint64_t)v->as.i64;
    default: return v->as.u64;
    }
}
static void returned(qa_native_value *out, qa_native_value_type type, uint64_t value)
{
    *out = (qa_native_value){.type = type};
    if (type == QA_NATIVE_ADDRESS) out->as.address = value;
    else if (type == QA_NATIVE_U32) out->as.u32 = (uint32_t)value;
    else if (type == QA_NATIVE_I32) { uint32_t bits = (uint32_t)value; memcpy(&out->as.i32, &bits, 4); }
    else if (type == QA_NATIVE_I64) memcpy(&out->as.i64, &value, 8);
    else if (type != QA_NATIVE_VOID) out->as.u64 = value;
}
static size_t file_size(const guest_windows *r, const windows_stdio_file *f)
{ return f->legacy ? r->target.pointer_bytes == 4 ? 32 : 48 : r->target.pointer_bytes; }
static size_t flag_offset(const guest_windows *r)
{ return r->target.pointer_bytes == 4 ? 12 : 24; }
static bool flags_store(guest_windows *r, windows_stdio_file *f, qa_error *e)
{ return !f->legacy || windows_write(r, f->address + flag_offset(r), 4, f->flags, e); }
static bool crt_errno(guest_windows *r, uint32_t value, qa_error *e)
{ return windows_write(r, r->crt->error_number, 4, value, e); }
static bool file_error(guest_windows *r, windows_stdio_file *f, uint32_t value, qa_error *e)
{
    if (f) f->flags |= WST_FLAG_ERROR;
    return crt_errno(r, value, e) && (!f || flags_store(r, f, e));
}
static windows_stdio_file *file_at(guest_windows *r, uint64_t address)
{
    for (size_t i = 0; i < r->crt->file_count; ++i)
        if (r->crt->files[i].address == address) return r->crt->files + i;
    return NULL;
}
bool windows_stdio_file_in_use(const guest_windows *r, uint64_t handle)
{
    if (!r || !r->crt || !handle) return false;
    for (size_t i = 0; i < r->crt->file_count; ++i)
        if (r->crt->files[i].handle == handle) return true;
    return false;
}
static bool path_string(guest_windows *r, uint64_t address, char **out, qa_error *e)
{
    uint16_t *units = NULL; size_t count = 0;
    if (!address) return guest_fail(e, QA_ERROR_ARGUMENT, 0, "Windows fopen requires terminated path and mode strings");
    if (!windows_string(r, address, false, &units, &count, e)) return false;
    char *text = malloc(count + 1);
    if (!text) { free(units); return guest_fail(e, QA_ERROR_MEMORY, 0, "holding Windows fopen string"); }
    for (size_t i = 0; i < count; ++i) text[i] = (char)units[i];
    text[count] = 0; free(units); *out = text; return true;
}
static void pending_remove(guest_windows_crt *crt, size_t i)
{
    free(crt->pending_files[i]->name); free(crt->pending_files[i]);
    memmove(crt->pending_files + i, crt->pending_files + i + 1,
        (crt->pending_file_count - i - 1) * sizeof(*crt->pending_files));
    --crt->pending_file_count;
}
static bool open_file(windows_service *service, uint64_t name_address, uint64_t mode_address,
    uint64_t *out, qa_error *e)
{
    guest_windows *r = service->owner; guest_windows_crt *crt = r->crt;
    char *name = NULL, *mode = NULL; bool okay = false; *out = 0;
    if (!name_address || !mode_address) return crt_errno(r, 22, e);
    if (!path_string(r, name_address, &name, e) || !path_string(r, mode_address, &mode, e)) goto done;
    bool plus = false, binary = false, text = false;
    if (!*name || (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a')) goto invalid;
    for (size_t i = 1; mode[i]; ++i) {
        if (mode[i] == '+' && !plus) plus = true;
        else if (mode[i] == 'b' && !binary && !text) binary = true;
        else if (mode[i] == 't' && !binary && !text) text = true;
        else goto invalid;
    }
    uint32_t rights = plus ? 3 : mode[0] == 'r' ? GUEST_RUNTIME_FILE_READ : GUEST_RUNTIME_FILE_WRITE;
    uint32_t creation = mode[0] == 'w' ? QA_FS_CREATE_ALWAYS : mode[0] == 'a' ? QA_FS_OPEN_ALWAYS : QA_FS_OPEN_EXISTING;
    guest_runtime_file_view view = {0}; bool found = false, busy = false, fresh = false;
    for (size_t i = 0; i < guest_runtime_resources_count(r->resources); ++i) {
        guest_runtime_file_view candidate;
        if (!guest_runtime_resources_at(r->resources, i, &candidate, e)) goto done;
        if (candidate.closed || !candidate.name || strcmp(candidate.name, name)) continue;
        if (windows_stdio_file_in_use(r, candidate.id)) { busy = true; continue; }
        if (found) { okay = crt_errno(r, 16, e); goto done; }
        view = candidate; found = true;
    }
    if (found && (view.mode & rights) != rights) { okay = crt_errno(r, 13, e); goto done; }
    if (crt->next_file > INT32_MAX) { okay = crt_errno(r, 24, e); goto done; }
    if (!guest_grow((void **)&crt->files, &crt->file_capacity, crt->file_count + 1, sizeof(*crt->files), e)) goto done;
    windows_stdio_file f = {.mode = rights, .descriptor = crt->next_file,
        .legacy = !strcmp(service->library, "msvcrt.dll"), .binary = binary, .append = mode[0] == 'a'};
    f.flags = (uint32_t)(WST_FLAG_UNBUFFERED | (plus ? WST_FLAG_UPDATE : mode[0] == 'r' ? WST_FLAG_READ : WST_FLAG_WRITE));
    if (!windows_allocate(r, file_size(r, &f), 0, &f.address, e)) goto done;
    if (!f.address) { okay = crt_errno(r, 12, e); goto done; }
    if (!windows_zero(r, f.address, file_size(r, &f), e) || !flags_store(r, &f, e) ||
        (f.legacy && !windows_write(r, f.address + flag_offset(r) + 4, 4, f.descriptor, e))) goto allocation_failed;
    if (!found) {
        if (!r->capabilities.open_file) { okay = crt_errno(r, busy ? 16 : 2, e); goto allocation_failed; }
        windows_stdio_pending *pending = calloc(1, sizeof(*pending));
        if (!pending) { guest_fail(e, QA_ERROR_MEMORY, 0, "retaining Windows fopen capability"); goto allocation_failed; }
        pending->name = name; name = NULL; pending->creation = creation;
        if (!guest_grow((void **)&crt->pending_files, &crt->pending_file_capacity,
            crt->pending_file_count + 1, sizeof(*crt->pending_files), e)) {
            free(pending->name); free(pending); goto allocation_failed;
        }
        size_t index = crt->pending_file_count; crt->pending_files[crt->pending_file_count++] = pending;
        qa_error opening = {0};
        bool opened = r->capabilities.open_file(r->capabilities.context, pending->name, rights, creation,
            &pending->capability, &pending->opened, &opening);
        if (!opened) {
            if (!pending->opened) {
                pending_remove(crt, index);
                if (opening.code == QA_ERROR_NOT_FOUND || opening.code == QA_ERROR_IO) {
                    okay = crt_errno(r, opening.code == QA_ERROR_NOT_FOUND ? 2 : 5, e);
                    goto allocation_failed;
                }
            }
            if (e) *e = opening;
            goto allocation_failed;
        }
        if (!pending->opened) { pending_remove(crt, index); okay = crt_errno(r, 2, e); goto allocation_failed; }
        if (!guest_runtime_resources_file(r->resources, f.address, pending->name, creation, &pending->capability, e))
            goto allocation_failed;
        pending->opened = false; pending_remove(crt, index);
        if (!guest_runtime_resources_find(r->resources, f.address, &view, e)) goto allocation_failed;
        fresh = true;
    }
    f.handle = view.id; f.capability = view.capability;
    /* Once the real capability is owned, all partial preparation remains
     * represented by this FILE row for checked retirement. */
    crt->files[crt->file_count++] = f; ++crt->next_file;
    windows_stdio_file *entry = crt->files + crt->file_count - 1;
    uint64_t start = 0;
    if (mode[0] == 'w' && !fresh && !guest_runtime_resources_truncate(r->resources, f.handle, 0, e)) goto done;
    if (entry->append && plus && !binary) {
        uint64_t size; uint8_t last = 0; size_t count = 0;
        if (!guest_runtime_resources_size(r->resources, f.handle, &size, e)) goto done;
        if (size && (!guest_runtime_resources_seek(r->resources, f.handle, size - 1, e) ||
            !guest_runtime_resources_read(r->resources, f.handle, &last, 1, &count, e))) goto done;
        if (count && last == 26 && !guest_runtime_resources_truncate(r->resources, f.handle, size - 1, e)) goto done;
    }
    if (!guest_runtime_resources_seek(r->resources, f.handle, start, e)) goto done;
    *out = f.address; okay = true; goto done;
invalid:
    okay = crt_errno(r, 22, e); goto done;
allocation_failed:
    if (!windows_free(r, f.address, 0, e)) okay = false;
done:
    free(name); free(mode); return okay;
}
static bool pending_write(guest_windows *r, windows_stdio_file *f, qa_error *e)
{
    if (!f->has_pending) return true;
    size_t count = 0; qa_error failure = {0};
    bool okay = guest_runtime_resources_write(r->resources, f->handle, (qa_bytes){&f->pending, 1}, &count, &failure);
    if (count) f->has_pending = false;
    if (!okay) { if (e) *e = failure; return false; }
    return count || guest_fail(e, QA_ERROR_IO, f->handle, "Windows FILE pending output made no progress");
}
static bool flush_file(guest_windows *r, windows_stdio_file *f, qa_error *e)
{
    return !(f->mode & GUEST_RUNTIME_FILE_WRITE) ||
        (pending_write(r, f, e) && guest_runtime_resources_flush(r->resources, f->handle, e));
}
static bool read_byte(guest_windows *r, windows_stdio_file *f, uint8_t *out, bool *present, qa_error *e)
{
    *present = false;
    if (f->has_pushback) { *out = f->pushback; f->has_pushback = false; *present = true; return true; }
    if (f->flags & WST_FLAG_EOF) return true;
    uint8_t byte = 0; size_t count = 0;
    bool okay = guest_runtime_resources_read(r->resources, f->handle, &byte, 1, &count, e);
    if (!okay) { if (count) { *out = byte; *present = true; } return false; }
    if (!count || (!f->binary && byte == 26)) {
        f->flags |= WST_FLAG_EOF; return flags_store(r, f, e);
    }
    if (!f->binary && byte == 13) {
        uint8_t next = 0;
        okay = guest_runtime_resources_read(r->resources, f->handle, &next, 1, &count, e);
        if (!okay) {
            *out = count && next == 10 ? 10 : byte; *present = true;
            if (count && next != 10) { f->pushback = next; f->has_pushback = true; }
            return false;
        }
        if (count && next == 10) byte = 10;
        else if (count) {
            guest_runtime_file_view view;
            if (!guest_runtime_resources_find(r->resources, f->handle, &view, e) ||
                !guest_runtime_resources_seek(r->resources, f->handle, view.offset - 1, e)) return false;
        }
    }
    *out = byte; *present = true; return true;
}
static bool write_byte(guest_windows *r, windows_stdio_file *f, uint8_t byte, bool *completed, qa_error *e)
{
    *completed = false;
    if (!pending_write(r, f, e)) return false;
    if (f->append) {
        uint64_t size;
        if (!guest_runtime_resources_size(r->resources, f->handle, &size, e) ||
            !guest_runtime_resources_seek(r->resources, f->handle, size, e)) return false;
    }
    uint8_t bytes[2] = {byte, 0}; size_t total = 1, count = 0;
    if (!f->binary && byte == 10) { bytes[0] = 13; bytes[1] = 10; total = 2; }
    bool okay = guest_runtime_resources_write(r->resources, f->handle, (qa_bytes){bytes, total}, &count, e);
    if (count == 1 && total == 2) { f->pending = 10; f->has_pending = true; }
    *completed = count == total;
    if (!okay) return false;
    if (f->has_pending && !pending_write(r, f, e)) {
        *completed = !f->has_pending;
        return false;
    }
    *completed = count != 0 && !f->has_pending;
    return *completed || guest_fail(e, QA_ERROR_IO, f->handle, "Windows FILE output made no progress");
}
static bool io_failure(guest_windows *r, windows_stdio_file *f, const qa_error *failure, qa_error *e)
{
    if (!file_error(r, f, 5, e)) return false;
    if (failure->code == QA_ERROR_IO) return true;
    if (e) *e = *failure;
    return false;
}
static bool transfer(guest_windows *r, windows_stdio_file *f, bool writing, uint64_t address,
    uint64_t size, uint64_t elements, uint64_t *out, qa_error *e)
{
    *out = 0;
    if (!size || !elements) return true;
    uint64_t maximum = r->target.pointer_bytes == 4 ? UINT32_MAX : UINT64_MAX;
    if (size > maximum / elements || size * elements > SIZE_MAX) return file_error(r, f, 22, e);
    size_t bytes = (size_t)(size * elements);
    if (!guest_range(r->guest, address, bytes, writing ? QA_NATIVE_GUEST_READ : QA_NATIVE_GUEST_WRITE, e)) return false;
    if (!(f->mode & (writing ? GUEST_RUNTIME_FILE_WRITE : GUEST_RUNTIME_FILE_READ))) return file_error(r, f, 9, e);
    uint8_t data[16384]; size_t done = 0;
    while (done < bytes) {
        qa_error failure = {0}; size_t count = 0;
        bool okay = true;
        if (f->binary && !f->has_pushback) {
            size_t chunk = bytes - done < sizeof(data) ? bytes - done : sizeof(data);
            if (writing) {
                if (!qa_native_guest_read(r->guest, address + done, data, chunk, e)) return false;
                if (f->append) {
                    uint64_t end;
                    if (!guest_runtime_resources_size(r->resources, f->handle, &end, e) ||
                        !guest_runtime_resources_seek(r->resources, f->handle, end, e)) return false;
                }
                okay = guest_runtime_resources_write(r->resources, f->handle, (qa_bytes){data, chunk}, &count, &failure);
            } else if (!(f->flags & WST_FLAG_EOF)) {
                okay = guest_runtime_resources_read(r->resources, f->handle, data, chunk, &count, &failure);
            }
            if (!writing && count && !qa_native_guest_write(r->guest, address + done, (qa_bytes){data, count}, e)) return false;
        } else {
            bool present = false;
            if (writing) {
                if (!qa_native_guest_read(r->guest, address + done, data, 1, e)) return false;
                okay = write_byte(r, f, *data, &present, &failure);
            } else {
                okay = read_byte(r, f, data, &present, &failure);
                if (present && !qa_native_guest_write(r->guest, address + done, (qa_bytes){data, 1}, e)) return false;
            }
            count = present ? 1 : 0;
        }
        done += count; *out = done / size;
        if (!okay) return io_failure(r, f, &failure, e);
        if (!count) {
            if (writing) return file_error(r, f, 5, e);
            f->flags |= WST_FLAG_EOF; return flags_store(r, f, e);
        }
    }
    return true;
}
static bool seek_file(guest_windows *r, windows_stdio_file *f, int64_t offset,
    uint64_t origin, bool *accepted, qa_error *e)
{
    *accepted = false;
    if (origin > 2) return crt_errno(r, 22, e);
    if (!flush_file(r, f, e)) return false;
    uint64_t base = 0;
    if (origin == 1) {
        guest_runtime_file_view view;
        if (!guest_runtime_resources_find(r->resources, f->handle, &view, e)) return false;
        base = view.offset;
        if (f->has_pushback && base) --base;
    } else if (origin == 2 && !guest_runtime_resources_size(r->resources, f->handle, &base, e)) return false;
    uint64_t distance = offset < 0 ? UINT64_C(0) - (uint64_t)offset : (uint64_t)offset;
    if ((offset < 0 && distance > base) || (offset >= 0 && (base > INT64_MAX || distance > (uint64_t)INT64_MAX - base)))
        return crt_errno(r, 22, e);
    uint64_t destination = offset < 0 ? base - distance : base + distance;
    if (destination > INT64_MAX) return crt_errno(r, 22, e);
    if (!guest_runtime_resources_seek(r->resources, f->handle, destination, e)) return false;
    f->has_pushback = false; f->flags &= ~(uint32_t)WST_FLAG_EOF;
    *accepted = true; return flags_store(r, f, e);
}
bool windows_stdio_invoke(windows_service *service, const qa_native_value *args, size_t count,
    qa_native_value *out, qa_error *e)
{
    guest_windows *r = service->owner; uint32_t op = service->operation;
    qa_native_value_type type = service->function.signature.result.kind;
    uint64_t a = count ? word(args) : 0, value = UINT64_MAX;
    returned(out, type, op == WST_OPEN || op == WST_READ || op == WST_WRITE ? 0 : value);
    if (op == WST_OPEN) {
        bool okay = open_file(service, a, word(args + 1), &value, e); returned(out, type, value); return okay;
    }
    if (op == WST_FLUSH && !a) {
        for (size_t i = 0; i < r->crt->file_count; ++i) {
            windows_stdio_file *f = r->crt->files + i; qa_error failure = {0};
            if (f->closing || f->legacy != !strcmp(service->library, "msvcrt.dll")) continue;
            if (!flush_file(r, f, &failure)) return io_failure(r, f, &failure, e);
        }
        returned(out, type, 0); return true;
    }
    uint64_t address = op == WST_READ || op == WST_WRITE ? word(args + 3) :
        op == WST_PUT || op == WST_UNGET ? word(args + 1) : a;
    windows_stdio_file *f = file_at(r, address);
    if (!f || f->legacy != !strcmp(service->library, "msvcrt.dll") ||
        (f->closing && op != WST_CLOSE)) return crt_errno(r, 9, e);
    if (op == WST_CLOSE) {
        qa_error failure = {0};
        if (!f->closing && !flush_file(r, f, &failure)) return io_failure(r, f, &failure, e);
        f->closing = true;
        if (!guest_runtime_resources_close(r->resources, f->handle, &failure)) return io_failure(r, f, &failure, e);
        size_t index = (size_t)(f - r->crt->files);
        if (!windows_free(r, f->address, 0, e)) return false;
        memmove(f, f + 1, (r->crt->file_count - index - 1) * sizeof(*f)); --r->crt->file_count;
        returned(out, type, 0); return true;
    }
    if (op == WST_READ || op == WST_WRITE) {
        bool okay = transfer(r, f, op == WST_WRITE, a, word(args + 1), word(args + 2), &value, e);
        returned(out, type, value); return okay;
    }
    if (op == WST_SEEK || op == WST_SEEK64 || op == WST_REWIND) {
        int64_t offset = 0;
        if (op == WST_SEEK) offset = args[1].as.i32;
        else if (op == WST_SEEK64) offset = args[1].as.i64;
        bool accepted = false; qa_error failure = {0};
        bool okay = seek_file(r, f, offset, op == WST_REWIND ? 0 : word(args + 2), &accepted, &failure);
        if (!okay) return io_failure(r, f, &failure, e);
        if (op == WST_REWIND) { f->flags &= ~(uint32_t)WST_FLAG_ERROR; if (!flags_store(r, f, e)) return false; }
        returned(out, type, accepted ? 0 : UINT64_MAX); return true;
    }
    if (op == WST_TELL || op == WST_TELL64) {
        guest_runtime_file_view view;
        if (!guest_runtime_resources_find(r->resources, f->handle, &view, e)) return false;
        value = view.offset;
        if (f->has_pushback && value) --value;
        if (f->has_pending) {
            if (value == UINT64_MAX) return crt_errno(r, 22, e);
            ++value;
        }
        if (value > (op == WST_TELL ? (uint64_t)INT32_MAX : (uint64_t)INT64_MAX)) return crt_errno(r, 22, e);
    } else if (op == WST_FLUSH) {
        qa_error failure = {0}; if (!flush_file(r, f, &failure)) return io_failure(r, f, &failure, e);
        value = 0;
    } else if (op == WST_EOF) value = (f->flags & WST_FLAG_EOF) != 0;
    else if (op == WST_ERROR) value = (f->flags & WST_FLAG_ERROR) != 0;
    else if (op == WST_CLEAR) { f->flags &= ~(uint32_t)(WST_FLAG_EOF | WST_FLAG_ERROR); if (!flags_store(r, f, e)) return false; value = 0; }
    else if (op == WST_FILENO) value = f->descriptor;
    else if (op == WST_GET || op == WST_PUT) {
        /* Legacy getc/putc macros decrement _cnt before calling the real
         * refill/overflow import. Unbuffered FILEs restore it after entry. */
        if (f->legacy && !windows_write(r, f->address + r->target.pointer_bytes, 4, 0, e)) return false;
        if (!(f->mode & (op == WST_GET ? GUEST_RUNTIME_FILE_READ : GUEST_RUNTIME_FILE_WRITE))) return file_error(r, f, 9, e);
        uint8_t byte = (uint8_t)a; bool present = false; qa_error failure = {0};
        bool okay = op == WST_GET ? read_byte(r, f, &byte, &present, &failure) : write_byte(r, f, byte, &present, &failure);
        value = present ? byte : UINT64_MAX;
        if (!okay) { returned(out, type, value); return io_failure(r, f, &failure, e); }
    } else if (op == WST_UNGET) {
        if ((uint32_t)a != UINT32_MAX && !f->has_pushback && (f->mode & GUEST_RUNTIME_FILE_READ)) {
            f->pushback = (uint8_t)a; f->has_pushback = true; f->flags &= ~(uint32_t)WST_FLAG_EOF;
            if (!flags_store(r, f, e)) return false;
            value = f->pushback;
        }
    } else return guest_fail(e, QA_ERROR_ARGUMENT, op, "invalid Windows FILE operation");
    returned(out, type, value); return true;
}
bool windows_stdio_close_files(guest_windows *r, qa_error *e)
{
    if (!r || !r->crt) return true;
    for (size_t i = 0; i < r->crt->file_count; ++i) {
        windows_stdio_file *f = r->crt->files + i;
        guest_runtime_file_view view;
        if (!guest_runtime_resources_find(r->resources, f->handle, &view, e)) return false;
        if (view.closed) continue;
        /* Adoption has transferred matching close owners and frozen the old
         * registry. Only its remaining close capabilities may run here. */
        if (!r->retired && !f->closing && !flush_file(r, f, e)) return false;
        f->closing = true;
        if (!guest_runtime_resources_close(r->resources, f->handle, e)) return false;
    }
    while (r->crt->pending_file_count) {
        windows_stdio_pending *pending = r->crt->pending_files[0];
        if (pending->opened && (!pending->capability.close ||
            !pending->capability.close(pending->capability.context, e))) return false;
        pending->opened = false; pending_remove(r->crt, 0);
    }
    return true;
}
bool windows_stdio_valid(const guest_windows *r, qa_error *e)
{
    const guest_windows_crt *crt = r->crt;
    if (r->guest && crt->has_file_opener != (r->capabilities.open_file != NULL))
        return guest_fail(e, QA_ERROR_FORMAT, 0, "Windows FILE opener differs from its retained process capability");
    if (crt->next_file < 3 || crt->next_file > (uint32_t)INT32_MAX + 1u || crt->pending_file_count)
        return guest_fail(e, QA_ERROR_FORMAT, 0, "Windows FILE continuation has an invalid descriptor cursor or unfinished open");
    for (size_t i = 0; i < crt->file_count; ++i) {
        const windows_stdio_file *f = crt->files + i; guest_runtime_file_view view; uint64_t allocation;
        if (!f->address || !f->handle || !f->capability || !f->mode || (f->mode & ~3u) ||
            f->descriptor < 3 || f->descriptor >= crt->next_file || f->descriptor > INT32_MAX ||
            (f->flags & ~(uint32_t)(WST_FLAG_READ | WST_FLAG_WRITE | WST_FLAG_UNBUFFERED | WST_FLAG_EOF | WST_FLAG_ERROR | WST_FLAG_UPDATE)) ||
            !(f->flags & WST_FLAG_UNBUFFERED) ||
            (f->flags & (WST_FLAG_READ | WST_FLAG_WRITE | WST_FLAG_UPDATE)) !=
                (uint32_t)(f->mode == 3 ? WST_FLAG_UPDATE : f->mode == 1 ? WST_FLAG_READ : WST_FLAG_WRITE) ||
            (f->append && !(f->mode & GUEST_RUNTIME_FILE_WRITE)) ||
            (f->has_pushback && !(f->mode & GUEST_RUNTIME_FILE_READ)) ||
            (f->has_pending && (f->binary || !(f->mode & GUEST_RUNTIME_FILE_WRITE) || f->pending != 10)))
            return guest_fail(e, QA_ERROR_FORMAT, f->address, "invalid retained Windows FILE state");
        if ((r->target.pointer_bytes == 4 && (f->address > UINT32_MAX ||
            file_size(r, f) - 1 > UINT32_MAX - f->address)) ||
            !windows_allocation_size((guest_windows *)r, f->address, 0, &allocation) || allocation != file_size(r, f))
            return guest_fail(e, QA_ERROR_FORMAT, f->address, "Windows FILE differs from its retained heap allocation");
        if (!guest_runtime_resources_find(r->resources, f->handle, &view, e)) return false;
        if (view.capability != f->capability || (view.mode & f->mode) != f->mode || (view.closed && !f->closing))
            return guest_fail(e, QA_ERROR_FORMAT, f->handle, "Windows FILE capability differs from its retained resource");
        for (size_t j = 0; j < i; ++j)
            if (crt->files[j].address == f->address || crt->files[j].handle == f->handle || crt->files[j].descriptor == f->descriptor)
                return guest_fail(e, QA_ERROR_FORMAT, f->address, "duplicate retained Windows FILE identity");
    }
    return true;
}
bool windows_stdio_lower_valid(guest_windows *r, qa_error *e)
{
    if (!windows_stdio_valid(r, e)) return false;
    for (size_t i = 0; i < r->crt->file_count; ++i) {
        const windows_stdio_file *f = r->crt->files + i; uint64_t size, value;
        if (!windows_allocation_size(r, f->address, 0, &size) || size != file_size(r, f) ||
            !windows_validate_storage(r, f->address, file_size(r, f), 0x57484c, e))
            return guest_fail(e, QA_ERROR_FORMAT, f->address, "Windows FILE is not its actual CRT guest allocation");
        if (!f->legacy) {
            if (!windows_read(r, f->address, r->target.pointer_bytes, &value, e) || value)
                return guest_fail(e, QA_ERROR_FORMAT, f->address, "invalid opaque UCRT FILE storage");
        } else {
            const size_t width = r->target.pointer_bytes;
            size_t base = width == 4 ? 8 : 16;
            size_t offsets[] = {0, width, base, flag_offset(r), flag_offset(r) + 4,
                flag_offset(r) + 8, flag_offset(r) + 12, flag_offset(r) + 16};
            for (size_t j = 0; j < 8; ++j) {
                size_t bytes = j == 0 || j == 2 || j == 7 ? width : 4;
                uint64_t expected = j == 3 ? f->flags : j == 4 ? f->descriptor : 0;
                if (!windows_read(r, f->address + offsets[j], bytes, &value, e) || value != expected)
                    return guest_fail(e, QA_ERROR_FORMAT, f->address + offsets[j], "MSVCRT FILE fields differ from their continuation");
            }
        }
    }
    return true;
}
