#include "qa/native_runtime.h"
#include "qa/filesystem.h"
#include "qa/source_save.h"
#include "../compat/native/guest/profile/guard.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

enum { RUNTIME_FILES = 15, WINE_FILE = 12, SAME_HOST_FILE = 13,
       PROFILE_FILE = 14, PROFILE_LAUNCHER = 9 };
typedef struct runtime_file {
    char *path;
    qa_fs_file *file;
    qa_fs_identity identity;
    qa_sha256_digest digest;
} runtime_file;
struct qa_native_runtime {
    size_t references;
    qa_fs_root *root;
    qa_native_runner_config config;
    runtime_file files[RUNTIME_FILES];
    char *wine_drive;
    qa_native_runner_validate_fn prior_validate;
    qa_native_profile_validate_fn prior_profile_validate;
    void *prior_context;
};

typedef struct runtime_slot {
    size_t field;
    const char *relative;
    qa_native_os os;
    qa_native_arch arch;
    bool program;
} runtime_slot;
#define SLOT(field, path, os, arch, program) {offsetof(qa_native_runner_config, field), path, os, arch, program}
static const runtime_slot slots[RUNTIME_FILES] = {
    SLOT(windows_i386_runner, "windows-i386/qa-native-runner.exe", QA_NATIVE_OS_WINDOWS, QA_NATIVE_ARCH_I386, true),
    SLOT(windows_x86_64_runner, "windows-x86_64/qa-native-runner.exe", QA_NATIVE_OS_WINDOWS, QA_NATIVE_ARCH_X86_64, true),
    SLOT(linux_i386_runner, "linux-i386/qa-native-runner", QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_I386, true),
    SLOT(linux_x86_64_runner, "linux-x86_64/qa-native-runner", QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_X86_64, true),
    SLOT(windows_i386_drrun, "windows-i386/dynamorio/bin32/drrun.exe", QA_NATIVE_OS_WINDOWS, QA_NATIVE_ARCH_I386, true),
    SLOT(windows_x86_64_drrun, "windows-x86_64/dynamorio/bin64/drrun.exe", QA_NATIVE_OS_WINDOWS, QA_NATIVE_ARCH_X86_64, true),
    SLOT(windows_i386_client, "windows-i386/qa-native-hooks.dll", QA_NATIVE_OS_WINDOWS, QA_NATIVE_ARCH_I386, false),
    SLOT(windows_x86_64_client, "windows-x86_64/qa-native-hooks.dll", QA_NATIVE_OS_WINDOWS, QA_NATIVE_ARCH_X86_64, false),
    SLOT(linux_i386_drrun, "linux-i386/dynamorio/bin32/drrun", QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_I386, true),
    SLOT(linux_x86_64_drrun, "linux-x86_64/dynamorio/bin64/drrun", QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_X86_64, true),
    SLOT(linux_i386_client, "linux-i386/qa-native-hooks.so", QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_I386, false),
    SLOT(linux_x86_64_client, "linux-x86_64/qa-native-hooks.so", QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_X86_64, false),
    SLOT(wine, "wine/bin/wine", QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_X86_64, true),
    SLOT(same_host_runner, NULL, QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_X86_64, true),
    SLOT(linux_x86_64_profile, "linux-x86_64/qa-native-profile.so", QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_X86_64, false)
};
#undef SLOT

static bool fail(qa_error *error, qa_status status, const char *message)
{
    qa_error_set(error, status, 0, "%s", message);
    return false;
}

static bool targets_equal(qa_native_target a, qa_native_target b)
{
    return a.os == b.os && a.arch == b.arch && a.abi == b.abi && a.pointer_bytes == b.pointer_bytes;
}

static const char *relative_slot(size_t index, char *path, size_t capacity)
{
    if (index != SAME_HOST_FILE) return slots[index].relative;
    qa_native_target target = qa_native_host_target();
    const char *os = target.os == QA_NATIVE_OS_WINDOWS ? "windows" :
        target.os == QA_NATIVE_OS_LINUX ? "linux" : NULL;
    const char *arch = target.arch == QA_NATIVE_ARCH_I386 ? "i386" :
        target.arch == QA_NATIVE_ARCH_X86_64 ? "x86_64" :
        target.arch == QA_NATIVE_ARCH_AARCH64 ? "aarch64" : NULL;
    if (!os || !arch) return NULL;
    int written = snprintf(path, capacity, "%s-%s/qa-native-runner%s", os, arch,
        target.os == QA_NATIVE_OS_WINDOWS ? ".exe" : "");
    return written > 0 && (size_t)written < capacity ? path : NULL;
}

static char *copy(const char *text, qa_error *error)
{
    size_t length = strlen(text);
    char *result = length < SIZE_MAX ? malloc(length + 1) : NULL;
    if (!result) { fail(error, QA_ERROR_MEMORY, "Retaining native runtime path"); return NULL; }
    memcpy(result, text, length + 1);
    return result;
}

static char *join(const char *base, const char *tail, qa_error *error)
{
    size_t first = strlen(base), second = strlen(tail);
    if (first > SIZE_MAX - 2 || second > SIZE_MAX - first - 2) {
        fail(error, QA_ERROR_MEMORY, "Native runtime path exceeds address space"); return NULL;
    }
    char *path = malloc(first + second + 2);
    if (!path) { fail(error, QA_ERROR_MEMORY, "Retaining native runtime path"); return NULL; }
    memcpy(path, base, first); path[first] = '/';
    memcpy(path + first + 1, tail, second + 1);
    return path;
}

static bool absolute_file(const char *input, char **out, qa_error *error)
{
    char *path = copy(input, error);
    if (!path) return false;
#if defined(_WIN32)
    for (char *p = path; *p; ++p) if (*p == '\\') *p = '/';
#endif
    char *slash = strrchr(path, '/');
    const char *leaf = slash ? slash + 1 : path;
    char *name = copy(leaf, error);
    if (!name) { free(path); return false; }
    if (slash) {
        if (slash == path || (slash == path + 2 && path[1] == ':')) slash[1] = 0;
        else *slash = 0;
    }
    qa_fs_root *parent = NULL;
    bool okay = qa_fs_root_open(slash ? path : ".", &parent, error) &&
        qa_fs_root_join(parent, name, out, error);
    qa_fs_root_close(parent); free(name); free(path);
    return okay;
}

static bool unchanged(const runtime_file *file, qa_error *error)
{
    if (!file->file) return true;
    bool same = false;
    if (!qa_fs_file_path_unchanged(file->file, &file->identity, &same, error)) return false;
    return same || fail(error, QA_ERROR_FORMAT, "Native runtime file no longer names its held capability");
}

static bool executable_path(const runtime_file *file, qa_error *error)
{
#if !defined(_WIN32)
    if (access(file->path, X_OK) != 0)
        return fail(error, QA_ERROR_IO, "Native runtime program is not executable");
#else
    (void)file; (void)error;
#endif
    return true;
}

static bool open_slot(qa_native_runtime *runtime, size_t index,
    const char *override, qa_error *error)
{
    runtime_file *file = &runtime->files[index];
    const runtime_slot *slot = &slots[index];
    char host_path[96];
    const char *relative = relative_slot(index, host_path, sizeof(host_path));
    qa_fs_entry_kind kind = QA_FS_MISSING;
    bool explicit_path = override != NULL;
    if (explicit_path) {
        if (!absolute_file(override, &file->path, error) ||
            !qa_fs_path_status(file->path, true, &kind, NULL, error)) return false;
    } else if (runtime->root) {
        if (!relative) return fail(error, QA_ERROR_UNSUPPORTED, "Native package has no exact host helper directory");
        if (!qa_fs_root_status(runtime->root, relative, &kind, NULL, error)) return false;
        if (kind != QA_FS_MISSING && !qa_fs_root_join(runtime->root, relative, &file->path, error)) return false;
    }
    if (kind == QA_FS_MISSING)
        return !explicit_path || fail(error, QA_ERROR_NOT_FOUND, "Explicit native runtime program was not found");
    if (kind != QA_FS_REGULAR)
        return fail(error, QA_ERROR_FORMAT, "Native runtime artifact is not a regular file");
    bool okay = explicit_path ? qa_fs_file_open(file->path, &file->file, &file->identity, error) :
        qa_fs_root_file_open(runtime->root, relative, &file->file, &file->identity, error);
    qa_buffer bytes = {0};
    qa_native_image_info image = {0};
    if (okay) okay = qa_fs_file_read_snapshot(file->file, &file->identity, &bytes, error);
    if (okay) okay = slot->program ? qa_native_inspect_program((qa_bytes){bytes.data, bytes.size}, &image, error) :
        qa_native_inspect((qa_bytes){bytes.data, bytes.size}, &image, error);
    qa_native_target expected = index == WINE_FILE || index == SAME_HOST_FILE ? qa_native_host_target() :
        (qa_native_target){.os = slot->os, .arch = slot->arch};
    if (okay && (image.target.os != expected.os || image.target.arch != expected.arch))
        okay = fail(error, QA_ERROR_FORMAT, "Native runtime artifact differs from its actual packaged ABI");
    if (okay && index == SAME_HOST_FILE && !targets_equal(image.target, expected))
        okay = fail(error, QA_ERROR_FORMAT, "Native same-host helper differs from the exact process ABI");
    if (okay && slot->program && image.target.os == qa_native_host_target().os)
        okay = executable_path(file, error);
    if (okay) {
        file->digest = image.digest;
        const char *published = file->path;
        memcpy((uint8_t *)&runtime->config + slot->field, &published, sizeof(published));
    }
    qa_buffer_free(&bytes);
    return okay;
}

static bool validate_target(void *context, qa_native_target target, bool instrumented, qa_error *error)
{
    qa_native_runtime *runtime = context;
    if (!runtime || (target.os != QA_NATIVE_OS_WINDOWS && target.os != QA_NATIVE_OS_LINUX))
        return fail(error, QA_ERROR_UNSUPPORTED, "Native runtime has no packaged capability for this target");
    bool x86 = target.arch == QA_NATIVE_ARCH_I386 || target.arch == QA_NATIVE_ARCH_X86_64;
    size_t runner = SAME_HOST_FILE;
    if (x86) {
        runner = target.os == QA_NATIVE_OS_WINDOWS ? 0 : 2;
        if (target.arch == QA_NATIVE_ARCH_X86_64) ++runner;
    }
    if (!x86 || !runtime->files[runner].file) {
        if (!targets_equal(target, qa_native_host_target()))
            return fail(error, QA_ERROR_UNSUPPORTED, "Native helper fallback requires the exact host ABI");
        runner = SAME_HOST_FILE;
    }
    if (instrumented && !x86)
        return fail(error, QA_ERROR_UNSUPPORTED, "This native target has no packaged instrumentation capability");
    size_t launcher = target.os == QA_NATIVE_OS_WINDOWS ? 4 : 8;
    if (target.arch == QA_NATIVE_ARCH_X86_64) ++launcher;
    size_t client = launcher + 2;
    if (!runtime->files[runner].file ||
        (instrumented && (!runtime->files[launcher].file || !runtime->files[client].file)))
        return fail(error, QA_ERROR_UNSUPPORTED, "Required native helper or DynamoRIO artifacts are not installed");
    if (!unchanged(&runtime->files[runner], error) ||
        (instrumented && (!unchanged(&runtime->files[launcher], error) || !unchanged(&runtime->files[client], error))))
        return false;
    qa_native_target host = qa_native_host_target();
    if (target.os == QA_NATIVE_OS_WINDOWS && host.os != QA_NATIVE_OS_WINDOWS) {
        if (!runtime->files[WINE_FILE].file)
            return fail(error, QA_ERROR_UNSUPPORTED, "Windows native modules require the installed Wine runtime");
        if (!unchanged(&runtime->files[WINE_FILE], error) || !executable_path(&runtime->files[WINE_FILE], error)) return false;
    }
    return !runtime->prior_validate || runtime->prior_validate(runtime->prior_context, target, instrumented, error);
}

static bool validate_profile(void *context, qa_error *error)
{
    qa_native_runtime *runtime = context;
    qa_native_target host = qa_native_host_target();
    if (!runtime || host.os != QA_NATIVE_OS_LINUX || host.arch != QA_NATIVE_ARCH_X86_64 ||
        host.pointer_bytes != 8)
        return fail(error, QA_ERROR_UNSUPPORTED, "Source hardware monitor requires the actual Linux x64 host");
    if (!runtime->files[PROFILE_LAUNCHER].file || !runtime->files[PROFILE_FILE].file)
        return fail(error, QA_ERROR_UNSUPPORTED, "Source hardware monitor launcher or client is not installed");
    if (!unchanged(&runtime->files[PROFILE_LAUNCHER], error) ||
        !unchanged(&runtime->files[PROFILE_FILE], error) ||
        !executable_path(&runtime->files[PROFILE_LAUNCHER], error)) return false;
    return !runtime->prior_profile_validate ||
        runtime->prior_profile_validate(runtime->prior_context, error);
}

bool qa_native_runtime_create(const qa_native_runtime_options *options, qa_native_runtime **out, qa_error *error)
{
    if (!options || !out || *out || (!options->root && !options->executable_directory))
        return fail(error, QA_ERROR_ARGUMENT, "Native runtime requires the actual executable directory or explicit package root");
    qa_native_runtime *runtime = calloc(1, sizeof(*runtime));
    if (!runtime) return fail(error, QA_ERROR_MEMORY, "Allocating native runtime owner");
    runtime->references = 1;
    char *root_path = options->root ? copy(options->root, error) : NULL;
#if defined(QA_NATIVE_RUNTIME_RELATIVE_ROOT)
    if (!options->root) root_path = join(options->executable_directory, QA_NATIVE_RUNTIME_RELATIVE_ROOT, error);
#else
    if (!options->root) { qa_native_runtime_release(runtime); return fail(error, QA_ERROR_UNSUPPORTED, "Build omitted its native runtime install-relative directory"); }
#endif
    qa_fs_entry_kind kind = QA_FS_MISSING;
    bool okay = root_path && qa_fs_path_status(root_path, true, &kind, NULL, error);
    if (okay && kind == QA_FS_MISSING && options->root)
        okay = fail(error, QA_ERROR_NOT_FOUND, "Explicit native runtime directory was not found");
    if (okay && kind != QA_FS_MISSING) okay = qa_fs_root_open(root_path, &runtime->root, error);
    free(root_path);
    for (size_t i = 0; okay && i < RUNTIME_FILES; ++i) {
        const char *override = NULL;
        if (options->overrides) memcpy(&override, (const uint8_t *)options->overrides + slots[i].field, sizeof(override));
        if (i == WINE_FILE && options->wine) override = options->wine;
        if (i == WINE_FILE && qa_native_host_target().os == QA_NATIVE_OS_WINDOWS && !override) continue;
        okay = open_slot(runtime, i, override, error);
    }
    const char *drive = options->overrides && options->overrides->wine_drive ? options->overrides->wine_drive : "Z:";
    if (okay) okay = (runtime->wine_drive = copy(drive, error)) != NULL;
    if (okay) {
        runtime->config.wine_drive = runtime->wine_drive;
        runtime->config.maximum_frame_bytes = options->overrides ? options->overrides->maximum_frame_bytes : 0;
        runtime->prior_validate = options->overrides ? options->overrides->validate : NULL;
        runtime->prior_profile_validate = options->overrides ? options->overrides->validate_profile : NULL;
        runtime->prior_context = options->overrides ? options->overrides->validation_context : NULL;
        runtime->config.validate = validate_target;
        runtime->config.validation_context = runtime;
        runtime->config.validate_profile = validate_profile;
        *out = runtime;
    } else qa_native_runtime_release(runtime);
    return okay;
}

void qa_native_runtime_retain(qa_native_runtime *runtime)
{
    if (runtime) ++runtime->references;
}

void qa_native_runtime_release(qa_native_runtime *runtime)
{
    if (!runtime || --runtime->references) return;
    for (size_t i = 0; i < RUNTIME_FILES; ++i) {
        qa_fs_file_close(runtime->files[i].file);
        free(runtime->files[i].path);
    }
    qa_fs_root_close(runtime->root); free(runtime->wine_drive); free(runtime);
}

const qa_native_runner_config *qa_native_runtime_config(const qa_native_runtime *runtime)
{
    return runtime ? &runtime->config : NULL;
}

bool qa_native_runtime_profile_launch(const qa_native_runtime *runtime,
    guest_profile_guard_launch *out, qa_error *error)
{
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "Source monitor launch requires its output");
    if (!validate_profile((void *)runtime, error)) return false;
    *out = (guest_profile_guard_launch){runtime->config.linux_x86_64_drrun,
        runtime->config.linux_x86_64_profile};
    return true;
}

static bool text(qa_source_save_io *io, const char *actual)
{
    size_t length = strlen(actual), saved = length;
    if (!qa_source_save_count(io, &saved, length) || saved != length) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)actual, length);
    if (length > io->input.size - io->offset || memcmp(io->input.data + io->offset, actual, length)) return false;
    io->offset += length;
    return true;
}

static bool fields(qa_source_save_io *io, const qa_native_runtime *runtime)
{
    uint8_t magic[4] = {'Q','N','R','T'};
    uint32_t count = RUNTIME_FILES;
    uint64_t maximum = runtime->config.maximum_frame_bytes;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QNRT", 4) ||
        !qa_source_save_u32(io, &count) || count != RUNTIME_FILES ||
        !qa_source_save_u64(io, &maximum) || maximum != runtime->config.maximum_frame_bytes ||
        !text(io, runtime->wine_drive)) return false;
    for (size_t i = 0; i < RUNTIME_FILES; ++i) {
        const runtime_file *file = &runtime->files[i];
        bool present = file->file != NULL, expected = present;
        if (!qa_source_save_bool(io, &present) || present != expected) return false;
        if (!present) continue;
        if (!unchanged(file, io->error) || !text(io, file->path)) return false;
        qa_fs_identity identity = file->identity;
        qa_sha256_digest digest = file->digest;
        for (size_t word = 0; word < QA_FS_IDENTITY_WORDS; ++word)
            if (!qa_source_save_u64(io, &identity.words[word])) return false;
        if (!qa_source_save_bytes(io, &digest, sizeof(digest)) ||
            !qa_fs_identity_equal(&identity, &file->identity) ||
            memcmp(&digest, &file->digest, sizeof(digest))) return false;
    }
    return true;
}

bool qa_native_runtime_checkpoint(const qa_native_runtime *runtime, qa_buffer *out, qa_error *error)
{
    if (!runtime || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Native runtime continuation needs its actual owner and empty output");
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && fields(&io, runtime) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return okay;
}

bool qa_native_runtime_validate(const qa_native_runtime *runtime, qa_bytes bytes, qa_error *error)
{
    if (!runtime) return fail(error, QA_ERROR_ARGUMENT, "Native runtime restore requires its retained actual owner");
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, runtime) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    return okay || fail(error, QA_ERROR_FORMAT, "Saved native runtime capabilities differ from the retained owner");
}
