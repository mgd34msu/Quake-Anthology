#include "remote_q3_transport.h"
#include "qa/application_native_q3_client_modules.h"

struct frontend_remote_q3_transport {
    frontend_remote_q3 *row;
    qa_native_q3_remote_client_transport *transport;
    size_t callbacks;
    bool attached,constructing,retiring,released;
};
frontend_remote_q3 *frontend_remote_q3_transport_parent(const frontend_remote_q3_transport *owner)
{ return owner?owner->row:NULL; }
static bool current(void *context,const qa_application_q3_remote_source *source)
{
    const frontend_remote_q3_transport *owner=context;
    frontend_remote_q3_resources resources;
    if(!owner || !source || !source->descriptor || !owner->attached || owner->retiring || owner->released ||
        frontend_remote_q3_transport_read(owner->row)!=owner ||
        !frontend_remote_q3_resources_read(owner->row,&resources,NULL)) return false;
    const qa_application_q3_remote_source *actual=&resources.domain.source;
    const qa_application_q3_client_context *a=&actual->receiver,*b=&source->receiver;
    return source->descriptor->storage==actual->descriptor->storage &&
        source->descriptor->content==actual->descriptor->content &&
        qa_sha256_equal(&source->descriptor->identity,&actual->descriptor->identity) &&
        source->configuration_generation==actual->configuration_generation &&
        source->connection_epoch==resources.domain.epoch && b->native_source &&
        a->session==b->session && a->receiver==b->receiver && a->seat==b->seat &&
        a->service_owner==b->service_owner && a->frontend_lifetime==b->frontend_lifetime &&
        a->console==b->console && a->cvars==b->cvars && a->client_time_cvars==b->client_time_cvars &&
        a->client_time_owner==b->client_time_owner && frontend_remote_q3_resources_current(&resources);
}
static bool callbacks_idle(void *context)
{ const frontend_remote_q3_transport *owner=context; return owner && !owner->callbacks; }
static uint32_t milliseconds(void *context)
{
    const frontend_remote_q3_transport *owner=context;
    return (uint32_t)(frontend_remote_q3_frontend(owner->row)->wall_time_ns/UINT64_C(1000000));
}
static bool forward(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_remote_q3_transport *owner=context;
    if(!owner || owner->callbacks || owner->retiring || owner->released)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Pure CLIENT forwarding retains an entered or retired transport");
    ++owner->callbacks;
    bool okay=frontend_network_client_forward(frontend_remote_q3_frontend(owner->row),call,error);
    --owner->callbacks; return okay;
}
static bool release(void *context,qa_error *error)
{
    frontend_remote_q3_transport *owner=context;
    if(!callbacks_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Pure CLIENT transport retains an actual invocation");
    owner->released=true; return true;
}
bool frontend_remote_q3_transport_current(const frontend_remote_q3_transport *owner)
{
    return owner && owner->attached && !owner->constructing && !owner->retiring && !owner->released &&
        frontend_remote_q3_transport_read(owner->row)==owner && owner->transport &&
        qa_native_q3_remote_client_transport_current(owner->transport);
}
bool frontend_remote_q3_transport_idle(const frontend_remote_q3_transport *owner)
{
    return !owner || (!owner->constructing && !owner->callbacks &&
        (!owner->transport || qa_native_q3_remote_client_transport_idle(owner->transport)));
}
bool frontend_remote_q3_transport_retired(const frontend_remote_q3_transport *owner)
{ return owner && owner->retiring && !owner->transport && frontend_remote_q3_transport_idle(owner); }
static bool create(frontend_remote_q3 *row,bool restoring,frontend_remote_q3_transport **out,qa_error *error)
{
    frontend_remote_q3_resources resources;
    qa_application_native_q3_client_modules_recipe recipe;
    qa_frontend *f=frontend_remote_q3_frontend(row);
    qa_application_q3_remote_source source;
    if(!f || !out || *out || f->capture || f->resource_inventory || restoring!=f->source_restoring ||
        !(restoring?frontend_remote_q3_resources_import_read(row,&resources,error):
            frontend_remote_q3_resources_read(row,&resources,error)) ||
        !qa_application_native_q3_client_modules_recipe_read(f->application,&resources.domain.source,
            resources.domain.gamestate,&recipe,error) || !recipe.pure ||
        !qa_application_q3_remote_source_read(f->application,resources.domain.source.receiver.receiver,
            resources.domain.source.receiver.seat,resources.domain.epoch,&source,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Pure CLIENT transport requires its actual decoded SDK recipe");
    frontend_remote_q3_transport *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining pure decoded CLIENT transport");
    owner->row=row;
    if(!frontend_remote_q3_transport_attach(row,owner,error)) { free(owner); return false; }
    owner->attached=true; owner->constructing=true; *out=owner;
    qa_native_q3_remote_transport_services services={.source=source,.context=owner,.current=current,
        .idle=callbacks_idle,.milliseconds=milliseconds,.forward=forward,.release=release};
    bool okay=qa_native_q3_remote_client_transport_create(f->application,&services,&owner->transport,error);
    owner->constructing=false; return okay;
}
bool frontend_remote_q3_transport_create(frontend_remote_q3 *row,frontend_remote_q3_transport **out,qa_error *error)
{ return create(row,false,out,error); }
bool frontend_remote_q3_transport_create_restored(frontend_remote_q3 *row,frontend_remote_q3_transport **out,qa_error *error)
{ return create(row,true,out,error); }
bool frontend_remote_q3_transport_destroy(frontend_remote_q3_transport **out,qa_error *error)
{
    if(!out || !*out) return true;
    frontend_remote_q3_transport *owner=*out;
    qa_frontend *f=frontend_remote_q3_frontend(owner->row);
    if(!f || f->capture || f->resource_inventory || !owner->attached ||
        frontend_remote_q3_transport_read(owner->row)!=owner ||
        frontend_remote_q3_modules_read(owner->row) || !frontend_remote_q3_transport_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Pure CLIENT transport retirement requires returned source modules");
    owner->retiring=true;
    if(!qa_native_q3_remote_client_transport_destroy(&owner->transport,error) ||
        !frontend_remote_q3_transport_detach(owner->row,owner,error)) return false;
    owner->attached=false; free(owner); *out=NULL; return true;
}
