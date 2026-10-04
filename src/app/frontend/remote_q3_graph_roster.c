#include "remote_q3_graph_roster.h"
#include "remote_q3_runtime.h"
#include "remote_q3_transport.h"
#include "network_initial_graph.h"
#include "network_restore.h"
#include "network_restore_attempt.h"
#include "network_predictor.h"
#include "qa/q3_assets_save.h"
#include "qa/application_native_q3_remote_modules_save.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

typedef struct graph_parent {
    frontend_remote_q3_graph_recipe *recipe;
    frontend_remote_q3 *decoded;
    frontend_remote_q3_initial *initial;
    frontend_remote_q3_modules *modules;
    frontend_remote_q3_graph_children *children;
    frontend_remote_q3_frame_callbacks callbacks;
    qa_bytes topology,compiled,wrapper;
    bool prefix,modules_prepared,runtime_prepared,children_restored,modules_restored;
} graph_parent;
struct frontend_remote_q3_graph_roster {
    qa_frontend *frontend;
    qa_application *application;
    graph_parent *parents;
    size_t decoded,count;
    bool initial,attempted;
};
static bool blob(qa_source_save_io *io,qa_bytes *value)
{
    size_t size=io->direction==QA_SOURCE_SAVE_WRITE?value->size:0;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE)
        return !size || (value->data && qa_source_save_bytes(io,(void *)value->data,size));
    if(io->offset>io->input.size || size>io->input.size-io->offset)
        return frontend_fail(io->error,QA_ERROR_FORMAT,"Remote graph child exceeds its complete envelope");
    *value=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
static bool header(qa_source_save_io *io,size_t *decoded,bool *initial)
{
    uint8_t magic[4]={'Q','R','G','O'}; return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QRGO",4) &&
        qa_source_save_count(io,decoded,SIZE_MAX/sizeof(graph_parent)-1) && qa_source_save_bool(io,initial);
}
static bool shape(const graph_parent *parent,bool decoded)
{ return parent->topology.size && parent->wrapper.size && (decoded || !parent->compiled.size); }
bool frontend_remote_q3_graph_checkpoint(qa_frontend *f,const frontend_remote_q3_modules_save_refs *refs,
    qa_buffer *out,qa_error *error)
{
    frontend_network_initial_graph_view initial;
    if(!f || !f->capture || f->source_restoring || !refs || !refs->movies || !out || out->data || out->size ||
        !frontend_network_initial_graph_read(f,&initial,error)) return false;
    size_t decoded=frontend_remote_q3_count(f); bool has_initial=initial.present;
    qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,qa_application_session(f->application),error) && header(&io,&decoded,&has_initial);
    for(size_t i=0;okay && i<decoded+(has_initial?1:0);++i) {
        qa_buffer topology={0},compiled={0},wrapper={0}; frontend_remote_q3_modules *modules=NULL;
        if(i<decoded) {
            frontend_remote_q3 *row=frontend_remote_q3_at(f,i); frontend_remote_q3_resources resources;
            qa_application_native_q3_client_modules_recipe recipe;
            okay=frontend_remote_q3_resources_read(row,&resources,error) &&
                frontend_remote_q3_graph_resources_checkpoint(&resources,&topology,error) &&
                qa_application_native_q3_client_modules_recipe_read(f->application,&resources.domain.source,
                    resources.domain.gamestate,&recipe,error);
            modules=frontend_remote_q3_modules_read(row);
            if(okay && !recipe.pure) okay=frontend_remote_q3_graph_children_checkpoint(row,&refs->audio,&compiled,error);
            if(okay && !modules)
                okay=frontend_fail(error,QA_ERROR_FORMAT,"Remote graph lacks its actual source UI module wrapper");
        } else {
            frontend_remote_q3_initial_view resources;
            modules=initial.modules;
            okay=initial.parent && modules && frontend_remote_q3_initial_read(initial.parent,&resources,error) &&
                frontend_remote_q3_graph_initial_checkpoint(&resources,&topology,error);
        }
        if(okay && modules) okay=frontend_remote_q3_modules_checkpoint(modules,refs,&wrapper,error);
        graph_parent parent={.topology={topology.data,topology.size},.compiled={compiled.data,compiled.size},
            .wrapper={wrapper.data,wrapper.size}};
        okay=okay && shape(&parent,i<decoded) && blob(&io,&parent.topology) &&
            blob(&io,&parent.compiled) && blob(&io,&parent.wrapper);
        qa_buffer_free(&topology); qa_buffer_free(&compiled); qa_buffer_free(&wrapper);
    }
    okay=okay && frontend_network_initial_graph_current(f,&initial) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
void frontend_remote_q3_graph_destroy(frontend_remote_q3_graph_roster *graph)
{
    if(!graph) return;
    for(size_t i=0;graph->parents && i<graph->count;++i) {
        frontend_remote_q3_graph_recipe_destroy(graph->parents[i].recipe);
        frontend_remote_q3_graph_children_destroy(graph->parents[i].children);
    }
    free(graph->parents); free(graph);
}
bool frontend_remote_q3_graph_decode(qa_frontend *f,qa_bytes bytes,
    frontend_remote_q3_graph_roster **out,qa_error *error)
{
    if(!f || !f->application || !f->source_restoring || f->capture || !out || *out) return false;
    frontend_remote_q3_graph_roster *graph=calloc(1,sizeof(*graph));
    if(!graph) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining the real remote cold graph roster");
    graph->frontend=f; graph->application=f->application; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,qa_application_session(f->application),bytes,error) &&
        header(&io,&graph->decoded,&graph->initial);
    if(okay) {
        graph->count=graph->decoded+(graph->initial?1:0);
        if(graph->count>(io.input.size-io.offset)/24) okay=false;
        else if(graph->count) {
            graph->parents=calloc(graph->count,sizeof(*graph->parents));
            if(!graph->parents) okay=frontend_fail(error,QA_ERROR_MEMORY,"Retaining remote graph parent recipes");
        }
    }
    for(size_t i=0;okay && i<graph->count;++i) {
        graph_parent *parent=graph->parents+i;
        okay=blob(&io,&parent->topology) && blob(&io,&parent->compiled) && blob(&io,&parent->wrapper) &&
            shape(parent,i<graph->decoded);
    }
    okay=okay && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    for(size_t i=0;okay && i<graph->count;++i) {
        graph_parent *parent=graph->parents+i; frontend_remote_q3_graph_resources resources;
        okay=frontend_remote_q3_graph_recipe_decode(f,parent->topology,&parent->recipe,error) &&
            frontend_remote_q3_graph_recipe_read(parent->recipe,&resources,error) && resources.decoded==(i<graph->decoded);
    }
    if(!okay) { frontend_remote_q3_graph_destroy(graph); return false; }
    *out=graph; return true;
}
static bool current(const frontend_remote_q3_graph_roster *graph)
{ return graph && graph->frontend->application==graph->application && graph->frontend->source_restoring && !graph->frontend->capture; }
bool frontend_remote_q3_graph_prepare(frontend_remote_q3_graph_roster *graph,qa_error *error)
{
    if(!current(graph) || graph->attempted) return false;
    graph->attempted=true; qa_frontend *f=graph->frontend;
    for(size_t i=0;i<graph->count;++i) {
        graph_parent *parent=graph->parents+i;
        if(i<graph->decoded) {
            frontend_network_client_domain domain; qa_application_native_q3_client_modules_recipe recipe;
            if(!frontend_network_client_restore_domain_read(f,&domain,error) ||
                !qa_application_native_q3_client_modules_recipe_read(f->application,&domain.source,domain.gamestate,&recipe,error) ||
                (recipe.pure?parent->compiled.size!=0:parent->compiled.size==0) ||
                !frontend_remote_q3_graph_prepare_resources(parent->recipe,&domain,&parent->decoded,error) ||
                !frontend_network_client_restore_adopt(f,&domain,parent->decoded,error)) return false;
            if(parent->compiled.size) {
                qa_bytes client,source;
                if(!frontend_remote_q3_graph_children_decode(parent->decoded,parent->compiled,&parent->children,error) ||
                    !frontend_remote_q3_graph_child_read(parent->children,FRONTEND_REMOTE_Q3_CHILD_CLIENT,&client,error) ||
                    !frontend_remote_q3_graph_child_read(parent->children,FRONTEND_REMOTE_Q3_CHILD_SOURCE,&source,error) ||
                    !frontend_remote_q3_services_prepare_restored(parent->decoded,client,source,error)) return false;
                frontend_remote_q3_runtime *runtime=NULL;
                if(!frontend_remote_q3_runtime_create_restored(parent->decoded,&runtime,error) ||
                    !frontend_remote_q3_runtime_callbacks_read_restored(runtime,&parent->callbacks,error)) return false;
            } else {
                frontend_remote_q3_transport *transport=NULL;
                if(!frontend_remote_q3_transport_create_restored(parent->decoded,&transport,error)) return false;
            }
        } else {
            frontend_network_client_attempt attempt; bool present=false;
            if(!frontend_network_client_restore_attempt_read(f,&attempt,&present,error) || !present ||
                !frontend_remote_q3_graph_prepare_initial(parent->recipe,&attempt,&parent->initial,error) ||
                !frontend_network_client_restore_initial_adopt(f,&attempt,parent->initial,NULL,error) ||
                !frontend_remote_q3_initial_transport_create(parent->initial,error)) return false;
        }
        parent->prefix=true;
    }
    return true;
}
bool frontend_remote_q3_graph_prepare_modules(frontend_remote_q3_graph_roster *graph,qa_error *error)
{
    if(!current(graph) || !graph->attempted) return false;
    for(size_t i=0;i<graph->count;++i) {
        graph_parent *parent=graph->parents+i;
        if(!parent->prefix || parent->modules_prepared) return false;
        if(parent->wrapper.size) {
            qa_application_q3_remote_source source; qa_bytes modules={0}; bool present=false;
            if(parent->decoded) {
                frontend_remote_q3_resources resources;
                if(!frontend_remote_q3_resources_import_read(parent->decoded,&resources,error)) return false;
                source=resources.domain.source;
            } else {
                frontend_remote_q3_initial_view resources;
                if(!frontend_remote_q3_initial_import_read(parent->initial,&resources,error)) return false;
                source=resources.attempt.source;
            }
            if(!qa_native_q3_remote_client_modules_restore_read(graph->application,&source,&modules,&present,error) || !present)
                return false;
            bool restored=parent->decoded?
                frontend_remote_q3_modules_restore_decoded(parent->decoded,parent->wrapper,modules,&parent->modules,error):
                frontend_remote_q3_modules_restore_initial(graph->frontend,parent->initial,parent->wrapper,modules,&parent->modules,error);
            if(parent->initial && parent->modules &&
                frontend_remote_q3_modules_initial_parent(parent->modules)==parent->initial) {
                frontend_remote_q3_initial_view resources;
                if(!frontend_remote_q3_initial_import_read(parent->initial,&resources,error) ||
                    !frontend_network_client_restore_initial_adopt(graph->frontend,&resources.attempt,parent->initial,parent->modules,error)) return false;
            }
            if(!restored) return false;
        }
        parent->modules_prepared=true;
    }
    return true;
}
bool frontend_remote_q3_graph_prepare_runtime(frontend_remote_q3_graph_roster *graph,qa_error *error)
{
    if(!current(graph)) return false;
    for(size_t i=0;i<graph->count;++i) {
        graph_parent *parent=graph->parents+i;
        if(!parent->prefix || parent->runtime_prepared) return false;
        if(parent->compiled.size && !frontend_remote_q3_runtime_prepare_import(
            frontend_remote_q3_runtime_read(parent->decoded),error)) return false;
        parent->runtime_prepared=true;
    }
    return true;
}
bool frontend_remote_q3_graph_restore_children(frontend_remote_q3_graph_roster *graph,
    const qa_audio_checkpoint_refs *audio,qa_error *error)
{
    if(!current(graph)) return false;
    for(size_t i=0;i<graph->count;++i) {
        graph_parent *parent=graph->parents+i;
        if(!parent->runtime_prepared || parent->children_restored) return false;
        if(parent->children) {
            qa_bytes media,clients,runtime; q3n_client_refs refs;
            if(!frontend_remote_q3_graph_child_read(parent->children,FRONTEND_REMOTE_Q3_CHILD_MEDIA,&media,error) ||
                !frontend_remote_q3_graph_child_read(parent->children,FRONTEND_REMOTE_Q3_CHILD_CLIENTS,&clients,error) ||
                !frontend_remote_q3_graph_child_read(parent->children,FRONTEND_REMOTE_Q3_CHILD_RUNTIME,&runtime,error) ||
                !frontend_remote_q3_graph_client_refs(parent->children,&refs,error)) return false;
            frontend_remote_q3_resources resources;
            if(!frontend_remote_q3_resources_import_read(parent->decoded,&resources,error) ||
                !qa_q3_assets_capture_begin(resources.assets,error)) return false;
            bool okay=frontend_remote_q3_services_finish_restore(parent->decoded,media,&refs,clients,error) &&
                frontend_remote_q3_runtime_restore(frontend_remote_q3_runtime_read(parent->decoded),runtime,audio,error);
            qa_q3_assets_capture_end(resources.assets);
            if(!okay) return false;
        }
        parent->children_restored=true;
    }
    return true;
}
bool frontend_remote_q3_graph_restore_modules(frontend_remote_q3_graph_roster *graph,
    const frontend_remote_q3_modules_save_refs *refs,qa_error *error)
{
    if(!current(graph) || !refs || !refs->movies) return false;
    for(size_t i=0;i<graph->count;++i) {
        graph_parent *parent=graph->parents+i;
        if(!parent->modules_prepared || parent->modules_restored) return false;
        if(parent->modules && !frontend_remote_q3_modules_restore_continuation(parent->modules,refs,error)) return false;
        parent->modules_restored=true;
    }
    return true;
}
bool frontend_remote_q3_graph_restore_frames(frontend_remote_q3_graph_roster *graph,qa_error *error)
{
    if(!current(graph)) return false;
    for(size_t i=0;i<graph->count;++i) {
        graph_parent *parent=graph->parents+i;
        if(!parent->children_restored) return false;
        if(!parent->children) continue;
        qa_bytes frame; frontend_remote_q3_frame *frames=NULL;
        frontend_remote_q3_runtime *runtime=frontend_remote_q3_runtime_read(parent->decoded);
        if(!frontend_remote_q3_graph_child_read(parent->children,FRONTEND_REMOTE_Q3_CHILD_FRAME,&frame,error) ||
            !frontend_remote_q3_frame_prepare_restored(parent->decoded,&parent->callbacks,frame,&frames,error) ||
            !frontend_network_client_predictor_finish_restore(graph->frontend,error)) return false;
        frontend_remote_prediction *predictor=NULL; bool present=false;
        frontend_remote_prediction_source source; bool source_present=false;
        if(!frontend_network_client_predictor_read(graph->frontend,&predictor,&present,error) ||
            (present && !frontend_remote_prediction_source_read(predictor,&source,&source_present,error)) ||
            (present && !source_present) ||
            !frontend_remote_q3_frame_finish_restore(frames,predictor,present?&source:NULL,error) ||
            !frontend_remote_q3_runtime_bind_frames(runtime,frames,error) ||
            !frontend_remote_q3_runtime_music_restore_bind(runtime,error)) return false;
    }
    return true;
}
bool frontend_remote_q3_graph_finish_modules(frontend_remote_q3_graph_roster *graph,
    const frontend_remote_q3_modules_save_refs *refs,qa_error *error)
{
    if(!current(graph) || !refs || !refs->movies) return false;
    for(size_t i=0;i<graph->count;++i) {
        graph_parent *parent=graph->parents+i;
        if(!parent->modules_restored || !parent->children_restored) return false;
        if(!parent->modules) continue;
        if(!frontend_remote_q3_modules_finish_restore(parent->modules,refs,error)) return false;
        qa_application_q3_remote_source source;
        if(parent->decoded) {
            frontend_remote_q3_resources resources;
            if(!frontend_remote_q3_resources_import_read(parent->decoded,&resources,error)) return false;
            source=resources.domain.source;
        } else {
            frontend_remote_q3_initial_view resources;
            if(!frontend_remote_q3_initial_import_read(parent->initial,&resources,error)) return false;
            source=resources.attempt.source;
        }
        if(!qa_native_q3_remote_client_modules_restore_complete(graph->application,&source,error)) return false;
    }
    return true;
}
bool frontend_remote_q3_graph_finish_sources(frontend_remote_q3_graph_roster *graph,qa_error *error)
{
    if(!current(graph)) return false;
    for(size_t i=0;i<graph->count;++i)
        if(!graph->parents[i].children_restored || !graph->parents[i].modules_restored) return false;
    if(graph->decoded && !frontend_network_client_restore_finish(graph->frontend,error)) return false;
    for(size_t i=0;i<graph->decoded;++i)
        if(!frontend_remote_q3_resources_finish_import(graph->parents[i].decoded,error)) return false;
    return !graph->initial || frontend_network_client_restore_initial_finish(graph->frontend,error);
}
