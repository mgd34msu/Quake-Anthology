#include "config_bindings.h"
#include "qa/text.h"
#include <stdlib.h>
#include <string.h>

struct frontend_config_bindings {
    qa_input_seat *store;
    unsigned registered;
    bool busy;
    frontend_config_binding_commands commands;
};
static const char *const names[]={"bind","unbind","unbindall","bindlist"};
static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }
static void print(frontend_config_bindings *owner,const char *text)
{ if (owner->commands.print) owner->commands.print(owner->commands.context,text); }
static bool handler(void *context,const qa_command_invocation *command,qa_error *error)
{
    frontend_config_bindings *owner=context;
    if (!qa_console_invocation_current(owner->commands.console,command) ||
        !qa_console_invocation_delivered_view(command,owner->commands.command.cvar_view,
            owner->commands.owner,owner->commands.owner))
        return fail(error,QA_ERROR_ARGUMENT,"Dedicated binding command lost its actual Source registration");
    if (!command->argc) return true;
    if (owner->busy) return fail(error,QA_ERROR_ARGUMENT,"Dedicated binding dictionary is already executing");
    if (command->context.origin!=QA_COMMAND_SERVER && command->context.origin!=QA_COMMAND_LOCAL) {
        owner->busy=true;
        if (qa_text_ascii_equal(command->argv[0],"bind")) print(owner,"bind <key> [command]\n");
        else if (qa_text_ascii_equal(command->argv[0],"unbind")) print(owner,command->argc==2?
            "unbind requires a local binding owner.\n":"unbind <key> : remove commands from a key\n");
        owner->busy=false; return true;
    }
    if (!owner->commands.current(owner->commands.context,&command->context))
        return fail(error,QA_ERROR_ARGUMENT,"Dedicated binding command leaves its actual local source scope");
    owner->busy=true;
    bool ok=qa_input_bindings_command(owner->store,command,true,
        owner->commands.print,owner->commands.context,error);
    owner->busy=false; return ok;
}
frontend_config_bindings *frontend_config_bindings_create(qa_error *error)
{
    frontend_config_bindings *owner=calloc(1,sizeof(*owner));
    if (!owner) {
        fail(error,QA_ERROR_MEMORY,"Allocating dedicated binding owner"); return NULL;
    }
    owner->store=qa_input_seat_bindings_create(error);
    if (!owner->store) { free(owner); return NULL; }
    return owner;
}
bool frontend_config_bindings_commands(frontend_config_bindings *owner,
    const frontend_config_binding_commands *commands,qa_error *error)
{
    if (!owner || owner->busy || owner->registered || !commands || !commands->console ||
        !commands->owner || !commands->context || !commands->current || !commands->print ||
        !commands->command.cvar_view)
        return fail(error,QA_ERROR_ARGUMENT,"Dedicated commands need their actual source console authority");
    owner->commands=*commands;
    for (unsigned i=0;i<4;++i) {
        if (!qa_console_register_context(commands->console,&commands->command,names[i],"Dedicated console binding",commands->owner,
            commands->owner,true,handler,owner,error)) return false;
        owner->registered|=1u<<i;
    }
    return true;
}
bool frontend_config_bindings_destroy(frontend_config_bindings *owner,qa_error *error)
{
    if (!owner) return true;
    if (owner->busy) return fail(error,QA_ERROR_ARGUMENT,"Dedicated binding dictionary is executing");
    for (unsigned i=0;i<4;++i) if (owner->registered&(1u<<i))
        qa_console_unregister_context(owner->commands.console,&owner->commands.command,names[i],owner->commands.owner);
    qa_input_seat_destroy(owner->store); free(owner); return true;
}
bool frontend_config_bindings_clone(const frontend_config_bindings *previous,
    frontend_config_bindings **out,qa_error *error)
{
    if (!previous || previous->busy || !out || *out)
        return fail(error,QA_ERROR_ARGUMENT,"Dedicated binding carry needs its returned actual dictionary");
    frontend_config_bindings *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Allocating dedicated binding owner");
    if (!qa_input_seat_configuration_clone(previous->store,&owner->store,error)) {
        free(owner); return false;
    }
    *out=owner; return true;
}
bool frontend_config_bindings_config(const frontend_config_bindings *owner,qa_buffer *out,qa_error *error)
{
    if (!owner || owner->busy || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"Dedicated config needs its returned actual binding dictionary");
    return qa_input_bindings_config(owner->store,true,out,error);
}
bool frontend_config_bindings_fields(frontend_config_bindings *owner,qa_source_save_io *io)
{
    if (!owner || !io || owner->busy ||
        (io->direction==QA_SOURCE_SAVE_READ && qa_input_seat_binding_count(owner->store)))
        return fail(io?io->error:NULL,QA_ERROR_ARGUMENT,"Dedicated binding codec needs its returned actual dictionary");
    size_t count=qa_input_seat_binding_count(owner->store);
    size_t maximum=io->direction==QA_SOURCE_SAVE_READ?(io->input.size-io->offset)/14:SIZE_MAX;
    if (!qa_source_save_count(io,&count,maximum)) return false;
    for (size_t i=0;i<count;++i) {
        const qa_input_binding *row=io->direction==QA_SOURCE_SAVE_WRITE?
            qa_input_seat_binding_at(owner->store,i):NULL;
        qa_physical_input input=row?row->input:(qa_physical_input){0};
        uint32_t kind=input.kind,device=(uint32_t)input.device,code=input.code;
        const char *text=row?row->command:NULL;
        size_t length=text?strlen(text):0;
        maximum=io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX-1;
        if (!qa_source_save_u32(io,&kind) || kind>QA_PHYSICAL_AXIS ||
            !qa_source_save_u32(io,&device) || device>INT32_MAX || !qa_source_save_u32(io,&code) ||
            !qa_source_save_bool(io,&input.positive) || !qa_source_save_count(io,&length,maximum) || length==SIZE_MAX) return false;
        input.kind=(qa_physical_kind)kind; input.device=(int32_t)device; input.code=code;
        if (!qa_input_physical_valid(input))
            return fail(io->error,QA_ERROR_FORMAT,"Saved dedicated binding leaves the physical input namespace");
        if (io->direction==QA_SOURCE_SAVE_WRITE) {
            if (!qa_source_save_bytes(io,(void *)text,length)) return false;
        } else {
            qa_bytes bytes;
            if (!qa_source_save_span(io,length,&bytes)) return false;
            if ((length && memchr(bytes.data,0,length)) || qa_input_seat_binding(owner->store,input))
                return fail(io->error,QA_ERROR_FORMAT,"Saved dedicated binding duplicates a control or contains invalid text");
            qa_arena storage={0};
            char *decoded=qa_arena_alloc(&storage,length+1,1,io->error);
            bool ok=decoded!=NULL;
            if (ok) {
                if (length) memcpy(decoded,bytes.data,length);
                decoded[length]=0;
                qa_input_binding binding={.input=input,.kind=QA_BIND_COMMAND,.command=decoded};
                ok=qa_input_seat_bind(owner->store,&binding,io->error);
            }
            qa_arena_destroy(&storage); if (!ok) return false;
        }
    }
    return true;
}
