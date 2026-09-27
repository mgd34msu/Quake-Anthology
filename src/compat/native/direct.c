#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "internal.h"

#include <errno.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <link.h>
#include <sys/uio.h>
#endif
#include <dirent.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#endif

static bool same_target(qa_native_target left, qa_native_target right) {
    return left.os == right.os && left.arch == right.arch && left.abi == right.abi &&
           left.pointer_bytes == right.pointer_bytes;
}

static const char *image_suffix(qa_native_image_format format) {
    return format == QA_NATIVE_IMAGE_PE32 || format == QA_NATIVE_IMAGE_PE32_PLUS ? ".dll" : ".so";
}

static bool safe_file_name(const char *name) {
    if (!name || !name[0] || !strcmp(name, ".") || !strcmp(name, ".."))
        return false;
    for (const char *cursor = name; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':')
            return false;
    return true;
}

static const char *artifact_name(const qa_native_module *module) {
    const char *name = module->source;
    for (const char *cursor = module->source; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\')
            name = cursor + 1;
    return safe_file_name(name) ? name : NULL;
}

#if defined(_WIN32)
static bool windows_error(qa_error *error, qa_status code, const char *operation) {
    DWORD number = GetLastError();
    char message[160] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, number, 0,
                   message, (DWORD)sizeof(message), NULL);
    qa_error_set(error, code, number, "%s: %s", operation, message[0] ? message : "Windows error");
    return false;
}

bool native_temp_directory(char **path, qa_error *error) {
    char root[MAX_PATH + 1];
    DWORD root_length = GetTempPathA(MAX_PATH, root);
    if (!root_length || root_length > MAX_PATH)
        return windows_error(error, QA_ERROR_IO, "finding temporary directory");
    char temporary[MAX_PATH + 1];
    if (!GetTempFileNameA(root, "qan", 0, temporary))
        return windows_error(error, QA_ERROR_IO, "reserving temporary path");
    DeleteFileA(temporary);
    if (!CreateDirectoryA(temporary, NULL))
        return windows_error(error, QA_ERROR_IO, "creating temporary directory");
    *path = native_strdup(temporary, error);
    if (!*path)
        RemoveDirectoryA(temporary);
    return *path != NULL;
}

static bool write_handle(HANDLE handle, qa_bytes bytes, qa_error *error) {
    size_t offset = 0;
    while (offset < bytes.size) {
        DWORD amount = bytes.size - offset > UINT32_MAX ? UINT32_MAX : (DWORD)(bytes.size - offset);
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data + offset, amount, &written, NULL) || written != amount)
            return windows_error(error, QA_ERROR_IO, "writing temporary file");
        offset += written;
    }
    return true;
}

bool native_temp_file(const char *directory, const char *name, qa_bytes bytes, char **path,
                      qa_error *error) {
    size_t size;
    if (!native_size_add(strlen(directory), strlen(name) + 2u, &size))
        return native_fail(error, QA_ERROR_MEMORY, 0, "temporary native path length overflow");
    char *joined = malloc(size);
    if (!joined)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating temporary native path");
    snprintf(joined, size, "%s\\%s", directory, name);
    HANDLE handle =
        CreateFileA(joined, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        free(joined);
        return windows_error(error, QA_ERROR_IO, "creating temporary native file");
    }
    bool ok = write_handle(handle, bytes, error);
    if (!CloseHandle(handle) && ok)
        ok = windows_error(error, QA_ERROR_IO, "closing temporary native file");
    if (!ok) {
        DeleteFileA(joined);
        free(joined);
        return false;
    }
    *path = joined;
    return true;
}

bool native_write_file(const char *path, qa_bytes bytes, qa_error *error) {
    HANDLE handle =
        CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return windows_error(error, QA_ERROR_IO, "creating native state file");
    bool ok = write_handle(handle, bytes, error);
    if (!CloseHandle(handle) && ok)
        ok = windows_error(error, QA_ERROR_IO, "closing native state file");
    return ok;
}

void native_remove_tree(const char *path) {
    if (!path)
        return;
    char pattern[MAX_PATH + 3];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(pattern, &data);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, ".."))
                continue;
            char member[MAX_PATH + 1];
            snprintf(member, sizeof(member), "%s\\%s", path, data.cFileName);
            DeleteFileA(member);
        } while (FindNextFileA(search, &data));
        FindClose(search);
    }
    RemoveDirectoryA(path);
}
#else
static bool posix_error(qa_error *error, qa_status code, const char *operation) {
    qa_error_set(error, code, (size_t)errno, "%s: %s", operation, strerror(errno));
    return false;
}

bool native_temp_directory(char **path, qa_error *error) {
    char pattern[] = "/tmp/qa-native-XXXXXX";
    if (!mkdtemp(pattern))
        return posix_error(error, QA_ERROR_IO, "creating temporary directory");
    *path = native_strdup(pattern, error);
    if (!*path)
        rmdir(pattern);
    return *path != NULL;
}

static bool write_descriptor(int descriptor, qa_bytes bytes, qa_error *error) {
    size_t offset = 0;
    while (offset < bytes.size) {
        ssize_t written = write(descriptor, bytes.data + offset, bytes.size - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return posix_error(error, QA_ERROR_IO, "writing temporary file");
        offset += (size_t)written;
    }
    return true;
}

static bool join_path(const char *directory, const char *name, char **out, qa_error *error) {
    size_t size;
    if (!native_size_add(strlen(directory), strlen(name) + 2u, &size))
        return native_fail(error, QA_ERROR_MEMORY, 0, "temporary native path length overflow");
    char *joined = malloc(size);
    if (!joined)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating temporary native path");
    snprintf(joined, size, "%s/%s", directory, name);
    *out = joined;
    return true;
}

bool native_temp_file(const char *directory, const char *name, qa_bytes bytes, char **path,
                      qa_error *error) {
    char *joined = NULL;
    if (!join_path(directory, name, &joined, error))
        return false;
    int descriptor = open(joined, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700);
    if (descriptor < 0) {
        free(joined);
        return posix_error(error, QA_ERROR_IO, "creating temporary native file");
    }
    bool ok = write_descriptor(descriptor, bytes, error);
    if (close(descriptor) && ok)
        ok = posix_error(error, QA_ERROR_IO, "closing temporary native file");
    if (!ok) {
        unlink(joined);
        free(joined);
        return false;
    }
    *path = joined;
    return true;
}

bool native_write_file(const char *path, qa_bytes bytes, qa_error *error) {
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (descriptor < 0)
        return posix_error(error, QA_ERROR_IO, "creating native state file");
    bool ok = write_descriptor(descriptor, bytes, error);
    if (close(descriptor) && ok)
        ok = posix_error(error, QA_ERROR_IO, "closing native state file");
    return ok;
}

void native_remove_tree(const char *path) {
    if (!path)
        return;
    DIR *directory = opendir(path);
    if (directory) {
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            size_t size = strlen(path) + strlen(entry->d_name) + 2u;
            char *member = malloc(size);
            if (!member)
                continue;
            snprintf(member, size, "%s/%s", path, entry->d_name);
            unlink(member);
            free(member);
        }
        closedir(directory);
    }
    rmdir(path);
}
#endif

bool native_read_file(const char *path, qa_buffer *out, qa_error *error) {
    return qa_file_read_all(path, out, error);
}

bool native_direct_open(qa_native_instance *instance, qa_error *error) {
    qa_native_target host = qa_native_host_target();
    qa_native_image_info image = instance->module->info.image;
    if (!same_target(host, image.target)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "native artifact target %u/%u/%u cannot execute directly on "
                     "host %u/%u/%u",
                     image.target.os, image.target.arch, image.target.abi, host.os, host.arch,
                     host.abi);
        return false;
    }
    const char *name = artifact_name(instance->module);
    char fallback[32];
    if (!name) {
        snprintf(fallback, sizeof(fallback), "module%s", image_suffix(image.format));
        name = fallback;
    }
    if (!native_temp_directory(&instance->materialized_directory, error))
        return false;
    for (size_t index = 0; index < instance->options.dependency_count; ++index) {
        const qa_native_dependency *dependency = &instance->options.dependencies[index];
        if (!safe_file_name(dependency->path) ||
            (!dependency->bytes.data && dependency->bytes.size) ||
            !strcmp(dependency->path, name)) {
            native_fail(error, QA_ERROR_ARGUMENT, index,
                        "native dependency must have a distinct safe file name");
            native_remove_tree(instance->materialized_directory);
            free(instance->materialized_directory);
            instance->materialized_directory = NULL;
            return false;
        }
        for (size_t previous = 0; previous < index; ++previous) {
            if (!strcmp(instance->options.dependencies[previous].path, dependency->path)) {
                native_fail(error, QA_ERROR_ARGUMENT, index,
                            "native dependency file name is duplicated");
                native_remove_tree(instance->materialized_directory);
                free(instance->materialized_directory);
                instance->materialized_directory = NULL;
                return false;
            }
        }
        char *dependency_path = NULL;
        if (!native_temp_file(instance->materialized_directory, dependency->path, dependency->bytes,
                              &dependency_path, error)) {
            native_remove_tree(instance->materialized_directory);
            free(instance->materialized_directory);
            instance->materialized_directory = NULL;
            return false;
        }
        free(dependency_path);
    }
    qa_bytes bytes = {instance->module->bytes, instance->module->size};
    if (!native_temp_file(instance->materialized_directory, name, bytes,
                          &instance->materialized_path, error)) {
        native_remove_tree(instance->materialized_directory);
        free(instance->materialized_directory);
        instance->materialized_directory = NULL;
        return false;
    }
#if defined(_WIN32)
    HMODULE handle =
        LoadLibraryExA(instance->materialized_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!handle) {
        windows_error(error, QA_ERROR_IO, "loading native module");
        native_direct_close(instance);
        return false;
    }
    instance->loader_handle = handle;
    instance->image_base = (qa_native_address)(uintptr_t)handle;
#else
    void *handle = dlopen(instance->materialized_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char *loader_error = dlerror();
        qa_error_set(error, QA_ERROR_IO, 0, "loading native module: %s",
                     loader_error ? loader_error : "dynamic loader error");
        native_direct_close(instance);
        return false;
    }
    instance->loader_handle = handle;
#if defined(__linux__) && defined(RTLD_DI_LINKMAP)
    struct link_map *mapping = NULL;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &mapping) || !mapping) {
        qa_error_set(error, QA_ERROR_IO, 0, "dynamic loader did not report the native image base");
        native_direct_close(instance);
        return false;
    }
    instance->image_base = (qa_native_address)(uintptr_t)mapping->l_addr;
#else
    instance->image_base = 0;
#endif
#endif
    instance->image_bytes = image.image_bytes;
    return true;
}

void native_direct_close(qa_native_instance *instance) {
    if (!instance)
        return;
    if (instance->loader_handle) {
#if defined(_WIN32)
        FreeLibrary((HMODULE)instance->loader_handle);
#else
        dlclose(instance->loader_handle);
#endif
        instance->loader_handle = NULL;
    }
    if (instance->materialized_directory)
        native_remove_tree(instance->materialized_directory);
    free(instance->materialized_path);
    free(instance->materialized_directory);
    instance->materialized_path = NULL;
    instance->materialized_directory = NULL;
    instance->image_base = 0;
}

bool native_direct_export(const qa_native_instance *instance, const char *name,
                          qa_native_address *out, qa_error *error) {
    if (!instance || !instance->loader_handle || !name || !out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "loaded native instance, export and output are required");
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress((HMODULE)instance->loader_handle, name);
    if (!symbol) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, GetLastError(), "native export %s was not found",
                     name);
        return false;
    }
    *out = (qa_native_address)(uintptr_t)symbol;
#else
    dlerror();
    void *symbol = dlsym(instance->loader_handle, name);
    const char *loader_error = dlerror();
    if (loader_error || !symbol) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "native export %s was not found: %s", name,
                     loader_error ? loader_error : "null symbol");
        return false;
    }
    *out = (qa_native_address)(uintptr_t)symbol;
#endif
    return true;
}

bool native_direct_read(qa_native_address address, void *out, size_t bytes, qa_error *error) {
    if ((!out && bytes) || (!address && bytes))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "valid native read address and output are required");
    if (bytes > UINT64_MAX - address)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native read address range overflows");
    if (!bytes)
        return true;
#if defined(_WIN32)
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)address, out, bytes,
                           &read) ||
        read != bytes)
        return windows_error(error, QA_ERROR_ARGUMENT, "reading native module memory");
    return true;
#elif defined(__linux__)
    size_t offset = 0;
    while (offset < bytes) {
        struct iovec local = {(uint8_t *)out + offset, bytes - offset};
        struct iovec remote = {(void *)(uintptr_t)(address + offset), bytes - offset};
        ssize_t read = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
        if (read < 0 && errno == EINTR)
            continue;
        if (read <= 0)
            return posix_error(error, QA_ERROR_ARGUMENT, "reading native module memory");
        offset += (size_t)read;
    }
    return true;
#else
    memcpy(out, (const void *)(uintptr_t)address, bytes);
    return true;
#endif
}

bool native_direct_write(qa_native_address address, const void *bytes, size_t size,
                         qa_error *error) {
    if ((!bytes && size) || (!address && size))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "valid native write address and bytes are required");
    if (size > UINT64_MAX - address)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native write address range overflows");
    if (!size)
        return true;
#if defined(_WIN32)
    SIZE_T written = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)address, bytes, size,
                            &written) ||
        written != size)
        return windows_error(error, QA_ERROR_ARGUMENT, "writing native module memory");
    return true;
#elif defined(__linux__)
    size_t offset = 0;
    while (offset < size) {
        struct iovec local = {(void *)((const uint8_t *)bytes + offset), size - offset};
        struct iovec remote = {(void *)(uintptr_t)(address + offset), size - offset};
        ssize_t written = process_vm_writev(getpid(), &local, 1, &remote, 1, 0);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return posix_error(error, QA_ERROR_ARGUMENT, "writing native module memory");
        offset += (size_t)written;
    }
    return true;
#else
    memcpy((void *)(uintptr_t)address, bytes, size);
    return true;
#endif
}
