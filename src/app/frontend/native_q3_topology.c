#include "native_q3_topology.h"
#include "save_private.h"
#include "qa/launch_native_save.h"

typedef struct native_plan {
    frontend_native_q3_topology *topology;
    frontend_native_q3 *row;
    frontend_native_q3_import state;
    qa_launch_restored_instance recipe;
    qa_launch_instance_lease *metadata;
    qa_vfs_acquisition acquisition;
    uint64_t roles, mounts;
    const qa_resource *resources[64];
    size_t resource_count;
    qa_buffer children;
} native_plan;
struct frontend_native_q3_topology {
    qa_frontend *frontend;
    qa_application_content_graph *graph;
    native_plan *rows;
    size_t count;
    bool decoded,prepared;
};
static bool resource_encode(void *context,const qa_resource *resource,uint64_t *out,qa_error *error)
{
    native_plan *row=context;
    for(size_t i=0;out && i<row->resource_count;++i)
        if(row->resources[i]==resource) { *out=i+1; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"Native animation resource leaves its actual holder dictionary");
}
static bool resource_decode(void *context,uint64_t ordinal,const qa_resource **out,qa_error *error)
{
    native_plan *row=context;
    if(!out || !ordinal || ordinal>row->resource_count)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved native animation resource ordinal is absent");
    *out=row->resources[(size_t)ordinal-1]; return true;
}
static q3n_client_refs refs(native_plan *row)
{ return (q3n_client_refs){row,resource_encode,resource_decode}; }
static bool resource_fields(qa_source_save_io *io,qa_application_content_graph *graph,
    const qa_resource **resource)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint64_t pool=0,version=0;
    if(!reading && *resource && !qa_application_content_resource_id(graph,*resource,&pool,&version)) return false;
    if(!qa_source_save_u64(io,&pool) || !qa_source_save_u64(io,&version) || (!!pool!=!!version)) return false;
    if(reading) *resource=version?qa_application_content_resource(graph,pool,version):NULL;
    return !version || *resource!=NULL;
}
static bool clock_fields(qa_source_save_io *io,qa_clock_config *clock)
{
    uint32_t kind=clock->kind;
    if(!qa_source_save_u32(io,&kind) || kind>QA_CLOCK_Q3 ||
        !qa_source_save_u64(io,&clock->initial_time_ns) || !qa_source_save_u64(io,&clock->interval_ns) ||
        !qa_source_save_u64(io,&clock->minimum_frame_ns) || !qa_source_save_u64(io,&clock->maximum_frame_ns) ||
        !qa_source_save_u64(io,&clock->initial_lead_ns) || !qa_source_save_u32(io,&clock->maximum_steps)) return false;
    clock->kind=(qa_clock_kind)kind; return true;
}
static bool bytes_fields(qa_source_save_io *io,qa_bytes *bytes,bool owned)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=reading?0:bytes->size;
    if(!qa_source_save_count(io,&count,reading?io->input.size-io->offset:SIZE_MAX)) return false;
    if(!reading) return !count || (bytes->data && qa_source_save_bytes(io,(void *)bytes->data,count));
    if(!owned) { *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true; }
    uint8_t *copy=count?malloc(count):NULL;
    if(count && !copy) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining native source options");
    *bytes=(qa_bytes){copy,count};
    return !count || qa_source_save_bytes(io,copy,count);
}
static bool acquisition_fields(qa_source_save_io *io,qa_vfs_acquisition *a)
{
    return qa_source_save_u64(io,&a->mount) && a->mount &&
        qa_source_save_u64(io,&a->resource_id) && a->resource_id &&
        frontend_save_text(io,&a->path) && a->path && *a->path &&
        frontend_save_text(io,&a->lookup_path) && a->lookup_path && *a->lookup_path &&
        frontend_save_text(io,&a->link_source) && frontend_save_text(io,&a->link_target) &&
        ((a->link_source!=NULL)==(a->link_target!=NULL));
}
static bool recipe_fields(qa_source_save_io *io,native_plan *row)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_application_content_graph *graph=row->topology->graph;
    qa_launch_restored_instance *recipe=&row->recipe;
    qa_launch_provider *p=&recipe->selection;
    uint64_t catalog=reading?0:qa_application_content_catalog_id(graph,recipe->catalog);
    uint64_t view=reading?0:qa_application_content_view_id(graph,recipe->content);
    uint32_t runtime=p->runtime;
    if(!qa_source_save_u64(io,&catalog) || !catalog || !qa_source_save_u64(io,&view) || !view ||
        !qa_source_save_u32(io,&p->product) || !p->product ||
        !frontend_save_text(io,(char **)&p->instance) || !p->instance || !*p->instance ||
        !qa_source_save_u32(io,&runtime) || runtime!=QA_PROGRAM_BUILTIN ||
        !frontend_save_text(io,(char **)&p->implementation) ||
        !frontend_save_text(io,(char **)&p->artifact) || !frontend_save_text(io,(char **)&p->component) ||
        !clock_fields(io,&p->clock) || !bytes_fields(io,&p->options,true) ||
        !qa_source_save_u64(io,&row->roles) || !row->roles || (row->roles>>QA_ROLE_COUNT) ||
        !(row->roles&QA_ROLE_BIT(QA_ROLE_ENTITIES)) ||
        !qa_source_save_bytes(io,recipe->identity.bytes,32) ||
        !resource_fields(io,graph,&recipe->artifact) || !resource_fields(io,graph,&recipe->declaration)) return false;
    p->runtime=(qa_program_kind)runtime;
    if(reading) {
        recipe->catalog=qa_application_content_catalog(graph,catalog);
        recipe->content=qa_application_content_view(graph,view);
    }
    const qa_product *product=recipe->catalog?qa_catalog_product(recipe->catalog,p->product):NULL;
    if(!product || product->family!=QA_GAME_Q3 || !recipe->content) return false;
    qa_resource_pool *pool=qa_vfs_resources(recipe->content);
    if(!pool || (recipe->artifact && qa_resource_pool_find(pool,qa_resource_id(recipe->artifact))!=recipe->artifact) ||
        (recipe->declaration && qa_resource_pool_find(pool,qa_resource_id(recipe->declaration))!=recipe->declaration)) return false;
    if(recipe->artifact) {
        if(!acquisition_fields(io,&row->acquisition) ||
            row->acquisition.resource_id!=qa_resource_id(recipe->artifact) ||
            !qa_vfs_acquisition_retained(recipe->content,&row->acquisition,io->error)) return false;
        recipe->artifact_acquisition=&row->acquisition;
    } else recipe->artifact_acquisition=NULL;
    if(!qa_source_save_count(io,&recipe->interface_count,
        reading?(io->input.size-io->offset)/26:SIZE_MAX/sizeof(qa_launch_resource))) return false;
    if(reading && recipe->interface_count) {
        recipe->interfaces=calloc(recipe->interface_count,sizeof(*recipe->interfaces));
        if(!recipe->interfaces) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining native source interfaces");
    }
    for(size_t i=0;i<recipe->interface_count;++i) {
        qa_launch_resource *r=(qa_launch_resource *)recipe->interfaces+i;
        if(!qa_source_save_u32(io,&r->product) || !qa_catalog_product(recipe->catalog,r->product) ||
            !frontend_save_text(io,(char **)&r->path) || !r->path || !*r->path ||
            !resource_fields(io,graph,&r->resource) || !r->resource ||
            qa_resource_pool_find(pool,qa_resource_id(r->resource))!=r->resource) return false;
    }
    if(!qa_source_save_count(io,&recipe->behavior_count,
        reading?(io->input.size-io->offset)/9:SIZE_MAX/sizeof(*recipe->behaviors))) return false;
    if(reading && recipe->behavior_count) {
        recipe->behaviors=calloc(recipe->behavior_count,sizeof(*recipe->behaviors));
        if(!recipe->behaviors) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining native source behaviors");
    }
    for(size_t i=0;i<recipe->behavior_count;++i) {
        char *id=reading?NULL:(char *)(recipe->behaviors[i]?recipe->behaviors[i]->id:NULL);
        bool ok=frontend_save_text(io,&id) && id && *id;
        const qa_catalog_weapon_behavior *behavior=ok?qa_catalog_weapon_behavior_find(recipe->catalog,p->product,id):NULL;
        if(reading) { free(id); ((const qa_catalog_weapon_behavior **)recipe->behaviors)[i]=behavior; }
        if(!ok || !behavior || behavior!=recipe->behaviors[i]) return false;
    }
    return true;
}
static bool row_fields(qa_source_save_io *io,native_plan *row)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    frontend_native_q3_view *v=&row->state.owners;
    uint32_t product=v->product;
    if(!qa_source_save_u64(io,&v->identity) || v->identity<=QA_FRONTEND_COMMAND_OWNER ||
        !qa_source_save_string(io,&v->service_owner) || !v->service_owner ||
        !qa_source_save_u32(io,&v->seat) || v->seat>=row->topology->frontend->options.seats ||
        !qa_source_save_u32(io,&v->launch_seat) || !qa_source_save_u32(io,&v->physical_client) ||
        v->physical_client>=64 || !qa_source_save_u32(io,&product) ||
        (product!=QA_Q3_ARENA && product!=QA_Q3_TEAM_ARENA) ||
        !qa_source_save_bool(io,&v->music_attached) ||
        !qa_source_save_actor(io,&v->actor) || !v->actor.registry ||
        !qa_source_save_u64(io,&row->mounts) || !row->mounts || !recipe_fields(io,row)) return false;
    v->product=(qa_q3_product)product;
    v->source_files=row->recipe.content;
    qa_vfs *mounts=qa_application_content_view(row->topology->graph,row->mounts);
    if(!mounts || mounts==v->source_files || qa_vfs_resources(mounts)!=qa_vfs_resources(v->source_files)) return false;
    if(reading) v->mounts=mounts;
    if(!qa_source_save_count(io,&row->resource_count,64)) return false;
    for(size_t i=0;i<row->resource_count;++i) {
        if(!resource_fields(io,row->topology->graph,row->resources+i) || !row->resources[i] ||
            qa_resource_pool_find(qa_vfs_resources(v->source_files),qa_resource_id(row->resources[i]))!=row->resources[i]) return false;
        for(size_t j=0;j<i;++j) if(row->resources[j]==row->resources[i]) return false;
    }
    qa_bytes children=reading?(qa_bytes){0}:(qa_bytes){row->children.data,row->children.size};
    return bytes_fields(io,&children,false) && children.size &&
        (!reading || frontend_native_q3_split(children,&row->state,io->error));
}
static bool fields(qa_source_save_io *io,frontend_native_q3_topology *topology)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','F','N','T'}; uint32_t version=1;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFNT",4) ||
        !qa_source_save_u32(io,&version) || version!=1 ||
        !qa_source_save_count(io,&topology->count,reading?(io->input.size-io->offset)/128:
            SIZE_MAX/sizeof(*topology->rows))) return false;
    if(reading && topology->count) {
        topology->rows=calloc(topology->count,sizeof(*topology->rows));
        if(!topology->rows) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining native physical topology");
    }
    for(size_t i=0;i<topology->count;++i) {
        native_plan *row=topology->rows+i; row->topology=topology;
        if(!row_fields(io,row)) return false;
        for(size_t j=0;j<i;++j) {
            native_plan *prior=topology->rows+j;
            if(row->state.owners.identity==prior->state.owners.identity || row->mounts==prior->mounts ||
                (row->state.owners.launch_seat==prior->state.owners.launch_seat &&
                 !strcmp(row->recipe.selection.instance,prior->recipe.selection.instance))) return false;
        }
    }
    return true;
}
void frontend_native_q3_topology_destroy(frontend_native_q3_topology *topology)
{
    if(!topology) return;
    for(size_t i=0;topology->rows && i<topology->count;++i) {
        native_plan *row=topology->rows+i; qa_launch_restored_instance *r=&row->recipe;
        if(topology->decoded) {
            free((char *)r->selection.instance); free((char *)r->selection.implementation);
            free((char *)r->selection.artifact); free((char *)r->selection.component); free((void *)r->selection.options.data);
            for(size_t j=0;r->interfaces && j<r->interface_count;++j) free((char *)r->interfaces[j].path);
            free((void *)r->interfaces); free((void *)r->behaviors); qa_vfs_acquisition_dispose(&row->acquisition);
        }
        qa_launch_instance_lease_release(row->metadata); qa_buffer_free(&row->children);
    }
    free(topology->rows); free(topology);
}
bool frontend_native_q3_topology_checkpoint(qa_frontend *f,qa_buffer *out,qa_error *error)
{
    if(!f || !f->application || !f->capture || f->source_restoring || f->stepping || f->preparing || f->round ||
        !frontend_seat_callbacks_idle(f) || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native topology needs its real capture leases");
    frontend_native_q3_topology *topology=calloc(1,sizeof(*topology));
    if(!topology) return frontend_fail(error,QA_ERROR_MEMORY,"Collecting native source topology");
    topology->frontend=f; topology->graph=qa_application_content_graph_read(f->application);
    topology->count=frontend_native_q3_count(f);
    topology->rows=topology->count?calloc(topology->count,sizeof(*topology->rows)):NULL;
    bool ok=topology->graph && (!topology->count || topology->rows);
    for(size_t i=0;ok && i<topology->count;++i) {
        native_plan *row=topology->rows+i; row->topology=topology;
        ok=frontend_native_q3_read(f,i,&row->state.owners,error);
        frontend_native_q3_view *v=&row->state.owners;
        const qa_launch_instance *s=v->source_launch;
        if(!ok || !s || v->receiver!=v->source_owner || v->source_files!=s->content) { ok=false; break; }
        row->recipe=(qa_launch_restored_instance){.catalog=qa_launch_instance_catalog(s),.selection=s->selection,
            .content=s->content,.artifact=s->artifact,.declaration=s->declaration,
            .artifact_acquisition=s->artifact_acquisition,.interfaces=s->interfaces,.interface_count=s->interface_count,
            .behaviors=s->behaviors,.behavior_count=s->behavior_count,.identity=s->identity};
        row->roles=s->roles; row->mounts=qa_application_content_view_id(topology->graph,v->mounts);
        if(s->artifact_acquisition) row->acquisition=*s->artifact_acquisition;
        for(uint32_t client=0;ok && client<64;++client) {
            const qa_resource *resource=NULL; const qa_vfs_acquisition *receipt=NULL;
            ok=frontend_native_q3_animation_holder(f,i,client,&resource,&receipt,error);
            if(!ok || !resource) continue;
            if(!receipt || !qa_vfs_acquisition_retained(s->content,receipt,error)) { ok=false; break; }
            size_t j=0; while(j<row->resource_count && row->resources[j]!=resource) ++j;
            if(j==row->resource_count) row->resources[row->resource_count++]=resource;
        }
        q3n_client_refs resource_refs=refs(row);
        /* The linked producer owns the actual physical row; no private owner
         * is synthesized from its inventory view. */
        frontend_native_q3 *actual=frontend_native_q3_at(f,i);
        ok=ok && actual && frontend_native_q3_checkpoint(actual,&resource_refs,&row->children,error);
    }
    qa_source_save_io io={0};
    ok=ok && qa_source_save_writer(&io,qa_application_session(f->application),error) &&
        fields(&io,topology) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); frontend_native_q3_topology_destroy(topology);
    if(!ok && error && error->code==QA_OK)
        frontend_fail(error,QA_ERROR_FORMAT,"Native topology lost a real source descriptor or child owner");
    return ok;
}
bool frontend_native_q3_topology_decode(qa_frontend *f,qa_bytes bytes,
    frontend_native_q3_topology **out,qa_error *error)
{
    if(!f || !f->application || !f->source_restoring || f->native_q3 || !out || *out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native topology decode needs its isolated frontend prefix");
    frontend_native_q3_topology *topology=calloc(1,sizeof(*topology));
    if(!topology) return frontend_fail(error,QA_ERROR_MEMORY,"Decoding native source topology");
    topology->frontend=f; topology->graph=qa_application_content_graph_read(f->application); topology->decoded=true;
    qa_source_save_io io={0};
    bool ok=topology->graph && qa_source_save_reader(&io,qa_application_session(f->application),bytes,error) &&
        fields(&io,topology) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!ok) {
        frontend_native_q3_topology_destroy(topology);
        if(error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid native source topology");
        return false;
    }
    *out=topology; return true;
}
bool frontend_native_q3_topology_prepare(frontend_native_q3_topology *topology,
    const qa_launch_snapshot *snapshot,qa_error *error)
{
    if(!topology || !topology->decoded || topology->prepared || !snapshot || topology->frontend->native_q3)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native topology requires its actual provider routing once");
    qa_frontend *f=topology->frontend;
    /* The actual row factory links at the head. Reverse construction preserves
     * the serialized physical list and every dependent dictionary ordinal. */
    for(size_t i=topology->count;i>0;--i) {
        native_plan *row=topology->rows+i-1;
        const qa_launch_instance *source=qa_launch_snapshot_find(snapshot,row->recipe.selection.instance);
        qa_actor_owner owner=0;
        if(!source || !qa_application_provider_owner(f->application,row->recipe.selection.instance,&owner) || !owner ||
            !qa_launch_instance_restore_native_metadata(source,&row->recipe,row->roles,&row->metadata,error)) return false;
        frontend_native_q3_view *v=&row->state.owners;
        v->source_owner=v->receiver=owner; v->source_launch=qa_launch_instance_lease_view(row->metadata);
        if(!frontend_native_q3_prepare_restored(f,v,NULL,&row->row,error) ||
            !frontend_native_q3_prepare_commands(row->row,row->state.commands,error)) return false;
        qa_vfs *mounts=NULL;
        if(!qa_application_content_claim_view(topology->graph,row->mounts,&mounts,error)) return false;
        bool media=frontend_native_q3_prepare_media(row->row,&mounts,error);
        /* The genuine row consumes admission, including partial factory
         * failure. A rejected admission still leaves its view with us. */
        qa_vfs_destroy(mounts);
        if(!media || !frontend_native_q3_read(f,0,v,error)) return false;
    }
    topology->prepared=true; return true;
}
size_t frontend_native_q3_topology_count(const frontend_native_q3_topology *topology)
{ return topology?topology->count:0; }
bool frontend_native_q3_topology_read(frontend_native_q3_topology *topology,size_t ordinal,
    frontend_native_q3 **row,frontend_native_q3_import **state,q3n_client_refs *resources,qa_error *error)
{
    if(!topology || !topology->decoded || ordinal>=topology->count || !row || !state || !resources)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native topology read needs its saved physical ordinal");
    native_plan *plan=topology->rows+ordinal;
    *row=plan->row; *state=&plan->state; *resources=refs(plan); return true;
}
