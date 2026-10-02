struct qa_fs_opened_file {
    qa_fs_root *root;
    int descriptor;
    char *path;
    qa_fs_object_reference object;
    uint32_t mode, creation;
    bool ready;
    size_t references;
};
static bool opened_error(qa_error *error, const char *path, int code)
{
    qa_error_set(error, code == ENOENT ? QA_ERROR_NOT_FOUND : QA_ERROR_IO,
        0, "cannot open native file %s: %s", path, strerror(code)); return false;
}
bool qa_fs_root_opened_file(qa_fs_root *root, const char *path, uint32_t mode,
    qa_fs_opened_creation creation, qa_fs_opened_file **out, bool *opened, qa_error *error)
{
    if (opened) *opened = false;
    if (!root || !out || *out || !opened || (mode & ~3u) || creation < 1 || creation > 5 ||
        !qa_fs_relative_valid(path, false, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid contained opened-file request"); return false;
    }
    if ((creation == QA_FS_CREATE_ALWAYS || creation == QA_FS_TRUNCATE_EXISTING) && !(mode & QA_FS_OPENED_WRITE)) {
        qa_error_set(error, QA_ERROR_IO, 0, "Truncating an opened file requires actual write authority"); return false;
    }
    qa_fs_opened_file *file = calloc(1, sizeof(*file));
    if (file) { file->descriptor = -1; file->path = copy_string(path); }
    if (!file || !file->path) { if (file) free(file->path); free(file);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Owning contained opened file"); return false; }
    file->references = 1; file->root = root; qa_fs_root_retain(root); file->mode = mode; file->creation = creation;
    bool changing = (mode & QA_FS_OPENED_WRITE) || creation != QA_FS_OPEN_EXISTING;
    int flags = mode == 3 ? O_RDWR : (mode & QA_FS_OPENED_WRITE) ? O_WRONLY : O_RDONLY;
#if defined(O_PATH)
    if (!mode && !changing) flags = O_PATH;
#endif
    if (changing) {
        char *leaf = NULL;
        int parent = writable_parent(root, path, false, &leaf, error);
        if (parent < 0) { qa_fs_opened_file_close(&file, NULL); return false; }
        flags |= O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
        if (creation == QA_FS_CREATE_NEW) flags |= O_CREAT | O_EXCL;
        else if (creation == QA_FS_CREATE_ALWAYS || creation == QA_FS_OPEN_ALWAYS) flags |= O_CREAT;
        do { file->descriptor = openat(parent, leaf, flags, 0600); } while (file->descriptor < 0 && errno == EINTR);
        int code = errno; (void)close(parent); free(leaf); errno = code;
    } else {
#if defined(__linux__) && defined(SYS_openat2)
        struct open_how how = {.flags = (uint64_t)(unsigned int)(flags | O_CLOEXEC | O_NONBLOCK),
            .resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS};
        do { file->descriptor = (int)syscall(SYS_openat2, root->descriptor, path, &how, sizeof(how)); }
        while (file->descriptor < 0 && errno == EINTR);
        if (file->descriptor < 0 && errno != EXDEV && errno != ENOSYS && errno != EINVAL) {
            int code = errno; qa_fs_opened_file_close(&file, NULL);
            return opened_error(error, path, code);
        }
#endif
        if (file->descriptor < 0) {
            do { file->descriptor = openat(root->descriptor, path, flags | O_CLOEXEC | O_NONBLOCK); }
            while (file->descriptor < 0 && errno == EINTR);
        }
    }
    if (file->descriptor < 0) { int code = errno; qa_fs_opened_file_close(&file, NULL);
        return opened_error(error, path, code); }
    *out = file; *opened = true;
    struct stat info;
    if (!descriptor_stat(file->descriptor, &info)) return fail_errno(error, "cannot inspect opened file", path, errno);
    if (!S_ISREG(info.st_mode) || info.st_size < 0 || !descriptor_contained(root, file->descriptor, path)) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opened native file is not a contained regular object"); return false; }
    file->object = (qa_fs_object_reference){1, {(uint64_t)info.st_dev, (uint64_t)info.st_ino, 0}};
    if ((creation == QA_FS_CREATE_ALWAYS || creation == QA_FS_TRUNCATE_EXISTING) && ftruncate(file->descriptor, 0))
        return fail_errno(error, "cannot truncate opened file", path, errno);
    file->ready = true; return true;
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
    struct stat info;
    if (!file || !file->ready || !descriptor_stat(file->descriptor, &info)) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opened native file no longer has its admitted handle"); return false; }
    if (!S_ISREG(info.st_mode) || (uint64_t)info.st_dev != file->object.words[0] ||
        (uint64_t)info.st_ino != file->object.words[1]) {
        qa_error_set(error, QA_ERROR_IO, 0, "Opened native file object identity differs"); return false; }
    return true;
}
bool qa_fs_opened_file_read(qa_fs_opened_file *file, uint64_t offset, void *out,
    size_t bytes, size_t *done, qa_error *error)
{
    if (done) *done = 0;
    if (!done || (!out && bytes) || !file || !(file->mode & QA_FS_OPENED_READ) || offset > INT64_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid actual native file read"); return false;
    }
    if (!qa_fs_opened_file_current(file, error)) return false;
    size_t amount = bytes > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes;
    ssize_t result; do { result = pread(file->descriptor, out, amount, (off_t)offset); } while (result < 0 && errno == EINTR);
    if (result < 0) return fail_errno(error, "cannot read opened file", file->path, errno);
    *done = (size_t)result; return true;
}
bool qa_fs_opened_file_write(qa_fs_opened_file *file, uint64_t offset, qa_bytes bytes,
    size_t *done, qa_error *error)
{
    if (done) *done = 0;
    if (!done || (!bytes.data && bytes.size) || !file || !(file->mode & QA_FS_OPENED_WRITE) || offset > INT64_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid actual native file write"); return false;
    }
    if (!qa_fs_opened_file_current(file, error)) return false;
    size_t amount = bytes.size > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes.size;
    ssize_t result; do { result = pwrite(file->descriptor, bytes.data, amount, (off_t)offset); } while (result < 0 && errno == EINTR);
    if (result < 0) return fail_errno(error, "cannot write opened file", file->path, errno);
    *done = (size_t)result; return true;
}
bool qa_fs_opened_file_size(qa_fs_opened_file *file, uint64_t *out, qa_error *error)
{
    if (!out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing actual file size output"); return false; }
    if (!qa_fs_opened_file_current(file, error)) return false;
    struct stat info;
    if (!descriptor_stat(file->descriptor, &info) || info.st_size < 0)
        return fail_errno(error, "cannot size opened file", file->path, errno);
    *out = (uint64_t)info.st_size; return true;
}
bool qa_fs_opened_file_truncate(qa_fs_opened_file *file, uint64_t bytes, qa_error *error)
{
    if (!file || !(file->mode & QA_FS_OPENED_WRITE) || bytes > INT64_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native truncate needs write authority and a signed host extent"); return false;
    }
    if (!qa_fs_opened_file_current(file, error)) return false;
    return !ftruncate(file->descriptor, (off_t)bytes) || fail_errno(error, "cannot truncate opened file", file->path, errno);
}
bool qa_fs_opened_file_flush(qa_fs_opened_file *file, qa_error *error)
{
    if (!qa_fs_opened_file_current(file, error)) return false;
    return !fsync(file->descriptor) || fail_errno(error, "cannot flush opened file", file->path, errno);
}
bool qa_fs_opened_file_close(qa_fs_opened_file **pointer, qa_error *error)
{
    if (!pointer) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Opened-file close needs its owned pointer"); return false; }
    qa_fs_opened_file *file = *pointer; if (!file) return true;
    if (file->references > 1) { --file->references; *pointer = NULL; return true; }
    bool okay = file->descriptor < 0 || !close(file->descriptor);
    if (!okay) fail_errno(error, "cannot close opened file", file->path, errno);
    qa_fs_root_close(file->root); free(file->path); free(file); *pointer = NULL; return okay;
}
void qa_fs_opened_file_retain(qa_fs_opened_file *file)
{ if (file) ++file->references; }
