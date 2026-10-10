#include "startup_config.h"
#include "qa/console_release.h"
#include <stdlib.h>
#include <string.h>

typedef struct startup_script { const char *name; frontend_script_scope scope; } startup_script;
struct frontend_startup_config {
    frontend_startup_config_options options;
    startup_script scripts[5];
    size_t count, index, depth, capacity;
    char **stack;
    char *image_text;
    qa_console *console;
    qa_console_release *prefix;
    bool active, completed, defaults, archive, running, images;
    qa_error failure;
};
static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
static bool same_owner(const qa_command_context *a, const qa_command_context *b)
{
    return qa_command_context_equal(a,b,QA_COMMAND_CONTEXT_IGNORE_DIRECT |
        QA_COMMAND_CONTEXT_IGNORE_SCRIPT | QA_COMMAND_CONTEXT_IGNORE_CONSOLE_TEXT);
}
static void add(frontend_startup_config *owner, const char *name, frontend_script_scope scope)
{ owner->scripts[owner->count++]=(startup_script){name,scope}; }
frontend_startup_config *frontend_startup_config_create(const frontend_startup_config_options *options, qa_error *error)
{
    if (!options || !options->read || !options->apply_defaults || !options->apply_archive ||
        !options->apply_launch || options->command.script || options->command.origin==QA_COMMAND_REMOTE ||
        options->command.dialect>QA_RULESET_Q3)
        return fail(error,QA_ERROR_ARGUMENT,"Startup configuration requires its actual local source owners"),NULL;
    frontend_startup_config *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Allocating startup configuration"),NULL;
    owner->options=*options;
    if (options->continuation) return owner;
    qa_ruleset_id dialect=options->command.dialect;
    if (options->seat_scope) {
        add(owner,dialect==QA_RULESET_Q3?"q3config.cfg":"config.cfg",FRONTEND_SCRIPT_SEAT);
        add(owner,"autoexec.cfg",FRONTEND_SCRIPT_SEAT); owner->defaults=true;
    } else if (dialect==QA_RULESET_QUAKEWORLD && options->command.origin==QA_COMMAND_SERVER)
        add(owner,"server.cfg",FRONTEND_SCRIPT_USER);
    else if (dialect==QA_RULESET_NETQUAKE || dialect==QA_RULESET_QUAKEWORLD)
        add(owner,"quake.rc",FRONTEND_SCRIPT_MOUNTED);
    else {
        add(owner,"default.cfg",FRONTEND_SCRIPT_MOUNTED);
        add(owner,dialect==QA_RULESET_Q3?"q3config.cfg":"config.cfg",
            dialect==QA_RULESET_Q2_RERELEASE?FRONTEND_SCRIPT_LOOSE:FRONTEND_SCRIPT_USER);
        if (dialect==QA_RULESET_Q2_RERELEASE && options->has_mod)
            add(owner,"autoexec.cfg",FRONTEND_SCRIPT_BASE_LOOSE);
        add(owner,"autoexec.cfg",dialect==QA_RULESET_Q3?FRONTEND_SCRIPT_USER:FRONTEND_SCRIPT_GAME_LOOSE);
        if (dialect==QA_RULESET_Q2_RERELEASE) add(owner,"postexec.cfg",FRONTEND_SCRIPT_LOOSE);
    }
    return owner;
}
frontend_startup_config *frontend_startup_images_create(const qa_command_context *command,qa_bytes bytes,qa_error *error)
{
    if (!command || command->script || command->origin==QA_COMMAND_REMOTE || command->dialect>QA_RULESET_Q3 ||
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
    return owner && owner->images && owner->running && owner->console==console &&
        qa_console_release_context_current(owner->prefix,console,command);
}
bool frontend_startup_images_completed(const frontend_startup_config *owner,const qa_console *console)
{
    return owner && owner->images && owner->completed && !owner->running &&
        owner->console==console && console && qa_console_idle(console) && !owner->prefix;
}
bool frontend_startup_config_destroy(frontend_startup_config *owner, qa_error *error)
{
    if (!owner) return true;
    if (owner->running) return fail(error,QA_ERROR_ARGUMENT,"Startup configuration is executing");
    if (owner->prefix) {
        qa_console_release_outcome outcome;
        if (!qa_console_release_abort(owner->prefix,&outcome,error)) return false;
        owner->prefix=NULL;
    }
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
        qa_ruleset_id dialect=owner->options.command.dialect;
        if (owner->failure.code==QA_OK && direct &&
            (((dialect==QA_RULESET_NETQUAKE || dialect==QA_RULESET_QUAKEWORLD) &&
              (!strcmp(name,"quake.rc") || !strcmp(name,"server.cfg"))) ||
             (dialect==QA_RULESET_Q3 && !strcmp(name,"autoexec.cfg")) ||
             ((dialect==QA_RULESET_Q2_CLASSIC || dialect==QA_RULESET_Q2_RERELEASE) && !strcmp(name,"config.cfg"))))
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
static bool prefix_advance(frontend_startup_config *owner,qa_console *console,
    const char *text,bool *complete,qa_error *error)
{
    if (!owner->prefix && !qa_console_release_prepare(console,&owner->options.command,
            text,&owner->prefix,error)) return false;
    qa_console_release_outcome outcome;
    bool ok=qa_console_release_advance(owner->prefix,&outcome,error);
    *complete=ok && outcome==QA_CONSOLE_RELEASE_COMPLETED;
    if (*complete) {
        if (!qa_console_release_ready(owner->prefix,console,error)) return false;
        qa_console_release_publish(owner->prefix); owner->prefix=NULL;
    }
    return ok;
}
bool frontend_startup_config_advance(frontend_startup_config *owner,qa_console *console,bool *complete,qa_error *error)
{
    if (!owner || !console || !complete || owner->running || !qa_console_idle(console) ||
        (owner->console && owner->console!=console))
        return fail(error,QA_ERROR_ARGUMENT,"Startup frame requires its idle retained console");
    *complete=false;
    if (owner->failure.code!=QA_OK) { if (error) *error=owner->failure; return false; }
    if (owner->completed) { *complete=true; return true; }
    owner->console=console;
    owner->running=true; bool ok=true;
    if (owner->images) {
        bool done=false;
        ok=prefix_advance(owner,console,owner->image_text,&done,error);
        if (ok) { owner->index=1; owner->completed=done; *complete=done; }
    } else {
        if ((owner->options.continuation || (!owner->options.seat_scope &&
            owner->options.command.dialect==QA_RULESET_QUAKEWORLD &&
            owner->options.command.origin==QA_COMMAND_SERVER)) && !owner->archive) {
            owner->defaults=true; ok=apply(owner,owner->options.apply_defaults);
            if (ok) { owner->archive=true; ok=apply(owner,owner->options.apply_archive); }
        }
        while (ok) {
            char command[64]={0};
            if (!owner->active && !owner->prefix) {
                if (owner->index==owner->count) {
                    if (!owner->defaults || !owner->archive) {
                        ok=fail(error,QA_ERROR_FORMAT,"Startup scripts did not reach defaults and archived configuration"); break;
                    }
                    owner->completed=true; ok=apply(owner,owner->options.apply_launch); *complete=ok; break;
                }
                const startup_script *script=owner->scripts+owner->index++;
                if (owner->options.command.dialect==QA_RULESET_Q3 && owner->options.safe_mode && !strcmp(script->name,"q3config.cfg")) {
                    owner->archive=true; continue;
                }
                size_t length=strlen(script->name);
                memcpy(command,"exec ",5); memcpy(command+5,script->name,length); memcpy(command+5+length,"\n",2);
                owner->active=true;
            }
            bool done=false;
            ok=prefix_advance(owner,console,command,&done,error);
            if (owner->failure.code!=QA_OK) { if (error) *error=owner->failure; ok=false; }
            if (!ok || !done || owner->active) break;
        }
    }
    owner->running=false;
    if (!ok && owner->failure.code==QA_OK) {
        if (error && error->code!=QA_OK) owner->failure=*error;
        else qa_error_set(&owner->failure,QA_ERROR_ARGUMENT,0,"Startup configuration frame failed");
    }
    return ok;
}
