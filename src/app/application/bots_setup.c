#include "bots_setup.h"
#include "map_players_private.h"

static bool live_shared_source(qa_application *app,application_provider **out)
{
    application_provider *source=app?application_world_provider(app,QA_ROLE_ENTITIES,""):NULL;
    if(!source || (source->kind!=APPLICATION_PROVIDER_Q1 && source->kind!=APPLICATION_PROVIDER_Q2) ||
       !source->constructed || !source->attached || !source->map_bound || source->close_pending)
        return false;
    *out=source;return true;
}

bool application_bots_requested(qa_application *app,qa_error *error)
{
    if(!app || app->destroy_requested || app->state==QA_APPLICATION_FAULTED ||
       app->state==QA_APPLICATION_STOPPING || !app->map_resource || !app->players)
        return application_fail(error,QA_ERROR_ARGUMENT,"Requested bots require the actual live application map");
    if(app->bots && app->bots->population) return true;
    application_provider *source;
    if(!live_shared_source(app,&source) || source!=app->players->map_provider)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"Requested shared bots require an actual native Q1 or Q2 source");
    if(app->bots && (app->bots->source!=source || app->bots->restoring || app->bots->producing || app->bots->calls))
        return application_fail(error,QA_ERROR_ARGUMENT,"Requested bots require their idle actual source owner");
    const qa_launch_snapshot *snapshot=qa_application_launch(app);
    const qa_launch_choices *choices=snapshot?qa_launch_snapshot_choices(snapshot):NULL;
    qa_bsp_view geometry;
    if(!choices) return application_fail(error,QA_ERROR_NOT_FOUND,"Requested bots have no published launch choices");
    if(!qa_bsp_open(qa_resource_bytes(app->map_resource),&geometry,error) ||
       !application_bots_prepare_requested(app,choices,&geometry,error)) return false;
    return application_bots_source_initialize(app->bots,error);
}

bool application_bots_frame_request(qa_application *app,const qa_source_frame *frames,size_t count,
    uint64_t host_ns,qa_error *error)
{
    if(!app || (!frames && count))
        return application_fail(error,QA_ERROR_ARGUMENT,"Bot minimum-player request has no actual frame input");
    if(app->bots && app->bots->population) return true;
    application_provider *source;
    if(!live_shared_source(app,&source)) return true;
    const qa_cvar_view *minimum=qa_cvars_find(app->cvars,"bot_minplayers");
    if(!minimum || !(minimum->number>0)) return true;
    const qa_source_frame *frame=NULL;
    for(size_t i=0;i<count;++i) if(frames[i].provider==source->owner) {frame=frames+i;break;}
    if(!frame) return true;
    qa_source_frame actual;uint64_t actual_host;
    if(!qa_session_active_frame(app->session,source->owner,&actual) ||
       !qa_session_frame_host_time(app->session,&actual_host) || actual_host!=host_ns ||
       actual.kind!=frame->kind || actual.number!=frame->number || actual.start_ns!=frame->start_ns ||
       actual.elapsed_ns!=frame->elapsed_ns || actual.time_ns!=frame->time_ns)
        return application_fail(error,QA_ERROR_ARGUMENT,"Bot minimum-player request requires its genuine source frame");
    return application_bots_requested(app,error);
}
