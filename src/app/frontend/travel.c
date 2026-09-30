#include "internal.h"
#include "qa/scene_world_save.h"

bool frontend_travel(qa_frontend *frontend, qa_error *error)
{
    qa_application_travel_view travel;
    if (qa_application_should_stop(frontend->application)) return true;
    if (frontend->stepping || qa_application_event_count(frontend->application) ||
        qa_application_q2_map_event_count(frontend->application) || qa_application_q3_map_event_count(frontend->application) ||
        qa_application_q2_player_event_count(frontend->application) || qa_application_protocol_event_count(frontend->application) ||
        (frontend->scene_world && !qa_scene_world_idle(frontend->scene_world)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "map travel requires drained frontend event and scene owners");
    if (!qa_application_prepare_match_travel(frontend->application, error)) return false;
    /* Source setters may publish fresh events after the earlier frame drain.
     * Their current world must stay alive until those projections finish. */
    if (qa_application_event_count(frontend->application) ||
        qa_application_q2_map_event_count(frontend->application) || qa_application_q3_map_event_count(frontend->application) ||
        qa_application_q2_player_event_count(frontend->application) || qa_application_protocol_event_count(frontend->application))
        return true;
    if (!qa_application_travel_read(frontend->application, &travel) || travel.target.kind != QA_TRAVEL_MAP) return true;
    if (!frontend_source_rebind_ready(frontend, frontend, error) ||
        !frontend_world_change_ready(frontend, frontend->application, error)) return false;
    return qa_application_commit_travel(frontend->application, travel.revision, error) &&
           qa_application_finish_match_travel(frontend->application, travel.revision, error);
}
