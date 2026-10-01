#include "remote_q3_client.h"
#include "remote_config.h"
#include "qa/application_character_selection.h"
#include "qa/scene_world_save.h"
#include "qa/q3_assets_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/font_save.h"

struct frontend_remote_q3 {
    frontend_remote_q3 *next;
    qa_frontend *frontend;
    qa_application *application;
    qa_launch_instance_lease *descriptor;
    frontend_remote_q3_resources resources;
    qa_resource *map;
    size_t users;
    bool resources_ready, constructing;
};
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
    return !row->constructing && !row->users && (!v->assets || qa_q3_assets_idle(v->assets)) &&
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
static bool build_resources(frontend_remote_q3 *,frontend_remote_config *,qa_error *);
bool frontend_remote_q3_resources_create(qa_frontend *f,const frontend_network_client_domain *domain,
    frontend_remote_q3 **out,qa_error *error)
{
    if(!f || !f->application || !domain || !out || *out || f->capture || f->source_restoring ||
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
    frontend_remote_config *configuration=frontend_config_store_client(f->config_store,domain->source.receiver.console);
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
    v->images=v->mounts?qa_scene_resources_create(v->mounts,error):NULL;
    v->materials=v->images?qa_material_library_create(v->images,f->order,error):NULL;
    v->fonts=v->images?qa_font_library_create(v->mounts,v->images,error):NULL;
    v->movies=v->images?qa_media_library_create(v->images,error):NULL;
    qa_scene_image_options images={.family=QA_SCENE_Q3,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=-1};
    qa_bsp_view bsp;
    if(!v->mounts || !v->images || !v->materials || !v->fonts || !v->movies ||
        !qa_material_library_load_scripts(v->materials,v->mounts,&images,error) ||
        !frontend_material_remaps(f,v->materials,error) || !qa_audio_bank_create(v->mounts,&v->sounds,error) ||
        !qa_bsp_open(qa_resource_bytes(row->map),&bsp,error)) return false;
    if(bsp.family!=QA_BSP_Q3)
        return frontend_fail(error,QA_ERROR_FORMAT,"Native remote CLIENT requires its actual Q3 BSP map");
    qa_scene_world_options options={.images=images,.subdivisions=64,.q1_water_alpha=1,
        .q2_light_modulate=1,.q3_overbright=1};
    qa_q3_presentation_asset_options assets={.provider={v->mounts,v->images,v->materials,QA_SCENE_Q3},
        .sounds=v->sounds,.movies=v->movies};
    if(!qa_collision_create(&bsp,&v->geometry,error) ||
        !qa_scene_world_create(&bsp,v->images,v->materials,&options,&v->world,error) ||
        !qa_q3_presentation_assets_create(&assets,&v->assets,error)) return false;
    if(!frontend_network_client_domain_current(f,domain))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote constructor callbacks changed their actual CLIENT domain");
    return true;
}
bool frontend_remote_q3_resources_read(const frontend_remote_q3 *row,frontend_remote_q3_resources *out,qa_error *error)
{
    frontend_network_client_domain domain;
    if(!row || !out || !linked(row) || !row->resources_ready || row->application!=row->frontend->application ||
        !frontend_network_client_domain_read(row->frontend,&domain,error)) return false;
    if(!same_domain(&row->resources.domain,&domain) || !row->resources.descriptor ||
        row->resources.descriptor->storage!=domain.source.descriptor->storage ||
        row->resources.physical_seat>=row->frontend->options.seats ||
        row->resources.input!=row->frontend->seats[row->resources.physical_seat].input ||
        frontend_client_registry_cvars(row->resources.registry)!=domain.source.receiver.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote resource receipt differs from its retained physical CLIENT");
    *out=row->resources; out->domain=domain; return true;
}
bool frontend_remote_q3_resources_current(const frontend_remote_q3_resources *v)
{
    const frontend_remote_q3 *row=v?v->owner:NULL;
    const frontend_remote_q3_resources *actual=row?&row->resources:NULL;
    return row && linked(row) && row->resources_ready && row->application==row->frontend->application &&
        frontend_network_client_domain_current(row->frontend,&v->domain) && same_domain(&actual->domain,&v->domain) &&
        v->identity==actual->identity && v->physical_seat==actual->physical_seat &&
        v->descriptor==actual->descriptor && v->mounts==actual->mounts && v->images==actual->images &&
        v->materials==actual->materials && v->fonts==actual->fonts && v->sounds==actual->sounds &&
        v->movies==actual->movies && v->assets==actual->assets && v->world==actual->world &&
        v->geometry==actual->geometry && v->map==row->map && v->registry==actual->registry &&
        v->input==actual->input && v->physical_seat<row->frontend->options.seats &&
        v->input==row->frontend->seats[v->physical_seat].input;
}
bool frontend_remote_q3_basis_read(const frontend_remote_q3 *row,qa_native_q3_remote_client_basis *out,qa_error *error)
{
    frontend_remote_q3_resources v;
    if(!out || !frontend_remote_q3_resources_read(row,&v,error)) return false;
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
    if(!frontend_remote_q3_resources_current(&v))
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
    const qa_resource **map,const qa_collision_geometry **geometry,bool *present,qa_error *error)
{
    if(!f || !receiver || !map || !geometry || !present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote geometry requires its actual CLIENT recipient");
    *map=NULL; *geometry=NULL; *present=false;
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
        *map=v.map; *geometry=v.geometry; *present=true; return true;
    }
    return true;
}
bool frontend_remote_q3_resources_destroy(frontend_remote_q3 **owned,qa_error *error)
{
    if(!owned || !*owned) return true;
    frontend_remote_q3 *row=*owned;
    if(!linked(row) || row->frontend->capture || !resources_idle(row))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote resources retain an actual constructor or renderer borrower");
    if(!frontend_client_registry_release(&row->resources.registry,error)) return false;
    frontend_remote_q3_resources *v=&row->resources;
    qa_q3_presentation_assets_destroy(v->assets); qa_scene_world_destroy(v->world);
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
bool frontend_remote_q3_content_visit(const qa_frontend *f,const qa_application_content_visitor *visitor,qa_error *error)
{
    if(!f || !visitor || !visitor->view || !visitor->pool || !frontend_remote_q3_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native remote content capture requires returned physical owners");
    for(const frontend_remote_q3 *row=f->remote_q3;row;row=row->next) {
        frontend_remote_q3_resources v;
        if(!frontend_remote_q3_resources_read(row,&v,error) || !visitor->view(visitor->context,v.domain.content,error) ||
            !visitor->view(visitor->context,v.mounts,error) ||
            !visitor->pool(visitor->context,qa_vfs_resources(v.mounts),error)) return false;
    }
    return true;
}
