#include "config_bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct binding {
    qa_physical_input input;
    char *command;
} binding;
struct frontend_config_bindings {
    binding *rows;
    size_t count,capacity;
    unsigned registered;
    bool busy;
    frontend_config_binding_commands commands;
};
static const char *const names[]={"bind","unbind","unbindall","bindlist"};
static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }
static bool equal(const char *left,const char *right)
{
    for (;;++left,++right) {
        unsigned a=(unsigned char)*left,b=(unsigned char)*right;
        if (a>='A' && a<='Z') a+='a'-'A';
        if (b>='A' && b<='Z') b+='a'-'A';
        if (a!=b) return false;
        if (!a) return true;
    }
}
static bool same(qa_physical_input a,qa_physical_input b)
{
    return a.kind==b.kind && a.code==b.code &&
        (a.kind<QA_PHYSICAL_BUTTON || a.device==b.device) &&
        (a.kind!=QA_PHYSICAL_AXIS || a.positive==b.positive);
}
static size_t find(const frontend_config_bindings *owner,qa_physical_input input)
{
    size_t index=0; while (index<owner->count && !same(owner->rows[index].input,input)) ++index;
    return index;
}
static bool set(frontend_config_bindings *owner,qa_physical_input input,const char *command,qa_error *error)
{
    size_t length=strlen(command);
    if (length==SIZE_MAX) return fail(error,QA_ERROR_MEMORY,"Dedicated binding exceeds command storage");
    char *copy=malloc(length+1);
    if (!copy) return fail(error,QA_ERROR_MEMORY,"Retaining dedicated binding command");
    memcpy(copy,command,length+1);
    size_t index=find(owner,input);
    if (index==owner->count && owner->count==owner->capacity) {
        size_t next=owner->capacity?owner->capacity*2:16;
        if (next<owner->capacity || next>SIZE_MAX/sizeof(*owner->rows)) {
            free(copy); return fail(error,QA_ERROR_MEMORY,"Dedicated binding dictionary exceeds storage");
        }
        binding *rows=realloc(owner->rows,next*sizeof(*rows));
        if (!rows) { free(copy); return fail(error,QA_ERROR_MEMORY,"Growing dedicated binding dictionary"); }
        owner->rows=rows; owner->capacity=next;
    }
    if (index<owner->count) free(owner->rows[index].command); else ++owner->count;
    owner->rows[index]=(binding){input,copy}; return true;
}
static void remove_at(frontend_config_bindings *owner,size_t index)
{
    free(owner->rows[index].command); --owner->count;
    memmove(owner->rows+index,owner->rows+index+1,(owner->count-index)*sizeof(*owner->rows));
}
static void print(frontend_config_bindings *owner,const char *text)
{ if (owner->commands.print) owner->commands.print(owner->commands.context,text); }
static void print_row(frontend_config_bindings *owner,qa_physical_input input,const char *text)
{
    char name[128]; if (!qa_input_physical_name(input,name,sizeof(name))) return;
    print(owner,name); print(owner," = "); print(owner,text); print(owner,"\n");
}
static bool execute(frontend_config_bindings *owner,const qa_command_invocation *command,qa_error *error)
{
    if (equal(command->argv[0],"unbindall")) {
        while (owner->count) remove_at(owner,owner->count-1);
        return true;
    }
    if (equal(command->argv[0],"bindlist")) {
        for (size_t i=0;i<owner->count;++i) print_row(owner,owner->rows[i].input,owner->rows[i].command);
        return true;
    }
    bool unbind=equal(command->argv[0],"unbind");
    if (command->argc<2 || (unbind && command->argc!=2)) {
        print(owner,unbind?"unbind <key> : remove commands from a key\n":"bind <key> [command]\n"); return true;
    }
    int32_t device=0;
    for (size_t i=0;i<owner->count;++i) if (owner->rows[i].input.kind>=QA_PHYSICAL_BUTTON) {
        device=owner->rows[i].input.device; break;
    }
    qa_physical_input input;
    if (!qa_input_physical_parse(command->argv[1],device,&input)) {
        print(owner,"Unknown key "); print(owner,command->argv[1]); print(owner,"\n"); return true;
    }
    if (unbind) {
        for (size_t i=0;i<owner->count;) {
            qa_physical_input candidate=owner->rows[i].input;
            if (input.kind>=QA_PHYSICAL_BUTTON) candidate.device=input.device;
            if (same(candidate,input)) remove_at(owner,i); else ++i;
        }
        return true;
    }
    size_t index=find(owner,input);
    if (command->argc==2) {
        print_row(owner,input,index<owner->count?owner->rows[index].command:"Unbound"); return true;
    }
    size_t length=0;
    for (size_t i=2;i<command->argc;++i) {
        size_t part=strlen(command->argv[i]);
        if (part>=SIZE_MAX-length-(i>2?1:0)) return fail(error,QA_ERROR_MEMORY,"Dedicated binding command exceeds storage");
        length+=part+(i>2?1:0);
    }
    char *text=malloc(length+1);
    if (!text) return fail(error,QA_ERROR_MEMORY,"Joining dedicated binding command");
    size_t offset=0;
    for (size_t i=2;i<command->argc;++i) {
        if (i>2) text[offset++]=' ';
        size_t part=strlen(command->argv[i]); memcpy(text+offset,command->argv[i],part); offset+=part;
    }
    text[offset]=0; bool ok=set(owner,input,text,error); free(text); return ok;
}
static bool handler(void *context,const qa_command_invocation *command,qa_error *error)
{
    frontend_config_bindings *owner=context;
    if (!command->argc) return true;
    if (owner->busy) return fail(error,QA_ERROR_ARGUMENT,"Dedicated binding dictionary is already executing");
    if (command->context.origin!=QA_COMMAND_SERVER && command->context.origin!=QA_COMMAND_LOCAL) {
        owner->busy=true;
        if (equal(command->argv[0],"bind")) print(owner,"bind <key> [command]\n");
        else if (equal(command->argv[0],"unbind")) print(owner,command->argc==2?
            "unbind requires a local binding owner.\n":"unbind <key> : remove commands from a key\n");
        owner->busy=false; return true;
    }
    if (!owner->commands.current(owner->commands.context,&command->context))
        return fail(error,QA_ERROR_ARGUMENT,"Dedicated binding command leaves its actual local source scope");
    owner->busy=true; bool ok=execute(owner,command,error); owner->busy=false; return ok;
}
frontend_config_bindings *frontend_config_bindings_create(qa_error *error)
{
    frontend_config_bindings *owner=calloc(1,sizeof(*owner));
    if (!owner) fail(error,QA_ERROR_MEMORY,"Allocating dedicated binding dictionary");
    return owner;
}
bool frontend_config_bindings_commands(frontend_config_bindings *owner,
    const frontend_config_binding_commands *commands,qa_error *error)
{
    if (!owner || owner->busy || owner->registered || !commands || !commands->console ||
        !commands->owner || !commands->context || !commands->current || !commands->print)
        return fail(error,QA_ERROR_ARGUMENT,"Dedicated commands need their actual source console authority");
    owner->commands=*commands;
    for (unsigned i=0;i<4;++i) {
        if (!qa_console_register_owned(commands->console,names[i],"Dedicated console binding",commands->owner,
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
        qa_console_unregister(owner->commands.console,names[i],owner->commands.owner);
    while (owner->count) remove_at(owner,owner->count-1);
    free(owner->rows); free(owner); return true;
}
bool frontend_config_bindings_clone(const frontend_config_bindings *previous,
    frontend_config_bindings **out,qa_error *error)
{
    if (!previous || previous->busy || !out || *out)
        return fail(error,QA_ERROR_ARGUMENT,"Dedicated binding carry needs its returned actual dictionary");
    frontend_config_bindings *owner=frontend_config_bindings_create(error);
    if (!owner) return false;
    for (size_t i=0;i<previous->count;++i) if (!set(owner,previous->rows[i].input,previous->rows[i].command,error)) {
        frontend_config_bindings_destroy(owner,NULL); return false;
    }
    *out=owner; return true;
}
static bool append(qa_buffer *out,const char *text,qa_error *error)
{
    size_t length=strlen(text);
    if (length>=SIZE_MAX-out->size) return fail(error,QA_ERROR_MEMORY,"Dedicated config text exceeds storage");
    uint8_t *next=realloc(out->data,out->size+length+1);
    if (!next) return fail(error,QA_ERROR_MEMORY,"Retaining dedicated config text");
    out->data=next; memcpy(next+out->size,text,length+1); out->size+=length; return true;
}
bool frontend_config_bindings_config(const frontend_config_bindings *owner,qa_buffer *out,qa_error *error)
{
    if (!owner || owner->busy || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"Dedicated config needs its returned actual binding dictionary");
    static const char *const buttons[]={"A_BUTTON","B_BUTTON","X_BUTTON","Y_BUTTON","BACK","GUIDE","START",
        "LEFT_STICK","RIGHT_STICK","LEFT_SHOULDER","RIGHT_SHOULDER","DPAD_UP","DPAD_DOWN","DPAD_LEFT",
        "DPAD_RIGHT","MISC","PADDLE1","PADDLE2","PADDLE3","PADDLE4","TOUCHPAD"};
    bool ok=append(out,"unbindall\n",error);
    for (size_t i=0;ok && i<owner->count;++i) {
        const binding *row=owner->rows+i; char name[128];
        if (row->input.kind<QA_PHYSICAL_BUTTON) {
            if (!qa_input_physical_name(row->input,name,sizeof(name))) continue;
        } else if (row->input.device==0 && row->input.kind==QA_PHYSICAL_BUTTON &&
            row->input.code<sizeof(buttons)/sizeof(*buttons))
            snprintf(name,sizeof(name),"GAMEPAD_%s",buttons[row->input.code]);
        else if (row->input.device==0 && row->input.kind==QA_PHYSICAL_AXIS && row->input.positive &&
            row->input.code>=QA_AXIS_LEFT_TRIGGER && row->input.code<=QA_AXIS_RIGHT_TRIGGER)
            snprintf(name,sizeof(name),"GAMEPAD_%s_TRIGGER",row->input.code==QA_AXIS_LEFT_TRIGGER?"LEFT":"RIGHT");
        else continue;
        if (strpbrk(name,"\"\r\n") || strpbrk(row->command,"\"\r\n")) {
            ok=fail(error,QA_ERROR_FORMAT,"Source cfg cannot encode this dedicated binding"); break;
        }
        ok=append(out,"bind \"",error) && append(out,name,error) && append(out,"\" \"",error) &&
            append(out,row->command,error) && append(out,"\"\n",error);
    }
    if (!ok) qa_buffer_free(out);
    return ok;
}
bool frontend_config_bindings_fields(frontend_config_bindings *owner,qa_source_save_io *io)
{
    if (!owner || !io || owner->busy || (io->direction==QA_SOURCE_SAVE_READ && owner->count))
        return fail(io?io->error:NULL,QA_ERROR_ARGUMENT,"Dedicated binding codec needs its returned actual dictionary");
    size_t count=owner->count;
    size_t maximum=io->direction==QA_SOURCE_SAVE_READ?(io->input.size-io->offset)/14:SIZE_MAX;
    if (!qa_source_save_count(io,&count,maximum)) return false;
    for (size_t i=0;i<count;++i) {
        qa_physical_input input=io->direction==QA_SOURCE_SAVE_WRITE?owner->rows[i].input:(qa_physical_input){0};
        uint32_t kind=input.kind,device=(uint32_t)input.device,code=input.code;
        char *text=io->direction==QA_SOURCE_SAVE_WRITE?owner->rows[i].command:NULL;
        size_t length=text?strlen(text):0;
        maximum=io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX-1;
        if (!qa_source_save_u32(io,&kind) || kind>QA_PHYSICAL_AXIS ||
            !qa_source_save_u32(io,&device) || device>INT32_MAX || !qa_source_save_u32(io,&code) ||
            !qa_source_save_bool(io,&input.positive) || !qa_source_save_count(io,&length,maximum) || length==SIZE_MAX) return false;
        input.kind=(qa_physical_kind)kind; input.device=(int32_t)device; input.code=code;
        if (!qa_input_physical_valid(input))
            return fail(io->error,QA_ERROR_FORMAT,"Saved dedicated binding leaves the physical input namespace");
        if (io->direction==QA_SOURCE_SAVE_WRITE) { if (!qa_source_save_bytes(io,text,length)) return false; }
        else {
            text=malloc(length+1);
            if (!text) return fail(io->error,QA_ERROR_MEMORY,"Decoding dedicated binding command");
            bool ok=qa_source_save_bytes(io,text,length);
            text[length]=0;
            if (ok && (memchr(text,0,length) || find(owner,input)!=owner->count))
                ok=fail(io->error,QA_ERROR_FORMAT,"Saved dedicated binding duplicates a control or contains invalid text");
            if (ok) ok=set(owner,input,text,io->error);
            free(text); if (!ok) return false;
        }
    }
    return true;
}
