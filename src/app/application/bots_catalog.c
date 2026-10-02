#include "bots_catalog.h"
#include "bots_setup.h"
#include "bot_admission.h"
#include "bot_world.h"
#include "bots_transport.h"
#include "map_players_private.h"
#include "native_q3_clients.h"
#include "native_q3_console.h"
#include "native_q3_match.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_round.h"
#include "qa/game_q3_source.h"
#include <limits.h>
#include <string.h>

static bool source(application_bots *bots,application_provider **out,qa_error *error) {
    application_provider *actual=bots?application_bot_source(bots):NULL;
    if(!actual || !actual->constructed || !actual->attached || !actual->map_bound ||
       actual->close_pending || (!bots->shared_world && actual->kind!=APPLICATION_PROVIDER_Q3))
        return application_fail(error,QA_ERROR_ARGUMENT,"bot catalogue has no actual live native or shared GAME owner");
    *out=actual;return true;
}
static qa_cvars *registry(application_bots *bots,const char *name) {
    application_provider *actual=application_bot_source(bots);
    return actual->kind==APPLICATION_PROVIDER_Q3?application_native_q3_cvar_owner(actual,name):bots->application->cvars;
}
static bool print(void *context,const char *text,qa_error *error) {
    application_bots *bots=context;qa_bot_services ai=application_bots_services(bots);
    return ai.print(ai.context,text,error);
}
static bool cvar(void *context,const char *name,qa_cvar_view *out,bool *found,qa_error *error) {
    application_bots *bots=context;application_provider *actual;
    if(!source(bots,&actual,error)) return false;
    qa_cvars *owner=registry(bots,name);
    if(!owner) return application_fail(error,QA_ERROR_NOT_FOUND,"bot catalogue cvar owner is absent");
    const qa_cvar_view *value=qa_cvars_find(owner,name);*found=value!=NULL;
    *out=value?*value:(qa_cvar_view){0};return true;
}
static bool register_cvar(void *context,const char *name,const char *value,uint32_t flags,qa_error *error) {
    application_bots *bots=context;qa_bot_services ai=application_bots_services(bots);
    return ai.register_cvar(ai.context,name,value,flags,error);
}
static bool set_cvar(void *context,const char *name,const char *value,qa_error *error) {
    application_bots *bots=context;application_provider *actual;
    if(!source(bots,&actual,error)) return false;
    bool native=actual->kind==APPLICATION_PROVIDER_Q3;
    if(native && !application_native_q3_console_borrow(actual,error)) return false;
    qa_cvars *owner=registry(bots,name);
    bool okay=owner?qa_cvars_set(owner,name,value,true,error):
        application_fail(error,QA_ERROR_NOT_FOUND,"bot catalogue cvar setter owner is absent");
    if(native) application_native_q3_console_release(actual);
    return okay;
}
static bool server_info(void *context,char *out,size_t capacity,qa_error *error) {
    application_bots *bots=context;application_provider *actual;qa_buffer text={0};
    if(!out || !capacity || !source(bots,&actual,error)) return false;
    qa_cvars *owner=registry(bots,"mapname");
    if(!owner || !qa_cvars_info(owner,QA_CVAR_SERVERINFO,8192,&text,error)) return false;
    size_t size=text.size;if(size>=capacity) size=capacity-1;
    memcpy(out,text.data,size);out[size]=0;qa_buffer_free(&text);return true;
}
static bool clock_read(void *context,qa_bot_catalog_clock *out,qa_error *error) {
    application_bots *bots=context;application_provider *actual;
    if(!source(bots,&actual,error)) return false;
    if(bots->shared_world) {
        /* Shared SourceBotGame owns FFA and a zero constructor startTime. */
        uint32_t maximum=application_bot_world_max_clients(bots->shared_world);
        if(maximum>INT32_MAX) return application_fail(error,QA_ERROR_FORMAT,"shared bot source client extent is not signed");
        *out=(qa_bot_catalog_clock){.max_clients=(int32_t)maximum};
        return application_bot_world_clock(bots->shared_world,&out->time,&out->intermission_time,error);
    }
    qa_q3_round_source round;
    if(!qa_q3_round_read(actual->state.q3,&round,error) || round.max_clients>INT32_MAX) return false;
    *out=(qa_bot_catalog_clock){.time=round.current_time_ms,.start_time=round.start_time_ms,
        .max_clients=(int32_t)round.max_clients};
    return application_native_q3_settings_integer(actual,"g_gametype",&out->game_type,error) &&
        application_native_q3_match_intermission(actual,&out->intermission_time,error);
}
static bool client_read(void *context,int32_t number,qa_bot_catalog_client *out,qa_error *error) {
    application_bots *bots=context;application_provider *actual;
    if(number<0 || !source(bots,&actual,error)) return false;
    if(bots->shared_world) {
        application_bot_world_entity entity;
        if(!application_bot_world_read(bots->shared_world,number,&entity,error)) return false;
        *out=(qa_bot_catalog_client){.name=entity.name?entity.name:"",.team=entity.team,
            .has_player=entity.has_player,.connected=entity.connected,.bot=entity.bot};return true;
    }
    qa_q3_source_binding binding;qa_q3_native_client client;qa_q3_bot_player_state state;
    if(!qa_q3_source_binding_read(actual->state.q3,(uint32_t)number,&binding,error) ||
       !qa_q3_client_slot_read(actual->state.q3,(uint32_t)number,&client,error) ||
       !qa_q3_client_bot_state_read(actual->state.q3,(uint32_t)number,&state,error)) return false;
    memcpy(bots->catalogue_name,client.netname,sizeof(bots->catalogue_name));
    *out=(qa_bot_catalog_client){.name=bots->catalogue_name,.team=client.session.team,
        .has_player=state.has_player,.connected=client.connected==QA_Q3_CLIENT_CONNECTED,
        .bot=(binding.server_flags&8u)!=0};return true;
}
static bool allocate_client(void *context,int32_t *out,qa_error *error) {
    application_bots *bots=context;const qa_launch_seat *desired;
    if(!application_bots_admission_seat(bots->application,&desired))
        return application_fail(error,QA_ERROR_ARGUMENT,"bot allocation lacks its actual catalogue admission");
    return application_players_bot_allocate(bots->application,desired,out,error);
}
static bool choose_team(void *context,int32_t ignored,int32_t *out,qa_error *error) {
    application_bots *bots=context;application_provider *actual;
    if(!source(bots,&actual,error)) return false;
    if(bots->shared_world) {*out=0;return true;}
    return application_native_q3_client_pick_team(actual,ignored,out,error);
}
static bool activate(void *context,int32_t number,qa_error *error) {
    application_bots *bots=context;application_provider *actual;
    if(number<0 || !source(bots,&actual,error)) return false;
    if(bots->shared_world) return application_bot_world_activate(bots->shared_world,(uint32_t)number,error);
    qa_q3_source_binding binding;
    return qa_q3_source_binding_read(actual->state.q3,(uint32_t)number,&binding,error) &&
        qa_q3_client_activate_bot(actual->state.q3,binding.actor,error);
}
static bool userinfo(void *context,int32_t number,char *out,size_t capacity,qa_error *error) {
    application_bots *bots=context;application_provider *actual;const char *text;
    if(number<0 || !out || !capacity || !source(bots,&actual,error)) return false;
    if(bots->shared_world) text=application_bot_world_userinfo(bots->shared_world,(uint32_t)number);
    else if(!application_native_q3_wire_userinfo_read(actual,(uint32_t)number,&text,error)) return false;
    size_t size=strlen(text);if(size>=capacity) size=capacity-1;memcpy(out,text,size);out[size]=0;return true;
}
static bool set_userinfo(void *context,int32_t number,const char *text,qa_error *error) {
    application_bots *bots=context;
    return number>=0 && application_players_bot_userinfo(bots->application,(uint32_t)number,text,error);
}
static bool connect_client(void *context,int32_t number,bool first,bool bot,bool *accepted,qa_error *error) {
    application_bots *bots=context;
    return number>=0 && application_players_bot_connect(bots->application,(uint32_t)number,first,bot,accepted,error);
}
static bool begin_client(void *context,int32_t number,qa_error *error) {
    application_bots *bots=context;
    return number>=0 && application_players_bot_begin(bots->application,(uint32_t)number,error);
}
static bool reset_podium(void *context,qa_error *error) {
    application_bots *bots=context;application_provider *actual;
    if(!source(bots,&actual,error)) return false;
    /* The shared SourceBotGame's actual resetPodiumPlayers closure is empty. */
    return bots->shared_world || qa_q3_source_reset_podium_players(actual->state.q3,error);
}
static bool command(void *context,const char *text,bool append,qa_error *error) {
    application_bots *bots=context;application_provider *actual;qa_console *console;
    if(!source(bots,&actual,error)) return false;
    qa_command_context origin={.owner=actual->owner,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_SERVER};
    if(bots->shared_world) console=bots->application->console;
    else if(!application_native_q3_console_at(actual,&console,NULL,NULL))
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot catalogue source console is absent");
    return append?qa_console_append(console,&origin,text,error):qa_console_insert(console,&origin,text,error);
}
static bool insert_command(void *context,const char *text,qa_error *error) {return command(context,text,false,error);}
static bool append_command(void *context,const char *text,qa_error *error) {return command(context,text,true,error);}
static bool server_command(void *context,int32_t number,const char *text,qa_error *error) {
    application_bots *bots=context;application_provider *actual;
    if(!source(bots,&actual,error)) return false;
    if(bots->shared_world) return application_bot_transport_message(bots->transport,number,text,error);
    return application_native_q3_send_command(actual,number,text,error);
}
static bool random(void *context,float *out,qa_error *error) {
    application_bots *bots=context;qa_bot_services ai=application_bots_services(bots);
    return ai.random(ai.context,out,error);
}
bool application_bots_catalog_create(application_bots *bots,qa_error *error) {
    application_provider *actual;
    if(!bots || bots->catalogue || !source(bots,&actual,error)) return false;
    qa_bot_services ai=application_bots_services(bots);
    qa_bot_catalog_services services={.context=bots,.session=bots->application->session,.files=bots->files,
        .memory=ai.memory,.print=print,.cvar=cvar,.register_cvar=register_cvar,.set_cvar=set_cvar,
        .server_info=server_info,.clock=clock_read,.client=client_read,.allocate_client=allocate_client,
        .choose_team=choose_team,.activate=activate,.userinfo=userinfo,.set_userinfo=set_userinfo,
        .connect=connect_client,.begin=begin_client,.reset_podium=reset_podium,.insert_command=insert_command,
        .append_command=append_command,.server_command=server_command,.random=random};
    return qa_bot_catalog_create(&services,&bots->catalogue,error);
}
bool application_bots_catalog_initialize(application_bots *bots,bool restart,qa_error *error) {
    if(!bots || !bots->population || bots->restoring || bots->calls || bots->producing)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot catalogue initialization requires its genuine loaded AI map");
    if(bots->catalogue_ready) return true;
    bool okay=(bots->catalogue || application_bots_catalog_create(bots,error)) &&
        register_cvar(bots,"bot_enable","1",0,error) &&
        register_cvar(bots,"g_spSkill","2",QA_CVAR_ARCHIVE|QA_CVAR_LATCH,error) &&
        qa_bot_catalog_initialize(bots->catalogue,restart,error);
    if(okay) bots->catalogue_ready=true;
    return okay;
}
bool application_bots_spawn_admitted(const qa_application *app) {
    const application_bots *bots=app?app->bots:NULL;
    return bots && bots->catalogue_spawn && bots->producing && bots->calls==1 &&
        !bots->restoring && bots->round_phase==APPLICATION_BOT_ROUND_ACTIVE;
}
bool application_bots_admission_seat(const qa_application *app,const qa_launch_seat **desired) {
    const application_bots *bots=app?app->bots:NULL;
    if(!bots || !bots->catalogue || !desired || qa_bot_catalog_can_destroy(bots->catalogue)) return false;
    *desired=bots->catalogue_seat;return true;
}
bool application_bots_catalog_check_spawn(void *context,qa_error *error) {
    application_bots *bots=context;
    if(!bots || !bots->catalogue || bots->catalogue_spawn || bots->restoring ||
       !bots->producing || bots->calls!=1 || bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE)
        return application_fail(error,QA_ERROR_ARGUMENT,"source bot spawn callback lacks its actual admitted frame");
    bots->catalogue_spawn=true;bool okay=qa_bot_catalog_check_spawn(bots->catalogue,error);
    bots->catalogue_spawn=false;return okay;
}
bool application_bots_catalog_console(qa_application *app,const qa_command_invocation *invocation,bool *handled,qa_error *error) {
    if(!app || !invocation || !handled) return application_fail(error,QA_ERROR_ARGUMENT,"bot console requires its actual command");
    *handled=false;const char *name=invocation->argc?invocation->argv[0]:"";
    char folded[8];size_t size=strlen(name);
    if(size>=sizeof(folded)) return true;
    for(size_t i=0;i<=size;++i) {
        unsigned char ch=(unsigned char)name[i];folded[i]=(char)(ch>='A' && ch<='Z'?ch+32:ch);
    }
    bool add=!strcmp(folded,"addbot"),list=!strcmp(folded,"botlist");
    if(!add && !list) return true;
    application_bots *bots=app->bots;
    if(!bots || !bots->catalogue) {
        application_provider *source=application_world_provider(app,QA_ROLE_ENTITIES,"");
        if(!source || (source->kind!=APPLICATION_PROVIDER_Q1 && source->kind!=APPLICATION_PROVIDER_Q2)) return true;
        if(!application_bots_requested(app,error)) return false;
        bots=app->bots;
    }
    if(bots->restoring || bots->calls || bots->producing || bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot catalogue command requires its idle source map");
    *handled=true;return qa_bot_catalog_console(bots->catalogue,invocation->argv,invocation->argc,error);
}
bool application_bots_catalog_add(qa_application *app,const qa_bot_catalog_add_request *request,qa_error *error) {
    if(app && (!app->bots || !app->bots->population) && !application_bots_requested(app,error)) return false;
    application_bots *bots=app?app->bots:NULL;
    if(!bots || !bots->catalogue || bots->restoring || bots->calls || bots->producing ||
       bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE)
        return application_fail(error,QA_ERROR_ARGUMENT,"public bot launch requires its idle actual catalogue");
    return qa_bot_catalog_add_utf8(bots->catalogue,request,error);
}
bool application_bots_catalog_add_seat(qa_application *app,const qa_launch_seat *seat,
    const qa_bot_catalog_add_request *request,qa_error *error) {
    application_bots *bots=app?app->bots:NULL;
    if(!bots || !seat || !seat->bot || bots->catalogue_seat || !bots->catalogue ||
       !qa_bot_catalog_can_destroy(bots->catalogue))
        return application_fail(error,QA_ERROR_ARGUMENT,"launch bot admission requires its actual exclusive seat");
    bots->catalogue_seat=seat;bool okay=application_bots_catalog_add(app,request,error);
    bots->catalogue_seat=NULL;return okay;
}
bool application_bots_catalog_remove_begin(qa_application *app,uint32_t number,qa_error *error) {
    application_bots *bots=app?app->bots:NULL;
    if(!bots || !bots->catalogue) return true;
    if(number>INT32_MAX) return application_fail(error,QA_ERROR_ARGUMENT,"bot Begin removal exceeds its physical source extent");
    return qa_bot_catalog_remove_queued_begin(bots->catalogue,(int32_t)number,error);
}
bool application_bots_catalog_character(qa_application *app,const char *name,char *out,size_t capacity,qa_error *error) {
    application_bots *bots=app?app->bots:NULL;qa_buffer info={0};bool found;
    if(!bots || !bots->catalogue || !out || !capacity || !name)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot settings require their actual loaded definition");
    bool okay=qa_bot_catalog_bot_name_utf8(bots->catalogue,name,&info,&found,error);
    if(okay && !found) okay=application_fail(error,QA_ERROR_NOT_FOUND,"launch bot definition is absent from its actual catalogue");
    if(okay) {qa_q3_client_info_value((char *)info.data,"aifile",out,capacity);
        if(!*out) okay=application_fail(error,QA_ERROR_FORMAT,"launch bot definition has no actual aifile");}
    qa_buffer_free(&info);return okay;
}
