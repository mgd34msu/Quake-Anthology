#include "internal.h"
#include "capture.h"
#include "save_commands.h"
#include "save_private.h"
#include "tools_restore.h"
#include "persistence.h"
#include "config_store.h"
#include "network_session.h"
#include "remote_q1_client.h"
#include "qc_messages.h"
#include "campaign_cinematic.h"
#include "qa/frontend_save.h"
#include "qa/application_save_policy.h"
#include "qa/application_q1_save.h"
#include "qa/application_q2_save.h"
#include "qa/q1_save_product.h"
#include "qa/recovery.h"
#include "qa/application_startup_prepare.h"
#include <stdio.h>

typedef struct save_command_request {
    bool load;
    char *name, *script;
    qa_command_context context;
} save_command_request;
typedef enum save_failed_operation {
    SAVE_FAILURE_NONE, SAVE_FAILURE_MANUAL, SAVE_FAILURE_CAMPAIGN
} save_failed_operation;
struct frontend_save_commands {
    qa_fs_root *root;
    qa_fs_root *level_root;
    qa_recovery *recovery;
    qa_autosave_state autosave;
    uint64_t next_nonce, command_registry;
    save_command_request request;
    qa_frontend_original_restore *original;
    qa_save_image *campaign;
    qa_frontend *retained[3];
    qa_error failure;
    save_failed_operation failed_operation;
    bool pending, draining;
    bool recovery_checked,recovery_available,recovery_pending,recovery_resume;
    bool recovery_abandoned;
};

typedef enum recovery_journal_kind { RECOVERY_COMMAND, RECOVERY_FRAME } recovery_journal_kind;
typedef struct recovery_command {
    uint32_t seat,kind,dialect,origin;
    uint64_t wall_ns,time_ns,frame_number;
    char *instance,*text;
    bool direct,console_text;
} recovery_command;
static bool recovery_inspect(qa_frontend *,qa_error *);
static bool recovery_start(qa_frontend *,qa_error *);
static bool recovery_drain(qa_frontend **,qa_error *);
static bool recovery_checkpoint(qa_frontend *f,const qa_save_image *image,qa_error *error)
{
    frontend_save_commands *owner=f->save_commands;
    bool ok=owner->recovery?qa_recovery_checkpoint(owner->recovery,image,error):
        qa_recovery_begin(owner->level_root,"recovery.qdemo",image,&owner->recovery,error);
    if (ok) {
        owner->recovery_abandoned=false;
    }
    return ok;
}

static bool command_fields(qa_source_save_io *io,recovery_command *command)
{
    return qa_source_save_u32(io,&command->seat) &&
        qa_source_save_u32(io,&command->kind) && command->kind<=QA_APPLICATION_CONSOLE_CLIENT &&
        qa_source_save_u32(io,&command->dialect) && command->dialect<=QA_CONSOLE_Q3 &&
        qa_source_save_u32(io,&command->origin) && command->origin<=QA_COMMAND_SEAT &&
        qa_source_save_u64(io,&command->wall_ns) && qa_source_save_u64(io,&command->time_ns) &&
        qa_source_save_u64(io,&command->frame_number) &&
        qa_source_save_owned_text(io,&command->instance) && command->instance &&
        qa_source_save_owned_text(io,&command->text) && command->text && *command->text &&
        qa_source_save_bool(io,&command->direct) && qa_source_save_bool(io,&command->console_text);
}
static bool frame_fields(qa_source_save_io *io,frontend_replay_timing *frame)
{
    return qa_source_save_u64(io,&frame->wall_ns) && qa_source_save_u64(io,&frame->time_ns) &&
        qa_source_save_u64(io,&frame->duration_ns) && qa_source_save_u64(io,&frame->frame_before) &&
        qa_source_save_u64(io,&frame->frame_after) && qa_source_save_bool(io,&frame->advanced) &&
        qa_source_save_bool(io,&frame->completed);
}
static void recovery_fault(qa_frontend *f,const qa_error *error)
{
    frontend_save_commands *owner=f->save_commands;
    if (owner->recovery_abandoned) return;
    owner->recovery_abandoned=true;
    frontend_console_print(f,NULL,"Crash recovery recording stopped: ");
    frontend_console_print(f,NULL,error->message);
    frontend_console_print(f,NULL,"\n");
}
void frontend_save_commands_recovery_abandon(qa_frontend *f)
{ if (f && f->save_commands) f->save_commands->recovery_abandoned=true; }
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
    free(request->name); free(request->script);
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
uint64_t frontend_save_commands_registry(const qa_frontend *f)
{ return f && f->save_commands ? f->save_commands->command_registry : 0; }
qa_fs_root *frontend_save_commands_root(const qa_frontend *f)
{ return f && f->save_commands ? f->save_commands->root : NULL; }
bool frontend_save_commands_pending(const qa_frontend *f)
{ return f && f->save_commands && (f->save_commands->pending || f->save_commands->original || f->save_commands->campaign || f->save_commands->failed_operation!=SAVE_FAILURE_NONE || f->save_commands->recovery_pending || f->save_commands->draining || cleanup_pending(f->save_commands)); }
bool frontend_save_commands_restoring(const qa_frontend *f)
{ return f && f->save_commands && (f->save_commands->original || f->save_commands->campaign || f->save_commands->failed_operation!=SAVE_FAILURE_NONE || cleanup_pending(f->save_commands)); }
bool frontend_save_commands_capture_ready(const qa_frontend *f)
{
    return f && f->save_commands && !cleanup_pending(f->save_commands)
        && f->save_commands->failed_operation==SAVE_FAILURE_NONE
        && !f->save_commands->original
        && (!f->save_commands->draining || !f->save_commands->pending);
}
bool frontend_save_commands_destroy(qa_frontend *f, qa_error *error)
{
    if (!f || !f->save_commands) return true;
    frontend_save_commands *owner = f->save_commands;
    if (owner->draining)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Save command is using its frontend owner");
    if (!cleanup_retained(owner, error)) return false;
    if (!qa_save_image_destroy_checked(&owner->campaign,error)) return false;
    if (owner->original) {
        qa_frontend_original_restore *original = owner->original;
        owner->original = NULL; owner->draining = true;
        bool disposed = qa_frontend_original_restore_dispose(original, &owner->retained[1], error);
        request_free(&owner->request); owner->pending = false;
        owner->draining = false;
        if (!disposed) return false;
    }
    if (owner->recovery && !owner->recovery_abandoned) {
        qa_error local={0};
        if (!qa_recovery_close_clean(owner->recovery,&local)) recovery_fault(f,&local);
    }
    qa_recovery_destroy(owner->recovery);
    request_free(&owner->request); qa_fs_root_close(owner->level_root); qa_fs_root_close(owner->root);
    free(owner); f->save_commands = NULL; return true;
}

static bool absolute_path(const char *input)
{
    if (input[0]=='/') return true;
#ifdef _WIN32
    return input[0]=='\\' ||
        (((input[0]>='A' && input[0]<='Z') || (input[0]>='a' && input[0]<='z')) &&
            input[1]==':' && (input[2]=='/' || input[2]=='\\'));
#else
    return false;
#endif
}
static bool path_extension(const char *input)
{
    const char *leaf=strrchr(input,'/');leaf=leaf?leaf+1:input;
#ifdef _WIN32
    const char *backslash=strrchr(input,'\\');
    if (backslash && backslash+1>leaf) leaf=backslash+1;
#endif
    const char *dot=strrchr(leaf,'.');
    return dot && dot!=leaf && strcmp(leaf,"..");
}
static char *slot_name(frontend_save_commands *owner, const char *input, bool load, qa_error *error)
{
    char *directory = NULL;
    if (!qa_fs_root_join(owner->root, "saves", &directory, error)) return NULL;
    bool absolute=absolute_path(input);
#ifdef _WIN32
    for (char *part = directory; *part; ++part) if (*part == '\\') *part = '/';
#endif
    size_t length = strlen(input), prefix = absolute ? 0 : strlen(directory) + 1;
    size_t suffix=load || path_extension(input)?0:4;
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
        if (load && absolute_path(resolved)) { free(directory);return resolved; }
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
    if (!load && !qa_save_slot_name(path, error)) { free(path); return NULL; }
    return path;
}
char *frontend_save_commands_slot_path(const qa_frontend *f, const char *name, qa_error *error)
{
    if (!f || !f->save_commands || !name || !*name) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Save slot requires its actual directory and name"); return NULL;
    }
    return slot_name(f->save_commands, name, false, error);
}
bool frontend_save_commands_queue(qa_frontend *f, const qa_command_invocation *command, qa_error *error)
{
    if (!f || !f->application || !f->save_commands || !command ||
        command->argc != 2 || !command->argv ||
        !command->argv[0] || !command->argv[1] || !*command->argv[1])
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Usage: save <name or path>, load <name or path>");
    frontend_save_commands *owner = f->save_commands;
    if (owner->pending || owner->original || owner->draining || cleanup_pending(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Another save operation is in progress");
    save_command_request request = {0};
    request.load = !strcmp(command->argv[0], "load");
    if (!request.load && strcmp(command->argv[0], "save"))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Unknown save command");
    if (!qa_application_capture_command_context(f->application, &command->context, &request.context, error)) return false;
    request.name = slot_name(owner, command->argv[1], request.load, error);
    if (!request.name) return false;
    request.script = copy_text(request.context.script, error);
    if (request.context.script && !request.script) { request_free(&request); return false; }
    request.context.script = request.script;
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
    ok = ok && qa_source_save_owned_text(io, &saved)
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
    ok = qa_source_save_bool(io, &request->load)
        && qa_source_save_owned_text(io, &request->name) && request->name &&
        ((request->load && absolute_path(request->name)) ||
            (qa_save_slot_name(request->name,io->error) && !strncmp(request->name,"saves/",6)))
        && frontend_save_command_context(io, &request->context, &request->script, registry);
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

static bool recovery_directory(frontend_save_commands *owner,bool create,qa_error *error)
{
    if (owner->level_root) return true;
    qa_fs_entry_kind kind;
    if (!qa_fs_root_status(owner->root,"saves",&kind,NULL,error)) return false;
    if (kind==QA_FS_MISSING && !create) return true;
    char *path=NULL;
    bool ok=(kind!=QA_FS_MISSING || qa_fs_root_create_directory(owner->root,"saves",error)) &&
        qa_fs_root_join(owner->root,"saves",&path,error) && qa_fs_root_open(path,&owner->level_root,error);
    free(path);return ok;
}
static bool recovery_inspect(qa_frontend *f,qa_error *error)
{
    frontend_save_commands *owner=f->save_commands;
    if (owner->recovery_checked || owner->recovery || f->options.dedicated ||
        frontend_network_save_authority(f)!=QA_SAVE_OFFLINE) return true;
    owner->recovery_checked=true;
    if (!recovery_directory(owner,false,error) || (owner->level_root &&
        !qa_recovery_available(owner->level_root,"recovery.qdemo",&owner->recovery_available,error))) {
        recovery_fault(f,error);return false;
    }
    if (owner->recovery_available)
        frontend_console_print(f,NULL,"An interrupted offline session is available in Load Game. Choose Recover interrupted session or Discard interrupted session.\n");
    return true;
}
bool frontend_save_commands_recovery_available(qa_frontend *f,bool *available,qa_error *error)
{
    if (!f || !f->save_commands || !available)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Recovery choices require their actual save owner");
    if (!recovery_inspect(f,error)) return false;
    *available=f->save_commands->recovery_available;return true;
}
bool frontend_save_commands_recovery_queue(qa_frontend *f,bool resume,qa_error *error)
{
    bool available=false;
    if (!frontend_save_commands_recovery_available(f,&available,error)) return false;
    if (!available || frontend_save_commands_pending(f) || f->options.dedicated ||
        !qa_application_save_policy(f->application,frontend_network_save_authority(f),false,true,QA_SAVE_RECOVERY,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Recovery requires an available interrupted offline session");
    f->save_commands->recovery_pending=true;f->save_commands->recovery_resume=resume;return true;
}
static bool recovery_start(qa_frontend *f,qa_error *error)
{
    frontend_save_commands *owner=f->save_commands;
    if (!owner || owner->recovery ||
        owner->recovery_available || owner->recovery_abandoned ||
        f->options.dedicated || f->source_restoring || frontend_network_save_authority(f)!=QA_SAVE_OFFLINE ||
        qa_application_startup_pending(f->application) || qa_application_should_stop(f->application) ||
        qa_application_get_state(f->application)!=QA_APPLICATION_RUNNING) return true;
    qa_error local={0},cleanup={0};qa_save_image *image=NULL;
    if (!qa_application_save_policy(f->application,QA_SAVE_OFFLINE,false,false,QA_SAVE_RECOVERY,&local)) return true;
    owner->draining=true;
    bool ok=recovery_directory(owner,true,&local) &&
        qa_frontend_persistence_capture(f,f->options.persistence_services,QA_SAVE_RECOVERY,&image,&local);
    if (ok) ok=recovery_checkpoint(f,image,&local);
    if (!frontend_save_image_release(f,&image,ok?&local:&cleanup)) ok=false;
    owner->draining=false;
    if (!ok) recovery_fault(f,&local);
    (void)error;return true;
}
typedef struct recovery_candidate {
    qa_save_image *image;
    qa_demo_record *records;
    size_t count,capacity;
} recovery_candidate;
typedef struct recovery_restore {
    qa_frontend **slot;
    qa_frontend *displaced,*retained;
} recovery_restore;
static void recovery_candidate_free(recovery_candidate *candidate)
{
    if (!candidate) return;
    (void)qa_save_image_destroy_checked(&candidate->image,NULL);
    free(candidate->records);free(candidate);
}
static bool recovery_create(void *context,const qa_save_image *image,void **out,qa_error *error)
{
    (void)context;
    recovery_candidate *candidate=calloc(1,sizeof(*candidate));qa_bytes bytes={0};
    if (!candidate) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining recovery checkpoint");
    bool ok=qa_save_image_encode(image,&bytes,error) &&
        qa_save_image_decode(bytes,&candidate->image,error);
    if (!ok) { recovery_candidate_free(candidate);return false; }
    *out=candidate;return true;
}
static bool recovery_collect(void *context,void *value,const qa_demo_record *record,qa_error *error)
{
    (void)context;recovery_candidate *candidate=value;
    if (record->protocol.kind!=QA_NET_UNIFIED_1 || record->protocol.revision || record->protocol.flags ||
        (record->kind!=QA_DEMO_INPUT && record->kind!=QA_DEMO_JOURNAL && record->kind!=QA_DEMO_ADVANCE))
        return frontend_fail(error,QA_ERROR_FORMAT,"Recovery contains another command protocol");
    if (candidate->count==candidate->capacity) {
        size_t capacity=candidate->capacity?candidate->capacity*2:64;
        if (capacity<candidate->capacity || capacity>SIZE_MAX/sizeof(*candidate->records))
            return frontend_fail(error,QA_ERROR_MEMORY,"Recovery record inventory exceeds memory");
        qa_demo_record *records=realloc(candidate->records,capacity*sizeof(*records));
        if (!records) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining completed recovery records");
        candidate->records=records;candidate->capacity=capacity;
    }
    candidate->records[candidate->count++]=*record;return true;
}
static bool recovery_finish(void *context,void *value,qa_error *error)
{
    (void)context;recovery_candidate *candidate=value;
    return !candidate->count || candidate->records[candidate->count-1].kind==QA_DEMO_ADVANCE ||
        frontend_fail(error,QA_ERROR_FORMAT,"Recovery ends inside an unfinished frame");
}
static bool recovery_console(qa_frontend *f,const recovery_command *saved,qa_console **out,
    qa_command_context *command,qa_error *error)
{
    size_t count=qa_application_console_count(f->application);
    for (size_t i=0;i<count;++i) {
        qa_console *console=qa_application_console_at(f->application,i,NULL);
        qa_application_console_scope scope;
        if (!qa_application_console_scope_read(f->application,console,&scope) ||
            (uint32_t)scope.kind!=saved->kind) continue;
        const char *instance=scope.provider?qa_application_provider_instance(f->application,scope.provider):"";
        if (!instance || strcmp(instance,saved->instance) ||
            ((scope.kind==QA_APPLICATION_CONSOLE_CLIENT || scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME ||
              scope.kind==QA_APPLICATION_CONSOLE_Q3_UI) && scope.seat!=saved->seat)) continue;
        qa_command_context actual;
        if (!qa_console_context_read(console,&actual,error)) return false;
        actual.seat=saved->seat;actual.dialect=(qa_console_dialect)saved->dialect;
        actual.origin=(qa_command_origin)saved->origin;actual.direct=saved->direct;
        actual.console_text=saved->console_text;actual.script=NULL;
        if (!qa_application_player_actor(f->application,saved->seat,&actual.actor))
            return frontend_fail(error,QA_ERROR_FORMAT,"Recovery command lost its actual local actor");
        if (!qa_application_capture_command_context(f->application,&actual,command,error)) return false;
        *out=console;return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Recovery command lost its actual Source console");
}
static bool recovery_replay(void *context,qa_frontend *f,qa_error *error)
{
    recovery_candidate *candidate=context;frontend_replay_timing frame={0};bool frame_pending=false;
    bool ok=true;
    for (size_t i=0;ok && i<candidate->count;++i) {
        const qa_demo_record *record=candidate->records+i;
        if (frame_pending && record->kind!=QA_DEMO_ADVANCE) {
            ok=frontend_fail(error,QA_ERROR_FORMAT,"Recovery frame lacks its completion record");
            break;
        }
        if (record->kind==QA_DEMO_INPUT) {
            qa_recovery_input input;
            ok=qa_recovery_input_decode(record->payload,&input,error);
            if (ok) {
                qa_platform_events_push(f->platform_events,QA_PLATFORM_EVENT_USERCMD,
                    f->wall_time_ns,(int32_t)input.seat,0,
                    (qa_bytes){(const uint8_t *)&input.command,sizeof(input.command)});
                ok=frontend_platform_drain(f,error);
            }
        } else if (record->kind==QA_DEMO_JOURNAL) {
            qa_source_save_io io={0};uint32_t kind=UINT32_MAX;recovery_command command={0};
            ok=qa_source_save_reader(&io,NULL,record->payload,error) && qa_source_save_u32(&io,&kind);
            if (ok && kind==RECOVERY_FRAME) {
                ok=frame_fields(&io,&frame) && qa_source_save_finish(&io,NULL);
                frame_pending=ok;
            } else if (ok && kind==RECOVERY_COMMAND) {
                ok=command_fields(&io,&command) && qa_source_save_finish(&io,NULL);
                qa_console *console=NULL;qa_command_context actual;
                if (ok) ok=recovery_console(f,&command,&console,&actual,error);
                if (ok) {
                    frontend_replay_command queued={.console=console,
                        .context=actual,.text=command.text,
                        .wall_ns=command.wall_ns,.time_ns=command.time_ns,.frame_number=command.frame_number};
                    qa_platform_events_push(f->platform_events,QA_PLATFORM_EVENT_CONSOLE_COMMAND,
                        command.wall_ns,0,0,(qa_bytes){(const uint8_t *)&queued,sizeof(queued)});
                    ok=frontend_platform_drain(f,error);
                }
            } else if (ok) ok=frontend_fail(error,QA_ERROR_FORMAT,"Unknown recovery journal operation");
            free(command.instance);free(command.text);qa_source_save_dispose(&io);
            if (!ok && error && error->code==QA_OK)
                frontend_fail(error,QA_ERROR_FORMAT,"Invalid recovery journal fields");
        } else if (record->kind==QA_DEMO_ADVANCE) {
            if (!frame_pending) {
                ok=frontend_fail(error,QA_ERROR_FORMAT,"Recovery advance lacks its actual frame clocks");
                break;
            }
            ok=frontend_replay_frame(f,&frame,error);
            if (ok && qa_session_elapsed(qa_application_session(f->application))!=record->time_ns)
                ok=frontend_fail(error,QA_ERROR_FORMAT,"Recovery replay reached another simulation time");
            frame_pending=false;
        }
    }
    qa_application_travel_view travel;
    if (ok && (qa_application_should_stop(f->application) || qa_application_startup_pending(f->application) ||
        qa_application_travel_read(f->application,&travel)))
        ok=frontend_fail(error,QA_ERROR_FORMAT,"Recovery replay did not return its completed offline world");
    return ok;
}
static bool recovery_publish(void *context,void *value,qa_error *error)
{
    recovery_restore *restore=context;recovery_candidate *candidate=value;
    qa_frontend *active=*restore->slot;
    bool ok=frontend_persistence_restore_replay(restore->slot,active->options.persistence_services,
        candidate->image,candidate,recovery_replay,&restore->displaced,&restore->retained,error);
    if (ok) recovery_candidate_free(candidate);
    return ok;
}
static void recovery_discard(void *context,void *value)
{ (void)context;recovery_candidate_free(value); }
static bool recovery_drain(qa_frontend **slot,qa_error *error)
{
    qa_frontend *active=*slot;frontend_save_commands *owner=active->save_commands;
    bool resume=owner->recovery_resume;owner->recovery_pending=false;owner->draining=true;
    qa_error local={0};uint64_t reached=0;recovery_restore restore={.slot=slot};
    qa_demo_seek_ops ops={.create=recovery_create,.apply=recovery_collect,.finish=recovery_finish,
        .publish=recovery_publish,.discard=recovery_discard};
    bool ok=resume?qa_recovery_restore(owner->level_root,"recovery.qdemo",&restore,&ops,&reached,&local):
        qa_fs_root_remove(owner->level_root,"recovery.qdemo",&local);
    owner->draining=false;
    frontend_save_commands *current=(*slot)->save_commands;
    current->retained[0]=restore.displaced;current->retained[2]=restore.retained;
    if (ok) {
        current->recovery_checked=true;current->recovery_available=false;
        frontend_console_print(*slot,NULL,resume?"Interrupted session recovered.\n":"Interrupted session discarded.\n");
    } else {
        frontend_console_print(*slot,NULL,"Recovery failed: ");
        frontend_console_print(*slot,NULL,local.message);frontend_console_print(*slot,NULL,"\n");
    }
    qa_error cleanup={0};
    if (cleanup_retained(current,&cleanup) && ok) return recovery_start(*slot,error);
    return true;
}
static bool read_saved(frontend_save_commands *owner,const char *name,qa_save_image **image,
    qa_q1_save_data **source,qa_q2_save_data **q2,char **path,qa_error *error)
{
    qa_fs_root *root=owner->root,*external=NULL;
    char *relative=copy_text(name,error);
    if (!relative) return false;
    bool ok=true;
#ifdef _WIN32
    for (char *part=relative;*part;++part) if (*part=='\\') *part='/';
#endif
    if (absolute_path(name)) {
        char *slash=strrchr(relative,'/');
        size_t parent=slash?(size_t)(slash-relative):0;
        if (parent==0) parent=1;
#ifdef _WIN32
        else if (parent==2 && relative[1]==':') parent=3;
#endif
        char *directory=malloc(parent+1);
        if (!directory) ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining original save directory");
        else {
            memcpy(directory,relative,parent);directory[parent]=0;
            ok=qa_fs_root_open(directory,&external,error);
            free(directory);
        }
        if (ok) { root=external;memmove(relative,slash+1,strlen(slash+1)+1); }
    }
    qa_fs_entry_kind kind=QA_FS_MISSING;
    if (ok) ok=qa_fs_root_status(root,relative,&kind,NULL,error);
    if (ok && kind==QA_FS_MISSING && !path_extension(relative)) {
        size_t length=strlen(relative);
        char *file=length<=SIZE_MAX-5?realloc(relative,length+5):NULL;
        if (!file) ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining save filename");
        else { relative=file;memcpy(relative+length,".sav",5); }
    }
    if (ok) ok=qa_save_slot_name(relative,error) && qa_saved_game_read(root,relative,image,source,q2,error) &&
        qa_fs_root_join(root,relative,path,error);
    free(relative);qa_fs_root_close(external);
    return ok;
}
static bool restore_saved(qa_frontend **slot, frontend_save_commands *owner,
    const save_command_request *request, qa_save_image **image, qa_q1_save_data **source,qa_q2_save_data **q2,
    qa_frontend **displaced, qa_frontend **retained_candidate,
    qa_error *error)
{
    char *path=NULL;
    if (!read_saved(owner,request->name,image,source,q2,&path,error)) return false;
    qa_frontend *f = *slot;
    if (*image) {
        free(path);
        return qa_frontend_persistence_restore(slot,f->options.persistence_services,
            *image,displaced,retained_candidate,error);
    }
    const qa_product *product=NULL,*preferred=qa_application_save_original_product(f->application);
    qa_frontend_original_save original={0};
    bool ok=false;
    if (*source) {
        ok=qa_q1_save_select_product(qa_application_catalog(f->application),*source,path,preferred,&product,error);
        original=(qa_frontend_original_save){.family=QA_GAME_Q1,.state.q1=*source};
    } else {
        ok=qa_q2_save_select_product(qa_application_catalog(f->application),*q2,preferred,&product,error) &&
            qa_application_q2_save_import_ready(f->application,*q2,product->key,error);
        original=(qa_frontend_original_save){.family=QA_GAME_Q2,.state.q2=*q2};
    }
    free(path);
    if (ok) ok=qa_frontend_original_restore_begin(f,f->options.persistence_services,&original,
        product->key,&owner->original,error);
    if (original.family==QA_GAME_Q1) *source=original.state.q1;
    else *q2=original.state.q2;
    return ok;
}

static bool write_game(qa_frontend *f,qa_fs_root *root,const char *name,
    qa_save_purpose purpose,uint64_t nonce,qa_save_image **image,qa_q1_save_data **source,qa_q2_save_data **q2,qa_error *error)
{
    const qa_product *product=qa_application_save_original_product(f->application);
    bool original=false,written=false;
    if (product && product->family==QA_GAME_Q1 && product->edition!=QA_EDITION_QUAKEWORLD) {
        original=true;
        if (!frontend_cinematic_capture_ready(f))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Cannot save during standalone cinematic playback");
        qa_q1_save_client client;
        written=(!f->qc_messages || frontend_qc_messages_drain(f->qc_messages,error)) &&
            frontend_q1_save_client_read(f,0,&client,error) &&
            qa_application_q1_save_capture(f->application,&client,source,error) &&
            qa_q1_save_write(root,name,*source,nonce,error);
    } else if (product && product->family==QA_GAME_Q2 && product->edition==QA_EDITION_CLASSIC) {
        original=true;
        const qa_q2_config_entry *configs=NULL;size_t count=0;
        written=frontend_network_q2_configs(f,&configs,&count,error) &&
            qa_application_q2_save_capture(f->application,purpose,configs,count,q2,error) &&
            qa_q2_save_directory_write(root,name,*q2,nonce,error);
    }
    if (original) return written;
    return (*image || qa_frontend_persistence_capture(f,f->options.persistence_services,purpose,image,error)) &&
        qa_save_write(root,name,*image,nonce,error);
}
static bool save_driver_returned(const qa_frontend *f)
{
    qa_session *session=f && f->application?qa_application_session(f->application):NULL;
    qa_application_state state=f && f->application?qa_application_get_state(f->application):QA_APPLICATION_FAULTED;
    return f && (state==QA_APPLICATION_READY || state==QA_APPLICATION_RUNNING) &&
        !qa_application_startup_pending(f->application) && !f->stepping && !f->preparing &&
        !f->source_restoring &&
        qa_session_safe(session) && !qa_session_faulted(session);
}
static bool save_failure_returned(const qa_frontend *f)
{
    return save_driver_returned(f) && !f->save_image_pending && !f->native_resource_inventory_pending &&
        frontend_owners_idle(f) && frontend_seat_callbacks_idle(f);
}
static bool campaign_fresh(qa_frontend *f, qa_error *error)
{
    if (!save_failure_returned(f) || qa_application_get_state(f->application)!=QA_APPLICATION_RUNNING)
        return false;
    qa_error failure=error?*error:(qa_error){0}, fallback={0};
    if (!qa_application_campaign_stage(f->application,NULL,NULL,&fallback)) {
        if (error) *error=fallback;
        return false;
    }
    qa_application_feature_report(f->application,"Hub restore skipped; loading fresh map",&failure);
    if (error) *error=(qa_error){0};
    return true;
}
static bool failed_operation_finish(qa_frontend *f, qa_error *error)
{
    frontend_save_commands *owner=f->save_commands;
    save_failed_operation failed=owner->failed_operation;
    qa_error failure=owner->failure;
    owner->failed_operation=SAVE_FAILURE_NONE; owner->failure=(qa_error){0};
    bool ok=failed==SAVE_FAILURE_CAMPAIGN?campaign_fresh(f,&failure):save_failure_returned(f);
    if (!ok && error) *error=failure;
    return ok;
}
bool frontend_save_commands_campaign(qa_frontend *f, uint64_t revision, bool *handled,
    qa_error *error)
{
    if (!f || !f->save_commands || !handled)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign travel needs its actual save command owner");
    frontend_save_commands *owner=f->save_commands;
    *handled=owner->pending || owner->campaign || owner->original ||
        owner->failed_operation!=SAVE_FAILURE_NONE || cleanup_pending(owner);
    if (*handled) return true;
    bool needed=false;
    if (!qa_application_campaign_depart(f->application,revision,&needed,error))
        return campaign_fresh(f,error);
    if (!needed) return true;
    /* Real disconnect events are projected before retaining the departed cut. */
    if (!frontend_events(f,error)) return false;
    qa_save_image *departure=NULL;qa_q2_save_data *original=NULL;
    owner->draining=true;
    const qa_product *product=qa_application_save_original_product(f->application);
    bool ok;
    if (product && product->family==QA_GAME_Q2 && product->edition==QA_EDITION_CLASSIC) {
        const qa_q2_config_entry *configs=NULL;size_t count=0;
        ok=frontend_network_q2_configs(f,&configs,&count,error) &&
            qa_application_q2_save_capture(f->application,QA_SAVE_TRANSITION,configs,count,&original,error) &&
            qa_application_campaign_stage(f->application,NULL,original->levels,error);
    } else ok=qa_frontend_persistence_capture(f,f->options.persistence_services,QA_SAVE_TRANSITION,&departure,error) &&
        qa_application_campaign_stage(f->application,departure,NULL,error);
    if (!frontend_save_image_release(f,&departure,error)) ok=false;
    qa_q2_save_destroy(original);
    owner->draining=false;
    if (!ok) return campaign_fresh(f,error);
    qa_bytes cached=qa_application_campaign_restore(f->application);
    if (!cached.size) return true;
    ok=qa_save_image_decode(cached,&owner->campaign,error);
    if (ok) *handled=true;
    return ok || campaign_fresh(f,error);
}

static bool campaign_restore(qa_frontend **slot, qa_error *error)
{
    qa_frontend *active=*slot;
    frontend_save_commands *owner=active->save_commands;
    qa_save_image *image=owner->campaign;
    owner->campaign=NULL; owner->draining=true;
    qa_frontend *displaced=NULL,*retained=NULL;
    bool ok=qa_frontend_persistence_restore(slot,active->options.persistence_services,image,&displaced,&retained,error);
    if (!frontend_save_image_release(active,&image,error)) ok=false;
    owner->draining=false;
    frontend_save_commands *current=(*slot)->save_commands;
    current->retained[0]=displaced; current->retained[2]=retained;
    qa_error cleanup={0};
    (void)cleanup_retained(current,&cleanup);
    if (ok) return true;
    if (*slot!=active || displaced || !save_driver_returned(active) ||
        qa_application_get_state(active->application)!=QA_APPLICATION_RUNNING) return false;
    owner->failed_operation=SAVE_FAILURE_CAMPAIGN;
    owner->failure=error?*error:(qa_error){0};
    if (cleanup_pending(owner)) return true;
    return failed_operation_finish(active,error);
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
    if (!cleanup_retained(owner, &cleanup)) {
        qa_application_feature_report(f->application,"Save candidate cleanup",&cleanup);
        if (save_driver_returned(f)) return true;
        if (error) *error=cleanup;
        return false;
    }
    if (owner->failed_operation!=SAVE_FAILURE_NONE) return failed_operation_finish(f,error);
    qa_error inspection={0};
    if (!recovery_inspect(f,&inspection)) return true;
    if (owner->recovery_pending) return recovery_drain(slot,error);
    if (owner->campaign) return campaign_restore(slot,error);
    if (!owner->pending) return frontend_save_commands_autosave(f,error) && recovery_start(f,error);
    save_command_request request = owner->request;
    owner->pending = false; owner->draining = true;
    qa_save_image *image = NULL;
    qa_q1_save_data *source = NULL;qa_q2_save_data *q2=NULL;
    qa_frontend *displaced = NULL, *retained_source = NULL, *retained_candidate = NULL;
    qa_error local = {0};
    bool ok = qa_application_command_context_active(f->application, &request.context);
    if (!ok) frontend_fail(&local, QA_ERROR_ARGUMENT, "Save command belongs to a retired world");
    if (ok) ok = qa_application_save_policy(f->application, frontend_network_save_authority(f),
        f->options.dedicated, request.load, QA_SAVE_MANUAL, &local);
    bool complete = !owner->original;
    if (owner->original) {
        if (ok) ok = qa_frontend_original_restore_advance(owner->original, slot, &complete,
            &displaced, &retained_candidate, &local);
    } else if (ok && request.load) {
        ok = restore_saved(slot, owner, &request, &image, &source,&q2, &displaced,
            &retained_candidate, &local);
        if (ok && owner->original) complete = false;
    } else if (ok) {
        if (owner->next_nonce == UINT64_MAX) ok = frontend_fail(&local, QA_ERROR_ARGUMENT, "Save write sequence exhausted");
        else {
            uint64_t nonce = owner->next_nonce++;
            ok=write_game(f,owner->root,request.name,QA_SAVE_MANUAL,nonce,&image,&source,&q2,&local);
            if (ok && !owner->recovery_available && !f->options.dedicated &&
                frontend_network_save_authority(f)==QA_SAVE_OFFLINE) {
                qa_error recovery_error={0};
                bool recorded=recovery_directory(owner,true,&recovery_error) &&
                    (image || qa_frontend_persistence_capture(f,f->options.persistence_services,
                        QA_SAVE_MANUAL,&image,&recovery_error)) && recovery_checkpoint(f,image,&recovery_error);
                if (!recorded) recovery_fault(f,&recovery_error);
            }
        }
    }
    if (!frontend_save_image_release(f,&image,ok?&local:&cleanup)) {
        if (local.code==QA_OK) local=cleanup;
        ok=false;
    }
    qa_q1_save_destroy(source);qa_q2_save_destroy(q2);
    if (ok && !complete) {
        owner->pending = true; owner->draining = false;
        return true;
    }
    if (owner->original) {
        qa_frontend_original_restore *original = owner->original;
        owner->original = NULL;
        if (!qa_frontend_original_restore_dispose(original, &retained_source, &cleanup)) {
            if (local.code==QA_OK) local=cleanup;
            ok=false;
        }
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
    if (ok) return true;
    qa_application_feature_report((*slot)->application,"Manual save/load",&local);
    if (*slot==f && !displaced && save_driver_returned(f) &&
        !f->save_image_pending && !f->native_resource_inventory_pending) {
        if (cleanup_pending(current)) {
            current->failed_operation=SAVE_FAILURE_MANUAL; current->failure=local;
            return true;
        }
        if (save_failure_returned(f)) return true;
    }
    if (error) *error=local;
    return false;
}

bool frontend_save_commands_autosave(qa_frontend *f, qa_error *error)
{
    qa_application_save_request request;
    if (!f || !f->save_commands || f->stepping || f->preparing || f->source_restoring ||
        frontend_save_commands_pending(f) || !qa_application_save_request_read(f->application,&request)) return true;
    frontend_save_commands *owner=f->save_commands;
    qa_error local={0}, cleanup={0};
    if (!recovery_inspect(f,&local))
        return qa_application_save_request_complete(f->application,&request,error);
    if (f->options.dedicated || frontend_network_save_authority(f)!=QA_SAVE_OFFLINE ||
        !qa_application_save_policy(f->application,QA_SAVE_OFFLINE,false,false,QA_SAVE_LEVEL_ENTRY,&local))
        return qa_application_save_request_complete(f->application,&request,error);
    const qa_cvar_view *enabled=qa_cvars_find(qa_application_cvars(f->application),"sv_autosave");
    qa_autosave_configure(&owner->autosave,!enabled || enabled->number!=0);
    bool autosaved=false;
    bool ok=qa_autosave_level_entry(&owner->autosave,request.world_generation,request.fresh_entry,&local);
    if (ok && request.authored && owner->autosave.enabled) owner->autosave.pending=true;
    if (ok) ok=recovery_directory(owner,true,&local);
    qa_save_image *image=NULL; qa_q1_save_data *source=NULL;qa_q2_save_data *q2=NULL;char *autosave=NULL;
    owner->draining=true;
    if (ok) ok=qa_frontend_persistence_capture(f,f->options.persistence_services,QA_SAVE_LEVEL_ENTRY,&image,&local);
    if (ok && owner->autosave.pending) {
        const qa_launch_snapshot *snapshot=qa_application_launch(f->application);
        const qa_launch_binding *binding=qa_launch_binding_for(qa_launch_snapshot_choices(snapshot),
            (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
        const qa_launch_instance *instance=binding?qa_launch_snapshot_find(snapshot,binding->instance):NULL;
        const qa_product *product=instance?qa_catalog_product(qa_application_catalog(f->application),instance->selection.product):NULL;
        if (!product || !product->key)
            ok=frontend_fail(&local,QA_ERROR_ARGUMENT,"Autosave requires its actual GAME profile");
        size_t length=ok?strlen(product->key):0;
        if (ok) {
            const char *prefix=qa_application_save_original_product(f->application)?
                "autosave-":"autosave-custom-";
            size_t prefix_length=strlen(prefix);
            autosave=length<=SIZE_MAX-prefix_length-sizeof(".sav")?
                malloc(prefix_length+length+sizeof(".sav")):NULL;
            if (!autosave) ok=frontend_fail(&local,QA_ERROR_MEMORY,"Retaining actual GAME autosave slot");
            else {
                memcpy(autosave,prefix,prefix_length);memcpy(autosave+prefix_length,product->key,length);
                memcpy(autosave+prefix_length+length,".sav",sizeof(".sav"));
                ok=qa_save_slot_name(autosave,&local);
            }
        }
        if (ok && owner->next_nonce==UINT64_MAX) ok=frontend_fail(&local,QA_ERROR_ARGUMENT,"Autosave write sequence exhausted");
        if (ok) {
            ok=write_game(f,owner->level_root,autosave,QA_SAVE_LEVEL_ENTRY,owner->next_nonce++,&image,&source,&q2,&local);
            if (ok) owner->autosave.pending=false;
            autosaved=ok;
        }
    }
    bool recovered=false;
    if (ok && !owner->recovery_available) {
        ok=recovery_checkpoint(f,image,&local);
        if (ok) {
            recovered=true;
        } else recovery_fault(f,&local);
    }
    if (!frontend_save_image_release(f,&image,ok?&local:&cleanup)) ok=false;
    qa_q1_save_destroy(source);qa_q2_save_destroy(q2);
    owner->draining=false;
    if (!qa_application_save_request_complete(f->application,&request,error)) { free(autosave);return false; }
    char message[512];
    if (ok && !autosaved && !recovered) { free(autosave);return true; }
    if (ok) snprintf(message,sizeof(message),"Level checkpoint saved: saves/%s.\n",autosaved?autosave:"recovery.qdemo");
    else snprintf(message,sizeof(message),"Autosave/recovery failed: %s.\n",local.message);
    free(autosave);
    qa_console_emit(qa_application_console(f->application),NULL,message);
    return true;
}
