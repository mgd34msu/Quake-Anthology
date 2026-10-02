#include "guest_q3_components_private.h"

bool application_q3_components_command(qa_application *app,qa_actor_id actor,
    const qa_unified_document *identity,uint64_t generation,const qa_command_invocation *command,bool *handled,qa_error *e)
{
    application_q3_components *owner=app?app->components:NULL;
    if(!owner||owner->closing||!identity||!command||!handled||!qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component command requires its actual Source actor and roster");
    *handled=false;
    const qa_json_document *document=qa_unified_document_json(identity);
    qa_json_id root=qa_unified_document_root(identity); uint64_t declared_generation;
    if(!qa_json_u64(document,qa_json_get(document,root,"generation"),&declared_generation,e)||declared_generation!=generation)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component command changed its actual publication generation");
    qa_buffer provider={0};
    if(!qa_json_string(document,qa_json_get(document,root,"provider"),&provider,e)) return false;
    component_game_row *selected=NULL;
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        qa_bytes name=qa_strings_text(qa_session_strings(app->session),row->publication.owner);
        if(name.size==provider.size&&!memcmp(name.data,provider.data,name.size)&&row->publication.generation==generation) selected=row;
    }
    qa_buffer_free(&provider);
    if(!selected||!selected->attached||!selected->initialized||!q3components_current(selected))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component command names no genuine current executor");
    return application_q3_component_command(selected->publication.game,actor,command,handled,e);
}
