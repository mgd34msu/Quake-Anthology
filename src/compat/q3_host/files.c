#include "files.h"

struct qa_q3_host_write_file {
    qa_fs_stream *stream;
    char *path;
    qa_fs_stream_mode mode;
    uint64_t position;
    qa_fs_stream_reference reference;
};

bool q3_write_view_root(const qa_q3_host_options *options, qa_fs_root **out,
                         qa_error *error)
{
    if (!options || !out)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 write view requires host options and output");
    qa_fs_root *legacy = NULL;
    if (options->writable_mount) {
        bool found = false;
        for (size_t i = 0; i < qa_vfs_mount_count(options->mounts); ++i) {
            qa_vfs_mount_info info;
            if (!qa_vfs_mount_at(options->mounts, i, &info) || info.id != options->writable_mount) continue;
            if (info.is_archive || !info.writable)
                return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 writable mount is not a writable directory");
            legacy = qa_vfs_mount_root(options->mounts, info.id);
            found = legacy != NULL;
            break;
        }
        if (!found)
            return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 writable mount lacks its retained native root");
    }
    qa_fs_root *root = options->write_view.root;
    const qa_fs_stream_resolver *resolver = &options->write_view.resolver;
    if ((resolver->context != NULL) != (resolver->root != NULL) ||
        ((resolver->context || resolver->root) && !root))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 write view has an incomplete native resolver");
    if (root && legacy && !qa_fs_root_same_object(root, legacy))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 write view and legacy writable mount differ");
    *out = root ? root : legacy;
    return true;
}

static bool open_write_file(qa_fs_root *root, const char *path, qa_fs_stream_mode mode,
                             qa_q3_host_write_file **out,
                             qa_fs_stream_open_stage *stage, qa_error *error)
{
    if (stage) *stage = QA_FS_STREAM_OPEN_PREPARE;
    if (out) *out = NULL;
    if (!root || !out || mode < QA_FS_STREAM_WRITE || mode > QA_FS_STREAM_APPEND_SYNC ||
        !path)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 writable stream owner or mode");
    char *normalized = qa_vfs_normalize_path(path, error);
    if (!normalized) return false;
    qa_q3_host_write_file *file = calloc(1, sizeof(*file));
    if (!file) {
        free(normalized);
        return q3_fail(error, QA_ERROR_MEMORY, 0, "Allocating Q3 writable stream");
    }
    file->path = normalized;
    file->mode = mode;
    /* Native writes are unbuffered, including the source append-sync mode. */
    qa_fs_stream_mode native_mode = mode == QA_FS_STREAM_APPEND_SYNC ? QA_FS_STREAM_APPEND : mode;
    uint64_t size = 0;
    if (!qa_fs_root_stream_open_result(root, normalized, native_mode, false,
                                        &file->stream, &size, stage, error)) {
        q3_write_file_close(file);
        return false;
    }
    file->position = mode == QA_FS_STREAM_WRITE ? 0 : size;
    if (!qa_fs_stream_reference_read(file->stream, &file->reference)) {
        q3_write_file_close(file);
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 writable stream lacks its actual native reference");
    }
    file->reference.path = file->path;
    *out = file;
    return true;
}

bool q3_write_file_open(qa_fs_root *root, const char *path, qa_fs_stream_mode mode,
                         qa_q3_host_write_file **out,
                         qa_fs_stream_open_stage *stage, qa_error *error)
{
    return open_write_file(root, path, mode, out, stage, error);
}

bool q3_write_file_resume(const qa_q3_host_write_view *view, const q3_write_file_state *state,
                           qa_q3_host_write_file **out, qa_error *error)
{
    if (out) *out = NULL;
    if (!view || !view->root || !state || !out || !state->path || state->position > INT64_MAX ||
        state->mode < QA_FS_STREAM_WRITE || state->mode > QA_FS_STREAM_APPEND_SYNC ||
        ((view->resolver.context != NULL) != (view->resolver.root != NULL)))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 writable continuation lacks its real view, reference or cursor");
    if (!qa_fs_stream_reference_valid(&state->reference, error)) return false;
    qa_fs_stream_mode native_mode = state->mode == QA_FS_STREAM_APPEND_SYNC ? QA_FS_STREAM_APPEND : state->mode;
    if (state->reference.mode != native_mode || strcmp(state->path, state->reference.path))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 writable continuation reference differs from its source state");
    char *normalized = qa_vfs_normalize_path(state->path, error);
    if (!normalized) return false;
    if (strcmp(normalized, state->path)) {
        free(normalized);
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 writable continuation path is not normalized");
    }
    qa_q3_host_write_file *file = calloc(1, sizeof(*file));
    if (!file) {
        free(normalized);
        return q3_fail(error, QA_ERROR_MEMORY, 0, "Allocating resumed Q3 writable stream");
    }
    file->path = normalized; file->mode = state->mode; file->position = state->position;
    file->reference = state->reference; file->reference.path = file->path;
    uint64_t size;
    bool mapped = view->resolver.context && view->resolver.root;
    bool ok = mapped ? qa_fs_stream_resume_mapped(view->root, &file->reference,
        &view->resolver, &file->stream, &size, error) :
        qa_fs_stream_resume(view->root, &file->reference, &file->stream, &size, error);
    if (!ok) {
        q3_write_file_close(file);
        return false;
    }
    *out = file;
    return true;
}

bool q3_write_file_write(qa_q3_host_write_file *file, qa_bytes bytes,
                          q3_write_result *result, qa_error *error)
{
    if (result) *result = (q3_write_result){0};
    if (!file || !result || (!bytes.data && bytes.size) || bytes.size > (size_t)PTRDIFF_MAX ||
        (file->mode == QA_FS_STREAM_WRITE &&
         (file->position > INT64_MAX || bytes.size > (uint64_t)INT64_MAX - file->position)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 writable transfer or position");
    if (!bytes.size) return true;
    size_t total = 0;
    bool retried = false;
    while (total < bytes.size) {
        size_t amount = 0;
        qa_error local = {0};
        bool ok = qa_fs_stream_write_some(file->stream, (qa_bytes){bytes.data + total, bytes.size - total},
                                            file->position, &amount, &local);
        if (!ok && local.code != QA_ERROR_IO && local.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = local;
            return false;
        }
        if (!amount) {
            if (retried) {
                result->written = 0;
                result->zero_retry_exhausted = true;
                return true;
            }
            retried = true;
            continue;
        }
        total += amount;
        result->written = total;
        if (file->mode == QA_FS_STREAM_WRITE) file->position += amount;
        else if (!qa_fs_stream_size(file->stream, &file->position, error)) return false;
    }
    return true;
}

bool q3_write_file_seek(qa_q3_host_write_file *file, int64_t offset,
                         qa_vfs_seek_origin origin, qa_error *error)
{
    if (!file || origin < QA_VFS_SEEK_SET || origin > QA_VFS_SEEK_END)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 writable seek");
    uint64_t length;
    if (!qa_fs_stream_size(file->stream, &length, error)) return false;
    uint64_t base = origin == QA_VFS_SEEK_CURRENT ? file->position :
        origin == QA_VFS_SEEK_END ? length : 0;
    uint64_t magnitude = offset < 0 ? (uint64_t)(-(offset + 1)) + 1 : (uint64_t)offset;
    if ((offset < 0 && magnitude > base) ||
        (offset >= 0 && (base > INT64_MAX || magnitude > (uint64_t)INT64_MAX - base)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 writable seek is outside the supported range");
    file->position = offset < 0 ? base - magnitude : base + magnitude;
    return true;
}

q3_write_file_state q3_write_file_capture(const qa_q3_host_write_file *file)
{
    return file ? (q3_write_file_state){file->path, file->mode, file->position, file->reference} :
        (q3_write_file_state){0};
}

bool q3_write_file_portable_ready(const qa_q3_host_write_view *view,
    const qa_q3_host_write_file *file, qa_error *error)
{
    if (!view || !view->root || !view->resolver.context || !view->resolver.root ||
        !file || !file->stream || !file->path || file->position > INT64_MAX ||
        file->mode < QA_FS_STREAM_WRITE || file->mode > QA_FS_STREAM_APPEND_SYNC ||
        !qa_fs_root_same_object(view->root, qa_fs_stream_root(file->stream)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Portable writable file lost its retained mapped root or stream");
    qa_fs_stream_reference current;
    if (!qa_fs_stream_reference_valid(&file->reference, error)) return false;
    if (!qa_fs_stream_reference_read(file->stream, &current))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Portable writable file lacks its current native receipt");
    if (!qa_fs_stream_reference_valid(&current, error)) return false;
    qa_fs_stream_mode native_mode = file->mode == QA_FS_STREAM_APPEND_SYNC ?
        QA_FS_STREAM_APPEND : file->mode;
    if (current.mode != native_mode || file->reference.mode != native_mode ||
        strcmp(current.path, file->path) || strcmp(file->reference.path, file->path))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Portable writable file differs from its source path or mode");
    /* Resume already admitted the saved origin through the installed mapper.
     * Its historic root/object may differ from the current external file. */
    return true;
}

bool q3_write_file_close_checked(qa_q3_host_write_file *file, qa_error *error)
{
    if (!file) return true;
    bool ok = qa_fs_stream_close_checked(file->stream, error);
    free(file->path);
    free(file);
    return ok;
}

void q3_write_file_close(qa_q3_host_write_file *file)
{
    (void)q3_write_file_close_checked(file, NULL);
}

bool q3_file_close_checked(q3_file *file, qa_error *error)
{
    qa_resource_release(file->resource);
    bool ok = q3_write_file_close_checked(file->writable, error);
    qa_buffer_free(&file->restored_bytes);
    free(file->restored_path);
    *file = (q3_file){0};
    return ok;
}

void q3_file_close(q3_file *file)
{
    (void)q3_file_close_checked(file, NULL);
}

static qa_bytes file_bytes(const q3_file *file)
{
    return file->resource ? qa_resource_bytes(file->resource) :
                           (qa_bytes){file->restored_bytes.data, file->restored_bytes.size};
}

static q3_file *file_at(qa_q3_host *host, int32_t slot, qa_error *error)
{
    if (slot <= 0 || slot >= 64) {
        q3_fail(error, QA_ERROR_ARGUMENT, (size_t)(uint32_t)slot, "FS_FileForHandle: out of range");
        return NULL;
    }
    if (host->files[slot].kind == Q3_FILE_CLOSED) {
        q3_fail(error, QA_ERROR_ARGUMENT, (size_t)slot, "FS_FileForHandle: NULL");
        return NULL;
    }
    return &host->files[slot];
}

static size_t free_slot(qa_q3_host *host, qa_error *error)
{
    for (size_t i = 1; i < 64; ++i)
        if (host->files[i].kind == Q3_FILE_CLOSED) return i;
    q3_fail(error, QA_ERROR_FORMAT, 0, "FS_HandleForFile: none free");
    return 0;
}

static bool publish_open(q3_call *call, uint64_t destination, q3_file *file,
                          int32_t value, int32_t *result, qa_error *error)
{
    size_t slot = free_slot(call->host, error);
    if (!slot) { q3_file_close(file); return false; }
    if (call->host->file_serial == UINT64_MAX) {
        q3_file_close(file);
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 file identity exhausted");
    }
    uint64_t serial = file->serial = ++call->host->file_serial;
    call->host->files[slot] = *file;
    *file = (q3_file){0};
    if (!q3_write_word(call, destination, (uint32_t)slot, error)) {
        if (call->host->files[slot].serial == serial)
            q3_file_close(&call->host->files[slot]);
        return false;
    }
    *result = value;
    return true;
}

static bool open_file(q3_call *call, int32_t *result, qa_error *error)
{
    qa_q3_host *host = call->host;
    uint64_t destination = call->arguments[1];
    int32_t mode = q3_integer(call, 2);
    if (mode < 0 || mode > 3)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "FSH_FOpenFile: bad mode");
    if (host->options.role == QA_QVM_GAME && !destination && mode)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "writable Q3 file open needs a handle output");
    uint8_t check[4];
    if (host->options.role == QA_QVM_GAME && destination &&
        !q3_read(call, destination, check, sizeof(check), error)) return false;
    if (!call->arguments[0])
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "FS_FOpenFileRead: NULL filename");
    if (host->options.role != QA_QVM_GAME && destination &&
        !q3_read(call, destination, check, sizeof(check), error)) return false;
    qa_buffer name = {0};
    if (!q3_string(call, call->arguments[0], &name, error)) return false;
    if (!destination && mode) {
        qa_buffer_free(&name);
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "writable Q3 file open needs a handle output");
    }
    const char *path = (const char *)name.data;
    q3_file file = {0};
    bool ok = true;
    if (!mode) {
        if (*path == '/' || *path == '\\') ++path;
        if (strstr(path, "..") || strstr(path, "::") || strstr(path, "q3key")) {
            *result = destination ? -1 : 0;
            ok = !destination || q3_write_word(call, destination, 0, error);
        } else {
            if (!destination) {
                bool found = false; uint64_t size = 0;
                ok = qa_vfs_probe(host->options.mounts, path, &found, &size, error);
                if (ok) *result = found ? 1 : 0;
                qa_buffer_free(&name); return ok;
            }
            qa_mount_id mount = 0;
            qa_error local = {0};
            ok = qa_vfs_acquire(host->options.mounts, path, &file.resource, &mount, &local);
            if (!ok && local.code == QA_ERROR_NOT_FOUND) {
                *result = destination ? -1 : 0;
                ok = !destination || q3_write_word(call, destination, 0, error);
            } else if (!ok) {
                if (error) *error = local;
            } else {
                qa_bytes bytes = qa_resource_bytes(file.resource);
                if (bytes.size > INT32_MAX)
                    ok = q3_fail(error, QA_ERROR_FORMAT, bytes.size, "Q3 opened file exceeds source length");
                else {
                    file.kind = Q3_FILE_READ;
                    for (size_t i = 0; i < qa_vfs_mount_count(host->options.mounts); ++i) {
                        qa_vfs_mount_info info;
                        if (qa_vfs_mount_at(host->options.mounts, i, &info) && info.id == mount) {
                            file.zip = info.format == QA_ARCHIVE_PK3;
                            break;
                        }
                    }
                    ok = publish_open(call, destination, &file, (int32_t)bytes.size, result, error);
                }
            }
        }
    } else if (!host->options.write_view.root) {
        ok = q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 filesystem has no writable owner");
    } else {
        if (!free_slot(host, error)) ok = false;
        if (ok) {
            qa_fs_stream_mode write_mode = mode == 1 ? QA_FS_STREAM_WRITE :
                mode == 2 ? QA_FS_STREAM_APPEND : QA_FS_STREAM_APPEND_SYNC;
            qa_error local = {0};
            qa_fs_stream_open_stage stage;
            ok = q3_write_file_open(host->options.write_view.root, path,
                                     write_mode, &file.writable, &stage, &local);
            if (!ok && stage == QA_FS_STREAM_OPEN_NATIVE) {
                *result = -1;
                ok = q3_write_word(call, destination, 0, error);
            } else if (!ok) {
                if (error) *error = local;
            } else {
                file.kind = Q3_FILE_WRITE;
                ok = publish_open(call, destination, &file, 0, result, error);
            }
        }
    }
    q3_file_close(&file);
    qa_buffer_free(&name);
    return ok;
}

static bool read_file(q3_call *call, int32_t *result, qa_error *error)
{
    int32_t slot = q3_integer(call, 2), requested = q3_integer(call, 1);
    if (!slot) return true;
    if (requested < 0)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "negative Q3 file read length");
    if (call->vm && (call->arguments[0] || requested)) {
        qa_bytes admitted;
        if (!q3_vm_span(call->vm, call->arguments[0], (size_t)requested, &admitted, error)) return false;
    }
    q3_file *file = file_at(call->host, slot, error);
    if (!file) return false;
    if (file->kind != Q3_FILE_READ)
        return q3_fail(error, QA_ERROR_ARGUMENT, (size_t)slot, "FS_Read: file is not readable");
    qa_bytes bytes = file_bytes(file);
    uint64_t serial = file->serial;
    size_t count = file->position < bytes.size ? bytes.size - (size_t)file->position : 0;
    if (count > (size_t)requested) count = (size_t)requested;
    if (count && !q3_write(call, call->arguments[0],
                            (qa_bytes){bytes.data + (size_t)file->position, count}, error)) return false;
    if (file->serial == serial) file->position += count;
    if (call->native && call->native_profile == QA_NATIVE_QUAKE_LIVE_GAME_API10)
        *result = (int32_t)count;
    return true;
}

static bool write_file(q3_call *call, int32_t *result, qa_error *error)
{
    int32_t slot = q3_integer(call, 2), requested = q3_integer(call, 1);
    if (!slot) return true;
    if (requested < 0)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "negative Q3 file write length");
    qa_bytes bytes = {0};
    qa_buffer owned = {0};
    if (call->arguments[0] || requested) {
        if (call->vm) {
            if (!q3_vm_span(call->vm, call->arguments[0], (size_t)requested, &bytes, error)) return false;
        } else {
            owned.size = (size_t)requested;
            owned.data = owned.size ? malloc(owned.size) : NULL;
            if (owned.size && !owned.data)
                return q3_fail(error, QA_ERROR_MEMORY, 0, "allocating native Q3 file transfer");
            if (!q3_read(call, call->arguments[0], owned.data, owned.size, error)) {
                qa_buffer_free(&owned); return false;
            }
            bytes = (qa_bytes){owned.data, owned.size};
        }
    }
    q3_file *file = file_at(call->host, slot, error);
    bool ok = file != NULL;
    if (ok && file->kind == Q3_FILE_READ) {
        if (call->host->options.common.print)
            call->host->options.common.print(call->host->options.common.context,
                                              "FS_Write: 0 bytes written\n");
    } else if (ok) {
        q3_write_result transfer;
        ok = q3_write_file_write(file->writable, bytes, &transfer, error);
        if (ok && transfer.zero_retry_exhausted) {
            if (call->host->options.common.print)
                call->host->options.common.print(call->host->options.common.context,
                                                  "FS_Write: 0 bytes written\n");
        }
        if (ok && call->native && call->native_profile == QA_NATIVE_QUAKE_LIVE_GAME_API10)
            *result = (int32_t)transfer.written;
    }
    qa_buffer_free(&owned);
    return ok;
}

static bool seek_file(q3_call *call, int32_t *result, qa_error *error)
{
    q3_file *file = file_at(call->host, q3_integer(call, 0), error);
    if (!file) return false;
    int32_t displacement = q3_integer(call, 1), origin = q3_integer(call, 2);
    if (origin < 0 || origin > 2)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Bad origin in FS_Seek");
    if (file->kind == Q3_FILE_WRITE) {
        qa_vfs_seek_origin translated = origin == 0 ? QA_VFS_SEEK_CURRENT :
                                            origin == 1 ? QA_VFS_SEEK_END : QA_VFS_SEEK_SET;
        qa_error local = {0};
        bool ok = q3_write_file_seek(file->writable, displacement, translated, &local);
        if (!ok && (local.code == QA_ERROR_IO || local.code == QA_ERROR_NOT_FOUND || local.code == QA_ERROR_ARGUMENT)) {
            *result = -1;
            return true;
        }
        if (!ok && error) *error = local;
        return ok;
    }
    qa_bytes bytes = file_bytes(file);
    if (file->zip) {
        if (displacement < 0 || displacement >= 65536)
            return q3_fail(error, QA_ERROR_ARGUMENT, 0, "ZIP seek exceeds source scratch limit");
        file->position = (uint32_t)displacement < bytes.size ? (uint32_t)displacement : bytes.size;
        *result = !displacement && origin == 2 ? 0 : (int32_t)file->position;
        return true;
    }
    for (unsigned step = 0; step < 2; ++step) {
        uint64_t base = origin == 0 ? file->position : origin == 1 ? bytes.size : 0;
        if ((displacement < 0 && base < (uint64_t)(-(int64_t)displacement)) ||
            (displacement >= 0 && base > UINT64_MAX - (uint32_t)displacement)) {
            *result = -1;
        } else {
            file->position = displacement < 0 ? base - (uint64_t)(-(int64_t)displacement) :
                                               base + (uint32_t)displacement;
            *result = 0;
        }
    }
    return true;
}

static bool mod_listing(const char *path)
{
    static const char expected[] = "$modlist";
    for (size_t i = 0; i < sizeof(expected); ++i) {
        unsigned char c = (unsigned char)path[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)expected[i]) return false;
    }
    return true;
}

static bool list_files(q3_call *call, int32_t *result, qa_error *error)
{
    qa_buffer path = {0}, extension = {0};
    bool ok = q3_string(call, call->arguments[0], &path, error) &&
              q3_string(call, call->arguments[1], &extension, error);
    int32_t capacity = q3_integer(call, 3);
    if (ok && capacity <= 0)
        ok = q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 file listing needs a nonempty destination");
    if (ok && call->vm) {
        qa_bytes admitted;
        ok = q3_vm_span(call->vm, call->arguments[2], (size_t)capacity, &admitted, error);
    }
    qa_vfs_listing listing = {0};
    bool mods = ok && mod_listing((const char *)path.data);
    if (mods) {
        ok = call->host->options.common.installed_mods &&
             call->host->options.common.installed_mods(call->host->options.common.context,
                                                        &listing, error);
        if (!call->host->options.common.installed_mods)
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "$modlist requires the installed product catalog");
    } else if (ok) {
        ok = qa_vfs_list(call->host->options.mounts, (const char *)path.data,
                          (const char *)extension.data, &listing, error);
    }
    if (ok) {
        const uint8_t zero = 0;
        ok = q3_write(call, call->arguments[2], (qa_bytes){&zero, 1}, error);
        size_t used = 0;
        if (mods && listing.count % 2)
            ok = q3_fail(error, QA_ERROR_FORMAT, 0, "invalid Q3 installed-mod listing");
        for (size_t i = 0; ok && i < listing.count; ) {
            size_t count = mods ? 2u : 1u, needed = 0;
            for (size_t j = 0; j < count; ++j) {
                size_t length = strlen(listing.names[i + j]);
                if (length >= SIZE_MAX - needed) { ok = false; break; }
                needed += length + 1u;
            }
            if (!ok) { q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 file list length overflow"); break; }
            if (needed >= (size_t)capacity - used || (size_t)capacity - used - needed <= 1u) break;
            for (size_t j = 0; ok && j < count; ++j) {
                const char *name = listing.names[i++]; size_t length = strlen(name) + 1u;
                ok = q3_write(call, call->arguments[2] + used,
                                (qa_bytes){(const uint8_t *)name, length}, error);
                used += length;
            }
            if (ok) ++*result;
        }
    }
    qa_vfs_listing_free(&listing);
    qa_buffer_free(&path); qa_buffer_free(&extension);
    return ok;
}

q3_service_result q3_files(q3_call *call, int32_t *result, qa_error *error)
{
    qa_qvm_role role = call->host->options.role;
    int32_t open = role == QA_QVM_UI ? 13 : 10;
    int32_t seek = role == QA_QVM_UI ? 86 : role == QA_QVM_GAME ? 45 : 89;
    int32_t list = role == QA_QVM_UI ? 17 : role == QA_QVM_GAME ? 38 : -1;
    int32_t trap = call->service;
    if (!((trap >= open && trap <= open + 3) || trap == seek || trap == list)) return Q3_UNHANDLED;
    if (!call->host->options.mounts) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 mounted content is unbound");
        return Q3_FAILED;
    }
    bool ok;
    if (trap == open) ok = open_file(call, result, error);
    else if (trap == open + 1) ok = read_file(call, result, error);
    else if (trap == open + 2) ok = write_file(call, result, error);
    else if (trap == seek) ok = seek_file(call, result, error);
    else if (trap == list) ok = list_files(call, result, error);
    else {
        int32_t slot = q3_integer(call, 0);
        if (slot < 0 || slot >= 64)
            ok = q3_fail(error, QA_ERROR_ARGUMENT, (size_t)(uint32_t)slot, "FS_FileForHandle: out of range");
        else ok = !slot || q3_file_close_checked(&call->host->files[slot], error);
    }
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
