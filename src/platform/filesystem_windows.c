#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

#include "filesystem_internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct qa_fs_root {
    size_t references;
    HANDLE handle;
    DWORD volume, file_index_high, file_index_low;
    uint64_t creation;
};

struct qa_fs_file {
    size_t references;
    HANDLE handle;
    char *path;
};

struct qa_fs_stream {
    HANDLE handle;
    qa_fs_stream_mode mode;
    char *path;
    qa_fs_root *root;
    qa_fs_object_reference object;
};

static bool missing_error(DWORD code)
{
    return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
        || code == ERROR_INVALID_NAME;
}

static _Thread_local qa_fs_native_error opened_native_failure;
static bool fail_windows(qa_error *error, const char *operation,
                         const char *path, DWORD code)
{
    opened_native_failure = (qa_fs_native_error){2, code, true};
    qa_error_set(error, missing_error(code) ? QA_ERROR_NOT_FOUND : QA_ERROR_IO,
                 0, "%s %s (Windows error %lu)", operation, path,
                 (unsigned long)code);
    return false;
}

static char *copy_string(const char *source)
{
    size_t length = strlen(source);
    char *copy = length == SIZE_MAX ? NULL : malloc(length + 1);
    if (copy != NULL)
        memcpy(copy, source, length + 1);
    return copy;
}

static wchar_t *copy_wide(const wchar_t *source)
{
    size_t length = wcslen(source);
    if (length > (SIZE_MAX / sizeof(*source)) - 1)
        return NULL;
    wchar_t *copy = malloc((length + 1) * sizeof(*copy));
    if (copy != NULL)
        memcpy(copy, source, (length + 1) * sizeof(*copy));
    return copy;
}

static wchar_t *utf8_to_wide(const char *text, qa_error *error)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    text, -1, NULL, 0);
    if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(wchar_t)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "invalid UTF-8 filesystem path");
        return NULL;
    }
    wchar_t *wide = malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate UTF-16 filesystem path");
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                            wide, count) == 0) {
        free(wide);
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "cannot convert UTF-8 filesystem path");
        return NULL;
    }
    for (wchar_t *cursor = wide; *cursor != L'\0'; ++cursor)
        if (*cursor == L'/')
            *cursor = L'\\';
    return wide;
}

static char *wide_to_utf8(const wchar_t *wide, qa_error *error)
{
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                    NULL, 0, NULL, NULL);
    if (count <= 0) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "cannot convert filesystem name to UTF-8");
        return NULL;
    }
    char *text = malloc((size_t)count);
    if (text == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate UTF-8 filesystem name");
        return NULL;
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                            text, count, NULL, NULL) == 0) {
        free(text);
        qa_error_set(error, QA_ERROR_IO, 0,
                     "cannot convert filesystem name to UTF-8");
        return NULL;
    }
    return text;
}

static wchar_t *full_path(const wchar_t *path, qa_error *error)
{
    DWORD needed = GetFullPathNameW(path, 0, NULL, NULL);
    if (needed == 0 || needed == UINT32_MAX
        || (size_t)needed > SIZE_MAX / sizeof(wchar_t) - 1) {
        fail_windows(error, "cannot resolve path", "", GetLastError());
        return NULL;
    }
    wchar_t *result = malloc(((size_t)needed + 1) * sizeof(*result));
    if (result == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate absolute filesystem path");
        return NULL;
    }
    DWORD written = GetFullPathNameW(path, needed + 1, result, NULL);
    if (written == 0 || written > needed) {
        DWORD code = GetLastError();
        free(result);
        fail_windows(error, "cannot resolve path", "", code);
        return NULL;
    }
    return result;
}

static wchar_t *handle_path(HANDLE handle, qa_error *error)
{
    DWORD needed = GetFinalPathNameByHandleW(handle, NULL, 0,
                                              FILE_NAME_NORMALIZED
                                              | VOLUME_NAME_DOS);
    if (needed == 0 || needed == UINT32_MAX
        || (size_t)needed > SIZE_MAX / sizeof(wchar_t) - 1) {
        fail_windows(error, "cannot resolve opened filesystem object", "",
                     GetLastError());
        return NULL;
    }
    wchar_t *path = malloc(((size_t)needed + 1) * sizeof(*path));
    if (path == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate final filesystem path");
        return NULL;
    }
    DWORD written = GetFinalPathNameByHandleW(handle, path, needed + 1,
                                               FILE_NAME_NORMALIZED
                                               | VOLUME_NAME_DOS);
    if (written == 0 || written > needed) {
        DWORD code = GetLastError();
        free(path);
        fail_windows(error, "cannot resolve opened filesystem object", "",
                     code);
        return NULL;
    }
    return path;
}

static bool path_within(const wchar_t *root, const wchar_t *path)
{
    size_t length = wcslen(root);
    while (length != 0
           && (root[length - 1] == L'\\' || root[length - 1] == L'/'))
        --length;
    if (length > (size_t)INT_MAX || wcslen(path) < length
        || CompareStringOrdinal(root, (int)length, path, (int)length, FALSE)
        != CSTR_EQUAL)
        return false;
    return path[length] == L'\0' || path[length] == L'\\'
        || path[length] == L'/';
}

static wchar_t *join_wide(const wchar_t *base, const char *relative,
                          qa_error *error)
{
    wchar_t *tail = utf8_to_wide(relative, error);
    if (tail == NULL)
        return NULL;
    for (wchar_t *cursor = tail; *cursor != L'\0'; ++cursor)
        if (*cursor == L'/')
            *cursor = L'\\';
    size_t x = wcslen(base), y = wcslen(tail);
    bool separator = x != 0 && base[x - 1] != L'\\' && base[x - 1] != L'/';
    size_t limit = SIZE_MAX / sizeof(wchar_t);
    size_t extra = separator ? 2 : 1;
    if (x > limit - extra || y > limit - x - extra) {
        free(tail);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "filesystem path is too long");
        return NULL;
    }
    wchar_t *path = malloc((x + y + (separator ? 2 : 1)) * sizeof(*path));
    if (path == NULL) {
        free(tail);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate filesystem path");
        return NULL;
    }
    memcpy(path, base, x * sizeof(*path));
    if (separator)
        path[x++] = L'\\';
    memcpy(path + x, tail, (y + 1) * sizeof(*path));
    free(tail);
    return path;
}

static bool handle_info(HANDLE handle, BY_HANDLE_FILE_INFORMATION *legacy,
                        FILE_BASIC_INFO *basic, qa_error *error,
                        const char *path)
{
    if (!GetFileInformationByHandle(handle, legacy)
        || !GetFileInformationByHandleEx(handle, FileBasicInfo, basic,
                                         sizeof(*basic)))
        return fail_windows(error, "cannot inspect", path, GetLastError());
    return true;
}

static void identity_from_info(const BY_HANDLE_FILE_INFORMATION *legacy,
                               const FILE_BASIC_INFO *basic,
                               qa_fs_identity *out)
{
    uint64_t index = ((uint64_t)legacy->nFileIndexHigh << 32)
        | legacy->nFileIndexLow;
    uint64_t size = ((uint64_t)legacy->nFileSizeHigh << 32)
        | legacy->nFileSizeLow;
    *out = (qa_fs_identity){{
        legacy->dwVolumeSerialNumber,
        index,
        size,
        (uint64_t)basic->LastWriteTime.QuadPart,
        (uint64_t)basic->ChangeTime.QuadPart,
        (uint64_t)basic->CreationTime.QuadPart,
        basic->FileAttributes
    }};
}

static qa_fs_entry_kind kind_from_attributes(DWORD attributes)
{
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return QA_FS_LINK;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return QA_FS_DIRECTORY;
    if ((attributes & FILE_ATTRIBUTE_DEVICE) != 0)
        return QA_FS_OTHER;
    return QA_FS_REGULAR;
}

bool qa_fs_identity_modified_time(const qa_fs_identity *identity, qa_fs_timestamp *out)
{
    if (!identity || !out) return false;
    uint64_t encoded = identity->words[3];
    int64_t ticks = encoded <= INT64_MAX ? (int64_t)encoded :
        -1 - (int64_t)(UINT64_MAX - encoded);
    int64_t seconds = ticks / INT64_C(10000000);
    int64_t remainder = ticks % INT64_C(10000000);
    if (remainder < 0) { --seconds; remainder += INT64_C(10000000); }
    out->seconds = seconds - INT64_C(11644473600);
    out->nanoseconds = (uint32_t)remainder * 100;
    return true;
}

static HANDLE open_contained(qa_fs_root *root, const char *relative,
                             DWORD access, DWORD flags, qa_error *error)
{
    wchar_t *current_root = handle_path(root->handle, error);
    if (current_root == NULL)
        return INVALID_HANDLE_VALUE;
    wchar_t *path = join_wide(current_root,
                              relative[0] == '\0' ? "." : relative,
                              error);
    if (path == NULL) {
        free(current_root);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE handle = CreateFileW(path, access,
                                FILE_SHARE_READ | FILE_SHARE_WRITE
                                | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, flags, NULL);
    DWORD code = GetLastError();
    free(path);
    if (handle == INVALID_HANDLE_VALUE) {
        free(current_root);
        fail_windows(error, "cannot open contained path", relative, code);
        return INVALID_HANDLE_VALUE;
    }
    wchar_t *final = handle_path(handle, error);
    if (final == NULL || !path_within(current_root, final)) {
        free(final);
        free(current_root);
        CloseHandle(handle);
        if (error == NULL || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_IO, 0,
                         "contained path escapes its filesystem root: %s",
                         relative);
        return INVALID_HANDLE_VALUE;
    }
    free(final);
    free(current_root);
    return handle;
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
    wchar_t *input = utf8_to_wide(path, error);
    wchar_t *absolute = input == NULL ? NULL : full_path(input, error);
    free(input);
    if (absolute == NULL)
        return false;
    HANDLE handle = CreateFileW(absolute, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE
                                | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD code = GetLastError();
    if (handle == INVALID_HANDLE_VALUE) {
        free(absolute);
        return fail_windows(error, "cannot open directory", path, code);
    }
    BY_HANDLE_FILE_INFORMATION legacy;
    FILE_BASIC_INFO basic;
    if (!handle_info(handle, &legacy, &basic, error, path)
        || (legacy.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        if (error == NULL || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_IO, 0,
                         "filesystem root is not a directory: %s", path);
        CloseHandle(handle);
        free(absolute);
        return false;
    }
    wchar_t *final = handle_path(handle, error);
    free(absolute);
    if (final == NULL) {
        free(final);
        CloseHandle(handle);
        return false;
    }
    free(final);
    qa_fs_root *root = calloc(1, sizeof(*root));
    if (root == NULL) {
        CloseHandle(handle);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate filesystem root");
        return false;
    }
    root->references = 1;
    root->handle = handle;
    root->volume = legacy.dwVolumeSerialNumber;
    root->file_index_high = legacy.nFileIndexHigh;
    root->file_index_low = legacy.nFileIndexLow;
    root->creation = ((uint64_t)legacy.ftCreationTime.dwHighDateTime << 32) | legacy.ftCreationTime.dwLowDateTime;
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
        && left->volume == right->volume
        && left->file_index_high == right->file_index_high
        && left->file_index_low == right->file_index_low;
}

bool qa_fs_root_reference_read(const qa_fs_root *root, qa_fs_object_reference *out)
{
    if (!root || !out) return false;
    *out = (qa_fs_object_reference){2, {root->volume,
        ((uint64_t)root->file_index_high << 32) | root->file_index_low, root->creation}};
    return true;
}

void qa_fs_root_close(qa_fs_root *root)
{
    if (root == NULL || --root->references != 0)
        return;
    CloseHandle(root->handle);
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
    wchar_t *current_wide = handle_path(root->handle, error);
    char *current = current_wide == NULL
        ? NULL : wide_to_utf8(current_wide, error);
    free(current_wide);
    if (current == NULL)
        return false;
    if (relative[0] == '\0' || strcmp(relative, ".") == 0) {
        *out = copy_string(current);
    } else {
        size_t base = strlen(current), tail = strlen(relative);
        bool separator = base == 0
            || (current[base - 1] != '\\' && current[base - 1] != '/');
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
            memcpy(*out, current, base);
            if (separator)
                (*out)[base++] = '\\';
            memcpy(*out + base, relative, tail + 1);
        }
    }
    free(current);
    if (*out == NULL)
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate filesystem path");
    return *out != NULL;
}

static bool list_handle(HANDLE directory, const char *display,
                        qa_fs_listing *out, qa_error *error)
{
    const DWORD capacity = UINT32_C(65536);
    unsigned char *buffer = malloc(capacity);
    if (buffer == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate directory listing buffer");
        return false;
    }
    bool ok = true;
    for (;;) {
        if (!GetFileInformationByHandleEx(directory, FileIdBothDirectoryInfo,
                                          buffer, capacity)) {
            DWORD code = GetLastError();
            if (code != ERROR_NO_MORE_FILES)
                ok = fail_windows(error, "cannot list directory", display, code);
            break;
        }
        const FILE_ID_BOTH_DIR_INFO *entry = (const void *)buffer;
        for (;;) {
            if (entry->FileNameLength % sizeof(wchar_t) != 0
                || entry->FileNameLength
                    > UINT32_MAX - (DWORD)sizeof(wchar_t)) {
                qa_error_set(error, QA_ERROR_IO, 0,
                             "invalid directory entry returned for %s", display);
                ok = false;
                break;
            }
            size_t count = entry->FileNameLength / sizeof(wchar_t);
            wchar_t *wide = malloc((count + 1) * sizeof(*wide));
            if (wide == NULL) {
                qa_error_set(error, QA_ERROR_MEMORY, 0,
                             "cannot allocate directory entry name");
                ok = false;
                break;
            }
            memcpy(wide, entry->FileName, entry->FileNameLength);
            wide[count] = L'\0';
            if (wcscmp(wide, L".") != 0 && wcscmp(wide, L"..") != 0) {
                char *name = wide_to_utf8(wide, error);
                if (name == NULL
                    || !qa_fs_listing_add(
                        out, name,
                        kind_from_attributes(entry->FileAttributes), error)) {
                    free(name);
                    free(wide);
                    ok = false;
                    break;
                }
                free(name);
            }
            free(wide);
            if (entry->NextEntryOffset == 0)
                break;
            entry = (const void *)((const unsigned char *)entry
                                    + entry->NextEntryOffset);
        }
        if (!ok)
            break;
    }
    free(buffer);
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
    wchar_t *wide = utf8_to_wide(path, error);
    if (wide == NULL)
        return false;
    HANDLE handle = CreateFileW(wide, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE
                                | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD code = GetLastError();
    free(wide);
    if (handle == INVALID_HANDLE_VALUE)
        return fail_windows(error, "cannot open directory", path, code);
    bool ok = list_handle(handle, path, out, error);
    CloseHandle(handle);
    return ok;
}

bool qa_fs_root_list(qa_fs_root *root, const char *relative,
                     qa_fs_listing *out, qa_error *error)
{
    if (out != NULL)
        *out = (qa_fs_listing){0};
    if (root == NULL || out == NULL
        || !qa_fs_relative_valid(relative, true, error))
        return false;
    HANDLE handle = open_contained(root, relative,
                                   FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                   FILE_FLAG_BACKUP_SEMANTICS, error);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    bool ok = list_handle(handle,
                          relative[0] == '\0' ? "." : relative,
                          out, error);
    CloseHandle(handle);
    return ok;
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
    qa_error local = {0};
    HANDLE handle = open_contained(root, relative, FILE_READ_ATTRIBUTES,
                                   FILE_FLAG_BACKUP_SEMANTICS, &local);
    if (handle == INVALID_HANDLE_VALUE) {
        if (local.code == QA_ERROR_NOT_FOUND)
            return true;
        if (error != NULL)
            *error = local;
        return false;
    }
    BY_HANDLE_FILE_INFORMATION legacy;
    FILE_BASIC_INFO basic;
    bool ok = handle_info(handle, &legacy, &basic, error,
                          relative[0] == '\0' ? "." : relative);
    CloseHandle(handle);
    if (!ok)
        return false;
    *kind = kind_from_attributes(legacy.dwFileAttributes);
    if (identity != NULL)
        identity_from_info(&legacy, &basic, identity);
    return true;
}

static bool status_wide(const wchar_t *path, bool follow_links,
                        qa_fs_entry_kind *kind, qa_fs_identity *identity,
                        qa_error *error, const char *display)
{
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (!follow_links)
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    HANDLE handle = CreateFileW(path, FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE
                                | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, flags, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        if (missing_error(code)) {
            *kind = QA_FS_MISSING;
            return true;
        }
        return fail_windows(error, "cannot inspect", display, code);
    }
    BY_HANDLE_FILE_INFORMATION legacy;
    FILE_BASIC_INFO basic;
    bool ok = handle_info(handle, &legacy, &basic, error, display);
    CloseHandle(handle);
    if (!ok)
        return false;
    *kind = kind_from_attributes(legacy.dwFileAttributes);
    if (identity != NULL)
        identity_from_info(&legacy, &basic, identity);
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
    wchar_t *wide = utf8_to_wide(path, error);
    if (wide == NULL)
        return false;
    bool ok = status_wide(wide, follow_links, kind, identity, error, path);
    free(wide);
    return ok;
}

static bool file_from_handle(HANDLE handle, const char *path,
                             qa_fs_file **out, qa_fs_identity *identity,
                             qa_status nonregular, qa_error *error)
{
    BY_HANDLE_FILE_INFORMATION legacy;
    FILE_BASIC_INFO basic;
    if (!handle_info(handle, &legacy, &basic, error, path)) {
        CloseHandle(handle);
        return false;
    }
    if (kind_from_attributes(legacy.dwFileAttributes) != QA_FS_REGULAR) {
        CloseHandle(handle);
        qa_error_set(error, nonregular, 0,
                     "filesystem object is not a regular file: %s", path);
        return false;
    }
    qa_fs_file *file = calloc(1, sizeof(*file));
    if (file != NULL)
        file->path = copy_string(path);
    if (file == NULL || file->path == NULL) {
        free(file);
        CloseHandle(handle);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate filesystem file");
        return false;
    }
    file->references = 1;
    file->handle = handle;
    if (identity != NULL)
        identity_from_info(&legacy, &basic, identity);
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
    wchar_t *input = utf8_to_wide(path, error);
    wchar_t *absolute = input == NULL ? NULL : full_path(input, error);
    free(input);
    if (absolute == NULL)
        return false;
    char *display = wide_to_utf8(absolute, error);
    if (display == NULL) {
        free(absolute);
        return false;
    }
    HANDLE handle = CreateFileW(absolute, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE
                                | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD code = GetLastError();
    free(absolute);
    if (handle == INVALID_HANDLE_VALUE) {
        free(display);
        return fail_windows(error, "cannot open file", path, code);
    }
    bool ok = file_from_handle(handle, display, out, identity,
                               QA_ERROR_IO, error);
    free(display);
    return ok;
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
    HANDLE handle = open_contained(root, relative, GENERIC_READ,
                                   FILE_ATTRIBUTE_NORMAL, error);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    char *path = NULL;
    if (!qa_fs_root_join(root, relative, &path, error)) {
        CloseHandle(handle);
        return false;
    }
    bool ok = file_from_handle(handle, path, out, identity,
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
    CloseHandle(file->handle);
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
    BY_HANDLE_FILE_INFORMATION legacy;
    FILE_BASIC_INFO basic;
    if (!handle_info(file->handle, &legacy, &basic, error, file->path))
        return false;
    identity_from_info(&legacy, &basic, out);
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
    LARGE_INTEGER zero = {.QuadPart = 0};
    if (!SetFilePointerEx(file->handle, zero, NULL, FILE_BEGIN))
        return fail_windows(error, "cannot seek file prefix", file->path, GetLastError());
    size_t limit = before.words[2] < (uint64_t)capacity ? (size_t)before.words[2] : capacity;
    size_t offset = 0;
    while (offset < limit) {
        size_t remaining = limit - offset;
        DWORD request = remaining > UINT32_C(0x7ffff000) ? UINT32_C(0x7ffff000) : (DWORD)remaining;
        DWORD count = 0;
        if (!ReadFile(file->handle, (uint8_t *)bytes + offset, request, &count, NULL))
            return fail_windows(error, "cannot read file prefix", file->path, GetLastError());
        if (!count) return fail_windows(error, "cannot read file prefix", file->path, ERROR_HANDLE_EOF);
        offset += count;
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
    LARGE_INTEGER zero = {.QuadPart = 0};
    if (!SetFilePointerEx(file->handle, zero, NULL, FILE_BEGIN)) {
        DWORD code = GetLastError();
        qa_buffer_free(&result);
        return fail_windows(error, "cannot seek file", file->path, code);
    }
    size_t offset = 0;
    while (offset < result.size) {
        size_t remaining = result.size - offset;
        DWORD request = remaining > UINT32_C(0x7ffff000)
            ? UINT32_C(0x7ffff000) : (DWORD)remaining;
        DWORD received = 0;
        if (!ReadFile(file->handle, result.data + offset, request,
                      &received, NULL) || received == 0) {
            DWORD code = GetLastError();
            if (code == ERROR_SUCCESS)
                code = ERROR_HANDLE_EOF;
            qa_buffer_free(&result);
            return fail_windows(error, "cannot read file", file->path, code);
        }
        offset += received;
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

static wchar_t *wide_child(const wchar_t *parent, const wchar_t *leaf,
                           qa_error *error);

typedef struct writable_path {
    HANDLE *directories;
    size_t directory_count;
    wchar_t *root_path;
    wchar_t *parent_path;
    wchar_t *leaf;
} writable_path;

static void writable_path_close(writable_path *path)
{
    if (path == NULL)
        return;
    for (size_t i = path->directory_count; i != 0; --i)
        CloseHandle(path->directories[i - 1]);
    free(path->directories);
    free(path->root_path);
    free(path->parent_path);
    free(path->leaf);
    *path = (writable_path){0};
}

static bool same_handle_object(const BY_HANDLE_FILE_INFORMATION *left,
                               const BY_HANDLE_FILE_INFORMATION *right)
{
    return left->dwVolumeSerialNumber == right->dwVolumeSerialNumber
        && left->nFileIndexHigh == right->nFileIndexHigh
        && left->nFileIndexLow == right->nFileIndexLow
        && CompareFileTime(&left->ftCreationTime,
                           &right->ftCreationTime) == 0;
}

static bool lock_writable_directory(
    const wchar_t *path, const char *display, HANDLE *out,
    wchar_t **final, BY_HANDLE_FILE_INFORMATION *identity, qa_error *error)
{
    *out = INVALID_HANDLE_VALUE;
    *final = NULL;
    HANDLE handle = CreateFileW(path,
                                FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS
                                | FILE_FLAG_OPEN_REPARSE_POINT,
                                NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return fail_windows(error, "cannot lock writable directory", display,
                            GetLastError());
    BY_HANDLE_FILE_INFORMATION legacy;
    FILE_BASIC_INFO basic;
    bool ok = handle_info(handle, &legacy, &basic, error, display);
    if (ok && ((legacy.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
               || (legacy.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "writable path contains a link or non-directory: %s",
                     display);
        ok = false;
    }
    wchar_t *opened = ok ? handle_path(handle, error) : NULL;
    if (!ok || opened == NULL) {
        free(opened);
        CloseHandle(handle);
        return false;
    }
    *identity = legacy;
    *final = opened;
    *out = handle;
    return true;
}

static bool writable_parent(qa_fs_root *root, const char *relative,
                            bool create, writable_path *out,
                            qa_error *error)
{
    *out = (writable_path){0};
    wchar_t *wide = utf8_to_wide(relative, error);
    if (wide == NULL)
        return false;
    size_t directory_capacity = 1;
    for (wchar_t *cursor = wide; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'/')
            *cursor = L'\\';
        if (*cursor == L'\\' && directory_capacity != SIZE_MAX)
            ++directory_capacity;
    }
    if (directory_capacity == SIZE_MAX
        || directory_capacity > SIZE_MAX / sizeof(*out->directories)) {
        free(wide);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "writable directory chain is too large");
        return false;
    }
    out->directories = malloc(directory_capacity * sizeof(*out->directories));
    if (out->directories == NULL) {
        free(wide);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate writable directory chain");
        return false;
    }
    BY_HANDLE_FILE_INFORMATION root_identity;
    FILE_BASIC_INFO root_basic;
    wchar_t *current = NULL;
    HANDLE locked = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION locked_identity;
    wchar_t *root_name = handle_path(root->handle, error);
    bool ok = root_name != NULL
        && handle_info(root->handle, &root_identity, &root_basic,
                       error, relative)
        && lock_writable_directory(root_name, relative, &locked, &current,
                                   &locked_identity, error);
    free(root_name);
    if (!ok) {
        free(wide);
        writable_path_close(out);
        return false;
    }
    if (!same_handle_object(&root_identity, &locked_identity)) {
        CloseHandle(locked);
        free(current);
        free(wide);
        writable_path_close(out);
        qa_error_set(error, QA_ERROR_IO, 0,
                     "filesystem root changed while locking: %s", relative);
        return false;
    }
    out->directories[out->directory_count++] = locked;
    out->root_path = copy_wide(current);
    if (out->root_path == NULL) {
        free(current);
        free(wide);
        writable_path_close(out);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot retain locked filesystem root");
        return false;
    }

    wchar_t *component = wide;
    wchar_t *separator;
    while ((separator = wcschr(component, L'\\')) != NULL) {
        *separator = L'\0';
        wchar_t *next_path = wide_child(current, component, error);
        if (next_path == NULL) {
            free(current);
            free(wide);
            writable_path_close(out);
            return false;
        }
        if (create && !CreateDirectoryW(next_path, NULL)
            && GetLastError() != ERROR_ALREADY_EXISTS) {
            DWORD code = GetLastError();
            free(next_path);
            free(current);
            free(wide);
            writable_path_close(out);
            return fail_windows(error, "cannot create writable directory",
                                relative, code);
        }
        wchar_t *opened = NULL;
        HANDLE next = INVALID_HANDLE_VALUE;
        BY_HANDLE_FILE_INFORMATION next_identity;
        ok = lock_writable_directory(next_path, relative, &next, &opened,
                                     &next_identity, error);
        free(next_path);
        if (!ok || !path_within(out->root_path, opened)) {
            if (next != INVALID_HANDLE_VALUE)
                CloseHandle(next);
            free(opened);
            free(current);
            free(wide);
            writable_path_close(out);
            if (ok)
                qa_error_set(error, QA_ERROR_IO, 0,
                             "writable path escapes its filesystem root: %s",
                             relative);
            return false;
        }
        out->directories[out->directory_count++] = next;
        free(current);
        current = opened;
        component = separator + 1;
    }
    out->leaf = copy_wide(component);
    if (out->leaf == NULL) {
        free(current);
        free(wide);
        writable_path_close(out);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot retain writable leaf name");
        return false;
    }
    free(wide);
    out->parent_path = current;
    return true;
}

static wchar_t *wide_child(const wchar_t *parent, const wchar_t *leaf,
                           qa_error *error)
{
    size_t x = wcslen(parent), y = wcslen(leaf);
    bool separator = x != 0 && parent[x - 1] != L'\\'
        && parent[x - 1] != L'/';
    size_t extra = separator ? 2 : 1;
    if (x > SIZE_MAX / sizeof(wchar_t) - extra
        || y > SIZE_MAX / sizeof(wchar_t) - x - extra) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "writable path is too long");
        return NULL;
    }
    size_t count = x + y + extra;
    wchar_t *path = malloc(count * sizeof(*path));
    if (path != NULL) {
        if (separator)
            (void)swprintf(path, count, L"%ls\\%ls", parent, leaf);
        else
            (void)swprintf(path, count, L"%ls%ls", parent, leaf);
    } else {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate writable path");
    }
    return path;
}

bool qa_fs_path_create_directory(const char *path, qa_error *error)
{
    if (!path || !*path) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "directory creation needs a native path");
        return false;
    }
    wchar_t *input = utf8_to_wide(path, error);
    wchar_t *absolute = input ? full_path(input, error) : NULL;
    free(input);
    if (!absolute) return false;
    wchar_t *probe = copy_wide(absolute);
    if (!probe) { free(absolute); qa_error_set(error, QA_ERROR_MEMORY, 0,
                                              "retaining startup directory path"); return false; }
    size_t length = wcslen(probe);
    qa_fs_root *root = NULL; bool ok = true;
    while (true) {
        char *native = wide_to_utf8(probe, error);
        if (!native) { ok = false; break; }
        qa_error local = {0};
        bool opened = qa_fs_root_open(native, &root, &local);
        free(native);
        if (opened) break;
        if (local.code != QA_ERROR_NOT_FOUND) { if (error) *error = local; ok = false; break; }
        size_t parent = length;
        while (parent && probe[parent - 1] == L'\\') --parent;
        while (parent && probe[parent - 1] != L'\\') --parent;
        if (!parent || parent == length) { if (error) *error = local; ok = false; break; }
        /* Keep the separator so a drive root remains C:\ rather than C:. */
        length = parent; probe[length] = 0;
    }
    if (ok) {
        wchar_t *tail = absolute + length;
        while (*tail == L'\\') ++tail;
        char *relative = wide_to_utf8(tail, error);
        if (!relative) ok = false;
        else {
            for (char *cursor = relative; *cursor; ++cursor) if (*cursor == '\\') *cursor = '/';
            ok = !*relative || qa_fs_root_create_directory(root, relative, error);
            free(relative);
        }
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
    writable_path locked;
    if (!writable_parent(root, relative, true, &locked, error)) return false;
    wchar_t *target = wide_child(locked.parent_path, locked.leaf, error);
    if (target == NULL) { writable_path_close(&locked); return false; }
    bool ok = CreateDirectoryW(target, NULL) != 0;
    DWORD code = GetLastError();
    if (!ok && code == ERROR_ALREADY_EXISTS) ok = true;
    if (!ok) fail_windows(error, "cannot create writable directory", relative, code);
    HANDLE directory = INVALID_HANDLE_VALUE;
    wchar_t *opened = NULL;
    BY_HANDLE_FILE_INFORMATION identity;
    if (ok) ok = lock_writable_directory(target, relative, &directory,
                                         &opened, &identity, error);
    if (ok && !path_within(locked.root_path, opened)) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "created writable directory escapes its root: %s", relative);
        ok = false;
    }
    if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
    free(opened); free(target); writable_path_close(&locked);
    return ok;
}

bool qa_fs_root_replace(qa_fs_root *root, const char *relative,
                        qa_bytes bytes, uint64_t nonce, qa_error *error)
{
    bool created;
    return qa_fs_root_publish(root, relative, bytes, nonce, false, false, &created, error);
}

static PSECURITY_DESCRIPTOR private_descriptor(qa_error *error)
{
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        fail_windows(error, "cannot inspect private-file owner", "", GetLastError()); return NULL;
    }
    DWORD size = 0;
    (void)GetTokenInformation(token, TokenUser, NULL, 0, &size);
    TOKEN_USER *user = size ? malloc(size) : NULL;
    if (!user) {
        CloseHandle(token); qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating private-file owner"); return NULL;
    }
    if (!GetTokenInformation(token, TokenUser, user, size, &size)) {
        DWORD code = GetLastError(); free(user); CloseHandle(token);
        fail_windows(error, "cannot inspect private-file owner", "", code); return NULL;
    }
    LPWSTR sid = NULL;
    bool ok = ConvertSidToStringSidW(user->User.Sid, &sid) != 0;
    DWORD code = GetLastError(); free(user); CloseHandle(token);
    if (!ok) { fail_windows(error, "cannot encode private-file owner", "", code); return NULL; }
    size_t length = wcslen(sid);
    if (length > (SIZE_MAX / sizeof(wchar_t) - 32) / 2) {
        LocalFree(sid); qa_error_set(error, QA_ERROR_MEMORY, 0, "private-file owner is too long"); return NULL;
    }
    size_t capacity = length * 2 + 32;
    wchar_t *text = malloc(capacity * sizeof(*text));
    if (!text) { LocalFree(sid); qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating private-file permissions"); return NULL; }
    (void)swprintf(text, capacity, L"O:%lsD:P(A;;FA;;;%ls)", sid, sid);
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = NULL;
    ok = ConvertStringSecurityDescriptorToSecurityDescriptorW(text, SDDL_REVISION_1, &descriptor, NULL) != 0;
    code = GetLastError(); free(text);
    if (!ok) fail_windows(error, "cannot encode private-file permissions", "", code);
    return descriptor;
}

static bool private_handle(HANDLE handle, const char *path, qa_error *error)
{
    const SECURITY_INFORMATION fields = OWNER_SECURITY_INFORMATION
        | DACL_SECURITY_INFORMATION;
    DWORD size = 0;
    (void)GetKernelObjectSecurity(handle, fields, NULL, 0, &size);
    if (!size)
        return fail_windows(error, "cannot inspect staged permissions", path,
                            GetLastError());
    PSECURITY_DESCRIPTOR actual = malloc(size);
    if (!actual) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "allocating staged permissions");
        return false;
    }
    if (!GetKernelObjectSecurity(handle, fields, actual, size, &size)) {
        DWORD code = GetLastError();
        free(actual);
        return fail_windows(error, "cannot inspect staged permissions", path,
                            code);
    }
    PSECURITY_DESCRIPTOR expected = private_descriptor(error);
    if (!expected) { free(actual); return false; }
    PSID owner = NULL, current_owner = NULL;
    PACL acl = NULL;
    BOOL owner_defaulted = FALSE, current_defaulted = FALSE;
    BOOL present = FALSE, acl_defaulted = FALSE;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ACL_SIZE_INFORMATION information = {0};
    void *entry = NULL;
    bool ok = GetSecurityDescriptorOwner(actual, &owner, &owner_defaulted)
        && GetSecurityDescriptorOwner(expected, &current_owner, &current_defaulted)
        && owner && current_owner && EqualSid(owner, current_owner)
        && GetSecurityDescriptorDacl(actual, &present, &acl, &acl_defaulted)
        && present && acl
        && GetSecurityDescriptorControl(actual, &control, &revision)
        && (control & SE_DACL_PROTECTED)
        && GetAclInformation(acl, &information, sizeof information, AclSizeInformation)
        && information.AceCount == 1 && GetAce(acl, 0, &entry);
    if (ok) {
        const ACCESS_ALLOWED_ACE *ace = entry;
        ok = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE
            && ace->Header.AceFlags == 0
            && ace->Mask == FILE_ALL_ACCESS
            && EqualSid((PSID)&ace->SidStart, current_owner);
    }
    LocalFree(expected);
    free(actual);
    if (!ok)
        qa_error_set(error, QA_ERROR_IO, 0,
                     "staged file is not private to the current owner: %s", path);
    return ok;
}

bool qa_fs_root_publish(qa_fs_root *root, const char *relative,
                        qa_bytes bytes, uint64_t nonce, bool exclusive,
                        bool private_file, bool *created, qa_error *error)
{
    if (root == NULL || created == NULL || (bytes.data == NULL && bytes.size != 0)
        || bytes.size > (size_t)PTRDIFF_MAX
        || !qa_fs_relative_valid(relative, false, error)) {
        if (root == NULL || created == NULL || (bytes.data == NULL && bytes.size != 0)
            || bytes.size > (size_t)PTRDIFF_MAX)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "invalid atomic file replacement");
        return false;
    }
    writable_path locked;
    if (!writable_parent(root, relative, true, &locked, error))
        return false;
    wchar_t *target = wide_child(locked.parent_path, locked.leaf, error);
    if (target == NULL) {
        writable_path_close(&locked);
        return false;
    }
    qa_fs_entry_kind kind;
    if (!status_wide(target, false, &kind, NULL, error, relative)) {
        free(target);
        writable_path_close(&locked);
        return false;
    }
    if (exclusive && kind != QA_FS_MISSING) {
        free(target); writable_path_close(&locked); *created = false; return true;
    }
    if (kind != QA_FS_MISSING && kind != QA_FS_REGULAR) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "write target is not a regular file: %s", relative);
        free(target);
        writable_path_close(&locked);
        return false;
    }
    wchar_t name[112];
    wchar_t *temporary = NULL;
    HANDLE handle = INVALID_HANDLE_VALUE;
    PSECURITY_DESCRIPTOR descriptor = private_file ? private_descriptor(error) : NULL;
    if (private_file && !descriptor) { free(target); writable_path_close(&locked); return false; }
    SECURITY_ATTRIBUTES attributes = {sizeof attributes, descriptor, FALSE};
    for (unsigned int attempt = 0; attempt < 64; ++attempt) {
        (void)swprintf(name, sizeof(name) / sizeof(name[0]),
                       L".qa-write-%lu-%llu-%u",
                       (unsigned long)GetCurrentProcessId(),
                       (unsigned long long)nonce, attempt);
        free(temporary);
        temporary = wide_child(locked.parent_path, name, error);
        if (temporary == NULL)
            break;
        if (CompareStringOrdinal(temporary, -1, target, -1, TRUE)
            == CSTR_EQUAL)
            continue;
        handle = CreateFileW(temporary, GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_DELETE,
                             private_file ? &attributes : NULL, CREATE_NEW,
                             FILE_ATTRIBUTE_TEMPORARY
                             | FILE_FLAG_OPEN_REPARSE_POINT,
                             NULL);
        if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS)
            break;
    }
    if (descriptor) LocalFree(descriptor);
    if (temporary == NULL) {
        free(target);
        writable_path_close(&locked);
        return false;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        free(temporary);
        free(target);
        writable_path_close(&locked);
        return fail_windows(error, "cannot create temporary file for",
                            relative, code);
    }
    wchar_t *opened = handle_path(handle, error);
    if (opened == NULL || !path_within(locked.root_path, opened)) {
        free(opened);
        CloseHandle(handle);
        DeleteFileW(temporary);
        free(temporary);
        free(target);
        writable_path_close(&locked);
        if (error == NULL || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_IO, 0,
                         "temporary file escapes writable root: %s", relative);
        return false;
    }
    free(opened);
    bool success = false;
    size_t offset = 0;
    while (offset < bytes.size) {
        size_t remaining = bytes.size - offset;
        DWORD request = remaining > UINT32_C(0x7ffff000)
            ? UINT32_C(0x7ffff000) : (DWORD)remaining;
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data + offset, request, &written, NULL)
            || written == 0) {
            fail_windows(error, "cannot write", relative, GetLastError());
            goto done;
        }
        offset += written;
    }
    if (!FlushFileBuffers(handle)) {
        fail_windows(error, "cannot sync", relative, GetLastError());
        goto done;
    }
    if (!CloseHandle(handle)) {
        handle = INVALID_HANDLE_VALUE;
        fail_windows(error, "cannot close", relative, GetLastError());
        goto done;
    }
    handle = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(temporary, target,
                     (exclusive ? 0 : MOVEFILE_REPLACE_EXISTING) | MOVEFILE_WRITE_THROUGH)) {
        DWORD code = GetLastError();
        if (exclusive && (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS)) {
            DeleteFileW(temporary); *created = false; success = true; goto done;
        }
        fail_windows(error, "cannot replace", relative, GetLastError());
        goto done;
    }
    *created = true; success = true;
done:
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    if (!success)
        DeleteFileW(temporary);
    free(temporary);
    free(target);
    writable_path_close(&locked);
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
    writable_path locked;
    if (!writable_parent(root, relative, false, &locked, error))
        return false;
    wchar_t *target = wide_child(locked.parent_path, locked.leaf, error);
    if (target == NULL) {
        writable_path_close(&locked);
        return false;
    }
    qa_fs_entry_kind kind;
    if (!status_wide(target, false, &kind, NULL, error, relative)) {
        free(target);
        writable_path_close(&locked);
        return false;
    }
    if (kind == QA_FS_MISSING) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0,
                     "remove target is missing: %s", relative);
        free(target);
        writable_path_close(&locked);
        return false;
    }
    if (kind != QA_FS_REGULAR) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "remove target is not a regular file: %s", relative);
        free(target);
        writable_path_close(&locked);
        return false;
    }
    bool ok = DeleteFileW(target) != 0;
    DWORD code = GetLastError();
    free(target);
    writable_path_close(&locked);
    return ok ? true : fail_windows(error, "cannot remove", relative, code);
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
        stream->handle = INVALID_HANDLE_VALUE;
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
    writable_path locked;
    if (!writable_parent(root, relative, !resume || append,
                         &locked, error)) {
        qa_fs_stream_close(stream);
        return false;
    }
    wchar_t *target = wide_child(locked.parent_path, locked.leaf, error);
    if (target == NULL) {
        writable_path_close(&locked);
        qa_fs_stream_close(stream);
        return false;
    }
    qa_fs_entry_kind existing;
    if (!status_wide(target, false, &existing, NULL, error, relative)) {
        free(target);
        writable_path_close(&locked);
        qa_fs_stream_close(stream);
        return false;
    }
    if (existing != QA_FS_MISSING && existing != QA_FS_REGULAR) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "writable file is not regular: %s", relative);
        free(target);
        writable_path_close(&locked);
        qa_fs_stream_close(stream);
        return false;
    }
    DWORD access = append ? FILE_APPEND_DATA | FILE_READ_ATTRIBUTES
                          : GENERIC_WRITE | FILE_READ_ATTRIBUTES;
    DWORD disposition = !resume || append ? OPEN_ALWAYS : OPEN_EXISTING;
    if (stage) *stage = QA_FS_STREAM_OPEN_NATIVE;
    HANDLE handle = CreateFileW(target, access,
                                FILE_SHARE_READ | FILE_SHARE_WRITE
                                | FILE_SHARE_DELETE,
                                NULL, disposition,
                                FILE_ATTRIBUTE_NORMAL
                                | FILE_FLAG_OPEN_REPARSE_POINT,
                                NULL);
    DWORD code = GetLastError();
    if (handle == INVALID_HANDLE_VALUE) {
        free(target);
        writable_path_close(&locked);
        qa_fs_stream_close(stream);
        return fail_windows(error, "cannot open writable file", relative, code);
    }
    bool created = disposition == OPEN_ALWAYS && code != ERROR_ALREADY_EXISTS;
    stream->handle = handle;
    if (stage) *stage = QA_FS_STREAM_OPEN_VALIDATE;
    wchar_t *opened = handle_path(handle, error);
    if (opened == NULL || !path_within(locked.root_path, opened)) {
        free(opened);
        qa_fs_stream_close(stream);
        if (created)
            (void)DeleteFileW(target);
        free(target);
        writable_path_close(&locked);
        if (error == NULL || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_IO, 0,
                         "writable file escapes its filesystem root: %s",
                         relative);
        return false;
    }
    free(opened);
    free(target);
    BY_HANDLE_FILE_INFORMATION legacy;
    FILE_BASIC_INFO basic;
    if (!handle_info(handle, &legacy, &basic, error, relative)
        || kind_from_attributes(legacy.dwFileAttributes) != QA_FS_REGULAR) {
        if (error == NULL || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_IO, 0,
                         "writable file is not regular: %s", relative);
        qa_fs_stream_close(stream);
        writable_path_close(&locked);
        return false;
    }
    if (!resume && mode == QA_FS_STREAM_WRITE) {
        if (stage) *stage = QA_FS_STREAM_OPEN_TRUNCATE;
        LARGE_INTEGER zero = {.QuadPart = 0};
        if (!SetFilePointerEx(handle, zero, NULL, FILE_BEGIN)
            || !SetEndOfFile(handle)) {
            code = GetLastError();
            qa_fs_stream_close(stream);
            writable_path_close(&locked);
            return fail_windows(error, "cannot truncate writable file",
                                relative, code);
        }
    }
    stream->mode = mode;
    stream->object = (qa_fs_object_reference){2, {legacy.dwVolumeSerialNumber,
        ((uint64_t)legacy.nFileIndexHigh << 32) | legacy.nFileIndexLow,
        ((uint64_t)legacy.ftCreationTime.dwHighDateTime << 32) | legacy.ftCreationTime.dwLowDateTime}};
    stream->root = root;
    qa_fs_root_retain(root);
    *initial_size = !resume && mode == QA_FS_STREAM_WRITE
        ? 0 : (((uint64_t)legacy.nFileSizeHigh << 32) | legacy.nFileSizeLow);
    *out = stream;
    writable_path_close(&locked);
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
    if (stream->mode == QA_FS_STREAM_WRITE) {
        LARGE_INTEGER offset = {.QuadPart = (LONGLONG)position};
        if (!SetFilePointerEx(stream->handle, offset, NULL, FILE_BEGIN))
            return fail_windows(error, "cannot seek writable file", stream->path, GetLastError());
    }
    DWORD request = bytes.size > UINT32_C(0x7ffff000) ? UINT32_C(0x7ffff000) : (DWORD)bytes.size;
    DWORD amount = 0;
    if (!WriteFile(stream->handle, bytes.data, request, &amount, NULL))
        return fail_windows(error, "cannot write", stream->path, GetLastError());
    *written = amount;
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
    LARGE_INTEGER size;
    if (!GetFileSizeEx(stream->handle, &size) || size.QuadPart < 0)
        return fail_windows(error, "cannot inspect writable file",
                            stream->path, GetLastError());
    *out = (uint64_t)size.QuadPart;
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
    if (stream->mode == QA_FS_STREAM_WRITE) {
        LARGE_INTEGER offset = {.QuadPart = (LONGLONG)position};
        if (!SetFilePointerEx(stream->handle, offset, NULL, FILE_BEGIN))
            return fail_windows(error, "cannot seek writable file",
                                stream->path, GetLastError());
    }
    size_t offset = 0;
    while (offset < bytes.size) {
        size_t remaining = bytes.size - offset;
        DWORD request = remaining > UINT32_C(0x7ffff000)
            ? UINT32_C(0x7ffff000) : (DWORD)remaining;
        DWORD amount = 0;
        if (!WriteFile(stream->handle, bytes.data + offset, request,
                       &amount, NULL) || amount == 0) {
            if (written != NULL)
                *written = offset;
            return fail_windows(error, "cannot write", stream->path,
                                GetLastError());
        }
        offset += amount;
        if (written != NULL)
            *written = offset;
    }
    if (stream->mode == QA_FS_STREAM_APPEND_SYNC
        && !FlushFileBuffers(stream->handle))
        return fail_windows(error, "cannot sync", stream->path,
                            GetLastError());
    return qa_fs_stream_size(stream, resulting_size, error);
}

bool qa_fs_stream_close_checked(qa_fs_stream *stream, qa_error *error)
{
    if (stream == NULL)
        return true;
    bool ok = true;
    if (stream->handle != INVALID_HANDLE_VALUE && !CloseHandle(stream->handle))
        ok = fail_windows(error, "cannot close", stream->path, GetLastError());
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
    HANDLE handle;
    writable_path locked;
    wchar_t *temporary, *target;
    char *display;
    bool sealed, published, readonly, closing, closing_keep, cleanup_done;
};
static bool stage_argument(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false;
}
static bool stage_identity(qa_fs_stage *stage, qa_fs_identity *out, qa_error *error) {
    BY_HANDLE_FILE_INFORMATION legacy, named;
    FILE_BASIC_INFO basic, named_basic;
    if (!handle_info(stage->handle, &legacy, &basic, error, stage->display)) return false;
    HANDLE check = CreateFileW(stage->temporary, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (check == INVALID_HANDLE_VALUE)
        return fail_windows(error, "cannot inspect stage name", stage->display, GetLastError());
    bool ok = handle_info(check, &named, &named_basic, error, stage->display);
    CloseHandle(check);
    if (!ok) return false;
    if (kind_from_attributes(legacy.dwFileAttributes) != QA_FS_REGULAR ||
        kind_from_attributes(named.dwFileAttributes) != QA_FS_REGULAR ||
        legacy.nNumberOfLinks != 1 || !same_handle_object(&legacy, &named)) {
        qa_error_set(error, QA_ERROR_IO, 0, "Staged file identity or link count changed: %s", stage->display); return false;
    }
    identity_from_info(&legacy, &basic, out); return true;
}
static bool stage_open(qa_fs_root *root, const char *target, uint64_t nonce, bool resume, bool readonly, bool checked,
                       qa_fs_stage **out, uint64_t *initial, bool *collision, qa_error *error) {
    if (collision) *collision = false;
    if (!root || !out || (checked && *out) || !initial || !qa_fs_relative_valid(target, false, error))
        return stage_argument(error, "Invalid contained staging request");
    qa_fs_stage *stage = calloc(1, sizeof(*stage));
    if (!stage) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating file stage"); return false; }
    if (checked) *out = stage;
    stage->handle = INVALID_HANDLE_VALUE; stage->display = copy_string(target);
    stage->readonly = readonly; stage->sealed = readonly;
    if (!stage->display || !writable_parent(root, target, !resume, &stage->locked, error)) {
        if (!stage->display) qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining stage target");
        if (!checked) qa_fs_stage_close(stage, true);
        return false;
    }
    wchar_t name[64];
    (void)swprintf(name, sizeof(name) / sizeof(name[0]), L".qa-stage-%016llx", (unsigned long long)nonce);
    stage->temporary = wide_child(stage->locked.parent_path, name, error);
    stage->target = wide_child(stage->locked.parent_path, stage->locked.leaf, error);
    if (!stage->temporary || !stage->target) { if (!checked) qa_fs_stage_close(stage, true); return false; }
    if (CompareStringOrdinal(stage->temporary, -1, stage->target, -1, TRUE) == CSTR_EQUAL) {
        if (!checked) qa_fs_stage_close(stage, true);
        return stage_argument(error, "Stage and target names must differ");
    }
    PSECURITY_DESCRIPTOR descriptor = resume ? NULL : private_descriptor(error);
    if (!resume && !descriptor) { if (!checked) qa_fs_stage_close(stage, true); return false; }
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), descriptor, FALSE};
    DWORD access = GENERIC_READ | FILE_READ_ATTRIBUTES;
    if (!readonly) access |= GENERIC_WRITE | DELETE;
    stage->handle = CreateFileW(stage->temporary, access,
        FILE_SHARE_READ | FILE_SHARE_DELETE, resume ? NULL : &attributes, resume ? OPEN_EXISTING : CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    DWORD code = GetLastError();
    if (descriptor) LocalFree(descriptor);
    if (stage->handle == INVALID_HANDLE_VALUE) {
        if (collision) *collision = !resume && (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS);
        if (!checked) qa_fs_stage_close(stage, true);
        return fail_windows(error, "cannot open staged file", target, code);
    }
    wchar_t *opened = handle_path(stage->handle, error);
    bool contained = opened && path_within(stage->locked.root_path, opened);
    free(opened);
    qa_fs_identity identity;
    if (!contained || !stage_identity(stage, &identity, error)
        || !private_handle(stage->handle, target, error)) {
        if (!contained && (!error || error->code == QA_OK)) stage_argument(error, "Stage escapes retained root");
        if (!checked) qa_fs_stage_close(stage, resume);
        return false;
    }
    *initial = identity.words[2]; *out = stage; return true;
}
bool qa_fs_stage_open(qa_fs_root *root, const char *target, uint64_t nonce, bool resume,
    qa_fs_stage **out, uint64_t *initial, qa_error *error) {
    return stage_open(root, target, nonce, resume, false, false, out, initial, NULL, error);
}
bool qa_fs_stage_open_readonly(qa_fs_root *root, const char *target, uint64_t nonce,
    qa_fs_stage **out, uint64_t *initial, qa_error *error) {
    return stage_open(root, target, nonce, true, true, false, out, initial, NULL, error);
}
bool qa_fs_stage_open_checked(qa_fs_root *root, const char *target, uint64_t nonce, bool resume,
    qa_fs_stage **out, uint64_t *initial, qa_error *error) {
    return stage_open(root, target, nonce, resume, false, true, out, initial, NULL, error);
}
bool qa_fs_stage_open_unique_checked(qa_fs_root *root, const char *target,
    uint64_t *namespace_nonce, uint64_t excluded_nonce, qa_fs_stage **out, uint64_t *initial, qa_error *error) {
    if (!namespace_nonce || !out || *out || !initial)
        return stage_argument(error, "Unique stage admission requires its retained namespace and empty owner");
    for (;;) {
        if (*namespace_nonce == UINT64_MAX)
            return stage_argument(error, "Native stage namespace is exhausted");
        ++*namespace_nonce;
        if (*namespace_nonce == excluded_nonce) continue;
        bool collision = false; qa_error attempt = {0};
        if (stage_open(root, target, *namespace_nonce, false, false, true, out, initial, &collision, &attempt))
            return true;
        if (!collision) { if (error) *error = attempt; return false; }
        if (!qa_fs_stage_close_checked(out, true, error)) return false;
    }
}
bool qa_fs_stage_size(qa_fs_stage *stage, uint64_t *out, qa_error *error) {
    if (!stage || stage->closing || !out) return stage_argument(error, "Missing stage size output");
    LARGE_INTEGER size;
    if (!GetFileSizeEx(stage->handle, &size) || size.QuadPart < 0)
        return fail_windows(error, "cannot inspect staged size", stage->display, GetLastError());
    *out = (uint64_t)size.QuadPart; return true;
}
bool qa_fs_stage_read(qa_fs_stage *stage, uint64_t offset, void *bytes, size_t capacity,
                       size_t *received, qa_error *error) {
    if (!stage || stage->closing || !received || (capacity && !bytes) || offset > INT64_MAX ||
        (uint64_t)capacity > (uint64_t)INT64_MAX - offset)
        return stage_argument(error, "Invalid stage read range");
    *received = 0;
    LARGE_INTEGER position = {.QuadPart = (LONGLONG)offset};
    if (!SetFilePointerEx(stage->handle, position, NULL, FILE_BEGIN))
        return fail_windows(error, "cannot seek staged file", stage->display, GetLastError());
    while (*received < capacity) {
        size_t remaining = capacity - *received;
        DWORD request = remaining > UINT32_C(0x7ffff000) ? UINT32_C(0x7ffff000) : (DWORD)remaining;
        DWORD count = 0;
        if (!ReadFile(stage->handle, (uint8_t *)bytes + *received, request, &count, NULL))
            return fail_windows(error, "cannot read staged file", stage->display, GetLastError());
        if (!count) break;
        *received += count;
    }
    return true;
}
bool qa_fs_stage_write(qa_fs_stage *stage, uint64_t offset, qa_bytes bytes, size_t *written, qa_error *error) {
    if (!stage || stage->closing || !written || stage->sealed || stage->published || (bytes.size && !bytes.data) ||
        offset > INT64_MAX || (uint64_t)bytes.size > (uint64_t)INT64_MAX - offset)
        return stage_argument(error, "Invalid stage write range/state");
    *written = 0;
    LARGE_INTEGER position = {.QuadPart = (LONGLONG)offset};
    if (!SetFilePointerEx(stage->handle, position, NULL, FILE_BEGIN))
        return fail_windows(error, "cannot seek staged file", stage->display, GetLastError());
    while (*written < bytes.size) {
        size_t remaining = bytes.size - *written;
        DWORD request = remaining > UINT32_C(0x7ffff000) ? UINT32_C(0x7ffff000) : (DWORD)remaining;
        DWORD count = 0;
        if (!WriteFile(stage->handle, bytes.data + *written, request, &count, NULL) || !count)
            return fail_windows(error, "cannot write staged file", stage->display, GetLastError());
        *written += count;
    }
    return true;
}
bool qa_fs_stage_seal(qa_fs_stage *stage, qa_fs_identity *identity, qa_error *error) {
    if (!stage || stage->closing || !identity || stage->published || stage->readonly) return stage_argument(error, "Invalid stage seal");
    if (!FlushFileBuffers(stage->handle)) return fail_windows(error, "cannot sync staged file", stage->display, GetLastError());
    if (!stage_identity(stage, identity, error)) return false;
    stage->sealed = true; return true;
}
struct qa_fs_stage_mapping { qa_bytes bytes; HANDLE mapping; };
qa_bytes qa_fs_stage_mapping_bytes(const qa_fs_stage_mapping *mapping) {
    return mapping ? mapping->bytes : (qa_bytes){0};
}
void qa_fs_stage_unmap(qa_fs_stage_mapping *mapping) {
    if (!mapping) return;
    if (mapping->bytes.data) UnmapViewOfFile(mapping->bytes.data);
    if (mapping->mapping) CloseHandle(mapping->mapping);
    free(mapping);
}
bool qa_fs_stage_map(qa_fs_stage *stage, qa_fs_stage_mapping **out, qa_error *error) {
    qa_fs_identity identity;
    if (!stage || stage->closing || !out || !stage->sealed || stage->published) return stage_argument(error, "Stage mapping requires a sealed unpublished file");
    if (!stage_identity(stage, &identity, error)) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(stage->handle, &size)) return fail_windows(error, "cannot inspect staged mapping size", stage->display, GetLastError());
    if (size.QuadPart < 0 || (uint64_t)size.QuadPart > SIZE_MAX || (uint64_t)size.QuadPart > PTRDIFF_MAX)
        return stage_argument(error, "Stage exceeds mapping address range");
    qa_fs_stage_mapping *mapping = calloc(1, sizeof(*mapping));
    if (!mapping) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating stage inspection mapping"); return false; }
    if (size.QuadPart) {
        mapping->mapping = CreateFileMappingW(stage->handle, NULL, PAGE_READONLY, 0, 0, NULL);
        if (mapping->mapping) mapping->bytes.data = MapViewOfFile(mapping->mapping, FILE_MAP_READ, 0, 0, (SIZE_T)size.QuadPart);
        if (!mapping->mapping || !mapping->bytes.data) { DWORD code = GetLastError(); qa_fs_stage_unmap(mapping); return fail_windows(error, "cannot map staged file", stage->display, code); }
        mapping->bytes.size = (size_t)size.QuadPart;
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
    qa_fs_identity identity;
    if (!stage_identity(stage, &identity, error)) return false;
    if (!qa_fs_identity_equal(expected, &identity)) {
        qa_error_set(error, QA_ERROR_IO, 0, "Staged file changed after inspection"); return false;
    }
    qa_fs_entry_kind kind;
    if (!status_wide(stage->target, false, &kind, NULL, error, stage->display)) return false;
    if (exclusive && kind != QA_FS_MISSING) return true;
    if (!exclusive && kind != QA_FS_MISSING && kind != QA_FS_REGULAR)
        return stage_argument(error, "Stage target is not regular");
    size_t characters = wcslen(stage->target);
    if (characters > (SIZE_MAX - sizeof(FILE_RENAME_INFO)) / sizeof(wchar_t) ||
        characters > UINT32_MAX / sizeof(wchar_t)) return stage_argument(error, "Stage target path too long");
    size_t size = sizeof(FILE_RENAME_INFO) + characters * sizeof(wchar_t);
    if (size > UINT32_MAX) return stage_argument(error, "Stage rename exceeds system limit");
    FILE_RENAME_INFO *rename = calloc(1, size);
    if (!rename) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating stage rename"); return false; }
    rename->ReplaceIfExists = !exclusive;
    rename->FileNameLength = (DWORD)(characters * sizeof(wchar_t));
    memcpy(rename->FileName, stage->target, rename->FileNameLength);
    bool ok = SetFileInformationByHandle(stage->handle, FileRenameInfo, rename, (DWORD)size) != 0;
    DWORD code = GetLastError(); free(rename);
    if (!ok) {
        if (exclusive && (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS)) return true;
        return fail_windows(error, "cannot publish staged file", stage->display, code);
    }
    stage->published = true; *created = true;
    if (!FlushFileBuffers(stage->handle)) return fail_windows(error, "cannot sync published stage", stage->display, GetLastError());
    return true;
}
void qa_fs_stage_close(qa_fs_stage *stage, bool keep) {
    if (!stage) return;
    if (stage->handle != INVALID_HANDLE_VALUE) {
        if (!keep && !stage->published && !stage->readonly) {
            FILE_DISPOSITION_INFO disposition = {TRUE};
            (void)SetFileInformationByHandle(stage->handle, FileDispositionInfo, &disposition, sizeof(disposition));
        } else if (keep && !stage->published && !stage->readonly) (void)FlushFileBuffers(stage->handle);
        CloseHandle(stage->handle);
    }
    writable_path_close(&stage->locked); free(stage->temporary); free(stage->target); free(stage->display); free(stage);
}
bool qa_fs_stage_close_checked(qa_fs_stage **owned, bool keep, qa_error *error) {
    qa_fs_stage *stage = owned ? *owned : NULL;
    if (!stage) return true;
    if (stage->closing && stage->closing_keep != keep)
        return stage_argument(error, "Stage cleanup cannot change its retained-file decision");
    stage->closing = true; stage->closing_keep = keep;
    if (!stage->cleanup_done) {
        if (stage->handle != INVALID_HANDLE_VALUE && !stage->published && !stage->readonly) {
            if (keep) {
                if (!FlushFileBuffers(stage->handle))
                    return fail_windows(error, "cannot sync retained stage", stage->display, GetLastError());
            } else {
                FILE_DISPOSITION_INFO disposition = {TRUE};
                if (!SetFileInformationByHandle(stage->handle, FileDispositionInfo, &disposition, sizeof(disposition)))
                    return fail_windows(error, "cannot remove closing stage", stage->display, GetLastError());
            }
        }
        stage->cleanup_done = true;
    }
    if (stage->handle != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(stage->handle)) return fail_windows(error, "cannot close stage", stage->display, GetLastError());
        stage->handle = INVALID_HANDLE_VALUE;
    }
    while (stage->locked.directory_count) {
        HANDLE directory = stage->locked.directories[stage->locked.directory_count - 1];
        if (!CloseHandle(directory)) return fail_windows(error, "cannot close stage parent", stage->display, GetLastError());
        --stage->locked.directory_count;
    }
    writable_path_close(&stage->locked); free(stage->temporary); free(stage->target); free(stage->display); free(stage);
    *owned = NULL; return true;
}

bool qa_fs_stream_sync(qa_fs_stream *stream, qa_error *error) {
    if (!stream) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing writable stream"); return false; }
    return FlushFileBuffers(stream->handle) != 0 || fail_windows(error, "cannot sync writable stream", stream->path, GetLastError());
}

#include "filesystem_opened_windows.inc"
