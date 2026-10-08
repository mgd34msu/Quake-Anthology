#include "source_admin.h"
#include "internal.h"
#include "config_store.h"
#include "network_admin.h"
#include "qa/application_network_qw.h"
#include "qa/filesystem.h"
#include "qa/source_save.h"
#include "qa/network_services_save.h"
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
    qa_fs_object_reference saved_preferences;
    qa_console_dialect saved_dialect;
    bool restoring;
    bool busy;
};
static bool fail(qa_error *error,qa_status status,const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
static bool source_view(const qa_console *console,const qa_cvars *cvars,const qa_command_context *command)
{
    return console && cvars && command && command->owner &&
        qa_cvars_same_store(qa_console_cvars(console),cvars) &&
        command->cvar_view==qa_cvars_view_identity(cvars) && command->dialect==qa_cvars_dialect(cvars);
}
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
        source_view(owner->console,owner->cvars,out) &&
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
static qa_admin_options admin_options(frontend_source_admin *owner,qa_console_dialect dialect)
{
    return (qa_admin_options){.dialect=dialect,.filters=1024,.rate_entries=1024,.burst=10,
        .rate_interval_ns=UINT64_C(1000000000),.heartbeat_interval_ns=UINT64_C(300000000000),
        .hooks={.context=owner,.password=password,.execute=execute,.send=send_packet,.travel=travel,
            .players=players,.random=random_rotation,.rate_registry=rate_registry,.print=print}};
}
bool frontend_source_admin_bind(frontend_source_admin *owner,qa_application *application,
    qa_console *console,qa_cvars *cvars,const qa_command_context *command,qa_error *error)
{
    if (!owner || owner->busy || owner->restoring || !owner->admin || !application ||
        !source_view(console,cvars,command))
        return fail(error,QA_ERROR_ARGUMENT,"Early administration requires its actual returned Source namespace");
    owner->application=application; owner->console=console; owner->cvars=cvars;
    owner->command=*command;
    owner->command.script=NULL;
    return policy(owner,error);
}
bool frontend_source_admin_unbind(frontend_source_admin *owner,const qa_cvars *view,qa_error *error)
{
    if (!owner) return true;
    if (!view) return fail(error,QA_ERROR_ARGUMENT,"Early administration release needs its actual Source view");
    if (owner->cvars!=view) return true;
    if (owner->busy) return fail(error,QA_ERROR_ARGUMENT,"Early administration is entered by its actual Source");
    owner->application=NULL; owner->console=NULL; owner->cvars=NULL;
    owner->command=(qa_command_context){0};
    return true;
}
void frontend_source_admin_rebind(frontend_source_admin *owner,qa_frontend *frontend)
{
    if (owner && !owner->busy && frontend) owner->frontend=frontend;
}
bool frontend_source_admin_create(qa_frontend *frontend,qa_application *application,qa_console *console,
    qa_cvars *cvars,const qa_command_context *command,frontend_source_admin **out,qa_error *error)
{
    if (!frontend || !out || *out || !application || !source_view(console,cvars,command))
        return fail(error,QA_ERROR_ARGUMENT,"Early administration requires the real Source constructor");
    frontend_source_admin *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Allocating early Source administration owner");
    owner->frontend=frontend; owner->nonce=qa_platform_time_ns();
    owner->random=(uint32_t)owner->nonce^(uint32_t)(owner->nonce>>32);
    qa_admin_options options=admin_options(owner,command->dialect);
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
    if (!owner || owner->busy || !owner->admin || !call || !handled)
        return fail(error,QA_ERROR_ARGUMENT,"Early administration requires its actual returned Source owner");
    qa_application_startup_source source;
    if (!frontend_config_store_server_invocation_read(owner->frontend->config_store,call,&source,error) ||
        source.console!=owner->console || source.cvars!=owner->cvars ||
        source.scope.provider!=owner->command.owner || source.command.session!=owner->command.session ||
        !source_view(source.console,source.cvars,&source.command) ||
        source.command.cvar_view!=owner->command.cvar_view || call->console!=owner->console ||
        !qa_console_invocation_current(call->console,call) || call->context.dialect!=source.command.dialect ||
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
    return !owner->admin || frontend_network_admin_adopt(owner->frontend,&owner->admin,owner->random,error);
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
const qa_console *frontend_source_admin_console(const frontend_source_admin *owner)
{ return owner && !owner->busy && !owner->restoring && owner->admin?owner->console:NULL; }
static bool root_fields(qa_source_save_io *io,qa_fs_object_reference *root)
{
    if (!qa_source_save_u32(io,&root->platform) || (root->platform!=1 && root->platform!=2)) return false;
    for (size_t i=0;i<3;++i) if (!qa_source_save_u64(io,root->words+i)) return false;
    return true;
}
static bool root_matches(qa_settings_store store,const qa_fs_object_reference *reference)
{
    const qa_fs_object_reference *roots=NULL; size_t count=0;
    if (!qa_vfs_mount_root_references(store.vfs,store.mount,&roots,&count)) return false;
    for (size_t i=0;i<count;++i)
        if (roots[i].platform==reference->platform && !memcmp(roots[i].words,reference->words,sizeof(reference->words))) return true;
    return false;
}
static bool packet_address_fields(qa_source_save_io *io,qa_net_address *address)
{
    uint32_t kind=address->kind;
    if (!qa_source_save_u32(io,&kind) || (kind!=QA_NET_IPV4 && kind!=QA_NET_IPV6) ||
        !qa_source_save_u16(io,&address->port)) return false;
    address->kind=(qa_net_address_kind)kind;
    return kind==QA_NET_IPV4?qa_source_save_bytes(io,address->host.ipv4,4):
        qa_source_save_bytes(io,address->host.ipv6.bytes,16) && qa_source_save_u32(io,&address->host.ipv6.scope);
}
static bool saved_blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t size=bytes->size;
    if (!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,size);
    if (size>io->input.size-io->offset) return fail(io->error,QA_ERROR_FORMAT,"Source administration bytes exceed their actual envelope");
    *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
static bool admin_fields(qa_source_save_io *io,frontend_source_admin *owner,qa_bytes *admin)
{
    uint8_t magic[4]={'Q','F','S','A'}; uint32_t dialect=owner->saved_dialect;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFSA",4) ||
        !qa_source_save_u32(io,&dialect) || dialect>QA_CONSOLE_Q3 ||
        !qa_source_save_u64(io,&owner->nonce) || !qa_source_save_u32(io,&owner->random) ||
        !root_fields(io,&owner->saved_preferences) || !saved_blob(io,admin) || !admin->size) return false;
    owner->saved_dialect=(qa_console_dialect)dialect;
    size_t count=0;
    if (io->direction==QA_SOURCE_SAVE_WRITE) for (admin_packet *p=owner->first;p;p=p->next) ++count;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?(io->input.size-io->offset)/10:SIZE_MAX)) return false;
    admin_packet *p=owner->first;
    for (size_t i=0;i<count;++i) {
        if (io->direction==QA_SOURCE_SAVE_READ) {
            p=calloc(1,sizeof(*p));
            if (!p) return fail(io->error,QA_ERROR_MEMORY,"Restoring unsent Source administration packet");
            if (owner->last) owner->last->next=p; else owner->first=p;
            owner->last=p;
        }
        qa_bytes bytes={p->bytes.data,p->bytes.size};
        if (!packet_address_fields(io,&p->address) || !saved_blob(io,&bytes)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ && bytes.size) {
            p->bytes.data=malloc(bytes.size);
            if (!p->bytes.data) return fail(io->error,QA_ERROR_MEMORY,"Restoring unsent Source administration payload");
            memcpy(p->bytes.data,bytes.data,bytes.size); p->bytes.size=bytes.size;
        }
        p=p->next;
    }
    return true;
}
bool frontend_source_admin_checkpoint(const frontend_source_admin *owner,qa_application *application,
    qa_console *console,qa_cvars *cvars,const qa_command_context *command,qa_settings_store store,
    qa_buffer *out,qa_error *error)
{
    qa_fs_root *root=qa_vfs_mount_root(store.vfs,store.mount);
    if (!owner || owner->busy || owner->restoring || !owner->admin || !out || out->data || out->size ||
        owner->application!=application || owner->console!=console || owner->cvars!=cvars || !command ||
        owner->command.owner!=command->owner || owner->command.session!=command->session ||
        owner->command.dialect!=command->dialect || !source_view(console,cvars,command) ||
        !root || !qa_fs_root_same_object(root,owner->preferences))
        return fail(error,QA_ERROR_ARGUMENT,"Source administration capture lost its exact returned Source and preference owner");
    frontend_source_admin copy=*owner; copy.saved_dialect=qa_cvars_dialect(cvars);
    if (!qa_fs_root_reference_read(owner->preferences,&copy.saved_preferences))
        return fail(error,QA_ERROR_ARGUMENT,"Source administration preference capability has no retained identity");
    qa_buffer admin={0};
    if (!qa_server_admin_checkpoint(owner->admin,&admin,error)) return false;
    qa_bytes bytes={admin.data,admin.size}; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && admin_fields(&io,&copy,&bytes) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&admin);
    if (!ok && error && error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Source administration leaves its actual saved field domains");
    return ok;
}
bool frontend_source_admin_restore(qa_frontend *frontend,qa_bytes bytes,
    frontend_source_admin **out,qa_error *error)
{
    if (!frontend || !out || *out) return fail(error,QA_ERROR_ARGUMENT,"Source administration restore needs its actual empty parent child");
    frontend_source_admin *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Restoring Source administration owner");
    owner->frontend=frontend; owner->restoring=true;
    qa_source_save_io io={0}; qa_bytes admin={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && admin_fields(&io,owner,&admin) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    qa_admin_options options=admin_options(owner,owner->saved_dialect);
    if (ok) ok=qa_server_admin_restore_checkpoint(admin,&options,&owner->admin,error);
    if (!ok) {
        if (error && error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Invalid Source administration continuation");
        frontend_source_admin_destroy(owner,NULL); return false;
    }
    *out=owner; return true;
}
bool frontend_source_admin_finish_restore(frontend_source_admin *owner,qa_application *application,
    qa_console *console,qa_cvars *cvars,const qa_command_context *command,qa_settings_store store,qa_error *error)
{
    qa_fs_root *root=qa_vfs_mount_root(store.vfs,store.mount);
    if (!owner || !owner->restoring || owner->busy || !owner->admin || owner->preferences || !application ||
        !source_view(console,cvars,command) ||
        command->dialect!=owner->saved_dialect || qa_cvars_dialect(cvars)!=owner->saved_dialect ||
        !root || !root_matches(store,&owner->saved_preferences))
        return fail(error,QA_ERROR_FORMAT,"Saved Source administration differs from its actual reconstructed Source and directory lineage");
    qa_fs_root_retain(root); owner->preferences=root;
    owner->application=application; owner->console=console; owner->cvars=cvars; owner->command=*command;
    owner->command.script=NULL; owner->restoring=false; return true;
}
