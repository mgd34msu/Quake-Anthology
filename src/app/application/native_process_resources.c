#include "qa/native_process_resources.h"
#include "qa/native_sysv_process_save.h"
#include "qa/native_windows_process_save.h"
#include "qa/source_save.h"
#include "../../compat/native/guest/elf.h"
#include "../../compat/native/guest/pe.h"
#include "../../compat/native/guest/profile/guard.h"

#include <stdlib.h>
#include <string.h>

typedef struct process_artifact {
    qa_resource *resource;
    qa_vfs_acquisition acquisition;
    char *path;
    qa_native_image_info image;
    uint64_t base;
    bool receipt;
} process_artifact;
typedef struct process_root {
    char *prefix;
    qa_fs_root *root;
    qa_fs_object_reference reference;
    uint32_t mode;
    bool active;
} process_root;
typedef struct process_file {
    struct qa_native_process_resources *owner;
    uint64_t id;
    size_t root;
    char *name;
    qa_fs_opened_file *file;
    qa_fs_opened_reference reference;
    bool ready, closed;
} process_file;
struct qa_native_process_resources {
    qa_native_process_resources_options options;
    qa_launch_instance_lease *lease;
    const qa_launch_instance *descriptor;
    process_artifact *artifacts;
    process_root *roots;
    process_file **files;
    size_t references, busy, artifact_count, root_count, root_capacity, file_count, file_capacity;
    uint64_t next_file, allocation_base, trap_base, first_callback;
    qa_fs_file *bootstrap;
    qa_fs_identity bootstrap_identity;
    char *bootstrap_path;
    char **argv, **environment;
    uint16_t *command_line, *windows_environment;
    guest_profile_guard_launch guard;
    qa_native_sysv_artifact *sysv_artifacts;
    qa_native_windows_artifact *windows_artifacts;
    qa_native_sysv_file standards[3];
    qa_native_sysv_process_options sysv;
    qa_native_windows_process_options windows;
    qa_native_sysv_process_restore_bindings sysv_restore;
    qa_native_windows_process_restore_bindings windows_restore;
    qa_native_sysv_program_options program;
    qa_native_sysv_program_aux *program_auxiliary;
    char *interpreter_path, *program_platform, *program_base_platform;
    uint8_t program_random[16];
    bool closing, failed, captured, raw_program, has_interpreter, file_cold;
    size_t interpreter;
};
static _Thread_local const qa_native_process_resources *native_error_owner;
static _Thread_local qa_fs_native_error native_error_value;
static _Thread_local uint64_t native_error_platform_operation;
static void resource_native_begin(const qa_native_process_resources *owner)
{
    native_error_owner = owner; native_error_value = (qa_fs_native_error){0};
    qa_fs_native_error ignored;
    native_error_platform_operation = 0;
    if (owner) qa_native_process_platform_native_error_read(owner->options.platform,
        &ignored, &native_error_platform_operation);
}
static void resource_native_filesystem(const qa_native_process_resources *owner)
{
    native_error_owner = owner;
    qa_fs_opened_native_error_read(&native_error_value);
    qa_fs_native_error ignored;
    qa_native_process_platform_native_error_read(owner->options.platform,
        &ignored, &native_error_platform_operation);
}
bool qa_native_process_resources_native_error_read(const qa_native_process_resources *owner,
    qa_fs_native_error *out)
{
    if (!owner || !out) return false;
    qa_fs_native_error platform = {0}; uint64_t operation = 0;
    if (!qa_native_process_platform_native_error_read(owner->options.platform, &platform, &operation)) return false;
    *out = native_error_owner == owner && operation == native_error_platform_operation ?
        native_error_value : platform;
    return true;
}
static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static char *text_copy(const char *text)
{
    if (!text) return NULL;
    size_t size = strlen(text);
    if (size == SIZE_MAX) return NULL;
    char *copy = malloc(size + 1);
    if (copy) memcpy(copy, text, size + 1);
    return copy;
}
static bool rounded(uint64_t value, uint64_t alignment, uint64_t *out)
{
    if (!alignment || (alignment & (alignment - 1)) || value > UINT64_MAX - (alignment - 1)) return false;
    *out = (value + alignment - 1) & ~(alignment - 1); return true;
}
static bool target_equal(const qa_native_target *a, const qa_native_target *b)
{
    return a->os == b->os && a->arch == b->arch && a->abi == b->abi && a->pointer_bytes == b->pointer_bytes;
}
static bool object_equal(const qa_fs_object_reference *a, const qa_fs_object_reference *b)
{
    return a->platform == b->platform && a->words[0] == b->words[0] &&
        a->words[1] == b->words[1] && a->words[2] == b->words[2];
}
static bool resources_retained(const qa_native_process_resources *owner, qa_error *error)
{
    if (!owner || owner->closing || owner->failed || !owner->descriptor)
        return fail(error, QA_ERROR_ARGUMENT, "Native prepared resource authority is not current");
    if ((!owner->captured && !owner->options.current(owner->options.context, owner->descriptor,
        owner->options.receiver, owner->options.service_owner, error)) ||
        !qa_native_process_platform_retained(owner->options.platform, error)) return false;
    for (size_t i = 0; i < owner->artifact_count; ++i) {
        const process_artifact *row = owner->artifacts + i;
        if (row->receipt && !qa_vfs_acquisition_retained(owner->descriptor->content, &row->acquisition, error)) return false;
    }
    for (size_t i = 0; i < owner->root_count; ++i) {
        qa_fs_object_reference actual;
        if (!qa_fs_root_reference_read(owner->roots[i].root, &actual) ||
            !object_equal(&actual, &owner->roots[i].reference))
            return fail(error, QA_ERROR_IO, "Native acquired root identity differs");
    }
    return true;
}
bool qa_native_process_resources_current(const qa_native_process_resources *owner, qa_error *error)
{
    if (owner && owner->captured)
        return fail(error, QA_ERROR_ARGUMENT, "Captured native resources have no entered source authority");
    if (!resources_retained(owner, error) || !qa_native_process_platform_current(owner->options.platform, error)) return false;
    if (owner->options.policy.backend == QA_NATIVE_GUEST_HOST_X86_64) {
        bool unchanged = false;
        if (!qa_fs_file_path_unchanged(owner->bootstrap, &owner->bootstrap_identity, &unchanged, error) || !unchanged)
            return fail(error, QA_ERROR_IO, "Actual native child bootstrap changed");
    }
    return true;
}
static bool current_callback(void *context, qa_error *error)
{ return qa_native_process_resources_current(context, error); }
static bool platform_entropy(void *context, void *out, size_t bytes, qa_error *error)
{
    qa_native_process_resources *owner = context;
    return qa_native_process_resources_current(owner, error) &&
        qa_native_process_platform_entropy(owner->options.platform, out, bytes, error) &&
        qa_native_process_resources_current(owner, error);
}
static bool platform_milliseconds(void *context, int64_t *out, qa_error *error)
{
    qa_native_process_resources *owner = context;
    return qa_native_process_resources_current(owner, error) &&
        qa_native_process_platform_milliseconds(owner->options.platform, out, error) &&
        qa_native_process_resources_current(owner, error);
}
static bool platform_seconds(void *context, int64_t *out, qa_error *error)
{
    qa_native_process_resources *owner = context;
    return qa_native_process_resources_current(owner, error) &&
        qa_native_process_platform_seconds(owner->options.platform, out, error) &&
        qa_native_process_resources_current(owner, error);
}
static bool platform_performance(void *context, int64_t *out, qa_error *error)
{
    qa_native_process_resources *owner = context;
    return qa_native_process_resources_current(owner, error) &&
        qa_native_process_platform_performance(owner->options.platform, out, error) &&
        qa_native_process_resources_current(owner, error);
}
static bool platform_calendar(void *context, int64_t time, bool local, qa_native_windows_calendar *out, qa_error *error)
{
    qa_native_process_resources *owner = context;
    return qa_native_process_resources_current(owner, error) &&
        qa_native_process_platform_calendar(owner->options.platform, time, local, out, error) &&
        qa_native_process_resources_current(owner, error);
}
static bool platform_compare_string(void *context, uint32_t locale, uint32_t flags, bool wide,
    const uint16_t *first, size_t first_count, const uint16_t *second, size_t second_count,
    int32_t *out, uint32_t *source_error, qa_error *error)
{
    qa_native_process_resources *owner = context;
    return qa_native_process_resources_current(owner,error) &&
        qa_native_process_platform_compare_string(owner->options.platform,locale,flags,wide,
            first,first_count,second,second_count,out,source_error,error) &&
        qa_native_process_resources_current(owner,error);
}
static bool file_read(void *context, uint64_t offset, void *out, size_t bytes, size_t *done, qa_error *error)
{
    if (done) *done = 0;
    process_file *row = context;
    resource_native_begin(row ? row->owner : NULL);
    if (!row || !row->ready || row->closed)
        return fail(error, QA_ERROR_IO, "Native file capability is closed or was not admitted");
    if (!qa_native_process_resources_current(row->owner, error)) return false;
    ++row->owner->busy;
    bool okay = qa_fs_opened_file_read(row->file, offset, out, bytes, done, error);
    resource_native_filesystem(row->owner);
    if (okay) okay = qa_native_process_resources_current(row->owner, error);
    --row->owner->busy; return okay;
}
static bool file_write(void *context, uint64_t offset, qa_bytes bytes, size_t *done, qa_error *error)
{
    if (done) *done = 0;
    process_file *row = context;
    resource_native_begin(row ? row->owner : NULL);
    if (!row || !row->ready || row->closed)
        return fail(error, QA_ERROR_IO, "Native file capability is closed or was not admitted");
    if (!qa_native_process_resources_current(row->owner, error)) return false;
    ++row->owner->busy;
    bool okay = qa_fs_opened_file_write(row->file, offset, bytes, done, error);
    resource_native_filesystem(row->owner);
    if (okay) okay = qa_native_process_resources_current(row->owner, error);
    --row->owner->busy; return okay;
}
static bool file_size(void *context, uint64_t *out, qa_error *error)
{
    process_file *row = context;
    resource_native_begin(row ? row->owner : NULL);
    if (!row || !row->ready || row->closed)
        return fail(error, QA_ERROR_IO, "Native file capability is closed or was not admitted");
    if (!qa_native_process_resources_current(row->owner, error)) return false;
    ++row->owner->busy; bool okay = qa_fs_opened_file_size(row->file, out, error);
    resource_native_filesystem(row->owner);
    if (okay) okay = qa_native_process_resources_current(row->owner, error);
    --row->owner->busy; return okay;
}
static bool file_truncate(void *context, uint64_t bytes, qa_error *error)
{
    process_file *row = context;
    resource_native_begin(row ? row->owner : NULL);
    if (!row || !row->ready || row->closed)
        return fail(error, QA_ERROR_IO, "Native file capability is closed or was not admitted");
    if (!qa_native_process_resources_current(row->owner, error)) return false;
    ++row->owner->busy; bool okay = qa_fs_opened_file_truncate(row->file, bytes, error);
    resource_native_filesystem(row->owner);
    if (okay) okay = qa_native_process_resources_current(row->owner, error);
    --row->owner->busy; return okay;
}
static bool file_flush(void *context, qa_error *error)
{
    process_file *row = context;
    resource_native_begin(row ? row->owner : NULL);
    if (!row || !row->ready || row->closed)
        return fail(error, QA_ERROR_IO, "Native file capability is closed or was not admitted");
    if (!qa_native_process_resources_current(row->owner, error)) return false;
    ++row->owner->busy; bool okay = qa_fs_opened_file_flush(row->file, error);
    resource_native_filesystem(row->owner);
    if (okay) okay = qa_native_process_resources_current(row->owner, error);
    --row->owner->busy; return okay;
}
static bool file_close(void *context, qa_error *error)
{
    process_file *row = context;
    resource_native_begin(row ? row->owner : NULL);
    if (!row || !row->owner || row->owner->busy) return fail(error, QA_ERROR_ARGUMENT, "Native file owner is inside I/O");
    if (row->closed) return true;
    bool okay = qa_fs_opened_file_close(&row->file, error);
    resource_native_filesystem(row->owner);
    if (!row->file) row->closed = true;
    return okay;
}
static qa_native_windows_file windows_file(process_file *row)
{
    return (qa_native_windows_file){.id = row->id, .mode = row->reference.mode,
        .read = file_read, .write = file_write, .size = file_size, .truncate = file_truncate,
        .flush = file_flush, .close = file_close, .context = row};
}
static qa_native_sysv_file sysv_file(process_file *row)
{
    return (qa_native_sysv_file){.handle = row->id, .capability = row->id, .name = row->name,
        .mode = row->reference.mode, .creation = row->reference.creation,
        .read = file_read, .write = file_write, .size = file_size, .truncate = file_truncate,
        .flush = file_flush, .close = file_close, .context = row};
}
bool qa_native_process_resources_open_file(qa_native_process_resources *owner, const char *name,
    uint32_t mode, qa_fs_opened_creation creation, qa_native_windows_file *out, bool *opened, qa_error *error)
{
    resource_native_begin(owner);
    if (opened) *opened = false;
    if (!name || !out || !opened || (mode & ~3u) || creation < 1 || creation > 5 ||
        !qa_native_process_resources_current(owner, error)) return false;
    bool changing = creation != QA_FS_OPEN_EXISTING || (mode & QA_FS_OPENED_WRITE);
    bool windows = owner->artifacts[owner->options.primary].image.target.os == QA_NATIVE_OS_WINDOWS;
    if (!owner->next_file || owner->next_file == UINT64_MAX)
        return fail(error, QA_ERROR_MEMORY, "Native file identity space is exhausted");
    if (owner->file_count == owner->file_capacity) {
        size_t count = owner->file_capacity ? owner->file_capacity * 2 : 8;
        if (count < owner->file_capacity || count > SIZE_MAX / sizeof(*owner->files)) {
            return fail(error, QA_ERROR_MEMORY, "Native file roster overflows");
        }
        process_file **rows = realloc(owner->files, count * sizeof(*rows));
        if (!rows) return fail(error, QA_ERROR_MEMORY, "Owning native opened-file roster");
        owner->files = rows; owner->file_capacity = count;
    }
    process_file *row = calloc(1, sizeof(*row));
    if (row) row->name = text_copy(name);
    if (!row || !row->name) {
        if (row) free(row->name);
        free(row); return fail(error, QA_ERROR_MEMORY, "Owning native opened-file capability");
    }
    row->owner = owner; row->id = owner->next_file++;
    row->reference.mode = mode; row->reference.creation = creation;
    ++owner->busy;
    bool okay = false, admitted = false, missing = false;
    for (size_t i = 0; i < owner->root_count; ++i) {
        const process_root *root = owner->roots + i; size_t length = strlen(root->prefix);
        if (!root->active || (mode & ~root->mode) || (changing && !(root->mode & QA_FS_OPENED_WRITE))) continue;
        if (!length && (name[0] == '/' || (windows &&
            (name[0] == '\\' || (name[0] && name[1] == ':'))))) continue;
        if (strncmp(name, root->prefix, length)) continue;
        admitted = true; missing = false; row->root = i;
        char *path = text_copy(name + length);
        if (!path) { fail(error, QA_ERROR_MEMORY, "Owning source file spelling"); break; }
        if (windows) for (char *p = path; *p; ++p) if (*p == '\\') *p = '/';
        qa_error failure = {0};
        okay = qa_fs_root_opened_file(root->root, path, mode, creation, &row->file, opened, &failure);
        resource_native_filesystem(owner);
        free(path);
        if (okay || *opened) {
            if (!okay && error) *error = failure;
            break;
        }
        missing = failure.code == QA_ERROR_NOT_FOUND;
        if (!missing || changing) { if (error) *error = failure; break; }
    }
    if (*opened) {
        owner->files[owner->file_count++] = row;
        if (okay) okay = qa_fs_opened_file_reference_read(row->file, &row->reference);
        row->ready = okay; *out = windows_file(row);
        if (okay) okay = qa_native_process_resources_current(owner, error);
    } else {
        free(row->name); free(row);
        if (!admitted) okay = fail(error, QA_ERROR_IO, "Source file has no acquired directory authority for these rights");
        else if (missing) { okay = true; if (error) *error = (qa_error){0}; }
    }
    if (okay && !*opened) okay = qa_native_process_resources_current(owner, error);
    --owner->busy; return okay;
}
bool qa_native_process_resources_resolve_file(qa_native_process_resources *owner, uint64_t id,
    qa_native_windows_file *out, qa_error *error)
{
    if (!out || !resources_retained(owner, error)) return false;
    for (size_t i = 0; i < owner->file_count; ++i) {
        process_file *row = owner->files[i];
        if (row->id != id) continue;
        if (!row->ready || (!row->closed && !row->file))
            return fail(error, QA_ERROR_NOT_FOUND, "Saved native file capability lacks its retained owner");
        /* A consumed close can report failure. The source close-started row
         * still owns this idempotent callback until its retry commits; data
         * operations remain rejected on the genuinely closed physical row. */
        *out = windows_file(row); return true;
    }
    return fail(error, QA_ERROR_NOT_FOUND, "Saved native file capability is absent from the retained graph");
}
bool qa_native_process_resources_open_sysv_file(qa_native_process_resources *owner, const char *name,
    uint32_t mode, qa_fs_opened_creation creation, qa_native_sysv_file *out, bool *opened, qa_error *error)
{
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "Missing native SysV file output");
    qa_native_windows_file file = {0};
    bool okay = qa_native_process_resources_open_file(owner, name, mode, creation, &file, opened, error);
    if (opened && *opened) *out = sysv_file(file.context);
    return okay;
}
bool qa_native_process_resources_resolve_sysv_file(qa_native_process_resources *owner, uint64_t id,
    qa_native_sysv_file *out, qa_error *error)
{
    if (!out || !resources_retained(owner, error)) return false;
    for (size_t i = 0; i < 3; ++i) if (owner->standards[i].capability == id) { *out = owner->standards[i]; return true; }
    qa_native_windows_file file;
    if (!qa_native_process_resources_resolve_file(owner, id, &file, error)) return false;
    *out = sysv_file(file.context); return true;
}
bool qa_native_process_resources_file_status(qa_native_process_resources *owner, uint64_t id,
    qa_fs_posix_status *out, qa_error *error)
{
    resource_native_begin(owner);
    if (!out || !qa_native_process_resources_current(owner, error)) return false;
    for (size_t i = 0; i < 3; ++i) if (owner->standards[i].capability == id)
        return qa_native_process_platform_file_status(owner->options.platform, id, out, error) &&
            qa_native_process_resources_current(owner, error);
    for (size_t i = 0; i < owner->file_count; ++i) {
        process_file *row = owner->files[i];
        if (row->id != id) continue;
        if (!row->ready || row->closed || !row->file)
            return fail(error, QA_ERROR_NOT_FOUND, "Native file status requires its admitted open object");
        ++owner->busy;
        bool okay = qa_fs_opened_file_posix_status_read(row->file, out, error);
        resource_native_filesystem(owner);
        if (okay) okay = qa_native_process_resources_current(owner, error);
        --owner->busy; return okay;
    }
    return fail(error, QA_ERROR_NOT_FOUND, "Native file status capability is absent from the owned graph");
}
bool qa_native_process_resources_linux_identity_read(qa_native_process_resources *owner,
    qa_native_process_linux_identity *out, qa_error *error)
{
    resource_native_begin(owner);
    return qa_native_process_resources_current(owner, error) &&
        qa_native_process_platform_linux_identity_read(owner->options.platform, out, error) &&
        qa_native_process_resources_current(owner, error);
}
bool qa_native_process_resources_descriptor_status(qa_native_process_resources *owner, uint64_t id,
    qa_native_sysv_program_descriptor_status *out, qa_error *error)
{
    resource_native_begin(owner);
    if (!out || !qa_native_process_resources_current(owner, error)) return false;
    qa_fs_posix_descriptor_status value;
    bool found = false, okay = false;
    for (size_t i = 0; i < 3; ++i) if (owner->standards[i].capability == id) {
        found = true;
        okay = qa_native_process_platform_descriptor_status(owner->options.platform, id, &value, error);
        break;
    }
    if (!found) for (size_t i = 0; i < owner->file_count; ++i) {
        process_file *row = owner->files[i];
        if (row->id != id) continue;
        found = true;
        if (!row->ready || row->closed || !row->file)
            return fail(error, QA_ERROR_NOT_FOUND, "Actual native descriptor capability is closed");
        ++owner->busy;
        okay = qa_fs_opened_file_posix_descriptor_read(row->file, &value, error);
        resource_native_filesystem(owner); --owner->busy;
        break;
    }
    if (!found) return fail(error, QA_ERROR_NOT_FOUND, "Actual native descriptor capability is absent");
    if (!okay || !qa_native_process_resources_current(owner, error)) return false;
    *out = (qa_native_sysv_program_descriptor_status){.flags = value.flags,
        .offset = value.offset, .seekable = value.seekable};
    return true;
}
bool qa_native_process_resources_linux_clock_read(qa_native_process_resources *owner,
    int32_t id, int64_t *seconds, int32_t *nanoseconds, qa_error *error)
{
    resource_native_begin(owner);
    return qa_native_process_resources_current(owner, error) &&
        qa_native_process_platform_linux_clock_read(owner->options.platform, id, seconds, nanoseconds, error) &&
        qa_native_process_resources_current(owner, error);
}
bool qa_native_process_resources_descriptor_flags(qa_native_process_resources *owner, uint64_t id,
    uint32_t flags, qa_error *error)
{
    resource_native_begin(owner);
    if (!qa_native_process_resources_current(owner, error)) return false;
    bool append = (flags & UINT32_C(1024)) != 0, nonblocking = (flags & UINT32_C(2048)) != 0;
    for (size_t i = 0; i < 3; ++i) if (owner->standards[i].capability == id)
        return qa_native_process_platform_descriptor_flags(owner->options.platform, id, append, nonblocking, error) &&
            qa_native_process_resources_current(owner, error);
    for (size_t i = 0; i < owner->file_count; ++i) {
        process_file *row = owner->files[i];
        if (row->id != id) continue;
        if (!row->ready || row->closed || !row->file)
            return fail(error, QA_ERROR_NOT_FOUND, "Actual native descriptor capability is closed");
        ++owner->busy;
        bool okay = qa_fs_opened_file_posix_flags(row->file, append, nonblocking, error);
        resource_native_filesystem(owner);
        if (okay) okay = qa_native_process_resources_current(owner, error);
        --owner->busy; return okay;
    }
    return fail(error, QA_ERROR_NOT_FOUND, "Actual native descriptor capability is absent");
}
static bool windows_open(void *context, const char *name, uint32_t mode, uint32_t creation,
    qa_native_windows_file *out, bool *opened, qa_error *error)
{ return qa_native_process_resources_open_file(context, name, mode, (qa_fs_opened_creation)creation, out, opened, error); }
static bool windows_resolve(void *context, uint64_t id, qa_native_windows_file *out, qa_error *error)
{ return qa_native_process_resources_resolve_file(context, id, out, error); }
static bool sysv_resolve(void *context, uint64_t id, qa_native_sysv_file *out, qa_error *error)
{ return qa_native_process_resources_resolve_sysv_file(context, id, out, error); }
static bool sysv_open(void *context, const char *name, uint32_t mode, uint32_t creation,
    qa_native_sysv_file *out, bool *opened, qa_error *error)
{ return qa_native_process_resources_open_sysv_file(context, name, mode, (qa_fs_opened_creation)creation, out, opened, error); }
bool qa_native_process_resources_root_add(qa_native_process_resources *owner,
    const qa_native_process_resource_root *source, uint64_t *out, qa_error *error)
{
    if (!source || !source->prefix || !source->root || !out || (source->mode & ~3u) ||
        !owner || owner->busy || !qa_native_process_resources_current(owner, error)) return false;
    size_t length = strlen(source->prefix);
    if (length && source->prefix[length - 1] != '/' && source->prefix[length - 1] != '\\')
        return fail(error, QA_ERROR_ARGUMENT, "Native root prefix must end at a directory boundary");
    for (size_t i = 0; i < owner->root_count; ++i)
        if (owner->roots[i].active && !strcmp(owner->roots[i].prefix, source->prefix))
            return fail(error, QA_ERROR_ARGUMENT, "Native directory namespace is already registered");
    if (owner->root_count == owner->root_capacity) {
        size_t count = owner->root_capacity ? owner->root_capacity * 2 : 4;
        if (count < owner->root_capacity || count > SIZE_MAX / sizeof(*owner->roots))
            return fail(error, QA_ERROR_MEMORY, "Native directory roster overflows");
        process_root *roots = realloc(owner->roots, count * sizeof(*roots));
        if (!roots) return fail(error, QA_ERROR_MEMORY, "Owning actual temporary directory namespace");
        owner->roots = roots; owner->root_capacity = count;
    }
    process_root row = {.prefix = text_copy(source->prefix), .root = source->root,
        .mode = source->mode, .active = true};
    if (!row.prefix) return fail(error, QA_ERROR_MEMORY, "Owning actual temporary directory prefix");
    if (!qa_fs_root_reference_read(row.root, &row.reference)) {
        free(row.prefix); return fail(error, QA_ERROR_ARGUMENT, "Temporary directory has no held native identity");
    }
    qa_fs_root_retain(row.root); owner->roots[owner->root_count++] = row;
    *out = owner->root_count; return true;
}
bool qa_native_process_resources_root_remove(qa_native_process_resources *owner, uint64_t id, qa_error *error)
{
    if (!owner || owner->busy || !id || id > owner->root_count ||
        !qa_native_process_resources_current(owner, error)) return false;
    size_t index = (size_t)(id - 1);
    for (size_t i = 0; i < owner->file_count; ++i)
        if (owner->files[i]->root == index && !owner->files[i]->closed)
            return fail(error, QA_ERROR_ARGUMENT, "Temporary directory still owns live source file rows");
    owner->roots[index].active = false; return true;
}
static bool acquisition_copy(const qa_vfs_acquisition *source, qa_vfs_acquisition *out)
{
    *out = (qa_vfs_acquisition){.mount = source->mount, .resource_id = source->resource_id,
        .path = text_copy(source->path), .lookup_path = text_copy(source->lookup_path),
        .link_source = text_copy(source->link_source), .link_target = text_copy(source->link_target)};
    return (!source->path || out->path) && (!source->lookup_path || out->lookup_path) &&
        (!source->link_source || out->link_source) && (!source->link_target || out->link_target);
}
static bool vector_copy(const char *const *source, size_t count, char ***out)
{
    if (!count) return true;
    if (!source || count > SIZE_MAX / sizeof(**out)) return false;
    *out = calloc(count, sizeof(**out)); if (!*out) return false;
    for (size_t i = 0; i < count; ++i) if (!source[i] || !((*out)[i] = text_copy(source[i]))) return false;
    return true;
}
static bool units_copy(const uint16_t *source, size_t count, uint16_t **out)
{
    if (!count) return true;
    if (!source || count > SIZE_MAX / sizeof(*source)) return false;
    *out = malloc(count * sizeof(*source));
    if (*out) memcpy(*out, source, count * sizeof(*source));
    return *out != NULL;
}
static bool resources_create(const qa_native_process_resources_options *options, bool program,
    bool has_interpreter, size_t interpreter,
    qa_native_process_resources **out, qa_error *error)
{
    if (!options || !out || *out || !options->descriptor || !options->current || !options->service_owner ||
        !options->receiver || !options->platform || !options->artifacts || !options->artifact_count ||
        options->primary >= options->artifact_count || !options->policy.maximum_image_bytes ||
        !options->policy.maximum_backing_bytes || !options->policy.stack_bytes || options->policy.stack_bytes % 4096 ||
        !options->policy.runtime_trap_bytes || options->policy.runtime_trap_bytes % 4096 ||
        (options->root_count && !options->roots) || options->artifact_count > SIZE_MAX / sizeof(process_artifact) ||
        options->root_count > SIZE_MAX / sizeof(process_root) ||
        (options->policy.backend != QA_NATIVE_GUEST_EMULATED && options->policy.backend != QA_NATIVE_GUEST_HOST_X86_64) ||
        (options->policy.backend == QA_NATIVE_GUEST_EMULATED ? !options->policy.instruction_budget : options->policy.instruction_budget != 0))
        return fail(error, QA_ERROR_ARGUMENT, "Native process resource graph lacks its actual policy or source authority");
    if (!options->current(options->context, options->descriptor, options->receiver, options->service_owner, error)) return false;
    qa_native_process_resources *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Owning native process resource graph");
    owner->references = 1; owner->options = *options; owner->failed = true;
    owner->raw_program = program; owner->has_interpreter = has_interpreter; owner->interpreter = interpreter;
    qa_native_process_platform_retain(options->platform);
    qa_native_runtime_retain(options->runtime); *out = owner;
    if (!qa_launch_instance_retain_metadata(options->descriptor, &owner->lease, error)) return false;
    owner->descriptor = qa_launch_instance_lease_view(owner->lease);
    owner->options.descriptor = owner->descriptor;
    owner->artifacts = calloc(options->artifact_count, sizeof(*owner->artifacts));
    owner->sysv_artifacts = calloc(options->artifact_count, sizeof(*owner->sysv_artifacts));
    owner->windows_artifacts = calloc(options->artifact_count, sizeof(*owner->windows_artifacts));
    owner->roots = options->root_count ? calloc(options->root_count, sizeof(*owner->roots)) : NULL;
    owner->root_capacity = options->root_count;
    if (!owner->artifacts || !owner->sysv_artifacts || !owner->windows_artifacts || (options->root_count && !owner->roots))
        return fail(error, QA_ERROR_MEMORY, "Owning native artifact and root inventories");
    uint64_t cursor = program && options->policy.backend == QA_NATIVE_GUEST_EMULATED ?
        QA_NATIVE_GUEST_PAGE : UINT64_C(65536);
    for (size_t i = 0; i < options->artifact_count; ++i) {
        const qa_native_process_resource_artifact *source = options->artifacts + i;
        process_artifact *row = owner->artifacts + i; owner->artifact_count = i + 1;
        if (!source->resource || !source->path || !qa_resource_id(source->resource))
            return fail(error, QA_ERROR_ARGUMENT, "Native artifact has no genuine acquired identity");
        row->resource = (qa_resource *)source->resource; qa_resource_retain(row->resource);
        row->path = text_copy(source->path);
        if (!row->path || !qa_native_inspect(qa_resource_bytes(row->resource), &row->image, error)) return false;
        if (i && !target_equal(&row->image.target, &owner->artifacts[0].image.target))
            return fail(error, QA_ERROR_ARGUMENT, "Native dependency target differs from the acquired process");
        for (size_t j = 0; j < i; ++j) if (qa_resource_id(owner->artifacts[j].resource) == qa_resource_id(row->resource))
            return fail(error, QA_ERROR_ARGUMENT, "Native acquired provider identity repeats");
        if (source->acquisition) {
            row->receipt = true;
            if (source->acquisition->resource_id != qa_resource_id(row->resource) ||
                !acquisition_copy(source->acquisition, &row->acquisition) ||
                !qa_vfs_acquisition_retained(owner->descriptor->content, &row->acquisition, error)) return false;
        }
        uint64_t alignment = QA_NATIVE_GUEST_PAGE, base = row->image.preferred_base;
        bool elf = row->image.format == QA_NATIVE_IMAGE_ELF32 || row->image.format == QA_NATIVE_IMAGE_ELF64;
        if (elf) {
            guest_elf *parsed = NULL;
            if (!guest_elf_open(qa_resource_bytes(row->resource), &row->image,
                program ? GUEST_ELF_PROGRAM : GUEST_ELF_LIBRARY, 0,
                options->policy.maximum_image_bytes, &parsed, error)) return false;
            const guest_elf_view *view = guest_elf_describe(parsed);
            bool fixed = view->executable;
            if (program && i == options->primary && view->interpreter) {
                owner->interpreter_path = text_copy(view->interpreter);
                if (!owner->interpreter_path) {
                    guest_elf_close(&parsed); return fail(error, QA_ERROR_MEMORY, "Owning actual PT_INTERP path");
                }
            }
            for (size_t j = 0; j < view->segment_count; ++j)
                if (view->segments[j].type == 1 && view->segments[j].alignment > alignment)
                    alignment = view->segments[j].alignment;
            guest_elf_close(&parsed);
            if (fixed && base < cursor)
                return fail(error, QA_ERROR_UNSUPPORTED, "Fixed ELF executable placement conflicts with the actual process namespace");
        } else alignment = 65536;
        if (!base || base < cursor) {
            if (elf) {
                uint64_t bias;
                if (!rounded(cursor - row->image.preferred_base, alignment, &bias) ||
                    row->image.preferred_base > UINT64_MAX - bias)
                    return fail(error, QA_ERROR_ARGUMENT, "Native ELF load bias overflows");
                base = row->image.preferred_base + bias;
            } else if (!rounded(cursor, alignment, &base)) {
                return fail(error, QA_ERROR_ARGUMENT, "Native image placement overflows");
            }
        }
        uint64_t bytes;
        if (!rounded(row->image.image_bytes, QA_NATIVE_GUEST_PAGE, &bytes) || base > UINT64_MAX - bytes ||
            (row->image.target.pointer_bytes == 4 && base + bytes > UINT32_MAX))
            return fail(error, QA_ERROR_ARGUMENT, "Native image placement exceeds its actual pointer domain");
        row->base = base; cursor = base + bytes;
        qa_bytes artifact = qa_resource_bytes(row->resource); uint64_t id = qa_resource_id(row->resource);
        owner->sysv_artifacts[i] = (qa_native_sysv_artifact){id, base - row->image.preferred_base,
            program ? QA_NATIVE_SYSV_PROGRAM : QA_NATIVE_SYSV_LIBRARY,
            row->image, artifact, options->policy.maximum_image_bytes};
        owner->windows_artifacts[i] = (qa_native_windows_artifact){id, base, row->image,
            artifact, options->policy.maximum_image_bytes, row->path};
    }
    if (!rounded(cursor, 65536, &owner->trap_base) ||
        owner->trap_base > UINT64_MAX - options->policy.runtime_trap_bytes ||
        !rounded(owner->trap_base + options->policy.runtime_trap_bytes, QA_NATIVE_GUEST_PAGE, &owner->allocation_base))
        return fail(error, QA_ERROR_ARGUMENT, "Native private allocation namespace overflows");
    const qa_native_image_info *primary = &owner->artifacts[options->primary].image;
    bool windows = primary->target.os == QA_NATIVE_OS_WINDOWS;
    if (!windows && primary->target.os != QA_NATIVE_OS_LINUX)
        return fail(error, QA_ERROR_UNSUPPORTED, "Acquired native target has no installed process owner");
    owner->first_callback = 1 + (windows ? 0 : options->policy.runtime_trap_bytes / 16);
    if (windows && owner->first_callback >= qa_native_windows_process_callback_minimum())
        return fail(error, QA_ERROR_ARGUMENT, "Native SDK callback range overlaps the actual Windows runtime");
    if (!owner->first_callback || (primary->target.pointer_bytes == 4 && owner->allocation_base > UINT32_MAX))
        return fail(error, QA_ERROR_ARGUMENT, "Native runtime callback or address namespace is absent");
    for (size_t i = 0; i < options->root_count; ++i) {
        const qa_native_process_resource_root *source = options->roots + i;
        process_root *row = owner->roots + i; owner->root_count = i + 1;
        if (!source->root || !source->prefix || (source->mode & ~3u))
            return fail(error, QA_ERROR_ARGUMENT, "Native file namespace lacks an acquired root");
        size_t length = strlen(source->prefix);
        if (length && source->prefix[length - 1] != '/' && source->prefix[length - 1] != '\\')
            return fail(error, QA_ERROR_ARGUMENT, "Native root prefix must end at a directory boundary");
        row->prefix = text_copy(source->prefix); row->root = source->root;
        qa_fs_root_retain(row->root); row->mode = source->mode; row->active = true;
        if (!row->prefix || !qa_fs_root_reference_read(row->root, &row->reference))
            return fail(error, QA_ERROR_MEMORY, "Owning acquired native directory authority");
    }
    if (!vector_copy(options->argv, options->argc, &owner->argv) ||
        !vector_copy(options->environment, options->environment_count, &owner->environment) ||
        !units_copy(options->command_line, options->command_line_units, &owner->command_line) ||
        !units_copy(options->windows_environment, options->windows_environment_units, &owner->windows_environment))
        return fail(error, QA_ERROR_MEMORY, "Owning actual source process arguments");
    if (options->policy.backend == QA_NATIVE_GUEST_HOST_X86_64) {
        if (!options->runtime || !options->bootstrap || !*options->bootstrap ||
            !qa_native_runtime_profile_launch(options->runtime, &owner->guard, error)) return false;
        owner->bootstrap_path = text_copy(options->bootstrap);
        if (!owner->bootstrap_path || !qa_fs_file_open(owner->bootstrap_path, &owner->bootstrap, &owner->bootstrap_identity, error)) return false;
    }
    bool terminal;
    if (!qa_native_process_platform_sysv_files(options->platform, owner->standards, &terminal, error) ||
        (program && !qa_native_process_platform_program_files(options->platform, owner->standards, error))) return false;
    owner->next_file = 1;
    for (size_t i = 0; i < 3; ++i) {
        if (owner->standards[i].capability == UINT64_MAX) return fail(error, QA_ERROR_ARGUMENT, "Native standard identity exhausts the file namespace");
        if (owner->next_file <= owner->standards[i].capability) owner->next_file = owner->standards[i].capability + 1;
    }
    qa_native_guest_options guest = {.image = *primary, .allocation_base = owner->allocation_base,
        .maximum_backing_bytes = options->policy.maximum_backing_bytes, .backend = options->policy.backend,
        .host_executable = owner->bootstrap_path,
        .profile_guard = options->policy.backend == QA_NATIVE_GUEST_HOST_X86_64 ? &owner->guard : NULL};
    owner->sysv = (qa_native_sysv_process_options){.guest = guest, .artifacts = owner->sysv_artifacts,
        .artifact_count = owner->artifact_count, .scope = options->service_owner, .first_function = 1,
        .trap_base = owner->trap_base, .trap_bytes = options->policy.runtime_trap_bytes,
        .stack_bytes = options->policy.stack_bytes, .instruction_budget = options->policy.instruction_budget,
        .anonymous_permissions = QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE,
        .argv = (const char *const *)owner->argv, .argc = options->argc,
        .environment = (const char *const *)owner->environment, .environment_count = options->environment_count,
        .files = owner->standards, .file_count = 3, .open_file = sysv_open, .file_context = owner,
        .clock_id = options->service_owner,
        .output_is_terminal = terminal, .time = platform_seconds, .current = current_callback, .context = owner};
    for (size_t i = 0; i < 3; ++i) owner->sysv.standard_handles[i] = owner->standards[i].handle;
    qa_native_windows_capabilities capabilities = {.id = options->service_owner,
        .entropy = platform_entropy, .milliseconds = platform_milliseconds, .performance = platform_performance,
        .performance_frequency = qa_native_process_platform_frequency(options->platform), .calendar = platform_calendar,
        .open_file = windows_open, .resolve_file = windows_resolve, .current = current_callback, .context = owner};
    if (!qa_native_process_platform_locale_read(options->platform,&capabilities.locale,error)) return false;
    capabilities.compare_string = capabilities.locale.source == 2 ? platform_compare_string : NULL;
    if (!qa_native_process_platform_windows_streams(options->platform, capabilities.streams, error)) return false;
    owner->windows = (qa_native_windows_process_options){.guest = guest, .artifacts = owner->windows_artifacts,
        .artifact_count = owner->artifact_count, .primary_image = qa_resource_id(owner->artifacts[options->primary].resource),
        .stack_bytes = options->policy.stack_bytes, .instruction_budget = options->policy.instruction_budget,
        .command_line = owner->command_line, .command_line_units = options->command_line_units,
        .environment = owner->windows_environment, .environment_units = options->windows_environment_units,
        .capabilities = capabilities};
    owner->sysv_restore = (qa_native_sysv_process_restore_bindings){.maximum_backing_bytes = options->policy.maximum_backing_bytes,
        .artifacts = owner->sysv_artifacts, .artifact_count = owner->artifact_count,
        .backend = options->policy.backend, .host_executable = owner->bootstrap_path, .profile_guard = guest.profile_guard,
        .clock_id = options->service_owner, .output_is_terminal = terminal, .time = platform_seconds,
        .current = current_callback, .context = owner, .file = sysv_resolve,
        .open_file = sysv_open, .file_context = owner,
        .external_callback = options->external_callback, .external_context = options->external_context};
    owner->windows_restore = (qa_native_windows_process_restore_bindings){.maximum_backing_bytes = options->policy.maximum_backing_bytes,
        .artifacts = owner->windows_artifacts, .artifact_count = owner->artifact_count,
        .backend = options->policy.backend, .host_executable = owner->bootstrap_path, .profile_guard = guest.profile_guard,
        .capabilities = capabilities, .external_callback = options->external_callback, .external_context = options->external_context};
    owner->options.artifacts = NULL; owner->options.roots = NULL;
    owner->options.argv = owner->options.environment = NULL;
    owner->options.command_line = owner->options.windows_environment = NULL;
    owner->options.bootstrap = NULL; owner->failed = false;
    return qa_native_process_resources_current(owner, error);
}
bool qa_native_process_resources_create(const qa_native_process_resources_options *options,
    qa_native_process_resources **out, qa_error *error)
{ return resources_create(options, false, false, 0, out, error); }
static bool program_status(void *context, uint64_t id, qa_fs_posix_status *out, qa_error *error)
{ return qa_native_process_resources_file_status(context, id, out, error); }
static bool program_identity(void *context, qa_native_process_linux_identity *out, qa_error *error)
{ return qa_native_process_resources_linux_identity_read(context, out, error); }
static bool program_descriptor(void *context, uint64_t id,
    qa_native_sysv_program_descriptor_status *out, qa_error *error)
{ return qa_native_process_resources_descriptor_status(context, id, out, error); }
static bool program_descriptor_flags(void *context, uint64_t id, uint32_t flags, qa_error *error)
{ return qa_native_process_resources_descriptor_flags(context, id, flags, error); }
static bool program_clock(void *context, int32_t id, int64_t *seconds, int32_t *nanoseconds, qa_error *error)
{ return qa_native_process_resources_linux_clock_read(context, id, seconds, nanoseconds, error); }
static bool program_native_error(const void *context, qa_fs_native_error *out)
{ return qa_native_process_resources_native_error_read(context, out); }
bool qa_native_process_resources_program_create(const qa_native_process_resources_options *options,
    const qa_native_process_resource_program *program, qa_native_process_resources **out, qa_error *error)
{
    if (!options || !program || !out || *out || program->anonymous_permissions > 7 ||
        options->artifact_count != (program->has_interpreter ? 2u : 1u) ||
        (program->has_interpreter && (program->interpreter >= options->artifact_count || program->interpreter == options->primary)) ||
        (program->has_interpreter && !program->interpreter_path) ||
        (!program->has_interpreter && program->interpreter_path) ||
        !program->random.data || program->random.size != 16 || !program->auxiliary ||
        !program->auxiliary_count || program->auxiliary_count > SIZE_MAX / sizeof(*program->auxiliary))
        return fail(error, QA_ERROR_ARGUMENT, "PROGRAM resource graph lacks its acquired kernel startup recipe");
    if (!resources_create(options, true, program->has_interpreter, program->interpreter, out, error)) return false;
    qa_native_process_resources *owner = *out; owner->failed = true;
    const process_artifact *primary = owner->artifacts + options->primary;
    if (primary->image.format != QA_NATIVE_IMAGE_ELF64 || primary->image.target.os != QA_NATIVE_OS_LINUX ||
        primary->image.target.abi != QA_NATIVE_ABI_SYSTEM_V_X64 ||
        (owner->interpreter_path != NULL) != program->has_interpreter ||
        (program->has_interpreter && (strcmp(owner->interpreter_path, program->interpreter_path) ||
            strcmp(owner->artifacts[program->interpreter].path, program->interpreter_path))))
        return fail(error, QA_ERROR_ARGUMENT, "PROGRAM role or declared PT_INTERP differs from its actual acquired graph");
    owner->program_auxiliary = malloc(program->auxiliary_count * sizeof(*program->auxiliary));
    owner->program_platform = text_copy(program->platform);
    owner->program_base_platform = text_copy(program->base_platform);
    if (!owner->program_auxiliary || (program->platform && !owner->program_platform) ||
        (program->base_platform && !owner->program_base_platform))
        return fail(error, QA_ERROR_MEMORY, "Owning actual PROGRAM kernel startup fields");
    memcpy(owner->program_auxiliary, program->auxiliary, program->auxiliary_count * sizeof(*program->auxiliary));
    memcpy(owner->program_random, program->random.data, 16);
    owner->program = (qa_native_sysv_program_options){.guest = owner->sysv.guest,
        .program = owner->sysv_artifacts[options->primary],
        .interpreter = program->has_interpreter ? owner->sysv_artifacts[program->interpreter] : (qa_native_sysv_artifact){0},
        .interpreter_path = owner->interpreter_path, .executed_path = primary->path,
        .platform = owner->program_platform, .base_platform = owner->program_base_platform,
        .stack_bytes = options->policy.stack_bytes, .instruction_budget = options->policy.instruction_budget,
        .anonymous_permissions = program->anonymous_permissions, .read_implies_execute = program->read_implies_execute,
        .argv = (const char *const *)owner->argv, .environment = (const char *const *)owner->environment,
        .argc = options->argc, .environment_count = options->environment_count,
        .random = {owner->program_random, 16}, .auxiliary = owner->program_auxiliary,
        .auxiliary_count = program->auxiliary_count, .process_id = program->process_id, .thread_id = program->thread_id,
        .services = {.id = options->service_owner, .current = current_callback, .open_file = sysv_open,
            .resolve_file = sysv_resolve, .file_status = program_status,
            .descriptor_status = program_descriptor, .descriptor_flags = program_descriptor_flags,
            .identity = program_identity,
            .entropy = platform_entropy, .clock = program_clock, .native_error = program_native_error, .context = owner}};
    memcpy(owner->program.standard_files, owner->standards, sizeof(owner->standards));
    owner->failed = false;
    return qa_native_process_resources_current(owner, error);
}
bool qa_native_process_resources_program_read(qa_native_process_resources *owner,
    qa_native_sysv_program_options *out, qa_error *error)
{
    if (!out || !owner || !owner->raw_program || !qa_native_process_resources_current(owner, error)) return false;
    if (owner->options.policy.backend == QA_NATIVE_GUEST_HOST_X86_64 &&
        !qa_native_runtime_profile_launch(owner->options.runtime, &owner->guard, error)) return false;
    *out = owner->program; return true;
}
bool qa_native_process_resources_program_restore_read(qa_native_process_resources *owner,
    qa_native_sysv_program_restore_bindings *out, qa_error *error)
{
    if (!out || !owner || !owner->raw_program || !qa_native_process_resources_current(owner, error)) return false;
    if (owner->options.policy.backend == QA_NATIVE_GUEST_HOST_X86_64 &&
        !qa_native_runtime_profile_launch(owner->options.runtime, &owner->guard, error)) return false;
    *out = (qa_native_sysv_program_restore_bindings){.backend = owner->options.policy.backend,
        .program = owner->program.program, .interpreter = owner->program.interpreter,
        .maximum_backing_bytes = owner->options.policy.maximum_backing_bytes,
        .maximum_image_bytes = owner->options.policy.maximum_image_bytes,
        .host_executable = owner->bootstrap_path, .profile_guard = owner->program.guest.profile_guard,
        .services = owner->program.services};
    return true;
}
static void source_resources_retain(void *context)
{ qa_native_process_resources_retain(context); }
static bool source_resources_release(void **context, qa_error *error)
{
    qa_native_process_resources *owner = *context;
    bool okay = qa_native_process_resources_release(&owner,error);
    *context = owner; return okay;
}
static bool source_root_add(void *context, const char *prefix, qa_fs_root *root, uint32_t mode,
    uint64_t *out, qa_error *error)
{
    const qa_native_process_resource_root authority = {prefix,root,mode};
    return qa_native_process_resources_root_add(context,&authority,out,error);
}
static bool source_root_remove(void *context, uint64_t id, qa_error *error)
{ return qa_native_process_resources_root_remove(context,id,error); }
static bool source_open_windows_file(void *context, const char *path, uint32_t mode,
    qa_fs_opened_creation creation, qa_native_windows_file *out, bool *opened, qa_error *error)
{ return qa_native_process_resources_open_file(context,path,mode,creation,out,opened,error); }
static bool source_open_sysv_file(void *context, const char *path, uint32_t mode,
    qa_fs_opened_creation creation, qa_native_sysv_file *out, bool *opened, qa_error *error)
{ return qa_native_process_resources_open_sysv_file(context,path,mode,creation,out,opened,error); }
bool qa_native_process_resources_options_read(qa_native_process_resources *owner, qa_native_process_options *out, qa_error *error)
{
    if (!out || !qa_native_process_resources_current(owner, error)) return false;
    if (owner->raw_program) return fail(error, QA_ERROR_ARGUMENT, "Raw PROGRAM uses its distinct kernel scheduler recipe");
    if (owner->options.policy.backend == QA_NATIVE_GUEST_HOST_X86_64 &&
        !qa_native_runtime_profile_launch(owner->options.runtime, &owner->guard, error)) return false;
    bool windows = owner->artifacts[owner->options.primary].image.target.os == QA_NATIVE_OS_WINDOWS;
    *out = (qa_native_process_options){.kind = windows ? QA_NATIVE_PROCESS_WINDOWS : QA_NATIVE_PROCESS_SYSV,
        .source_id = qa_resource_id(owner->artifacts[owner->options.primary].resource),
        .first_callback = owner->first_callback, .resources = {.context = owner,
            .retain = source_resources_retain, .release = source_resources_release,
            .root_add = source_root_add, .root_remove = source_root_remove,
            .open_windows_file = source_open_windows_file, .open_sysv_file = source_open_sysv_file}};
    if (windows) out->fresh.windows = &owner->windows;
    else out->fresh.sysv = &owner->sysv;
    return true;
}
bool qa_native_process_resources_restore_read(qa_native_process_resources *owner, qa_bytes continuation,
    qa_native_instance *previous, qa_native_process_options *out, qa_error *error)
{
    if (!continuation.data || !continuation.size || !qa_native_process_resources_options_read(owner, out, error)) return false;
    out->continuation = continuation; out->previous = previous;
    if (out->kind == QA_NATIVE_PROCESS_WINDOWS) out->restored.windows = &owner->windows_restore;
    else out->restored.sysv = &owner->sysv_restore;
    return true;
}
static bool match_u64(qa_source_save_io *io, uint64_t expected)
{ uint64_t value = expected; return qa_source_save_u64(io, &value) && value == expected; }
static bool match_u32(qa_source_save_io *io, uint32_t expected)
{ uint32_t value = expected; return qa_source_save_u32(io, &value) && value == expected; }
static bool match_bool(qa_source_save_io *io, bool expected)
{ bool value = expected; return qa_source_save_bool(io, &value) && value == expected; }
static bool match_bytes(qa_source_save_io *io, const void *bytes, size_t count)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)bytes, count);
    if (io->offset > io->input.size || count > io->input.size - io->offset ||
        (count && memcmp(io->input.data + io->offset, bytes, count))) return false;
    io->offset += count; return true;
}
static bool resource_text(qa_source_save_io *io,const char *expected,char **imported)
{
    bool present=expected!=NULL;
    if (imported) {
        if (!qa_source_save_bool(io,&present)) return false;
        if (!present) return true;
        size_t size=0;
        if (!qa_source_save_count(io,&size,SIZE_MAX-1) || io->offset>io->input.size ||
            size>io->input.size-io->offset || memchr(io->input.data+io->offset,0,size)) return false;
        char *text=malloc(size+1);
        if (!text) return fail(io->error,QA_ERROR_MEMORY,"Importing named native resource");
        memcpy(text,io->input.data+io->offset,size); text[size]=0; io->offset+=size;
        *imported=text; return true;
    }
    return match_bool(io,present) && (!present ||
        (match_u64(io,strlen(expected)) && match_bytes(io,expected,strlen(expected))));
}
static bool match_text(qa_source_save_io *io,const char *text)
{ return resource_text(io,text,NULL); }
static bool match_object(qa_source_save_io *io, const qa_fs_object_reference *reference)
{
    return match_u32(io, reference->platform) && match_u64(io, reference->words[0]) &&
        match_u64(io, reference->words[1]) && match_u64(io, reference->words[2]);
}
static bool resource_file_fields(qa_source_save_io *io,const process_file *row,process_file *imported)
{
    if (!imported) return match_u64(io,row->id) && match_u64(io,row->root) && match_text(io,row->name) &&
        match_bool(io,row->ready) && match_bool(io,row->closed) &&
        match_u32(io,row->reference.mode) && match_u32(io,row->reference.creation) &&
        (!row->ready || (match_object(io,&row->reference.root) && match_object(io,&row->reference.object)));
    uint64_t root=0; bool closed=true;
    if (!qa_source_save_u64(io,&imported->id) || !qa_source_save_u64(io,&root) || root>SIZE_MAX ||
        !resource_text(io,NULL,&imported->name) || !qa_source_save_bool(io,&imported->ready) ||
        !qa_source_save_bool(io,&closed) || !qa_source_save_u32(io,&imported->reference.mode) ||
        !qa_source_save_u32(io,&imported->reference.creation)) return false;
    imported->root=(size_t)root;
    if (!imported->name || !*imported->name || root>=imported->owner->root_count ||
        (imported->reference.mode & ~3u) || imported->reference.creation<1 || imported->reference.creation>5 ||
        (!imported->ready && !closed)) return false;
    if (imported->ready) {
        qa_fs_object_reference *references[2]={&imported->reference.root,&imported->reference.object};
        for (size_t i=0;i<2;++i)
            if (!qa_source_save_u32(io,&references[i]->platform) ||
                !qa_source_save_u64(io,&references[i]->words[0]) || !qa_source_save_u64(io,&references[i]->words[1]) ||
                !qa_source_save_u64(io,&references[i]->words[2])) return false;
        if (!object_equal(&imported->reference.root,&imported->owner->roots[imported->root].reference) ||
            imported->reference.object.platform!=imported->reference.root.platform) return false;
    }
    if (!closed) {
        const process_root *authority=&imported->owner->roots[imported->root];
        size_t prefix=strlen(authority->prefix);
        if (!authority->active || (imported->reference.mode & ~authority->mode) ||
            strncmp(imported->name,authority->prefix,prefix)) return false;
        char *path=text_copy(imported->name+prefix); bool opened=false;
        if (!path) return fail(io->error,QA_ERROR_MEMORY,"Importing contained native file name");
        bool windows=imported->owner->artifacts[imported->owner->options.primary].image.target.os==QA_NATIVE_OS_WINDOWS;
        if (windows) for (char *at=path;*at;++at) if (*at=='\\') *at='/';
        bool ok=qa_fs_root_opened_file(authority->root,path,imported->reference.mode,
            QA_FS_OPEN_EXISTING,&imported->file,&opened,io->error);
        free(path);
        if (imported->file) imported->closed=false;
        qa_fs_opened_reference actual={0};
        if (!ok || !opened || !qa_fs_opened_file_reference_read(imported->file,&actual) ||
            !object_equal(&actual.root,&imported->reference.root) ||
            !object_equal(&actual.object,&imported->reference.object)) return false;
        imported->reference.path=actual.path;
    }
    return true;
}
static bool resource_fields(qa_source_save_io *io, const qa_native_process_resources *owner, qa_native_process_resources *imported)
{
    if (!match_bytes(io, "QNPR", 4) ||
        !match_bytes(io, &owner->descriptor->identity, sizeof(owner->descriptor->identity)) ||
        !match_u32(io, owner->options.receiver) || !match_u64(io, owner->options.service_owner) ||
        !match_u32(io, owner->options.policy.backend) ||
        !match_u64(io, owner->options.policy.maximum_image_bytes) || !match_u64(io, owner->options.policy.maximum_backing_bytes) ||
        !match_u64(io, owner->options.policy.stack_bytes) || !match_u64(io, owner->options.policy.instruction_budget) ||
        !match_u64(io, owner->options.policy.runtime_trap_bytes) || !match_u64(io, owner->first_callback) ||
        !match_u64(io, owner->allocation_base) || !match_u64(io, owner->trap_base) ||
        !match_bool(io, owner->raw_program) || !match_bool(io, owner->has_interpreter) ||
        !match_u64(io, owner->interpreter) ||
        !match_u64(io, owner->options.primary) || !match_u64(io, owner->artifact_count)) return false;
    if (owner->raw_program) {
        if (!match_text(io, owner->interpreter_path) || !match_text(io, owner->program_platform) ||
            !match_text(io, owner->program_base_platform) || !match_bytes(io, owner->program_random, 16) ||
            !match_u32(io, owner->program.anonymous_permissions) || !match_bool(io, owner->program.read_implies_execute) ||
            !match_u64(io, owner->program.process_id) || !match_u64(io, owner->program.thread_id) ||
            !match_u64(io, owner->program.auxiliary_count)) return false;
        for (size_t i = 0; i < owner->program.auxiliary_count; ++i)
            if (!match_u64(io, owner->program_auxiliary[i].tag) ||
                !match_u64(io, owner->program_auxiliary[i].value)) return false;
    }
    for (size_t i = 0; i < owner->artifact_count; ++i) {
        const process_artifact *row = owner->artifacts + i;
        if (!match_u64(io, qa_resource_id(row->resource)) || !match_text(io, row->path) ||
            !match_bytes(io, &row->image.digest, sizeof(row->image.digest)) || !match_u64(io, row->base) ||
            !match_bool(io, row->receipt)) return false;
        if (row->receipt && (!match_u64(io, row->acquisition.mount) ||
            !match_u64(io, row->acquisition.resource_id) || !match_text(io, row->acquisition.path) ||
            !match_text(io, row->acquisition.lookup_path) || !match_text(io, row->acquisition.link_source) ||
            !match_text(io, row->acquisition.link_target))) return false;
    }
    if (!match_u64(io, owner->root_count)) return false;
    for (size_t i = 0; i < owner->root_count; ++i) {
        const process_root *row = owner->roots + i;
        if (!match_text(io, row->prefix) || !match_object(io, &row->reference) ||
            !match_u32(io, row->mode)) return false;
        if (imported) {
            if (!qa_source_save_bool(io,&imported->roots[i].active)) return false;
        } else if (!match_bool(io,row->active)) return false;
    }
    if (!match_u64(io, owner->options.argc)) return false;
    for (size_t i = 0; i < owner->options.argc; ++i) if (!match_text(io, owner->argv[i])) return false;
    if (!match_u64(io, owner->options.environment_count)) return false;
    for (size_t i = 0; i < owner->options.environment_count; ++i) if (!match_text(io, owner->environment[i])) return false;
    if (!match_u64(io, owner->options.command_line_units)) return false;
    for (size_t i = 0; i < owner->options.command_line_units; ++i) {
        uint16_t value = owner->command_line[i];
        if (!qa_source_save_u16(io, &value) || value != owner->command_line[i]) return false;
    }
    if (!match_u64(io, owner->options.windows_environment_units)) return false;
    for (size_t i = 0; i < owner->options.windows_environment_units; ++i) {
        uint16_t value = owner->windows_environment[i];
        if (!qa_source_save_u16(io, &value) || value != owner->windows_environment[i]) return false;
    }
    size_t count=owner->file_count;
    if (imported) {
        uint64_t first_file=owner->next_file;
        if (!qa_source_save_u64(io,&imported->next_file) || imported->next_file<first_file ||
            !qa_source_save_count(io,&count,SIZE_MAX/sizeof(*imported->files)) ||
            io->offset>io->input.size || count>(io->input.size-io->offset)/35) return false;
        imported->files=count?calloc(count,sizeof(*imported->files)):NULL;
        if (count && !imported->files) return fail(io->error,QA_ERROR_MEMORY,"Importing native file inventory");
        imported->file_capacity=count;
        uint64_t previous=first_file-1;
        for (size_t i=0;i<count;++i) {
            process_file *row=calloc(1,sizeof(*row));
            if (!row) return fail(io->error,QA_ERROR_MEMORY,"Importing native file capability row");
            imported->files[i]=row; imported->file_count=i+1; row->owner=imported; row->closed=true;
            if (!resource_file_fields(io,NULL,row) || row->id<=previous || row->id>=imported->next_file) return false;
            previous=row->id;
        }
    } else {
        if (!match_u64(io,owner->next_file) || !match_u64(io,count)) return false;
        for (size_t i=0;i<count;++i) if (!resource_file_fields(io,owner->files[i],NULL)) return false;
    }
    return true;
}

bool qa_native_process_resources_checkpoint(const qa_native_process_resources *owner, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !owner || owner->busy ||
        !(owner->captured ? resources_retained(owner, error) : qa_native_process_resources_current(owner, error))) return false;
    for (size_t i = 0; i < owner->file_count; ++i) {
        const process_file *row = owner->files[i];
        if (!row->closed && (!row->ready || !qa_fs_opened_file_current(row->file, error))) return false;
    }
    qa_buffer platform = {0}; qa_source_save_io io = {0};
    bool okay = qa_native_process_platform_checkpoint(owner->options.platform, &platform, error) &&
        qa_source_save_writer(&io, NULL, error) && resource_fields(&io, owner, NULL) &&
        match_u64(&io, platform.size) && match_bytes(&io, platform.data, platform.size) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&platform); return okay;
}
static bool resource_recipe_read(qa_native_process_resources *imported,
    const qa_native_process_resources *owner,qa_bytes bytes,qa_error *error)
{
    qa_source_save_io io={0}; size_t platform_size=0;
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && resource_fields(&io,owner,imported) &&
        qa_source_save_count(&io,&platform_size,SIZE_MAX) && io.offset<=io.input.size &&
        platform_size<=io.input.size-io.offset;
    if (ok) {
        qa_bytes platform={io.input.data+io.offset,platform_size}; io.offset+=platform_size;
        ok=imported?qa_native_process_platform_restore(imported->options.platform,platform,error):
            qa_native_process_platform_validate(owner->options.platform,platform,
                owner->file_cold && !owner->captured,error);
    }
    if (ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code==QA_OK))
        fail(error,QA_ERROR_FORMAT,"Native resource continuation differs from its prepared graph");
    return ok;
}
bool qa_native_process_resources_restore(const qa_native_process_resources_options *options,
    qa_bytes bytes,qa_native_process_resources **out,qa_error *error)
{
    if (!bytes.data || !bytes.size || !qa_native_process_resources_create(options,out,error)) return false;
    (*out)->file_cold=true;
    bool ok=resource_recipe_read(*out,*out,bytes,error);
    if (!ok) (*out)->failed=true;
    return ok;
}
bool qa_native_process_resources_validate(const qa_native_process_resources *owner, qa_bytes bytes, qa_error *error)
{
    return owner && !owner->busy && resources_retained(owner,error) && resource_recipe_read(NULL,owner,bytes,error);
}
static bool resource_copy(const qa_native_process_resources *source,
    qa_native_process_resources **out, qa_error *error)
{
    if (!source || !out || *out || source->busy || source->closing || source->failed)
        return fail(error, QA_ERROR_ARGUMENT, "Native resource capture requires its quiet retained owner");
    qa_native_process_resources *copy = calloc(1, sizeof(*copy));
    if (!copy) return fail(error, QA_ERROR_MEMORY, "Owning captured native resource graph");
    copy->options = source->options; copy->options.platform = NULL; copy->options.runtime = NULL;
    copy->references = 1; copy->failed = true; copy->captured = true;
    copy->next_file = source->next_file; copy->allocation_base = source->allocation_base;
    copy->trap_base = source->trap_base; copy->first_callback = source->first_callback; copy->guard = source->guard;
    copy->raw_program = source->raw_program; copy->has_interpreter = source->has_interpreter;
    copy->file_cold = source->file_cold;
    copy->interpreter = source->interpreter;
    *out = copy;
    if (!qa_launch_instance_retain_metadata(source->descriptor, &copy->lease, error)) return false;
    copy->descriptor = qa_launch_instance_lease_view(copy->lease); copy->options.descriptor = copy->descriptor;
    copy->options.runtime = source->options.runtime; qa_native_runtime_retain(copy->options.runtime);
    if (!qa_native_process_platform_capture(source->options.platform, &copy->options.platform, error)) return false;
    copy->bootstrap = source->bootstrap; qa_fs_file_retain(copy->bootstrap);
    copy->bootstrap_identity = source->bootstrap_identity; copy->bootstrap_path = text_copy(source->bootstrap_path);
    if (source->bootstrap_path && !copy->bootstrap_path) return fail(error, QA_ERROR_MEMORY, "Retaining actual child bootstrap name");
    copy->artifacts = calloc(source->artifact_count, sizeof(*copy->artifacts));
    copy->sysv_artifacts = calloc(source->artifact_count, sizeof(*copy->sysv_artifacts));
    copy->windows_artifacts = calloc(source->artifact_count, sizeof(*copy->windows_artifacts));
    copy->roots = source->root_count ? calloc(source->root_count, sizeof(*copy->roots)) : NULL;
    copy->root_capacity = source->root_count;
    copy->files = source->file_count ? calloc(source->file_count, sizeof(*copy->files)) : NULL;
    if (!copy->artifacts || !copy->sysv_artifacts || !copy->windows_artifacts ||
        (source->root_count && !copy->roots) || (source->file_count && !copy->files))
        return fail(error, QA_ERROR_MEMORY, "Retaining native captured inventories");
    for (size_t i = 0; i < source->artifact_count; ++i) {
        const process_artifact *old = source->artifacts + i; process_artifact *row = copy->artifacts + i;
        copy->artifact_count = i + 1;
        row->resource = old->resource; qa_resource_retain(row->resource);
        row->image = old->image; row->base = old->base; row->receipt = old->receipt;
        row->path = text_copy(old->path);
        if (!row->path || (row->receipt && !acquisition_copy(&old->acquisition, &row->acquisition)))
            return fail(error, QA_ERROR_MEMORY, "Retaining acquired native artifact receipt");
        copy->sysv_artifacts[i] = source->sysv_artifacts[i];
        copy->windows_artifacts[i] = source->windows_artifacts[i]; copy->windows_artifacts[i].path = row->path;
    }
    for (size_t i = 0; i < source->root_count; ++i) {
        process_root *row = copy->roots + i; const process_root *old = source->roots + i;
        copy->root_count = i + 1; *row = *old; row->prefix = text_copy(old->prefix); qa_fs_root_retain(row->root);
        if (!row->prefix) return fail(error, QA_ERROR_MEMORY, "Retaining acquired native root namespace");
    }
    copy->file_capacity = source->file_count;
    for (size_t i = 0; i < source->file_count; ++i) {
        const process_file *old = source->files[i]; process_file *row = calloc(1, sizeof(*row));
        if (!row) return fail(error, QA_ERROR_MEMORY, "Retaining actual opened native file row");
        *row = *old; row->owner = copy; row->name = text_copy(old->name);
        qa_fs_opened_file_retain(row->file); copy->files[copy->file_count++] = row;
        if (!row->name) return fail(error, QA_ERROR_MEMORY, "Retaining actual opened file name");
    }
    if (!vector_copy((const char *const *)source->argv, source->options.argc, &copy->argv) ||
        !vector_copy((const char *const *)source->environment, source->options.environment_count, &copy->environment) ||
        !units_copy(source->command_line, source->options.command_line_units, &copy->command_line) ||
        !units_copy(source->windows_environment, source->options.windows_environment_units, &copy->windows_environment))
        return fail(error, QA_ERROR_MEMORY, "Retaining actual source process argument rows");
    bool terminal;
    if (!qa_native_process_platform_sysv_files(copy->options.platform, copy->standards, &terminal, error) ||
        (copy->raw_program && !qa_native_process_platform_program_files(copy->options.platform, copy->standards, error))) return false;
    copy->sysv = source->sysv; copy->windows = source->windows;
    copy->sysv_restore = source->sysv_restore; copy->windows_restore = source->windows_restore;
    copy->sysv_restore.artifacts = copy->sysv_artifacts;
    copy->windows_restore.artifacts = copy->windows_artifacts;
    copy->sysv.artifacts = copy->sysv_artifacts; copy->sysv.files = copy->standards;
    copy->sysv.argv = (const char *const *)copy->argv; copy->sysv.environment = (const char *const *)copy->environment;
    copy->sysv.context = copy; copy->sysv.file_context = copy;
    copy->sysv.guest.host_executable = copy->bootstrap_path;
    copy->sysv.guest.profile_guard = source->sysv.guest.profile_guard ? &copy->guard : NULL;
    copy->windows.artifacts = copy->windows_artifacts; copy->windows.command_line = copy->command_line;
    copy->windows.environment = copy->windows_environment; copy->windows.guest = copy->sysv.guest;
    copy->windows.capabilities.context = copy;
    if (!qa_native_process_platform_locale_read(copy->options.platform,&copy->windows.capabilities.locale,error)) return false;
    if (!qa_native_process_platform_windows_streams(copy->options.platform, copy->windows.capabilities.streams, error)) return false;
    copy->sysv_restore.host_executable = copy->bootstrap_path; copy->sysv_restore.profile_guard = copy->sysv.guest.profile_guard;
    copy->sysv_restore.context = copy; copy->sysv_restore.file_context = copy;
    copy->windows_restore.host_executable = copy->bootstrap_path; copy->windows_restore.profile_guard = copy->sysv.guest.profile_guard;
    copy->windows_restore.capabilities = copy->windows.capabilities;
    if (source->raw_program) {
        copy->program = source->program;
        copy->interpreter_path = text_copy(source->interpreter_path);
        copy->program_platform = text_copy(source->program_platform);
        copy->program_base_platform = text_copy(source->program_base_platform);
        copy->program_auxiliary = malloc(source->program.auxiliary_count * sizeof(*copy->program_auxiliary));
        if (!copy->program_auxiliary || (source->interpreter_path && !copy->interpreter_path) ||
            (source->program_platform && !copy->program_platform) ||
            (source->program_base_platform && !copy->program_base_platform))
            return fail(error, QA_ERROR_MEMORY, "Retaining actual PROGRAM startup recipe");
        memcpy(copy->program_auxiliary, source->program_auxiliary,
            source->program.auxiliary_count * sizeof(*copy->program_auxiliary));
        memcpy(copy->program_random, source->program_random, 16);
        copy->program.guest = copy->sysv.guest;
        copy->program.program = copy->sysv_artifacts[copy->options.primary];
        copy->program.interpreter = copy->has_interpreter ? copy->sysv_artifacts[copy->interpreter] : (qa_native_sysv_artifact){0};
        copy->program.interpreter_path = copy->interpreter_path;
        copy->program.executed_path = copy->artifacts[copy->options.primary].path;
        copy->program.platform = copy->program_platform; copy->program.base_platform = copy->program_base_platform;
        copy->program.argv = (const char *const *)copy->argv;
        copy->program.environment = (const char *const *)copy->environment;
        copy->program.random = (qa_bytes){copy->program_random, 16};
        copy->program.auxiliary = copy->program_auxiliary; copy->program.services.context = copy;
        memcpy(copy->program.standard_files, copy->standards, sizeof(copy->standards));
    }
    copy->failed = false; return true;
}
bool qa_native_process_resources_capture(const qa_native_process_resources *source,
    qa_native_process_resources **out, qa_buffer *bytes, qa_error *error)
{
    if (!bytes || bytes->data || bytes->size || !qa_native_process_resources_current(source, error)) return false;
    if (!resource_copy(source, out, error)) return false;
    return qa_native_process_resources_checkpoint(*out, bytes, error);
}
bool qa_native_process_resources_rebind(const qa_native_process_resources *capture,
    const qa_native_process_resources_options *bindings, qa_bytes recipe,
    qa_native_process_resources **out, qa_error *error)
{
    if (!capture || !bindings || !bindings->descriptor || !bindings->current || !out || *out ||
        bindings->receiver != capture->options.receiver || bindings->service_owner != capture->options.service_owner ||
        !qa_sha256_equal(&bindings->descriptor->identity, &capture->descriptor->identity) ||
        !bindings->current(bindings->context, bindings->descriptor, bindings->receiver, bindings->service_owner, error))
        return fail(error, QA_ERROR_ARGUMENT, "Cold native resource binding differs from its prepared source");
    /* The capture's original execution row can already be retired. Its held
     * external objects remain real; no live old-source callback is required. */
    if (!resource_copy(capture, out, error)) return false;
    qa_native_process_resources *copy = *out;
    qa_launch_instance_lease *lease = NULL;
    if (!qa_launch_instance_retain_metadata(bindings->descriptor, &lease, error)) return false;
    qa_launch_instance_lease_release(copy->lease); copy->lease = lease;
    copy->descriptor = qa_launch_instance_lease_view(lease); copy->options.descriptor = copy->descriptor;
    copy->options.current = bindings->current; copy->options.context = bindings->context;
    copy->captured = false; copy->file_cold = false;
    copy->options.external_callback = bindings->external_callback; copy->options.external_context = bindings->external_context;
    copy->sysv_restore.external_callback = bindings->external_callback; copy->sysv_restore.external_context = bindings->external_context;
    copy->windows_restore.external_callback = bindings->external_callback; copy->windows_restore.external_context = bindings->external_context;
    return qa_native_process_resources_validate(copy, recipe, error);
}
void qa_native_process_resources_retain(qa_native_process_resources *owner)
{ if (owner) ++owner->references; }
bool qa_native_process_resources_release(qa_native_process_resources **pointer, qa_error *error)
{
    if (!pointer) return fail(error, QA_ERROR_ARGUMENT, "Native resource release requires its actual owner");
    qa_native_process_resources *owner = *pointer; if (!owner) return true;
    if (owner->busy) return fail(error, QA_ERROR_ARGUMENT, "Native resources are inside actual I/O");
    if (owner->references > 1) { --owner->references; *pointer = NULL; return true; }
    owner->closing = true;
    for (size_t i = 0; i < owner->file_count; ++i) if (!file_close(owner->files[i], error)) return false;
    if (!qa_native_process_platform_release(&owner->options.platform, error)) return false;
    for (size_t i = 0; i < owner->file_count; ++i) { free(owner->files[i]->name); free(owner->files[i]); }
    for (size_t i = 0; i < owner->artifact_count; ++i) {
        qa_resource_release(owner->artifacts[i].resource);
        qa_vfs_acquisition_dispose(&owner->artifacts[i].acquisition); free(owner->artifacts[i].path);
    }
    for (size_t i = 0; i < owner->root_count; ++i) { qa_fs_root_close(owner->roots[i].root); free(owner->roots[i].prefix); }
    for (size_t i = 0; i < owner->options.argc; ++i) if (owner->argv) free(owner->argv[i]);
    for (size_t i = 0; i < owner->options.environment_count; ++i) if (owner->environment) free(owner->environment[i]);
    qa_fs_file_close(owner->bootstrap); free(owner->bootstrap_path);
    qa_native_runtime_release(owner->options.runtime);
    qa_launch_instance_lease_release(owner->lease);
    free(owner->artifacts); free(owner->sysv_artifacts); free(owner->windows_artifacts); free(owner->roots); free(owner->files);
    free(owner->argv); free(owner->environment); free(owner->command_line); free(owner->windows_environment);
    free(owner->program_auxiliary); free(owner->interpreter_path);
    free(owner->program_platform); free(owner->program_base_platform);
    if (native_error_owner == owner) resource_native_begin(NULL);
    free(owner); *pointer = NULL; return true;
}
