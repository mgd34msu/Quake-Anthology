#include "internal.h"
#include "match_intents.h"
#include "rankings.h"
#include "qa/application_startup_prepare.h"

bool qa_application_prepare_frame(qa_application *app, qa_error *error)
{
    qa_application_travel_view travel;
    bool pending_map = qa_application_travel_read(app, &travel) &&
        travel.target.kind == QA_TRAVEL_MAP;
    if (!app || app->operation != APPLICATION_IDLE || app->frame_preparing ||
        qa_application_startup_pending(app) || pending_map ||
        app->q3_round_active || app->publication_started || app->destroy_requested ||
        app->finalizing || app->pending_close || app->routing_snapshot ||
        app->routing_providers || app->routing_provider_count ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING) ||
        !app->session || !qa_session_safe(app->session) ||
        !qa_session_destroy_ready(app->session) || !app->world ||
        !qa_world_idle(app->world) || !qa_combat_idle(app->combat) ||
        !qa_console_idle(app->console) || !application_guests_idle(app) ||
        !application_bots_can_destroy(app) || !application_rankings_idle(app) ||
        (app->modes && !qa_modes_idle(app->modes)) ||
        (app->equipment && !qa_equipment_idle(app->equipment)) ||
        !application_match_intents_idle(app->match_intents))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "frame preparation requires its actual idle driver boundary");

    if (!app->match_intents) return true;
    app->frame_preparing = true;
    bool consumed = false;
    bool okay = application_match_intents_prepare(app->match_intents, app, &consumed, error);
    app->frame_preparing = false;
    return okay;
}
