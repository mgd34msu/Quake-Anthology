#include "guest_q3_components_private.h"

bool application_q3_components_command(qa_application *app,qa_actor_id actor,
    const qa_unified_component_owner *identity,const qa_command_invocation *command,bool *handled,qa_error *e)
{
    application_q3_components *owner=app?app->components:NULL;
    if(!owner||owner->closing||!identity||!identity->provider||!identity->generation||!command||!handled||!qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component command requires its actual Source actor and roster");
    *handled=false;
    size_t provider_length=strlen(identity->provider);
    component_game_row *selected=NULL;
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        qa_bytes name=qa_strings_text(qa_session_strings(app->session),row->publication.owner);
        if(name.size==provider_length&&!memcmp(name.data,identity->provider,name.size)&&row->publication.generation==identity->generation) selected=row;
    }
    if(!selected||!selected->attached||!selected->initialized||!q3components_current(selected))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component command names no genuine current executor");
    return application_q3_component_command(selected->publication.game,actor,command,handled,e);
}
