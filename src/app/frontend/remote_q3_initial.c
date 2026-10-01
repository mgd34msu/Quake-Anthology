#include "remote_q3_initial.h"
#include "remote_config.h"
#include "qa/q3_assets_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/font_save.h"

struct frontend_remote_q3_initial {
    qa_frontend *frontend;
    qa_application *application;
    qa_launch_instance_lease *descriptor;
    frontend_remote_q3_initial_view view;
    size_t users,children;
    bool constructing,ready;
};
static bool same_attempt(const frontend_network_client_attempt *a,
    const frontend_network_client_attempt *b)
{
    const qa_application_q3_client_context *x=&a->source.receiver,*y=&b->source.receiver;
    return a->epoch==b->epoch && a->restart_generation==b->restart_generation &&
        qa_net_address_equal(&a->endpoint,&b->endpoint,true) &&
        a->source.configuration_generation==b->source.configuration_generation &&
        a->source.connection_epoch==b->source.connection_epoch &&
        a->source.descriptor->storage==b->source.descriptor->storage &&
        a->configuration.owner==b->configuration.owner &&
        a->configuration.console==b->configuration.console && a->configuration.cvars==b->configuration.cvars &&
        x->session==y->session && x->receiver==y->receiver && x->seat==y->seat &&
        x->service_owner==y->service_owner && x->frontend_lifetime==y->frontend_lifetime &&
        x->console==y->console && x->cvars==y->cvars && x->native_source==y->native_source;
}
bool frontend_remote_q3_initial_idle(const frontend_remote_q3_initial *owner)
{
    if(!owner) return true;
    const frontend_remote_q3_initial_view *v=&owner->view;
    return !owner->users && !owner->constructing &&
        (!v->assets || qa_q3_assets_idle(v->assets)) &&
        (!v->images || qa_scene_resources_idle(v->images)) &&
        (!v->materials || qa_material_library_idle(v->materials)) &&
        (!v->fonts || qa_font_library_idle(v->fonts));
}
static bool build(frontend_remote_q3_initial *owner,qa_error *error)
{
    qa_frontend *f=owner->frontend;
    frontend_remote_q3_initial_view *v=&owner->view;
    const frontend_network_client_attempt *attempt=&v->attempt;
    if(!frontend_source_identity_allocate(f,&v->identity,error) ||
        !qa_launch_instance_retain_metadata(attempt->source.descriptor,&owner->descriptor,error)) return false;
    v->descriptor=qa_launch_instance_lease_view(owner->descriptor);
    v->attempt.source.descriptor=v->descriptor;
    if(!qa_native_q3_remote_client_product_read(owner->application,&v->attempt.source,&v->product,error) ||
        !frontend_remote_config_acquire(attempt->configuration.owner,&v->registry,error)) return false;
    if(frontend_client_registry_cvars(v->registry)!=attempt->source.receiver.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI acquired a different physical CLIENT heap");
    v->mounts=qa_vfs_clone(v->descriptor->content,error);
    v->images=v->mounts?qa_scene_resources_create(v->mounts,error):NULL;
    v->materials=v->images?qa_material_library_create(v->images,f->order,error):NULL;
    v->fonts=v->images?qa_font_library_create(v->mounts,v->images,error):NULL;
    v->movies=v->images?qa_media_library_create(v->images,error):NULL;
    qa_scene_image_options images={.family=QA_SCENE_Q3,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=-1};
    qa_q3_presentation_asset_options assets={.provider={v->mounts,v->images,v->materials,QA_SCENE_Q3}};
    if(!v->mounts || !v->images || !v->materials || !v->fonts || !v->movies ||
        !qa_material_library_load_scripts(v->materials,v->mounts,&images,error) ||
        !frontend_material_remaps(f,v->materials,error) ||
        !qa_audio_bank_create(v->mounts,&v->sounds,error)) return false;
    assets.sounds=v->sounds; assets.movies=v->movies;
    if(!qa_q3_presentation_assets_create(&assets,&v->assets,error) ||
        !frontend_network_client_attempt_current(f,&v->attempt))
        return error && error->code!=QA_OK?false:
            frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI media acquisition changed its actual attempt");
    return true;
}
bool frontend_remote_q3_initial_create(qa_frontend *f,const frontend_network_client_attempt *attempt,
    frontend_remote_q3_initial **out,qa_error *error)
{
    if(!f || !out || *out || !attempt || !f->application || !f->seats ||
        f->capture || f->source_restoring || f->round || f->shutdown ||
        !frontend_network_client_attempt_current(f,attempt))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI resources require their actual completed CLIENT attempt");
    uint32_t ordinal;
    if(!qa_application_constructor_seat_ordinal(f->application,attempt->source.receiver.receiver,
        attempt->source.receiver.seat,&ordinal,error)) return false;
    if(ordinal>=f->options.seats || attempt->configuration.physical_seat!=ordinal ||
        !f->seats[ordinal].input || f->seats[ordinal].frontend!=f || f->seats[ordinal].id!=ordinal ||
        attempt->source.receiver.console!=attempt->configuration.console ||
        attempt->source.receiver.cvars!=attempt->configuration.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI has no actual canonical physical input recipient");
    frontend_remote_q3_initial *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining initial remote UI media owner");
    owner->frontend=f; owner->application=f->application;
    owner->view=(frontend_remote_q3_initial_view){.owner=owner,.attempt=*attempt,
        .physical_seat=ordinal,.input=f->seats[ordinal].input};
    *out=owner; owner->constructing=true;
    bool preparing=f->preparing; f->preparing=true;
    bool ok=build(owner,error);
    f->preparing=preparing;
    owner->constructing=false; owner->ready=ok;
    return ok;
}
bool frontend_remote_q3_initial_read(const frontend_remote_q3_initial *owner,
    frontend_remote_q3_initial_view *out,qa_error *error)
{
    frontend_network_client_attempt attempt; bool present;
    if(!owner || !out || !owner->ready || owner->application!=owner->frontend->application ||
        !frontend_network_client_attempt_read(owner->frontend,&attempt,&present,error)) return false;
    const frontend_remote_q3_initial_view *v=&owner->view;
    if(!present || !same_attempt(&v->attempt,&attempt) ||
        v->physical_seat>=owner->frontend->options.seats || !owner->frontend->seats ||
        v->input!=owner->frontend->seats[v->physical_seat].input ||
        frontend_client_registry_cvars(v->registry)!=attempt.source.receiver.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI media leaves its retained attempt namespace");
    *out=*v; out->attempt=attempt; return true;
}
bool frontend_remote_q3_initial_current(const frontend_remote_q3_initial_view *v)
{
    const frontend_remote_q3_initial *owner=v?v->owner:NULL;
    const frontend_remote_q3_initial_view *actual=owner?&owner->view:NULL;
    return owner && owner->ready && owner->application==owner->frontend->application &&
        frontend_network_client_attempt_current(owner->frontend,&v->attempt) &&
        same_attempt(&actual->attempt,&v->attempt) && v->identity==actual->identity &&
        v->physical_seat==actual->physical_seat && v->product==actual->product &&
        v->descriptor==actual->descriptor && v->mounts==actual->mounts && v->images==actual->images &&
        v->materials==actual->materials && v->fonts==actual->fonts && v->sounds==actual->sounds &&
        v->movies==actual->movies && v->assets==actual->assets && v->registry==actual->registry &&
        v->input==actual->input && owner->frontend->seats &&
        v->physical_seat<owner->frontend->options.seats &&
        v->input==owner->frontend->seats[v->physical_seat].input;
}
bool frontend_remote_q3_initial_borrow(frontend_remote_q3_initial *owner,
    frontend_remote_q3_initial_view *out,qa_error *error)
{
    if(!owner || owner->users==SIZE_MAX || !frontend_remote_q3_initial_read(owner,out,error)) return false;
    ++owner->users; return true;
}
bool frontend_remote_q3_initial_child_retain(frontend_remote_q3_initial *owner,
    frontend_remote_q3_initial **out,qa_error *error)
{
    frontend_remote_q3_initial_view view;
    if(!owner || !out || *out || owner->children==SIZE_MAX ||
        !frontend_remote_q3_initial_read(owner,&view,error)) return false;
    ++owner->children; *out=owner; return true;
}
bool frontend_remote_q3_initial_child_release(frontend_remote_q3_initial **out,qa_error *error)
{
    if(!out || !*out) return true;
    frontend_remote_q3_initial *owner=*out;
    if(!owner->children || !frontend_remote_q3_initial_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI child still retains entered media callbacks");
    --owner->children; *out=NULL; return true;
}
void frontend_remote_q3_initial_release(frontend_remote_q3_initial *owner)
{ if(owner && owner->users) --owner->users; }
bool frontend_remote_q3_initial_destroy(frontend_remote_q3_initial **out,qa_error *error)
{
    if(!out || !*out) return true;
    frontend_remote_q3_initial *owner=*out;
    if(owner->frontend->capture || owner->children || !frontend_remote_q3_initial_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI media retains an actual entered borrower");
    frontend_remote_q3_initial_view *v=&owner->view;
    if(!frontend_client_registry_release(&v->registry,error)) return false;
    qa_q3_presentation_assets_destroy(v->assets); qa_media_library_destroy(v->movies);
    qa_font_library_destroy(v->fonts); qa_audio_bank_destroy(v->sounds);
    qa_material_library_destroy(v->materials); qa_scene_resources_destroy(v->images); qa_vfs_destroy(v->mounts);
    qa_launch_instance_lease_release(owner->descriptor);
    free(owner); *out=NULL; return true;
}
