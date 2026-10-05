#include "campaign_menu.h"
#include "campaign_ui.h"
#include "ui_features_private.h"
#include "qa/text.h"
#include "qa/ui_library.h"
#include <stdio.h>

static double now(frontend_seat *seat) { return (double)seat->frontend->time_ns/1000000.; }
static bool open(frontend_seat *seat,qa_ui_id id,qa_error *error)
{
    return qa_ui_open(seat->ui,id,now(seat),error);
}
static bool reserve(frontend_ui_seat_features *state,size_t count,qa_error *error)
{
    if (count<=state->campaign_capacity) return true;
    if (count>SIZE_MAX/sizeof(qa_ui_row) || count>SIZE_MAX/512)
        return frontend_fail(error,QA_ERROR_MEMORY,"Arena menu rows exceed memory extent");
    qa_ui_row *rows=calloc(count,sizeof(*rows)); char *labels=calloc(count,512);
    if (!rows || !labels) { free(rows); free(labels); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual arena menu rows"); }
    free(state->campaign_rows); free(state->campaign_labels);
    state->campaign_rows=rows; state->campaign_labels=labels; state->campaign_capacity=count; return true;
}
static bool read(frontend_seat *seat,frontend_campaign_ui_view *view,qa_error *error)
{
    return frontend_campaign_ui_read(seat->frontend,seat->id,view,error) &&
        (view->installed || frontend_fail(error,QA_ERROR_ARGUMENT,"No campaign is installed for this actual local recipient"));
}
static bool identity(qa_frontend *f,qa_actor_owner owner,const char *text)
{
    const char *actual=qa_application_provider_instance(f->application,owner);
    return actual && text && !strcmp(actual,text);
}
static bool copy_identity(qa_frontend *f,qa_actor_owner owner,char **out,qa_error *error)
{
    const char *actual=qa_application_provider_instance(f->application,owner);
    if (!actual) return frontend_fail(error,QA_ERROR_FORMAT,"Campaign UI source has no actual provider identity");
    size_t size=strlen(actual)+1; char *copy=malloc(size);
    if (!copy) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual campaign UI source identity");
    memcpy(copy,actual,size); free(*out); *out=copy; return true;
}
static bool stage(frontend_seat *seat,frontend_campaign_ui_action action,int32_t arena,int32_t skill,qa_error *error)
{
    if (!frontend_campaign_ui_stage(seat->frontend,seat->id,action,arena,skill,error)) return false;
    return (action!=FRONTEND_CAMPAIGN_PLAY && action!=FRONTEND_CAMPAIGN_RETRY && action!=FRONTEND_CAMPAIGN_NEXT) ||
        qa_ui_close_all(seat->ui,now(seat),error);
}
static bool action(void *context,uint32_t seat_id,qa_ui_id control,const qa_ui_action *event,qa_error *error)
{
    frontend_seat *seat=context; frontend_ui_seat_features *state=frontend_ui_features_seat(seat);
    frontend_campaign_ui_view view; qa_ui_state ui;
    if (!state || seat_id!=seat->id || !read(seat,&view,error) || !qa_ui_state_read(seat->ui,&ui,error)) return false;
    if (ui.menu==FRONTEND_ARENA_PROGRESS && event->kind==QA_UI_ACTIVATE) {
        if (control==2) return qa_ui_library_open_arenas(seat->library,error);
        if (control==3) return open(seat,FRONTEND_ARENA_RESET,error);
        if (control==4) return qa_ui_close(seat->ui,now(seat),error);
    } else if (ui.menu==FRONTEND_ARENA_RESET && event->kind==QA_UI_ACTIVATE) {
        if (control==2 && !frontend_campaign_ui_stage(seat->frontend,seat->id,FRONTEND_CAMPAIGN_RESET,0,0,error)) return false;
        return qa_ui_close(seat->ui,now(seat),error);
    } else if ((ui.menu==FRONTEND_ARENA_RESULT || ui.menu==FRONTEND_TEAM_RESULT) && event->kind==QA_UI_ACTIVATE) {
        if (control==4) return stage(seat,FRONTEND_CAMPAIGN_RETRY,0,0,error);
        if (control==5) return stage(seat,FRONTEND_CAMPAIGN_NEXT,0,0,error);
        if (control==6) return qa_ui_library_open_arenas(seat->library,error);
        if (control==7) return open(seat,FRONTEND_ARENA_PROGRESS,error);
        if (control==8) return frontend_campaign_ui_stage(seat->frontend,seat->id,FRONTEND_CAMPAIGN_QUIT,0,0,error);
    }
    return true;
}
static qa_ui_control button(frontend_seat *seat,qa_ui_id id,const char *label,float y,bool enabled)
{
    return (qa_ui_control){.id=id,.kind=QA_UI_BUTTON,.label=label,.rect={48,y,544,32},
        .visible=true,.enabled=enabled,.context=seat,.action=action};
}
static bool progress_menu(void *context,uint32_t seat_id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; frontend_ui_seat_features *state=frontend_ui_features_seat(seat);
    frontend_campaign_ui_view view; (void)seat_id;
    if (!state || !read(seat,&view,error) || view.team) return false;
    size_t count=qa_base_arena_catalog_count(view.catalog);
    if (count>SIZE_MAX-6 || !reserve(state,count+6,error)) return false;
    for (size_t i=0;i<count;++i) {
        const qa_base_arena *arena=qa_base_arena_catalog_at(view.catalog,i); qa_arena_best best; bool available;
        if (!qa_arena_progress_best(view.progression,arena->number,&best,error) ||
            !qa_arena_progress_available(view.progression,arena->number,&available,error)) return false;
        char *detail=state->campaign_labels+i*512;
        if (best.rank) snprintf(detail,512,"Best rank %d, skill %d",best.rank,best.skill);
        else snprintf(detail,512,"%s",available?"Available":"Locked");
        state->campaign_rows[i]=(qa_ui_row){.key=arena->map,.label=arena->title,.detail=detail,.enabled=true};
    }
    const char *medals[]={"Accuracy","Impressive","Excellent","Gauntlet","Frags","Perfect"};
    for (unsigned medal=0;medal<6;++medal) {
        int32_t amount;
        if (!qa_arena_progress_award(view.progression,(int32_t)medal,&amount,error)) return false;
        char *detail=state->campaign_labels+(count+medal)*512; snprintf(detail,512,"%d",amount);
        state->campaign_rows[count+medal]=(qa_ui_row){.key=medals[medal],.label=medals[medal],.detail=detail,.enabled=true};
    }
    seat->controls[0]=button(seat,1,"Progress and awards",96,true); seat->controls[0].kind=QA_UI_LIST;
    seat->controls[0].rect=(qa_scene_rect_f){48,96,544,231};
    seat->controls[0].value.list.rows=state->campaign_rows; seat->controls[0].value.list.count=count+6;
    seat->controls[0].value.list.row_height=33; seat->controls[0].value.list.revision=view.source.publication_generation;
    seat->controls[1]=button(seat,2,"Choose an arena",336,true);
    seat->controls[2]=button(seat,3,"Reset progress...",376,true); seat->controls[3]=button(seat,4,"Back",424,true);
    *out=(qa_ui_menu){.id=FRONTEND_ARENA_PROGRESS,.title="Arena progress",.controls=seat->controls,.count=4,.fullscreen=true}; return true;
}
static bool reset_menu(void *context,uint32_t seat_id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; frontend_campaign_ui_view view; (void)seat_id;
    if (!read(seat,&view,error) || view.team) return false;
    seat->controls[0]=button(seat,1,"Keep progress",180,true); seat->controls[1]=button(seat,2,"Reset progress",230,true);
    *out=(qa_ui_menu){.id=FRONTEND_ARENA_RESET,.title="Reset arena progress and awards?",.controls=seat->controls,.count=2,.fullscreen=true}; return true;
}
static bool result_menu(void *context,uint32_t seat_id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; frontend_ui_seat_features *state=frontend_ui_features_seat(seat);
    frontend_campaign_ui_view view; (void)seat_id;
    if (!state || !read(seat,&view,error) || view.team || !view.result || !reserve(state,3,error)) return false;
    size_t count=view.player_count<3?view.player_count:3;
    for (size_t i=0;i<count;++i) {
        frontend_campaign_podium_player player=view.players[i]; char name[80];
        if (player.client<0 || !qa_application_q3_campaign_player_name(seat->frontend->application,
            &view.source,(uint32_t)player.client,name,error)) return false;
        char *label=state->campaign_labels+i*512; snprintf(label,512,"%d. %s   %d",player.rank,name,player.score);
        seat->controls[i]=button(seat,i+1,label,96+(float)i*38,false);
    }
    seat->controls[count++]=button(seat,4,"Retry",258,true);
    seat->controls[count++]=button(seat,5,"Next match",300,view.postgame.next_level>=0);
    seat->controls[count++]=button(seat,6,"Choose an arena",342,true);
    seat->controls[count++]=button(seat,7,"Progress and awards",384,true);
    seat->controls[count++]=button(seat,8,"Main menu",426,true);
    *out=(qa_ui_menu){.id=FRONTEND_ARENA_RESULT,.title=view.postgame.rank==1?"Victory":"Match complete",
        .controls=seat->controls,.count=count,.fullscreen=true}; return true;
}
static const qa_cvar_view *source_value(const frontend_campaign_ui_view *view,const char *name,qa_error *error)
{
    const qa_cvar_view *value=qa_cvars_find(view->source.cvars,name);
    if (!value) frontend_fail(error,QA_ERROR_FORMAT,"Team Arena result lost its actual source score cvar");
    return value;
}
static bool team_result_menu(void *context,uint32_t seat_id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; frontend_ui_seat_features *state=frontend_ui_features_seat(seat);
    frontend_campaign_ui_view view; (void)seat_id;
    if (!state || !read(seat,&view,error) || !view.team || !view.result) return false;
    const qa_cvar_view *map=source_value(&view,"ui_scoreMap",error),*points=source_value(&view,"ui_scoreScore",error),
        *time=source_value(&view,"ui_scoreTime",error);
    if (!map || !points || !time) return false;
    snprintf(state->campaign_title,sizeof(state->campaign_title),"%s: %s (%d - %d)",
        view.team_live.won?"Victory":"Defeat",map->value,view.team_live.score,view.team_live.opponent);
    snprintf(state->campaign_status,sizeof(state->campaign_status),"Score %s · Time %s",points->value,time->value);
    seat->controls[0]=button(seat,1,state->campaign_status,108,false);
    seat->controls[1]=button(seat,5,"Next match",196,true);
    seat->controls[2]=button(seat,4,"Retry match",240,true);
    seat->controls[3]=button(seat,8,"Main menu",328,true);
    *out=(qa_ui_menu){.id=FRONTEND_TEAM_RESULT,.title=state->campaign_title,.controls=seat->controls,.count=4,.fullscreen=true}; return true;
}
bool frontend_campaign_menu_create(frontend_seat *seat,qa_error *error)
{
    const qa_ui_menu_registration registrations[]={
        {FRONTEND_ARENA_PROGRESS,seat,progress_menu,NULL,NULL},
        {FRONTEND_ARENA_RESET,seat,reset_menu,NULL,NULL},
        {FRONTEND_ARENA_RESULT,seat,result_menu,NULL,NULL},
        {FRONTEND_TEAM_RESULT,seat,team_result_menu,NULL,NULL}};
    for (size_t i=0;i<sizeof(registrations)/sizeof(*registrations);++i) if (!qa_ui_register(seat->ui,registrations+i,error)) return false;
    return true;
}
bool frontend_campaign_menu_available(frontend_seat *seat,bool *available,qa_error *error)
{
    if (!seat || !available) return frontend_fail(error,QA_ERROR_ARGUMENT,"Campaign menu availability lost its actual seat");
    frontend_campaign_ui_view view;
    if (!frontend_campaign_ui_read(seat->frontend,seat->id,&view,error)) return false;
    *available=view.installed && !view.team; return true;
}
bool frontend_campaign_menu_sync(qa_frontend *f,qa_error *error)
{
    if (f->options.dedicated || !f->seats || !f->seats[0].ui) return true;
    for (unsigned i=0;i<f->options.seats;++i) {
        frontend_ui_seat_features *state=frontend_ui_features_seat(f->seats+i); frontend_campaign_ui_view view;
        if (!state || !frontend_campaign_ui_read(f,i,&view,error)) return false;
        if (view.action_pending) continue;
        if (!view.installed || !view.result) { state->shown_result=false; continue; }
        int32_t intermission=view.team?view.team_live.intermission_time_ms:0;
        if (state->shown_result && identity(f,view.source.source_owner,state->shown_instance) &&
            state->shown_generation==view.source.publication_generation && state->shown_map_revision==view.source.map_revision &&
            state->shown_command_generation==view.source.command_generation && state->shown_intermission==intermission) {
            if (view.team) {
                qa_ui_state ui;
                if (!qa_ui_state_read(f->seats[i].ui,&ui,error) || (!ui.depth && !open(f->seats+i,FRONTEND_TEAM_RESULT,error))) return false;
            }
            continue;
        }
        if (!qa_ui_close_all(f->seats[i].ui,now(f->seats+i),error) ||
            !open(f->seats+i,view.team?FRONTEND_TEAM_RESULT:FRONTEND_ARENA_RESULT,error)) return false;
        if (!copy_identity(f,view.source.source_owner,&state->shown_instance,error)) return false;
        state->shown_result=true;
        state->shown_generation=view.source.publication_generation; state->shown_map_revision=view.source.map_revision;
        state->shown_command_generation=view.source.command_generation; state->shown_intermission=intermission;
    }
    return true;
}
