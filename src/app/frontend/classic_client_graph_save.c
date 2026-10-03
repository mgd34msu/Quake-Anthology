#include "classic_client_graph_save.h"
#include "network_q1_recipe_save.h"
#include "remote_q1_effects.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct frontend_classic_client_graph {
    qa_frontend *frontend;
    bool present,decoded,staged,prepared;
    frontend_network_q1_client_recipe recipe;
    frontend_network_q1_client_state state;
    qa_bytes encoded_recipe;
};
static uint64_t q1_owner(size_t ordinal)
{ return UINT64_C(0x2000000000000000)|(uint64_t)(ordinal+1); }
static bool numbers(qa_frontend *f,frontend_scene_namespace *space,qa_error *error)
{
    for(size_t i=0;i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1 *row=frontend_remote_q1_at(f,i);
        for(size_t j=0;j<remote_q1_effects_light_count(row);++j) {
            uint64_t id=0;
            if(!remote_q1_effects_light_at(row,j,&id)) continue;
            if(!frontend_scene_namespace_capture_light(space,q1_owner(i),j,id,error)) return false;
        }
        for(size_t j=0;j<remote_q1_effects_static_count(row);++j) {
            uint64_t id=0; const qa_audio_asset *asset=NULL; qa_audio_mixer *mixer=NULL;
            if(!remote_q1_effects_static_at(row,j,&id,&asset,&mixer) || !id || !asset || !mixer ||
                !frontend_scene_namespace_capture_static_audio(space,q1_owner(i),j,id,error)) return false;
        }
    }
    return true;
}
bool frontend_classic_client_graph_capture_numbers(qa_frontend *f,frontend_scene_namespace *space,qa_error *error)
{ return f && f->capture && space && numbers(f,space,error); }
static bool span(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=bytes->size;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,count);
    if(io->offset>io->input.size || count>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool state_blob(qa_source_save_io *io,qa_buffer *value)
{
    qa_bytes bytes={value->data,value->size};
    if(!span(io,&bytes)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        value->data=bytes.size?malloc(bytes.size):NULL; value->size=bytes.size;
        if(bytes.size && !value->data) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining classic CLIENT continuation");
        if(bytes.size) memcpy(value->data,bytes.data,bytes.size);
    }
    return true;
}
static bool fields(qa_source_save_io *io,frontend_classic_client_graph *graph)
{
    uint8_t magic[4]={'Q','F','C','L'}; if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFCL",4) ||
        !qa_source_save_bool(io,&graph->present)) return false;
    if(!graph->present) return true;
    return span(io,&graph->encoded_recipe) && graph->encoded_recipe.size &&
        state_blob(io,&graph->state.physical) && graph->state.physical.size &&
        state_blob(io,&graph->state.receiver) &&
        state_blob(io,&graph->state.handshake) &&
        ((graph->state.receiver.size!=0)==(graph->state.handshake.size!=0)) &&
        state_blob(io,&graph->state.controller) && graph->state.controller.size;
}
bool frontend_classic_client_graph_checkpoint(qa_frontend *f,const frontend_remote_q1_restore_refs *refs,
    qa_buffer *out,qa_error *error)
{
    frontend_classic_client_graph graph={.frontend=f}; qa_buffer recipe={0}; qa_source_save_io io={0};
    if(!refs) return false;
    frontend_remote_q1_restore_refs scoped=*refs; scoped.owner=1; scoped.effects_owner=q1_owner(0);
    bool okay=f && f->capture && out && !out->data && !out->size &&
        frontend_network_q1_checkpoint(f,&scoped,&graph.present,&graph.recipe,&graph.state,error) &&
        frontend_remote_q1_count(f)==(graph.state.receiver.size?1u:0u) &&
        (!graph.present || frontend_network_q1_recipe_checkpoint(&graph.recipe,&recipe,error));
    graph.encoded_recipe=(qa_bytes){recipe.data,recipe.size};
    okay=okay && qa_source_save_writer(&io,NULL,error) && fields(&io,&graph) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&recipe); frontend_network_q1_client_state_free(&graph.state); return okay;
}
bool frontend_classic_client_graph_decode(qa_frontend *f,qa_bytes bytes,frontend_classic_client_graph **out,qa_error *error)
{
    if(!f || !f->source_restoring || !out || *out) return false;
    frontend_classic_client_graph *graph=calloc(1,sizeof(*graph));
    if(!graph) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining classic CLIENT graph prefix");
    graph->frontend=f; *out=graph;
    qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,graph) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(okay && graph->present) okay=frontend_network_q1_recipe_restore(graph->encoded_recipe,&graph->recipe,error);
    graph->decoded=okay; return okay;
}
bool frontend_classic_client_graph_stage(frontend_classic_client_graph *graph,const frontend_remote_q1_restore_refs *refs,
    const qa_console_save_resolvers *console,qa_error *error)
{
    if(!graph || !graph->decoded || graph->staged || !refs) return false;
    graph->staged=true; if(!graph->present) return true;
    frontend_remote_q1_restore_refs scoped=*refs; scoped.owner=1; scoped.effects_owner=q1_owner(0);
    return frontend_network_q1_stage_recipe(graph->frontend,&graph->recipe,&scoped,console,&graph->state,error);
}
bool frontend_classic_client_graph_prepare(frontend_classic_client_graph *graph,qa_error *error)
{
    if(!graph || !graph->staged || graph->prepared ||
        frontend_remote_q1_count(graph->frontend)!=(graph->state.receiver.size?1u:0u))
        return frontend_fail(error,QA_ERROR_FORMAT,"Classic CLIENT prefix differs from its actual receiver roster");
    graph->prepared=true; return true;
}
bool frontend_classic_client_graph_roots(frontend_classic_client_graph *graph,const frontend_remote_q1_restore_refs *refs,qa_error *error)
{
    if(!graph || !graph->prepared || !refs) return false;
    if(!graph->state.receiver.size) return true;
    frontend_remote_q1_restore_refs scoped=*refs; scoped.owner=1; scoped.effects_owner=q1_owner(0);
    return frontend_remote_q1_roots_attach_restored(frontend_remote_q1_at(graph->frontend,0),&scoped,error);
}
bool frontend_classic_client_graph_finish(frontend_classic_client_graph *graph,const frontend_remote_q1_restore_refs *refs,qa_error *error)
{
    if(!graph || !graph->prepared || !refs) return false;
    if(!graph->present) return true;
    frontend_remote_q1_restore_refs scoped=*refs; scoped.owner=1; scoped.effects_owner=q1_owner(0);
    return frontend_network_q1_finish_import(graph->frontend,&scoped,error);
}
void frontend_classic_client_graph_destroy(frontend_classic_client_graph *graph)
{
    if(!graph) return;
    frontend_network_q1_recipe_free(&graph->recipe); frontend_network_q1_client_state_free(&graph->state); free(graph);
}
