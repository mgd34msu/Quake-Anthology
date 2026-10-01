#include "internal.h"

void q3_file_close(q3_file *file)
{
    qa_resource_release(file->resource);
    qa_vfs_file_close(file->writable);
    qa_buffer_free(&file->restored_bytes);
    free(file->restored_path);
    *file = (q3_file){0};
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
    if (!destination && mode)
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
    } else if (!host->options.writable_mount) {
        ok = q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 filesystem has no writable owner");
    } else {
        if (!free_slot(host, error)) ok = false;
        if (ok) {
            qa_vfs_write_mode write_mode = mode == 1 ? QA_VFS_WRITE : mode == 2 ? QA_VFS_APPEND : QA_VFS_APPEND_SYNC;
            qa_error local = {0};
            ok = qa_vfs_file_open(host->options.mounts, host->options.writable_mount, path,
                                   write_mode, &file.writable, &local);
            if (!ok && local.code == QA_ERROR_IO) {
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
        size_t written;
        ok = qa_vfs_file_write(file->writable, bytes, &written, error);
        if (ok && call->native && call->native_profile == QA_NATIVE_QUAKE_LIVE_GAME_API10)
            *result = (int32_t)written;
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
        bool ok = qa_vfs_file_seek(file->writable, displacement, translated, &local);
        if (!ok && local.code == QA_ERROR_IO) { *result = -1; return true; }
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
        else { if (slot) q3_file_close(&call->host->files[slot]); ok = true; }
    }
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
