#include "remote_q3_private.h"
#include "remote_q3_services.h"
#include "capture.h"
#include "../application/native_q3_remote_client_settings.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <sys/sysinfo.h>
#endif

struct frontend_remote_q3_services {
    frontend_remote_q3 *row;
    frontend_remote_q3_services_view view;
    qa_command_context origin;
    size_t callbacks;
    bool retiring, released;
};
static bool returned(void *context)
{
    const frontend_remote_q3_services *owner=context;
    return owner && !owner->callbacks && !owner->row->users &&
        frontend_seat_callbacks_returned(owner->row->frontend);
}
static bool basis_current(void *context,const qa_native_q3_remote_client_basis *basis)
{
    frontend_remote_q3_services *owner=context;
    qa_native_q3_remote_client_basis actual;
    if(!owner || owner->retiring || owner->released || !basis ||
        !frontend_remote_q3_basis_read(owner->row,&actual,NULL)) return false;
    const qa_application_q3_client_context *a=&actual.client,*b=&basis->client;
    return basis->application==actual.application && basis->session==actual.session &&
        basis->descriptor && basis->descriptor->storage==actual.descriptor->storage &&
        basis->content==actual.content && basis->content_product==actual.content_product && basis->product==actual.product &&
        qa_net_client_id_equal(basis->connection,actual.connection) && basis->epoch==actual.epoch &&
        basis->restart_generation==actual.restart_generation && basis->publication_generation==actual.publication_generation &&
        basis->configuration_generation==actual.configuration_generation && basis->map==actual.map &&
        basis->geometry==actual.geometry && basis->gamestate==actual.gamestate &&
        basis->physical_client==actual.physical_client && basis->initial_message==actual.initial_message &&
        basis->initial_command==actual.initial_command && a->session==b->session && a->receiver==b->receiver &&
        a->source_owner==b->source_owner && qa_actor_id_equal(a->source_actor,b->source_actor) && a->seat==b->seat &&
        a->source_client==b->source_client && a->service_owner==b->service_owner &&
        a->frontend_lifetime==b->frontend_lifetime && a->console==b->console && a->cvars==b->cvars &&
        a->source_cvars==b->source_cvars && a->client_time_cvars==b->client_time_cvars &&
        a->client_time_owner==b->client_time_owner && a->native_source==b->native_source && a->initialized==b->initialized;
}
static bool release(void *context,qa_error *error)
{
    frontend_remote_q3_services *owner=context;
    if(!returned(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote CLIENT service release retains an actual callback borrower");
    owner->released=true; return true;
}
static bool reliable(void *context,const qa_command_context *origin,const char *text,qa_error *error)
{
    frontend_remote_q3_services *owner=context;
    if(!owner || owner->retiring || owner->released || owner->callbacks==SIZE_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote reliable command lost its retained CLIENT owner");
    ++owner->callbacks;
    bool ok=frontend_network_client_reliable(owner->row->frontend,origin,text,error);
    --owner->callbacks; return ok;
}
static bool console(void *context,const qa_command_context *origin,const char *text,qa_error *error)
{
    frontend_remote_q3_services *owner=context; frontend_remote_q3_resources resources;
    if(!owner || owner->retiring || owner->released || !origin || !text || owner->callbacks==SIZE_MAX ||
        !frontend_remote_q3_resources_read(owner->row,&resources,error) ||
        origin->owner!=resources.domain.source.receiver.receiver || origin->seat!=resources.domain.source.receiver.seat ||
        origin->dialect!=QA_CONSOLE_Q3 || origin->origin!=QA_COMMAND_SEAT ||
        !qa_application_command_context_active(owner->row->application,origin))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote console command lost its actual captured CLIENT origin");
    ++owner->callbacks;
    bool ok=qa_console_append(resources.domain.source.receiver.console,origin,text,error);
    --owner->callbacks;
    return ok && frontend_remote_q3_resources_current(&resources);
}
static void print(void *context,const char *text)
{
    frontend_remote_q3_services *owner=context;
    if(owner && !owner->retiring && !owner->released)
        qa_console_emit(owner->row->resources.domain.source.receiver.console,&owner->origin,text);
}
static size_t memory_remaining(void)
{
    uint64_t available=0;
#ifdef _WIN32
    MEMORYSTATUSEX status={.dwLength=sizeof(status)};
    if(GlobalMemoryStatusEx(&status)) available=status.ullAvailPhys;
#elif defined(__APPLE__)
    mach_port_t host=mach_host_self(); vm_size_t page=0;
    vm_statistics64_data_t statistics; mach_msg_type_number_t count=HOST_VM_INFO64_COUNT;
    if(host_page_size(host,&page)==KERN_SUCCESS &&
        host_statistics64(host,HOST_VM_INFO64,(host_info64_t)&statistics,&count)==KERN_SUCCESS)
        available=(uint64_t)statistics.free_count*(uint64_t)page;
    mach_port_deallocate(mach_task_self(),host);
#else
    struct sysinfo information;
    if(sysinfo(&information)==0) available=(uint64_t)information.freeram*information.mem_unit;
#endif
    return available>INT32_MAX?INT32_MAX:(size_t)available;
}
static bool reload(void *context,uint32_t number,const char *text,qa_error *error)
{
    frontend_remote_q3_services *owner=context;
    q3n_remote_source_view source; q3n_client_settings settings;
    if(!owner || owner->retiring || owner->released || !owner->view.clients || !text || number>=64 ||
        owner->callbacks==SIZE_MAX || !q3n_remote_source_read(owner->view.source,&source,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote force-model reload lacks its actual client-info child");
    const char *current; uint64_t revision;
    if(!q3n_remote_source_configstring(owner->view.source,544+number,&current,&revision,error) || strcmp(current,text))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote force-model reload differs from its reached player configstring");
    ++owner->callbacks;
    bool ok=application_native_q3_remote_client_info_settings(owner->view.client,
        memory_remaining(),!source.basis.client.initialized,&settings,error) &&
        q3n_clients_remote_register_one(owner->view.clients,&source,&settings,number,error);
    --owner->callbacks; return ok && q3n_remote_source_current(&source);
}
static bool status_visible(void *context)
{
    const frontend_remote_q3_services *owner=context; qa_application_presentation_view view;
    return owner && !owner->retiring && !owner->released &&
        qa_application_presentation_read(owner->row->application,owner->row->resources.domain.source.receiver.seat,&view) &&
        view.hud==owner->row->resources.domain.source.receiver.receiver;
}
static bool publication_read(void *context,q3n_remote_publication *out,qa_error *error)
{ frontend_remote_q3_services *owner=context; return frontend_network_native_publication_read(owner->row->frontend,out,error); }
static bool publication_current(void *context,const q3n_remote_publication *view)
{ frontend_remote_q3_services *owner=context; return frontend_network_native_publication_current(owner->row->frontend,view); }
static bool command_read(void *context,int32_t sequence,q3n_remote_command *out,qa_error *error)
{
    frontend_remote_q3_services *owner=context;
    if(owner->retiring || owner->released || owner->callbacks==SIZE_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote reached command lost its retained source owner");
    ++owner->callbacks;
    bool ok=frontend_network_native_command_read(owner->row->frontend,sequence,out,error);
    --owner->callbacks;
    return ok && frontend_remote_q3_services_bind(owner->row,error);
}
static bool command_current(void *context,const q3n_remote_command *view)
{ frontend_remote_q3_services *owner=context; return frontend_network_native_command_current(owner->row->frontend,view); }

bool frontend_remote_q3_services_create(frontend_remote_q3 *row,qa_error *error)
{
    frontend_remote_q3_resources resources; qa_native_q3_remote_client_services services={0};
    if(!row || row->services || row->constructing || row->users || row->frontend->capture ||
        !frontend_remote_q3_resources_read(row,&resources,error) ||
        !frontend_remote_q3_basis_read(row,&services.basis,error) || services.basis.client.initialized)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote service construction requires its actual uninitialized CLIENT and declaration");
    frontend_remote_q3_services *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining native remote CLIENT children");
    owner->row=row; row->services=owner; row->constructing=true;
    qa_native_q3_character_selection character={0};
    bool ok=qa_native_q3_remote_client_character_selection_read(row->application,&resources.domain.source,&character,error) &&
        qa_application_capture_command_context(row->application,&services.basis.client.command_context,&owner->origin,error) &&
        frontend_network_presentation_services(row->frontend,&services.basis.client,&services.network,error);
    services.input=resources.input; services.reliable_origin=services.console_origin=owner->origin;
    services.context=owner; services.current=basis_current; services.idle=returned; services.release=release;
    services.reliable=reliable; services.console=console; services.reload_client_info=reload; services.status_visible=status_visible;
    if(ok) ok=qa_native_q3_remote_client_create(&services,&character,&owner->view.client,error);
    if(character.lifetime) character.release(character.lifetime);
    q3n_remote_source_options source={.client=owner->view.client,.context=owner,.publication_read=publication_read,
        .publication_current=publication_current,.command_read=command_read,.command_current=command_current,.idle=returned};
    if(ok) ok=q3n_remote_source_create(&source,&owner->view.source,error);
    qa_native_q3_remote_client_basis basis;
    if(ok) ok=qa_native_q3_remote_client_basis_read(owner->view.client,&basis,error);
    if(ok) {
        q3n_media_options media={.product=basis.product,.assets=resources.assets,.remote_source=owner->view.source};
        q3n_client_options clients={.content=resources.domain.content,.assets=resources.assets,.product=basis.product,
            .remote_source=owner->view.source,.context=owner,.print=print};
        ok=q3n_media_create(&media,&owner->view.media,error) &&
            q3n_clients_create_remote(&clients,&owner->view.clients,error);
    }
    row->constructing=false; return ok;
}
bool frontend_remote_q3_services_read(const frontend_remote_q3 *row,frontend_remote_q3_services_view *out,qa_error *error)
{
    const frontend_remote_q3_services *owner=row?row->services:NULL; q3n_remote_source_view source;
    frontend_remote_q3_resources resources;
    if(!owner || !out || owner->retiring || owner->released || !owner->view.client || !owner->view.source ||
        !owner->view.media || !owner->view.clients || !frontend_remote_q3_resources_read(row,&resources,error) ||
        !q3n_remote_source_read(owner->view.source,&source,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote CLIENT children lack their completed constructor associations");
    *out=owner->view; out->resources=resources; return true;
}
bool frontend_remote_q3_services_bind(frontend_remote_q3 *row,qa_error *error)
{
    frontend_remote_q3_services *owner=row?row->services:NULL; qa_native_q3_remote_client_basis basis;
    return owner && !owner->retiring && !owner->released && owner->view.client && !row->frontend->capture &&
        frontend_remote_q3_basis_read(row,&basis,error) && qa_native_q3_remote_client_source_bind(owner->view.client,&basis,error);
}
bool frontend_remote_q3_services_idle(const frontend_remote_q3_services *owner)
{
    return !owner || (returned((void *)owner) && (!owner->view.client || qa_native_q3_remote_client_idle(owner->view.client)) &&
        q3n_remote_source_idle(owner->view.source) &&
        (!owner->view.media || q3n_media_idle(owner->view.media)) &&
        (!owner->view.clients || q3n_clients_idle(owner->view.clients)));
}
bool frontend_remote_q3_services_destroy(frontend_remote_q3_services **owned,qa_error *error)
{
    if(!owned || !*owned) return true;
    frontend_remote_q3_services *owner=*owned;
    if(owner->row->frontend->capture || !frontend_remote_q3_services_idle(owner) ||
        !qa_native_q3_remote_client_retire_ready(owner->view.client,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote CLIENT retirement retains an entered child");
    owner->retiring=true;
    q3n_clients_destroy(owner->view.clients); owner->view.clients=NULL;
    q3n_media_destroy(owner->view.media); owner->view.media=NULL;
    if(!q3n_remote_source_destroy(owner->view.source,error)) return false;
    owner->view.source=NULL;
    if(!qa_native_q3_remote_client_destroy(owner->view.client,error)) return false;
    owner->view.client=NULL; free(owner); *owned=NULL; return true;
}
