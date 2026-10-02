#include "unified_graph_save.h"
#include "network_unified_restore.h"
#include "remote_unified.h"
#include "internal.h"
#include "save_private.h"
#include "unified_q3_runtime_factory.h"
#include "qa/media_library_save.h"
#include "qa/media_resource.h"
#include <stdlib.h>
#include <string.h>

struct frontend_unified_graph {
    qa_frontend *frontend;
    qa_bytes service,children;
    frontend_remote_unified *replica;
    bool service_present,replica_present,decoded,staged,prepared;
};
typedef struct unified_factory_scope unified_factory_scope;
typedef struct unified_refs_context {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    const frontend_unified_graph_refs *refs;
    unified_factory_scope *factories;
} unified_refs_context;
struct unified_factory_scope {
    unified_factory_scope *next;
    unified_refs_context *parent;
    frontend_remote_unified *replica;
    size_t row;
};
static void refs_dispose(unified_refs_context *context)
{
    while(context->factories) {
        unified_factory_scope *scope=context->factories;
        context->factories=scope->next; free(scope);
    }
}
static bool span(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=bytes->size;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,count);
    if(io->offset>io->input.size || count>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool fields(qa_source_save_io *io,frontend_unified_graph *graph)
{
    char magic[4]={'Q','F','U','G'}; uint32_t version=1;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFUG",4) &&
        qa_source_save_u32(io,&version) && version==1 &&
        qa_source_save_bool(io,&graph->service_present) && span(io,&graph->service) &&
        (graph->service_present==(graph->service.size!=0)) &&
        qa_source_save_bool(io,&graph->replica_present) && span(io,&graph->children) &&
        (graph->replica_present==(graph->children.size!=0)) &&
        (!graph->replica_present || graph->service_present);
}
static bool image_encode(void *context,const qa_scene_image *image,uint64_t *out,qa_error *e)
{ return frontend_scene_image_encode(((unified_refs_context *)context)->refs->scene,image,out,e); }
static bool image_decode(void *context,uint64_t key,const qa_scene_image **out,qa_error *e)
{ return frontend_scene_image_decode(((unified_refs_context *)context)->refs->scene,key,out,e); }
static bool asset_encode(void *context,const qa_audio_asset *asset,uint64_t *out,qa_error *e)
{
    const frontend_unified_graph_refs *r=((unified_refs_context *)context)->refs;
    uint64_t index=0;
    if(!r->assets || !out || !qa_audio_asset_inventory_index(r->assets,asset,&index) || index==UINT64_MAX)
        return frontend_fail(e,QA_ERROR_FORMAT,"Unified audio holder leaves the actual asset dictionary");
    *out=index+1; return true;
}
static bool asset_decode(void *context,uint64_t key,const qa_audio_asset **out,qa_error *e)
{
    const frontend_unified_graph_refs *r=((unified_refs_context *)context)->refs;
    qa_audio_asset *asset=r->assets && key?qa_audio_asset_inventory_at(r->assets,key-1):NULL;
    if(!asset || !out) return frontend_fail(e,QA_ERROR_FORMAT,"Unified audio key has no imported asset holder");
    *out=asset; return true;
}
static bool actor_encode(void *context,qa_actor_id actor,qa_saved_actor_id *out,qa_error *e)
{
    frontend_remote_unified *replica=((unified_refs_context *)context)->replica;
    return (out && frontend_remote_unified_wire_actor(replica,actor,out)) ||
        frontend_fail(e,QA_ERROR_FORMAT,"Unified effect actor leaves its actual replica ledger");
}
static bool actor_decode(void *context,qa_saved_actor_id saved,qa_actor_id *out,qa_error *e)
{ return frontend_remote_unified_actor_retained(((unified_refs_context *)context)->replica,saved.slot,saved.generation,out,e); }
static bool identity_encode(void *context,bool audio,uint64_t actual,uint64_t *out,qa_error *e)
{
    frontend_scene_namespace *space=((unified_refs_context *)context)->refs->scene;
    return audio?frontend_scene_static_audio_identity_encode(space,actual,out,e):
        frontend_scene_light_identity_encode(space,actual,out,e);
}
static bool identity_decode(void *context,bool audio,uint64_t key,uint64_t *out,qa_error *e)
{
    frontend_scene_namespace *space=((unified_refs_context *)context)->refs->scene;
    return audio?frontend_scene_static_audio_identity_decode(space,key,out,e):
        frontend_scene_light_identity_decode(space,key,out,e);
}
static bool q2_light_encode(void *context,uint64_t id,uint64_t *out,qa_error *e)
{ return identity_encode(context,false,id,out,e); }
static bool q2_light_decode(void *context,uint64_t key,uint64_t *out,qa_error *e)
{ return identity_decode(context,false,key,out,e); }
static bool model_encode(void *context,const qa_scene_model *model,uint64_t *out,qa_error *e)
{ return frontend_scene_root_encode(((unified_refs_context *)context)->refs->roots,model,out,e); }
static bool model_decode(void *context,uint64_t key,qa_scene_model **out,qa_error *e)
{ return frontend_scene_root_decode(((unified_refs_context *)context)->refs->roots,key,out,e); }
static bool scene_current(void *context,const void *owner,uint64_t identity,qa_error *e)
{
    unified_refs_context *c=context;
    return frontend_component_scene_save_current(c->frontend,c->refs->components,owner,identity,e);
}
static bool factory_bank(unified_factory_scope *scope,frontend_unified_presentation_q3_row *row,
    frontend_unified_bank_view *bank,qa_error *e)
{
    return frontend_remote_unified_presentation_q3_row_read(scope->replica,scope->row,row,e) &&
        frontend_unified_media_bank_read(row->media,row->bank,bank) && bank->movies && bank->files;
}
static bool factory_asset_encode(void *context,const qa_cinematic_asset *asset,uint64_t *out,qa_error *e)
{
    unified_factory_scope *scope=context; frontend_unified_presentation_q3_row row;
    frontend_unified_bank_view bank;
    if(!out || !factory_bank(scope,&row,&bank,e)) return false;
    for(size_t i=0;i<qa_media_library_record_count(bank.movies);++i)
        if(qa_media_library_record_at(bank.movies,i)==asset) { *out=i+1; return true; }
    return frontend_fail(e,QA_ERROR_FORMAT,"Compiled movie leaves its actual Source bank cache");
}
static bool factory_asset_decode(void *context,uint64_t key,const char *path,qa_cinematic_asset **out,qa_error *e)
{
    unified_factory_scope *scope=context; frontend_unified_presentation_q3_row row;
    frontend_unified_bank_view bank;
    if(!out || *out || !path || !key || key-1>SIZE_MAX || !factory_bank(scope,&row,&bank,e)) return false;
    const qa_cinematic_asset *asset=qa_media_library_record_at(bank.movies,(size_t)key-1);
    const qa_resource *resource=qa_cinematic_asset_resource(asset);
    uint64_t pool=0,version=0;
    if(!asset || !resource ||
        !qa_application_content_resource_id(scope->parent->refs->content,resource,&pool,&version) ||
        qa_resource_pool_find(qa_vfs_resources(bank.files),qa_resource_id(resource))!=resource)
        return frontend_fail(e,QA_ERROR_FORMAT,"Compiled movie has no exact imported cache resource");
    qa_cinematic_source source=qa_cinematic_asset_source(asset);
    const char *extension=strrchr(path,'.');
    const char *expected=source.format==QA_CINEMATIC_CIN?".cin":source.format==QA_CINEMATIC_ROQ?".roq":
        source.format==QA_CINEMATIC_OGV?".ogv":source.format==QA_CINEMATIC_IMAGE?".pcx":NULL;
    if(!extension || !expected || strlen(extension)!=4)
        return frontend_fail(e,QA_ERROR_FORMAT,"Compiled movie path has an incompatible cache format");
    for(size_t i=0;i<4;++i) {
        unsigned c=(unsigned char)extension[i]; if(c>='A' && c<='Z') c+='a'-'A';
        if(c!=(unsigned char)expected[i]) return frontend_fail(e,QA_ERROR_FORMAT,"Compiled movie path has an incompatible cache format");
    }
    qa_cinematic_asset_retain((qa_cinematic_asset *)asset); *out=(qa_cinematic_asset *)asset; return true;
}
static bool factory_target_encode(void *context,uint64_t target,qa_buffer *out,qa_error *e)
{
    unified_factory_scope *scope=context; frontend_unified_presentation_q3_row row;
    if(!frontend_remote_unified_presentation_q3_row_read(scope->replica,scope->row,&row,e) ||
        target!=row.audio_owner || !scope->parent->refs->audio || !scope->parent->refs->audio->encode) return false;
    const qa_audio_checkpoint_refs *audio=scope->parent->refs->audio;
    return audio->encode(audio->context,QA_AUDIO_REFERENCE_BUS,target,out,e);
}
static bool factory_target_decode(void *context,qa_bytes bytes,uint64_t *out,qa_error *e)
{
    unified_factory_scope *scope=context; frontend_unified_presentation_q3_row row;
    const qa_audio_checkpoint_refs *audio=scope->parent->refs->audio; uint64_t target=0;
    if(!out || !audio || !audio->decode ||
        !frontend_remote_unified_presentation_q3_row_read(scope->replica,scope->row,&row,e) ||
        !audio->decode(audio->context,QA_AUDIO_REFERENCE_BUS,bytes,&target,e) || target!=row.audio_owner)
        return frontend_fail(e,QA_ERROR_FORMAT,"Compiled movie target differs from its actual Source bus");
    *out=target; return true;
}
static bool factory_image_encode(void *context,const qa_scene_image *image,uint64_t *out,qa_error *e)
{ return image_encode(((unified_factory_scope *)context)->parent,image,out,e); }
static bool factory_image_decode(void *context,uint64_t key,const qa_scene_image **out,qa_error *e)
{ return image_decode(((unified_factory_scope *)context)->parent,key,out,e); }
static bool factory_refs(void *context,frontend_remote_unified *replica,size_t row,bool importing,
    const q3n_client_refs *clients,frontend_unified_q3_runtime_factory_refs *out,qa_error *e)
{
    unified_refs_context *parent=context;
    if(!parent || replica!=parent->replica || !clients || !out || !parent->refs->audio ||
        (importing && !parent->frontend->source_restoring))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Compiled factory requires its actual graph and Source row");
    unified_factory_scope *scope=calloc(1,sizeof(*scope));
    if(!scope) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining compiled factory dictionary scope");
    *scope=(unified_factory_scope){.next=parent->factories,.parent=parent,.replica=replica,.row=row};
    parent->factories=scope;
    *out=(frontend_unified_q3_runtime_factory_refs){.clients=*clients,.audio=*parent->refs->audio,
        .movies={.context=scope,.asset_encode=factory_asset_encode,.asset_decode=factory_asset_decode,
            .playback={scope,factory_target_encode,factory_target_decode},
            .publication={scope,factory_image_encode,factory_image_decode}}};
    return true;
}
static frontend_unified_presentation_refs child_refs(unified_refs_context *context)
{
    const frontend_unified_graph_refs *r=context->refs;
    frontend_unified_media_refs media={.content=r->content,.scene=r->scene,
        .models=r->models,.roots=r->roots,.audio=r->assets,.owner=1};
    frontend_unified_media_refs pending=media; pending.owner=2;
    qa_hud_checkpoint_refs hud={context,image_encode,image_decode};
    return (frontend_unified_presentation_refs){.media=media,.pending_media=pending,
        .render={.scene=r->scene,.hud=hud},
        .events={context,asset_encode,asset_decode},
        .q1={.audio=r->audio,.hud=hud,.context=context,.identity_encode=identity_encode,
            .identity_decode=identity_decode,.asset_encode=asset_encode,.asset_decode=asset_decode},
        .q2={.content=r->content,.audio=r->audio,
            .effects={.context=context,.image_encode=image_encode,.image_decode=image_decode,
                .actor_encode=actor_encode,.actor_decode=actor_decode,
                .light_encode=q2_light_encode,.light_decode=q2_light_decode},
            .model_encode=model_encode,.model_decode=model_decode},
        .components={.content=r->content,.context=context,.scene_current=scene_current},
        .factory_context=context,.factory_refs=factory_refs};
}
/* Q1 group scopes use a distinct physical producer domain. Each numeric row
 * follows the genuine child getter's traversal, separately for both kinds. */
static bool group_owner(size_t replica,size_t group,uint64_t *out)
{
    if(replica>=0x3fffffffu || group>=UINT32_MAX || !out) return false;
    *out=UINT64_C(0x4000000000000000)|((uint64_t)(replica+1)<<32)|(uint64_t)(group+1); return true;
}
static bool numbers(qa_frontend *f,frontend_scene_namespace *space,bool capture,qa_error *e)
{
    for(size_t i=0;i<frontend_remote_unified_count(f);++i) {
        frontend_unified_presentation_children children;
        if(!frontend_remote_unified_presentation_children_read(frontend_remote_unified_at(f,i),&children,e)) return false;
        for(size_t group=0;group<frontend_unified_q1_group_count(children.q1);++group) {
            uint64_t owner=0;
            if(!group_owner(i,group,&owner)) return frontend_fail(e,QA_ERROR_FORMAT,"Unified Q1 group ordinal overflows its physical namespace");
            for(size_t n=0;n<frontend_unified_q1_light_count(children.q1,group);++n) {
                uint64_t id=0;
                if(!frontend_unified_q1_light_at(children.q1,group,n,&id)) return false;
                if(!id) continue;
                if(!(capture?frontend_scene_namespace_capture_light(space,owner,n,id,e):
                    frontend_scene_namespace_qualify_light(space,owner,n,id,e))) return false;
            }
            for(size_t n=0;n<frontend_unified_q1_static_count(children.q1,group);++n) {
                uint64_t id=0; const qa_audio_asset *asset=NULL; qa_audio_mixer *mixer=NULL;
                if(!frontend_unified_q1_static_at(children.q1,group,n,&id,&asset,&mixer) || !id) return false;
                if(!(capture?frontend_scene_namespace_capture_static_audio(space,owner,n,id,e):
                    frontend_scene_namespace_qualify_static_audio(space,owner,n,id,e))) return false;
            }
        }
        for(size_t bank=0;bank<frontend_unified_q2_bank_count(children.q2);++bank) {
            uint64_t owner=0;
            if(!group_owner(i,bank,&owner)) return frontend_fail(e,QA_ERROR_FORMAT,"Unified Q2 bank ordinal overflows its physical namespace");
            owner=(owner&UINT64_C(0x3fffffffffffffff))|UINT64_C(0x8000000000000000);
            for(size_t n=0;n<frontend_unified_q2_light_count(children.q2,bank);++n) {
                uint64_t id=0;
                if(!frontend_unified_q2_light_at(children.q2,bank,n,&id)) return false;
                if(!id) continue;
                if(!(capture?frontend_scene_namespace_capture_light(space,owner,n,id,e):
                    frontend_scene_namespace_qualify_light(space,owner,n,id,e))) return false;
            }
        }
    }
    return true;
}
bool frontend_unified_graph_capture_numbers(qa_frontend *f,frontend_scene_namespace *space,qa_error *e)
{ return f && f->capture && space && numbers(f,space,true,e); }
bool frontend_unified_graph_audio_scope(qa_frontend *f,uint64_t id,uint32_t *domain,uint64_t *owner,uint64_t *row)
{
    if(!f || !id || !domain || !owner || !row) return false;
    for(size_t i=0;i<frontend_remote_unified_count(f);++i) {
        frontend_remote_unified *replica=frontend_remote_unified_at(f,i);
        frontend_unified_presentation_children children;
        if(!frontend_remote_unified_presentation_children_read(replica,&children,NULL)) return false;
        if(children.audio_owner==id) { *domain=12; *owner=i+1; *row=0; return true; }
        for(size_t n=0;n<frontend_remote_unified_presentation_q3_client_count(replica);++n) {
            frontend_unified_presentation_q3_row source;
            if(!frontend_remote_unified_presentation_q3_row_read(replica,n,&source,NULL)) return false;
            if(source.audio_owner==id) { *domain=17; *owner=i+1; *row=n+1; return true; }
        }
        for(size_t n=0;n<frontend_unified_q1_group_count(children.q1);++n) {
            uint64_t bus=0; qa_audio_music *player=NULL;
            if(frontend_unified_q1_music_at(children.q1,n,&bus,&player) && bus==id) {
                *domain=13; *owner=i+1; *row=n+1; return true;
            }
        }
        for(size_t n=0;n<frontend_unified_q2_music_count(children.q2);++n) {
            uint64_t bus=0; qa_audio_music *player=NULL;
            if(frontend_unified_q2_music_at(children.q2,n,&bus,&player) && bus==id) {
                *domain=14; *owner=i+1; *row=n+1; return true;
            }
        }
        for(size_t n=0;n<frontend_remote_unified_presentation_audio_count(replica);++n) {
            uint64_t actual=0; qa_actor_id actor={0};
            if(!frontend_remote_unified_presentation_audio_read(replica,n,&actor,&actual)) return false;
            if(actual==id) { *domain=15; *owner=i+1; *row=n+1; return true; }
        }
    }
    return false;
}
bool frontend_unified_graph_audio_resolve(qa_frontend *f,uint32_t domain,uint64_t owner,uint64_t row,uint64_t *out)
{
    if(!f || !owner || owner-1>SIZE_MAX || !out) return false;
    frontend_remote_unified *replica=frontend_remote_unified_at(f,(size_t)owner-1);
    frontend_unified_presentation_children children;
    if(!replica || !frontend_remote_unified_presentation_children_read(replica,&children,NULL)) return false;
    if(domain==12) { if(row || !children.audio_owner) return false; *out=children.audio_owner; return true; }
    if(!row || row-1>SIZE_MAX) return false;
    qa_audio_music *player=NULL;
    if(domain==13) return frontend_unified_q1_music_at(children.q1,(size_t)row-1,out,&player) && *out;
    if(domain==14) return frontend_unified_q2_music_at(children.q2,(size_t)row-1,out,&player) && *out;
    if(domain==15) { qa_actor_id actor={0}; return frontend_remote_unified_presentation_audio_read(replica,(size_t)row-1,&actor,out) && *out; }
    if(domain==17) {
        frontend_unified_presentation_q3_row source;
        if(!frontend_remote_unified_presentation_q3_row_read(replica,(size_t)row-1,&source,NULL) || !source.audio_owner) return false;
        *out=source.audio_owner; return true;
    }
    return false;
}
bool frontend_unified_graph_checkpoint(qa_frontend *f,const frontend_unified_graph_refs *refs,qa_buffer *out,qa_error *e)
{
    frontend_unified_graph graph={.frontend=f}; qa_buffer service={0},children={0}; qa_net_client_id client={0};
    bool ok=f && refs && refs->content && out && !out->data && !out->size &&
        frontend_network_unified_service_checkpoint(f,refs->content,&graph.service_present,&service,e) &&
        frontend_network_unified_replica_capture_read(f,&client,&graph.replica,&graph.replica_present,e);
    unified_refs_context context={.frontend=f,.replica=graph.replica,.refs=refs}; frontend_unified_presentation_refs child=child_refs(&context);
    if(ok && graph.replica_present) ok=frontend_remote_unified_presentation_checkpoint(graph.replica,&child,&children,e);
    graph.service=(qa_bytes){service.data,service.size}; graph.children=(qa_bytes){children.data,children.size};
    qa_source_save_io io={0};
    ok=ok && qa_source_save_writer(&io,NULL,e) && fields(&io,&graph) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&service); qa_buffer_free(&children); refs_dispose(&context); return ok;
}
bool frontend_unified_graph_decode(qa_frontend *f,qa_bytes bytes,frontend_unified_graph **out,qa_error *e)
{
    if(!f || !f->source_restoring || !out || *out) return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified graph decode requires its isolated actual parent");
    frontend_unified_graph *graph=calloc(1,sizeof(*graph));
    if(!graph) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining Unified graph envelope");
    graph->frontend=f; *out=graph;
    qa_source_save_io io={0}; bool ok=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,graph) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    graph->decoded=ok;
    return ok || frontend_fail(e,QA_ERROR_FORMAT,"Unified graph has an invalid physical service or child envelope");
}
bool frontend_unified_graph_stage(frontend_unified_graph *graph,qa_application_content_graph *content,const qa_console_save_resolvers *console,qa_error *e)
{
    if(!graph || !graph->decoded || graph->staged || !content || (graph->service_present && !console))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified graph staging requires its genuine console resolver lease");
    graph->staged=true;
    return !graph->service_present || frontend_network_unified_service_stage(graph->frontend,content,console,graph->service,e);
}
bool frontend_unified_graph_prepare(frontend_unified_graph *graph,const frontend_unified_graph_refs *refs,qa_error *e)
{
    qa_net_client_id client={0}; bool present=false; frontend_remote_unified *replica=NULL;
    if(!graph || !graph->staged || graph->prepared || !refs ||
        !frontend_network_unified_replica_import_read(graph->frontend,&client,&replica,&present,e) ||
        present!=graph->replica_present) return frontend_fail(e,QA_ERROR_FORMAT,"Unified graph differs from the sole imported physical replica");
    graph->replica=replica;
    unified_refs_context context={.frontend=graph->frontend,.replica=replica,.refs=refs}; frontend_unified_presentation_refs child=child_refs(&context);
    bool ok=!present || frontend_remote_unified_presentation_restore_prepare(replica,&child,graph->children,e);
    refs_dispose(&context); if(!ok) return false;
    graph->prepared=true; return true;
}
typedef bool (*unified_stage)(frontend_remote_unified *,const frontend_unified_presentation_refs *,qa_error *);
static bool apply(frontend_unified_graph *graph,const frontend_unified_graph_refs *refs,unified_stage stage,qa_error *e)
{
    if(!graph || !graph->prepared || !refs) return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified child stage requires its admitted actual replica prefix");
    if(!graph->replica_present) return true;
    unified_refs_context context={.frontend=graph->frontend,.replica=graph->replica,.refs=refs}; frontend_unified_presentation_refs child=child_refs(&context);
    bool ok=stage(graph->replica,&child,e); refs_dispose(&context); return ok;
}
bool frontend_unified_graph_prepare_components(frontend_unified_graph *graph,const frontend_unified_graph_refs *refs,qa_error *e)
{ return apply(graph,refs,frontend_remote_unified_presentation_restore_components,e); }
bool frontend_unified_graph_roots(frontend_unified_graph *graph,const frontend_unified_graph_refs *refs,qa_error *e)
{ return apply(graph,refs,frontend_remote_unified_presentation_restore_roots,e); }
bool frontend_unified_graph_audio_prefix(frontend_unified_graph *graph,const frontend_unified_graph_refs *refs,qa_error *e)
{
    return apply(graph,refs,frontend_remote_unified_presentation_restore_factories,e) &&
        apply(graph,refs,frontend_remote_unified_presentation_restore_audio_prefix,e);
}
bool frontend_unified_graph_finish(frontend_unified_graph *graph,const frontend_unified_graph_refs *refs,qa_error *e)
{
    return apply(graph,refs,frontend_remote_unified_presentation_restore_finish,e) &&
        numbers(graph->frontend,refs->scene,false,e) &&
        (!graph->service_present || frontend_network_unified_finish_import(graph->frontend,e));
}
void frontend_unified_graph_destroy(frontend_unified_graph *graph)
{ free(graph); }
