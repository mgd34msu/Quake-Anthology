#include "remote_q3_graph_children.h"
#include "remote_q3_private.h"
#include "remote_q3_runtime.h"
#include "capture.h"
#include "qa/application_native_q3_client_modules.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

struct frontend_remote_q3_graph_children {
    frontend_remote_q3 *parent;
    qa_frontend *frontend;
    qa_application *application;
    qa_application_content_graph *graph;
    qa_vfs *content, *mounts;
    const qa_resource *map, *animations[64];
    uint64_t identity, epoch, restart, configuration;
    qa_actor_owner receiver;
    qa_string_id service;
    uint32_t physical, seat;
    qa_bytes children[FRONTEND_REMOTE_Q3_CHILD_COUNT];
    bool reading;
};
static bool compiled(const frontend_remote_q3_resources *resources,qa_error *error)
{
    qa_application_native_q3_client_modules_recipe recipe;
    qa_frontend *f=frontend_remote_q3_frontend(resources->owner);
    return qa_application_native_q3_client_modules_recipe_read(f->application,&resources->domain.source,
        resources->domain.gamestate,&recipe,error) && (!recipe.pure ||
        frontend_fail(error,QA_ERROR_FORMAT,"Compiled children belong to the actual builtin CGAME recipe"));
}
static bool tuple(frontend_remote_q3_graph_children *plan,
    const frontend_remote_q3_resources *v,bool reading,qa_error *error)
{
    const qa_application_q3_remote_source *source=&v->domain.source;
    if(source->receiver.service_owner>UINT32_MAX)
        return frontend_fail(error,QA_ERROR_FORMAT,"Remote child service namespace is not an actual session string");
    if(!reading) {
        plan->identity=v->identity; plan->physical=v->physical_seat;
        plan->receiver=source->receiver.receiver; plan->service=(qa_string_id)source->receiver.service_owner;
        plan->seat=source->receiver.seat; plan->epoch=v->domain.epoch;
        plan->restart=v->domain.restart_generation; plan->configuration=source->configuration_generation;
        plan->content=v->domain.content; plan->mounts=v->mounts; plan->map=v->map;
        return true;
    }
    return (plan->identity==v->identity && plan->physical==v->physical_seat &&
        plan->receiver==source->receiver.receiver && plan->service==source->receiver.service_owner &&
        plan->seat==source->receiver.seat && plan->epoch==v->domain.epoch &&
        plan->restart==v->domain.restart_generation && plan->configuration==source->configuration_generation &&
        plan->content==v->domain.content && plan->mounts==v->mounts && plan->map==v->map) ||
        frontend_fail(error,QA_ERROR_FORMAT,"Remote children differ from their actual retained CLIENT resource parent");
}
static bool current(const frontend_remote_q3_graph_children *plan,qa_error *error)
{
    frontend_remote_q3_resources resources;
    if(!plan || !plan->parent || plan->frontend->application!=plan->application ||
        plan->graph!=qa_application_content_graph_read(plan->application) ||
        (plan->reading?!plan->frontend->source_restoring:!plan->frontend->capture) ||
        !frontend_remote_q3_resources_read(plan->parent,&resources,error) || !compiled(&resources,error)) return false;
    return tuple((frontend_remote_q3_graph_children *)plan,&resources,true,error);
}
static bool resource_encode(void *context,const qa_resource *resource,uint64_t *key,qa_error *error)
{
    frontend_remote_q3_graph_children *plan=context;
    if(!key || !resource || !current(plan,error)) return false;
    for(size_t i=0;i<64;++i) if(plan->animations[i]==resource) { *key=i+1; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"Remote animation leaves its actual physical holder inventory");
}
static bool resource_decode(void *context,uint64_t key,const qa_resource **out,qa_error *error)
{
    frontend_remote_q3_graph_children *plan=context;
    if(!out || !key || key>64 || !plan->animations[key-1] || !current(plan,error))
        return frontend_fail(error,QA_ERROR_FORMAT,"Remote animation has no retained graph resource row");
    *out=plan->animations[key-1]; return true;
}
static bool blob(qa_source_save_io *io,qa_bytes *bytes)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=reading?0:bytes->size;
    if(!qa_source_save_count(io,&count,SIZE_MAX) || !count) return false;
    if(!reading) return bytes->data && qa_source_save_bytes(io,(void *)bytes->data,count);
    if(io->offset>io->input.size || count>io->input.size-io->offset)
        return frontend_fail(io->error,QA_ERROR_FORMAT,"Remote child exceeds its complete saved envelope");
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool fields(qa_source_save_io *io,frontend_remote_q3_graph_children *plan)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','R','C','H'}; uint32_t version=1;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QRCH",4) ||
        !qa_source_save_u32(io,&version) || version!=1 ||
        !qa_source_save_u64(io,&plan->identity) || !plan->identity ||
        !qa_source_save_u32(io,&plan->physical) ||
        !qa_source_save_string(io,&plan->receiver) || !plan->receiver ||
        !qa_source_save_string(io,&plan->service) || !plan->service ||
        !qa_source_save_u32(io,&plan->seat) || !qa_source_save_u64(io,&plan->epoch) || !plan->epoch ||
        !qa_source_save_u64(io,&plan->restart) ||
        !qa_source_save_u64(io,&plan->configuration) || !plan->configuration) return false;
    for(size_t i=0;i<64;++i) {
        uint64_t pool=0,resource=0;
        if((!reading && plan->animations[i] &&
            !qa_application_content_resource_id(plan->graph,plan->animations[i],&pool,&resource)) ||
            !qa_source_save_u64(io,&pool) || !qa_source_save_u64(io,&resource) || (!!pool!=!!resource)) return false;
        if(reading) {
            plan->animations[i]=resource?qa_application_content_resource(plan->graph,pool,resource):NULL;
            if(resource && (!plan->animations[i] ||
                qa_resource_pool_find(qa_vfs_resources(plan->content),qa_resource_id(plan->animations[i]))!=plan->animations[i]))
                return frontend_fail(io->error,QA_ERROR_FORMAT,"Remote animation resource leaves its actual CLIENT content pool");
        }
    }
    for(size_t i=0;i<FRONTEND_REMOTE_Q3_CHILD_COUNT;++i) if(!blob(io,plan->children+i)) return false;
    return true;
}
static bool child_checkpoint(frontend_remote_q3 *row,const frontend_remote_q3_services_view *v,
    const q3n_client_refs *refs,size_t child,qa_buffer *out,qa_error *error)
{
    switch(child) {
    case FRONTEND_REMOTE_Q3_CHILD_CLIENT: return qa_native_q3_remote_client_checkpoint(v->client,out,error);
    case FRONTEND_REMOTE_Q3_CHILD_SOURCE: return q3n_remote_source_checkpoint(v->source,out,error);
    case FRONTEND_REMOTE_Q3_CHILD_MEDIA: return q3n_media_checkpoint(v->media,out,error);
    case FRONTEND_REMOTE_Q3_CHILD_CLIENTS: return q3n_clients_checkpoint(v->clients,refs,out,error);
    case FRONTEND_REMOTE_Q3_CHILD_RUNTIME: return frontend_remote_q3_runtime_checkpoint(row->runtime,out,error);
    case FRONTEND_REMOTE_Q3_CHILD_FRAME: return frontend_remote_q3_frame_checkpoint(row->frames,out,error);
    default: return false;
    }
}
bool frontend_remote_q3_graph_children_checkpoint(frontend_remote_q3 *row,qa_buffer *out,qa_error *error)
{
    frontend_remote_q3_services_view services;
    if(!row || !out || out->data || out->size || !row->frontend->capture || row->frontend->source_restoring ||
        !row->runtime || !row->frames || !frontend_remote_q3_services_read(row,&services,error) ||
        !compiled(&services.resources,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote compiled children require their actual captured complete owners");
    bool captured=false;
    for(size_t i=0;;++i) {
        qa_q3_presentation_assets *assets=frontend_capture_assets_at(row->frontend->capture,i);
        if(!assets) break;
        if(assets==services.resources.assets) { captured=true; break; }
    }
    if(!captured) return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote children require their real registry capture lease");
    frontend_remote_q3_graph_children plan={.parent=row,.frontend=row->frontend,.application=row->application,
        .graph=qa_application_content_graph_read(row->application)};
    if(!plan.graph || !tuple(&plan,&services.resources,false,error)) return false;
    for(uint32_t i=0;i<64;++i) {
        const qa_vfs_acquisition *receipt=NULL;
        if(!q3n_clients_animation_holder(services.clients,i,plan.animations+i,&receipt,error)) return false;
    }
    q3n_client_refs refs={&plan,resource_encode,resource_decode};
    qa_buffer buffers[FRONTEND_REMOTE_Q3_CHILD_COUNT]={0}; qa_source_save_io io={0};
    bool okay=current(&plan,error);
    for(size_t i=0;okay && i<FRONTEND_REMOTE_Q3_CHILD_COUNT;++i) {
        okay=child_checkpoint(row,&services,&refs,i,buffers+i,error);
        plan.children[i]=(qa_bytes){buffers[i].data,buffers[i].size};
    }
    if(okay) okay=current(&plan,error) && qa_source_save_writer(&io,qa_application_session(row->application),error) &&
        fields(&io,&plan) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    for(size_t i=0;i<FRONTEND_REMOTE_Q3_CHILD_COUNT;++i) qa_buffer_free(buffers+i);
    if(!okay && (!error || error->code==QA_OK))
        frontend_fail(error,QA_ERROR_FORMAT,"Remote compiled children do not form a complete retained envelope");
    return okay;
}
bool frontend_remote_q3_graph_children_decode(frontend_remote_q3 *row,qa_bytes bytes,
    frontend_remote_q3_graph_children **out,qa_error *error)
{
    frontend_remote_q3_resources resources;
    if(!row || !out || *out || row->frontend->capture || row->modules || row->services ||
        !frontend_remote_q3_resources_import_read(row,&resources,error) || !compiled(&resources,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote child decode precedes actual empty service reconstruction");
    frontend_remote_q3_graph_children *plan=calloc(1,sizeof(*plan));
    if(!plan) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining remote compiled child envelope");
    plan->parent=row; plan->frontend=row->frontend; plan->application=row->application;
    plan->graph=qa_application_content_graph_read(row->application); plan->reading=true;
    plan->content=resources.domain.content; plan->mounts=resources.mounts; plan->map=resources.map;
    qa_source_save_io io={0};
    bool okay=plan->graph && qa_source_save_reader(&io,qa_application_session(row->application),bytes,error) &&
        fields(&io,plan) && qa_source_save_finish(&io,NULL) && current(plan,error);
    qa_source_save_dispose(&io);
    if(!okay) {
        free(plan);
        if(!error || error->code==QA_OK)
            frontend_fail(error,QA_ERROR_FORMAT,"Invalid complete remote compiled child envelope");
        return false;
    }
    *out=plan; return true;
}
bool frontend_remote_q3_graph_child_read(const frontend_remote_q3_graph_children *plan,
    frontend_remote_q3_graph_child child,qa_bytes *out,qa_error *error)
{
    if(!out || (unsigned)child>=FRONTEND_REMOTE_Q3_CHILD_COUNT || !current(plan,error)) return false;
    *out=plan->children[child]; return true;
}
bool frontend_remote_q3_graph_client_refs(frontend_remote_q3_graph_children *plan,
    q3n_client_refs *out,qa_error *error)
{
    if(!out || !current(plan,error)) return false;
    *out=(q3n_client_refs){plan,resource_encode,resource_decode}; return true;
}
void frontend_remote_q3_graph_children_destroy(frontend_remote_q3_graph_children *plan)
{ free(plan); }
