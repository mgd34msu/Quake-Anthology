#include "guest_q3_component_clients.h"
#include "map_players_private.h"
#include "guest_q3_private.h"
#include "guest_projection_private.h"
#include "guest_q3_weapons.h"
#include "guest_qc_internal.h"
#include "guest_native_q2_private.h"
#include "native_q3_wire_state.h"
#include "native_q3_clients.h"
#include "control_frame.h"
#include "rankings.h"
#include "bots_round.h"
#include "client_events.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q2_player.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/text.h"
#include "qa/source_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct component_drop {
    struct component_drop *next;
    qa_actor_id actor;
    qa_actor_owner component;
    uint32_t slot;
    char *reason;
    bool rankings,bots,transport,source,character;
} component_drop;
struct application_q3_component_client_adapter {
    qa_application *application;
    application_provider *source;
    qa_actor_owner owner;
    qa_world *world;
    qa_buffer userinfo;
    component_drop *drops,*tail;
    unsigned calls;
    bool draining;
    void *transport_context;
    bool (*transport_drop)(void *,qa_actor_owner,qa_actor_id,const char *,qa_error *);
};

static bool storage(const application_q3_component_client_adapter *a)
{
    return a&&a->application&&a->source&&a->source->application==a->application&&
        a->source->owner==a->owner&&a->source->constructed&&a->source->attached&&!a->source->close_pending&&
        a->application->session&&a->application->players&&
        a->application->players->map_provider==a->source&&a->application->world==a->world;
}
static application_player_record *retained_player(application_q3_component_client_adapter *a,qa_actor_id actor)
{
    if(!storage(a)) return NULL;
    application_player_record *found=NULL;
    for(size_t i=0;i<a->application->players->count;++i) {
        application_player_record *p=a->application->players->records+i;
        if(qa_actor_id_equal(p->actor,actor)) {
            if(found) return NULL;
            found=p;
        }
    }
    return found;
}
static application_player_record *player(application_q3_component_client_adapter *a,qa_actor_id actor)
{
    application_player_record *p=retained_player(a,actor);
    return p&&!p->retiring&&!p->deferred&&qa_actors_get(qa_session_actors(a->application->session),actor)?p:NULL;
}
static bool current(void *context,qa_actor_id actor)
{
    application_q3_component_client_adapter *a=context;
    application_player_record *p=player(a,actor);
    if(!p) return false;
    application_provider *source=a->source;
    qa_error e={0};
    if(source->kind==APPLICATION_PROVIDER_Q1) {
        uint32_t slot;
        return qa_q1_native_client_slot(source->state.q1,actor,&slot,&e)&&slot==p->client_slot;
    }
    if(source->kind==APPLICATION_PROVIDER_Q2) {
        qa_builtin_player_info v;
        return qa_q2_player_projection(source->state.q2,actor,&v)&&v.connected&&v.slot==p->client_slot;
    }
    if(source->kind==APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine=source->state.qc.engine;
        uint32_t slot=p->client_slot+1;
        qa_qc_slot_binding binding;
        return engine&&engine->clients&&slot<=engine->max_clients&&
            engine->clients[slot].connected&&engine->clients[slot].spawned&&
            qa_actor_id_equal(engine->clients[slot].actor,actor)&&
            qa_qc_slot(source->state.qc.instance,slot,&binding)&&
            binding.kind!=QA_QC_SLOT_FREE&&qa_actor_id_equal(binding.actor,actor);
    }
    if(source->kind==APPLICATION_PROVIDER_Q3) {
        application_native_q3_wire_client_view v;bool present=false;
        return application_native_q3_wire_client_read(source,p->client_slot,&v,&present,&e)&&present&&
            v.begun&&qa_actor_id_equal(v.actor,actor);
    }
    if(source->kind==APPLICATION_PROVIDER_NATIVE&&source->state.native.q2_engine) {
        struct application_native_q2 *engine=source->state.native.q2_engine;
        if(p->client_slot>=256) return false;
        uint32_t slot=p->client_slot+1;
        qa_native_slot_binding binding;
        return slot<257&&engine->clients[slot].connected&&engine->clients[slot].begun&&
            !engine->clients[slot].disconnect_started&&qa_actor_id_equal(engine->clients[slot].actor,actor)&&
            source->state.native.host&&qa_native_slot(qa_native_host_instance(source->state.native.host),slot,&binding,&e)&&
            binding.kind!=QA_NATIVE_SLOT_FREE&&qa_actor_id_equal(binding.actor,actor);
    }
    struct application_q3_guest *engine=q3g_engine(source);
    uint32_t slot=UINT32_MAX;
    return engine&&engine->game&&engine->game->host&&engine->game->ready&&!engine->game->retired&&
        p->client_slot<64&&engine->clients[p->client_slot].connected&&engine->clients[p->client_slot].begun&&
        !engine->clients[p->client_slot].pending_retirement&&!engine->clients[p->client_slot].disconnect_started&&
        qa_actor_id_equal(engine->clients[p->client_slot].actor,actor)&&
        qa_q3_host_actor_slot(engine->game->host,actor,&slot,&e)&&slot==p->client_slot;
}
static application_player_record *entered(application_q3_component_client_adapter *a,qa_actor_id actor,qa_error *e)
{
    if(!current(a,actor)) {
        application_fail(e,QA_ERROR_NOT_FOUND,"Component client lost its actual full primary actor/slot");return NULL;
    }
    return player(a,actor);
}
static char *info_dictionary(const char *text,qa_error *e)
{
    typedef struct info_pair {const char *key,*value;size_t key_size,value_size;} info_pair;
    size_t length=strlen(text);
    if(length>SIZE_MAX-2) {application_fail(e,QA_ERROR_MEMORY,"Component userinfo dictionary overflows");return NULL;}
    info_pair *pairs=calloc(length/2+2,sizeof(*pairs));
    char *result=malloc(length+2);
    if(!pairs||!result) {free(pairs);free(result);application_fail(e,QA_ERROR_MEMORY,"Retaining actual client info dictionary");return NULL;}
    const char *cursor=text+(*text=='\\');size_t count=0,used=0;
    while(*cursor) {
        const char *separator=strchr(cursor,'\\');if(!separator) break;
        const char *value=separator+1,*end=strchr(value,'\\');
        info_pair pair={cursor,value,(size_t)(separator-cursor),end?(size_t)(end-value):strlen(value)};
        bool duplicate=false;
        for(size_t i=0;i<count;++i)
            if(pairs[i].key_size==pair.key_size&&!memcmp(pairs[i].key,pair.key,pair.key_size)) {duplicate=true;break;}
        if(!duplicate) {
            pairs[count++]=pair;
            result[used++]='\\';memcpy(result+used,pair.key,pair.key_size);used+=pair.key_size;
            result[used++]='\\';memcpy(result+used,pair.value,pair.value_size);used+=pair.value_size;
        }
        if(!end) break;
        cursor=end+1;
    }
    result[used]=0;free(pairs);return result;
}
static char *info_replace(const char *text,const char *key,const char *value,qa_error *e)
{
    char *dictionary=info_dictionary(text,e);if(!dictionary) return NULL;
    size_t length=strlen(dictionary),key_size=strlen(key),value_size=value?strlen(value):0;
    if(length>SIZE_MAX-3||key_size>SIZE_MAX-length-3||value_size>SIZE_MAX-length-key_size-3) {
        free(dictionary);application_fail(e,QA_ERROR_MEMORY,"Component Source info edit exceeds its actual extent");return NULL;
    }
    char *result=malloc(length+key_size+value_size+3);
    if(!result) {free(dictionary);application_fail(e,QA_ERROR_MEMORY,"Retaining physical Source info edit");return NULL;}
    const char *cursor=dictionary;size_t used=0;bool replaced=false;
    while(*cursor) {
        const char *entry=cursor+1,*separator=strchr(entry,'\\');
        const char *end=strchr(separator+1,'\\');size_t span=end?(size_t)(end-cursor):strlen(cursor);
        bool match=(size_t)(separator-entry)==key_size&&!memcmp(entry,key,key_size);
        if(match) {
            replaced=true;
            if(value) {
                result[used++]='\\';memcpy(result+used,key,key_size);used+=key_size;
                result[used++]='\\';memcpy(result+used,value,value_size);used+=value_size;
            }
        } else {memcpy(result+used,cursor,span);used+=span;}
        if(!end) break;
        cursor=end;
    }
    if(value&&!replaced) {
        result[used++]='\\';memcpy(result+used,key,key_size);used+=key_size;
        result[used++]='\\';memcpy(result+used,value,value_size);used+=value_size;
    }
    result[used]=0;free(dictionary);return result;
}
static bool color_team(const char *text,int32_t *out,qa_error *e)
{
    double number;
    if(!text||!qa_parse_ecmascript_number((qa_bytes){(const uint8_t *)text,strlen(text)},&number,e)) return false;
    if(!isfinite(number)||trunc(number)!=number||number<1||number>14)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component team has no original Quake color command");
    *out=(int32_t)number;return true;
}
static bool userinfo(void *context,qa_actor_id actor,const char **out,qa_error *e)
{
    application_q3_component_client_adapter *a=context;
    if(!out) return application_fail(e,QA_ERROR_ARGUMENT,"Component userinfo needs its borrowed output");
    application_player_record *p=entered(a,actor,e);
    if(!p) return false;
    application_provider *source=a->source;bool ok;
    ++a->calls;
    if(source->kind==APPLICATION_PROVIDER_Q1) {
        qa_buffer bytes={0};
        ok=qa_q1_source_client_userinfo_read(source->state.q1,actor,true,&bytes,e);
        if(ok) {qa_buffer_free(&a->userinfo);a->userinfo=bytes;*out=(const char *)bytes.data;}
    } else if(source->kind==APPLICATION_PROVIDER_Q2)
        ok=qa_q2_player_userinfo_read(source->state.q2,actor,out,e);
    else if(source->kind==APPLICATION_PROVIDER_Q3)
        ok=application_native_q3_wire_userinfo_read(source,p->client_slot,out,e);
    else if(source->kind==APPLICATION_PROVIDER_NATIVE&&source->state.native.q2_engine) {
        *out=source->state.native.q2_engine->clients[p->client_slot+1].userinfo;ok=true;
    } else if(source->kind==APPLICATION_PROVIDER_QC) {
        char *copy=info_dictionary(p->userinfo?p->userinfo:"",e);ok=copy!=NULL;
        if(ok) {qa_buffer_free(&a->userinfo);a->userinfo=(qa_buffer){(uint8_t *)copy,strlen(copy)};*out=copy;}
    }
    else {*out=q3g_engine(source)->clients[p->client_slot].userinfo;
        if(!*out) *out="";
        ok=true;}
    --a->calls;
    return ok&&(current(a,actor)||application_fail(e,QA_ERROR_ARGUMENT,"Component userinfo changed its physical client"));
}
static bool set_userinfo(void *context,qa_actor_id actor,const char *text,qa_error *e)
{
    application_q3_component_client_adapter *a=context;
    if(!text) return application_fail(e,QA_ERROR_ARGUMENT,"Component stored userinfo is absent");
    application_player_record *p=entered(a,actor,e);
    if(!p) return false;
    application_provider *source=a->source;bool ok;
    ++a->calls;
    if(source->kind==APPLICATION_PROVIDER_Q1) ok=qa_q1_source_client_userinfo_storage(source->state.q1,actor,text,e);
    else if(source->kind==APPLICATION_PROVIDER_Q2) ok=qa_q2_player_userinfo_storage(source->state.q2,actor,text,e);
    else if(source->kind==APPLICATION_PROVIDER_Q3) ok=application_native_q3_wire_userinfo(source,p->client_slot,text,e);
    else if(source->kind==APPLICATION_PROVIDER_NATIVE&&source->state.native.q2_engine) {
        application_native_q2_client *client=source->state.native.q2_engine->clients+p->client_slot+1;
        size_t size=strlen(text);
        ok=size<sizeof(client->userinfo);
        if(ok) {memcpy(client->userinfo,text,size+1);client->userinfo_present=true;}
        else application_fail(e,QA_ERROR_FORMAT,"Component userinfo exceeds actual original Q2 storage");
    } else {
        char *copy=source->kind==APPLICATION_PROVIDER_QC?info_dictionary(text,e):q3g_copy_text(text,e);ok=copy!=NULL;
        if(ok&&source->kind==APPLICATION_PROVIDER_QC) {free(p->userinfo);p->userinfo=copy;}
        else if(ok) {q3g_client *client=q3g_engine(source)->clients+p->client_slot;free(client->userinfo);client->userinfo=copy;}
    }
    --a->calls;
    return ok&&(current(a,actor)||application_fail(e,QA_ERROR_ARGUMENT,"Component storage changed its physical client"));
}
static bool command(void *context,qa_actor_id actor,const qa_q3_player *state,qa_q3_usercmd *out,qa_error *e)
{
    application_q3_component_client_adapter *a=context;
    if(!entered(a,actor,e)) return false;
    ++a->calls;
    bool ok=application_control_last_mod_command(a->application,actor,state,out,e);
    --a->calls;
    return ok&&(current(a,actor)||application_fail(e,QA_ERROR_ARGUMENT,"Component command changed its actual client"));
}
static bool active_command(void *context,qa_actor_id actor,const application_q3_mod_inputs *input,
    const qa_q3_player *state,qa_q3_usercmd *out,qa_error *e)
{
    application_q3_component_client_adapter *a=context;
    if(!entered(a,actor,e)) return false;
    ++a->calls;
    bool ok=application_control_mod_usercmd(a->application,actor,input,state,out,e);
    --a->calls;
    return ok&&(current(a,actor)||application_fail(e,QA_ERROR_ARGUMENT,"Component active command changed its client"));
}
static bool drop(void *context,qa_actor_owner component,qa_actor_id actor,const char *reason,qa_error *e)
{
    application_q3_component_client_adapter *a=context;
    application_player_record *p=entered(a,actor,e);
    if(!p) return false;
    if(!reason||!component||!qa_strings_cstr(qa_session_strings(a->application->session),component))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component drop lost its actual namespace or reason");
    component_drop *request=calloc(1,sizeof(*request));
    if(!request) return application_fail(e,QA_ERROR_MEMORY,"Retaining actual component client drop");
    request->reason=q3g_copy_text(reason,e);
    if(!request->reason) {free(request);return false;}
    request->actor=actor;request->slot=p->client_slot;request->component=component;
    if(a->tail) a->tail->next=request;else a->drops=request;
    a->tail=request;return true;
}
static bool source_score(application_q3_component_client_adapter *a,qa_actor_id actor,double *out,qa_error *e)
{
    application_provider *source=a->source;
    if(source->kind==APPLICATION_PROVIDER_Q1) {
        qa_q1_source_client_view view;
        if(!qa_q1_source_client_read(source->state.q1,actor,&view))
            return application_fail(e,QA_ERROR_NOT_FOUND,"Component score lost its actual Q1 client");
        *out=view.frags;return true;
    }
    if(source->kind==APPLICATION_PROVIDER_QC) {
        int32_t reference;float score;struct application_qc_state *engine=source->state.qc.engine;
        if(!application_qc_reference(engine,actor,&reference,e)||
            !application_qc_float(engine,reference,"frags",&score,e)) return false;
        *out=score;return true;
    }
    if(source->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_player_info view;
        if(!qa_q2_player_read(source->state.q2,actor,&view))
            return application_fail(e,QA_ERROR_NOT_FOUND,"Component score lost its actual Q2 player state");
        *out=view.score;return true;
    }
    int32_t score;
    if(source->kind==APPLICATION_PROVIDER_Q3) {
        application_player_record *p=player(a,actor);
        if(!p||!qa_q3_wire_client_source_score_read(source->state.q3,p->client_slot,&score,e)) return false;
    } else {
        struct application_q3_guest *engine=q3g_engine(source);
        if(!engine||!engine->game||!engine->game->weapons)
            return application_fail(e,QA_ERROR_UNSUPPORTED,"Component score needs a genuine Source match declaration");
        if(!application_q3_weapons_score(engine->game->weapons,actor,&score,e)) return false;
    }
    *out=score;return true;
}
static bool source_set_score(application_q3_component_client_adapter *a,qa_actor_id actor,double score,qa_error *e)
{
    application_provider *source=a->source;
    if(!isfinite(score)) return application_fail(e,QA_ERROR_ARGUMENT,"Component Source score must be finite");
    if(source->kind==APPLICATION_PROVIDER_Q1)
        return qa_q1_source_client_set_score(source->state.q1,actor,(float)qa_source_fround(score),e);
    if(source->kind==APPLICATION_PROVIDER_QC) {
        int32_t reference;struct application_qc_state *engine=source->state.qc.engine;
        return application_qc_reference(engine,actor,&reference,e)&&
            application_qc_set_float(engine,reference,"frags",(float)qa_source_fround(score),e);
    }
    if(trunc(score)!=score||score<INT32_MIN||score>INT32_MAX)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component Source score requires an int32");
    struct application_q3_guest *engine=q3g_engine(source);
    if(engine&&engine->game&&engine->game->weapons)
        return application_q3_weapons_set_score(engine->game->weapons,actor,(int32_t)score,e);
    if((source->kind!=APPLICATION_PROVIDER_Q2&&source->kind!=APPLICATION_PROVIDER_Q3)||
        !a->application->modes||!a->application->primary_mode_ready)
        return application_fail(e,QA_ERROR_UNSUPPORTED,"Component score write has no actual Source scoring owner");
    if(!qa_modes_set_score(a->application->modes,a->application->primary_mode,actor,(int32_t)score,e)) return false;
    double actual;
    return source_score(a,actor,&actual,e)&&(actual==score||
        application_fail(e,QA_ERROR_ARGUMENT,"Component score write did not reach the physical Source state"));
}
static bool source_set_team(application_q3_component_client_adapter *a,qa_actor_id actor,qa_team_id team,qa_error *e)
{
    application_provider *source=a->source;qa_combat_state current_team;
    if(!qa_combat_read(a->application->combat,actor,&current_team,e)) return false;
    if(current_team.team==team) return true;
    const char *text=team?qa_strings_cstr(qa_session_strings(a->application->session),team):NULL;
    if(team&&!text) return application_fail(e,QA_ERROR_ARGUMENT,"Component team lost its actual identity");
    struct application_q3_guest *engine=q3g_engine(source);
    if(engine&&engine->game&&engine->game->weapons) {
        const application_q3_weapon_team_command *command;
        if(!application_q3_weapons_team_command(engine->game->weapons,actor,team,&command,e)) return false;
        if(command->argument_count>SIZE_MAX/sizeof(const char *))
            return application_fail(e,QA_ERROR_MEMORY,"Component team command exceeds its actual vector extent");
        const char **arguments=malloc(command->argument_count*sizeof(*arguments));
        if(!arguments) return application_fail(e,QA_ERROR_MEMORY,"Retaining actual Source team command");
        for(size_t i=0;i<command->argument_count;++i)
            arguments[i]=qa_strings_cstr(qa_session_strings(a->application->session),command->arguments[i]);
        bool ok=application_q3_guest_client_command_vector(source,actor,arguments,command->argument_count,e);
        free(arguments);return ok;
    }
    if(source->kind==APPLICATION_PROVIDER_Q3) {
        const char *argument=!team?"free":!strcmp(text,"team:red")?"red":!strcmp(text,"team:blue")?"blue":NULL;
        return argument?application_native_q3_client_set_team(source,actor,argument,e):
            application_fail(e,QA_ERROR_ARGUMENT,"Component team has no original Q3 command");
    }
    if(source->kind==APPLICATION_PROVIDER_Q1) {
        qa_q1_options options;double source_seconds;qa_q1_source_client_view view;int32_t color;
        if(!qa_q1_source_respawn_options_read(source->state.q1,&options,&source_seconds,e)||
            !qa_q1_source_client_read(source->state.q1,actor,&view)) return false;
        if(options.program==QA_Q1_CTF) {
            color=text&&!strcmp(text,"team:red")?5:text&&!strcmp(text,"team:blue")?14:0;
            if(!color) return application_fail(e,QA_ERROR_ARGUMENT,"Component team has no actual Q1 CTF color command");
        } else if(!color_team(text,&color,e)) return false;
        return qa_q1_source_client_colors(source->state.q1,actor,view.shirt,color-1,e);
    }
    if(source->kind==APPLICATION_PROVIDER_QC) {
        struct application_qc_state *qc=source->state.qc.engine;
        if(source->state.qc.qualified)
            return application_fail(e,QA_ERROR_UNSUPPORTED,"Component QC team needs its actual declared team aliases");
        application_player_record *p=player(a,actor);if(!p) return false;
        char *info;
        if(qc->profile==QA_QC_QUAKEWORLD) {
            if(text&&strpbrk(text,"\\\"\n\r"))
                return application_fail(e,QA_ERROR_ARGUMENT,"Component QW team has invalid Source info bytes");
            info=info_replace(p->userinfo?p->userinfo:"","team",text,e);
        } else {
            int32_t color,reference;char value[4];
            if(!color_team(text,&color,e)||!application_qc_reference(qc,actor,&reference,e)) return false;
            snprintf(value,sizeof(value),"%d",color-1);
            info=info_replace(p->userinfo?p->userinfo:"","bottomcolor",value,e);
            if(!info) return false;
            if(!application_qc_set_float(qc,reference,"team",(float)color,e)) {free(info);return false;}
        }
        if(!info) return false;
        p=player(a,actor);if(!p) {free(info);return application_fail(e,QA_ERROR_ARGUMENT,"Component QC team replaced its actual client");}
        free(p->userinfo);p->userinfo=info;
        return application_qc_client_userinfo(source,actor,e)&&application_client_userinfo_changed(a->application,actor,e);
    }
    if(source->kind==APPLICATION_PROVIDER_Q2) {
        qa_application *app=a->application;qa_mode_view mode;
        if(!app->modes||!app->primary_mode_ready||application_mode_provider(app,app->primary_mode)!=source||
            !qa_modes_read(app->modes,app->primary_mode,&mode,e)) return false;
        if(mode.rules.source!=QA_MODE_Q2_CTF&&mode.rules.source!=QA_MODE_LMCTF)
            return application_fail(e,QA_ERROR_ARGUMENT,"Component Q2 team has no genuine CTF Source command");
        const char *argument=text&&!strcmp(text,"team:red")?"red":text&&!strcmp(text,"team:blue")?"blue":NULL;
        if(!argument) return application_fail(e,QA_ERROR_ARGUMENT,"Component Q2 team has no Source command argument");
        const char *arguments[]={"team",argument};bool handled=false;
        qa_command_invocation command={.context={.owner=source->owner,.actor=actor,.dialect=QA_CONSOLE_Q2,
            .origin=QA_COMMAND_SERVER},.argc=2,.argv=arguments,.args_text=argument};
        return qa_modes_console_command(app->modes,app->primary_mode,actor,&command,&handled,e)&&
            (handled||application_fail(e,QA_ERROR_UNSUPPORTED,"Actual Q2 CTF Source did not handle its team command"));
    }
    return application_fail(e,QA_ERROR_UNSUPPORTED,"Component team needs its physical Source command authority");
}
bool application_q3_component_client_match_read(void *context,qa_actor_id actor,qa_string_id *team,double *score,qa_error *e)
{
    application_q3_component_client_adapter *a=context;qa_combat_state state;double points;
    if(!team||!score)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component match needs its actual team and score outputs");
    if(!entered(a,actor,e)) return false;
    ++a->calls;
    bool ok=qa_combat_read(a->application->combat,actor,&state,e)&&source_score(a,actor,&points,e);
    --a->calls;
    if(!ok||!current(a,actor)) return ok?application_fail(e,QA_ERROR_ARGUMENT,"Component match read changed its source client"):false;
    *team=state.team;*score=points;return true;
}
bool application_q3_component_client_match_write(void *context,qa_actor_id actor,bool team,qa_string_id value,double score,qa_error *e)
{
    application_q3_component_client_adapter *a=context;
    if(!entered(a,actor,e)) return false;
    ++a->calls;
    bool ok=team?source_set_team(a,actor,value,e):source_set_score(a,actor,score,e);
    --a->calls;
    return ok&&(current(a,actor)||application_fail(e,QA_ERROR_ARGUMENT,"Component match write changed its client"));
}
bool application_q3_component_client_adapter_create(qa_application *app,application_provider *source,qa_world *world,
    application_q3_component_client_adapter **out,qa_error *e)
{
    if(!app||!source||source->application!=app||!source->constructed||!source->owner||!world||!out||*out)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component clients need their actual physical WORLD provider");
    application_q3_component_client_adapter *a=calloc(1,sizeof(*a));
    if(!a) return application_fail(e,QA_ERROR_MEMORY,"Retaining component primary client services");
    *a=(application_q3_component_client_adapter){.application=app,.source=source,.owner=source->owner,.world=world};
    *out=a;return true;
}
application_q3_component_clients application_q3_component_client_adapter_services(application_q3_component_client_adapter *a)
{
    return (application_q3_component_clients){.context=a,.current=current,.userinfo=userinfo,.set_userinfo=set_userinfo,
        .command=command,.active_command=active_command,.drop=drop};
}
bool application_q3_component_client_adapter_transport_bind(application_q3_component_client_adapter *a,
    void *context,bool (*callback)(void *,qa_actor_owner,qa_actor_id,const char *,qa_error *),qa_error *e)
{
    if(!a||!callback||a->calls||a->draining||a->drops||
        (a->transport_drop&&(a->transport_drop!=callback||a->transport_context!=context)))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component transport needs its actual retained callback owner");
    a->transport_context=context;a->transport_drop=callback;return true;
}
bool application_q3_component_client_adapter_idle(const application_q3_component_client_adapter *a)
{ return !a||(!a->calls&&!a->draining&&!a->drops); }
bool application_q3_component_client_drop_read(const application_q3_component_client_adapter *a,
    application_q3_component_client_drop *out,bool *present,qa_error *e)
{
    if(!out||!present||!storage(a)||a->draining)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component drop read lost its retained physical namespace");
    *out=(application_q3_component_client_drop){0};*present=a->drops!=NULL;
    if(a->drops) {
        const component_drop *request=a->drops;
        *out=(application_q3_component_client_drop){.component=request->component,.source=a->owner,
            .actor=request->actor,.source_slot=request->slot,.reason=request->reason};
    }
    return true;
}
bool application_q3_component_client_adapter_destroy(application_q3_component_client_adapter **pointer,qa_error *e)
{
    if(!pointer||!*pointer) return true;
    application_q3_component_client_adapter *a=*pointer;
    if(a->calls||a->draining||a->drops)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component client services retain entered calls or requested drops");
    qa_buffer_free(&a->userinfo);free(a);
    *pointer=NULL;return true;
}

static bool native_disconnected(application_provider *source,const component_drop *request,
    bool *disconnected,qa_error *e)
{
    uint32_t slot;qa_q3_native_client client;
    application_native_q3_wire_client_view wire;bool admitted;
    if(!qa_q3_native_client_slot(source->state.q3,request->actor,&slot,e)) return false;
    if(slot!=request->slot)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component native disconnect lost its retained actor/slot");
    if(!qa_q3_client_slot_read(source->state.q3,slot,&client,e)||
        !application_native_q3_wire_client_admission_read(source,slot,&wire,&admitted,e)) return false;
    *disconnected=client.connected==QA_Q3_CLIENT_DISCONNECTED&&!admitted;
    return true;
}

bool application_q3_component_client_adapter_drain(application_q3_component_client_adapter *a,qa_error *e)
{
    if(!a||!a->drops) return true;
    qa_application *app=a->application;
    if(!storage(a)||a->calls||a->draining||!qa_session_safe(app->session)||
        !qa_world_idle(app->world)||!qa_modes_idle(app->modes)||!qa_combat_idle(app->combat))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component drops require returned physical source execution");
    a->draining=true;bool ok=true;
    while(ok&&a->drops) {
        component_drop *request=a->drops;
        if(!qa_actors_get(qa_session_actors(app->session),request->actor)) {
            if(!application_players_component_retire(app,a->source,request->actor,e)) {ok=false;break;}
            goto consumed;
        }
        application_player_record *p=retained_player(a,request->actor);
        if(!p||p->client_slot!=request->slot) {
            ok=application_fail(e,QA_ERROR_ARGUMENT,"Queued component drop changed its full primary client identity");break;
        }
        application_provider *source=a->source;
        if(!request->transport&&source->kind!=APPLICATION_PROVIDER_Q3&&!q3g_engine(source)) {
            if(p->remote) {
                if(!a->transport_drop) {ok=application_fail(e,QA_ERROR_UNSUPPORTED,"Component remote drop has no actual transport owner");break;}
                if(!a->transport_drop(a->transport_context,request->component,request->actor,request->reason,e)) {ok=false;break;}
            }
            request->transport=true;
        }
        if(!request->rankings) {
            ok=source->kind==APPLICATION_PROVIDER_Q3||application_rankings_disconnect(app,request->actor,e);
            if(!ok) break;
            request->rankings=true;
        }
        if(!request->bots) {
            ok=source->kind==APPLICATION_PROVIDER_Q3||application_bots_client_shutdown(app,request->actor,false,e);
            if(!ok) break;
            request->bots=true;
        }
        if(!request->source) {
            if(source->kind==APPLICATION_PROVIDER_Q3) {
                bool disconnected;
                ok=native_disconnected(source,request,&disconnected,e);
                if(ok&&!disconnected)
                    ok=application_native_q3_wire_drop(source,request->slot,request->reason,e)&&
                        application_native_q3_clients_drain(app,e);
            }
            else if(source->kind==APPLICATION_PROVIDER_QC)
                ok=application_qc_disconnect_player(source,request->actor,e);
            else if(source->kind==APPLICATION_PROVIDER_Q2)
                ok=qa_q2_player_disconnect(source->state.q2,request->actor,e);
            else if(source->kind==APPLICATION_PROVIDER_NATIVE&&source->state.native.q2_engine)
                ok=application_native_q2_client_disconnect(source,request->slot+1,e);
            else if(q3g_engine(source)) {
                q3g_role *game=q3g_engine(source)->game;
                if(!request->transport) {
                    ok=!game->server.drop_client||game->server.drop_client(game->server.context,
                        request->slot,request->reason,e);
                    if(!ok) break;
                    request->transport=true;
                }
                if(qa_actors_get(qa_session_actors(app->session),request->actor))
                    ok=application_guest_client_drop(source,request->slot,request->reason,e)&&
                        application_guest_clients_drain(source,e);
                if(ok&&q3g_engine(source)->clients[request->slot].pending_retirement)
                    ok=application_fail(e,QA_ERROR_ARGUMENT,"Component drop retains actual Source client retirement leases");
            }
            if(!ok) break;
            request->source=true;
        }
        if(!qa_actors_get(qa_session_actors(app->session),request->actor)) {
            if(!application_players_component_retire(app,source,request->actor,e)) {ok=false;break;}
            goto consumed;
        }
        p=retained_player(a,request->actor);
        if(!p) {ok=application_fail(e,QA_ERROR_ARGUMENT,"Component drop lost its retained canonical client");break;}
        if(!request->character) {
            if(p->character&&p->character!=source&&p->character->kind==APPLICATION_PROVIDER_Q2)
                ok=qa_q2_player_disconnect(p->character->state.q2,request->actor,e);
            else if(p->character&&p->character!=source&&p->character->kind==APPLICATION_PROVIDER_NATIVE&&
                p->character->state.native.q2_engine)
                ok=application_native_q2_actor_disconnect(p->character,request->actor,e);
            if(!ok) break;
            request->character=true;
        }
        ok=source->kind==APPLICATION_PROVIDER_Q3?
            application_players_native_q3_retire(app,source,request->actor,e):
            application_players_component_retire(app,source,request->actor,e);
        if(!ok) break;
consumed:
        a->drops=request->next;if(!a->drops) a->tail=NULL;
        free(request->reason);free(request);
    }
    a->draining=false;return ok;
}
