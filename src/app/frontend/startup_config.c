#include "startup_config.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

typedef struct startup_script { const char *name; frontend_script_scope scope; } startup_script;
struct frontend_startup_config {
    frontend_startup_config_options options;
    startup_script scripts[5];
    size_t count, index, depth, capacity;
    char **stack;
    char *image_text;
    qa_console *image_console;
    bool active, completed, defaults, archive, running, images;
    qa_error failure;
};
static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
static bool same_owner(const qa_command_context *a, const qa_command_context *b)
{
    return a && b && a->session==b->session && a->owner==b->owner && a->client==b->client &&
        a->seat==b->seat && a->dialect==b->dialect && a->origin==b->origin &&
        a->registry==b->registry && a->generation==b->generation && qa_actor_id_equal(a->actor,b->actor);
}
static void add(frontend_startup_config *owner, const char *name, frontend_script_scope scope)
{ owner->scripts[owner->count++]=(startup_script){name,scope}; }
frontend_startup_config *frontend_startup_config_create(const frontend_startup_config_options *options, qa_error *error)
{
    if (!options || !options->read || !options->apply_defaults || !options->apply_archive ||
        !options->apply_launch || options->command.script || options->command.origin==QA_COMMAND_REMOTE ||
        options->command.dialect>QA_CONSOLE_Q3)
        return fail(error,QA_ERROR_ARGUMENT,"Startup configuration requires its actual local source owners"),NULL;
    frontend_startup_config *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Allocating startup configuration"),NULL;
    owner->options=*options;
    qa_console_dialect dialect=options->command.dialect;
    if (options->seat_scope) {
        add(owner,dialect==QA_CONSOLE_Q3?"q3config.cfg":"config.cfg",FRONTEND_SCRIPT_SEAT);
        add(owner,"autoexec.cfg",FRONTEND_SCRIPT_SEAT); owner->defaults=true;
    } else if (dialect==QA_CONSOLE_QW && options->command.origin==QA_COMMAND_SERVER)
        add(owner,"server.cfg",FRONTEND_SCRIPT_USER);
    else if (dialect==QA_CONSOLE_Q1 || dialect==QA_CONSOLE_QW)
        add(owner,"quake.rc",FRONTEND_SCRIPT_MOUNTED);
    else {
        add(owner,"default.cfg",FRONTEND_SCRIPT_MOUNTED);
        add(owner,dialect==QA_CONSOLE_Q3?"q3config.cfg":"config.cfg",
            dialect==QA_CONSOLE_Q2_RERELEASE?FRONTEND_SCRIPT_LOOSE:FRONTEND_SCRIPT_USER);
        if (dialect==QA_CONSOLE_Q2_RERELEASE && options->has_mod)
            add(owner,"autoexec.cfg",FRONTEND_SCRIPT_BASE_LOOSE);
        add(owner,"autoexec.cfg",dialect==QA_CONSOLE_Q3?FRONTEND_SCRIPT_USER:FRONTEND_SCRIPT_GAME_LOOSE);
        if (dialect==QA_CONSOLE_Q2_RERELEASE) add(owner,"postexec.cfg",FRONTEND_SCRIPT_LOOSE);
    }
    return owner;
}
frontend_startup_config *frontend_startup_images_create(const qa_command_context *command,qa_bytes bytes,qa_error *error)
{
    if (!command || command->script || command->origin==QA_COMMAND_REMOTE || command->dialect>QA_CONSOLE_Q3 ||
        bytes.size==SIZE_MAX || (bytes.size && (!bytes.data || memchr(bytes.data,0,bytes.size))))
        return fail(error,QA_ERROR_ARGUMENT,"Image configuration requires its actual local source programme"),NULL;
    frontend_startup_config *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Allocating image configuration programme"),NULL;
    owner->image_text=malloc(bytes.size+1);
    if (!owner->image_text) { free(owner); return fail(error,QA_ERROR_MEMORY,"Retaining image configuration bytes"),NULL; }
    if (bytes.size) memcpy(owner->image_text,bytes.data,bytes.size);
    owner->image_text[bytes.size]=0;
    owner->options.command=*command; owner->images=true; return owner;
}
bool frontend_startup_images_command_current(const frontend_startup_config *owner,
    const qa_console *console,const qa_command_context *command)
{
    return owner && owner->images && owner->running && owner->image_console==console &&
        console && !qa_console_idle(console) && same_owner(command,&owner->options.command) &&
        command->console_text==owner->options.command.console_text;
}
bool frontend_startup_images_completed(const frontend_startup_config *owner,const qa_console *console)
{
    return owner && owner->images && owner->completed && !owner->running &&
        owner->image_console==console && console && qa_console_idle(console) && !qa_console_pending(console);
}
bool frontend_startup_config_destroy(frontend_startup_config *owner, qa_error *error)
{
    if (!owner) return true;
    if (owner->running) return fail(error,QA_ERROR_ARGUMENT,"Startup configuration is executing");
    for (size_t i=0;owner->stack && i<owner->depth;++i) free(owner->stack[i]);
    free(owner->stack); free(owner->image_text); free(owner); return true;
}
static bool push(frontend_startup_config *owner,const char *name,qa_error *error)
{
    if (owner->depth==owner->capacity) {
        size_t capacity=owner->capacity?owner->capacity*2:8;
        if (capacity<owner->capacity || capacity>SIZE_MAX/sizeof(*owner->stack))
            return fail(error,QA_ERROR_MEMORY,"Startup script ancestry overflow");
        char **stack=realloc(owner->stack,capacity*sizeof(*stack));
        if (!stack) return fail(error,QA_ERROR_MEMORY,"Retaining startup script ancestry");
        owner->stack=stack; owner->capacity=capacity;
    }
    char *copy=malloc(strlen(name)+1);
    if (!copy) return fail(error,QA_ERROR_MEMORY,"Retaining startup script name");
    strcpy(copy,name); owner->stack[owner->depth++]=copy; return true;
}
bool frontend_startup_config_read(frontend_startup_config *owner,const qa_command_context *source,
    const char *name,qa_bytes *out,void **lease,qa_error *error)
{
    if (!owner || !name || !out || !lease || !same_owner(source,&owner->options.command))
        return fail(error,QA_ERROR_ARGUMENT,"Script read leaves its prepared source lifetime");
    if (owner->images) return false;
    frontend_script_scope scope=owner->options.seat_scope?FRONTEND_SCRIPT_SEAT:FRONTEND_SCRIPT_USER;
    if (owner->active) {
        const startup_script *active=owner->scripts+owner->index-1;
        bool direct=!owner->depth && !source->script && !strcmp(name,active->name);
        bool nested=owner->depth && source->script && !strcmp(source->script,owner->stack[owner->depth-1]);
        if (direct) scope=active->scope;
        else if (nested && owner->depth==1 && !strcmp(active->name,"quake.rc") && !strcmp(name,"default.cfg"))
            scope=FRONTEND_SCRIPT_MOUNTED;
        if ((direct || nested) && !push(owner,name,error)) { owner->failure=error?*error:(qa_error){.code=QA_ERROR_MEMORY}; return false; }
    }
    qa_error local={0};
    bool found=owner->options.read(owner->options.context,scope,name,source,out,lease,&local);
    if (!found && local.code!=QA_OK && local.code!=QA_ERROR_NOT_FOUND) owner->failure=local;
    if (!found && error) *error=local;
    return found;
}
void frontend_startup_config_release(frontend_startup_config *owner,void *lease)
{ if (owner && owner->options.release) owner->options.release(owner->options.context,lease); }
static bool apply(frontend_startup_config *owner,bool (*callback)(void *,qa_error *))
{
    qa_error error={0};
    if (!callback || callback(owner->options.context,&error)) return true;
    if (error.code==QA_OK) qa_error_set(&error,QA_ERROR_ARGUMENT,0,"Startup configuration stage failed");
    owner->failure=error; return false;
}
void frontend_startup_config_script_complete(frontend_startup_config *owner,
    const qa_command_context *source,const char *name,bool success)
{
    (void)success;
    if (!owner || owner->images || !owner->active || !same_owner(source,&owner->options.command)) return;
    if (!owner->depth || !name || strcmp(name,owner->stack[owner->depth-1])) return;
    bool direct=owner->depth==1;
    bool q1_child=owner->depth==2 && !strcmp(owner->scripts[owner->index-1].name,"quake.rc");
    if (owner->failure.code==QA_OK && (direct || q1_child)) {
        if (!strcmp(name,"default.cfg") && !owner->defaults) {
            owner->defaults=true; apply(owner,owner->options.apply_defaults);
        }
        if (owner->failure.code==QA_OK && (!strcmp(name,"config.cfg") || !strcmp(name,"q3config.cfg")) && !owner->archive) {
            owner->archive=true; apply(owner,owner->options.apply_archive);
        }
        qa_console_dialect dialect=owner->options.command.dialect;
        if (owner->failure.code==QA_OK && direct &&
            ((dialect==QA_CONSOLE_Q3 && !strcmp(name,"autoexec.cfg")) ||
             ((dialect==QA_CONSOLE_Q2 || dialect==QA_CONSOLE_Q2_RERELEASE) && !strcmp(name,"config.cfg"))))
            apply(owner,owner->options.replay_startup_variables);
    }
    free(owner->stack[--owner->depth]);
    if (direct) owner->active=false;
}
bool frontend_startup_config_restrict_shared(const frontend_startup_config *owner)
{
    if (!owner || owner->images || !owner->options.seat_scope || !owner->active) return false;
    const char *name=owner->scripts[owner->index-1].name;
    return !strcmp(name,"config.cfg") || !strcmp(name,"q3config.cfg");
}
bool frontend_startup_config_advance(frontend_startup_config *owner,qa_console *console,bool *complete,qa_error *error)
{
    if (!owner || !console || !complete || owner->running || !qa_console_idle(console))
        return fail(error,QA_ERROR_ARGUMENT,"Startup frame requires its idle exclusive source console");
    *complete=false;
    if (owner->failure.code!=QA_OK) { if (error) *error=owner->failure; return false; }
    if (owner->images && ((owner->image_console && owner->image_console!=console) ||
        (!owner->index && qa_console_pending(console))))
        return fail(error,QA_ERROR_ARGUMENT,"Image programme requires its actual initially empty console");
    if (owner->completed) { *complete=true; return true; }
    if (owner->images) {
        owner->image_console=console; owner->running=true;
        bool ok=true;
        if (!owner->index) {
            ok=qa_console_append(console,&owner->options.command,owner->image_text,error);
            if (ok) owner->index=1;
        }
        if (ok) ok=qa_console_drain(console,0,NULL,error);
        owner->running=false;
        if (!ok) {
            if (error && error->code!=QA_OK) owner->failure=*error;
            else qa_error_set(&owner->failure,QA_ERROR_ARGUMENT,0,"Image configuration programme failed");
            return false;
        }
        owner->completed=!qa_console_drain_yielded(console) && !qa_console_pending(console);
        *complete=owner->completed; return true;
    }
    owner->running=true; bool ok=true;
    if (!owner->options.seat_scope && owner->options.command.dialect==QA_CONSOLE_QW &&
        owner->options.command.origin==QA_COMMAND_SERVER && !owner->archive) {
        owner->defaults=true; ok=apply(owner,owner->options.apply_defaults);
        if (ok) { owner->archive=true; ok=apply(owner,owner->options.apply_archive); }
    }
    while (ok) {
        if (!owner->active) {
            if (qa_console_pending(console)) {
                size_t executed;
                ok=qa_console_drain(console,0,&executed,error);
                if (owner->failure.code!=QA_OK) { if (error) *error=owner->failure; ok=false; }
                if (!ok || qa_console_drain_yielded(console) || qa_console_pending(console)) break;
            }
            if (owner->index==owner->count) {
                if (!owner->defaults || !owner->archive) { ok=fail(error,QA_ERROR_FORMAT,"Startup scripts did not reach defaults and archived configuration"); break; }
                owner->completed=true; ok=apply(owner,owner->options.apply_launch); *complete=ok; break;
            }
            const startup_script *script=owner->scripts+owner->index++;
            if (owner->options.command.dialect==QA_CONSOLE_Q3 && owner->options.safe_mode && !strcmp(script->name,"q3config.cfg")) {
                owner->archive=true; continue;
            }
            char command[64]; size_t length=strlen(script->name);
            memcpy(command,"exec ",5); memcpy(command+5,script->name,length); memcpy(command+5+length,"\n",2);
            owner->active=true;
            ok=qa_console_append(console,&owner->options.command,command,error);
            if (!ok) break;
        }
        size_t executed;
        ok=qa_console_drain(console,0,&executed,error);
        if (owner->failure.code!=QA_OK) { if (error) *error=owner->failure; ok=false; }
        if (!ok || qa_console_drain_yielded(console) || owner->active) break;
    }
    owner->running=false;
    if (!ok && owner->failure.code==QA_OK) {
        if (error && error->code!=QA_OK) owner->failure=*error;
        else qa_error_set(&owner->failure,QA_ERROR_ARGUMENT,0,"Startup configuration frame failed");
    }
    return ok;
}
static bool text_fields(qa_source_save_io *io,char **text)
{
    size_t length=*text?strlen(*text):0;
    if (!qa_source_save_count(io,&length,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX) || length==SIZE_MAX) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        *text=malloc(length+1); if (!*text) return fail(io->error,QA_ERROR_MEMORY,"Importing startup script ancestry");
    }
    if (!qa_source_save_bytes(io,*text,length)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) { if (memchr(*text,0,length)) return false; (*text)[length]=0; }
    return true;
}
static bool fields(qa_source_save_io *io,frontend_startup_config *owner)
{
    uint8_t magic[4]={'Q','F','S','C'}; uint32_t dialect=owner->options.command.dialect;
    bool mod=owner->options.has_mod,seat=owner->options.seat_scope,safe=owner->options.safe_mode;
    uint32_t failure=owner->failure.code;
    size_t offset=owner->failure.offset;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFSC",4) || !qa_source_save_u32(io,&dialect) || dialect!=(uint32_t)owner->options.command.dialect ||
        !qa_source_save_bool(io,&mod) || mod!=owner->options.has_mod || !qa_source_save_bool(io,&seat) || seat!=owner->options.seat_scope ||
        !qa_source_save_bool(io,&safe) || safe!=owner->options.safe_mode || !qa_source_save_count(io,&owner->index,owner->count) ||
        !qa_source_save_bool(io,&owner->active) || !qa_source_save_bool(io,&owner->completed) ||
        !qa_source_save_bool(io,&owner->defaults) || !qa_source_save_bool(io,&owner->archive) ||
        !qa_source_save_u32(io,&failure) || failure>QA_ERROR_NOT_FOUND || !qa_source_save_count(io,&offset,SIZE_MAX) ||
        !qa_source_save_bytes(io,owner->failure.message,sizeof(owner->failure.message)) ||
        !memchr(owner->failure.message,0,sizeof(owner->failure.message)) ||
        !qa_source_save_count(io,&owner->depth,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    owner->failure.code=(qa_status)failure; owner->failure.offset=offset;
    if (io->direction==QA_SOURCE_SAVE_READ && owner->depth) {
        if (owner->depth>SIZE_MAX/sizeof(*owner->stack)) return false;
        owner->stack=calloc(owner->depth,sizeof(*owner->stack)); owner->capacity=owner->depth;
        if (!owner->stack) return fail(io->error,QA_ERROR_MEMORY,"Importing startup script stack");
    }
    for (size_t i=0;i<owner->depth;++i) if (!text_fields(io,owner->stack+i) || !*owner->stack[i]) return false;
    return (!owner->active || owner->index) && (!owner->depth || owner->active) &&
        (!owner->depth || !strcmp(owner->stack[0],owner->scripts[owner->index-1].name)) &&
        (!owner->completed || (!owner->active && owner->index==owner->count && owner->defaults && owner->archive));
}
bool frontend_startup_config_checkpoint(const frontend_startup_config *owner,qa_buffer *out,qa_error *error)
{
    if (!owner || owner->images || owner->running || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"Startup capture requires its returned phase owner");
    frontend_startup_config state=*owner; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && fields(&io,&state) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_startup_config_restore(frontend_startup_config *owner,qa_bytes bytes,qa_error *error)
{
    if (!owner || owner->images || owner->running || owner->index || owner->active || owner->completed || owner->depth)
        return fail(error,QA_ERROR_ARGUMENT,"Startup import requires its empty detached phase owner");
    frontend_startup_config *state=frontend_startup_config_create(&owner->options,error);
    if (!state) return false;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,state) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok) { free(owner->stack); *owner=*state; free(state); }
    else { frontend_startup_config_destroy(state,NULL); if (!error || error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Invalid startup phase continuation"); }
    return ok;
}
