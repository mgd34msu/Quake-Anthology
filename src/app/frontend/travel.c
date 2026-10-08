#include "internal.h"
#include "capture.h"
#include "campaign_cinematic.h"
#include "save_commands.h"
#include "qc_messages.h"
#include "qa/scene_world_save.h"
#include "qa/application_startup_prepare.h"

bool frontend_travel(qa_frontend *frontend, qa_error *error)
{
    qa_application_travel_view travel;
    if (qa_application_should_stop(frontend->application)) return true;
    if (frontend_startup_queued(frontend) || qa_application_startup_pending(frontend->application)) return true;
    if (frontend->stepping || !frontend_owners_idle(frontend) ||
        qa_application_events_local_first(frontend->application) < qa_application_events_next(frontend->application) ||
        (frontend->scene_world && !qa_scene_world_idle(frontend->scene_world)))
        return true;
    uint64_t completed;
    if (qa_application_travel_publication_read(frontend->application,&completed))
        return (!frontend->qc_messages || frontend_qc_messages_drain(frontend->qc_messages,error)) &&
            qa_application_finish_travel_publication(frontend->application,completed,error) &&
            frontend_save_commands_autosave(frontend,error);
    qa_application_map_view map;
    if (qa_application_map_read(frontend->application,&map) &&
        !qa_application_prepare_match_travel(frontend->application, error)) return false;
    /* Source setters may publish fresh events after the earlier frame drain.
     * Their current world must stay alive until those projections finish. */
    if (qa_application_events_local_first(frontend->application) < qa_application_events_next(frontend->application))
        return true;
    if (!qa_application_travel_read(frontend->application, &travel))
        return frontend_save_commands_autosave(frontend,error);
    if (travel.target.kind==QA_TRAVEL_CINEMATIC || travel.target.kind==QA_TRAVEL_PICTURE)
        return frontend_cinematic_travel(frontend,&travel,error);
    if (travel.target.kind!=QA_TRAVEL_MAP) return true;
    if (!frontend_source_rebind_ready(frontend, frontend, error) ||
        !frontend_world_change_ready(frontend, frontend->application, error)) return false;
    bool handled=false;
    if (!frontend_save_commands_campaign(frontend,travel.revision,&handled,error)) return false;
    if (handled) return true;
    if (!qa_application_commit_travel(frontend->application,travel.revision,error)) return false;
    if (qa_application_startup_pending(frontend->application)) return true;
    return (!frontend->qc_messages || frontend_qc_messages_drain(frontend->qc_messages,error)) &&
        (!qa_application_travel_publication_read(frontend->application,&completed) ||
        qa_application_finish_travel_publication(frontend->application,completed,error)) &&
        frontend_save_commands_autosave(frontend,error);
}
