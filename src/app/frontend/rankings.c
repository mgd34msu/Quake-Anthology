#include "internal.h"
#include "rankings.h"
#include "qa/application_players.h"
#include "qa/application_ui_names.h"

bool frontend_ranking_event(qa_frontend *frontend, const qa_builtin_event *event, qa_error *error)
{
    const qa_application_ui_names *names=qa_application_ui_names_read(frontend->application);
    if (event->kind!=QA_BUILTIN_EFFECT || event->resource!=names->ranking_menu ||
        frontend->options.dedicated || event->frame<0) return true;
    uint32_t launch_seat,index;
    if (!qa_application_player_seat(frontend->application,event->actor,&launch_seat) ||
        !frontend_seat_ordinal_read(frontend,launch_seat,&index) ||
        frontend_network_local_input_owned(frontend,index)) return true;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(frontend->application));
    if(!frontend_local_seat_present(choices,launch_seat) || !frontend->seats ||
        !frontend->seats[index].ui || !frontend->seats[index].rankings) return true;
    frontend_seat *seat=frontend->seats+index;
    return qa_ui_rankings_set_slot(seat->rankings,event->frame,error) &&
        qa_ui_open(seat->ui,FRONTEND_RANKINGS,(double)frontend->time_ns/1000000,error);
}
