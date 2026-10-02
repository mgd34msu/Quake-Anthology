struct qa_fs_opened_file {
    qa_fs_root *root;
    HANDLE handle;
    char *path;
    qa_fs_object_reference object;
    uint32_t mode, creation;
    bool ready;
    size_t references;
};
bool qa_fs_opened_native_error_read(qa_fs_native_error *out)
{ if (!out) return false; *out = opened_native_failure; return true; }
static bool opened_error(qa_error *error, const char *path, DWORD code)
{
    opened_native_failure = (qa_fs_native_error){2, code, true};
    qa_error_set(error, code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ?
        QA_ERROR_NOT_FOUND : QA_ERROR_IO, 0, "cannot open native file %s (Windows error %lu)",
        path, (unsigned long)code); return false;
}
bool qa_fs_root_opened_file(qa_fs_root *root, const char *path, uint32_t mode,
    qa_fs_opened_creation creation, qa_fs_opened_file **out, bool *opened, qa_error *error)
{
    opened_native_failure = (qa_fs_native_error){0};
    if (opened) *opened = false;
    if (!root || !out || *out || !opened || (mode & ~3u) || creation < 1 || creation > 5 ||
        !qa_fs_relative_valid(path, false, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid contained opened-file request"); return false;
    }
    if ((creation == QA_FS_CREATE_ALWAYS || creation == QA_FS_TRUNCATE_EXISTING) && !(mode & QA_FS_OPENED_WRITE)) {
        qa_error_set(error, QA_ERROR_IO, 0, "Truncating an opened file requires actual write authority"); return false;
    }
    qa_fs_opened_file *file = calloc(1, sizeof(*file));
    if (file) { file->handle = INVALID_HANDLE_VALUE; file->path = copy_string(path); }
    if (!file || !file->path) { if (file) free(file->path); free(file);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Owning contained opened file"); return false; }
    file->references = 1; file->root = root; qa_fs_root_retain(root); file->mode = mode; file->creation = creation;
    DWORD access = FILE_READ_ATTRIBUTES;
    if (mode & QA_FS_OPENED_READ) access |= GENERIC_READ;
    if (mode & QA_FS_OPENED_WRITE) access |= GENERIC_WRITE;
    writable_path locked = {0}; wchar_t *target = NULL;
    bool changing = (mode & QA_FS_OPENED_WRITE) || creation != QA_FS_OPEN_EXISTING;
    if (changing) {
        if (!writable_parent(root, path, false, &locked, error)) {
            qa_fs_native_error failure = opened_native_failure;
            qa_fs_opened_file_close(&file, NULL); opened_native_failure = failure; return false;
        }
        target = wide_child(locked.parent_path, locked.leaf, error);
        qa_fs_entry_kind existing;
        if (!target || !status_wide(target, false, &existing, NULL, error, path)) {
            qa_fs_native_error failure = opened_native_failure;
            free(target); writable_path_close(&locked); qa_fs_opened_file_close(&file, NULL);
            opened_native_failure = failure; return false;
        }
        if (existing != QA_FS_MISSING && existing != QA_FS_REGULAR) {
            qa_error_set(error, QA_ERROR_IO, 0, "Opened file target is not regular: %s", path);
            free(target); writable_path_close(&locked); qa_fs_opened_file_close(&file, NULL); return false;
        }
        DWORD disposition = creation == QA_FS_CREATE_NEW ? CREATE_NEW :
            (creation == QA_FS_CREATE_ALWAYS || creation == QA_FS_OPEN_ALWAYS) ? OPEN_ALWAYS : OPEN_EXISTING;
        file->handle = CreateFileW(target, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, disposition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        DWORD code = GetLastError(); free(target);
        if (file->handle == INVALID_HANDLE_VALUE) {
            writable_path_close(&locked); qa_fs_opened_file_close(&file, NULL);
            return opened_error(error, path, code);
        }
    } else {
        locked.root_path = handle_path(root->handle, error);
        if (locked.root_path) target = join_wide(locked.root_path, path, error);
        if (!target) {
            qa_fs_native_error failure = opened_native_failure;
            writable_path_close(&locked); qa_fs_opened_file_close(&file, NULL);
            opened_native_failure = failure; return false;
        }
        file->handle = CreateFileW(target, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD code = GetLastError(); free(target);
        if (file->handle == INVALID_HANDLE_VALUE) {
            writable_path_close(&locked); qa_fs_opened_file_close(&file, NULL);
            return opened_error(error, path, code);
        }
    }
    *out = file; *opened = true;
    bool okay = true;
    {
        wchar_t *final = handle_path(file->handle, error);
        okay = final && path_within(locked.root_path, final); free(final);
        if (!okay && (!error || error->code == QA_OK))
            qa_error_set(error, QA_ERROR_IO, 0, "Opened native file escapes its retained root");
    }
    BY_HANDLE_FILE_INFORMATION legacy; FILE_BASIC_INFO basic;
    if (okay) okay = handle_info(file->handle, &legacy, &basic, error, path);
    if (okay && kind_from_attributes(legacy.dwFileAttributes) != QA_FS_REGULAR) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opened native file is not a regular object"); okay = false;
    }
    if (okay) file->object = (qa_fs_object_reference){2, {legacy.dwVolumeSerialNumber,
        ((uint64_t)legacy.nFileIndexHigh << 32) | legacy.nFileIndexLow,
        ((uint64_t)legacy.ftCreationTime.dwHighDateTime << 32) | legacy.ftCreationTime.dwLowDateTime}};
    if (okay && (creation == QA_FS_CREATE_ALWAYS || creation == QA_FS_TRUNCATE_EXISTING)) {
        LARGE_INTEGER zero = {.QuadPart = 0};
        if (!SetFilePointerEx(file->handle, zero, NULL, FILE_BEGIN) || !SetEndOfFile(file->handle))
            okay = fail_windows(error, "cannot truncate opened file", path, GetLastError());
    }
    writable_path_close(&locked); file->ready = okay; return okay;
}
bool qa_fs_opened_file_reference_read(const qa_fs_opened_file *file, qa_fs_opened_reference *out)
{
    if (!file || !file->ready || !out) return false;
    qa_fs_opened_reference ref = {.object = file->object, .mode = file->mode,
        .creation = file->creation, .path = file->path};
    if (!qa_fs_root_reference_read(file->root, &ref.root)) return false;
    *out = ref; return true;
}
bool qa_fs_opened_file_current(const qa_fs_opened_file *file, qa_error *error)
{
    opened_native_failure = (qa_fs_native_error){0};
    if (!file || !file->ready || file->handle == INVALID_HANDLE_VALUE) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opened native file has no admitted handle"); return false;
    }
    BY_HANDLE_FILE_INFORMATION legacy; FILE_BASIC_INFO basic;
    if (!handle_info(file->handle, &legacy, &basic, error, file->path)) return false;
    qa_fs_object_reference actual = {2, {legacy.dwVolumeSerialNumber,
        ((uint64_t)legacy.nFileIndexHigh << 32) | legacy.nFileIndexLow,
        ((uint64_t)legacy.ftCreationTime.dwHighDateTime << 32) | legacy.ftCreationTime.dwLowDateTime}};
    if (kind_from_attributes(legacy.dwFileAttributes) != QA_FS_REGULAR ||
        actual.platform != file->object.platform || actual.words[0] != file->object.words[0] ||
        actual.words[1] != file->object.words[1] || actual.words[2] != file->object.words[2]) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opened native file object identity differs"); return false;
    }
    return true;
}
static bool opened_position(qa_fs_opened_file *file, uint64_t offset, qa_error *error)
{
    if (offset > INT64_MAX) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native file offset exceeds signed host domain"); return false; }
    LARGE_INTEGER position = {.QuadPart = (LONGLONG)offset};
    return SetFilePointerEx(file->handle, position, NULL, FILE_BEGIN) != 0 ||
        fail_windows(error, "cannot position opened file", file->path, GetLastError());
}
bool qa_fs_opened_file_read(qa_fs_opened_file *file, uint64_t offset, void *out,
    size_t bytes, size_t *done, qa_error *error)
{
    opened_native_failure = (qa_fs_native_error){0};
    if (done) *done = 0;
    if (!done || (!out && bytes) || !file || !(file->mode & QA_FS_OPENED_READ)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid actual native file read"); return false;
    }
    if (!qa_fs_opened_file_current(file, error) || !opened_position(file, offset, error)) return false;
    DWORD amount = 0; BOOL okay = ReadFile(file->handle, out, bytes > MAXDWORD ? MAXDWORD : (DWORD)bytes, &amount, NULL);
    DWORD code = GetLastError(); *done = amount;
    return okay || code == ERROR_HANDLE_EOF || fail_windows(error, "cannot read opened file", file->path, code);
}
bool qa_fs_opened_file_write(qa_fs_opened_file *file, uint64_t offset, qa_bytes bytes,
    size_t *done, qa_error *error)
{
    opened_native_failure = (qa_fs_native_error){0};
    if (done) *done = 0;
    if (!done || (!bytes.data && bytes.size) || !file || !(file->mode & QA_FS_OPENED_WRITE)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid actual native file write"); return false;
    }
    if (!qa_fs_opened_file_current(file, error) || !opened_position(file, offset, error)) return false;
    DWORD amount = 0; BOOL okay = WriteFile(file->handle, bytes.data, bytes.size > MAXDWORD ? MAXDWORD : (DWORD)bytes.size, &amount, NULL);
    DWORD code = GetLastError(); *done = amount;
    return okay || fail_windows(error, "cannot write opened file", file->path, code);
}
bool qa_fs_opened_file_size(qa_fs_opened_file *file, uint64_t *out, qa_error *error)
{
    opened_native_failure = (qa_fs_native_error){0};
    if (!out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing actual file size output"); return false; }
    if (!qa_fs_opened_file_current(file, error)) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file->handle, &size)) return fail_windows(error, "cannot size opened file", file->path, GetLastError());
    if (size.QuadPart < 0) { qa_error_set(error, QA_ERROR_IO, 0, "Negative native file size"); return false; }
    *out = (uint64_t)size.QuadPart; return true;
}
bool qa_fs_opened_file_posix_descriptor_read(qa_fs_opened_file *file,
    qa_fs_posix_descriptor_status *out, qa_error *error)
{
    (void)file; (void)out;
    opened_native_failure = (qa_fs_native_error){0};
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Windows opened objects do not supply POSIX descriptor status");
    return false;
}
bool qa_fs_opened_file_posix_flags(qa_fs_opened_file *file, bool append, bool nonblocking, qa_error *error)
{
    (void)file; (void)append; (void)nonblocking;
    opened_native_failure = (qa_fs_native_error){0};
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Windows opened objects do not supply POSIX descriptor flags");
    return false;
}
bool qa_fs_opened_file_posix_status_read(qa_fs_opened_file *file, qa_fs_posix_status *out, qa_error *error)
{
    opened_native_failure = (qa_fs_native_error){0};
    (void)file; (void)out;
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Windows opened objects do not supply POSIX file metadata"); return false;
}
bool qa_fs_opened_file_truncate(qa_fs_opened_file *file, uint64_t bytes, qa_error *error)
{
    opened_native_failure = (qa_fs_native_error){0};
    if (!file || !(file->mode & QA_FS_OPENED_WRITE)) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native truncate needs write authority"); return false; }
    if (!qa_fs_opened_file_current(file, error) || !opened_position(file, bytes, error)) return false;
    return SetEndOfFile(file->handle) != 0 || fail_windows(error, "cannot truncate opened file", file->path, GetLastError());
}
bool qa_fs_opened_file_flush(qa_fs_opened_file *file, qa_error *error)
{
    opened_native_failure = (qa_fs_native_error){0};
    if (!qa_fs_opened_file_current(file, error)) return false;
    return FlushFileBuffers(file->handle) != 0 || fail_windows(error, "cannot flush opened file", file->path, GetLastError());
}
bool qa_fs_opened_file_close(qa_fs_opened_file **pointer, qa_error *error)
{
    opened_native_failure = (qa_fs_native_error){0};
    if (!pointer) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Opened-file close needs its owned pointer"); return false; }
    qa_fs_opened_file *file = *pointer; if (!file) return true;
    if (file->references > 1) { --file->references; *pointer = NULL; return true; }
    if (file->handle != INVALID_HANDLE_VALUE && !CloseHandle(file->handle))
        return fail_windows(error, "cannot close opened file", file->path, GetLastError());
    qa_fs_root_close(file->root); free(file->path); free(file); *pointer = NULL; return true;
}
void qa_fs_opened_file_retain(qa_fs_opened_file *file)
{ if (file) ++file->references; }
