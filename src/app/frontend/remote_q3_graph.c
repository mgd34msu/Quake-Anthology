#include "remote_q3_graph_private.h"
#include <stdlib.h>
#include <string.h>

static bool blob(qa_source_save_io *io,qa_bytes *bytes)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=reading?0:bytes->size;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(!reading) return !count || (bytes->data && qa_source_save_bytes(io,(void *)bytes->data,count));
    if(io->offset>io->input.size || count>io->input.size-io->offset)
        return frontend_fail(io->error,QA_ERROR_FORMAT,"Remote resource recipe exceeds its complete envelope");
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool fields(qa_source_save_io *io,frontend_remote_q3_graph_recipe *recipe)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    frontend_remote_q3_graph_resources *v=&recipe->resources;
    uint8_t magic[4]={'Q','R','G','T'}; uint32_t version=1;
    uint64_t pool=0,resource=0;
    if(!reading && v->map && !qa_application_content_resource_id(recipe->graph,v->map,&pool,&resource)) return false;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QRGT",4) ||
        !qa_source_save_u32(io,&version) || version!=1 || !qa_source_save_bool(io,&v->decoded) ||
        !qa_source_save_u64(io,&v->identity) || v->identity<=QA_FRONTEND_COMMAND_OWNER ||
        !qa_source_save_u32(io,&v->physical_seat) || v->physical_seat>=recipe->frontend->options.seats ||
        !qa_source_save_string(io,&recipe->receiver) || !recipe->receiver ||
        !qa_source_save_u32(io,&recipe->logical_seat) ||
        !qa_source_save_string(io,&recipe->service_owner) || !recipe->service_owner ||
        !qa_source_save_u64(io,&recipe->configuration_generation) || !recipe->configuration_generation ||
        !qa_source_save_u64(io,&recipe->epoch) || !recipe->epoch ||
        !qa_source_save_u64(io,&recipe->restart_generation) ||
        !qa_source_save_u64(io,&recipe->catalog) || !recipe->catalog ||
        !qa_source_save_u64(io,&recipe->descriptor_view) || !recipe->descriptor_view ||
        !qa_source_save_u64(io,&recipe->provider_view) || !recipe->provider_view ||
        !qa_source_save_u64(io,&recipe->mounts) || !recipe->mounts ||
        !qa_source_save_u64(io,&recipe->roles) || !recipe->roles || (recipe->roles>>QA_ROLE_COUNT) ||
        !qa_source_save_u32(io,&recipe->product) || !recipe->product ||
        !frontend_save_text(io,&recipe->instance) || !recipe->instance || !*recipe->instance ||
        !qa_source_save_bytes(io,recipe->descriptor_identity.bytes,32) ||
        !qa_source_save_u64(io,&pool) || !qa_source_save_u64(io,&resource) || (!!pool!=!!resource) ||
        !blob(io,&v->portals)) return false;
    qa_catalog *catalog=qa_application_content_catalog(recipe->graph,recipe->catalog);
    qa_vfs *descriptor=qa_application_content_view(recipe->graph,recipe->descriptor_view);
    qa_vfs *provider=qa_application_content_view(recipe->graph,recipe->provider_view);
    qa_vfs *mounts=qa_application_content_view(recipe->graph,recipe->mounts);
    const qa_product *product=catalog?qa_catalog_product(catalog,recipe->product):NULL;
    const qa_resource *map=resource?qa_application_content_resource(recipe->graph,pool,resource):NULL;
    if(!product || product->family!=QA_GAME_Q3 || !descriptor || !provider || !mounts ||
        mounts==provider || mounts==descriptor || qa_vfs_resources(mounts)!=qa_vfs_resources(provider) ||
        (v->decoded?(!map || !v->portals.size):resource || v->portals.size) ||
        (map && qa_resource_pool_find(qa_vfs_resources(mounts),qa_resource_id(map))!=map)) return false;
    if(reading) { v->mounts=mounts; v->map=map; }
    return true;
}
static bool capture(qa_frontend *f,const qa_application_q3_remote_source *source,
    uint64_t restart,uint64_t identity,uint32_t physical,qa_vfs *provider,qa_vfs *mounts,
    const qa_resource *map,const qa_collision_geometry *geometry,qa_buffer *out,qa_error *error)
{
    if(!f || !f->application || !f->capture || f->source_restoring || !source || !source->descriptor ||
        !out || out->data || out->size || !provider || !mounts || (!!map!=!!geometry) ||
        source->receiver.service_owner>UINT32_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote topology requires its actual captured resource parent");
    const qa_launch_instance *descriptor=source->descriptor;
    frontend_remote_q3_graph_recipe recipe={.frontend=f,.application=f->application,
        .graph=qa_application_content_graph_read(f->application),.roles=descriptor->roles,
        .configuration_generation=source->configuration_generation,.epoch=source->connection_epoch,
        .restart_generation=restart,.service_owner=(qa_string_id)source->receiver.service_owner,
        .receiver=source->receiver.receiver,.logical_seat=source->receiver.seat,
        .product=descriptor->selection.product,.instance=(char *)descriptor->selection.instance,
        .descriptor_identity=descriptor->identity,
        .resources={.decoded=map!=NULL,.identity=identity,.physical_seat=physical,.mounts=mounts,.map=map}};
    if(!recipe.graph || descriptor->selection.runtime!=QA_PROGRAM_BUILTIN || descriptor->artifact)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote topology lacks its actual builtin CLIENT descriptor");
    recipe.catalog=qa_application_content_catalog_id(recipe.graph,qa_launch_instance_catalog(descriptor));
    recipe.descriptor_view=qa_application_content_view_id(recipe.graph,descriptor->content);
    recipe.provider_view=qa_application_content_view_id(recipe.graph,provider);
    recipe.mounts=qa_application_content_view_id(recipe.graph,mounts);
    qa_source_save_io io={0};
    bool okay=(!geometry || frontend_remote_q3_graph_geometry_checkpoint(geometry,&recipe.portals,error));
    recipe.resources.portals=(qa_bytes){recipe.portals.data,recipe.portals.size};
    okay=okay && qa_source_save_writer(&io,qa_application_session(f->application),error) &&
        fields(&io,&recipe) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&recipe.portals);
    if(!okay && (!error || error->code==QA_OK))
        frontend_fail(error,QA_ERROR_FORMAT,"Remote topology lost an actual descriptor, private view or map holder");
    return okay;
}
bool frontend_remote_q3_graph_resources_checkpoint(const frontend_remote_q3_resources *v,
    qa_buffer *out,qa_error *error)
{
    if(!v || !frontend_remote_q3_resources_current(v))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote topology requires its retained decoded constructor tuple");
    return capture(frontend_remote_q3_frontend(v->owner),&v->domain.source,v->domain.restart_generation,
        v->identity,v->physical_seat,v->domain.content,v->mounts,v->map,v->geometry,out,error);
}
bool frontend_remote_q3_graph_initial_checkpoint(const frontend_remote_q3_initial_view *v,
    qa_buffer *out,qa_error *error)
{
    if(!v || !frontend_remote_q3_initial_current(v))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial topology requires its retained absent-map constructor tuple");
    return capture(frontend_remote_q3_initial_frontend(v->owner),&v->attempt.source,v->attempt.restart_generation,
        v->identity,v->physical_seat,v->descriptor->content,v->mounts,NULL,NULL,out,error);
}
void frontend_remote_q3_graph_recipe_destroy(frontend_remote_q3_graph_recipe *recipe)
{
    if(!recipe) return;
    if(recipe->reading) free(recipe->instance);
    qa_buffer_free(&recipe->portals); free(recipe);
}
bool frontend_remote_q3_graph_recipe_decode(qa_frontend *f,qa_bytes bytes,
    frontend_remote_q3_graph_recipe **out,qa_error *error)
{
    if(!f || !f->application || !f->source_restoring || f->capture || !out || *out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote topology decode requires its actual isolated application graph");
    frontend_remote_q3_graph_recipe *recipe=calloc(1,sizeof(*recipe));
    if(!recipe) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining remote resource topology");
    recipe->frontend=f; recipe->application=f->application;
    recipe->graph=qa_application_content_graph_read(f->application); recipe->reading=true;
    qa_source_save_io io={0};
    bool okay=recipe->graph && qa_source_save_reader(&io,qa_application_session(f->application),bytes,error) &&
        fields(&io,recipe) && qa_source_save_finish(&io,NULL) &&
        (!recipe->resources.decoded || frontend_remote_q3_graph_geometry_validate(recipe->resources.portals,error));
    qa_source_save_dispose(&io);
    if(!okay) {
        frontend_remote_q3_graph_recipe_destroy(recipe);
        if(!error || error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid complete remote resource topology");
        return false;
    }
    *out=recipe; return true;
}
static bool recipe_current(const frontend_remote_q3_graph_recipe *recipe)
{
    return recipe && recipe->reading && !recipe->attempted && recipe->frontend->application==recipe->application &&
        recipe->frontend->source_restoring && !recipe->frontend->capture &&
        recipe->graph==qa_application_content_graph_read(recipe->application) &&
        recipe->resources.mounts==qa_application_content_view(recipe->graph,recipe->mounts);
}
bool frontend_remote_q3_graph_recipe_read(const frontend_remote_q3_graph_recipe *recipe,
    frontend_remote_q3_graph_resources *out,qa_error *error)
{
    if(!out || !recipe_current(recipe))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote recipe no longer borrows its actual isolated graph");
    *out=recipe->resources; return true;
}
static bool source_matches(const frontend_remote_q3_graph_recipe *recipe,
    const qa_application_q3_remote_source *source,uint64_t restart,qa_vfs *provider)
{
    const qa_launch_instance *descriptor=source?source->descriptor:NULL;
    return recipe_current(recipe) && descriptor && descriptor->storage &&
        descriptor->selection.runtime==QA_PROGRAM_BUILTIN && !descriptor->artifact &&
        descriptor->selection.product==recipe->product && descriptor->selection.instance &&
        !strcmp(descriptor->selection.instance,recipe->instance) && descriptor->roles==recipe->roles &&
        qa_sha256_equal(&descriptor->identity,&recipe->descriptor_identity) &&
        qa_launch_instance_catalog(descriptor)==qa_application_content_catalog(recipe->graph,recipe->catalog) &&
        descriptor->content==qa_application_content_view(recipe->graph,recipe->descriptor_view) &&
        provider==qa_application_content_view(recipe->graph,recipe->provider_view) &&
        source->receiver.receiver==recipe->receiver && source->receiver.seat==recipe->logical_seat &&
        source->receiver.service_owner==recipe->service_owner &&
        source->configuration_generation==recipe->configuration_generation &&
        source->connection_epoch==recipe->epoch && restart==recipe->restart_generation;
}
bool frontend_remote_q3_graph_initial_ready(const frontend_remote_q3_graph_recipe *recipe,
    const frontend_network_client_attempt *attempt,qa_error *error)
{
    uint32_t physical;
    if(!recipe || recipe->resources.decoded || !attempt ||
        !frontend_network_client_restore_attempt_current(recipe->frontend,attempt) ||
        !source_matches(recipe,&attempt->source,attempt->restart_generation,attempt->source.descriptor->content) ||
        !qa_application_constructor_seat_ordinal(recipe->application,attempt->source.receiver.receiver,
            attempt->source.receiver.seat,&physical,error) || physical!=recipe->resources.physical_seat)
        return frontend_fail(error,QA_ERROR_FORMAT,"Initial topology differs from its actual absent-gamestate CLIENT attempt");
    return true;
}
bool frontend_remote_q3_graph_prepare_resources(frontend_remote_q3_graph_recipe *recipe,
    const frontend_network_client_domain *domain,frontend_remote_q3 **out,qa_error *error)
{
    uint32_t physical;
    if(!recipe || !recipe->resources.decoded || recipe->attempted || !domain || !out || *out ||
        !frontend_network_client_restore_domain_current(recipe->frontend,domain) ||
        !source_matches(recipe,&domain->source,domain->restart_generation,domain->content) ||
        domain->map!=recipe->resources.map ||
        !qa_application_constructor_seat_ordinal(recipe->application,domain->source.receiver.receiver,
            domain->source.receiver.seat,&physical,error) || physical!=recipe->resources.physical_seat)
        return frontend_fail(error,QA_ERROR_FORMAT,"Decoded topology differs from its actual staged CLIENT map and descriptor");
    recipe->attempted=true;
    qa_vfs *mounts=NULL;
    if(!qa_application_content_claim_view(recipe->graph,recipe->mounts,&mounts,error)) return false;
    bool okay=frontend_remote_q3_resources_prepare_restored(recipe->frontend,domain,recipe->resources.identity,
        recipe->resources.physical_seat,&mounts,recipe->resources.portals,out,error);
    qa_vfs_destroy(mounts); return okay;
}
bool frontend_remote_q3_graph_prepare_initial(frontend_remote_q3_graph_recipe *recipe,
    const frontend_network_client_attempt *attempt,frontend_remote_q3_initial **out,qa_error *error)
{
    if(!recipe || recipe->attempted || !out || *out ||
        !frontend_remote_q3_graph_initial_ready(recipe,attempt,error)) return false;
    recipe->attempted=true;
    qa_vfs *mounts=NULL;
    if(!qa_application_content_claim_view(recipe->graph,recipe->mounts,&mounts,error)) return false;
    bool okay=frontend_remote_q3_initial_prepare_restored(recipe->frontend,attempt,recipe->resources.identity,
        recipe->resources.physical_seat,&mounts,out,error);
    qa_vfs_destroy(mounts); return okay;
}
