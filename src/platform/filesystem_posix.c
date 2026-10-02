#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "filesystem_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif

struct qa_fs_root {
    size_t references;
    int descriptor;
    dev_t device;
    ino_t inode;
    char *display_path;
    char *final_path;
};

struct qa_fs_file {
    size_t references;
    int descriptor;
    char *path;
};

struct qa_fs_stream {
    int descriptor;
    qa_fs_stream_mode mode;
    char *path;
    qa_fs_root *root;
    qa_fs_object_reference object;
};

static _Thread_local qa_fs_native_error opened_native_failure;
static bool fail_errno(qa_error *error, const char *operation,
                       const char *path, int code)
{
    opened_native_failure = (qa_fs_native_error){1, (uint32_t)code, true};
    qa_error_set(error, code == ENOENT || code == ENOTDIR
                           ? QA_ERROR_NOT_FOUND
                           : QA_ERROR_IO,
                 0, "%s %s: %s", operation, path, strerror(code));
    return false;
}

static bool descriptor_stat(int descriptor, struct stat *out)
{
    int result;
    do {
        result = fstat(descriptor, out);
    } while (result < 0 && errno == EINTR);
    return result == 0;
}

static void identity_from_stat(const struct stat *info, qa_fs_identity *out)
{
#if defined(__APPLE__)
    const struct timespec modified = info->st_mtimespec;
    const struct timespec changed = info->st_ctimespec;
#else
    const struct timespec modified = info->st_mtim;
    const struct timespec changed = info->st_ctim;
#endif
    *out = (qa_fs_identity){{
        (uint64_t)info->st_dev,
        (uint64_t)info->st_ino,
        (uint64_t)info->st_size,
        (uint64_t)modified.tv_sec,
        (uint64_t)modified.tv_nsec,
        (uint64_t)changed.tv_sec,
        (uint64_t)changed.tv_nsec
    }};
}

static qa_fs_entry_kind kind_from_mode(mode_t mode)
{
    if (S_ISREG(mode))
        return QA_FS_REGULAR;
    if (S_ISDIR(mode))
        return QA_FS_DIRECTORY;
    if (S_ISLNK(mode))
        return QA_FS_LINK;
    return QA_FS_OTHER;
}

bool qa_fs_identity_modified_time(const qa_fs_identity *identity, qa_fs_timestamp *out)
{
    if (!identity || !out || identity->words[4] >= UINT64_C(1000000000)) return false;
    uint64_t seconds = identity->words[3];
    out->seconds = seconds <= INT64_MAX ? (int64_t)seconds :
        -1 - (int64_t)(UINT64_MAX - seconds);
    out->nanoseconds = (uint32_t)identity->words[4];
    return true;
}

static char *copy_string(const char *source)
{
    size_t length = strlen(source);
    char *copy = length == SIZE_MAX ? NULL : malloc(length + 1);
    if (copy != NULL)
        memcpy(copy, source, length + 1);
    return copy;
}

static char *absolute_path(const char *path, qa_error *error)
{
    if (path[0] == '/') {
        char *copy = copy_string(path);
        if (copy == NULL)
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "cannot retain filesystem path");
        return copy;
    }
    char *working = getcwd(NULL, 0);
    if (working == NULL) {
        fail_errno(error, "cannot resolve working directory for", path, errno);
        return NULL;
    }
    size_t root = strlen(working), tail = strlen(path);
    if (root > SIZE_MAX - 2 || tail > SIZE_MAX - root - 2) {
        free(working);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "filesystem path is too long");
        return NULL;
    }
    char *result = malloc(root + tail + 2);
    if (result == NULL) {
        free(working);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot retain filesystem path");
        return NULL;
    }
    memcpy(result, working, root);
    result[root] = '/';
    memcpy(result + root + 1, path, tail + 1);
    free(working);
    return result;
}

static bool path_within(const char *root, const char *path)
{
    size_t length = strlen(root);
    if (length == 1 && root[0] == '/')
        return path[0] == '/';
    return strncmp(root, path, length) == 0
        && (path[length] == '\0' || path[length] == '/');
}

static char *descriptor_path(int descriptor)
{
#if defined(__linux__)
    char source[64];
    (void)snprintf(source, sizeof(source), "/proc/self/fd/%d", descriptor);
    size_t capacity = 256;
    for (;;) {
        char *path = malloc(capacity);
        if (path == NULL)
            return NULL;
        ssize_t length = readlink(source, path, capacity - 1);
        if (length < 0) {
            free(path);
            return NULL;
        }
        if ((size_t)length < capacity - 1) {
            path[length] = '\0';
            char *deleted = strstr(path, " (deleted)");
            if (deleted != NULL && deleted[10] == '\0')
                *deleted = '\0';
            char *canonical = realpath(path, NULL);
            free(path);
            return canonical;
        }
        free(path);
        if (capacity > SIZE_MAX / 2)
            return NULL;
        capacity *= 2;
    }
#elif defined(__APPLE__)
    char path[PATH_MAX];
    if (fcntl(descriptor, F_GETPATH, path) < 0)
        return NULL;
    return realpath(path, NULL);
#else
    (void)descriptor;
    return NULL;
#endif
}

static bool descriptor_contained(qa_fs_root *root, int descriptor,
                                 const char *relative)
{
    char *current_root = descriptor_path(root->descriptor);
    const char *root_path = current_root == NULL
        ? root->final_path : current_root;
    char *final = descriptor_path(descriptor);
    if (final != NULL) {
        bool contained = path_within(root_path, final);
        free(final);
        free(current_root);
        return contained;
    }
    const char *path = relative[0] == '\0' ? "." : relative;
    size_t base = strlen(root_path), tail = strlen(path);
    if (base > SIZE_MAX - 2 || tail > SIZE_MAX - base - 2) {
        free(current_root);
        return false;
    }
    char *candidate = malloc(base + tail + 2);
    if (candidate == NULL) {
        free(current_root);
        return false;
    }
    (void)snprintf(candidate, base + tail + 2, "%s/%s",
                   root_path, path);
    char *canonical = realpath(candidate, NULL);
    free(candidate);
    if (canonical == NULL || !path_within(root_path, canonical)) {
        free(canonical);
        free(current_root);
        return false;
    }
    struct stat opened, named;
    bool matches = descriptor_stat(descriptor, &opened)
        && stat(canonical, &named) == 0
        && opened.st_dev == named.st_dev
        && opened.st_ino == named.st_ino;
    free(canonical);
    free(current_root);
    return matches;
}

static int open_contained(qa_fs_root *root, const char *relative, int flags)
{
    const char *path = relative[0] == '\0' ? "." : relative;
    int descriptor;
#if defined(__linux__) && defined(SYS_openat2)
    struct open_how how = {
        .flags = (uint64_t)(unsigned int)(flags | O_CLOEXEC | O_NONBLOCK),
        .resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS
    };
    do {
        descriptor = (int)syscall(SYS_openat2, root->descriptor, path,
                                  &how, sizeof(how));
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor >= 0) {
        if (descriptor_contained(root, descriptor, path))
            return descriptor;
        (void)close(descriptor);
        errno = EXDEV;
        return -1;
    }
    if (errno != EXDEV && errno != ENOSYS && errno != EINVAL)
        return -1;
#endif
    do {
        descriptor = openat(root->descriptor, path,
                            flags | O_CLOEXEC | O_NONBLOCK);
    } while (descriptor < 0 && errno == EINTR);
    int code = errno;
    if (descriptor >= 0 && !descriptor_contained(root, descriptor, path)) {
        (void)close(descriptor);
        descriptor = -1;
        code = EXDEV;
    }
    errno = code;
    return descriptor;
}

bool qa_fs_root_open(const char *path, qa_fs_root **out, qa_error *error)
{
    if (out != NULL)
        *out = NULL;
    if (path == NULL || path[0] == '\0' || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "filesystem root needs a path and output");
        return false;
    }
    int descriptor;
    do {
        descriptor = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0)
        return fail_errno(error, "cannot open directory", path, errno);
    struct stat opened;
    if (!descriptor_stat(descriptor, &opened)) {
        int code = errno;
        (void)close(descriptor);
        return fail_errno(error, "cannot open directory", path, code);
    }
    if (!S_ISDIR(opened.st_mode)) {
        (void)close(descriptor);
        return fail_errno(error, "cannot open directory", path, ENOTDIR);
    }
    char *final = descriptor_path(descriptor);
    if (final == NULL)
        final = realpath(path, NULL);
    char *display = final == NULL ? NULL : copy_string(final);
    if (final == NULL || display == NULL) {
        bool memory_failure = final != NULL;
        int code = errno;
        free(final);
        free(display);
        (void)close(descriptor);
        if (error == NULL || error->code == QA_OK) {
            if (memory_failure)
                qa_error_set(error, QA_ERROR_MEMORY, 0,
                             "cannot retain final directory path");
            else
                fail_errno(error, "cannot resolve directory", path, code);
        }
        return false;
    }
    struct stat resolved;
    int status = stat(final, &resolved);
    if (status < 0 || resolved.st_dev != opened.st_dev
        || resolved.st_ino != opened.st_ino) {
        int code = status < 0 ? errno : EIO;
        free(final);
        free(display);
        (void)close(descriptor);
        return fail_errno(error, "directory changed while opening", path, code);
    }
    qa_fs_root *root = calloc(1, sizeof(*root));
    if (root == NULL) {
        free(final);
        free(display);
        (void)close(descriptor);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate filesystem root");
        return false;
    }
    root->references = 1;
    root->descriptor = descriptor;
    root->device = opened.st_dev;
    root->inode = opened.st_ino;
    root->display_path = display;
    root->final_path = final;
    *out = root;
    return true;
}

void qa_fs_root_retain(qa_fs_root *root)
{
    if (root != NULL)
        ++root->references;
}

bool qa_fs_root_same_object(const qa_fs_root *left, const qa_fs_root *right)
{
    return left != NULL && right != NULL
        && left->device == right->device && left->inode == right->inode;
}

bool qa_fs_root_reference_read(const qa_fs_root *root, qa_fs_object_reference *out)
{
    if (!root || !out) return false;
    *out = (qa_fs_object_reference){1, {(uint64_t)root->device, (uint64_t)root->inode, 0}};
    return true;
}

void qa_fs_root_close(qa_fs_root *root)
{
    if (root == NULL || --root->references != 0)
        return;
    (void)close(root->descriptor);
    free(root->display_path);
    free(root->final_path);
    free(root);
}

bool qa_fs_root_join(const qa_fs_root *root, const char *relative,
                     char **out, qa_error *error)
{
    if (out != NULL)
        *out = NULL;
    if (root == NULL || out == NULL
        || !qa_fs_relative_valid(relative, true, error))
        return false;
    char *current = descriptor_path(root->descriptor);
    const char *base_path = current == NULL
        ? root->display_path : current;
    if (relative[0] == '\0' || strcmp(relative, ".") == 0) {
        *out = copy_string(base_path);
    } else {
        size_t base = strlen(base_path), tail = strlen(relative);
        bool separator = base == 0 || base_path[base - 1] != '/';
        size_t extra = separator ? 2 : 1;
        if (base > SIZE_MAX - extra
            || tail > SIZE_MAX - base - extra) {
            free(current);
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "filesystem path is too long");
            return false;
        }
        *out = malloc(base + tail + extra);
        if (*out != NULL) {
            memcpy(*out, base_path, base);
            if (separator)
                (*out)[base++] = '/';
            memcpy(*out + base, relative, tail + 1);
        }
    }
    free(current);
    if (*out == NULL)
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate filesystem path");
    return *out != NULL;
}

static bool list_descriptor(int descriptor, const char *path,
                            qa_fs_listing *out, qa_error *error)
{
    DIR *directory = fdopendir(descriptor);
    if (directory == NULL) {
        int code = errno;
        (void)close(descriptor);
        return fail_errno(error, "cannot list directory", path, code);
    }
    bool ok = true;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0)
                ok = fail_errno(error, "cannot list directory", path, errno);
            break;
        }
        if (strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0)
            continue;
        struct stat info;
        if (fstatat(dirfd(directory), entry->d_name, &info,
                    AT_SYMLINK_NOFOLLOW) < 0) {
            if (errno == ENOENT)
                continue;
            ok = fail_errno(error, "cannot inspect directory entry",
                            entry->d_name, errno);
            break;
        }
        if (!qa_fs_listing_add(out, entry->d_name,
                               kind_from_mode(info.st_mode), error)) {
            ok = false;
            break;
        }
    }
    if (closedir(directory) < 0 && ok)
        ok = fail_errno(error, "cannot close directory", path, errno);
    if (!ok)
        qa_fs_listing_free(out);
    return ok;
}

bool qa_fs_path_list(const char *path, qa_fs_listing *out, qa_error *error)
{
    if (out != NULL)
        *out = (qa_fs_listing){0};
    if (path == NULL || path[0] == '\0' || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "directory listing needs a path and output");
        return false;
    }
    int descriptor;
    do {
        descriptor = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0)
        return fail_errno(error, "cannot open directory", path, errno);
    return list_descriptor(descriptor, path, out, error);
}

bool qa_fs_root_list(qa_fs_root *root, const char *relative,
                     qa_fs_listing *out, qa_error *error)
{
    if (out != NULL)
        *out = (qa_fs_listing){0};
    if (root == NULL || out == NULL
        || !qa_fs_relative_valid(relative, true, error))
        return false;
    int descriptor = open_contained(root, relative, O_RDONLY | O_DIRECTORY);
    if (descriptor < 0)
        return fail_errno(error, "cannot open contained directory",
                          relative[0] == '\0' ? "." : relative, errno);
    return list_descriptor(descriptor,
                           relative[0] == '\0' ? "." : relative,
                           out, error);
}

bool qa_fs_root_status(qa_fs_root *root, const char *relative,
                       qa_fs_entry_kind *kind, qa_fs_identity *identity,
                       qa_error *error)
{
    if (kind != NULL)
        *kind = QA_FS_MISSING;
    if (root == NULL || kind == NULL
        || !qa_fs_relative_valid(relative, true, error)) {
        if (root == NULL || kind == NULL)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "contained status needs a root and kind output");
        return false;
    }
    int descriptor = open_contained(root, relative, O_RDONLY);
    if (descriptor < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return true;
        return fail_errno(error, "cannot inspect contained path",
                          relative[0] == '\0' ? "." : relative, errno);
    }
    struct stat info;
    if (!descriptor_stat(descriptor, &info)) {
        int code = errno;
        (void)close(descriptor);
        return fail_errno(error, "cannot inspect contained path",
                          relative[0] == '\0' ? "." : relative, code);
    }
    (void)close(descriptor);
    *kind = kind_from_mode(info.st_mode);
    if (identity != NULL)
        identity_from_stat(&info, identity);
    return true;
}

bool qa_fs_path_status(const char *path, bool follow_links,
                       qa_fs_entry_kind *kind, qa_fs_identity *identity,
                       qa_error *error)
{
    if (kind != NULL)
        *kind = QA_FS_MISSING;
    if (path == NULL || path[0] == '\0' || kind == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "filesystem status needs a path and kind output");
        return false;
    }
    struct stat info;
    int result;
    do {
        result = follow_links ? stat(path, &info) : lstat(path, &info);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return true;
        return fail_errno(error, "cannot inspect", path, errno);
    }
    *kind = kind_from_mode(info.st_mode);
    if (identity != NULL)
        identity_from_stat(&info, identity);
    return true;
}

static bool file_from_descriptor(int descriptor, const char *path,
                                 qa_fs_file **out, qa_fs_identity *identity,
                                 qa_status nonregular, qa_error *error)
{
    struct stat info;
    if (!descriptor_stat(descriptor, &info)) {
        int code = errno;
        (void)close(descriptor);
        return fail_errno(error, "cannot inspect file", path, code);
    }
    if (!S_ISREG(info.st_mode)) {
        (void)close(descriptor);
        qa_error_set(error, nonregular, 0,
                     "filesystem object is not a regular file: %s", path);
        return false;
    }
    char *absolute = absolute_path(path, error);
    qa_fs_file *file = absolute == NULL ? NULL : calloc(1, sizeof(*file));
    if (file == NULL) {
        free(absolute);
        (void)close(descriptor);
        if (error == NULL || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "cannot allocate filesystem file");
        return false;
    }
    file->references = 1;
    file->descriptor = descriptor;
    file->path = absolute;
    if (identity != NULL)
        identity_from_stat(&info, identity);
    *out = file;
    return true;
}

bool qa_fs_file_open(const char *path, qa_fs_file **out,
                     qa_fs_identity *identity, qa_error *error)
{
    if (out != NULL)
        *out = NULL;
    if (path == NULL || path[0] == '\0' || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "file open needs a path and output");
        return false;
    }
    int descriptor;
    do {
        descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0)
        return fail_errno(error, "cannot open file", path, errno);
    return file_from_descriptor(descriptor, path, out, identity,
                                QA_ERROR_IO, error);
}

bool qa_fs_root_file_open(qa_fs_root *root, const char *relative,
                          qa_fs_file **out, qa_fs_identity *identity,
                          qa_error *error)
{
    if (out != NULL)
        *out = NULL;
    if (root == NULL || out == NULL
        || !qa_fs_relative_valid(relative, false, error))
        return false;
    int descriptor = open_contained(root, relative, O_RDONLY);
    if (descriptor < 0)
        return fail_errno(error, "cannot open contained file", relative, errno);
    char *path = NULL;
    if (!qa_fs_root_join(root, relative, &path, error)) {
        (void)close(descriptor);
        return false;
    }
    bool ok = file_from_descriptor(descriptor, path, out, identity,
                                   QA_ERROR_NOT_FOUND, error);
    free(path);
    return ok;
}

void qa_fs_file_retain(qa_fs_file *file)
{
    if (file != NULL)
        ++file->references;
}

void qa_fs_file_close(qa_fs_file *file)
{
    if (file == NULL || --file->references != 0)
        return;
    (void)close(file->descriptor);
    free(file->path);
    free(file);
}

bool qa_fs_file_identity(qa_fs_file *file, qa_fs_identity *out,
                         qa_error *error)
{
    if (file == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "file identity needs a file and output");
        return false;
    }
    struct stat info;
    if (!descriptor_stat(file->descriptor, &info))
        return fail_errno(error, "cannot inspect file", file->path, errno);
    identity_from_stat(&info, out);
    return true;
}

bool qa_fs_file_path_unchanged(qa_fs_file *file,
                               const qa_fs_identity *expected,
                               bool *unchanged, qa_error *error)
{
    if (unchanged != NULL)
        *unchanged = false;
    if (file == NULL || expected == NULL || unchanged == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "file validation needs identity and output");
        return false;
    }
    qa_fs_identity opened, current;
    if (!qa_fs_file_identity(file, &opened, error))
        return false;
    qa_fs_entry_kind kind;
    if (!qa_fs_path_status(file->path, true, &kind, &current, error))
        return false;
    *unchanged = kind == QA_FS_REGULAR
        && qa_fs_identity_equal(expected, &opened)
        && qa_fs_identity_equal(expected, &current);
    return true;
}

bool qa_fs_file_read_prefix(qa_fs_file *file, const qa_fs_identity *expected,
    void *bytes, size_t capacity, size_t *received, qa_error *error)
{
    if (received) *received = 0;
    if (!file || !expected || !received || (capacity && !bytes) || capacity > PTRDIFF_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prefix read needs a retained file, identity and bounded output");
        return false;
    }
    qa_fs_identity before;
    if (!qa_fs_file_identity(file, &before, error)) return false;
    if (!qa_fs_identity_equal(expected, &before)) {
        qa_error_set(error, QA_ERROR_IO, 0, "File changed before prefix read"); return false;
    }
    size_t limit = before.words[2] < (uint64_t)capacity ? (size_t)before.words[2] : capacity;
    size_t offset = 0;
    while (offset < limit) {
        size_t amount = limit - offset;
        if (amount > (size_t)SSIZE_MAX) amount = (size_t)SSIZE_MAX;
        ssize_t count = pread(file->descriptor, (uint8_t *)bytes + offset, amount, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return fail_errno(error, "cannot read file prefix", file->path, count ? errno : EIO);
        offset += (size_t)count;
    }
    qa_fs_identity after;
    if (!qa_fs_file_identity(file, &after, error)) return false;
    if (!qa_fs_identity_equal(expected, &after)) {
        qa_error_set(error, QA_ERROR_IO, 0, "File changed during prefix read"); return false;
    }
    *received = offset; return true;
}

bool qa_fs_file_read_snapshot(qa_fs_file *file,
                              const qa_fs_identity *expected,
                              qa_buffer *out, qa_error *error)
{
    if (out != NULL)
        *out = (qa_buffer){0};
    if (file == NULL || expected == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "snapshot read needs file, identity, and output");
        return false;
    }
    qa_fs_identity before;
    if (!qa_fs_file_identity(file, &before, error))
        return false;
    if (!qa_fs_identity_equal(expected, &before)) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "resource changed before snapshot read");
        return false;
    }
    if (before.words[2] > (uint64_t)PTRDIFF_MAX
        || before.words[2] > (uint64_t)SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "file is too large for a memory snapshot");
        return false;
    }
    qa_buffer result = {.size = (size_t)before.words[2]};
    if (result.size != 0) {
        result.data = malloc(result.size);
        if (result.data == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "cannot allocate file snapshot");
            return false;
        }
    }
    size_t offset = 0;
    while (offset < result.size) {
        size_t amount = result.size - offset;
        if (amount > (size_t)SSIZE_MAX)
            amount = (size_t)SSIZE_MAX;
        ssize_t received = pread(file->descriptor, result.data + offset,
                                 amount, (off_t)offset);
        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0) {
            int code = received == 0 ? EIO : errno;
            qa_buffer_free(&result);
            return fail_errno(error, "cannot read file", file->path, code);
        }
        offset += (size_t)received;
    }
    qa_fs_identity after;
    if (!qa_fs_file_identity(file, &after, error)) {
        qa_buffer_free(&result);
        return false;
    }
    if (!qa_fs_identity_equal(expected, &after)) {
        qa_buffer_free(&result);
        qa_error_set(error, QA_ERROR_IO, 0,
                     "resource changed during snapshot read");
        return false;
    }
    bool unchanged;
    if (!qa_fs_file_path_unchanged(file, expected, &unchanged, error)) {
        qa_buffer_free(&result);
        return false;
    }
    if (!unchanged) {
        qa_buffer_free(&result);
        qa_error_set(error, QA_ERROR_IO, 0,
                     "resource path changed during snapshot read");
        return false;
    }
    *out = result;
    return true;
}

static int writable_parent(qa_fs_root *root, const char *relative,
                           bool create, char **leaf, qa_error *error)
{
    char *path = copy_string(relative);
    if (path == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate writable path");
        return -1;
    }
    int descriptor = fcntl(root->descriptor, F_DUPFD_CLOEXEC, 0);
    if (descriptor < 0) {
        fail_errno(error, "cannot retain writable root for", relative, errno);
        free(path);
        return -1;
    }
    char *component = path;
    char *separator;
    while ((separator = strchr(component, '/')) != NULL) {
        *separator = '\0';
        if (create && mkdirat(descriptor, component, 0700) < 0
            && errno != EEXIST) {
            fail_errno(error, "cannot create writable directory",
                       component, errno);
            (void)close(descriptor);
            free(path);
            return -1;
        }
        int next;
        do {
            next = openat(descriptor, component,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        } while (next < 0 && errno == EINTR);
        if (next < 0) {
            fail_errno(error, "cannot open writable directory",
                       component, errno);
            (void)close(descriptor);
            free(path);
            return -1;
        }
        (void)close(descriptor);
        descriptor = next;
        component = separator + 1;
    }
    *leaf = copy_string(component);
    free(path);
    if (*leaf == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot retain writable leaf name");
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static bool create_native_directory_tail(qa_fs_root *root, char *path, qa_error *error)
{
    int descriptor = fcntl(root->descriptor, F_DUPFD_CLOEXEC, 0);
    if (descriptor < 0) return fail_errno(error, "cannot retain startup directory", path, errno);
    bool ok = true;
    for (char *cursor = path; *cursor;) {
        while (*cursor == '/') ++cursor;
        char *component = cursor;
        while (*cursor && *cursor != '/') ++cursor;
        size_t length = (size_t)(cursor - component);
        if (!length) continue;
        char separator = *cursor; *cursor = 0;
        bool dot = !strcmp(component, ".") || !strcmp(component, "..");
        bool created = !dot && mkdirat(descriptor, component, 0700) == 0;
        if (!dot && !created && errno != EEXIST) {
            ok = fail_errno(error, "cannot create startup directory", component, errno);
            *cursor = separator; break;
        }
        int next;
        do { next = openat(descriptor, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); }
        while (next < 0 && errno == EINTR);
        if (next < 0) {
            ok = fail_errno(error, "cannot open startup directory", component, errno);
            *cursor = separator; break;
        }
        if (created && fsync(descriptor) < 0) {
            ok = fail_errno(error, "cannot commit startup directory", component, errno);
            (void)close(next); *cursor = separator; break;
        }
        (void)close(descriptor); descriptor = next;
        *cursor = separator;
    }
    (void)close(descriptor); return ok;
}

bool qa_fs_path_create_directory(const char *path, qa_error *error)
{
    if (!path || !*path) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "directory creation needs a native path");
        return false;
    }
    char *absolute = absolute_path(path, error);
    char *probe = absolute ? copy_string(absolute) : NULL;
    if (!probe) {
        free(absolute);
        if (!error || error->code == QA_OK) qa_error_set(error, QA_ERROR_MEMORY, 0,
                                                       "retaining startup directory path");
        return false;
    }
    size_t length = strlen(probe);
    while (length > 1 && probe[length - 1] == '/') probe[--length] = 0;
    qa_fs_root *root = NULL; bool ok = true;
    while (true) {
        qa_error local = {0};
        if (qa_fs_root_open(probe, &root, &local)) break;
        if (local.code != QA_ERROR_NOT_FOUND || length <= 1) {
            if (error) *error = local;
            ok = false; break;
        }
        while (length > 1 && probe[length - 1] != '/') --length;
        if (length > 1) --length;
        probe[length] = 0;
    }
    if (ok) {
        char *tail = absolute + length;
        while (*tail == '/') ++tail;
        /* Preserve the authored native traversal: missing/../prefs needs its
         * missing directory to exist before the retained spelling can reopen.
         * This startup native path is distinct from contained product paths. */
        ok = !*tail || create_native_directory_tail(root, tail, error);
    }
    qa_fs_root_close(root); free(probe); free(absolute);
    return ok;
}

bool qa_fs_root_create_directory(qa_fs_root *root, const char *relative,
                                  qa_error *error)
{
    if (root == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "directory creation requires a retained filesystem root");
        return false;
    }
    if (!qa_fs_relative_valid(relative, false, error)) return false;
    char *leaf = NULL;
    int parent = writable_parent(root, relative, true, &leaf, error);
    if (parent < 0) return false;
    bool created = mkdirat(parent, leaf, 0700) == 0;
    int code = errno;
    if (!created && code != EEXIST) {
        (void)close(parent); free(leaf);
        return fail_errno(error, "cannot create writable directory", relative, code);
    }
    int directory;
    do {
        directory = openat(parent, leaf,
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    } while (directory < 0 && errno == EINTR);
    code = errno;
    bool ok = directory >= 0;
    if (!ok) fail_errno(error, "cannot open created writable directory", relative, code);
    else if (!descriptor_contained(root, directory, relative)) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "created writable directory escapes its root: %s", relative);
        ok = false;
    }
    if (ok && created && fsync(parent) < 0)
        ok = fail_errno(error, "cannot sync created writable directory", relative, errno);
    if (directory >= 0) (void)close(directory);
    (void)close(parent); free(leaf);
    return ok;
}

bool qa_fs_root_replace(qa_fs_root *root, const char *relative,
                        qa_bytes bytes, uint64_t nonce, qa_error *error)
{
    bool created;
    return qa_fs_root_publish(root, relative, bytes, nonce, false, false, &created, error);
}
bool qa_fs_root_publish(qa_fs_root *root, const char *relative,
                        qa_bytes bytes, uint64_t nonce, bool exclusive,
                        bool private_file, bool *created, qa_error *error)
{
    (void)private_file; /* Every replacement is already created mode 0600. */
    if (root == NULL || created == NULL || (bytes.data == NULL && bytes.size != 0)
        || bytes.size > (size_t)PTRDIFF_MAX
        || !qa_fs_relative_valid(relative, false, error)) {
        if (root == NULL || created == NULL || (bytes.data == NULL && bytes.size != 0)
            || bytes.size > (size_t)PTRDIFF_MAX)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "invalid atomic file replacement");
        return false;
    }
    char *leaf = NULL;
    int parent = writable_parent(root, relative, true, &leaf, error);
    if (parent < 0)
        return false;
    struct stat previous;
    if (fstatat(parent, leaf, &previous, AT_SYMLINK_NOFOLLOW) == 0) {
        if (exclusive) { (void)close(parent); free(leaf); *created = false; return true; }
        if (!S_ISREG(previous.st_mode)) {
            qa_error_set(error, QA_ERROR_IO, 0,
                         "write target is not a regular file: %s", relative);
            (void)close(parent);
            free(leaf);
            return false;
        }
    } else if (errno != ENOENT) {
        int code = errno;
        (void)close(parent);
        free(leaf);
        return fail_errno(error, "cannot inspect write target", relative, code);
    }
    char temporary[96];
    int descriptor = -1;
    for (unsigned int attempt = 0; attempt < 64; ++attempt) {
        (void)snprintf(temporary, sizeof(temporary),
                       ".qa-write-%ld-%" PRIu64 "-%u",
                       (long)getpid(), nonce, attempt);
        if (strcmp(temporary, leaf) == 0)
            continue;
        descriptor = openat(parent, temporary,
                            O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                            0600);
        if (descriptor >= 0 || errno != EEXIST)
            break;
    }
    if (descriptor < 0) {
        int code = errno;
        (void)close(parent);
        free(leaf);
        return fail_errno(error, "cannot create temporary file for",
                          relative, code);
    }
    bool success = false, temporary_exists = true;
    size_t offset = 0;
    while (offset < bytes.size) {
        size_t amount = bytes.size - offset;
        if (amount > (size_t)SSIZE_MAX)
            amount = (size_t)SSIZE_MAX;
        ssize_t written = write(descriptor, bytes.data + offset, amount);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            int code = written == 0 ? EIO : errno;
            fail_errno(error, "cannot write", relative, code);
            goto done;
        }
        offset += (size_t)written;
    }
    if (fsync(descriptor) < 0) {
        fail_errno(error, "cannot sync", relative, errno);
        goto done;
    }
    if (close(descriptor) < 0) {
        descriptor = -1;
        fail_errno(error, "cannot close", relative, errno);
        goto done;
    }
    descriptor = -1;
    if (exclusive ? linkat(parent, temporary, parent, leaf, 0) < 0 : renameat(parent, temporary, parent, leaf) < 0) {
        if (exclusive && errno == EEXIST) { *created = false; success = true; goto done; }
        fail_errno(error, "cannot replace", relative, errno);
        goto done;
    }
    if (!exclusive) temporary_exists = false;
    if (fsync(parent) < 0) {
        fail_errno(error, "cannot sync parent of", relative, errno);
        goto done;
    }
    *created = true; success = true;
done:
    if (descriptor >= 0)
        (void)close(descriptor);
    if (temporary_exists)
        (void)unlinkat(parent, temporary, 0);
    (void)close(parent);
    free(leaf);
    return success;
}

bool qa_fs_root_remove(qa_fs_root *root, const char *relative,
                       qa_error *error)
{
    if (root == NULL || !qa_fs_relative_valid(relative, false, error)) {
        if (root == NULL)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "file removal needs a root");
        return false;
    }
    char *leaf = NULL;
    int parent = writable_parent(root, relative, false, &leaf, error);
    if (parent < 0)
        return false;
    struct stat info;
    bool success = false;
    if (fstatat(parent, leaf, &info, AT_SYMLINK_NOFOLLOW) < 0)
        fail_errno(error, "cannot inspect", relative, errno);
    else if (!S_ISREG(info.st_mode))
        qa_error_set(error, QA_ERROR_IO, 0,
                     "remove target is not a regular file: %s", relative);
    else if (unlinkat(parent, leaf, 0) < 0)
        fail_errno(error, "cannot remove", relative, errno);
    else if (fsync(parent) < 0)
        fail_errno(error, "cannot sync parent of", relative, errno);
    else
        success = true;
    (void)close(parent);
    free(leaf);
    return success;
}

bool qa_fs_root_stream_open(qa_fs_root *root, const char *relative,
                            qa_fs_stream_mode mode, bool resume,
                            qa_fs_stream **out, uint64_t *initial_size,
                            qa_error *error)
{
    return qa_fs_root_stream_open_result(root, relative, mode, resume, out,
                                          initial_size, NULL, error);
}

bool qa_fs_root_stream_open_result(qa_fs_root *root, const char *relative,
    qa_fs_stream_mode mode, bool resume, qa_fs_stream **out,
    uint64_t *initial_size, qa_fs_stream_open_stage *stage, qa_error *error)
{
    if (stage) *stage = QA_FS_STREAM_OPEN_PREPARE;
    if (out != NULL)
        *out = NULL;
    if (initial_size != NULL)
        *initial_size = 0;
    if (root == NULL || out == NULL || initial_size == NULL
        || mode < QA_FS_STREAM_WRITE || mode > QA_FS_STREAM_APPEND_SYNC
        || !qa_fs_relative_valid(relative, false, error)) {
        if (root == NULL || out == NULL || initial_size == NULL
            || mode < QA_FS_STREAM_WRITE || mode > QA_FS_STREAM_APPEND_SYNC)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "invalid writable stream state");
        return false;
    }
    qa_fs_stream *stream = calloc(1, sizeof(*stream));
    if (stream != NULL) {
        stream->descriptor = -1;
        stream->path = copy_string(relative);
    }
    if (stream == NULL || stream->path == NULL) {
        free(stream == NULL ? NULL : stream->path);
        free(stream);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate writable stream");
        return false;
    }
    bool append = mode != QA_FS_STREAM_WRITE;
    char *leaf = NULL;
    int parent = writable_parent(root, relative, !resume || append,
                                 &leaf, error);
    if (parent < 0) {
        qa_fs_stream_close(stream);
        return false;
    }
    int flags = O_WRONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    if (!resume || append)
        flags |= O_CREAT;
    if (append)
        flags |= O_APPEND;
    int descriptor;
    if (stage) *stage = QA_FS_STREAM_OPEN_NATIVE;
    do {
        descriptor = openat(parent, leaf, flags, 0600);
    } while (descriptor < 0 && errno == EINTR);
    int code = errno;
    (void)close(parent);
    free(leaf);
    if (descriptor < 0) {
        qa_fs_stream_close(stream);
        return fail_errno(error, "cannot open writable file", relative, code);
    }
    stream->descriptor = descriptor;
    if (stage) *stage = QA_FS_STREAM_OPEN_VALIDATE;
    struct stat info;
    if (!descriptor_stat(descriptor, &info)) {
        code = errno;
        qa_fs_stream_close(stream);
        return fail_errno(error, "cannot inspect writable file", relative, code);
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0) {
        qa_fs_stream_close(stream);
        qa_error_set(error, QA_ERROR_IO, 0,
                     "writable file is not regular: %s", relative);
        return false;
    }
    if (!resume && mode == QA_FS_STREAM_WRITE) {
        if (stage) *stage = QA_FS_STREAM_OPEN_TRUNCATE;
        if (ftruncate(descriptor, 0) < 0) {
            code = errno;
            qa_fs_stream_close(stream);
            return fail_errno(error, "cannot truncate writable file", relative, code);
        }
    }
    stream->mode = mode;
    stream->object = (qa_fs_object_reference){1, {(uint64_t)info.st_dev, (uint64_t)info.st_ino, 0}};
    stream->root = root;
    qa_fs_root_retain(root);
    *initial_size = !resume && mode == QA_FS_STREAM_WRITE
        ? 0 : (uint64_t)info.st_size;
    *out = stream;
    if (stage) *stage = QA_FS_STREAM_OPEN_READY;
    return true;
}

qa_fs_root *qa_fs_stream_root(const qa_fs_stream *stream)
{
    return stream ? stream->root : NULL;
}

bool qa_fs_stream_reference_read(const qa_fs_stream *stream, qa_fs_stream_reference *out)
{
    if (!stream || !out || !stream->root) return false;
    qa_fs_stream_reference value = {.object = stream->object, .mode = stream->mode, .path = stream->path};
    if (!qa_fs_root_reference_read(stream->root, &value.root)) return false;
    *out = value;
    return true;
}

bool qa_fs_stream_reference_valid(const qa_fs_stream_reference *reference, qa_error *error)
{
    if (!reference || reference->root.platform < 1 || reference->root.platform > 2 ||
        reference->object.platform != reference->root.platform ||
        (reference->root.platform == 1 && (reference->root.words[2] || reference->object.words[2])) ||
        reference->mode < QA_FS_STREAM_WRITE || reference->mode > QA_FS_STREAM_APPEND_SYNC) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid writable stream reference");
        return false;
    }
    return qa_fs_relative_valid(reference->path, false, error);
}

bool qa_fs_stream_resume(qa_fs_root *root, const qa_fs_stream_reference *reference,
    qa_fs_stream **out, uint64_t *size, qa_error *error)
{
    if (out) *out = NULL;
    if (size) *size = 0;
    if (!qa_fs_stream_reference_valid(reference, error)) return false;
    qa_fs_object_reference actual;
    if (!qa_fs_root_reference_read(root, &actual) || actual.platform != reference->root.platform ||
        memcmp(actual.words, reference->root.words, sizeof(actual.words))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Writable continuation has a different retained root");
        return false;
    }
    return qa_fs_root_stream_open(root, reference->path, reference->mode, true, out, size, error);
}

bool qa_fs_stream_resume_mapped(qa_fs_root *destination, const qa_fs_stream_reference *reference,
    const qa_fs_stream_resolver *resolver, qa_fs_stream **out, uint64_t *size, qa_error *error)
{
    if (out) *out = NULL;
    if (size) *size = 0;
    if (!destination || !out || !size || !resolver || !resolver->context || !resolver->root) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Writable continuation needs its actual root resolver");
        return false;
    }
    if (!qa_fs_stream_reference_valid(reference, error)) return false;
    qa_fs_root *mapped = NULL;
    bool ok = resolver->root(resolver->context, reference, &mapped, error);
    if (ok && !qa_fs_root_same_object(destination, mapped)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Writable root resolver differs from the actual destination owner");
        ok = false;
    }
    if (ok) ok = qa_fs_root_stream_open(mapped, reference->path, reference->mode, true, out, size, error);
    qa_fs_root_close(mapped);
    return ok;
}

bool qa_fs_stream_write_some(qa_fs_stream *stream, qa_bytes bytes, uint64_t position,
    size_t *written, qa_error *error)
{
    if (written) *written = 0;
    if (!stream || !written || (!bytes.data && bytes.size) || bytes.size > PTRDIFF_MAX ||
        (stream->mode == QA_FS_STREAM_WRITE &&
         (position > INT64_MAX || (uint64_t)bytes.size > (uint64_t)INT64_MAX - position))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid writable stream operation");
        return false;
    }
    if (!bytes.size) return true;
    size_t count = bytes.size > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes.size;
    ssize_t amount;
    do {
        amount = stream->mode == QA_FS_STREAM_WRITE ?
            pwrite(stream->descriptor, bytes.data, count, (off_t)position) :
            write(stream->descriptor, bytes.data, count);
    } while (amount < 0 && errno == EINTR);
    if (amount < 0) return fail_errno(error, "cannot write", stream->path, errno);
    *written = (size_t)amount;
    return true;
}

bool qa_fs_stream_size(qa_fs_stream *stream, uint64_t *out,
                       qa_error *error)
{
    if (stream == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "stream size needs a stream and output");
        return false;
    }
    struct stat info;
    if (!descriptor_stat(stream->descriptor, &info))
        return fail_errno(error, "cannot inspect writable file",
                          stream->path, errno);
    if (info.st_size < 0) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "writable file has an invalid size: %s", stream->path);
        return false;
    }
    *out = (uint64_t)info.st_size;
    return true;
}

bool qa_fs_stream_write(qa_fs_stream *stream, qa_bytes bytes,
                        uint64_t position, size_t *written,
                        uint64_t *resulting_size, qa_error *error)
{
    if (written != NULL)
        *written = 0;
    if (stream == NULL || resulting_size == NULL
        || (bytes.data == NULL && bytes.size != 0)
        || bytes.size > (size_t)PTRDIFF_MAX
        || (stream != NULL && stream->mode == QA_FS_STREAM_WRITE
            && (position > (uint64_t)INT64_MAX
                || (uint64_t)bytes.size > (uint64_t)INT64_MAX - position))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "invalid writable stream buffer or position");
        return false;
    }
    size_t offset = 0;
    while (offset < bytes.size) {
        size_t count = bytes.size - offset;
        if (count > (size_t)SSIZE_MAX)
            count = (size_t)SSIZE_MAX;
        ssize_t amount = stream->mode == QA_FS_STREAM_WRITE
            ? pwrite(stream->descriptor, bytes.data + offset, count,
                     (off_t)(position + offset))
            : write(stream->descriptor, bytes.data + offset, count);
        if (amount < 0 && errno == EINTR)
            continue;
        if (amount <= 0) {
            int code = amount == 0 ? EIO : errno;
            if (written != NULL)
                *written = offset;
            return fail_errno(error, "cannot write", stream->path, code);
        }
        offset += (size_t)amount;
        if (written != NULL)
            *written = offset;
    }
    if (stream->mode == QA_FS_STREAM_APPEND_SYNC
        && fsync(stream->descriptor) < 0)
        return fail_errno(error, "cannot sync", stream->path, errno);
    return qa_fs_stream_size(stream, resulting_size, error);
}

bool qa_fs_stream_close_checked(qa_fs_stream *stream, qa_error *error)
{
    if (stream == NULL)
        return true;
    bool ok = true;
    if (stream->descriptor >= 0 && close(stream->descriptor) < 0)
        ok = fail_errno(error, "cannot close", stream->path, errno);
    qa_fs_root_close(stream->root);
    free(stream->path);
    free(stream);
    return ok;
}

void qa_fs_stream_close(qa_fs_stream *stream)
{
    (void)qa_fs_stream_close_checked(stream, NULL);
}

struct qa_fs_stage {
    int parent, descriptor;
    char *leaf, *display;
    char temporary[64];
    bool sealed, published, readonly, closing, closing_keep, cleanup_done;
};
static bool stage_argument(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false;
}
static bool stage_identity(qa_fs_stage *stage, qa_fs_identity *out, qa_error *error) {
    struct stat info, named;
    if (!descriptor_stat(stage->descriptor, &info) ||
        fstatat(stage->parent, stage->temporary, &named, AT_SYMLINK_NOFOLLOW) < 0)
        return fail_errno(error, "cannot inspect staged file", stage->display, errno);
    if (!S_ISREG(info.st_mode) || !S_ISREG(named.st_mode) || info.st_dev != named.st_dev ||
        info.st_ino != named.st_ino || info.st_size < 0 || info.st_nlink != 1) {
        qa_error_set(error, QA_ERROR_IO, 0, "Staged file identity or link count changed: %s", stage->display); return false;
    }
    identity_from_stat(&info, out); return true;
}
static bool stage_open(qa_fs_root *root, const char *target, uint64_t nonce, bool resume, bool readonly, bool checked,
                       qa_fs_stage **out, uint64_t *initial, qa_error *error) {
    if (!root || !out || (checked && *out) || !initial || !qa_fs_relative_valid(target, false, error))
        return stage_argument(error, "Invalid contained staging request");
    qa_fs_stage *stage = calloc(1, sizeof(*stage));
    if (!stage) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating file stage"); return false; }
    if (checked) *out = stage;
    stage->parent = -1; stage->descriptor = -1;
    stage->readonly = readonly; stage->sealed = readonly;
    stage->display = copy_string(target);
    if (!stage->display) { if (!checked) qa_fs_stage_close(stage, true); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining stage target"); return false; }
    stage->parent = writable_parent(root, target, !resume, &stage->leaf, error);
    if (stage->parent < 0) { if (!checked) qa_fs_stage_close(stage, true); return false; }
    (void)snprintf(stage->temporary, sizeof(stage->temporary), ".qa-stage-%016" PRIx64, nonce);
    if (!strcmp(stage->temporary, stage->leaf)) { if (!checked) qa_fs_stage_close(stage, true); return stage_argument(error, "Stage and target names must differ"); }
    int flags = (readonly ? O_RDONLY : O_RDWR) | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    if (!resume) flags |= O_CREAT | O_EXCL;
    do { stage->descriptor = openat(stage->parent, stage->temporary, flags, 0600); }
    while (stage->descriptor < 0 && errno == EINTR);
    if (stage->descriptor < 0) {
        int code = errno; if (!checked) qa_fs_stage_close(stage, true);
        return fail_errno(error, "cannot open staged file", target, code);
    }
    /* A shared root cannot admit two active writers of the same resume nonce. */
    int locked = flock(stage->descriptor, (readonly ? LOCK_SH : LOCK_EX) | LOCK_NB);
    qa_fs_identity identity;
    struct stat private_info;
    bool private_file = descriptor_stat(stage->descriptor, &private_info) &&
        private_info.st_uid == geteuid() && !(private_info.st_mode & 077);
    if (locked < 0 || !private_file || !stage_identity(stage, &identity, error)) {
        if (locked < 0) fail_errno(error, "cannot lock staged file", target, errno);
        else if (!private_file) stage_argument(error, "Resume stage must remain private to its owner");
        if (!checked) qa_fs_stage_close(stage, resume);
        return false;
    }
    *initial = identity.words[2]; *out = stage; return true;
}
bool qa_fs_stage_open(qa_fs_root *root, const char *target, uint64_t nonce, bool resume,
    qa_fs_stage **out, uint64_t *initial, qa_error *error) {
    return stage_open(root, target, nonce, resume, false, false, out, initial, error);
}
bool qa_fs_stage_open_readonly(qa_fs_root *root, const char *target, uint64_t nonce,
    qa_fs_stage **out, uint64_t *initial, qa_error *error) {
    return stage_open(root, target, nonce, true, true, false, out, initial, error);
}
bool qa_fs_stage_open_checked(qa_fs_root *root, const char *target, uint64_t nonce, bool resume,
    qa_fs_stage **out, uint64_t *initial, qa_error *error) {
    return stage_open(root, target, nonce, resume, false, true, out, initial, error);
}
bool qa_fs_stage_open_readonly_checked(qa_fs_root *root, const char *target, uint64_t nonce,
    qa_fs_stage **out, uint64_t *initial, qa_error *error) {
    return stage_open(root, target, nonce, true, true, true, out, initial, error);
}
bool qa_fs_stage_size(qa_fs_stage *stage, uint64_t *out, qa_error *error) {
    if (!stage || stage->closing || !out) return stage_argument(error, "Missing stage size output");
    struct stat info;
    if (!descriptor_stat(stage->descriptor, &info)) return fail_errno(error, "cannot inspect staged size", stage->display, errno);
    if (info.st_size < 0) return stage_argument(error, "Staged file has negative size");
    *out = (uint64_t)info.st_size; return true;
}
bool qa_fs_stage_read(qa_fs_stage *stage, uint64_t offset, void *bytes, size_t capacity,
                       size_t *received, qa_error *error) {
    if (!stage || stage->closing || !received || (capacity && !bytes) || offset > INT64_MAX ||
        (uint64_t)capacity > (uint64_t)INT64_MAX - offset)
        return stage_argument(error, "Invalid stage read range");
    *received = 0;
    while (*received < capacity) {
        size_t amount = capacity - *received;
        if (amount > (size_t)SSIZE_MAX) amount = (size_t)SSIZE_MAX;
        ssize_t count = pread(stage->descriptor, (uint8_t *)bytes + *received, amount, (off_t)(offset + *received));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return fail_errno(error, "cannot read staged file", stage->display, errno);
        if (!count) break;
        *received += (size_t)count;
    }
    return true;
}
bool qa_fs_stage_write(qa_fs_stage *stage, uint64_t offset, qa_bytes bytes, size_t *written, qa_error *error) {
    if (!stage || stage->closing || !written || stage->sealed || stage->published || (bytes.size && !bytes.data) ||
        offset > INT64_MAX || (uint64_t)bytes.size > (uint64_t)INT64_MAX - offset)
        return stage_argument(error, "Invalid stage write range/state");
    *written = 0;
    while (*written < bytes.size) {
        size_t amount = bytes.size - *written;
        if (amount > (size_t)SSIZE_MAX) amount = (size_t)SSIZE_MAX;
        ssize_t count = pwrite(stage->descriptor, bytes.data + *written, amount, (off_t)(offset + *written));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return fail_errno(error, "cannot write staged file", stage->display, count ? errno : EIO);
        *written += (size_t)count;
    }
    return true;
}
bool qa_fs_stage_seal(qa_fs_stage *stage, qa_fs_identity *identity, qa_error *error) {
    if (!stage || stage->closing || !identity || stage->published || stage->readonly) return stage_argument(error, "Invalid stage seal");
    if (fsync(stage->descriptor) < 0) return fail_errno(error, "cannot sync staged file", stage->display, errno);
    if (!stage_identity(stage, identity, error)) return false;
    stage->sealed = true; return true;
}
struct qa_fs_stage_mapping { qa_bytes bytes; };
qa_bytes qa_fs_stage_mapping_bytes(const qa_fs_stage_mapping *mapping) {
    return mapping ? mapping->bytes : (qa_bytes){0};
}
void qa_fs_stage_unmap(qa_fs_stage_mapping *mapping) {
    if (!mapping) return;
    if (mapping->bytes.size) munmap((void *)mapping->bytes.data, mapping->bytes.size);
    free(mapping);
}
bool qa_fs_stage_map(qa_fs_stage *stage, qa_fs_stage_mapping **out, qa_error *error) {
    qa_fs_identity identity;
    if (!stage || stage->closing || !out || !stage->sealed || stage->published) return stage_argument(error, "Stage mapping requires a sealed unpublished file");
    if (!stage_identity(stage, &identity, error)) return false;
    uint64_t size = identity.words[2];
    if (size > SIZE_MAX || size > PTRDIFF_MAX) return stage_argument(error, "Stage exceeds mapping address range");
    qa_fs_stage_mapping *mapping = calloc(1, sizeof(*mapping));
    if (!mapping) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating stage inspection mapping"); return false; }
    if (size) {
        void *data = mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE, stage->descriptor, 0);
        if (data == MAP_FAILED) { free(mapping); return fail_errno(error, "cannot map staged file", stage->display, errno); }
        mapping->bytes = (qa_bytes){data, (size_t)size};
    }
    qa_fs_identity current;
    if (!stage_identity(stage, &current, error) || !qa_fs_identity_equal(&identity, &current)) {
        qa_fs_stage_unmap(mapping); return stage_argument(error, "Stage changed during mapping admission");
    }
    *out = mapping; return true;
}
bool qa_fs_stage_publish(qa_fs_stage *stage, const qa_fs_identity *expected, bool exclusive,
                          bool *created, qa_error *error) {
    if (!stage || stage->closing || !expected || !created || !stage->sealed || stage->published || stage->readonly)
        return stage_argument(error, "Invalid stage publication");
    *created = false;
    qa_fs_identity current;
    if (!stage_identity(stage, &current, error)) return false;
    if (!qa_fs_identity_equal(expected, &current)) {
        qa_error_set(error, QA_ERROR_IO, 0, "Staged file changed after inspection"); return false;
    }
    if (!exclusive) {
        struct stat previous;
        if (fstatat(stage->parent, stage->leaf, &previous, AT_SYMLINK_NOFOLLOW) == 0) {
            if (!S_ISREG(previous.st_mode)) return stage_argument(error, "Stage target is not regular");
        } else if (errno != ENOENT) return fail_errno(error, "cannot inspect stage target", stage->display, errno);
    }
    int result = exclusive ? linkat(stage->parent, stage->temporary, stage->parent, stage->leaf, 0) :
        renameat(stage->parent, stage->temporary, stage->parent, stage->leaf);
    if (result < 0) {
        if (exclusive && errno == EEXIST) return true;
        return fail_errno(error, "cannot publish staged file", stage->display, errno);
    }
    stage->published = true; *created = true;
    if (exclusive && unlinkat(stage->parent, stage->temporary, 0) < 0)
        return fail_errno(error, "cannot retire published stage name", stage->display, errno);
    if (fsync(stage->parent) < 0) return fail_errno(error, "cannot sync published stage parent", stage->display, errno);
    return true;
}
void qa_fs_stage_close(qa_fs_stage *stage, bool keep) {
    if (!stage) return;
    if (!keep && !stage->readonly && stage->parent >= 0 && stage->temporary[0]) {
        struct stat opened, named;
        if (stage->descriptor >= 0 && descriptor_stat(stage->descriptor, &opened) &&
            fstatat(stage->parent, stage->temporary, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
            opened.st_dev == named.st_dev && opened.st_ino == named.st_ino)
            (void)unlinkat(stage->parent, stage->temporary, 0);
    }
    if (stage->descriptor >= 0) (void)close(stage->descriptor);
    if (stage->parent >= 0) (void)close(stage->parent);
    free(stage->leaf); free(stage->display); free(stage);
}
bool qa_fs_stage_close_checked(qa_fs_stage **owned, bool keep, qa_error *error) {
    qa_fs_stage *stage = owned ? *owned : NULL;
    if (!stage) return true;
    if (stage->closing && stage->closing_keep != keep)
        return stage_argument(error, "Stage cleanup cannot change its retained-file decision");
    stage->closing = true; stage->closing_keep = keep;
    if (!stage->cleanup_done) {
        if (keep && !stage->readonly && !stage->published && stage->descriptor >= 0 && fsync(stage->descriptor) < 0)
            return fail_errno(error, "cannot sync retained stage", stage->display, errno);
        if (!keep && !stage->readonly && !stage->published && stage->parent >= 0 && stage->descriptor >= 0) {
            struct stat opened, named;
            if (!descriptor_stat(stage->descriptor, &opened)) return fail_errno(error, "cannot inspect closing stage", stage->display, errno);
            if (fstatat(stage->parent, stage->temporary, &named, AT_SYMLINK_NOFOLLOW) < 0) {
                if (errno != ENOENT) return fail_errno(error, "cannot inspect closing stage name", stage->display, errno);
            } else if (opened.st_dev == named.st_dev && opened.st_ino == named.st_ino &&
                unlinkat(stage->parent, stage->temporary, 0) < 0)
                return fail_errno(error, "cannot remove closing stage", stage->display, errno);
        }
        stage->cleanup_done = true;
    }
    if (stage->descriptor >= 0) {
        int descriptor = stage->descriptor; stage->descriptor = -1;
        if (close(descriptor) < 0) return fail_errno(error, "cannot close stage", stage->display, errno);
    }
    if (stage->parent >= 0) {
        int parent = stage->parent; stage->parent = -1;
        if (close(parent) < 0) return fail_errno(error, "cannot close stage parent", stage->display, errno);
    }
    free(stage->leaf); free(stage->display); free(stage); *owned = NULL; return true;
}

bool qa_fs_stream_sync(qa_fs_stream *stream, qa_error *error) {
    if (!stream) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing writable stream"); return false; }
    return fsync(stream->descriptor) == 0 || fail_errno(error, "cannot sync writable stream", stream->path, errno);
}

#include "filesystem_opened_posix.inc"
