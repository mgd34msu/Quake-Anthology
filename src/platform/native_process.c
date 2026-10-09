#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#include "qa/native_process_platform.h"
#include "qa/platform_services.h"
#include "qa/text.h"
#include "qa/source_save.h"
#include "qa/native_windows_locale_save.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#include <wchar.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <locale.h>
#include <langinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#if defined(__linux__)
#include <sys/utsname.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif
#endif

typedef struct platform_handle { size_t references; } platform_handle;
typedef struct platform_stream {
    struct qa_native_process_platform *owner;
    uint64_t id;
    int descriptor;
    uint32_t mode;
    uint64_t device, object;
    bool closed, durable_object;
    platform_handle *held;
} platform_stream;
struct qa_native_process_platform {
    uint64_t id;
    size_t references, busy;
    int64_t frequency;
    qa_native_windows_locale_profile locale;
    char *locale_name;
    uint32_t locale_input;
    platform_stream streams[3];
    bool closing, failed, terminal;
};
static _Thread_local const qa_native_process_platform *native_error_owner;
static _Thread_local qa_fs_native_error native_error_value;
static _Thread_local uint64_t native_error_operation;
static void platform_operation_begin(const qa_native_process_platform *owner)
{ native_error_owner = owner; native_error_value = (qa_fs_native_error){0}; ++native_error_operation; }
bool qa_native_process_platform_native_error_read(const qa_native_process_platform *owner,
    qa_fs_native_error *out, uint64_t *operation)
{
    if (!owner || !out || !operation) return false;
    *out = native_error_owner == owner ? native_error_value : (qa_fs_native_error){0};
    *operation = native_error_owner == owner ? native_error_operation : 0; return true;
}
static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
void qa_native_process_environment_dispose(qa_native_process_environment *environment)
{
    if (!environment) return;
    for (size_t i = 0; i < environment->count; ++i) free(environment->values[i]);
    free(environment->values);
    free(environment->windows_values);
    free(environment->executable);
    free(environment->executable_directory);
    for (size_t i = 0; i < environment->root_count; ++i) free(environment->root_paths[i]);
    free(environment->root_paths);
    *environment = (qa_native_process_environment){0};
}
#if defined(_WIN32)
static char *process_utf8(const wchar_t *wide, qa_error *error)
{
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    if (count <= 0) {
        qa_error_set(error, QA_ERROR_IO, GetLastError(), "Converting inherited process text to UTF-8");
        return NULL;
    }
    char *text = malloc((size_t)count);
    if (!text) { fail(error, QA_ERROR_MEMORY, "Owning inherited process text"); return NULL; }
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, text, count, NULL, NULL)) {
        qa_error_set(error, QA_ERROR_IO, GetLastError(), "Converting inherited process text to UTF-8");
        free(text); return NULL;
    }
    return text;
}
static bool process_drive_roots(qa_native_process_environment *out, qa_error *error)
{
    DWORD capacity = GetLogicalDriveStringsW(0, NULL);
    if (!capacity) {
        qa_error_set(error, QA_ERROR_IO, GetLastError(), "Acquiring logical filesystem roots");
        return false;
    }
    for (;;) {
        size_t bytes = (size_t)capacity * sizeof(wchar_t);
        if (bytes / sizeof(wchar_t) != capacity)
            return fail(error, QA_ERROR_MEMORY, "Logical filesystem roots exceed native storage");
        wchar_t *paths = malloc(bytes);
        if (!paths) return fail(error, QA_ERROR_MEMORY, "Owning logical filesystem roots");
        DWORD length = GetLogicalDriveStringsW(capacity, paths);
        if (!length) {
            DWORD code = GetLastError(); free(paths);
            qa_error_set(error, QA_ERROR_IO, code, "Acquiring logical filesystem roots");
            return false;
        }
        if (length >= capacity) { free(paths); capacity = length; continue; }
        size_t count = 0;
        for (const wchar_t *path = paths; *path; path += wcslen(path) + 1) ++count;
        bool okay = count < SIZE_MAX / sizeof(*out->root_paths);
        if (okay) out->root_paths = calloc(count + 1, sizeof(*out->root_paths));
        if (!okay || !out->root_paths) {
            free(paths);
            return fail(error, QA_ERROR_MEMORY, "Owning logical filesystem root paths");
        }
        out->root_count = count;
        const wchar_t *path = paths;
        for (size_t i = 0; i < count; ++i, path += wcslen(path) + 1) {
            out->root_paths[i] = process_utf8(path, error);
            if (!out->root_paths[i]) { free(paths); return false; }
        }
        free(paths); return true;
    }
}
#endif
#if !defined(_WIN32)
static bool process_windows_environment(qa_native_process_environment *out, qa_error *error)
{
    size_t units = out->count ? 1 : 2;
    for (size_t i = 0; i < out->count; ++i) {
        qa_bytes text = {(const uint8_t *)out->values[i], strlen(out->values[i])};
        size_t cursor = 0; uint32_t scalar;
        while (qa_utf8_next(text, &cursor, &scalar)) {
            size_t added = scalar > 0xffff ? 2 : 1;
            if (units > SIZE_MAX - added)
                return fail(error, QA_ERROR_MEMORY, "Inherited environment exceeds UTF-16 storage");
            units += added;
        }
        if (units == SIZE_MAX)
            return fail(error, QA_ERROR_MEMORY, "Inherited environment exceeds UTF-16 storage");
        ++units;
    }
    if (units > SIZE_MAX / sizeof(*out->windows_values))
        return fail(error, QA_ERROR_MEMORY, "Inherited environment exceeds UTF-16 storage");
    out->windows_values = calloc(units, sizeof(*out->windows_values));
    if (!out->windows_values)
        return fail(error, QA_ERROR_MEMORY, "Owning inherited Windows environment");
    out->windows_units = units;
    size_t written = 0;
    for (size_t i = 0; i < out->count; ++i) {
        qa_bytes text = {(const uint8_t *)out->values[i], strlen(out->values[i])};
        size_t cursor = 0; uint32_t scalar;
        while (qa_utf8_next(text, &cursor, &scalar)) {
            if (scalar > 0xffff) {
                scalar -= 0x10000;
                out->windows_values[written++] = (uint16_t)(0xd800 + (scalar >> 10));
                out->windows_values[written++] = (uint16_t)(0xdc00 + (scalar & 0x3ff));
            } else out->windows_values[written++] = (uint16_t)scalar;
        }
        ++written;
    }
    return true;
}
#endif
static bool process_environment_values(qa_native_process_environment *out, qa_error *error)
{
#if defined(_WIN32)
    wchar_t *native = GetEnvironmentStringsW();
    if (!native) {
        qa_error_set(error, QA_ERROR_IO, GetLastError(), "Acquiring inherited process environment");
        return false;
    }
    size_t count = 0, units = 1;
    for (const wchar_t *entry = native; *entry; entry += wcslen(entry) + 1) {
        ++count; units += wcslen(entry) + 1;
    }
    if (!count) units = 2;
    if (units > SIZE_MAX / sizeof(*out->windows_values)) {
        FreeEnvironmentStringsW(native);
        return fail(error, QA_ERROR_MEMORY, "Inherited environment exceeds UTF-16 storage");
    }
    out->windows_values = malloc(units * sizeof(*out->windows_values));
    if (!out->windows_values) {
        FreeEnvironmentStringsW(native);
        return fail(error, QA_ERROR_MEMORY, "Owning inherited Windows environment");
    }
    memcpy(out->windows_values, native, units * sizeof(*out->windows_values));
    out->windows_units = units;
#else
#if defined(__APPLE__)
    char **native = *_NSGetEnviron();
#else
    extern char **environ;
    char **native = environ;
#endif
    size_t count = 0;
    while (native && native[count]) ++count;
#endif
    bool okay = count < SIZE_MAX / sizeof(*out->values);
    if (okay) out->values = calloc(count + 1, sizeof(*out->values));
    if (!okay || !out->values) {
        fail(error, QA_ERROR_MEMORY, "Owning inherited process environment"); okay = false;
    } else {
        out->count = count;
#if defined(_WIN32)
        const wchar_t *entry = native;
        for (size_t i = 0; i < count; ++i, entry += wcslen(entry) + 1) {
            out->values[i] = process_utf8(entry, error);
            if (!out->values[i]) { okay = false; break; }
        }
#else
        for (size_t i = 0; i < count; ++i) {
            out->values[i] = strdup(native[i]);
            if (!out->values[i]) {
                fail(error, QA_ERROR_MEMORY, "Owning inherited process environment entry");
                okay = false; break;
            }
        }
#endif
    }
#if defined(_WIN32)
    FreeEnvironmentStringsW(native);
#else
    if (okay) okay = process_windows_environment(out, error);
#endif
    return okay;
}
static bool process_executable(char **out, qa_error *error)
{
    size_t capacity = 256;
    for (;;) {
#if defined(_WIN32)
        if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(wchar_t)) break;
        wchar_t *path = malloc(capacity * sizeof(*path));
        if (!path) return fail(error, QA_ERROR_MEMORY, "Owning current executable path");
        DWORD length = GetModuleFileNameW(NULL, path, (DWORD)capacity);
        if (!length) {
            DWORD code = GetLastError(); free(path);
            qa_error_set(error, QA_ERROR_IO, code, "Acquiring current executable path");
            return false;
        }
        if ((size_t)length < capacity) {
            path[length] = 0;
            *out = process_utf8(path, error); free(path);
            return *out != NULL;
        }
        free(path);
#elif defined(__APPLE__)
        if (capacity > UINT32_MAX) break;
        char *path = malloc(capacity);
        if (!path) return fail(error, QA_ERROR_MEMORY, "Owning current executable path");
        uint32_t needed = (uint32_t)capacity;
        if (!_NSGetExecutablePath(path, &needed)) {
            *out = realpath(path, NULL);
            int code = errno; free(path);
            if (*out) return true;
            qa_error_set(error, QA_ERROR_IO, (uint64_t)code, "Resolving current executable path");
            return false;
        }
        free(path);
        if (needed > capacity) { capacity = needed; continue; }
#elif defined(__linux__)
        char *path = malloc(capacity);
        if (!path) return fail(error, QA_ERROR_MEMORY, "Owning current executable path");
        ssize_t length = readlink("/proc/self/exe", path, capacity - 1);
        if (length < 0) {
            int code = errno; free(path);
            qa_error_set(error, QA_ERROR_IO, (uint64_t)code, "Acquiring current executable path");
            return false;
        }
        if ((size_t)length < capacity - 1) {
            path[length] = 0; *out = path; return true;
        }
        free(path);
#else
        (void)out;
        return fail(error, QA_ERROR_UNSUPPORTED, "Current executable path is unavailable on this platform");
#endif
        if (capacity > SIZE_MAX / 2) break;
        capacity *= 2;
    }
    return fail(error, QA_ERROR_MEMORY, "Current executable path exceeds native storage");
}
bool qa_native_process_environment_acquire(qa_native_process_environment *out, qa_error *error)
{
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "Inherited process output is required");
    qa_native_process_environment acquired = {0};
    if (!process_environment_values(&acquired, error) || !process_executable(&acquired.executable, error)) {
        qa_native_process_environment_dispose(&acquired); return false;
    }
    const char *separator = strrchr(acquired.executable, '/');
#if defined(_WIN32)
    const char *backslash = strrchr(acquired.executable, '\\');
    if (backslash && (!separator || backslash > separator)) separator = backslash;
#endif
    if (!separator) {
        qa_native_process_environment_dispose(&acquired);
        return fail(error, QA_ERROR_IO, "Current executable has no absolute directory");
    }
    size_t length = (size_t)(separator - acquired.executable);
    if (!length) length = 1;
#if defined(_WIN32)
    if (length == 2 && acquired.executable[1] == ':') ++length;
#endif
    acquired.executable_directory = malloc(length + 1);
    if (!acquired.executable_directory) {
        qa_native_process_environment_dispose(&acquired);
        return fail(error, QA_ERROR_MEMORY, "Owning current executable directory");
    }
    memcpy(acquired.executable_directory, acquired.executable, length);
    acquired.executable_directory[length] = 0;
#if defined(_WIN32)
    if (!process_drive_roots(&acquired, error)) {
        qa_native_process_environment_dispose(&acquired); return false;
    }
#endif
    *out = acquired; return true;
}
static bool platform_fail_errno(const qa_native_process_platform *owner, int code, qa_error *error, const char *message)
{
    native_error_owner = owner;
#if defined(_WIN32)
    native_error_value = (qa_fs_native_error){3, (uint32_t)code, true};
#else
    native_error_value = (qa_fs_native_error){1, (uint32_t)code, true};
#endif
    return fail(error, QA_ERROR_IO, message);
}
#if defined(_WIN32)
static bool platform_fail_windows(const qa_native_process_platform *owner, DWORD code, qa_error *error, const char *message)
{
    native_error_owner = owner; native_error_value = (qa_fs_native_error){2, code, true};
    return fail(error, QA_ERROR_IO, message);
}
#endif
static bool stream_identity(int descriptor, uint64_t *device, uint64_t *object)
{
#if defined(_WIN32)
    intptr_t handle = _get_osfhandle(descriptor);
    BY_HANDLE_FILE_INFORMATION info;
    if (handle == -1) return false;
    if (GetFileType((HANDLE)handle) == FILE_TYPE_DISK) {
        if (!GetFileInformationByHandle((HANDLE)handle, &info)) return false;
        *device = info.dwVolumeSerialNumber;
        *object = (uint64_t)info.nFileIndexHigh << 32 | info.nFileIndexLow;
    } else {
        /* This is the actual retained stream handle, never a source HANDLE. */
        *device = GetFileType((HANDLE)handle); *object = (uint64_t)handle;
    }
#else
    struct stat info;
    if (fstat(descriptor, &info)) return false;
    *device = (uint64_t)info.st_dev; *object = (uint64_t)info.st_ino;
#endif
    return true;
}
#if defined(_WIN32)
static bool locale_text(qa_native_process_platform *owner, LPCWSTR name, LCTYPE type,
    uint16_t out[QA_NATIVE_WINDOWS_LOCALE_UNITS], qa_error *error)
{
    WCHAR text[QA_NATIVE_WINDOWS_LOCALE_UNITS];
    int count = GetLocaleInfoEx(name,type,text,QA_NATIVE_WINDOWS_LOCALE_UNITS);
    if (!count) return platform_fail_windows(owner,GetLastError(),error,"Acquiring actual Windows locale text");
    if (count > QA_NATIVE_WINDOWS_LOCALE_UNITS || text[count - 1])
        return fail(error,QA_ERROR_FORMAT,"Actual Windows locale text is unterminated");
    for (int i = 0; i < count; ++i) out[i] = (uint16_t)text[i];
    return true;
}
static bool locale_record(qa_native_process_platform *owner, LCID id,
    qa_native_windows_locale *out, qa_error *error)
{
    WCHAR name[QA_NATIVE_WINDOWS_LOCALE_NAME_UNITS];
    int count = GetLocaleInfoW(id,LOCALE_SNAME,name,QA_NATIVE_WINDOWS_LOCALE_NAME_UNITS);
    if (!count) return platform_fail_windows(owner,GetLastError(),error,"Acquiring actual Windows NLS locale name");
    if (count > QA_NATIVE_WINDOWS_LOCALE_NAME_UNITS || name[count - 1])
        return fail(error,QA_ERROR_FORMAT,"Actual Windows NLS locale name is unterminated");
    for (int i = 0; i < count; ++i) out->collation_name[i] = (uint16_t)name[i];
    NLSVERSIONINFOEX sort = {.dwNLSVersionInfoSize = sizeof(sort)};
    if (!GetNLSVersionEx(COMPARE_STRING,name,&sort))
        return platform_fail_windows(owner,GetLastError(),error,"Acquiring actual Windows NLS sort version");
    out->sort_version = sort.dwNLSVersion; out->sort_defined_version = sort.dwDefinedVersion;
    out->sort_effective_id = sort.dwEffectiveId;
    memcpy(out->sort_custom_version,&sort.guidCustomVersion,sizeof(out->sort_custom_version));
    DWORD language = 0, ansi = 0, oem = 0;
    if (!GetLocaleInfoEx(name,LOCALE_ILANGUAGE | LOCALE_RETURN_NUMBER,(LPWSTR)&language,2) ||
        !GetLocaleInfoEx(name,LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,(LPWSTR)&ansi,2) ||
        !GetLocaleInfoEx(name,LOCALE_IDEFAULTCODEPAGE | LOCALE_RETURN_NUMBER,(LPWSTR)&oem,2))
        return platform_fail_windows(owner,GetLastError(),error,"Acquiring actual Windows locale code pages");
    out->lcid = id; out->language_id = language; out->ansi_code_page = ansi; out->oem_code_page = oem;
    return locale_text(owner,name,LOCALE_SDECIMAL,out->decimal,error) &&
        locale_text(owner,name,LOCALE_STHOUSAND,out->thousands,error) &&
        locale_text(owner,name,LOCALE_SGROUPING,out->grouping,error) &&
        locale_text(owner,name,LOCALE_SDECIMAL | LOCALE_NOUSEROVERRIDE,out->default_decimal,error) &&
        locale_text(owner,name,LOCALE_STHOUSAND | LOCALE_NOUSEROVERRIDE,out->default_thousands,error) &&
        locale_text(owner,name,LOCALE_SGROUPING | LOCALE_NOUSEROVERRIDE,out->default_grouping,error);
}
#else
static bool locale_ascii(const char *source, uint16_t out[QA_NATIVE_WINDOWS_LOCALE_UNITS])
{
    size_t count = strlen(source);
    if (count >= QA_NATIVE_WINDOWS_LOCALE_UNITS) return false;
    for (size_t i = 0; i < count; ++i) {
        if ((unsigned char)source[i] > 127) return false;
        out[i] = (uint8_t)source[i];
    }
    return true;
}
static bool locale_grouping(const char *source, uint16_t out[QA_NATIVE_WINDOWS_LOCALE_UNITS])
{
    size_t at = 0;
    for (size_t i = 0; i < QA_NATIVE_WINDOWS_LOCALE_UNITS; ++i) {
        unsigned char value = (unsigned char)source[i];
        if (value == (unsigned char)CHAR_MAX) {
            if (!at) out[0] = '0';
            return true;
        }
        if (value > 9 || at + (at ? 2u : 1u) >= QA_NATIVE_WINDOWS_LOCALE_UNITS) return false;
        if (at) out[at++] = ';';
        out[at++] = (uint16_t)('0' + value);
        if (!value) return true;
    }
    return false;
}
#endif
static bool acquire_locale(qa_native_process_platform *owner, qa_error *error)
{
#if defined(_WIN32)
    owner->locale.source = 2; owner->locale.ansi_code_page = GetACP(); owner->locale.oem_code_page = GetOEMCP();
    if (!locale_record(owner,GetUserDefaultLCID(),&owner->locale.user,error) ||
        !locale_record(owner,GetSystemDefaultLCID(),&owner->locale.system,error)) return false;
#else
    owner->locale.source = 1; owner->locale.ansi_code_page = 1252; owner->locale.oem_code_page = 437;
    const char *name = getenv("LC_ALL"); owner->locale_input = 1;
    if (!name || !*name) { name = getenv("LC_NUMERIC"); owner->locale_input = 2; }
    if (!name || !*name) { name = getenv("LANG"); owner->locale_input = 3; }
    if (!name || !*name) { name = "C"; owner->locale_input = 4; }
    owner->locale_name = strdup(name);
    if (!owner->locale_name) return fail(error,QA_ERROR_MEMORY,"Retaining acquired POSIX locale identity");
    /* The library object resolves installed locale data without changing the
     * controller's global locale. Its records are copied before it is freed. */
    errno = 0;
    locale_t acquired = newlocale(LC_NUMERIC_MASK | LC_CTYPE_MASK,owner->locale_name,(locale_t)0);
    if (!acquired) {
        if (errno == ENOMEM) return fail(error,QA_ERROR_MEMORY,"Acquiring actual POSIX locale records");
        return true; /* Unsupported/uninstalled input stays an unavailable default. */
    }
    name = owner->locale_name;
    uint32_t id = 0;
    if (!strcmp(name,"C") || !strcmp(name,"POSIX") || !strncmp(name,"C.",2)) id = 0x007f;
    else if (!strcmp(name,"en_US") || !strncmp(name,"en_US.",6)) id = 0x0409;
    qa_native_windows_locale record = {.lcid = id, .language_id = id, .ansi_code_page = 1252, .oem_code_page = 437};
    bool supported = id && locale_ascii(nl_langinfo_l(RADIXCHAR,acquired),record.decimal) &&
        locale_ascii(nl_langinfo_l(THOUSEP,acquired),record.thousands) &&
        locale_grouping(nl_langinfo_l(GROUPING,acquired),record.grouping);
    freelocale(acquired);
    if (supported) {
        memcpy(record.default_decimal,record.decimal,sizeof(record.decimal));
        memcpy(record.default_thousands,record.thousands,sizeof(record.thousands));
        memcpy(record.default_grouping,record.grouping,sizeof(record.grouping));
        /* The explicit POSIX cross-OS policy uses this process formatting
         * snapshot for both Win32 default aliases; it has no Windows NLS host. */
        owner->locale.user = owner->locale.system = record;
    }
#endif
    return qa_native_windows_locale_profile_valid(&owner->locale) ||
        fail(error,QA_ERROR_FORMAT,"Acquired native locale snapshot is invalid");
}
bool qa_native_process_platform_locale_read(const qa_native_process_platform *owner,
    qa_native_windows_locale_profile *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_retained(owner,error)) return false;
    *out = owner->locale; return true;
}
bool qa_native_process_platform_current(const qa_native_process_platform *owner, qa_error *error)
{
    platform_operation_begin(owner);
    if (!owner || owner->closing || owner->failed)
        return fail(error, QA_ERROR_ARGUMENT, "Native platform resource owner is retiring");
    for (size_t i = 0; i < 3; ++i) {
        const platform_stream *stream = owner->streams + i;
        if (stream->closed) continue;
        uint64_t device, object;
        if (!stream_identity(stream->descriptor, &device, &object)) {
#if !defined(_WIN32)
            return platform_fail_errno(owner, errno, error, "Reading retained native standard object");
#else
            native_error_value = (qa_fs_native_error){2, GetLastError(), true};
            return fail(error, QA_ERROR_IO, "Reading retained native standard object");
#endif
        }
        if (device != stream->device || object != stream->object)
            return fail(error, QA_ERROR_IO, "Native standard descriptor lost its retained object");
    }
    return true;
}
bool qa_native_process_platform_retained(const qa_native_process_platform *owner, qa_error *error)
{
    return (owner && !owner->closing && !owner->failed && !owner->busy) ||
        fail(error, QA_ERROR_ARGUMENT, "Native platform retained owner is unavailable");
}
bool qa_native_process_platform_create(const qa_native_process_platform_options *options,
    qa_native_process_platform **out, qa_error *error)
{
    if (!options || !options->id || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Native platform requires its assigned owner identity");
    for (size_t i = 0; i < 3; ++i) {
        if (!options->standard_ids[i]) return fail(error, QA_ERROR_ARGUMENT, "Native standard capability identity is absent");
        for (size_t j = 0; j < i; ++j) if (options->standard_ids[i] == options->standard_ids[j])
            return fail(error, QA_ERROR_ARGUMENT, "Native standard capability identities repeat");
    }
    qa_native_process_platform *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Owning native platform capabilities");
    platform_operation_begin(owner);
    owner->id = options->id; owner->references = 1; owner->failed = true;
    for (size_t i = 0; i < 3; ++i) owner->streams[i].closed = true;
    *out = owner;
    if (!acquire_locale(owner,error)) return false;
    int64_t counter; qa_error clock_failure={0};
    if (!qa_platform_performance_read(&counter,&owner->frequency,&clock_failure)) {
#if defined(_WIN32)
        return platform_fail_windows(owner,(DWORD)clock_failure.offset,error,"Reading actual performance-counter frequency");
#else
        return platform_fail_errno(owner,(int)clock_failure.offset,error,"Reading actual performance-counter frequency");
#endif
    }
    for (size_t i = 0; i < 3; ++i) {
        platform_stream *stream = owner->streams + i;
        stream->owner = owner; stream->id = options->standard_ids[i];
        stream->mode = i ? 2 : 1;
#if defined(_WIN32)
        stream->descriptor = _dup((int)i);
#else
        stream->descriptor = fcntl((int)i, F_DUPFD_CLOEXEC, 3);
#endif
        if (stream->descriptor < 0)
            return platform_fail_errno(owner, errno, error, "Duplicating actual native standard descriptor");
        stream->closed = false;
        stream->held = malloc(sizeof(*stream->held));
        if (!stream->held) return fail(error, QA_ERROR_MEMORY, "Owning actual native standard descriptor holds");
        stream->held->references = 1;
#if defined(_WIN32)
        if (_setmode(stream->descriptor, _O_BINARY) == -1)
            return platform_fail_errno(owner, errno, error, "Selecting byte I/O on the owned native standard descriptor");
#endif
        if (!stream_identity(stream->descriptor, &stream->device, &stream->object)) {
#if defined(_WIN32)
            return platform_fail_windows(owner, GetLastError(), error, "Reading retained native standard object identity");
#else
            return platform_fail_errno(owner, errno, error, "Reading retained native standard object identity");
#endif
        }
#if defined(_WIN32)
        stream->durable_object = GetFileType((HANDLE)_get_osfhandle(stream->descriptor)) == FILE_TYPE_DISK;
#else
        stream->durable_object = true;
#endif
    }
#if defined(_WIN32)
    owner->terminal = _isatty(owner->streams[1].descriptor) != 0;
#else
    owner->terminal = isatty(owner->streams[1].descriptor) != 0;
#endif
    owner->failed = false; return true;
}
bool qa_native_process_platform_capture(const qa_native_process_platform *source,
    qa_native_process_platform **out, qa_error *error)
{
    if (!out || *out || !qa_native_process_platform_retained(source, error)) return false;
    qa_native_process_platform *copy = malloc(sizeof(*copy));
    if (!copy) return fail(error, QA_ERROR_MEMORY, "Retaining native platform stream graph");
    *copy = *source; copy->references = 1; copy->busy = 0;
    copy->locale_name = source->locale_name ? strdup(source->locale_name) : NULL;
    if (source->locale_name && !copy->locale_name) {
        free(copy); return fail(error,QA_ERROR_MEMORY,"Retaining actual POSIX locale snapshot");
    }
    for (size_t i = 0; i < 3; ++i) {
        copy->streams[i].owner = copy;
        if (!copy->streams[i].closed && copy->streams[i].held) ++copy->streams[i].held->references;
    }
    *out = copy; return true;
}
bool qa_native_process_platform_entropy(void *context, void *out, size_t bytes, qa_error *error)
{
    qa_native_process_platform *owner = context;
    if ((!out && bytes) || !qa_native_process_platform_current(owner, error)) return false;
    qa_error failure={0};
    if (qa_platform_entropy(out,bytes,&failure)) return true;
#if defined(_WIN32)
    if (error) *error=failure;
    return false;
#else
    return platform_fail_errno(owner,(int)failure.offset,error,"Reading actual platform entropy");
#endif
}
bool qa_native_process_platform_random(void *context, void *out, size_t bytes,
    uint32_t flags, size_t *completed, int32_t *native_errno, qa_error *error)
{
    if (!qa_native_process_platform_current(context, error)) return false;
    return qa_platform_random(out, bytes, flags, completed, native_errno, error);
}
bool qa_native_process_platform_temporary_root(void *context, qa_fs_root **out,
    qa_error *error)
{
    return qa_native_process_platform_current(context, error) &&
        qa_fs_root_temporary_create(out, error);
}
bool qa_native_process_platform_milliseconds(void *context, int64_t *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_current(context, error)) return false;
    qa_platform_timespec time; qa_error failure={0};
    if (!qa_platform_clock_read(QA_PLATFORM_CLOCK_REALTIME,&time,&failure)) {
#if defined(_WIN32)
        return platform_fail_windows(context,(DWORD)failure.offset,error,"Reading actual platform wall clock");
#else
        return platform_fail_errno(context,(int)failure.offset,error,"Reading actual platform wall clock");
#endif
    }
    if (time.seconds > INT64_MAX / 1000 || time.seconds < INT64_MIN / 1000)
        return fail(error, QA_ERROR_UNSUPPORTED, "Platform wall clock exceeds signed milliseconds");
    int64_t base = time.seconds * 1000, fraction = time.nanoseconds / 1000000;
    if (base > INT64_MAX - fraction) return fail(error, QA_ERROR_UNSUPPORTED, "Platform wall clock exceeds signed milliseconds");
    *out = base + fraction;
    return true;
}
bool qa_native_process_platform_seconds(void *context, int64_t *out, qa_error *error)
{
    int64_t milliseconds;
    if (!out || !qa_native_process_platform_milliseconds(context, &milliseconds, error)) return false;
    *out = milliseconds / 1000 - (milliseconds % 1000 < 0); return true;
}
bool qa_native_process_platform_performance(void *context, int64_t *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_current(context, error)) return false;
    int64_t frequency; qa_error failure={0};
    if (qa_platform_performance_read(out,&frequency,&failure)) return true;
#if defined(_WIN32)
    return platform_fail_windows(context,(DWORD)failure.offset,error,"Reading actual performance counter");
#else
    return platform_fail_errno(context,(int)failure.offset,error,"Reading actual performance counter");
#endif
}
int64_t qa_native_process_platform_frequency(const qa_native_process_platform *owner)
{ return owner ? owner->frequency : 0; }
#if defined(_WIN32)
static bool comparison_sort_current(qa_native_process_platform *owner,
    const qa_native_windows_locale *record, LPCWSTR name, qa_error *error)
{
    NLSVERSIONINFOEX sort = {.dwNLSVersionInfoSize = sizeof(sort)};
    if (!GetNLSVersionEx(COMPARE_STRING,name,&sort))
        return platform_fail_windows(owner,GetLastError(),error,"Qualifying retained Windows NLS collation");
    if (sort.dwNLSVersion != record->sort_version || sort.dwDefinedVersion != record->sort_defined_version ||
        sort.dwEffectiveId != record->sort_effective_id ||
        memcmp(&sort.guidCustomVersion,record->sort_custom_version,sizeof(record->sort_custom_version)))
        return fail(error,QA_ERROR_UNSUPPORTED,"Actual Windows NLS sort version differs from its retained owner");
    return true;
}
static bool comparison_input(const uint16_t *source, size_t count, bool wide,
    uint32_t code_page, WCHAR **out, int *length, uint32_t *source_error, qa_error *error)
{
    if (count > INT_MAX || count > SIZE_MAX / sizeof(WCHAR) - 1)
        return fail(error,QA_ERROR_UNSUPPORTED,"Windows comparison exceeds native count domain");
    if (wide || !count) {
        WCHAR *text = malloc((count + 1) * sizeof(*text));
        if (!text) return fail(error,QA_ERROR_MEMORY,"Retaining Windows comparison input");
        for (size_t i = 0; i < count; ++i) text[i] = (WCHAR)source[i];
        text[count] = 0; *out = text; *length = (int)count; return true;
    }
    char *bytes = malloc(count);
    if (!bytes) return fail(error,QA_ERROR_MEMORY,"Retaining ANSI comparison input");
    for (size_t i = 0; i < count; ++i) {
        if (source[i] > 255) { free(bytes); return fail(error,QA_ERROR_FORMAT,"ANSI comparison input exceeds literal byte domain"); }
        bytes[i] = (char)(uint8_t)source[i];
    }
    int needed = MultiByteToWideChar(code_page,0,bytes,(int)count,NULL,0);
    if (!needed) { *source_error = GetLastError(); free(bytes); return true; }
    if ((size_t)needed > SIZE_MAX / sizeof(WCHAR) - 1) {
        free(bytes); return fail(error,QA_ERROR_UNSUPPORTED,"NLS ANSI conversion exceeds native storage domain");
    }
    WCHAR *text = malloc(((size_t)needed + 1) * sizeof(*text));
    if (!text) { free(bytes); return fail(error,QA_ERROR_MEMORY,"Retaining actual NLS ANSI conversion"); }
    int completed = MultiByteToWideChar(code_page,0,bytes,(int)count,text,needed);
    free(bytes);
    if (!completed) { *source_error = GetLastError(); free(text); return true; }
    text[completed] = 0; *out = text; *length = completed; return true;
}
#endif
bool qa_native_process_platform_compare_string(void *context, uint32_t locale, uint32_t flags,
    bool wide, const uint16_t *first, size_t first_count, const uint16_t *second, size_t second_count,
    int32_t *out, uint32_t *source_error, qa_error *error)
{
    if (!out || !source_error || !first || !second)
        return fail(error,QA_ERROR_ARGUMENT,"Windows comparison requires actual inputs and outputs");
    if (!qa_native_process_platform_current(context,error)) return false;
    qa_native_process_platform *owner = context;
    if (owner->locale.source != 2)
        return fail(error,QA_ERROR_UNSUPPORTED,"Native platform has no Windows-compatible collation producer");
#if defined(_WIN32)
    const qa_native_windows_locale *record = locale == 0 || locale == 0x0400 ? &owner->locale.user :
        locale == 0x0800 ? &owner->locale.system : locale == owner->locale.user.lcid ? &owner->locale.user :
        locale == owner->locale.system.lcid ? &owner->locale.system : NULL;
    if (!record) return fail(error,QA_ERROR_UNSUPPORTED,"Comparison locale differs from its acquired NLS profile");
    WCHAR name[QA_NATIVE_WINDOWS_LOCALE_NAME_UNITS];
    for (size_t i = 0; i < QA_NATIVE_WINDOWS_LOCALE_NAME_UNITS; ++i) name[i] = (WCHAR)record->collation_name[i];
    if (!comparison_sort_current(owner,record,name,error)) return false;
    uint32_t code_page = flags & LOCALE_USE_CP_ACP ? owner->locale.ansi_code_page : record->ansi_code_page;
    if (!wide && !code_page)
        return fail(error,QA_ERROR_UNSUPPORTED,"ANSI comparison locale has no acquired code page");
    WCHAR *a = NULL, *b = NULL; int a_count = 0, b_count = 0; uint32_t native_error = 0;
    bool okay = comparison_input(first,first_count,wide,code_page,&a,&a_count,&native_error,error) &&
        (!a || comparison_input(second,second_count,wide,code_page,&b,&b_count,&native_error,error));
    if (okay && a && b) {
        int compared = CompareStringEx(name,flags,a,a_count,b,b_count,NULL,NULL,0);
        if (!compared) native_error = GetLastError();
        okay = comparison_sort_current(owner,record,name,error);
        if (okay) *out = compared;
    } else if (okay) *out = 0;
    free(a); free(b);
    if (okay) *source_error = native_error;
    return okay;
#else
    (void)locale; (void)flags; (void)wide; (void)first_count; (void)second_count;
    return fail(error,QA_ERROR_UNSUPPORTED,"Native platform has no Windows NLS provider");
#endif
}
bool qa_native_process_platform_sysv_calendar(void *context, int64_t milliseconds, bool local,
    qa_platform_calendar_fields *out, qa_error *error)
{
    return qa_native_process_platform_current(context, error) &&
        qa_platform_calendar(milliseconds, local, out, error);
}
bool qa_native_process_platform_calendar(void *context, int64_t milliseconds, bool local,
    qa_native_windows_calendar *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_current(context, error)) return false;
    qa_platform_calendar_fields date;
    if (!qa_platform_calendar(milliseconds,local,&date,error)) return false;
    *out=(qa_native_windows_calendar){date.year,date.month,date.weekday,date.day,
        date.hour,date.minute,date.second,date.millisecond,date.yearday,date.daylight,date.timezone_minutes};
    return true;
}
bool qa_native_process_platform_linux_identity_read(qa_native_process_platform *owner,
    qa_native_process_linux_identity *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_current(owner, error)) return false;
#if defined(__linux__)
    struct utsname native;
    int64_t ticks = qa_platform_clock_ticks_per_second();
    if (ticks <= 0) return fail(error, QA_ERROR_UNSUPPORTED, "Actual Linux clock tick policy is unavailable");
    if (uname(&native)) return platform_fail_errno(owner, errno, error, "Reading actual Linux kernel identity");
    qa_native_process_linux_identity value = {.uid = (uint32_t)getuid(), .effective_uid = (uint32_t)geteuid(),
        .gid = (uint32_t)getgid(), .effective_gid = (uint32_t)getegid(), .clock_ticks = (int64_t)ticks};
    const char *sources[6] = {native.sysname, native.nodename, native.release, native.version, native.machine, native.domainname};
    char *targets[6] = {value.system, value.node, value.release, value.version, value.machine, value.domain};
    for (size_t i = 0; i < 6; ++i) {
        size_t bytes = strlen(sources[i]);
        if (bytes >= sizeof(value.system)) return fail(error, QA_ERROR_UNSUPPORTED, "Actual Linux kernel identity exceeds the owned named field");
        memcpy(targets[i], sources[i], bytes + 1);
    }
    if (!qa_native_process_platform_current(owner, error)) return false;
    *out = value; return true;
#else
    return fail(error, QA_ERROR_UNSUPPORTED, "Actual Linux kernel identity producer is unavailable on this operating system");
#endif
}
bool qa_native_process_platform_linux_clock_read(qa_native_process_platform *owner,
    int32_t id, int64_t *seconds, int32_t *nanoseconds, qa_error *error)
{
    if (!seconds || !nanoseconds || !qa_native_process_platform_current(owner, error)) return false;
    qa_platform_clock_kind kind;
    /* Linux source clock IDs retain their ABI on every controller host. */
    switch (id) {
    case 0: kind = QA_PLATFORM_CLOCK_REALTIME; break;
    case 1: kind = QA_PLATFORM_CLOCK_MONOTONIC; break;
    case 4: kind = QA_PLATFORM_CLOCK_MONOTONIC_RAW; break;
    case 5: kind = QA_PLATFORM_CLOCK_REALTIME_COARSE; break;
    case 6: kind = QA_PLATFORM_CLOCK_MONOTONIC_COARSE; break;
    case 7: kind = QA_PLATFORM_CLOCK_BOOTTIME; break;
    case 8: kind = QA_PLATFORM_CLOCK_REALTIME_ALARM; break;
    case 9: kind = QA_PLATFORM_CLOCK_BOOTTIME_ALARM; break;
    case 11: kind = QA_PLATFORM_CLOCK_TAI; break;
    case 2: case 3:
        return fail(error, QA_ERROR_UNSUPPORTED, "Source CPU clock requires its actual task owner");
    default:
        return fail(error, QA_ERROR_UNSUPPORTED, "Linux clock ID is outside the retained global-clock capability");
    }
    qa_platform_timespec time; qa_error failure = {0};
    if (!qa_platform_clock_read(kind, &time, &failure)) {
        if (failure.code == QA_ERROR_UNSUPPORTED) {
            if (error) *error = failure;
            return false;
        }
#if defined(_WIN32)
        return platform_fail_windows(owner, (DWORD)failure.offset, error,
            "Reading actual global clock");
#else
        return platform_fail_errno(owner, (int)failure.offset, error,
            "Reading actual global clock");
#endif
    }
    *seconds = time.seconds; *nanoseconds = time.nanoseconds;
    return true;
}
bool qa_native_process_platform_file_status(qa_native_process_platform *owner, uint64_t id,
    qa_fs_posix_status *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_current(owner, error)) return false;
    platform_stream *row = NULL;
    for (size_t i = 0; i < 3; ++i) if (owner->streams[i].id == id) row = owner->streams + i;
    if (!row || row->closed) return fail(error, QA_ERROR_NOT_FOUND, "Native standard file capability is absent or closed");
#if !defined(_WIN32)
    struct stat info;
    int result;
    do { result = fstat(row->descriptor, &info); } while (result < 0 && errno == EINTR);
    if (result < 0) return platform_fail_errno(owner, errno, error, "Reading actual native standard file status");
#if defined(__APPLE__)
    struct timespec access = info.st_atimespec, modification = info.st_mtimespec, change = info.st_ctimespec;
#else
    struct timespec access = info.st_atim, modification = info.st_mtim, change = info.st_ctim;
#endif
    if (access.tv_nsec < 0 || access.tv_nsec >= 1000000000L ||
        modification.tv_nsec < 0 || modification.tv_nsec >= 1000000000L ||
        change.tv_nsec < 0 || change.tv_nsec >= 1000000000L)
        return fail(error, QA_ERROR_IO, "Actual native standard timestamps are outside their POSIX domain");
    qa_fs_posix_status value = {.device = (uint64_t)info.st_dev, .inode = (uint64_t)info.st_ino,
        .links = (uint64_t)info.st_nlink, .special_device = (uint64_t)info.st_rdev,
        .mode = (uint32_t)info.st_mode, .uid = (uint32_t)info.st_uid, .gid = (uint32_t)info.st_gid,
        .size = (int64_t)info.st_size, .block_size = (int64_t)info.st_blksize, .blocks = (int64_t)info.st_blocks,
        .access = {(int64_t)access.tv_sec, (uint32_t)access.tv_nsec},
        .modification = {(int64_t)modification.tv_sec, (uint32_t)modification.tv_nsec},
        .change = {(int64_t)change.tv_sec, (uint32_t)change.tv_nsec}};
    if (!qa_native_process_platform_current(owner, error)) return false;
    *out = value; return true;
#else
    return fail(error, QA_ERROR_UNSUPPORTED, "Windows standard objects do not supply POSIX file metadata");
#endif
}

bool qa_native_process_platform_descriptor_status(qa_native_process_platform *owner, uint64_t id,
    qa_fs_posix_descriptor_status *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_current(owner, error)) return false;
    platform_stream *stream = NULL;
    for (size_t i = 0; i < 3; ++i) if (owner->streams[i].id == id) stream = owner->streams + i;
    if (!stream || stream->closed) return fail(error, QA_ERROR_NOT_FOUND, "Actual standard descriptor is closed or absent");
#if !defined(_WIN32)
    int flags;
    do { flags = fcntl(stream->descriptor, F_GETFL); } while (flags < 0 && errno == EINTR);
    if (flags < 0) return platform_fail_errno(owner, errno, error, "Reading actual standard descriptor status flags");
    off_t offset;
    do { offset = lseek(stream->descriptor, 0, SEEK_CUR); } while (offset < 0 && errno == EINTR);
    int code = errno;
    if (offset < 0 && code != ESPIPE)
        return platform_fail_errno(owner, code, error, "Observing actual standard descriptor position");
    *out = (qa_fs_posix_descriptor_status){.flags = (uint32_t)flags,
        .offset = offset < 0 ? 0 : (int64_t)offset, .seekable = offset >= 0};
    return true;
#else
    return fail(error, QA_ERROR_UNSUPPORTED, "Windows standard objects do not supply POSIX descriptor status");
#endif
}

bool qa_native_process_platform_descriptor_flags(qa_native_process_platform *owner, uint64_t id,
    bool append, bool nonblocking, qa_error *error)
{
    if (!qa_native_process_platform_current(owner, error)) return false;
    platform_stream *stream = NULL;
    for (size_t i = 0; i < 3; ++i) if (owner->streams[i].id == id) stream = owner->streams + i;
    if (!stream || stream->closed) return fail(error, QA_ERROR_NOT_FOUND, "Actual standard descriptor is closed or absent");
#if !defined(_WIN32)
    int previous;
    do { previous = fcntl(stream->descriptor, F_GETFL); } while (previous < 0 && errno == EINTR);
    if (previous < 0) return platform_fail_errno(owner, errno, error, "Reading actual standard descriptor flags");
    int next = (previous & ~(O_APPEND | O_NONBLOCK)) | (append ? O_APPEND : 0) | (nonblocking ? O_NONBLOCK : 0);
    if (next == previous) return true;
    int result;
    do { result = fcntl(stream->descriptor, F_SETFL, next); } while (result < 0 && errno == EINTR);
    if (result < 0) return platform_fail_errno(owner, errno, error, "Changing actual standard descriptor flags");
    return true;
#else
    (void)append; (void)nonblocking;
    return fail(error, QA_ERROR_UNSUPPORTED, "Windows standard objects do not supply POSIX descriptor flags");
#endif
}

static bool stream_read(void *context, uint64_t offset, void *out, size_t bytes,
    size_t *done, qa_error *error)
{
    (void)offset;
    platform_stream *stream = context;
    platform_operation_begin(stream ? stream->owner : NULL);
    if (done) *done = 0;
    if (!stream || !done || (!out && bytes) || stream->closed || !(stream->mode & 1) ||
        !qa_native_process_platform_current(stream->owner, error))
        return fail(error, QA_ERROR_ARGUMENT, "Native standard input lost its actual readable stream");
    ++stream->owner->busy;
#if defined(_WIN32)
    unsigned count = bytes > INT_MAX ? INT_MAX : (unsigned)bytes;
    int result; do { result = _read(stream->descriptor, out, count); } while (result < 0 && errno == EINTR);
#else
    size_t count = bytes > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes;
    ssize_t result; do { result = read(stream->descriptor, out, count); } while (result < 0 && errno == EINTR);
#endif
    --stream->owner->busy;
    if (result < 0) return platform_fail_errno(stream->owner, errno, error, "Reading actual native standard input");
    *done = (size_t)result; return true;
}
#if !defined(_WIN32)
static bool standard_write(platform_stream *stream, const void *bytes, size_t count,
    ssize_t *result, qa_error *error)
{
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    int status = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (status) return fail(error, QA_ERROR_IO, "Blocking SIGPIPE around actual native stream write");
    if (sigpending(&pending)) {
        status = pthread_sigmask(SIG_SETMASK, &previous, NULL);
        return fail(error, QA_ERROR_IO, status ? "Restoring native stream signal mask" :
            "Reading pending SIGPIPE before actual native stream write");
    }
    bool already_pending = sigismember(&pending, SIGPIPE) == 1;
    do { *result = write(stream->descriptor, bytes, count); } while (*result < 0 && errno == EINTR);
    int write_error = errno;
    if (*result < 0) {
        native_error_owner = stream->owner;
        native_error_value = (qa_fs_native_error){1, (uint32_t)write_error, true};
    }
    bool okay = true;
    /* A failed pipe write generates a thread-directed signal. Consume only
     * this write's new signal; a signal pending on entry belongs to its caller. */
    if (*result < 0 && write_error == EPIPE && !already_pending) {
#if defined(__APPLE__)
        if (sigpending(&pending)) okay = false;
        else if (sigismember(&pending, SIGPIPE) == 1) {
            int received = 0;
            status = sigwait(&blocked, &received);
            okay = !status && received == SIGPIPE;
        }
#else
        const struct timespec immediate = {0, 0};
        int received;
        do { received = sigtimedwait(&blocked, NULL, &immediate); }
        while (received < 0 && errno == EINTR);
        okay = received == SIGPIPE || (received < 0 && errno == EAGAIN);
#endif
    }
    status = pthread_sigmask(SIG_SETMASK, &previous, NULL);
    errno = write_error;
    if (status) return fail(error, QA_ERROR_IO, "Restoring native stream signal mask");
    return okay || fail(error, QA_ERROR_IO, "Consuming actual native stream write SIGPIPE");
}
#endif
static bool stream_write(void *context, uint64_t offset, qa_bytes bytes, size_t *done, qa_error *error)
{
    (void)offset;
    platform_stream *stream = context;
    platform_operation_begin(stream ? stream->owner : NULL);
    if (done) *done = 0;
    if (!stream || !done || (!bytes.data && bytes.size) || stream->closed || !(stream->mode & 2) ||
        !qa_native_process_platform_current(stream->owner, error))
        return fail(error, QA_ERROR_ARGUMENT, "Native standard output lost its actual writable stream");
    ++stream->owner->busy;
#if defined(_WIN32)
    unsigned count = bytes.size > INT_MAX ? INT_MAX : (unsigned)bytes.size;
    int result; do { result = _write(stream->descriptor, bytes.data, count); } while (result < 0 && errno == EINTR);
#else
    size_t count = bytes.size > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes.size;
    ssize_t result = -1;
    bool signals_okay = standard_write(stream, bytes.data, count, &result, error);
#endif
    --stream->owner->busy;
    if (result >= 0) *done = (size_t)result;
#if !defined(_WIN32)
    if (!signals_okay) return false;
#endif
    if (result < 0) return platform_fail_errno(stream->owner, errno, error, "Writing actual native standard output");
    return true;
}
static bool windows_stream_read(void *context, void *out, size_t bytes, size_t *done, qa_error *error)
{ return stream_read(context, 0, out, bytes, done, error); }
static bool windows_stream_write(void *context, qa_bytes bytes, qa_error *error)
{
    size_t offset = 0;
    while (offset < bytes.size) {
        size_t done = 0;
        if (!stream_write(context, 0, (qa_bytes){bytes.data + offset, bytes.size - offset}, &done, error)) return false;
        if (!done) return fail(error, QA_ERROR_IO, "Actual native standard output made no progress");
        offset += done;
    }
    return true;
}
static bool program_stream_read(void *context, uint64_t offset, void *out, size_t bytes,
    size_t *done, qa_error *error)
{
    platform_stream *stream = context;
    if (done) *done = 0;
    platform_operation_begin(stream ? stream->owner : NULL);
    if (!stream || !done || (!out && bytes) || stream->closed || !(stream->mode & 1))
        return fail(error, QA_ERROR_ARGUMENT, "PROGRAM standard input requires its actual readable object");
    qa_fs_posix_descriptor_status status;
    if (!qa_native_process_platform_descriptor_status(stream->owner, stream->id, &status, error)) return false;
    if (!status.seekable) return stream_read(context, offset, out, bytes, done, error);
#if !defined(_WIN32)
    if (offset > INT64_MAX) return fail(error, QA_ERROR_ARGUMENT, "PROGRAM standard offset exceeds its native signed domain");
    size_t count = bytes > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes;
    ++stream->owner->busy;
    ssize_t result;
    do { result = pread(stream->descriptor, out, count, (off_t)offset); } while (result < 0 && errno == EINTR);
    int code = errno; --stream->owner->busy;
    if (result < 0) return platform_fail_errno(stream->owner, code, error, "Reading actual positional PROGRAM standard input");
    *done = (size_t)result; return true;
#else
    return fail(error, QA_ERROR_UNSUPPORTED, "PROGRAM positional standard input requires POSIX objects");
#endif
}
static bool program_stream_write(void *context, uint64_t offset, qa_bytes bytes,
    size_t *done, qa_error *error)
{
    platform_stream *stream = context;
    if (done) *done = 0;
    platform_operation_begin(stream ? stream->owner : NULL);
    if (!stream || !done || (!bytes.data && bytes.size) || stream->closed || !(stream->mode & 2))
        return fail(error, QA_ERROR_ARGUMENT, "PROGRAM standard output requires its actual writable object");
    qa_fs_posix_descriptor_status status;
    if (!qa_native_process_platform_descriptor_status(stream->owner, stream->id, &status, error)) return false;
    if (!status.seekable) return stream_write(context, offset, bytes, done, error);
#if !defined(_WIN32)
    if (offset > INT64_MAX) return fail(error, QA_ERROR_ARGUMENT, "PROGRAM standard offset exceeds its native signed domain");
    size_t count = bytes.size > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes.size;
    ++stream->owner->busy;
    ssize_t result;
    do { result = pwrite(stream->descriptor, bytes.data, count, (off_t)offset); } while (result < 0 && errno == EINTR);
    int code = errno; --stream->owner->busy;
    if (result < 0) return platform_fail_errno(stream->owner, code, error, "Writing actual positional PROGRAM standard output");
    *done = (size_t)result; return true;
#else
    return fail(error, QA_ERROR_UNSUPPORTED, "PROGRAM positional standard output requires POSIX objects");
#endif
}
static bool stream_size(void *context, uint64_t *out, qa_error *error)
{
    platform_stream *stream = context;
    platform_operation_begin(stream ? stream->owner : NULL);
    if (!stream || !out || stream->closed || !qa_native_process_platform_current(stream->owner, error)) return false;
#if defined(_WIN32)
    struct _stat64 info;
    if (_fstat64(stream->descriptor, &info))
#else
    struct stat info;
    if (fstat(stream->descriptor, &info))
#endif
        return platform_fail_errno(stream->owner, errno, error, "Reading actual native standard stream metadata");
    if (info.st_size < 0) return fail(error, QA_ERROR_IO, "Actual native standard stream size is negative");
    *out = (uint64_t)info.st_size; return true;
}
static bool stream_flush(void *context, qa_error *error)
{
    platform_stream *stream = context;
    platform_operation_begin(stream ? stream->owner : NULL);
    if (!stream || stream->closed || !qa_native_process_platform_current(stream->owner, error)) return false;
    /* Writes bypass stdio buffering. Disk handles still receive a real sync;
     * terminals and pipes have no buffered data in this owner to flush. */
#if defined(_WIN32)
    HANDLE handle = (HANDLE)_get_osfhandle(stream->descriptor);
    if (GetFileType(handle) == FILE_TYPE_DISK && !FlushFileBuffers(handle))
        return platform_fail_windows(stream->owner, GetLastError(), error, "Flushing actual native standard stream");
#else
    struct stat info;
    if (fstat(stream->descriptor, &info)) return platform_fail_errno(stream->owner, errno, error, "Reading actual native standard stream kind");
    if (S_ISREG(info.st_mode) && fsync(stream->descriptor))
        return platform_fail_errno(stream->owner, errno, error, "Flushing actual native standard stream");
#endif
    return true;
}
static bool stream_close(void *context, qa_error *error)
{
    platform_stream *stream = context;
    platform_operation_begin(stream ? stream->owner : NULL);
    if (stream && stream->closed) return true;
    if (!stream || !stream->owner || stream->owner->busy)
        return fail(error, QA_ERROR_ARGUMENT, "Native standard stream is inside actual I/O");
    if (stream->held && stream->held->references > 1) {
        --stream->held->references; stream->held = NULL; stream->closed = true; return true;
    }
#if defined(_WIN32)
    int result = _close(stream->descriptor);
#else
    /* Linux close consumes the descriptor even on EINTR; never retry a numeric
     * descriptor that the OS could already have reused for another object. */
    int result = close(stream->descriptor);
#endif
    int code = errno;
    stream->closed = true; free(stream->held); stream->held = NULL;
    return !result || platform_fail_errno(stream->owner, code, error, "Closing actual native standard descriptor");
}
bool qa_native_process_platform_windows_streams(qa_native_process_platform *owner,
    qa_native_windows_stream out[3], qa_error *error)
{
    if (!out || !qa_native_process_platform_retained(owner, error)) return false;
    for (size_t i = 0; i < 3; ++i) out[i] = (qa_native_windows_stream){owner->streams[i].id,
        i ? NULL : windows_stream_read, i ? windows_stream_write : NULL, owner->streams + i};
    return true;
}
bool qa_native_process_platform_sysv_files(qa_native_process_platform *owner,
    qa_native_sysv_file out[3], bool *terminal, qa_error *error)
{
    if (!out || !terminal || !qa_native_process_platform_retained(owner, error)) return false;
    static const char *const names[3] = {"stdin", "stdout", "stderr"};
    for (size_t i = 0; i < 3; ++i) out[i] = (qa_native_sysv_file){
        .handle = owner->streams[i].id, .capability = owner->streams[i].id,
        .name = names[i], .mode = owner->streams[i].mode,
        .read = i ? NULL : stream_read, .write = i ? stream_write : NULL,
        .size = stream_size, .flush = stream_flush, .close = stream_close,
        .context = owner->streams + i};
    *terminal = owner->terminal;
    return true;
}
bool qa_native_process_platform_program_files(qa_native_process_platform *owner,
    qa_native_sysv_file out[3], qa_error *error)
{
    bool terminal;
    if (!qa_native_process_platform_sysv_files(owner, out, &terminal, error)) return false;
    for (size_t i = 0; i < 3; ++i) {
        out[i].read = i ? NULL : program_stream_read;
        out[i].write = i ? program_stream_write : NULL;
    }
    return true;
}
static bool platform_fields(qa_source_save_io *io, const qa_native_process_platform *owner,
    bool standard_roles,bool restored_closed[3])
{
    uint8_t magic[4] = {'Q','N','P','L'};
    uint64_t id = owner->id; int64_t frequency = owner->frequency;
    bool terminal = owner->terminal;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QNPL", 4) ||
        !qa_source_save_u64(io, &id) || id != owner->id ||
        !qa_source_save_i64(io, &frequency) || frequency != owner->frequency ||
        !qa_source_save_bool(io, &terminal) || terminal != owner->terminal) return false;
    qa_native_windows_locale_profile locale = owner->locale; uint32_t input = owner->locale_input;
    size_t length = owner->locale_name ? strlen(owner->locale_name) : 0;
    if (!qa_native_windows_locale_profile_save(io,&locale) || !qa_native_windows_locale_profile_equal(&locale,&owner->locale) ||
        !qa_source_save_u32(io,&input) || input != owner->locale_input || !qa_source_save_count(io,&length,SIZE_MAX) ||
        length != (owner->locale_name ? strlen(owner->locale_name) : 0)) return false;
    for (size_t at = 0; at < length;) {
        char bytes[64]; size_t count = length - at > sizeof(bytes) ? sizeof(bytes) : length - at;
        memcpy(bytes,owner->locale_name + at,count);
        if (!qa_source_save_bytes(io,bytes,count) || memcmp(bytes,owner->locale_name + at,count)) return false;
        at += count;
    }
    for (size_t i = 0; i < 3; ++i) {
        const platform_stream *row = owner->streams + i;
        uint64_t saved_id = row->id, device = row->device, object = row->durable_object ? row->object : 0;
        bool durable_object = row->durable_object;
        uint32_t mode = row->mode; bool closed = row->closed;
        if (!qa_source_save_u64(io, &saved_id) || saved_id != row->id ||
            !qa_source_save_u64(io, &device) || (!standard_roles && device != row->device) ||
            !qa_source_save_bool(io, &durable_object) || (!standard_roles && durable_object != row->durable_object) ||
            !qa_source_save_u64(io, &object) || (!durable_object && object) ||
            (!standard_roles && object != (row->durable_object ? row->object : 0)) ||
            !qa_source_save_u32(io, &mode) || mode != row->mode ||
            !qa_source_save_bool(io, &closed) || (!restored_closed && closed != row->closed)) return false;
        if (restored_closed) restored_closed[i]=closed;
    }
    return true;
}
bool qa_native_process_platform_checkpoint(const qa_native_process_platform *owner, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !owner || owner->busy ||
        !qa_native_process_platform_retained(owner, error)) return false;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && platform_fields(&io, owner, false, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}
bool qa_native_process_platform_validate(const qa_native_process_platform *owner, qa_bytes bytes,
    bool standard_roles, qa_error *error)
{
    if (!qa_native_process_platform_retained(owner, error)) return false;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && platform_fields(&io, owner, standard_roles, NULL) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    return okay || fail(error, QA_ERROR_FORMAT, "Native platform continuation differs from its retained resources");
}
bool qa_native_process_platform_restore(qa_native_process_platform *owner,qa_bytes bytes,qa_error *error)
{
    if (!qa_native_process_platform_retained(owner,error)) return false;
    qa_source_save_io io={0}; bool closed[3]={false,false,false};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && platform_fields(&io,owner,true,closed) &&
        qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok) return fail(error,QA_ERROR_FORMAT,"Native standard roles differ from their actual platform bindings");
    for (size_t i=0;i<3;++i) if (closed[i] && !stream_close(owner->streams+i,error)) return false;
    return true;
}
void qa_native_process_platform_retain(qa_native_process_platform *owner)
{ if (owner) ++owner->references; }
bool qa_native_process_platform_release(qa_native_process_platform **pointer, qa_error *error)
{
    if (!pointer) return fail(error, QA_ERROR_ARGUMENT, "Native platform release requires its actual owner");
    qa_native_process_platform *owner = *pointer;
    if (!owner) return true;
    if (owner->busy) return fail(error, QA_ERROR_ARGUMENT, "Native platform is inside actual I/O");
    if (owner->references > 1) { --owner->references; *pointer = NULL; return true; }
    owner->closing = true;
    bool okay = true; qa_error first = {0};
    for (size_t i = 0; i < 3; ++i) {
        qa_error failure = {0};
        if (!stream_close(owner->streams + i, &failure) && okay) { okay = false; first = failure; }
    }
    if (!okay) { if (error) *error = first; return false; }
    if (native_error_owner == owner) platform_operation_begin(NULL);
    free(owner->locale_name); free(owner); *pointer = NULL; return true;
}
