#include "native_libraries.h"
#include "qa/platform_services.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

struct qa_platform_library { void *object; };

typedef struct library_names {
    const char *name, *variable;
    const char *candidates[3];
} library_names;

static const library_names libraries[] = {
#if defined(_WIN32)
    {"vorbisfile", "QUAKE_VORBISFILE_LIBRARY", {"libvorbisfile-3.dll", "vorbisfile.dll"}},
    {"theoradec", "QUAKE_THEORA_LIBRARY", {"libtheoradec-1.dll", "theoradec.dll"}},
    {"freetype", "QUAKE_FREETYPE_LIBRARY", {"freetype.dll", "libfreetype-6.dll", "freetype6.dll"}}
#elif defined(__APPLE__)
    {"vorbisfile", "QUAKE_VORBISFILE_LIBRARY", {"libvorbisfile.3.dylib", "libvorbisfile.dylib"}},
    {"theoradec", "QUAKE_THEORA_LIBRARY", {"libtheoradec.2.dylib", "libtheoradec.dylib"}},
    {"freetype", "QUAKE_FREETYPE_LIBRARY", {"libfreetype.6.dylib", "libfreetype.dylib"}}
#else
    {"vorbisfile", "QUAKE_VORBISFILE_LIBRARY", {"libvorbisfile.so.3", "libvorbisfile.so"}},
    {"theoradec", "QUAKE_THEORA_LIBRARY", {"libtheoradec.so.2", "libtheoradec.so"}},
    {"freetype", "QUAKE_FREETYPE_LIBRARY", {"libfreetype.so.6", "libfreetype.so"}}
#endif
};

void qa_platform_library_close(qa_platform_library *library)
{
    if (!library) return;
    SDL_UnloadObject(library->object);
    free(library);
}

bool qa_platform_library_bind_symbol(qa_platform_library *library, const char *name,
                                     void *function, qa_error *error)
{
    void *address = SDL_LoadFunction(library->object, name);
    if (!address) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "%s: %s", name, SDL_GetError());
        return false;
    }
    memcpy(function, &address, sizeof(address));
    return true;
}

static bool candidate_open(const char *path,
    bool (*bind)(qa_platform_library *, void *, qa_error *), void *api,
    qa_platform_library **out, qa_error *failure)
{
    void *object = SDL_LoadObject(path);
    if (!object) {
        qa_error_set(failure, QA_ERROR_NOT_FOUND, 0, "%s: %s", path, SDL_GetError());
        return false;
    }
    qa_platform_library *library = malloc(sizeof(*library));
    if (!library) {
        SDL_UnloadObject(object);
        qa_error_set(failure, QA_ERROR_MEMORY, 0, "Retaining native library %s", path);
        return false;
    }
    library->object = object;
    qa_error binding = {0};
    if (!bind(library, api, &binding)) {
        qa_platform_library_close(library);
        qa_error_set(failure, binding.code, binding.offset, "%s: %s", path, binding.message);
        return false;
    }
    *out = library;
    return true;
}

static char *candidate_path(const char *directory, const char *name, qa_error *error)
{
    size_t length = strlen(directory), tail = strlen(name);
    bool separator = length && directory[length - 1] != '/' && directory[length - 1] != '\\';
    if (length > SIZE_MAX - tail - 1 - (size_t)separator) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Native library path exceeds storage");
        return NULL;
    }
    char *path = malloc(length + (size_t)separator + tail + 1);
    if (!path) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating native library path");
        return NULL;
    }
    memcpy(path, directory, length);
    if (separator) path[length++] = '/';
    memcpy(path + length, name, tail + 1);
    return path;
}

bool qa_platform_library_open(qa_platform_library_kind kind,
    bool (*bind)(qa_platform_library *, void *, qa_error *), void *api,
    qa_platform_library **out, qa_error *error)
{
    const library_names *names = &libraries[kind];
#if defined(_WIN32)
    wchar_t variable[sizeof("QUAKE_VORBISFILE_LIBRARY")];
    for (size_t i = 0; ; ++i) {
        variable[i] = (wchar_t)(unsigned char)names->variable[i];
        if (!variable[i]) break;
    }
    wchar_t *wide = NULL;
    DWORD capacity = 0;
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        DWORD length = GetEnvironmentVariableW(variable, wide, capacity);
        if (!length) {
            DWORD code = GetLastError();
            free(wide);
            if (code == ERROR_ENVVAR_NOT_FOUND) break;
            if (code == ERROR_SUCCESS)
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s must be nonempty", names->variable);
            else
                qa_error_set(error, QA_ERROR_IO, code, "Reading %s override", names->variable);
            return false;
        }
        if (length >= capacity) {
            size_t bytes = (size_t)length * sizeof(*wide);
            if (bytes / sizeof(*wide) != (size_t)length) {
                free(wide);
                qa_error_set(error, QA_ERROR_MEMORY, 0, "%s override exceeds storage", names->variable);
                return false;
            }
            wchar_t *next = realloc(wide, bytes);
            if (!next) {
                free(wide);
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Reading %s override", names->variable);
                return false;
            }
            wide = next;
            capacity = length;
            continue;
        }
        char *override = SDL_iconv_wchar_utf8(wide);
        free(wide);
        if (!override) {
            qa_error_set(error, QA_ERROR_IO, 0, "Converting %s override to UTF-8", names->variable);
            return false;
        }
        bool loaded = candidate_open(override, bind, api, out, error);
        SDL_free(override);
        return loaded;
    }
#else
    const char *override = getenv(names->variable);
    if (override) {
        if (!override[0]) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s must be nonempty", names->variable);
            return false;
        }
        return candidate_open(override, bind, api, out, error);
    }
#endif

    qa_buffer executable = {0};
    if (!qa_platform_directory_read(QA_PLATFORM_DIRECTORY_EXECUTABLE, &executable, error))
        return false;
    const char *directories[] = {(const char *)executable.data,
#if defined(__APPLE__)
        "/opt/homebrew/lib", "/usr/local/lib",
#endif
        NULL};
    qa_error failure = {0};
    size_t attempts = 0;
    for (size_t directory = 0; directory < sizeof(directories) / sizeof(*directories); ++directory) {
        for (size_t i = 0; i < sizeof(names->candidates) / sizeof(*names->candidates); ++i) {
            const char *name = names->candidates[i];
            if (!name) break;
            char *path = directories[directory] ? candidate_path(directories[directory], name, &failure) : NULL;
            if (directories[directory] && !path) goto failed;
            ++attempts;
            bool loaded = candidate_open(path ? path : name, bind, api, out, &failure);
            free(path);
            if (loaded) { qa_buffer_free(&executable); return true; }
            if (failure.code == QA_ERROR_MEMORY) goto failed;
        }
    }
    qa_buffer_free(&executable);
    qa_error_set(error, failure.code, failure.offset,
                 "Could not load %s (%zu candidates); %s", names->name, attempts, failure.message);
    return false;
failed:
    qa_buffer_free(&executable);
    if (error) *error = failure;
    return false;
}
