#include "source_cinematics.h"
#include "qa/q3_assets_custody.h"
#include "renderer_materials.h"
#include "q3_color_policy.h"
#include "remote_q3_initial.h"
#include "remote_q3_modules_video.h"
#include "remote_q3_video_media.h"
#include "qa/media_library_prepare.h"
#include "remote_config.h"
#include "shared_resource_policy.h"
#include "visual_access.h"
#include "q3_render_policy.h"
#include "material_movie_bindings.h"
#include "network_restore.h"
#include "network_restore_attempt.h"
#include "source_restore.h"
#include "capture.h"
#include "qa/q3_assets_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/font_save.h"

struct frontend_remote_q3_initial {
    frontend_remote_q3_initial *next;
    qa_frontend *frontend;
    qa_application *application;
    qa_launch_instance_lease *descriptor;
    qa_native_q3_remote_client_transport *transport;
    frontend_material_movies *shader_movies;
    frontend_remote_q3_initial_view view;
    size_t users,children,transport_callbacks;
    uint64_t video_generation;
    bool constructing,ready,retiring,transport_released,importing;
};
size_t frontend_remote_q3_initial_count(const qa_frontend *f)
{
    size_t count=0;
    for (const frontend_remote_q3_initial *row=f?f->initial_resources:NULL;row;row=row->next) ++count;
    return count;
}
frontend_remote_q3_initial *frontend_remote_q3_initial_at(const qa_frontend *f,size_t index)
{
    frontend_remote_q3_initial *row=f?f->initial_resources:NULL;
    while (row && index--) row=row->next;
    return row;
}
bool frontend_remote_q3_initial_idle_all(const qa_frontend *f)
{
    for (const frontend_remote_q3_initial *row=f?f->initial_resources:NULL;row;row=row->next)
        if (row->frontend!=f || !frontend_remote_q3_initial_idle(row)) return false;
    return true;
}
bool frontend_remote_q3_initial_destroy_all(qa_frontend *f,qa_error *error)
{
    while (f && f->initial_resources) {
        frontend_remote_q3_initial *row=f->initial_resources;
        if (!frontend_remote_q3_initial_destroy(&row,error)) return false;
    }
    return true;
}
qa_frontend *frontend_remote_q3_initial_frontend(const frontend_remote_q3_initial *owner)
{ return owner?owner->frontend:NULL; }
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
bool frontend_remote_q3_initial_capture_current(const frontend_remote_q3_initial *owner)
{
    frontend_remote_q3_initial_view view;
    const frontend_capture *capture=owner && owner->frontend?owner->frontend->capture:NULL;
    if (!capture || owner->constructing || owner->retiring || owner->importing || owner->users ||
        owner->transport_callbacks || !frontend_remote_q3_initial_read(owner,&view,NULL) ||
        (owner->transport && !qa_native_q3_remote_client_transport_idle(owner->transport))) return false;
    for (size_t i=0;;++i) {
        const qa_q3_presentation_assets *actual=frontend_capture_assets_at(capture,i);
        if (!actual) return false;
        if (actual==view.assets) return frontend_remote_q3_initial_current(&view);
    }
}
bool frontend_remote_q3_initial_idle(const frontend_remote_q3_initial *owner)
{
    if(!owner) return true;
    const frontend_remote_q3_initial_view *v=&owner->view;
    return !owner->users && !owner->constructing && !owner->transport_callbacks &&
        (!owner->transport || qa_native_q3_remote_client_transport_idle(owner->transport)) &&
        (!v->assets || qa_q3_assets_idle(v->assets)) &&
        (!v->images || qa_scene_resources_idle(v->images)) &&
        (!v->materials || qa_material_library_idle(v->materials)) &&
        (!v->fonts || qa_font_library_idle(v->fonts));
}
static bool model_initialize(void *context,const qa_q3_model_opening *opening,
    const qa_model *native,qa_scene_model *root,qa_error *error)
{
    frontend_remote_q3_initial *owner=context;
    frontend_remote_q3_initial_view view;
    if (!frontend_remote_q3_initial_read(owner,&view,error)) return false;
    return frontend_visual_registered_model_initialize(owner->frontend,opening,native,root,error) &&
        frontend_remote_q3_initial_current(&view);
}
static bool shader_movies_current(void *context,const frontend_material_movie_source *view)
{
    frontend_remote_q3_initial *owner=context;
    const frontend_remote_q3_initial_view *v=owner?&owner->view:NULL;
    return owner && !owner->retiring && owner->descriptor && owner->application==owner->frontend->application &&
        view && view->context==owner && view->frontend==owner->frontend && view->files==v->mounts &&
        view->images==v->images && view->materials==v->materials && view->media==v->movies;
}
bool frontend_remote_q3_initial_movie_source_read(frontend_remote_q3_initial *owner,frontend_material_movie_source *out,qa_error *error)
{
    frontend_material_movie_source view={.frontend=owner?owner->frontend:NULL,.files=owner?owner->view.mounts:NULL,
        .images=owner?owner->view.images:NULL,.materials=owner?owner->view.materials:NULL,
        .media=owner?owner->view.movies:NULL,.context=owner,.current=shader_movies_current};
    if (!out || !shader_movies_current(owner,&view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Movie binding lacks its retained Initial provider");
    *out=view; return true;
}
static bool build_media(frontend_remote_q3_initial *owner,qa_error *error);
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
    return v->mounts && build_media(owner,error);
}
static bool build_media(frontend_remote_q3_initial *owner,qa_error *error)
{
    qa_frontend *f=owner->frontend;
    frontend_remote_q3_initial_view *v=&owner->view;
    if (!frontend_q3_source_color_ensure(f,error)) return false;
    v->images=v->mounts?qa_scene_resources_create(v->mounts,error):NULL;
    if(v->images && !frontend_image_policy_initialize(f,v->images,error))return false;
    v->materials=v->images?qa_material_library_create(v->images,f->order,error):NULL;
    v->fonts=v->images?qa_font_library_create(v->mounts,v->images,error):NULL;
    v->movies=v->images?qa_media_library_create(v->images,error):NULL;
    if (v->materials) {
        frontend_material_movie_source movie;
        if (!frontend_remote_q3_initial_movie_source_read(owner,&movie,error) ||
            !frontend_q3_material_profile_initialize(f,v->materials,error) ||
            !qa_material_library_set_source_upload(v->materials,frontend_q3_source_upload_read,f,error) ||
            !frontend_material_movies_create(&movie,&owner->shader_movies,error) ||
            !frontend_source_cinematics_ensure(f,v->images,error) ||
            !frontend_material_movies_cinematic_attach(owner->shader_movies,f->source_cinematics,v->physical_seat,v->identity,error)) return false;
    }
    qa_scene_image_options images={.family=QA_GAME_Q3,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=-1};
    qa_q3_presentation_asset_options assets={.strings=qa_session_strings(qa_application_session(f->application)),.provider={v->mounts,v->images,v->materials,QA_GAME_Q3,qa_application_world(f->application)},
        .context=owner,.model_initialize=model_initialize};
    if(!v->mounts || !v->images || !v->materials || !v->fonts || !v->movies ||
        !qa_material_library_load_scripts(v->materials,v->mounts,&images,error) ||
        !qa_material_library_source_shaders_initialize(v->materials,&images,error) ||
        !frontend_material_remaps(f,v->materials,error) ||
        !qa_audio_bank_create(v->mounts, qa_session_strings(qa_application_session(f->application)),&v->sounds,error)) return false;
    assets.sounds=v->sounds; assets.movies=v->movies;
    if(!qa_q3_presentation_assets_create(&assets,&v->assets,error) ||
        !frontend_network_client_attempt_current(f,&v->attempt))
        return error && error->code!=QA_OK?false:
            frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI media acquisition changed its actual attempt");
    return true;
}
bool frontend_remote_q3_initial_video_refresh(frontend_remote_q3_initial *owner,
    frontend_remote_q3_modules *modules,uint64_t *generation,qa_error *error)
{
    if (!owner || !generation || owner->frontend->application!=owner->application ||
        frontend_remote_q3_modules_initial_parent(modules)!=owner || owner->constructing ||
        owner->retiring || owner->importing || owner->users || owner->transport_callbacks ||
        owner->frontend->capture || owner->frontend->resource_inventory ||
        owner->video_generation==UINT64_MAX ||
        !frontend_remote_q3_modules_video_media_ready(modules,error) ||
        !frontend_network_client_attempt_current(owner->frontend,&owner->view.attempt))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial media refresh requires its actual closed UI ticket and attempt");
    frontend_remote_q3_initial_view *v=&owner->view;
    if ((v->assets && !qa_q3_assets_idle(v->assets)) ||
        (v->images && !qa_scene_resources_idle(v->images)) ||
        (v->materials && !qa_material_library_idle(v->materials)) ||
        (v->fonts && !qa_font_library_idle(v->fonts)) ||
        (v->movies && !qa_media_library_idle(v->movies)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial video media retains an actual renderer borrower");
    if (!frontend_material_movies_destroy(&owner->shader_movies,error)) return false;
    if (v->assets && !qa_q3_assets_services_retire(v->assets,error)) return false;
    qa_q3_presentation_assets_destroy(v->assets); v->assets=NULL;
    qa_media_library_destroy(v->movies); v->movies=NULL;
    qa_font_library_destroy(v->fonts); v->fonts=NULL;
    qa_audio_bank_destroy(v->sounds); v->sounds=NULL;
    qa_material_library_destroy(v->materials); v->materials=NULL;
    qa_scene_resources_destroy(v->images); v->images=NULL;
    owner->ready=false; owner->constructing=true;
    bool okay=build_media(owner,error);
    owner->constructing=false; owner->ready=okay;
    if (!okay) return false;
    *generation=++owner->video_generation;
    return true;
}
bool frontend_remote_q3_initial_video_read(const frontend_remote_q3_initial *owner,
    const frontend_remote_q3_modules *modules,frontend_remote_q3_initial_view *out,
    uint64_t *generation,bool *complete,qa_error *error)
{
    bool linked=false;
    for (const frontend_remote_q3_initial *row=owner && owner->frontend?owner->frontend->initial_resources:NULL;
        row;row=row->next) if (row==owner) linked=true;
    if (!linked || !modules || !out || !generation || !complete ||
        frontend_remote_q3_modules_initial_parent(modules)!=owner || owner->constructing ||
        owner->retiring || owner->importing || owner->application!=owner->frontend->application ||
        owner->view.descriptor!=qa_launch_instance_lease_view(owner->descriptor) ||
        !owner->view.mounts || frontend_client_registry_cvars(owner->view.registry)!=owner->view.attempt.source.receiver.cvars ||
        !frontend_network_client_attempt_current(owner->frontend,&owner->view.attempt))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial video media lost its actual retained CLIENT attempt");
    *out=owner->view; *generation=owner->video_generation; *complete=owner->ready;
    return true;
}
bool frontend_remote_q3_initial_create(qa_frontend *f,const frontend_network_client_attempt *attempt,
    frontend_remote_q3_initial **out,qa_error *error)
{
    if(!f || !out || *out || !attempt || !f->application || !f->seats ||
        f->capture || f->resource_inventory || f->source_restoring || f->round || f->shutdown ||
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
    owner->next=f->initial_resources; f->initial_resources=owner;
    owner->view=(frontend_remote_q3_initial_view){.owner=owner,.attempt=*attempt,
        .physical_seat=ordinal,.input=f->seats[ordinal].input};
    *out=owner; owner->constructing=true;
    bool preparing=f->preparing; f->preparing=true;
    bool ok=build(owner,error);
    f->preparing=preparing;
    owner->constructing=false; owner->ready=ok;
    return ok;
}
static bool public_attempt_read(const frontend_remote_q3_initial *owner,
    frontend_network_client_attempt *attempt,bool *present,qa_error *error)
{
    if (owner->frontend->source_restoring &&
        frontend_network_client_restore_attempt_read(owner->frontend,attempt,present,error) && *present &&
        frontend_network_client_restore_initial_completed_current(owner->frontend,owner,attempt)) return true;
    return frontend_network_client_attempt_read(owner->frontend,
        &owner->view.attempt.source.receiver,attempt,present,error);
}
static bool public_attempt_current(const frontend_remote_q3_initial *owner,
    const frontend_network_client_attempt *attempt)
{
    return frontend_network_client_restore_initial_completed_current(owner->frontend,owner,attempt) ||
        frontend_network_client_attempt_current(owner->frontend,attempt);
}
bool frontend_remote_q3_initial_read(const frontend_remote_q3_initial *owner,
    frontend_remote_q3_initial_view *out,qa_error *error)
{
    if(owner && owner->importing) return frontend_remote_q3_initial_import_read(owner,out,error);
    frontend_network_client_attempt attempt; bool present;
    if(!owner || !out || !owner->ready || owner->retiring || owner->application!=owner->frontend->application ||
        !public_attempt_read(owner,&attempt,&present,error)) return false;
    const frontend_remote_q3_initial_view *v=&owner->view;
    if(!present || !same_attempt(&v->attempt,&attempt) ||
        v->physical_seat>=owner->frontend->options.seats || !owner->frontend->seats ||
        v->input!=owner->frontend->seats[v->physical_seat].input ||
        frontend_client_registry_cvars(v->registry)!=attempt.source.receiver.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI media leaves its retained attempt namespace");
    *out=*v; out->attempt=attempt; return true;
}
static bool initial_fields_current(const frontend_remote_q3_initial_view *v)
{
    const frontend_remote_q3_initial *owner=v?v->owner:NULL;
    const frontend_remote_q3_initial_view *actual=owner?&owner->view:NULL;
    return owner && owner->ready && !owner->constructing && !owner->retiring &&
        owner->application==owner->frontend->application &&
        actual->attempt.source.descriptor && v->attempt.source.descriptor &&
        same_attempt(&actual->attempt,&v->attempt) && v->identity==actual->identity &&
        v->physical_seat==actual->physical_seat && v->product==actual->product &&
        v->descriptor==actual->descriptor && v->mounts==actual->mounts && v->images==actual->images &&
        v->materials==actual->materials && v->fonts==actual->fonts && v->sounds==actual->sounds &&
        v->movies==actual->movies && v->assets==actual->assets && v->registry==actual->registry &&
        v->input==actual->input && owner->frontend->seats &&
        v->physical_seat<owner->frontend->options.seats &&
        v->input==owner->frontend->seats[v->physical_seat].input;
}
bool frontend_remote_q3_initial_current(const frontend_remote_q3_initial_view *v)
{
    if(v && v->owner && v->owner->importing) return frontend_remote_q3_initial_import_current(v);
    return v && v->owner && public_attempt_current(v->owner,&v->attempt) &&
        initial_fields_current(v);
}

bool frontend_remote_q3_initial_import_current(const frontend_remote_q3_initial_view *view)
{
    const frontend_remote_q3_initial *owner=view?view->owner:NULL;
    return owner && owner->importing && owner->frontend->source_restoring && !owner->frontend->resource_inventory &&
        initial_fields_current(view) && frontend_client_registry_cvars(view->registry)==view->attempt.source.receiver.cvars &&
        frontend_network_client_restore_attempt_current(owner->frontend,&view->attempt);
}
bool frontend_remote_q3_initial_import_read(const frontend_remote_q3_initial *owner,
    frontend_remote_q3_initial_view *out,qa_error *error)
{
    frontend_network_client_attempt attempt; bool present=false;
    if(!owner || !out || !owner->importing || !owner->ready ||
        !frontend_network_client_restore_attempt_read(owner->frontend,&attempt,&present,error) || !present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI resources lack their actual staged attempt");
    frontend_remote_q3_initial_view view=owner->view; view.attempt=attempt;
    if(!frontend_remote_q3_initial_import_current(&view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI graph changed its staged physical namespace");
    *out=view; return true;
}
bool frontend_remote_q3_initial_finish_import(frontend_remote_q3_initial *owner,qa_error *error)
{
    frontend_network_client_attempt attempt; bool present=false;
    if(!owner || !owner->importing || owner->frontend->capture || owner->frontend->resource_inventory ||
        !frontend_remote_q3_initial_idle(owner) ||
        !public_attempt_read(owner,&attempt,&present,error) || !present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial UI graph promotion requires its actual returned public attempt");
    frontend_remote_q3_initial_view view=owner->view; view.attempt=attempt;
    if(!initial_fields_current(&view) || !public_attempt_current(owner,&attempt))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restored initial UI changed before public admission");
    owner->view.attempt=attempt; owner->importing=false; return true;
}
bool frontend_remote_q3_initial_metadata_read(const frontend_remote_q3_initial *owner,
    frontend_remote_q3_initial_view *out,qa_error *error)
{
    frontend_network_client_attempt attempt;
    frontend_remote_q3_initial *actual=NULL;
    frontend_remote_q3_modules *modules=NULL; bool present=false;
    if (!owner || !out || !owner->ready || owner->constructing || owner->retiring || owner->importing ||
        owner->application!=owner->frontend->application || !owner->frontend->resource_inventory ||
        !frontend_network_initial_metadata_read(owner->frontend,&attempt,&actual,&modules,&present,error) ||
        !present || actual!=owner)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"InitialUI metadata lacks its installed complete resource parent");
    frontend_remote_q3_initial_view view=owner->view; view.attempt=attempt;
    if (!initial_fields_current(&view) ||
        frontend_client_registry_cvars(view.registry)!=attempt.source.receiver.cvars ||
        !frontend_network_initial_metadata_current(owner->frontend,&attempt,owner,modules))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"InitialUI metadata changed its physical resource namespace");
    *out=view; return true;
}
bool frontend_remote_q3_initial_metadata_current(const frontend_remote_q3_initial_view *view)
{
    const frontend_remote_q3_initial *owner=view?view->owner:NULL;
    frontend_network_client_attempt actual;
    frontend_remote_q3_initial *parent=NULL;
    frontend_remote_q3_modules *modules=NULL; bool present=false;
    return owner && !owner->importing && owner->frontend->resource_inventory && initial_fields_current(view) &&
        frontend_client_registry_cvars(view->registry)==view->attempt.source.receiver.cvars &&
        frontend_network_initial_metadata_read(owner->frontend,&actual,&parent,&modules,&present,NULL) &&
        present && parent==owner &&
        frontend_network_initial_metadata_current(owner->frontend,&view->attempt,owner,modules);
}
bool frontend_remote_q3_initial_borrow(frontend_remote_q3_initial *owner,
    frontend_remote_q3_initial_view *out,qa_error *error)
{
    if(!owner || owner->frontend->resource_inventory || owner->users==SIZE_MAX ||
        !frontend_remote_q3_initial_read(owner,out,error)) return false;
    ++owner->users; return true;
}
bool frontend_remote_q3_initial_child_retain(frontend_remote_q3_initial *owner,
    frontend_remote_q3_initial **out,qa_error *error)
{
    frontend_remote_q3_initial_view view;
    if(!owner || !out || *out || owner->frontend->resource_inventory || owner->children==SIZE_MAX ||
        !frontend_remote_q3_initial_read(owner,&view,error)) return false;
    ++owner->children; *out=owner; return true;
}
bool frontend_remote_q3_initial_child_release(frontend_remote_q3_initial **out,qa_error *error)
{
    if(!out || !*out) return true;
    frontend_remote_q3_initial *owner=*out;
    if(owner->frontend->resource_inventory || !owner->children || !frontend_remote_q3_initial_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI child still retains entered media callbacks");
    --owner->children; *out=NULL; return true;
}
void frontend_remote_q3_initial_release(frontend_remote_q3_initial *owner)
{ if(owner && owner->users) --owner->users; }
static bool transport_current(void *context,const qa_application_q3_remote_source *source)
{
    frontend_remote_q3_initial *owner=context;
    frontend_remote_q3_initial_view view;
    if(!owner || !source || owner->transport_released || !frontend_remote_q3_initial_read(owner,&view,NULL)) return false;
    view.attempt.source=*source;
    return frontend_remote_q3_initial_current(&view);
}
static bool transport_idle(void *context)
{ const frontend_remote_q3_initial *owner=context; return owner && !owner->transport_callbacks; }
static uint32_t transport_milliseconds(void *context)
{
    const frontend_remote_q3_initial *owner=context;
    return (uint32_t)(owner->frontend->wall_time_ns/UINT64_C(1000000));
}
static bool transport_forward(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_remote_q3_initial *owner=context;
    frontend_remote_q3_initial_view view;
    if(!owner || owner->transport_callbacks || owner->transport_released ||
        !frontend_remote_q3_initial_read(owner,&view,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting CLIENT forwarding has no current retained attempt");
    ++owner->transport_callbacks;
    bool ok=frontend_network_client_forward(owner->frontend,call,error);
    --owner->transport_callbacks; return ok;
}
static bool transport_release(void *context,qa_error *error)
{
    frontend_remote_q3_initial *owner=context;
    if(!transport_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting CLIENT transport still retains an invocation");
    owner->transport_released=true; return true;
}
bool frontend_remote_q3_initial_transport_create(frontend_remote_q3_initial *owner,qa_error *error)
{
    frontend_remote_q3_initial_view view;
    if(!owner || owner->frontend->resource_inventory || owner->transport || owner->transport_released ||
        !frontend_remote_q3_initial_idle(owner) || !frontend_remote_q3_initial_read(owner,&view,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting CLIENT transport requires its actual unentered parent");
    qa_native_q3_remote_transport_services services={.source=view.attempt.source,.context=owner,
        .current=transport_current,.idle=transport_idle,.milliseconds=transport_milliseconds,
        .forward=transport_forward,.release=transport_release};
    return qa_native_q3_remote_client_transport_create(owner->application,&services,&owner->transport,error);
}
qa_native_q3_remote_client_transport *frontend_remote_q3_initial_transport_read(
    const frontend_remote_q3_initial *owner)
{ return owner?owner->transport:NULL; }
bool frontend_remote_q3_initial_destroy(frontend_remote_q3_initial **out,qa_error *error)
{
    if(!out || !*out) return true;
    frontend_remote_q3_initial *owner=*out;
    if(owner->frontend->capture || owner->frontend->resource_inventory ||
        owner->children || !frontend_remote_q3_initial_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Connecting UI media retains an actual entered borrower");
    frontend_remote_q3_initial **link=&owner->frontend->initial_resources;
    while (*link && *link!=owner) link=&(*link)->next;
    if (*link!=owner) return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial resource retirement lost its parent custody");
    owner->retiring=true;
    if(!qa_native_q3_remote_client_transport_destroy(&owner->transport,error)) return false;
    frontend_remote_q3_initial_view *v=&owner->view;
    if(!frontend_client_registry_release(&v->registry,error)) return false;
    if (owner->shader_movies && owner->view.movies && !owner->frontend->source_restoring) {
        frontend_material_movie_source expected={.frontend=owner->frontend,.files=owner->view.mounts,.images=owner->view.images,
            .materials=owner->view.materials,.media=owner->view.movies,.context=owner,.current=shader_movies_current};
        if (!frontend_renderer_materials_adopt_movies(owner->frontend,&expected,&owner->shader_movies,&owner->view.movies,error)) return false;
    }
    if (!frontend_material_movies_destroy(&owner->shader_movies,error)) return false;
    if (v->assets && !qa_q3_assets_services_retire(v->assets,error)) return false;
    qa_q3_presentation_assets_destroy(v->assets); qa_media_library_destroy(v->movies);
    qa_font_library_destroy(v->fonts); qa_audio_bank_destroy(v->sounds);
    qa_material_library_destroy(v->materials); qa_scene_resources_destroy(v->images); qa_vfs_destroy(v->mounts);
    qa_launch_instance_lease_release(owner->descriptor);
    *link=owner->next; free(owner); *out=NULL; return true;
}

bool frontend_remote_q3_initial_material_bindings_restore(qa_frontend *f,qa_error *error)
{
    if (!f || !f->source_restoring || f->capture || f->resource_inventory)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source material rebinding requires its genuine imported physical banks");
    for (frontend_remote_q3_initial *owner=f->initial_resources;owner;owner=owner->next)
        if (owner->view.materials && !frontend_q3_material_source_bind(f,owner->view.materials,error)) return false;
    return true;
}
