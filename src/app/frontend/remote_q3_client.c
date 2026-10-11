#include "source_cinematics.h"
#include "qa/q3_assets_custody.h"
#include "renderer_materials.h"
#include "q3_color_policy.h"
#include "remote_q3_private.h"
#include "remote_q3_services.h"
#include "remote_q3_modules.h"
#include "remote_q3_modules_video.h"
#include "remote_q3_video_media.h"
#include "remote_q3_compiled_video.h"
#include "qa/media_library_prepare.h"
#include "remote_q3_frame.h"
#include "remote_q3_runtime.h"
#include "remote_q3_transport.h"
#include "remote_config.h"
#include "shared_resource_policy.h"
#include "visual_access.h"
#include "q3_render_policy.h"
#include "material_movie_bindings.h"
#include "network_restore.h"
#include "network_q3_restart.h"
#include "network_prediction.h"
#include "source_restore.h"
#include "capture.h"
#include "qa/application_character_selection.h"
#include "qa/scene_world_save.h"
#include "qa/q3_assets_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/font_save.h"

static bool linked(const frontend_remote_q3 *row)
{
    for(const frontend_remote_q3 *p=row && row->frontend?row->frontend->remote_q3:NULL;p;p=p->next)
        if(p==row) return true;
    return false;
}
static bool same_domain(const frontend_network_client_domain *a,const frontend_network_client_domain *b)
{
    const qa_application_q3_client_context *x=&a->source.receiver,*y=&b->source.receiver;
    return qa_net_client_id_equal(a->connection,b->connection) && a->epoch==b->epoch &&
        a->content_owner==b->content_owner && a->content==b->content && a->prepared_mounts==b->prepared_mounts &&
        a->map==b->map && a->product==b->product && a->gamestate==b->gamestate &&
        a->source.descriptor->storage==b->source.descriptor->storage &&
        a->source.configuration_generation==b->source.configuration_generation &&
        x->session==y->session && x->receiver==y->receiver && x->seat==y->seat &&
        x->service_owner==y->service_owner && x->frontend_lifetime==y->frontend_lifetime &&
        x->console==y->console && x->cvars==y->cvars && x->native_source==y->native_source &&
        x->source_client==y->source_client && a->initial.server_message==b->initial.server_message &&
        a->initial.last_executed_server_command==b->initial.last_executed_server_command &&
        a->initial.client_number==b->initial.client_number;
}
static bool resources_idle(const frontend_remote_q3 *row)
{
    const frontend_remote_q3_resources *v=&row->resources;
    return !row->constructing && !row->users && frontend_remote_q3_modules_idle(row->modules) &&
        frontend_remote_q3_transport_idle(row->transport) &&
        frontend_remote_q3_frame_idle(row->frames) &&
        frontend_remote_q3_runtime_idle(row->runtime) &&
        frontend_remote_q3_services_idle(row->services) &&
        (!v->assets || qa_q3_assets_idle(v->assets)) &&
        (!v->images || qa_scene_resources_idle(v->images)) &&
        (!v->materials || qa_material_library_idle(v->materials)) &&
        (!v->fonts || qa_font_library_idle(v->fonts)) && (!v->world || qa_scene_world_idle(v->world));
}
bool frontend_remote_q3_idle(const qa_frontend *f)
{
    if(!f) return false;
    for(const frontend_remote_q3 *row=f->remote_q3;row;row=row->next)
        if(!resources_idle(row)) return false;
    return true;
}
static bool model_initialize(void *context,const qa_q3_model_opening *opening,
    const qa_model *native,qa_scene_model *root,qa_error *error)
{
    frontend_remote_q3 *row=context;
    frontend_remote_q3_resources view;
    if (!frontend_remote_q3_resources_read(row,&view,error)) return false;
    return frontend_visual_registered_model_initialize(row->frontend,opening,native,root,error) &&
        frontend_remote_q3_resources_current(&view);
}
static bool shader_movies_current(void *context,const frontend_material_movie_source *view)
{
    frontend_remote_q3 *row=context;
    const frontend_remote_q3_resources *v=row?&row->resources:NULL;
    return linked(row) && !row->retiring && row->application==row->frontend->application && row->descriptor &&
        view && view->context==row && view->frontend==row->frontend && view->files==v->mounts &&
        view->images==v->images && view->materials==v->materials && view->media==v->movies;
}
bool frontend_remote_q3_movie_source_read(frontend_remote_q3 *row,frontend_material_movie_source *out,qa_error *error)
{
    frontend_material_movie_source view={.frontend=row?row->frontend:NULL,.files=row?row->resources.mounts:NULL,
        .images=row?row->resources.images:NULL,.materials=row?row->resources.materials:NULL,
        .media=row?row->resources.movies:NULL,.context=row,.current=shader_movies_current};
    if (!out || !shader_movies_current(row,&view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Movie binding lacks its retained remote provider");
    *out=view; return true;
}
static bool build_resources(frontend_remote_q3 *,frontend_remote_config *,qa_error *);

bool frontend_remote_q3_resources_create(qa_frontend *f,const frontend_network_client_domain *domain,
    frontend_remote_q3 **out,qa_error *error)
{
    if(!f || !f->application || !domain || !out || *out || f->capture || f->resource_inventory || f->source_restoring ||
        f->round || !f->seats || !frontend_network_client_domain_current(f,domain) ||
        domain->source.descriptor->selection.runtime!=QA_PROGRAM_BUILTIN ||
        domain->source.descriptor->artifact || !domain->source.receiver.native_source)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote resources require the actual builtin CLIENT constructor domain");
    uint32_t ordinal;
    if(!qa_application_constructor_seat_ordinal(f->application,domain->source.receiver.receiver,
        domain->source.receiver.seat,&ordinal,error)) return false;
    if(ordinal>=f->options.seats || !f->seats[ordinal].input)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote resources lack their real physical input recipient");
    for(frontend_remote_q3 *p=f->remote_q3;p;p=p->next)
        if(p->resources.domain.source.receiver.receiver==domain->source.receiver.receiver &&
            p->resources.domain.source.receiver.seat==domain->source.receiver.seat)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote CLIENT resource owner is already retained");
    frontend_remote_config *configuration=frontend_config_store_client(f->config_store,domain->source.receiver.cvars);
    frontend_remote_config_view settings;
    if(!configuration || !frontend_remote_config_read(configuration,&settings) || !settings.ready || !settings.published ||
        settings.physical_seat!=ordinal || settings.console!=domain->source.receiver.console ||
        settings.cvars!=domain->source.receiver.cvars || !frontend_remote_config_current(configuration,&settings))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote resources lost their completed canonical CLIENT configuration");
    if(qa_resource_pool_find(qa_vfs_resources(domain->content),qa_resource_id(domain->map))!=domain->map)
        return frontend_fail(error,QA_ERROR_FORMAT,"Native remote map leaves the actual descriptor content pool");
    frontend_remote_q3 *row=calloc(1,sizeof(*row));
    if(!row) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining native remote resource owner");
    row->frontend=f; row->application=f->application;
    row->resources=(frontend_remote_q3_resources){.owner=row,.domain=*domain,.physical_seat=ordinal,
        .input=f->seats[ordinal].input,.map=domain->map};
    row->next=f->remote_q3; f->remote_q3=row; *out=row;
    row->constructing=true;
    bool ok=build_resources(row,configuration,error);
    row->constructing=false; row->resources_ready=ok;
    return ok;
}
static bool build_media(frontend_remote_q3 *row,qa_error *error);
static bool build_resources(frontend_remote_q3 *row,frontend_remote_config *configuration,qa_error *error)
{
    qa_frontend *f=row->frontend;
    const frontend_network_client_domain *domain=&row->resources.domain;
    if(!frontend_source_identity_allocate(f,&row->resources.identity,error) ||
        !qa_launch_instance_retain_metadata(domain->source.descriptor,&row->descriptor,error)) return false;
    row->resources.descriptor=qa_launch_instance_lease_view(row->descriptor);
    row->resources.domain.source.descriptor=row->resources.descriptor;
    if(!frontend_remote_config_acquire(configuration,&row->resources.registry,error)) return false;
    if(frontend_client_registry_cvars(row->resources.registry)!=domain->source.receiver.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote resources adopted a different CLIENT registry heap");
    row->map=(qa_resource *)domain->map; qa_resource_retain(row->map);
    frontend_remote_q3_resources *v=&row->resources;
    v->mounts=qa_vfs_clone(domain->content,error);
    return v->mounts && build_media(row,error);
}
static bool build_media(frontend_remote_q3 *row,qa_error *error)
{
    qa_frontend *f=row->frontend;
    frontend_remote_q3_resources *v=&row->resources;
    const frontend_network_client_domain *domain=&v->domain;
    if (!frontend_q3_source_color_ensure(f,error)) return false;
    v->images=v->mounts?qa_scene_resources_create(v->mounts,error):NULL;
    if(v->images && !frontend_image_policy_initialize(f,v->images,error))return false;
    v->materials=v->images?qa_material_library_create(v->images,f->order,error):NULL;
    v->fonts=v->images?qa_font_library_create(v->mounts,v->images,error):NULL;
    v->movies=v->images?qa_media_library_create(v->images,error):NULL;
    if (v->materials) {
        frontend_material_movie_source movie;
        if (!frontend_remote_q3_movie_source_read(row,&movie,error) ||
            !frontend_q3_material_profile_initialize(f,v->materials,error) ||
            !qa_material_library_set_source_upload(v->materials,frontend_q3_source_upload_read,f,error) ||
            !frontend_material_movies_create(&movie,&row->shader_movies,error) ||
            !frontend_source_cinematics_ensure(f,v->images,error) ||
            !frontend_material_movies_cinematic_attach(row->shader_movies,f->source_cinematics,v->physical_seat,v->identity,error)) return false;
    }
    qa_scene_image_options images={.family=QA_GAME_Q3,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=-1};
    qa_bsp_view bsp;
    if(!v->mounts || !v->images || !v->materials || !v->fonts || !v->movies ||
        !qa_material_library_load_scripts(v->materials,v->mounts,&images,error) ||
        !qa_material_library_source_shaders_initialize(v->materials,&images,error) ||
        !frontend_material_remaps(f,v->materials,error) || !qa_audio_bank_create(v->mounts,&v->sounds,error) ||
        !qa_bsp_open(qa_resource_bytes(row->map),&bsp,error)) return false;
    if(bsp.family!=QA_BSP_Q3)
        return frontend_fail(error,QA_ERROR_FORMAT,"Native remote CLIENT requires its actual Q3 BSP map");
    qa_scene_world_options options={.images=images,.subdivisions=64,.q1_water_alpha=1,
        .q2_light_modulate=1,.q3_overbright=1};
    qa_q3_presentation_asset_options assets={.strings=qa_session_strings(qa_application_session(f->application)),.provider={v->mounts,v->images,v->materials,QA_GAME_Q3,qa_application_world(f->application)},
        .sounds=v->sounds,.movies=v->movies,.context=row,.model_initialize=model_initialize};
    if (!frontend_q3_world_policy_initialize(f,&options,error)) return false;
    if((!v->geometry && !qa_collision_create(&bsp,&v->geometry,error)) ||
        !qa_collision_bind_resource(v->geometry,row->map,error) ||
        !frontend_network_prediction_geometry_prepare(f,domain->source.receiver.receiver,
            domain->source.receiver.seat,v->geometry,&v->trace_scratch,error)) return false;
    options.geometry=v->geometry;
    if (!qa_scene_world_create(&bsp,v->images,v->materials,&options,&v->world,error) ||
        !qa_scene_world_source_resource_bind(v->world,row->map,error) ||
        !frontend_world_scratch_create(v->world,&v->world_scratch,error) ||
        !qa_q3_presentation_assets_create(&assets,&v->assets,error)) return false;
    if(!frontend_network_client_domain_current(f,domain))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote constructor callbacks changed their actual CLIENT domain");
    return true;
}
static bool rebuild_media(frontend_remote_q3 *row,qa_error *error)
{
    frontend_remote_q3_resources *v=&row->resources;
    if (!frontend_material_movies_destroy(&row->shader_movies,error)) return false;
    if (v->assets && !qa_q3_assets_services_retire(v->assets,error)) return false;
    qa_q3_presentation_assets_destroy(v->assets); v->assets=NULL;
    frontend_world_scratch_destroy(&v->world_scratch);
    qa_scene_world_destroy(v->world); v->world=NULL;
    qa_media_library_destroy(v->movies); v->movies=NULL;
    qa_font_library_destroy(v->fonts); v->fonts=NULL;
    qa_audio_bank_destroy(v->sounds); v->sounds=NULL;
    qa_material_library_destroy(v->materials); v->materials=NULL;
    qa_scene_resources_destroy(v->images); v->images=NULL;
    row->resources_ready=false; row->constructing=true;
    bool okay=build_media(row,error);
    row->constructing=false; row->resources_ready=okay;
    if (okay) ++row->video_generation;
    return okay;
}
bool frontend_remote_q3_resources_video_refresh(frontend_remote_q3 *row,
    frontend_remote_q3_modules *modules,uint64_t *generation,qa_error *error)
{
    if (!row || !generation || !linked(row) || row->application!=row->frontend->application ||
        row->modules!=modules || row->runtime || row->services || row->frames ||
        row->constructing || row->retiring || row->importing || row->users ||
        row->frontend->capture || row->frontend->resource_inventory ||
        row->video_generation==UINT64_MAX ||
        !frontend_remote_q3_modules_video_media_ready(modules,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote media refresh requires its exact closed acquired CG owner");
    frontend_remote_q3_resources *v=&row->resources;
    if ((v->assets && !qa_q3_assets_idle(v->assets)) ||
        (v->images && !qa_scene_resources_idle(v->images)) ||
        (v->materials && !qa_material_library_idle(v->materials)) ||
        (v->fonts && !qa_font_library_idle(v->fonts)) ||
        (v->movies && !qa_media_library_idle(v->movies)) ||
        (v->world && !qa_scene_world_idle(v->world)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote video media retains a real renderer borrower");
    if (!rebuild_media(row,error)) return false;
    *generation=row->video_generation;
    return true;
}
bool frontend_remote_q3_resources_video_read(const frontend_remote_q3 *row,
    const frontend_remote_q3_modules *modules,frontend_remote_q3_resources *out,
    uint64_t *generation,bool *complete,qa_error *error)
{
    if (!row || !modules || !out || !generation || !complete || !linked(row) ||
        row->modules!=modules || frontend_remote_q3_modules_parent(modules)!=row ||
        row->application!=row->frontend->application || row->constructing || row->retiring || row->importing ||
        row->resources.descriptor!=qa_launch_instance_lease_view(row->descriptor) ||
        row->resources.map!=row->map || !row->map || !row->resources.mounts ||
        frontend_client_registry_cvars(row->resources.registry)!=row->resources.domain.source.receiver.cvars ||
        qa_resource_pool_find(qa_vfs_resources(row->resources.mounts),qa_resource_id(row->map))!=row->map ||
        !frontend_network_client_domain_current(row->frontend,&row->resources.domain))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Video media lost its retained physical CLIENT graph");
    *out=row->resources; *generation=row->video_generation; *complete=row->resources_ready;
    return true;
}
bool frontend_remote_q3_resources_read(const frontend_remote_q3 *row,frontend_remote_q3_resources *out,qa_error *error)
{
    if(row && row->importing) return frontend_remote_q3_resources_import_read(row,out,error);
    frontend_network_client_domain domain;
    if(!row || !out || !linked(row) || !row->resources_ready || row->application!=row->frontend->application ||
        !frontend_network_client_domain_read(row->frontend,
            &row->resources.domain.source.receiver,&domain,error)) return false;
    if(!same_domain(&row->resources.domain,&domain) || !row->resources.descriptor ||
        row->resources.descriptor->storage!=domain.source.descriptor->storage ||
        row->resources.physical_seat>=row->frontend->options.seats ||
        row->resources.input!=row->frontend->seats[row->resources.physical_seat].input ||
        frontend_client_registry_cvars(row->resources.registry)!=domain.source.receiver.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote resource receipt differs from its retained physical CLIENT");
    *out=row->resources; out->domain=domain; return true;
}
bool frontend_remote_q3_resources_compiled_video_read(const frontend_remote_q3 *row,
    frontend_remote_q3_resources *out,qa_error *error)
{
    frontend_network_client_domain domain;
    if(!row || !out || !linked(row) ||
        !frontend_remote_q3_compiled_video_parent_is(row,row->compiled_video) ||
        row->application!=row->frontend->application || row->retiring || row->importing ||
        row->frontend->capture || row->frontend->resource_inventory || row->frontend->source_restoring ||
        !frontend_network_client_domain_read(row->frontend,
            &row->resources.domain.source.receiver,&domain,error))return false;
    const frontend_remote_q3_resources *v=&row->resources;
    if(!same_domain(&v->domain,&domain) || !row->descriptor ||
        v->descriptor!=qa_launch_instance_lease_view(row->descriptor) ||
        v->descriptor->storage!=domain.source.descriptor->storage || !v->mounts ||
        !row->map || v->map!=row->map || !v->geometry ||
        qa_collision_resource(v->geometry)!=row->map ||
        qa_resource_pool_find(qa_vfs_resources(v->mounts),qa_resource_id(row->map))!=row->map ||
        v->physical_seat>=row->frontend->options.seats ||
        v->input!=row->frontend->seats[v->physical_seat].input ||
        frontend_client_registry_cvars(v->registry)!=domain.source.receiver.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Compiled video lost its retained Source map or CLIENT namespace");
    *out=*v; out->domain=domain; return true;
}
bool frontend_remote_q3_resources_compiled_video_refresh(frontend_remote_q3 *row,qa_error *error)
{
    frontend_remote_q3_resources resources;
    if(!frontend_remote_q3_resources_compiled_video_read(row,&resources,error) || row->constructing ||
        row->video_generation==UINT64_MAX || !frontend_remote_q3_compiled_video_media_ready(row,error))return false;
    frontend_remote_q3_resources *v=&row->resources;
    if((v->assets && !qa_q3_assets_idle(v->assets)) || (v->images && !qa_scene_resources_idle(v->images)) ||
        (v->materials && !qa_material_library_idle(v->materials)) || (v->fonts && !qa_font_library_idle(v->fonts)) ||
        (v->movies && !qa_media_library_idle(v->movies)) || (v->world && !qa_scene_world_idle(v->world)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Compiled video media retains an actual renderer borrower");
    return rebuild_media(row,error);
}
static bool resources_fields_current(const frontend_remote_q3_resources *v)
{
    const frontend_remote_q3 *row=v?v->owner:NULL;
    const frontend_remote_q3_resources *actual=row?&row->resources:NULL;
    return row && linked(row) && row->resources_ready && row->application==row->frontend->application &&
        actual->domain.source.descriptor && v->domain.source.descriptor &&
        same_domain(&actual->domain,&v->domain) &&
        v->identity==actual->identity && v->physical_seat==actual->physical_seat &&
        v->descriptor==actual->descriptor && v->mounts==actual->mounts && v->images==actual->images &&
        v->materials==actual->materials && v->fonts==actual->fonts && v->sounds==actual->sounds &&
        v->movies==actual->movies && v->assets==actual->assets && v->world==actual->world &&
        v->geometry==actual->geometry && v->map==row->map && v->registry==actual->registry &&
        v->input==actual->input && v->physical_seat<row->frontend->options.seats &&
        v->input==row->frontend->seats[v->physical_seat].input;
}
bool frontend_remote_q3_resources_current(const frontend_remote_q3_resources *v)
{
    const frontend_remote_q3 *row=v?v->owner:NULL;
    if(row && row->importing) return frontend_remote_q3_resources_import_current(v);
    return row && linked(row) && row->resources_ready && row->application==row->frontend->application &&
        frontend_network_client_domain_current(row->frontend,&v->domain) && resources_fields_current(v);
}
bool frontend_remote_q3_resources_import_current(const frontend_remote_q3_resources *v)
{
    const frontend_remote_q3 *row=v?v->owner:NULL;
    return row && row->importing && row->frontend->source_restoring && !row->retiring &&
        !row->frontend->resource_inventory && resources_fields_current(v) &&
        v->domain.restart_generation==row->resources.domain.restart_generation &&
        frontend_client_registry_cvars(v->registry)==v->domain.source.receiver.cvars &&
        frontend_network_client_restore_domain_current(row->frontend,&v->domain);
}
bool frontend_remote_q3_resources_import_read(const frontend_remote_q3 *row,
    frontend_remote_q3_resources *out,qa_error *error)
{
    frontend_network_client_domain domain;
    if(!row || !out || !linked(row) || !row->importing || !row->resources_ready ||
        !frontend_network_client_restore_domain_read(row->frontend,&domain,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restored remote resources lack their actual staged parent");
    frontend_remote_q3_resources view=row->resources; view.domain=domain;
    if(!frontend_remote_q3_resources_import_current(&view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restored remote resource heaps changed their staged CLIENT namespace");
    *out=view; return true;
}
bool frontend_remote_q3_resources_world_adopt_ready(frontend_remote_q3 *row,qa_scene_world *world,qa_error *error)
{
    frontend_remote_q3_resources v;
    if(!frontend_remote_q3_resources_import_read(row,&v,error) || v.world || !world || !v.geometry ||
        !qa_scene_world_idle(world) || qa_scene_world_resource_owner(world)!=v.images ||
        qa_scene_world_material_owner(world)!=v.materials)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restored remote world adoption requires its decoded root and paired heaps");
    return frontend_world_scratch_create(world,&row->resources.world_scratch,error);
}
void frontend_remote_q3_resources_world_adopt(frontend_remote_q3 *row,qa_scene_world *world)
{ row->resources.world=world; }

bool frontend_remote_q3_resources_metadata_current(const frontend_remote_q3_resources *v)
{
    const frontend_remote_q3 *row=v?v->owner:NULL;
    return row && row->frontend && row->frontend->resource_inventory && !row->constructing && !row->retiring && !row->importing &&
        resources_fields_current(v) &&
        frontend_client_registry_cvars(v->registry)==v->domain.source.receiver.cvars &&
        frontend_network_client_domain_metadata_current(row->frontend,&v->domain);
}
bool frontend_remote_q3_resources_metadata_read(const frontend_remote_q3 *row,
    frontend_remote_q3_resources *out,qa_error *error)
{
    frontend_network_client_domain domain;
    if(!row || !out || !linked(row) || !row->resources_ready || row->constructing || row->retiring ||
        row->application!=row->frontend->application || !row->frontend->resource_inventory ||
        !frontend_network_client_domain_metadata_read(row->frontend,
            &row->resources.domain.source.receiver,&domain,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Resource inventory lacks its retained remote CLIENT metadata");
    frontend_remote_q3_resources receipt=row->resources; receipt.domain=domain;
    if(!frontend_remote_q3_resources_metadata_current(&receipt))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote CLIENT metadata differs from its installed resource holders");
    *out=receipt; return true;
}
bool frontend_remote_q3_basis_read(const frontend_remote_q3 *row,qa_native_q3_remote_client_basis *out,qa_error *error)
{
    frontend_remote_q3_resources v;
    if(!out || !(row && row->compiled_video?
        frontend_remote_q3_resources_compiled_video_read(row,&v,error):
        frontend_remote_q3_resources_read(row,&v,error))) return false;
    uint64_t publication; qa_q3_product product;
    if(!qa_native_q3_remote_client_publication_read(row->application,&v.domain.source,&publication,error) ||
        !qa_native_q3_remote_client_product_read(row->application,&v.domain.source,&product,error)) return false;
    qa_native_q3_remote_client_basis basis={.application=row->application,
        .session=qa_application_session(row->application),.client=v.domain.source.receiver,
        .descriptor=v.descriptor,.content=v.domain.content,.content_product=v.descriptor->selection.product,
        .product=product,
        .connection=v.domain.connection,.epoch=v.domain.epoch,.restart_generation=v.domain.restart_generation,
        .publication_generation=publication,.configuration_generation=v.domain.source.configuration_generation,
        .map=v.map,.geometry=v.geometry,.gamestate=v.domain.gamestate,
        .physical_client=(uint32_t)v.domain.initial.client_number,.initial_message=v.domain.initial.server_message,
        .initial_command=v.domain.initial.last_executed_server_command};
    frontend_remote_q3_resources actual;
    if(row->compiled_video?!frontend_remote_q3_resources_compiled_video_read(row,&actual,error):
        !frontend_remote_q3_resources_current(&v))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote basis changed during its pure observation");
    *out=basis; return true;
}
bool frontend_remote_q3_resources_borrow(frontend_remote_q3 *row,frontend_remote_q3_resources *out,qa_error *error)
{
    if(!row || row->users==SIZE_MAX || !frontend_remote_q3_resources_read(row,out,error)) return false;
    ++row->users; return true;
}
void frontend_remote_q3_resources_release(frontend_remote_q3 *row)
{ if(row && row->users) --row->users; }
qa_frontend *frontend_remote_q3_frontend(const frontend_remote_q3 *row)
{ return row && linked(row)?row->frontend:NULL; }
frontend_remote_q3_frame *frontend_remote_q3_frames_read(const frontend_remote_q3 *row)
{ return row && linked(row)?row->frames:NULL; }
bool frontend_remote_q3_runtime_attach(frontend_remote_q3 *row,frontend_remote_q3_runtime *child,qa_error *error)
{
    frontend_remote_q3_resources resources;
    if(!row || !child || !linked(row) || !row->resources_ready || row->constructing || row->retiring || row->users ||
        row->frontend->capture || row->frontend->resource_inventory || row->runtime || row->frames || !row->services ||
        frontend_remote_q3_runtime_parent(child)!=row ||
        !frontend_remote_q3_resources_read(row,&resources,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote runtime requires its actual empty constructor parent");
    row->runtime=child; return true;
}
frontend_remote_q3_runtime *frontend_remote_q3_runtime_read(const frontend_remote_q3 *row)
{ return row && linked(row)?row->runtime:NULL; }
bool frontend_remote_q3_runtime_detach(frontend_remote_q3 *row,frontend_remote_q3_runtime *child,qa_error *error)
{
    if(!row || !child || !linked(row) || row->constructing || row->users || row->frontend->capture ||
        row->frontend->resource_inventory ||
        row->runtime!=child || row->frames || frontend_remote_q3_runtime_parent(child)!=row ||
        !frontend_remote_q3_runtime_retired(child))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote runtime still retains its actual callback or renderer children");
    row->runtime=NULL; return true;
}
bool frontend_remote_q3_modules_attach(frontend_remote_q3 *row,frontend_remote_q3_modules *child,qa_error *error)
{
    frontend_remote_q3_resources resources;
    if(!row || !child || !linked(row) || !row->resources_ready || row->constructing || row->retiring || row->users ||
        row->frontend->capture || row->frontend->resource_inventory || row->modules || frontend_remote_q3_modules_parent(child)!=row ||
        !frontend_remote_q3_resources_read(row,&resources,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Acquired CLIENT child requires its actual empty constructor parent");
    row->modules=child; return true;
}
frontend_remote_q3_modules *frontend_remote_q3_modules_read(const frontend_remote_q3 *row)
{ return row && linked(row)?row->modules:NULL; }
bool frontend_remote_q3_modules_detach(frontend_remote_q3 *row,frontend_remote_q3_modules *child,qa_error *error)
{
    if(!row || !child || !linked(row) || row->constructing || row->users || row->frontend->capture ||
        row->frontend->resource_inventory ||
        row->modules!=child || frontend_remote_q3_modules_parent(child)!=row ||
        !frontend_remote_q3_modules_retired(child))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Acquired CLIENT child still retains actual host or media owners");
    row->modules=NULL; return true;
}
bool frontend_remote_q3_transport_attach(frontend_remote_q3 *row,frontend_remote_q3_transport *child,qa_error *error)
{
    frontend_remote_q3_resources resources;
    if(!row || !child || !linked(row) || !row->resources_ready || row->constructing || row->retiring || row->users ||
        row->frontend->capture || row->frontend->resource_inventory || row->transport || row->services ||
        row->runtime || row->frames || frontend_remote_q3_transport_parent(child)!=row ||
        !frontend_remote_q3_resources_read(row,&resources,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Pure CLIENT transport requires its actual empty decoded parent");
    row->transport=child; return true;
}
frontend_remote_q3_transport *frontend_remote_q3_transport_read(const frontend_remote_q3 *row)
{ return row && linked(row)?row->transport:NULL; }
bool frontend_remote_q3_transport_detach(frontend_remote_q3 *row,frontend_remote_q3_transport *child,qa_error *error)
{
    if(!row || !child || !linked(row) || row->constructing || row->users || row->frontend->capture ||
        row->frontend->resource_inventory || row->transport!=child || row->modules ||
        frontend_remote_q3_transport_parent(child)!=row || !frontend_remote_q3_transport_retired(child))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Pure CLIENT transport retains its actual source or invocation children");
    row->transport=NULL; return true;
}
size_t frontend_remote_q3_count(const qa_frontend *f)
{
    size_t count=0;
    for(const frontend_remote_q3 *row=f?f->remote_q3:NULL;row;row=row->next) ++count;
    return count;
}
frontend_remote_q3 *frontend_remote_q3_at(const qa_frontend *f,size_t ordinal)
{
    frontend_remote_q3 *row=f?f->remote_q3:NULL;
    while(row && ordinal--) row=row->next;
    return row;
}
bool frontend_remote_q3_geometry_read(const qa_frontend *f,const qa_application_q3_client_context *receiver,
    const qa_resource **map,const qa_collision_geometry **geometry,qa_trace_scratch **scratch,bool *present,qa_error *error)
{
    if(!f || !receiver || !map || !geometry || !present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote geometry requires its actual CLIENT recipient");
    *map=NULL; *geometry=NULL; *scratch=NULL; *present=false;
    for(const frontend_remote_q3 *row=f->remote_q3;row;row=row->next) {
        const qa_application_q3_client_context *expected=&row->resources.domain.source.receiver;
        if(expected->receiver!=receiver->receiver || expected->seat!=receiver->seat) continue;
        frontend_remote_q3_resources v;
        if(!frontend_remote_q3_resources_read(row,&v,error)) return false;
        const qa_application_q3_client_context *actual=&v.domain.source.receiver;
        if(receiver->service_owner!=actual->service_owner || receiver->frontend_lifetime!=actual->frontend_lifetime ||
            receiver->session!=actual->session || receiver->console!=actual->console || receiver->cvars!=actual->cvars ||
            receiver->source_client!=actual->source_client || receiver->native_source!=actual->native_source ||
            !frontend_network_q3_client_context_current((qa_frontend *)f,receiver))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote geometry differs from its true physical service lease");
        *map=v.map; *geometry=v.geometry; *scratch=v.trace_scratch; *present=true; return true;
    }
    return true;
}
bool frontend_remote_q3_resources_destroy(frontend_remote_q3 **owned,qa_error *error)
{
    if(!owned || !*owned) return true;
    frontend_remote_q3 *row=*owned;
    if(!linked(row) || row->compiled_video || row->frontend->capture || row->frontend->resource_inventory || !resources_idle(row))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote resources retain an actual constructor or renderer borrower");
    row->retiring=true;
    if(!frontend_remote_q3_modules_destroy(&row->modules,error) ||
        !frontend_remote_q3_transport_destroy(&row->transport,error) ||
        !frontend_remote_q3_frame_destroy(&row->frames,error) ||
        !frontend_remote_q3_runtime_destroy(&row->runtime,error) ||
        !frontend_remote_q3_services_destroy(&row->services,error) ||
        !frontend_client_registry_release(&row->resources.registry,error)) return false;
    frontend_remote_q3_resources *v=&row->resources;
    if (row->shader_movies && row->resources.movies && !row->frontend->source_restoring) {
        frontend_material_movie_source expected={.frontend=row->frontend,.files=row->resources.mounts,.images=row->resources.images,
            .materials=row->resources.materials,.media=row->resources.movies,.context=row,.current=shader_movies_current};
        if (!frontend_renderer_materials_adopt_movies(row->frontend,&expected,&row->shader_movies,&row->resources.movies,error)) return false;
    }
    if (!frontend_material_movies_destroy(&row->shader_movies,error)) return false;
    if (v->assets && !qa_q3_assets_services_retire(v->assets,error)) return false;
    qa_q3_presentation_assets_destroy(v->assets);
    frontend_world_scratch_destroy(&v->world_scratch);
    qa_scene_world_destroy(v->world);
    qa_collision_destroy(v->geometry); qa_resource_release(row->map);
    qa_media_library_destroy(v->movies); qa_font_library_destroy(v->fonts); qa_audio_bank_destroy(v->sounds);
    qa_material_library_destroy(v->materials); qa_scene_resources_destroy(v->images); qa_vfs_destroy(v->mounts);
    qa_launch_instance_lease_release(row->descriptor);
    frontend_remote_q3 **link=&row->frontend->remote_q3;
    while(*link!=row) link=&(*link)->next;
    *link=row->next; free(row); *owned=NULL; return true;
}
bool frontend_remote_q3_destroy(qa_frontend *f,qa_error *error)
{
    if(!frontend_remote_q3_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote row destruction requires returned physical children");
    while(f->remote_q3) {
        frontend_remote_q3 *row=f->remote_q3;
        if(!frontend_remote_q3_resources_destroy(&row,error)) return false;
    }
    return true;
}
static bool captured_assets(const frontend_capture *capture,const qa_q3_presentation_assets *assets)
{
    for (size_t i=0;;++i) {
        const qa_q3_presentation_assets *actual=frontend_capture_assets_at(capture,i);
        if (!actual) return false;
        if (actual==assets) return true;
    }
}
bool frontend_remote_q3_capture_current(const qa_frontend *f,const frontend_capture *capture)
{
    if (!f || !capture || f->capture!=capture || f->resource_inventory || f->stepping || f->source_restoring) return false;
    for (const frontend_remote_q3 *row=f->remote_q3;row;row=row->next) {
        frontend_remote_q3_resources resources;
        if (row->frontend!=f || row->constructing || row->retiring || row->importing || row->users ||
            !frontend_remote_q3_resources_read(row,&resources,NULL) ||
            !captured_assets(capture,resources.assets) ||
            !frontend_remote_q3_frame_idle(row->frames) || !frontend_remote_q3_services_idle(row->services) ||
            !frontend_remote_q3_transport_idle(row->transport) ||
            (row->runtime && !frontend_remote_q3_runtime_capture_current(row->runtime)) ||
            !frontend_remote_q3_modules_capture_returned(row->modules,NULL)) return false;
    }
    return true;
}
bool frontend_remote_q3_content_visit(const qa_frontend *f,const qa_application_content_visitor *visitor,qa_error *error)
{
    if(!f || !visitor || !visitor->view || !visitor->pool ||
        !(f->capture?frontend_remote_q3_capture_current(f,f->capture):frontend_remote_q3_idle(f)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote content capture requires returned physical owners");
    for(const frontend_remote_q3 *row=f->remote_q3;row;row=row->next) {
        frontend_remote_q3_resources v;
        if(!frontend_remote_q3_resources_read(row,&v,error) || !visitor->view(visitor->context,v.domain.content,error) ||
            !visitor->view(visitor->context,v.mounts,error) ||
            !visitor->pool(visitor->context,qa_vfs_resources(v.mounts),error)) return false;
    }
    return true;
}
