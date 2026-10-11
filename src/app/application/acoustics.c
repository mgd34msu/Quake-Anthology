#include "internal.h"
#include "qa/application_acoustics.h"
#include <stdlib.h>

struct qa_application_acoustics {
    qa_application_acoustics_view view;
    qa_application_acoustics *next, *previous;
    unsigned readers;
};
bool application_acoustics_idle(const qa_application *app)
{ return app && !app->acoustics; }
bool qa_application_acoustics_current(const qa_application_acoustics *owner)
{
    const qa_application_acoustics_view *v=owner?&owner->view:NULL;
    qa_application *app=v?v->application:NULL;
    if (!app || app->destroy_requested || app->finalizing || app->world!=v->world ||
        app->geometry!=v->geometry || app->map_resource!=v->map_resource ||
        app->map_revision!=v->map_revision) return false;
    bool listed=false;
    for (const qa_application_acoustics *row=app->acoustics;row;row=row->next)
        if (row==owner) { listed=true; break; }
    return listed && (v->world ? v->geometry && v->map_resource &&
        qa_world_geometry(v->world)==v->geometry &&
        qa_world_actors(v->world)==qa_session_actor_registry(app->session) :
        !v->geometry && !v->map_resource);
}
bool qa_application_acoustics_hold(qa_application *app,
    qa_application_acoustics **out,qa_error *error)
{
    if (!app || !out || *out || app->destroy_requested || app->finalizing ||
        (app->world ? !app->geometry || !app->map_resource ||
            qa_world_geometry(app->world)!=app->geometry ||
            qa_world_actors(app->world)!=qa_session_actor_registry(app->session) :
            app->geometry || app->map_resource))
        return application_fail(error,QA_ERROR_ARGUMENT,"Acoustics requires its actual shared world parent");
    qa_application_acoustics *owner=calloc(1,sizeof(*owner));
    if (!owner) return application_fail(error,QA_ERROR_MEMORY,"Retaining shared acoustic world");
    owner->view=(qa_application_acoustics_view){app,app->world,app->geometry,
        app->map_resource,app->map_revision};
    qa_resource_retain(app->map_resource);
    owner->next=app->acoustics;
    if (owner->next) owner->next->previous=owner;
    app->acoustics=owner;
    *out=owner; return true;
}
bool qa_application_acoustics_read(const qa_application_acoustics *owner,
    qa_application_acoustics_view *out,qa_error *error)
{
    if (!out || !qa_application_acoustics_current(owner))
        return application_fail(error,QA_ERROR_ARGUMENT,"Shared acoustic world receipt is no longer current");
    *out=owner->view; return true;
}
bool qa_application_acoustics_trace(qa_application_acoustics *owner,qa_actor_id pass,
    qa_vec3 start,qa_vec3 end,qa_audio_trace_hit *out,qa_error *error)
{
    if (!out || !qa_vec_finite(start) || !qa_vec_finite(end) ||
        !qa_application_acoustics_current(owner) || !owner->view.world || owner->readers ||
        (pass.registry && !qa_actors_get(qa_world_actors(owner->view.world),pass)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Acoustic trace requires its live scene and full listener actor");
    qa_trace_query query={.start=start,.end=end,.shape={.kind=QA_SHAPE_POINT},
        .policy={.behavior = &qa_trace_behaviors[(true) ? QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC],.contents_mask=qa_collision_contents_mask(3, QA_GAME_Q2)},
        .pass_actor=pass};
    owner->readers++;
    qa_trace_result result;
    bool ok=qa_world_trace(owner->view.world,&query,&result,error);
    if (ok && !qa_application_acoustics_current(owner))
        ok=application_fail(error,QA_ERROR_ARGUMENT,"Acoustic world changed during linked actor trace");
    owner->readers--;
    if (ok) *out=(qa_audio_trace_hit){.fraction=result.fraction,
        .start_solid=result.start_solid,.all_solid=result.all_solid,.end=result.end,
            .material=result.has_surface ? result.surface.material_lower_id : QA_STRING_NONE};
    return ok;
}
void qa_application_acoustics_release(qa_application_acoustics *owner)
{
    if (!owner || owner->readers) return;
    qa_application *app=owner->view.application;
    if (owner->previous) owner->previous->next=owner->next;
    else app->acoustics=owner->next;
    if (owner->next) owner->next->previous=owner->previous;
    qa_resource_release((qa_resource *)owner->view.map_resource);
    free(owner);
}
