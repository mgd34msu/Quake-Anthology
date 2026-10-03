#include "q2_client_graph_save.h"
#include "network_q2_recipe_save.h"
#include "save_private.h"
#include <stdlib.h>
#include <string.h>

typedef struct q2_descriptor {
    uint64_t catalog,view;
    qa_launch_restored_instance saved;
    qa_launch_q2_client_metadata request;
    bool owned;
} q2_descriptor;
struct frontend_q2_client_graph {
    qa_frontend *frontend;
    frontend_network_q2_client_state state;
    q2_descriptor descriptors[2];
    qa_bytes recipe;
    bool present,distinct,decoded,staged,prepared;
};
static bool text(qa_source_save_io *io,const char **value)
{
    char *owned=(char *)*value;
    bool okay=frontend_save_text(io,&owned);
    if(io->direction==QA_SOURCE_SAVE_READ) *value=owned;
    return okay;
}
static bool span(qa_source_save_io *io,qa_bytes *value)
{
    size_t size=value->size;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)value->data,size);
    if(io->offset>io->input.size || size>io->input.size-io->offset) return false;
    *value=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
static bool buffer(qa_source_save_io *io,qa_buffer *value)
{
    qa_bytes bytes={value->data,value->size};
    if(!span(io,&bytes)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        value->data=bytes.size?malloc(bytes.size):NULL; value->size=bytes.size;
        if(bytes.size && !value->data) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining Q2 CLIENT graph continuation");
        if(bytes.size) memcpy(value->data,bytes.data,bytes.size);
    }
    return true;
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
static bool descriptor_fields(qa_source_save_io *io,q2_descriptor *row)
{
    qa_launch_provider *s=&row->saved.selection;
    uint32_t runtime=s->runtime;
    if(!qa_source_save_u64(io,&row->catalog) || !row->catalog ||
        !qa_source_save_u64(io,&row->view) || !row->view ||
        !qa_source_save_u32(io,&row->request.profile) || !row->request.profile ||
        !qa_source_save_u32(io,&row->request.selected) || !row->request.selected ||
        !qa_source_save_u32(io,&row->request.seat) ||
        !text(io,&s->instance) || !s->instance || !*s->instance ||
        !qa_source_save_u32(io,&s->product) || s->product!=row->request.selected ||
        !qa_source_save_u32(io,&runtime) || runtime!=QA_PROGRAM_BUILTIN ||
        !text(io,&s->implementation) || !s->implementation || !*s->implementation ||
        !text(io,&s->artifact) || !s->artifact || *s->artifact ||
        !text(io,&s->component) || !s->component || *s->component ||
        !clock_fields(io,&s->clock) || !span(io,&s->options) ||
        !qa_source_save_bytes(io,row->saved.identity.bytes,sizeof(row->saved.identity.bytes))) return false;
    s->runtime=(qa_program_kind)runtime; row->request.instance=s->instance; return true;
}
static bool command_fields(qa_source_save_io *io,qa_command_context *command)
{
    uint32_t dialect=command->dialect,origin=command->origin;
    if(!qa_source_save_u64(io,&command->session) || !qa_source_save_u64(io,&command->owner) ||
        !qa_source_save_u64(io,&command->client) || !qa_source_save_u32(io,&command->seat) ||
        !qa_source_save_u32(io,&dialect) || dialect>QA_CONSOLE_Q3 ||
        !qa_source_save_u32(io,&origin) || origin>QA_COMMAND_REMOTE ||
        !qa_source_save_bool(io,&command->direct) || !qa_source_save_bool(io,&command->console_text) ||
        !qa_source_save_u64(io,&command->registry) || !command->registry ||
        !qa_source_save_u64(io,&command->generation) || !command->generation || command->script ||
        !qa_source_save_actor(io,&command->actor)) return false;
    command->dialect=(qa_console_dialect)dialect; command->origin=(qa_command_origin)origin; return true;
}
static bool application_fields(qa_source_save_io *io,qa_application_client_state *state)
{
    if(!qa_source_save_bytes(io,state->descriptor_identity.bytes,sizeof(state->descriptor_identity.bytes)) ||
        !qa_source_save_string(io,&state->receiver) || !state->receiver ||
        !qa_source_save_string(io,&state->entity_owner) || !state->entity_owner ||
        !qa_source_save_u32(io,&state->seat) || !qa_source_save_u32(io,&state->physical_seat) ||
        !qa_source_save_u64(io,&state->configuration_generation) || !qa_source_save_u64(io,&state->connection_epoch) ||
        !qa_source_save_u64(io,&state->entity_generation) ||
        !qa_source_save_u64(io,&state->client.owner) || !qa_source_save_u64(io,&state->client.generation) ||
        !qa_source_save_u32(io,&state->client.slot) || !qa_source_save_u64(io,&state->network_seat.owner) ||
        !qa_source_save_u32(io,&state->network_seat.index) ||
        !qa_source_save_count(io,&state->actor_count,qa_actors_capacity(qa_session_actors(io->session)))) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && state->actor_count) {
        state->actors=calloc(state->actor_count,sizeof(*state->actors));
        if(!state->actors) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining Q2 CLIENT observer references");
    }
    for(size_t i=0;i<state->actor_count;++i)
        if(!qa_source_save_u64(io,&state->actors[i].generation) || !qa_source_save_u32(io,&state->actors[i].slot)) return false;
    return true;
}
static bool fields(qa_source_save_io *io,frontend_q2_client_graph *graph)
{
    uint8_t magic[4]={'Q','F','2','C'}; frontend_remote_q2_domain *domain=&graph->state.source.domain;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QF2C",4) ||
        !qa_source_save_bool(io,&graph->present)) return false;
    if(!graph->present) return true;
    if(!span(io,&graph->recipe) || !graph->recipe.size || !descriptor_fields(io,graph->descriptors) ||
        !qa_source_save_bool(io,&graph->distinct) ||
        (graph->distinct && !descriptor_fields(io,graph->descriptors+1)) ||
        !application_fields(io,&graph->state.application) ||
        !qa_source_save_bool(io,&graph->state.source.receiver_present) ||
        !qa_source_save_bool(io,&graph->state.source.configuration_complete) ||
        !qa_source_save_bool(io,&graph->state.source.retiring) ||
        !qa_source_save_bool(io,&graph->state.source.template_retired) ||
        !qa_source_save_bool(io,&graph->state.source.release_programmes) ||
        !command_fields(io,&domain->command_context) ||
        !qa_source_save_u64(io,&domain->configuration_generation) ||
        !buffer(io,&graph->state.source.console) || !graph->state.source.console.size ||
        !buffer(io,&graph->state.receiver) ||
        (graph->state.source.receiver_present!=(graph->state.receiver.size!=0)) ||
        (!graph->state.source.receiver_present && graph->distinct) ||
        !buffer(io,&graph->state.bootstrap) || !graph->state.bootstrap.size) return false;
    return true;
}
static bool descriptor_capture(q2_descriptor *row,const qa_launch_instance *value,
    const qa_launch_q2_client_metadata *request,qa_application_content_graph *content)
{
    if(!value || !request || !request->catalog || value->artifact || value->artifact_acquisition ||
        value->declaration || value->interface_count || value->behavior_count ||
        qa_launch_instance_catalog(value)!=request->catalog || request->prepared!=value->content ||
        !request->instance || strcmp(request->instance,value->selection.instance)) return false;
    row->catalog=qa_application_content_catalog_id(content,request->catalog);
    row->view=qa_application_content_view_id(content,value->content); row->request=*request;
    row->saved=(qa_launch_restored_instance){.catalog=request->catalog,.content=value->content,
        .selection=value->selection,.identity=value->identity};
    return row->catalog && row->view;
}
bool frontend_q2_client_graph_checkpoint(qa_frontend *f,const frontend_remote_q2_restore_refs *refs,
    qa_buffer *out,qa_error *error)
{
    frontend_q2_client_graph graph={.frontend=f}; qa_buffer recipe={0}; qa_source_save_io io={0};
    bool okay=f && f->capture && refs && refs->content && out && !out->data && !out->size &&
        frontend_network_q2_checkpoint(f,refs,&graph.present,&graph.state,error) &&
        frontend_remote_q2_count(f)==(graph.state.source.receiver_present?1u:0u);
    if(okay && graph.present) {
        graph.distinct=graph.state.source.constructor->storage!=graph.state.source.selected->storage;
        okay=descriptor_capture(graph.descriptors,graph.state.source.constructor,&graph.state.source.constructor_request,refs->content) &&
            (!graph.distinct || descriptor_capture(graph.descriptors+1,graph.state.source.selected,
                &graph.state.source.selected_request,refs->content)) &&
            frontend_network_q2_recipe_checkpoint(&graph.state,&recipe,error);
        graph.recipe=(qa_bytes){recipe.data,recipe.size};
    }
    okay=okay && qa_source_save_writer(&io,qa_application_session(f->application),error) &&
        fields(&io,&graph) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&recipe); frontend_network_q2_client_state_free(&graph.state); return okay;
}
bool frontend_q2_client_graph_decode(qa_frontend *f,qa_bytes bytes,frontend_q2_client_graph **out,qa_error *error)
{
    if(!f || !f->source_restoring || !out || *out) return false;
    frontend_q2_client_graph *graph=calloc(1,sizeof(*graph));
    if(!graph) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining Q2 CLIENT graph prefix");
    graph->frontend=f; *out=graph; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,qa_application_session(f->application),bytes,error) &&
        fields(&io,graph) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(okay && graph->present) {
        frontend_network_q2_client_state recipe={0};
        okay=frontend_network_q2_recipe_restore(graph->recipe,&recipe,error);
        graph->state.remote=recipe.remote; graph->state.protocol=recipe.protocol; graph->state.qport=recipe.qport;
        graph->state.physical_seat=recipe.physical_seat; graph->state.policy=recipe.policy;
        graph->state.negotiated=recipe.negotiated; graph->state.composition=recipe.composition;
        graph->state.retired=recipe.retired;
        qa_application_content_graph *content=qa_application_content_graph_read(f->application);
        for(size_t i=0;okay && i<(graph->distinct?2u:1u);++i) {
            q2_descriptor *row=graph->descriptors+i;
            row->saved.catalog=qa_application_content_catalog(content,row->catalog);
            okay=row->saved.catalog && qa_application_content_retain_view(content,row->view,&row->saved.content,error);
            row->owned=row->saved.content!=NULL; row->request.catalog=row->saved.catalog;
            row->request.prepared=row->saved.content;
        }
    }
    graph->decoded=okay; return okay;
}
static void descriptor_transfer(void *context,bool selected)
{
    frontend_q2_client_graph *graph=context;
    graph->descriptors[selected?1:0].owned=false;
}
bool frontend_q2_client_graph_stage(frontend_q2_client_graph *graph,const frontend_remote_q2_restore_refs *refs,
    const qa_console_save_resolvers *console,qa_error *error)
{
    if(!graph || !graph->decoded || graph->staged || !refs || !console || !console->command_context) return false;
    graph->staged=true; if(!graph->present) return true;
    frontend_remote_q2_domain domain=graph->state.source.domain;
    const qa_application_client_state *app=&graph->state.application;
    domain.application=graph->frontend->application; domain.catalog=graph->descriptors[0].saved.catalog;
    domain.product=graph->descriptors[0].request.profile; domain.protocol=graph->state.protocol;
    domain.physical_seat=graph->state.physical_seat; domain.client=app->client;
    domain.seat=app->network_seat; domain.epoch=app->connection_epoch;
    qa_command_context remapped;
    if(!console->command_context(console->context,domain.command_context.registry,&domain.command_context,&remapped,error)) return false;
    domain.command_context=remapped;
    frontend_network_q2_client_restore saved={.application=app,.policy=graph->state.policy,
        .negotiated=graph->state.negotiated,.composition=graph->state.composition,
        .bootstrap={graph->state.bootstrap.data,graph->state.bootstrap.size},.retired=graph->state.retired,
        .source={.constructor_request=graph->descriptors[0].request,
            .selected_request=graph->distinct?graph->descriptors[1].request:graph->descriptors[0].request,
            .constructor=&graph->descriptors[0].saved,.selected=graph->distinct?&graph->descriptors[1].saved:NULL,
            .domain=domain,.console={graph->state.source.console.data,graph->state.source.console.size},
            .receiver={graph->state.receiver.data,graph->state.receiver.size},
            .receiver_present=graph->state.source.receiver_present,
            .configuration_complete=graph->state.source.configuration_complete,
            .retiring=graph->state.source.retiring,.template_retired=graph->state.source.template_retired,
            .release_programmes=graph->state.source.release_programmes,
            .console_resolvers=console,.receiver_refs=refs,.descriptor_context=graph,.descriptor_transfer=descriptor_transfer}};
    frontend_network_q2_client_recipe recipe={.remote=graph->state.remote,.protocol=graph->state.protocol,
        .qport=graph->state.qport,.physical_seat=graph->state.physical_seat};
    return frontend_network_q2_stage_recipe(graph->frontend,&recipe,&saved,error);
}
bool frontend_q2_client_graph_prepare(frontend_q2_client_graph *graph,qa_error *error)
{
    if(!graph || !graph->staged || graph->prepared ||
        frontend_remote_q2_count(graph->frontend)!=(graph->state.source.receiver_present?1u:0u))
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 graph differs from its actual physical receiver roster");
    if(graph->present) {
        frontend_remote_q2_source_view view;
        if(!frontend_network_q2_import_read(graph->frontend,&view,error) ||
            (view.receiver!=NULL)!=graph->state.source.receiver_present || graph->descriptors[0].owned ||
            (graph->distinct && graph->descriptors[1].owned)) return false;
    }
    graph->prepared=true; return true;
}
bool frontend_q2_client_graph_roots(frontend_q2_client_graph *graph,const frontend_remote_q2_restore_refs *refs,qa_error *error)
{
    return graph && graph->prepared && refs && (!graph->state.source.receiver_present ||
        frontend_remote_q2_roots_attach_restored(frontend_remote_q2_at(graph->frontend,0),refs,error));
}
bool frontend_q2_client_graph_finish(frontend_q2_client_graph *graph,const frontend_remote_q2_restore_refs *refs,qa_error *error)
{
    return graph && graph->prepared && refs && (!graph->present || frontend_network_q2_finish_import(graph->frontend,refs,error));
}
void frontend_q2_client_graph_destroy(frontend_q2_client_graph *graph)
{
    if(!graph) return;
    for(size_t i=0;i<2;++i) {
        q2_descriptor *row=graph->descriptors+i;
        if(row->owned) qa_vfs_destroy(row->saved.content);
        free((char *)row->saved.selection.instance); free((char *)row->saved.selection.implementation);
        free((char *)row->saved.selection.artifact); free((char *)row->saved.selection.component);
    }
    frontend_network_q2_client_state_free(&graph->state); free(graph);
}
