#include "source_admin.h"
#include "internal.h"
#include "network_admin.h"
#include "qa/application_network_qw.h"
#include "qa/filesystem.h"
#include <SDL2/SDL.h>

typedef struct admin_packet {
    struct admin_packet *next;
    qa_net_address address;
    qa_buffer bytes;
} admin_packet;
struct frontend_source_admin {
    qa_frontend *frontend;
    qa_application *application;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context command;
    qa_server_admin *admin;
    qa_fs_root *preferences;
    admin_packet *first,*last;
    uint64_t nonce;
    uint32_t random;
    bool busy;
};
static bool fail(qa_error *error,qa_status status,const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
static bool send_packet(void *context,const qa_net_address *address,qa_bytes bytes,qa_error *error)
{
    frontend_source_admin *owner=context;
    if (!address || (bytes.size && !bytes.data))
        return fail(error,QA_ERROR_ARGUMENT,"Early Source send lost its actual datagram");
    admin_packet *packet=calloc(1,sizeof(*packet));
    if (!packet) return fail(error,QA_ERROR_MEMORY,"Retaining pending Source administration datagram");
    if (bytes.size) {
        packet->bytes.data=malloc(bytes.size);
        if (!packet->bytes.data) { free(packet); return fail(error,QA_ERROR_MEMORY,"Retaining pending Source administration bytes"); }
        memcpy(packet->bytes.data,bytes.data,bytes.size);
        packet->bytes.size=bytes.size;
    }
    packet->address=*address;
    if (owner->last) owner->last->next=packet;
    else owner->first=packet;
    owner->last=packet; return true;
}
static const char *password(void *context,bool limited)
{
    frontend_source_admin *owner=context;
    const qa_cvar_view *value=qa_cvars_find(owner->cvars,limited?"lrcon_password":
        qa_cvars_dialect(owner->cvars)==QA_CONSOLE_Q3?"rconPassword":"rcon_password");
    return value?value->value:"";
}
static qa_cvars *rate_registry(void *context)
{ return ((frontend_source_admin *)context)->cvars; }
static bool command_current(frontend_source_admin *owner,qa_command_context *out,qa_error *error)
{
    *out=owner->command;
    out->registry=0; out->generation=0; out->actor=(qa_actor_id){0};
    return qa_application_capture_command_context(owner->application,out,out,error) &&
        qa_application_command_context_active(owner->application,out);
}
static void print(void *context,const char *text)
{
    frontend_source_admin *owner=context; qa_command_context command;
    if (command_current(owner,&command,NULL)) qa_console_emit(owner->console,&command,text);
}
typedef struct admin_output { qa_admin_write_fn write; void *context; qa_error error; } admin_output;
static void output(void *context,const qa_command_context *command,const char *text)
{
    admin_output *capture=context; (void)command;
    if (!capture->error.code) (void)capture->write(capture->context,text,&capture->error);
}
static bool execute(void *context,const qa_net_address *from,const char *text,bool limited,
    qa_admin_write_fn write,void *result,qa_error *error)
{
    frontend_source_admin *owner=context; qa_command_context command; (void)from; (void)limited;
    if (!command_current(owner,&command,error)) return false;
    command.direct=true; command.console_text=true; command.script="remote-console";
    admin_output capture={.write=write,.context=result};
    bool ok=qa_console_execute_capture(owner->console,&command,text,output,&capture,error);
    if (capture.error.code) { if (error) *error=capture.error; return false; }
    return ok;
}
static bool travel(void *context,const char *map,qa_error *error)
{
    frontend_source_admin *owner=context;
    return qa_application_queue_travel(owner->application,
        &(qa_application_travel_request){.expression=map,.carry_players=true},error);
}
static bool players(void *context,uint32_t *out,qa_error *error)
{
    frontend_source_admin *owner=context; uint32_t cursor=0,count=0;
    qa_application_network_qw_source source;
    if (!qa_application_network_qw_source_read(owner->application,&source,error)) return false;
    for (;;) {
        qa_application_network_qw_client client; bool present=false;
        if (!qa_application_network_qw_client_next(owner->application,&cursor,&present,&client,error)) return false;
        if (!present) break;
        ++count;
    }
    *out=count; return true;
}
static uint32_t random_rotation(void *context)
{
    frontend_source_admin *owner=context;
    owner->random=owner->random*UINT32_C(1664525)+UINT32_C(1013904223);
    return owner->random;
}
static bool persist_filters(void *context,qa_error *error)
{
    frontend_source_admin *owner=context; qa_buffer bytes={0}; bool created;
    if (owner->nonce==UINT64_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Early Source filter publication namespace exhausted");
    if (!qa_server_admin_save_filters(owner->admin,&bytes,error)) return false;
    bool ok=qa_fs_root_publish(owner->preferences,"network/filters.bin",
        (qa_bytes){bytes.data,bytes.size},++owner->nonce,false,true,&created,error);
    qa_buffer_free(&bytes); return ok;
}
static bool policy(frontend_source_admin *owner,qa_error *error)
{
    qa_console_dialect dialect=qa_cvars_dialect(owner->cvars);
    const qa_cvar_view *filter=qa_cvars_find(owner->cvars,"filterban"),
        *published=qa_cvars_find(owner->cvars,"public"),*dedicated=qa_cvars_find(owner->cvars,"dedicated");
    bool q2=dialect==QA_CONSOLE_Q2 || dialect==QA_CONSOLE_Q2_RERELEASE;
    return qa_server_admin_policy(owner->admin,dialect,!filter || filter->integer!=0,
        owner->frontend->options.network_host && owner->frontend->options.dedicated &&
        (q2?published && published->number!=0:dialect!=QA_CONSOLE_Q3 || (dedicated && dedicated->integer==2)),error);
}
bool frontend_source_admin_bind(frontend_source_admin *owner,qa_application *application,
    qa_console *console,qa_cvars *cvars,const qa_command_context *command,qa_error *error)
{
    if (!owner || owner->busy || !owner->admin || !application || !console || !cvars || !command ||
        !command->owner || qa_console_cvars(console)!=cvars || command->dialect!=qa_cvars_dialect(cvars))
        return fail(error,QA_ERROR_ARGUMENT,"Early administration requires its actual returned Source namespace");
    owner->application=application; owner->console=console; owner->cvars=cvars;
    owner->command=*command;
    owner->command.script=NULL;
    return policy(owner,error);
}
bool frontend_source_admin_unbind(frontend_source_admin *owner,const qa_console *console,qa_error *error)
{
    if (!owner) return true;
    if (owner->busy) return fail(error,QA_ERROR_ARGUMENT,"Early administration is entered by its actual Source");
    if (owner->console==console) {
        owner->application=NULL; owner->console=NULL; owner->cvars=NULL;
        owner->command=(qa_command_context){0};
    }
    return true;
}
void frontend_source_admin_rebind(frontend_source_admin *owner,qa_frontend *frontend)
{
    if (owner && !owner->busy && frontend) owner->frontend=frontend;
}
bool frontend_source_admin_create(qa_frontend *frontend,qa_application *application,qa_console *console,
    qa_cvars *cvars,const qa_command_context *command,frontend_source_admin **out,qa_error *error)
{
    if (!frontend || !out || *out || !application || !console || !cvars || !command ||
        !command->owner || qa_console_cvars(console)!=cvars || command->dialect!=qa_cvars_dialect(cvars))
        return fail(error,QA_ERROR_ARGUMENT,"Early administration requires the real Source constructor");
    frontend_source_admin *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Allocating early Source administration owner");
    owner->frontend=frontend; owner->nonce=SDL_GetPerformanceCounter();
    owner->random=(uint32_t)owner->nonce^(uint32_t)(owner->nonce>>32);
    qa_admin_options options={.dialect=command->dialect,.filters=1024,.rate_entries=1024,.burst=10,
        .rate_interval_ns=UINT64_C(1000000000),.heartbeat_interval_ns=UINT64_C(300000000000),
        .hooks={.context=owner,.password=password,.execute=execute,.send=send_packet,.travel=travel,
            .players=players,.random=random_rotation,.rate_registry=rate_registry,.print=print}};
    bool ok=qa_server_admin_create(&options,&owner->admin,error) &&
        frontend_source_admin_bind(owner,application,console,cvars,command,error) &&
        qa_fs_root_open(frontend->options.application.user_root,&owner->preferences,error);
    qa_fs_file *file=NULL; qa_fs_identity identity; qa_buffer bytes={0}; qa_error missing={0};
    if (ok && qa_fs_root_file_open(owner->preferences,"network/filters.bin",&file,&identity,&missing))
        ok=qa_fs_file_read_snapshot(file,&identity,&bytes,error) &&
            qa_server_admin_restore_filters(owner->admin,(qa_bytes){bytes.data,bytes.size},error);
    else if (ok && missing.code!=QA_ERROR_NOT_FOUND) { if (error) *error=missing; ok=false; }
    qa_fs_file_close(file); qa_buffer_free(&bytes);
    if (!ok) { (void)frontend_source_admin_destroy(owner,NULL); return false; }
    *out=owner; return true;
}
bool frontend_source_admin_dispatch(frontend_source_admin *owner,const qa_command_invocation *call,
    size_t skip,bool *handled,qa_error *error)
{
    if (!owner || owner->busy || !owner->admin || !call || !handled || call->console!=owner->console ||
        qa_console_cvars(call->console)!=owner->cvars || !qa_console_invocation_current(call->console,call) ||
        call->context.owner!=owner->command.owner || call->context.session!=owner->command.session ||
        call->context.dialect!=owner->command.dialect ||
        !qa_application_command_context_active(owner->application,&call->context))
        return fail(error,QA_ERROR_ARGUMENT,"Early administration lost its actual entered Source invocation");
    if (!policy(owner,error)) return false;
    owner->busy=true;
    bool ok=frontend_network_source_admin_dispatch(owner->frontend,owner->admin,owner,
        send_packet,persist_filters,call,skip,handled,error);
    owner->busy=false; return ok;
}
bool frontend_source_admin_adopt(frontend_source_admin *owner,qa_error *error)
{
    if (!owner || owner->busy || !owner->frontend->network)
        return fail(error,QA_ERROR_ARGUMENT,"Early administration adoption requires returned real transport custody");
    while (owner->first) {
        admin_packet *packet=owner->first;
        if (!frontend_network_admin_send(owner->frontend,&packet->address,
            (qa_bytes){packet->bytes.data,packet->bytes.size},error)) return false;
        owner->first=packet->next;
        if (!owner->first) owner->last=NULL;
        qa_buffer_free(&packet->bytes); free(packet);
    }
    return !owner->admin || frontend_network_admin_adopt(owner->frontend,&owner->admin,error);
}
bool frontend_source_admin_destroy(frontend_source_admin *owner,qa_error *error)
{
    if (!owner) return true;
    if (owner->busy) return fail(error,QA_ERROR_ARGUMENT,"Early administration is entered by its actual Source");
    while (owner->first) {
        admin_packet *packet=owner->first; owner->first=packet->next;
        qa_buffer_free(&packet->bytes); free(packet);
    }
    qa_server_admin_destroy(owner->admin); qa_fs_root_close(owner->preferences); free(owner); return true;
}
