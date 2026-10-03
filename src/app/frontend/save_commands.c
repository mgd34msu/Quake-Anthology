#include "internal.h"
#include "save_commands.h"
#include "save_private.h"
#include "tools_restore.h"
#include "campaign_cinematic.h"
#include "qa/frontend_save.h"
#include "qa/application_save_policy.h"
#include "qa/application_q1_save.h"
#include "qa/q1_save_product.h"
#include "qa/recovery.h"
#include <stdio.h>

typedef enum save_command_format { SAVE_SHARED, SAVE_Q1_V5, SAVE_Q1_V6 } save_command_format;
typedef struct save_command_request {
    bool load;
    save_command_format format;
    char *name, *product, *script;
    qa_command_context context;
} save_command_request;
struct frontend_save_commands {
    qa_fs_root *root;
    uint64_t next_nonce, command_registry;
    save_command_request request;
    qa_frontend_q1_restore *original;
    qa_frontend *retained[3];
    bool pending, draining;
};

static char *copy_text(const char *text, qa_error *error)
{
    if (!text) return NULL;
    size_t length = strlen(text);
    char *copy = length < SIZE_MAX ? malloc(length + 1) : NULL;
    if (!copy) { frontend_fail(error, QA_ERROR_MEMORY, "Retaining save command text"); return NULL; }
    memcpy(copy, text, length + 1); return copy;
}
static void request_free(save_command_request *request)
{
    free(request->name); free(request->product); free(request->script);
    *request = (save_command_request){0};
}
static bool cleanup_pending(const frontend_save_commands *owner)
{
    return owner->retained[0] || owner->retained[1] || owner->retained[2];
}
static bool cleanup_retained(frontend_save_commands *owner, qa_error *error)
{
    bool draining = owner->draining;
    owner->draining = true;
    for (size_t i = 0; i < 3; ++i) {
        if (owner->retained[i] && !qa_frontend_destroy(owner->retained[i], error)) {
            owner->draining = draining;
            return false;
        }
        owner->retained[i] = NULL;
    }
    owner->draining = draining;
    return true;
}
bool frontend_save_commands_create(qa_frontend *f, qa_error *error)
{
    const char *path = frontend_tools_output_root(f);
    if (!f || f->save_commands || !path || !*path)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Save commands require their actual tools directory");
    frontend_save_commands *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Creating save command owner");
    owner->next_nonce = 1;
    owner->command_registry = qa_actors_identity(qa_session_actors(qa_application_session(f->application)));
    if (!qa_fs_root_open(path, &owner->root, error)) { free(owner); return false; }
    f->save_commands = owner; return true;
}
bool frontend_save_commands_idle(const qa_frontend *f)
{ return f && (!f->save_commands || !f->save_commands->draining); }
qa_fs_root *frontend_save_commands_root(const qa_frontend *f)
{ return f && f->save_commands ? f->save_commands->root : NULL; }
bool frontend_save_commands_pending(const qa_frontend *f)
{ return f && f->save_commands && (f->save_commands->pending || f->save_commands->original || f->save_commands->draining || cleanup_pending(f->save_commands)); }
bool frontend_save_commands_restoring(const qa_frontend *f)
{ return f && f->save_commands && f->save_commands->original; }
bool frontend_save_commands_capture_ready(const qa_frontend *f)
{
    return f && f->save_commands && !cleanup_pending(f->save_commands)
        && (!f->save_commands->original || (f->save_commands->draining &&
            qa_frontend_q1_restore_capture_ready(f->save_commands->original)))
        && (!f->save_commands->draining || !f->save_commands->pending);
}
bool frontend_save_commands_destroy(qa_frontend *f, qa_error *error)
{
    if (!f || !f->save_commands) return true;
    frontend_save_commands *owner = f->save_commands;
    if (owner->draining)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Save command is using its frontend owner");
    if (!cleanup_retained(owner, error)) return false;
    if (owner->original) {
        qa_frontend_q1_restore *original = owner->original;
        owner->original = NULL; owner->draining = true;
        bool disposed = qa_frontend_q1_restore_dispose(original, &owner->retained[1], error);
        request_free(&owner->request); owner->pending = false;
        owner->draining = false;
        if (!disposed) return false;
    }
    request_free(&owner->request); qa_fs_root_close(owner->root);
    free(owner); f->save_commands = NULL; return true;
}

static char *slot_name(frontend_save_commands *owner, const char *input, qa_error *error)
{
    char *directory = NULL;
    if (!qa_fs_root_join(owner->root, "saves", &directory, error)) return NULL;
    const char *leaf = strrchr(input, '/'); leaf = leaf ? leaf + 1 : input;
    bool absolute = input[0] == '/';
#ifdef _WIN32
    const char *backslash = strrchr(input, '\\');
    if (backslash && backslash + 1 > leaf) leaf = backslash + 1;
    absolute = absolute || input[0] == '\\'
        || (((input[0] >= 'A' && input[0] <= 'Z') || (input[0] >= 'a' && input[0] <= 'z'))
            && input[1] == ':' && (input[2] == '/' || input[2] == '\\'));
    for (char *part = directory; *part; ++part) if (*part == '\\') *part = '/';
#endif
    const char *dot = strrchr(leaf, '.');
    bool extension = dot && dot != leaf && strcmp(leaf, "..");
    size_t length = strlen(input), prefix = absolute ? 0 : strlen(directory) + 1;
    size_t suffix = extension ? 0 : 4;
    if (length > SIZE_MAX - suffix - 1 || prefix > SIZE_MAX - length - suffix - 1) {
        free(directory); frontend_fail(error, QA_ERROR_MEMORY, "Save path exceeds address space"); return NULL;
    }
    char *resolved = malloc(prefix + length + suffix + 1);
    if (!resolved) { free(directory); frontend_fail(error, QA_ERROR_MEMORY, "Retaining save slot path"); return NULL; }
    if (prefix) { memcpy(resolved, directory, prefix - 1); resolved[prefix - 1] = '/'; }
    memcpy(resolved + prefix, input, length);
    if (suffix) memcpy(resolved + prefix + length, ".sav", 4);
    resolved[prefix + length + suffix] = 0;
#ifdef _WIN32
    for (char *part = resolved; *part; ++part) if (*part == '\\') *part = '/';
#endif
    size_t base = resolved[0] == '/' ? 1 : 0;
#ifdef _WIN32
    if (base && resolved[1] == '/') base = 2;
    else if (resolved[0] && resolved[1] == ':' && resolved[2] == '/') base = 3;
#endif
    size_t used = base;
    char *cursor = resolved + used;
    while (*cursor) {
        while (*cursor == '/') ++cursor;
        char *component = cursor;
        while (*cursor && *cursor != '/') ++cursor;
        size_t count = (size_t)(cursor - component);
        if (!count || (count == 1 && component[0] == '.')) continue;
        if (count == 2 && component[0] == '.' && component[1] == '.') {
            while (used > base && resolved[used - 1] != '/') --used;
            if (used > base) --used;
            continue;
        }
        if (used && resolved[used - 1] != '/') resolved[used++] = '/';
        memmove(resolved + used, component, count); used += count;
    }
    resolved[used] = 0;
    size_t directory_length = strlen(directory);
    if (strncmp(resolved, directory, directory_length) || resolved[directory_length] != '/') {
        free(resolved); free(directory);
        frontend_fail(error, QA_ERROR_ARGUMENT, "Save path is outside the save directory"); return NULL;
    }
    const char *name = resolved + directory_length + 1;
    size_t name_length = strlen(name);
    char *path = malloc(name_length + 7);
    if (!path) {
        free(resolved); free(directory);
        frontend_fail(error, QA_ERROR_MEMORY, "Retaining contained save slot path"); return NULL;
    }
    memcpy(path, "saves/", 6); memcpy(path + 6, name, name_length + 1);
    free(resolved); free(directory);
    if (!qa_save_slot_name(path, error)) { free(path); return NULL; }
    return path;
}
char *frontend_save_commands_slot_path(const qa_frontend *f, const char *name, qa_error *error)
{
    if (!f || !f->save_commands || !name || !*name) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Save slot requires its actual directory and name"); return NULL;
    }
    return slot_name(f->save_commands, name, error);
}
bool frontend_save_commands_queue(qa_frontend *f, const qa_command_invocation *command, qa_error *error)
{
    if (!f || !f->application || !f->save_commands || !command ||
        command->argc < 2 || command->argc > 3 || !command->argv ||
        !command->argv[0] || !command->argv[1] || !*command->argv[1] ||
        (command->argc == 3 && (!command->argv[2] || !*command->argv[2])))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Usage: save <name or path> [shared|v5|v6], load <name or path> [source-product]");
    frontend_save_commands *owner = f->save_commands;
    if (owner->pending || owner->original || owner->draining || cleanup_pending(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Another save operation is in progress");
    save_command_request request = {0};
    request.load = !strcmp(command->argv[0], "load");
    if (!request.load && strcmp(command->argv[0], "save"))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Unknown save command");
    if (!qa_application_capture_command_context(f->application, &command->context, &request.context, error)) return false;
    if (!request.load && command->argc == 3) {
        if (!strcmp(command->argv[2], "v5")) request.format = SAVE_Q1_V5;
        else if (!strcmp(command->argv[2], "v6")) request.format = SAVE_Q1_V6;
        else if (strcmp(command->argv[2], "shared"))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Save format must be shared, v5 or v6");
    }
    request.name = slot_name(owner, command->argv[1], error);
    if (!request.name) return false;
    request.script = copy_text(request.context.script, error);
    if (request.context.script && !request.script) { request_free(&request); return false; }
    request.context.script = request.script;
    if (request.load && command->argc == 3) {
        request.product = copy_text(command->argv[2], error);
        if (!request.product) { request_free(&request); return false; }
    }
    owner->request = request; owner->pending = true; return true;
}
static bool root_fields(qa_source_save_io *io, frontend_save_commands *owner)
{
    char *actual = NULL, *saved = NULL;
    qa_fs_identity identity = {{0}};
    qa_fs_entry_kind kind;
    bool ok = qa_fs_root_join(owner->root, "", &actual, io->error)
        && qa_fs_root_status(owner->root, "", &kind, &identity, io->error)
        && kind == QA_FS_DIRECTORY;
    uint64_t volume = identity.words[0], object = identity.words[1];
    if (io->direction == QA_SOURCE_SAVE_WRITE) saved = actual;
    ok = ok && frontend_save_text(io, &saved)
        && qa_source_save_u64(io, &volume) && qa_source_save_u64(io, &object);
    if (ok && io->direction == QA_SOURCE_SAVE_READ)
        ok = saved && !strcmp(saved, actual) && volume == identity.words[0] && object == identity.words[1];
    if (io->direction == QA_SOURCE_SAVE_READ) free(saved);
    free(actual);
    return ok || frontend_fail(io->error, QA_ERROR_FORMAT, "Saved commands name another actual writable root");
}
static bool fields(qa_source_save_io *io, frontend_save_commands *owner)
{
    uint8_t magic[4] = {'Q','F','S','C'}; uint64_t registry = owner->command_registry;
    bool ok = qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QFSC", 4)
        && root_fields(io, owner)
        && qa_source_save_u64(io, &registry) && registry
        && qa_source_save_u64(io, &owner->next_nonce) && owner->next_nonce
        && qa_source_save_bool(io, &owner->pending);
    if (ok && io->direction == QA_SOURCE_SAVE_READ) owner->command_registry = registry;
    if (!ok || !owner->pending) return ok;
    save_command_request *request = &owner->request;
    uint32_t format = request->format;
    ok = qa_source_save_bool(io, &request->load) && qa_source_save_u32(io, &format)
        && format <= SAVE_Q1_V6 && (!request->load || format == SAVE_SHARED)
        && frontend_save_text(io, &request->name) && request->name && qa_save_slot_name(request->name, io->error)
        && !strncmp(request->name, "saves/", 6) && frontend_save_text(io, &request->product)
        && (!request->product || (request->load && *request->product))
        && frontend_save_command_context(io, &request->context, &request->script, registry);
    if (ok) request->format = (save_command_format)format;
    return ok;
}
bool frontend_save_commands_checkpoint(qa_frontend *f, qa_buffer *out, qa_error *error)
{
    if (!frontend_save_commands_capture_ready(f) || !f->application)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Save command continuation has pending source preparation or native cleanup");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, qa_application_session(f->application), error)
        && fields(&io, f->save_commands) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_save_commands_restore(qa_frontend *f, qa_bytes bytes, qa_error *error)
{
    if (!f || !f->source_restoring || !f->application || !frontend_save_commands_create(f, error)) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(f->application), bytes, error)
        && fields(&io, f->save_commands) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { (void)frontend_save_commands_destroy(f, NULL); return false; }
    return true;
}
static bool restore_saved(qa_frontend **slot, frontend_save_commands *owner,
    const save_command_request *request, qa_save_image **image, qa_q1_save_data **source,
    qa_frontend **displaced, qa_frontend **retained_candidate,
    qa_error *error)
{
    if (!qa_saved_game_read(owner->root, request->name, image, source, error)) return false;
    qa_frontend *f = *slot;
    if (*image) return qa_frontend_persistence_restore(slot, f->options.persistence_services,
        *image, displaced, retained_candidate, error);
    char *path = NULL; const qa_product *product = NULL;
    bool ok = qa_fs_root_join(owner->root, request->name, &path, error) &&
        qa_q1_save_select_product(qa_application_catalog(f->application), *source, path,
            request->product, &product, error);
    free(path);
    return ok && qa_frontend_q1_restore_begin(f, f->options.persistence_services, *source,
        product->key, &owner->original, error);
}
static bool write_original(qa_frontend *f, frontend_save_commands *owner,
    const save_command_request *request, uint64_t nonce, qa_q1_save_data **source, qa_error *error)
{
    const char *leaf = strrchr(request->name, '/');
    leaf = leaf ? leaf + 1 : request->name;
    char *comment = copy_text(leaf, error);
    if (!comment) return false;
    size_t length = strlen(comment);
    if (length >= 4 && !strcmp(comment + length - 4, ".sav")) comment[length - 4] = 0;
    bool ok = qa_application_q1_save_capture(f->application,
        request->format == SAVE_Q1_V5 ? 5 : 6, comment, source, error) &&
        qa_q1_save_write(owner->root, request->name, *source, nonce, error);
    free(comment);
    return ok;
}
bool frontend_save_commands_drain(qa_frontend **slot, qa_error *error)
{
    if (!slot || !*slot || (*slot)->stepping || (*slot)->preparing)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Save commands require a completed frontend frame");
    qa_frontend *f = *slot;
    frontend_save_commands *owner = f->save_commands;
    if (!owner) return frontend_fail(error, QA_ERROR_ARGUMENT, "Frontend save command owner is absent");
    if (owner->draining) return frontend_fail(error, QA_ERROR_ARGUMENT, "Save command reentry");
    qa_error cleanup = {0};
    if (!cleanup_retained(owner, &cleanup)) return true;
    if (!owner->pending) return true;
    save_command_request request = owner->request;
    owner->pending = false; owner->draining = true;
    qa_save_image *image = NULL;
    qa_q1_save_data *source = NULL;
    qa_frontend *displaced = NULL, *retained_source = NULL, *retained_candidate = NULL;
    qa_error local = {0};
    bool ok = qa_application_command_context_active(f->application, &request.context);
    if (!ok) frontend_fail(&local, QA_ERROR_ARGUMENT, "Save command belongs to a retired world");
    if (ok) ok = qa_application_save_policy(f->application, frontend_network_save_authority(f),
        f->options.dedicated, request.load, QA_SAVE_MANUAL, &local);
    if (ok && !request.load && request.format != SAVE_SHARED && !frontend_cinematic_capture_ready(f))
        ok = frontend_fail(&local, QA_ERROR_ARGUMENT, "Original save export cannot capture standalone cinematic playback");
    bool complete = !owner->original;
    if (owner->original) {
        if (ok) ok = qa_frontend_q1_restore_advance(owner->original, slot, &complete,
            &displaced, &retained_candidate, &local);
    } else if (ok && request.load) {
        ok = restore_saved(slot, owner, &request, &image, &source, &displaced,
            &retained_candidate, &local);
        if (ok && owner->original) complete = false;
    } else if (ok) {
        if (owner->next_nonce == UINT64_MAX) ok = frontend_fail(&local, QA_ERROR_ARGUMENT, "Save write sequence exhausted");
        else {
            uint64_t nonce = owner->next_nonce++;
            if (request.format == SAVE_SHARED)
                ok = qa_frontend_persistence_capture(f, f->options.persistence_services, QA_SAVE_MANUAL, &image, &local)
                    && qa_save_write(owner->root, request.name, image, nonce, &local);
            else ok = write_original(f, owner, &request, nonce, &source, &local);
        }
    }
    if (!frontend_save_image_release(f,&image,ok?&local:&cleanup)) ok=false;
    qa_q1_save_destroy(source);
    if (ok && !complete) {
        owner->pending = true; owner->draining = false;
        return true;
    }
    if (owner->original) {
        qa_frontend_q1_restore *original = owner->original;
        owner->original = NULL;
        (void)qa_frontend_q1_restore_dispose(original, &retained_source, &cleanup);
    }
    owner->request = (save_command_request){0}; owner->pending = false;
    owner->draining = false;
    frontend_save_commands *current = (*slot)->save_commands;
    current->retained[0] = displaced;
    current->retained[1] = retained_source;
    current->retained[2] = retained_candidate;
    current->draining = true;
    char message[512];
    (void)snprintf(message, sizeof message, "%s%s.\n", ok ? request.load ? "Loaded " : "Saved " : "Save/load failed: ",
        ok ? request.name : local.message);
    qa_console_emit(qa_application_console((*slot)->application), ok && request.load ? NULL : &request.context, message);
    request_free(&request);
    current->draining = false;
    (void)cleanup_retained(current, &cleanup);
    return true;
}
