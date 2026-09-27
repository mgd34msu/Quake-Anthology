#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include "qa/common.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct qa_file_mapping {
    qa_bytes bytes;
#if defined(_WIN32)
    HANDLE file, mapping;
#endif
};

qa_bytes qa_file_mapping_bytes(const qa_file_mapping *mapping) {
    return mapping ? mapping->bytes : (qa_bytes){0};
}

void qa_file_mapping_close(qa_file_mapping *mapping) {
    if (!mapping)
        return;
#if defined(_WIN32)
    if (mapping->bytes.data)
        UnmapViewOfFile(mapping->bytes.data);
    if (mapping->mapping)
        CloseHandle(mapping->mapping);
    if (mapping->file && mapping->file != INVALID_HANDLE_VALUE)
        CloseHandle(mapping->file);
#else
    if (mapping->bytes.data)
        munmap((void *)mapping->bytes.data, mapping->bytes.size);
#endif
    free(mapping);
}

bool qa_file_map(const char *path, qa_file_mapping **out, qa_error *error) {
    if (!path || !*path || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "file mapping requires a path and output");
        return false;
    }
    qa_file_mapping *mapping = calloc(1, sizeof(*mapping));
    if (!mapping) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating file mapping");
        return false;
    }
#if defined(_WIN32)
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!length || (size_t)length > SIZE_MAX / sizeof(wchar_t)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid UTF-8 file path");
        goto fail;
    }
    wchar_t *wide = malloc((size_t)length * sizeof(*wide));
    if (!wide) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating UTF-16 file path");
        goto fail;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, length)) {
        free(wide);
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "converting UTF-8 file path");
        goto fail;
    }
    mapping->file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD failure = GetLastError();
    free(wide);
    if (mapping->file == INVALID_HANDLE_VALUE) {
        qa_error_set(error,
                     failure == ERROR_FILE_NOT_FOUND || failure == ERROR_PATH_NOT_FOUND
                         ? QA_ERROR_NOT_FOUND
                         : QA_ERROR_IO,
                     0, "cannot open %s (Windows error %lu)", path, (unsigned long)failure);
        goto fail;
    }
    LARGE_INTEGER length_bytes;
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileType(mapping->file) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(mapping->file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        !GetFileSizeEx(mapping->file, &length_bytes)) {
        qa_error_set(error, QA_ERROR_IO, 0, "cannot map non-regular file %s", path);
        goto fail;
    }
    if (length_bytes.QuadPart < 0 || (uint64_t)length_bytes.QuadPart > PTRDIFF_MAX ||
        (uint64_t)length_bytes.QuadPart > SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "file is too large to map: %s", path);
        goto fail;
    }
    mapping->bytes.size = (size_t)length_bytes.QuadPart;
    if (mapping->bytes.size) {
        mapping->mapping = CreateFileMappingW(mapping->file, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!mapping->mapping) {
            qa_error_set(error, QA_ERROR_IO, 0, "cannot create file mapping for %s", path);
            goto fail;
        }
        mapping->bytes.data = MapViewOfFile(mapping->mapping, FILE_MAP_READ, 0, 0, 0);
        if (!mapping->bytes.data) {
            qa_error_set(error, QA_ERROR_IO, 0, "cannot map file view for %s", path);
            goto fail;
        }
    }
#else
    int descriptor;
    do {
        descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        int code = errno;
        qa_error_set(error, code == ENOENT || code == ENOTDIR ? QA_ERROR_NOT_FOUND : QA_ERROR_IO, 0,
                     "cannot open %s: %s", path, strerror(code));
        goto fail;
    }
    struct stat info;
    int status;
    do {
        status = fstat(descriptor, &info);
    } while (status < 0 && errno == EINTR);
    if (status < 0 || !S_ISREG(info.st_mode)) {
        close(descriptor);
        qa_error_set(error, QA_ERROR_IO, 0, "cannot map non-regular file %s", path);
        goto fail;
    }
    if (info.st_size < 0 || (uintmax_t)info.st_size > PTRDIFF_MAX ||
        (uintmax_t)info.st_size > SIZE_MAX) {
        close(descriptor);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "file is too large to map: %s", path);
        goto fail;
    }
    mapping->bytes.size = (size_t)info.st_size;
    void *bytes = NULL;
    if (mapping->bytes.size)
        bytes = mmap(NULL, mapping->bytes.size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    int code = errno;
    close(descriptor);
    if (bytes == MAP_FAILED) {
        qa_error_set(error, QA_ERROR_IO, 0, "cannot map %s: %s", path, strerror(code));
        goto fail;
    }
    mapping->bytes.data = bytes;
#endif
    *out = mapping;
    return true;
fail:
    qa_file_mapping_close(mapping);
    return false;
}
