#include "campaign_menu.h"
#include "campaign_ui.h"
#include "ui_features_private.h"
#include "qa/text.h"
#include <stdio.h>

static double now(frontend_seat *seat) { return (double)seat->frontend->time_ns/1000000.; }
static bool selection_open(void *,uint32_t,qa_error *);
static bool open(frontend_seat *seat,qa_ui_id id,qa_error *error)
{
    if (id==FRONTEND_ARENA_SELECTION && !selection_open(seat,seat->id,error)) return false;
    return qa_ui_open(seat->ui,id,now(seat),error);
}
static int32_t tier(const qa_base_arena *arena)
{
    char special[9]={0}; size_t length=strlen(arena->special);
    if (length<sizeof(special)) for (size_t i=0;i<length;++i) special[i]=arena->special[i]>='A' && arena->special[i]<='Z'?
        (char)(arena->special[i]+'a'-'A'):arena->special[i];
    if (!strcmp(special,"training")) return -1;
    if (!strcmp(special,"final")) return -2;
    return arena->number/4+1;
}
static int tier_compare(const void *left,const void *right)
{
    const qa_ui_row *a=left,*b=right; char *a_end=NULL,*b_end=NULL;
    long a_selection=strtol(a->detail,&a_end,10),b_selection=strtol(b->detail,&b_end,10);
    if (a_selection!=b_selection) return a_selection<b_selection?-1:1;
    unsigned long long a_index=strtoull(a_end+1,NULL,10),b_index=strtoull(b_end+1,NULL,10);
    return a_index<b_index?-1:a_index>b_index;
}
static const qa_base_arena *arena_number(const frontend_campaign_ui_view *view,int32_t number)
{
    for (size_t i=0;i<qa_base_arena_catalog_count(view->catalog);++i) {
        const qa_base_arena *arena=qa_base_arena_catalog_at(view->catalog,i);
        if (arena->number==number) return arena;
    }
    return NULL;
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
static bool selection_open(void *context,uint32_t seat_id,qa_error *error)
{
    frontend_seat *seat=context; frontend_ui_seat_features *state=frontend_ui_features_seat(seat);
    frontend_campaign_ui_view view;
    if (!state || seat_id!=seat->id || !read(seat,&view,error) || view.team) return false;
    if (!copy_identity(seat->frontend,view.source.source_owner,&state->campaign_instance,error)) return false;
    state->campaign_generation=view.source.publication_generation;
    state->campaign_level=view.selection; state->campaign_selected=arena_number(&view,view.selection)!=NULL;
    const qa_base_arena *arena=arena_number(&view,view.selection);
    state->campaign_tier=arena?tier(arena):1; return true;
}
static const qa_base_arena *tier_row(const frontend_campaign_ui_view *view,int32_t wanted,size_t index)
{
    qa_arena_catalog levels=qa_base_arena_catalog_levels(view->catalog);
    /* Within a tier, authored numbering follows catalog order; training and
     * final rows share their selection value and retain that same stable order. */
    for (size_t i=0;i<qa_base_arena_catalog_count(view->catalog);++i) {
        const qa_base_arena *arena=qa_base_arena_catalog_at(view->catalog,i);
        if ((!*arena->special && arena->number>=levels.regular_levels) || tier(arena)!=wanted) continue;
        if (!index) return arena;
        --index;
    }
    return NULL;
}
static bool action(void *context,uint32_t seat_id,qa_ui_id control,const qa_ui_action *event,qa_error *error)
{
    frontend_seat *seat=context; frontend_ui_seat_features *state=frontend_ui_features_seat(seat);
    frontend_campaign_ui_view view; qa_ui_state ui;
    if (!state || seat_id!=seat->id || !read(seat,&view,error) || !qa_ui_state_read(seat->ui,&ui,error)) return false;
    if ((ui.menu==FRONTEND_ARENA_SELECTION || ui.menu==FRONTEND_ARENA_SKILL) &&
        (!identity(seat->frontend,view.source.source_owner,state->campaign_instance) ||
         state->campaign_generation!=view.source.publication_generation))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Arena draft belongs to an earlier source publication");
    if (ui.menu==FRONTEND_ARENA_SELECTION) {
        if (control==1 && event->kind==QA_UI_SELECT) {
            if (event->value.row>=state->campaign_capacity || !state->campaign_rows[event->value.row].key) return false;
            char *end=NULL; long value=strtol(state->campaign_rows[event->value.row].key,&end,10);
            if (!end || *end || value<INT32_MIN || value>INT32_MAX) return false;
            state->campaign_tier=(int32_t)value; state->campaign_selected=false; return true;
        }
        if (control==2 && (event->kind==QA_UI_SELECT || event->kind==QA_UI_ROW_ACTIVATE)) {
            const qa_base_arena *arena=tier_row(&view,state->campaign_tier,event->value.row);
            if (!arena) return frontend_fail(error,QA_ERROR_ARGUMENT,"Selected arena is outside its authored tier");
            state->campaign_level=arena->number; state->campaign_selected=true;
            if (event->kind==QA_UI_SELECT) return true;
            control=5;
        }
        if (event->kind!=QA_UI_ACTIVATE && event->kind!=QA_UI_ROW_ACTIVATE) return true;
        if (control==5) {
            bool available=false;
            if (!state->campaign_selected) {
                const qa_base_arena *arena=tier_row(&view,state->campaign_tier,0);
                if (arena) { state->campaign_level=arena->number; state->campaign_selected=true; }
            }
            if (!state->campaign_selected || !qa_arena_progress_available(view.progression,state->campaign_level,&available,error) || !available)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Win the preceding tier to unlock this arena");
            return open(seat,FRONTEND_ARENA_SKILL,error);
        }
        if (control==6) return qa_ui_close(seat->ui,now(seat),error);
    } else if (ui.menu==FRONTEND_ARENA_SKILL && event->kind==QA_UI_ACTIVATE) {
        if (control>=1 && control<=5) return stage(seat,FRONTEND_CAMPAIGN_PLAY,
            state->campaign_level,(int32_t)control,error);
        if (control==6) return qa_ui_close(seat->ui,now(seat),error);
    } else if (ui.menu==FRONTEND_ARENA_PROGRESS && event->kind==QA_UI_ACTIVATE) {
        if (control==2) return open(seat,FRONTEND_ARENA_SELECTION,error);
        if (control==3) return open(seat,FRONTEND_ARENA_RESET,error);
        if (control==4) return qa_ui_close(seat->ui,now(seat),error);
    } else if (ui.menu==FRONTEND_ARENA_RESET && event->kind==QA_UI_ACTIVATE) {
        if (control==2 && !frontend_campaign_ui_stage(seat->frontend,seat->id,FRONTEND_CAMPAIGN_RESET,0,0,error)) return false;
        return qa_ui_close(seat->ui,now(seat),error);
    } else if ((ui.menu==FRONTEND_ARENA_RESULT || ui.menu==FRONTEND_TEAM_RESULT) && event->kind==QA_UI_ACTIVATE) {
        if (control==4) return stage(seat,FRONTEND_CAMPAIGN_RETRY,0,0,error);
        if (control==5) return stage(seat,FRONTEND_CAMPAIGN_NEXT,0,0,error);
        if (control==6) return open(seat,FRONTEND_ARENA_SELECTION,error);
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
static bool selection_menu(void *context,uint32_t seat_id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; frontend_ui_seat_features *state=frontend_ui_features_seat(seat);
    frontend_campaign_ui_view view; (void)seat_id;
    if (!state || !read(seat,&view,error) || view.team ||
        !identity(seat->frontend,view.source.source_owner,state->campaign_instance) || state->campaign_generation!=view.source.publication_generation)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Arena selection lost its actual source publication");
    size_t count=qa_base_arena_catalog_count(view.catalog);
    if (count>SIZE_MAX/2 || !reserve(state,count*2,error)) return false;
    size_t tiers=0; qa_arena_catalog levels=qa_base_arena_catalog_levels(view.catalog);
    for (size_t i=0;i<count;++i) {
        const qa_base_arena *arena=qa_base_arena_catalog_at(view.catalog,i);
        if (!*arena->special && arena->number>=levels.regular_levels) continue;
        int32_t group=tier(arena); bool found=false;
        for (size_t j=0;j<tiers;++j) if (strtol(state->campaign_rows[j].key,NULL,10)==group) {
            char *detail=(char *)state->campaign_rows[j].detail;
            if (arena->selection<strtol(detail,NULL,10)) snprintf(detail,96,"%d:%zu",arena->selection,i);
            found=true;
        }
        if (found) continue;
        char *key=state->campaign_labels+tiers*512,*label=key+32,*order=key+160;
        snprintf(key,32,"%d",group);
        snprintf(order,96,"%d:%zu",arena->selection,i);
        if (group==-1) snprintf(label,128,"Training"); else if (group==-2) snprintf(label,128,"Final arena"); else snprintf(label,128,"Tier %d",group);
        state->campaign_rows[tiers++]=(qa_ui_row){.key=key,.label=label,.detail=order,.enabled=true};
    }
    if (tiers>1) qsort(state->campaign_rows,tiers,sizeof(*state->campaign_rows),tier_compare);
    /* Choice labels live in frame scratch while its factory result is borrowed. */
    const char **labels=tiers?qa_arena_alloc(&seat->frontend->frame.storage,tiers*sizeof(*labels),_Alignof(const char *),error):NULL;
    if (tiers && !labels) return false;
    size_t selected_tier=0;
    for (size_t i=0;i<tiers;++i) { labels[i]=state->campaign_rows[i].label; if (strtol(state->campaign_rows[i].key,NULL,10)==state->campaign_tier) selected_tier=i; }
    size_t rows=0,selected_row=0; const qa_base_arena *current=NULL;
    for (size_t i=0;;++i) {
        const qa_base_arena *arena=tier_row(&view,state->campaign_tier,i); if (!arena) break;
        bool available=false; qa_arena_best best;
        if (!qa_arena_progress_available(view.progression,arena->number,&available,error) || !qa_arena_progress_best(view.progression,arena->number,&best,error)) return false;
        char *detail=state->campaign_labels+(count+rows)*512;
        if (!available) snprintf(detail,512,"Locked"); else if (!best.rank) snprintf(detail,512,"Not completed"); else snprintf(detail,512,"Rank %d · Skill %d",best.rank,best.skill);
        state->campaign_rows[count+rows]=(qa_ui_row){.key=arena->map,.label=arena->title,.detail=detail,.enabled=true};
        if (state->campaign_selected && state->campaign_level==arena->number) { selected_row=rows; current=arena; }
        ++rows;
    }
    if (!current && rows) current=tier_row(&view,state->campaign_tier,0);
    seat->controls[0]=button(seat,1,"Tier",94,tiers!=0); seat->controls[0].kind=QA_UI_CHOICE;
    seat->controls[0].value.choice.labels=labels; seat->controls[0].value.choice.count=tiers; seat->controls[0].value.choice.selected=selected_tier;
    seat->controls[1]=button(seat,2,"Arenas",144,rows!=0); seat->controls[1].kind=QA_UI_LIST; seat->controls[1].rect.height=156;
    seat->controls[1].value.list.rows=state->campaign_rows+count; seat->controls[1].value.list.count=rows;
    seat->controls[1].value.list.selected=selected_row; seat->controls[1].value.list.row_height=39;
    seat->controls[1].value.list.revision=view.source.publication_generation^(uint64_t)(uint32_t)state->campaign_tier;
    size_t used=0; state->campaign_status[0]=0;
    if (current) {
        used=(size_t)snprintf(state->campaign_status,sizeof(state->campaign_status),"Opponents: ");
        for (size_t i=0;i<current->bot_count && used<sizeof(state->campaign_status)-1;++i) {
            int added=snprintf(state->campaign_status+used,sizeof(state->campaign_status)-used,"%s%s",i?", ":"",current->bots[i]);
            if (added<0) return false;
            used+=(size_t)added<sizeof(state->campaign_status)-used?(size_t)added:sizeof(state->campaign_status)-used-1;
        }
        if (!current->bot_count) snprintf(state->campaign_status+used,sizeof(state->campaign_status)-used,"None");
    } else snprintf(state->campaign_status,sizeof(state->campaign_status),"No single-player arenas are available.");
    seat->controls[2]=button(seat,3,state->campaign_status,318,false);
    char *limits=state->campaign_title; limits[0]=0;
    if (current) {
        char frags[32],minutes[32];
        if (current->frag_limit>0) {
            if (!qa_format_number(current->frag_limit,frags,error)) return false;
            snprintf(limits,sizeof(state->campaign_title),"%s frags",frags);
        }
        if (current->time_limit>0) {
            if (!qa_format_number(current->time_limit,minutes,error)) return false;
            size_t length=strlen(limits);
            snprintf(limits+length,sizeof(state->campaign_title)-length,"%s%s minutes",length?" · ":"",minutes);
        }
    }
    seat->controls[3]=button(seat,4,limits,350,false);
    bool available=false;
    if (current && !qa_arena_progress_available(view.progression,current->number,&available,error)) return false;
    seat->controls[4]=button(seat,5,available?"Choose difficulty":"Win the preceding tier to unlock",392,available);
    seat->controls[5]=button(seat,6,"Back",436,true);
    *out=(qa_ui_menu){.id=FRONTEND_ARENA_SELECTION,.title="Choose an arena",.controls=seat->controls,.count=6,.fullscreen=true}; return true;
}
static bool skill_menu(void *context,uint32_t seat_id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; frontend_campaign_ui_view view; (void)seat_id;
    if (!read(seat,&view,error) || view.team) return false;
    const char *labels[]={"I Can Win","Bring It On","Hurt Me Plenty","Hardcore","Nightmare"};
    for (unsigned i=0;i<5;++i) seat->controls[i]=button(seat,i+1,labels[i],108+(float)i*44,true);
    seat->controls[5]=button(seat,6,"Back",420,true);
    *out=(qa_ui_menu){.id=FRONTEND_ARENA_SKILL,.title="Difficulty",.controls=seat->controls,.count=6,.fullscreen=true}; return true;
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
        {FRONTEND_ARENA_SELECTION,seat,selection_menu,NULL,NULL},
        {FRONTEND_ARENA_SKILL,seat,skill_menu,NULL,NULL},
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
