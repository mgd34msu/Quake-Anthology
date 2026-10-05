#include "bot_world_bind.h"
#include "bots_private.h"
#include "map_players_private.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q2_bots.h"
#include "qa/application_players.h"
#include "qa/text.h"
#include "bots_transport.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct application_bot_world_binding {
    application_bots *bots;
    application_provider *source;
    application_bot_world *holder;
    qa_q2_connection_result connection;
    qa_q2_player_info client;
};
static bool source_live(application_bot_world_binding *binding,qa_error *error) {
    application_provider *source=binding->source;
    return source && source->constructed && source->attached && !source->close_pending &&
        source==application_bot_source(binding->bots)?true:
        application_fail(error,QA_ERROR_NOT_FOUND,"shared bot source provider retired or was replaced");
}
static application_player_record *player(application_bot_world_binding *binding,qa_actor_id actor) {
    struct application_player_roster *roster=binding->bots->application->players;
    for(size_t i=0;roster && i<roster->count;++i)
        if(!roster->records[i].retiring && qa_actor_id_equal(roster->records[i].actor,actor)) return roster->records+i;
    return NULL;
}
static qa_actor_id bot_actor(void *context,int32_t client) {
    application_bot_world_binding *binding=context;
    struct application_player_roster *roster=binding->bots->application->players;
    for(size_t i=0;client>=0 && roster && i<roster->count;++i) {
        application_player_record *record=roster->records+i;
        if(!record->retiring && record->bot && record->client_slot==(uint32_t)client) return record->actor;
    }
    return (qa_actor_id){0};
}
static bool metadata(void *context,qa_actor_id actor,application_bot_world_metadata *out,
    bool *found,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    application_provider *source=binding->source;
    application_provider *arsenal=application_provider_for(binding->bots->application,actor,QA_ROLE_ARSENAL,NULL);
    application_provider *q2=source->kind==APPLICATION_PROVIDER_Q2?source:
        arsenal && arsenal->kind==APPLICATION_PROVIDER_Q2?arsenal:NULL;
    qa_strings *strings=qa_session_strings(binding->bots->application->session);
    *found=false;
    if(q2) {
        qa_q2_bot_entity actual;
        if(!qa_q2_bot_entity_read(q2->state.q2,actor,&actual,error)) return false;
        if(actual.present) {
            const char *model=actual.model?qa_strings_cstr(strings,actual.model):"";
            if(!model) return false;
            int32_t maximum=qa_source_float_to_i32(actual.max_health);
            *out=(application_bot_world_metadata){.model=model,.classname=actual.classname,
                .frame=actual.frame,.max_health=maximum,.hidden=actual.hidden,.worldspawn=actual.worldspawn};
            *found=true;return true;
        }
        if(source->kind==APPLICATION_PROVIDER_Q2) return true;
    }
    qa_q1_bot_entity actual;
    if(!qa_q1_bot_entity_read(source->state.q1,actor,&actual,error)) return false;
    if(!actual.present) return true;
    int32_t maximum=qa_source_float_to_i32(actual.max_health);
    const char *model=actual.model?qa_strings_cstr(strings,actual.model):"";
    if(!model) return application_fail(error,QA_ERROR_FORMAT,"shared Q1 metadata has no actual source model text");
    *out=(application_bot_world_metadata){.model=model,.classname=actual.classname,.frame=actual.frame,
        .max_health=maximum,.worldspawn=actual.worldspawn};*found=true;return true;
}
static bool movement(void *context,qa_actor_id actor,application_bot_world_movement *out,
    bool *found,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    qa_application_control_view control;
    *found=qa_application_control_read(binding->bots->application,actor,&control);
    if(!*found) return true;
    application_player_record *record=player(binding,actor);
    if(!record || record->client_slot>INT32_MAX)
        return application_fail(error,QA_ERROR_FORMAT,"shared bot selected movement has no actual source player client");
    int32_t height=qa_source_float_to_i32(control.view_height);
    *out=(application_bot_world_movement){.source_client=(int32_t)record->client_slot,
        .view_height=height,.view_angles=control.view_angles};return true;
}
static bool client(void *context,qa_actor_id actor,application_bot_world_client *out,
    bool *found,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    if(binding->source->kind==APPLICATION_PROVIDER_Q2) {
        *found=qa_q2_player_read(binding->source->state.q2,actor,&binding->client);
        if(*found) *out=(application_bot_world_client){.name=binding->client.name,.skin=binding->client.skin,
            .score=binding->client.score,.spectator=binding->client.spectator};
        return true;
    }
    qa_q1_source_client_view actual;
    *found=qa_q1_source_client_read(binding->source->state.q1,actor,&actual);
    if(!*found) return true;
    int32_t score=qa_source_float_to_i32(actual.frags);
    qa_application *app=binding->bots->application;
    application_provider *appearance=application_provider_for(app,actor,QA_ROLE_SKIN,NULL);
    if(!actual.name || !appearance || !appearance->launch)
        return application_fail(error,QA_ERROR_NOT_FOUND,"shared Q1 client has no actual selected appearance owner");
    *out=(application_bot_world_client){.name=actual.name,.skin=appearance->launch->selection.instance,
        .score=score,.spectator=actual.observer};return true;
}
static bool combat(void *context,qa_actor_id actor,application_bot_world_combat *out,
    bool *found,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    qa_combat_state state;qa_error local={0};
    *found=qa_combat_read(binding->bots->application->combat,actor,&state,&local);
    if(!*found) {if(local.code!=QA_ERROR_NOT_FOUND) {if(error) *error=local;return false;}return true;}
    return application_bot_integer(state.health,&out->health,error) &&
        application_bot_integer(state.armor.regular.kind==QA_ARMOR_NONE?0:state.armor.regular.points,&out->armor,error);
}
static bool brush(void *context,qa_actor_id actor,bool *out,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    qa_physics_properties state;
    *out=application_control_physics_read(binding->bots->application,actor,&state) && state.solid==QA_PHYSICS_BRUSH;
    return true;
}
static bool weapon(void *context,qa_actor_id actor,int32_t *weapon,int32_t *phase,qa_error *error) {
    application_bot_world_binding *binding=context;
    return source_live(binding,error) && application_bot_source_weapon(binding->bots,actor,weapon,phase,error);
}
static qa_actor_id world_actor(void *context) {
    application_bot_world_binding *binding=context;
    if(binding->source->kind==APPLICATION_PROVIDER_Q1) {
        return qa_q1_bot_world_actor(binding->source->state.q1);
    }
    return qa_q2_bot_world_actor(binding->source->state.q2);
}
static int32_t milliseconds(uint64_t nanoseconds) {
    uint32_t word=(uint32_t)(nanoseconds/UINT64_C(1000000));
    int32_t value;memcpy(&value,&word,sizeof(value));return value;
}
static bool clock(void *context,int32_t *time,int32_t *intermission,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    *time=milliseconds(qa_session_elapsed(binding->bots->application->session));
    if(binding->source->kind==APPLICATION_PROVIDER_Q2) {
        uint64_t source_time,started;bool active;
        if(!qa_q2_bot_clock_read(binding->source->state.q2,&source_time,&active,&started,error)) return false;
        *intermission=active?milliseconds(started):0;
    } else {
        double source_time,exit_after;bool active;
        if(!qa_q1_bot_clock_read(binding->source->state.q1,&source_time,&active,&exit_after,error)) return false;
        *intermission=0;
        if (active) {
            double elapsed=(exit_after-5)*1000.0;
            if (!isfinite(elapsed) || elapsed< -0x1p63 || elapsed>=0x1p63)
                return application_fail(error,QA_ERROR_ARGUMENT,"Q1 bot intermission exceeds its native clock domain");
            uint32_t word=(uint32_t)(int64_t)elapsed;
            memcpy(intermission,&word,sizeof(*intermission));
        }
    }
    return true;
}
static bool print(void *context,const char *text,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error) || !text) return false;
    qa_application *app=binding->bots->application;
    qa_command_context request={.owner=binding->source->owner,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_SERVER},actual;
    if(!qa_application_capture_command_context(app,&request,&actual,error)) return false;
    qa_console_emit(app->console,&actual,text);return true;
}
static bool memory_debug(void *context,int32_t *out,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    *out=0;return true;
}
static bool q2_connect(void *context,const char *userinfo,bool bot,application_bot_world_connect *out,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error) || binding->source->kind!=APPLICATION_PROVIDER_Q2) return false;
    if(!qa_q2_player_connect(binding->source->state.q2,userinfo,bot,&binding->connection,error)) return false;
    *out=(application_bot_world_connect){.allowed=binding->connection.allowed,
        .userinfo=binding->connection.userinfo,.reason=binding->connection.reason};return true;
}
static bool userinfo_changed(void *context,qa_actor_id actor,const char *userinfo,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    if(binding->source->kind==APPLICATION_PROVIDER_Q1)
        return qa_q1_source_client_userinfo(binding->source->state.q1,actor,userinfo,error);
    qa_q2_player_info actual;
    if(!qa_q2_player_read(binding->source->state.q2,actor,&actual)) return true;
    return qa_q2_player_userinfo(binding->source->state.q2,actor,userinfo,error);
}
static bool bot_connect(void *context,uint32_t slot,bool restart,bool *accepted,qa_error *error) {
    application_bot_world_binding *binding=context;
    return source_live(binding,error) && application_bots_shared_connect(binding->bots,slot,restart,accepted,error);
}
static bool bot_begin(void *context,uint32_t slot,qa_error *error) {
    application_bot_world_binding *binding=context;qa_actor_id actor=bot_actor(binding,(int32_t)slot);
    if(!source_live(binding,error) || !actor.registry)
        return application_fail(error,QA_ERROR_NOT_FOUND,"shared bot Begin has no actual connected actor");
    qa_application_control_view actual;
    if(!qa_application_control_read(binding->bots->application,actor,&actual))
        return application_fail(error,QA_ERROR_NOT_FOUND,"shared bot Begin has no actual selected view");
    int32_t weapon,phase;
    return application_bot_source_weapon(binding->bots,actor,&weapon,&phase,error) &&
        qa_bots_source_begin(binding->bots->population,actor,actual.view_angles,weapon,error);
}
static bool bot_drop(void *context,uint32_t slot,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    qa_actor_id actor=bot_actor(binding,(int32_t)slot);
    return (!actor.registry || application_players_bot_detach(binding->bots->application,actor,error)) &&
        application_bot_transport_close(binding->bots->transport,slot,error);
}
static bool q2_activate(void *context,qa_actor_id actor,qa_error *error) {
    application_bot_world_binding *binding=context;
    return source_live(binding,error) && qa_q2_bot_activate(binding->source->state.q2,actor,error);
}
static bool exit_level(void *context,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    if(binding->source->kind==APPLICATION_PROVIDER_Q2)
        return qa_q2_players_end_deathmatch_level(binding->source->state.q2,error);
    return qa_q1_bot_exit_level(binding->source->state.q1,
        (double)qa_session_elapsed(binding->bots->application->session)/1e9,false,error);
}
static bool console(void *context,const char *text,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    qa_command_context request={.owner=binding->source->owner,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_SERVER};
    return qa_console_insert(binding->bots->application->console,&request,text,error);
}
static bool message(void *context,uint32_t slot,const char *text,qa_error *error) {
    application_bot_world_binding *binding=context;int32_t signed_slot;memcpy(&signed_slot,&slot,sizeof(signed_slot));
    return source_live(binding,error) && application_bot_transport_message(binding->bots->transport,signed_slot,text,error);
}
static bool random(void *context,float *out,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    if(binding->source->kind==APPLICATION_PROVIDER_Q2) return qa_q2_game_random(binding->source->state.q2,out,error);
    *out=qa_q1_game_random(binding->source->state.q1);return true;
}
bool application_bot_world_binding_create(application_bots *bots,application_bot_world_binding **out,qa_error *error) {
    application_provider *source=bots?application_bot_source(bots):NULL;
    if(!source || (source->kind!=APPLICATION_PROVIDER_Q1 && source->kind!=APPLICATION_PROVIDER_Q2) || !out || *out)
        return application_fail(error,QA_ERROR_ARGUMENT,"shared bot binding requires its actual Q1 or Q2 source");
    application_bot_world_binding *binding=calloc(1,sizeof(*binding));
    if(!binding) return application_fail(error,QA_ERROR_MEMORY,"allocating actual shared bot source binding");
    binding->bots=bots;binding->source=source;*out=binding;return true;
}
void application_bot_world_binding_destroy(application_bot_world_binding *binding) {free(binding);}
void application_bot_world_binding_holder(application_bot_world_binding *binding,application_bot_world *holder) {binding->holder=holder;}
bool application_bot_world_binding_services(application_bot_world_binding *binding,
    application_bot_world_services *out,qa_error *error) {
    if(!binding || !out || !source_live(binding,error)) return false;
    uint32_t maximum;
    bool okay=binding->source->kind==APPLICATION_PROVIDER_Q1?
        qa_q1_bot_max_clients(binding->source->state.q1,&maximum,error):
        qa_q2_bot_max_clients(binding->source->state.q2,&maximum,error);
    size_t models=qa_bsp_record_count(&binding->bots->geometry,QA_BSP_MODELS);
    if(!okay || models>INT32_MAX) return false;
    *out=(application_bot_world_services){.context=binding,.session=binding->bots->application->session,
        .world=binding->bots->application->world,.source=binding->source->kind==APPLICATION_PROVIDER_Q1?
            APPLICATION_BOT_WORLD_Q1:APPLICATION_BOT_WORLD_Q2,.max_clients=maximum,.base_model_count=(uint32_t)models,
        .metadata=metadata,.movement=movement,.client=client,.combat=combat,.brush=brush,.weapon=weapon,
        .world_actor=world_actor,.bot_actor=bot_actor,.clock=clock,.print=print,.memory_debug=memory_debug,
        .q2_connect=q2_connect,.userinfo_changed=userinfo_changed,.bot_connect=bot_connect,.bot_begin=bot_begin,
        .bot_drop=bot_drop,.q2_activate=q2_activate,.exit_level=exit_level,.console=console,.message=message,.random=random};
    return true;
}
bool application_bot_world_binding_transport_client(void *context,qa_actor_id actor,uint32_t *out,qa_error *error) {
    application_bot_world_binding *binding=context;
    if(!source_live(binding,error)) return false;
    application_player_record *record=player(binding,actor);
    if(!record || !record->bot || record->remote)
        return application_fail(error,QA_ERROR_NOT_FOUND,"local bot connection lacks its actual roster owner");
    if(binding->source->kind==APPLICATION_PROVIDER_Q1)
        return qa_q1_native_client_slot_prepared(binding->source->state.q1,actor,out,error);
    qa_q2_player_info actual;
    if(!qa_q2_player_read(binding->source->state.q2,actor,&actual))
        return application_fail(error,QA_ERROR_NOT_FOUND,"local bot connection lacks its actual Q2 source client");
    *out=actual.slot;return true;
}
bool application_bot_world_binding_transport_drop(void *context,uint32_t slot,const char *reason,qa_error *error) {
    application_bot_world_binding *binding=context;
    return source_live(binding,error) && application_bot_world_drop(binding->holder,slot,reason,error);
}
