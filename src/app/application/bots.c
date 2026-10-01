#include "bots_private.h"
#include "bots_round.h"
#include "map_players_private.h"
#include "guest_q3_private.h"
#include "guest_q3_restart.h"
#include "native_q3_console.h"
#include "native_q3_clients.h"
#include "native_q3_settings.h"
#include "native_q3_match.h"
#include "native_q3_wire_state.h"
#include "bot_world_bind.h"
#include "bots_transport.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static application_provider *bot_source(application_bots *);
static bool bot_source_client(void *,qa_actor_id,int32_t *,qa_error *);
static uint32_t random_word(void *opaque) {
    application_bots *bots=opaque;return qa_builtin_random_integer(&bots->application->random);
}
static int32_t source_milliseconds(const qa_source_frame *frame) {
    uint32_t word;
    if(frame->kind==QA_CLOCK_NETQUAKE || frame->kind==QA_CLOCK_QUAKEWORLD)
        word=(uint32_t)fmod(trunc(((double)frame->time_ns/1e9)*1000.0),4294967296.0);
    else word=(uint32_t)(frame->time_ns/1000000);
    int32_t value;memcpy(&value,&word,sizeof(value));return value;
}
static bool read_file(void *opaque,const qa_script_include *request,qa_script_resource *out,bool *found,qa_error *error) {
    application_bots *bots=opaque;*found=false;
    size_t directory=0;
    if(request->kind==QA_SCRIPT_INCLUDE_QUOTED && request->from_path) {
        const char *slash=strrchr(request->from_path,'/');
        if(slash) directory=(size_t)(slash-request->from_path)+1;
    }
    for(size_t i=0;i<3;++i) {
        if(i==0 && !directory) continue;
        const char *prefix=i==0?request->from_path:i==1?"":"botfiles/";
        size_t prefix_size=i==0?directory:strlen(prefix),requested=strlen(request->requested_path);
        if(prefix_size>SIZE_MAX-requested-1) return application_fail(error,QA_ERROR_MEMORY,"bot script path size overflow");
        size_t length=prefix_size+requested+1;
        char *path=malloc(length);
        if(!path) return application_fail(error,QA_ERROR_MEMORY,"allocating bot script path");
        memcpy(path,prefix,prefix_size);memcpy(path+prefix_size,request->requested_path,requested+1);
        qa_resource *resource=NULL;qa_error local={0};
        bool ok=qa_vfs_acquire(bots->files,path,&resource,NULL,&local);free(path);
        if(!ok) {if(local.code==QA_ERROR_NOT_FOUND) continue;if(error)*error=local;return false;}
        *out=(qa_script_resource){.path=qa_resource_path(resource),.bytes=qa_resource_bytes(resource),.lease=resource};
        *found=true;return true;
    }
    return true;
}
static void release_file(void *,qa_script_resource *);
static bool character_path(application_bots *bots,const char *name,char path[160],qa_error *error) {
    if(name && strlen(name)<=128) {
        size_t length=strlen(name);bool safe=length>0;
        memcpy(path,"bots/",5);
        for(size_t i=0;i<length;++i) {
            unsigned char letter=(unsigned char)name[i];
            if(letter>='A' && letter<='Z') letter=(unsigned char)(letter+'a'-'A');
            if(!((letter>='a' && letter<='z') || (letter>='0' && letter<='9') || letter=='_')) safe=false;
            path[5+i]=(char)letter;
        }
        memcpy(path+5+length,"_c.c",5);
        if(safe) {
            qa_script_include request={.kind=QA_SCRIPT_ROOT,.requested_path=path};
            qa_script_resource resource;bool found;
            if(!read_file(bots,&request,&resource,&found,error)) return false;
            if(found) {release_file(bots,&resource);return true;}
        }
    }
    memcpy(path,"bots/default_c.c",17);return true;
}
static void release_file(void *opaque,qa_script_resource *resource) {
    (void)opaque;qa_resource_release(resource->lease);*resource=(qa_script_resource){0};
}
bool application_bots_initial_settings(qa_application *app,const qa_launch_seat *seat,
    char *character,size_t capacity,float *skill,qa_error *error) {
    application_bots *bots=app?app->bots:NULL;char path[160];
    if(!bots || !seat || !seat->bot || !character || !skill || !isfinite(seat->bot_skill))
        return application_fail(error,QA_ERROR_ARGUMENT,"original bot settings require the actual prepared source and launch seat");
    if(!character_path(bots,seat->name,path,error)) return false;
    size_t size=strlen(path)+1;
    if(size>capacity) return application_fail(error,QA_ERROR_ARGUMENT,"original bot character path exceeds its output extent");
    memcpy(character,path,size);*skill=seat->bot_skill;return true;
}
static int32_t control_value(application_bots *bots,const char *name,int32_t fallback) {
    application_provider *source=bot_source(bots);
    qa_cvars *configuration=source && source->kind==APPLICATION_PROVIDER_Q3?
        application_native_q3_cvar_owner(source,name):bots->application->cvars;
    const qa_cvar_view *value=configuration?qa_cvars_find(configuration,name):NULL;
    return value?value->integer:fallback;
}
static bool register_controls(application_bots *bots,qa_error *error) {
    static const struct {const char *name,*value,*description;} values[]={
        {"bot_thinktime","100","Native bot decision interval in milliseconds"},
        {"bot_pause","0","Pause native bot decisions"},
        {"bot_challenge","0","Enable source challenge aiming"},
        {"bot_fastchat","0","Use fast native bot chat"},
        {"bot_nochat","0","Suppress native bot chat"},
        {"bot_rocketjump","1","Allow native bot weapon jump travel"},
        {"bot_grapple","0","Allow native bot grapple travel"},
        {"bot_report","0","Report native bot decisions"}
    };
    for(size_t i=0;i<sizeof(values)/sizeof(*values);++i)
        if(!qa_cvars_find(bots->application->cvars,values[i].name) &&
           !qa_cvars_register(bots->application->cvars,values[i].name,values[i].value,0,0,values[i].description,error)) return false;
    return true;
}
static bool controls(void *opaque,qa_bot_controls *out,qa_error *error) {
    (void)error;application_bots *bots=opaque;
    bots->controls=(qa_bot_controls){.think_time_ms=control_value(bots,"bot_thinktime",100),
        .paused=control_value(bots,"bot_pause",0)!=0,.challenge=control_value(bots,"bot_challenge",0)!=0,
        .fast_chat=control_value(bots,"bot_fastchat",0)!=0,.no_chat=control_value(bots,"bot_nochat",0)!=0,
        .rocket_jump=control_value(bots,"bot_rocketjump",1)!=0,.grapple=control_value(bots,"bot_grapple",0)!=0,
        .report=control_value(bots,"bot_report",0)!=0};
    *out=bots->controls;return true;
}
static bool think_time(void *opaque,int32_t milliseconds,qa_error *error) {
    application_bots *bots=opaque;
    if(milliseconds<1 || milliseconds>200) return application_fail(error,QA_ERROR_ARGUMENT,"bot think interval is outside 1..200 ms");
    application_provider *source=bot_source(bots);
    qa_cvars *configuration=source && source->kind==APPLICATION_PROVIDER_Q3?
        application_native_q3_cvar_owner(source,"bot_thinktime"):bots->application->cvars;
    if(!configuration || !qa_cvars_set_number(configuration,"bot_thinktime",(float)milliseconds,error)) return false;
    bots->controls.think_time_ms=milliseconds;return true;
}
static bool pickup_list(void *opaque,int32_t client,const qa_actor_id **out,size_t *count,void **lease,qa_error *error) {
    application_bots *bots=opaque;(void)client;
    if(bots->pickup_borrowed) return application_fail(error,QA_ERROR_ARGUMENT,"bot pickup snapshot is already borrowed");
    qa_builtin_services services=application_builtin_services(bots->application,bots->application->world,bots->application->physics);
    if(!qa_builtin_observations(&services,&bots->pickup_snapshot,error)) return false;
    bots->pickup_borrowed=true;*out=bots->pickup_snapshot.ids;*count=bots->pickup_snapshot.count;*lease=bots;return true;
}
static void pickup_end(void *opaque,void *lease) {
    application_bots *bots=opaque;if(lease==bots) bots->pickup_borrowed=false;
}
static bool pickup(void *opaque,int32_t client,qa_actor_id actor,qa_bot_pickup_goal *out,bool *found,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;*found=false;
    qa_actor_id player=application_bot_client_actor(bots,client);
    if(!player.registry) return true;
    qa_pickup_observation observation;bool observed;
    if(!qa_pickups_inspect(application->pickups,actor,player,&observation,&observed,error)) return false;
    if(!observed || !observation.available || observation.utility<=0) return true;
    qa_body_state body;
    if(!qa_world_body_read(application->world,actor,&body,error)) return false;
    const char *name=qa_strings_cstr(qa_session_strings(application->session),observation.item);
    int32_t number;if(!application_bot_entity_number(bots,actor,&number,error)) return false;
    *out=(qa_bot_pickup_goal){.actor=actor,.entity=number,.origin=body.origin,
        .bounds=body.bounds,.name=name?name:"source pickup",.utility=observation.utility};
    *found=qa_actors_get(qa_session_actors(application->session),actor)!=NULL;return true;
}
static bool owns_item(void *opaque,int32_t client,int32_t entity,bool *out,qa_error *error) {
    (void)opaque;(void)client;(void)entity;(void)error;
    /* Actual source observers own native pickup utility, including Q3 items.
     * Authoring metadata remains available for roam/camp/location goals. */
    *out=true;return true;
}
static int32_t entity_number(void *opaque,qa_actor_id actor) {
    int32_t number;qa_error error={0};
    return application_bot_entity_number(opaque,actor,&number,&error)?number:-1;
}
static application_provider *bot_source(application_bots *bots) {
    return bots->source?bots->source:application_world_provider(bots->application,QA_ROLE_ENTITIES,"");
}
application_provider *application_bot_source(application_bots *bots) {
    return bot_source(bots);
}
bool application_bot_entity_number(application_bots *bots,qa_actor_id actor,int32_t *out,qa_error *error) {
    if(bots->shared_world) return application_bot_world_entity_id(bots->shared_world,actor,out,error);
    application_provider *source=bot_source(bots);uint32_t number;
    if(source && source->kind==APPLICATION_PROVIDER_Q3) {
        if(!qa_q3_source_actor_slot(source->state.q3,actor,&number,error)) return false;
    } else {
        if(!qa_actors_get(qa_session_actors(bots->application->session),actor))
            return application_fail(error,QA_ERROR_NOT_FOUND,"bot observation actor is not live");
        number=actor.slot;
    }
    if(number>INT32_MAX) return application_fail(error,QA_ERROR_UNSUPPORTED,"bot entity number exceeds source range");
    *out=(int32_t)number;return true;
}
static bool bot_entity_extent(void *opaque,uint32_t *out,qa_error *error) {
    (void)error;application_bots *bots=opaque;application_provider *source=bot_source(bots);
    if(bots->shared_world) {*out=application_bot_world_entity_count(bots->shared_world);return true;}
    *out=source && source->kind==APPLICATION_PROVIDER_Q3?QA_Q3_SOURCE_ENTITIES:
        qa_actors_capacity(qa_session_actors(bots->application->session));return true;
}
static bool bot_entity_list(void *opaque,qa_builtin_actor_snapshot *out,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    qa_builtin_services shared=application_builtin_services(bots->application,bots->application->world,bots->application->physics);
    if(!qa_builtin_observations(&shared,out,error)) return false;
    if(bots->shared_world) {
        out->count=0;uint32_t count=application_bot_world_entity_count(bots->shared_world);
        for(uint32_t number=0;number<count;++number) {
            application_bot_world_entity actual;
            if(!application_bot_world_read(bots->shared_world,(int32_t)number,&actual,error)) return false;
            if(!actual.present) continue;
            if(out->count>=out->capacity) return application_fail(error,QA_ERROR_FORMAT,"shared bot source list exceeds actual actor storage");
            out->ids[out->count++]=actual.actor;
        }
        return true;
    }
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3) return true;
    uint32_t count;if(!qa_q3_source_entity_count(source->state.q3,&count,error)) return false;
    out->count=0;
    for(uint32_t slot=0;slot<count;++slot) {
        qa_q3_source_binding binding;
        if(!qa_q3_source_binding_read(source->state.q3,slot,&binding,error)) return false;
        if(!binding.in_use) continue;
        if(!binding.actor.registry || !qa_actors_get(qa_session_actors(bots->application->session),binding.actor) ||
           out->count>=out->capacity)
            return application_fail(error,QA_ERROR_FORMAT,"bot physical source entity list lost its actual actor extent");
        out->ids[out->count++]=binding.actor;
    }
    return true;
}
static bool bot_source_generic1(void *opaque,int32_t client,int32_t *out,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    if(bots->shared_world) {
        application_bot_world_entity actual;
        if(!application_bot_world_read(bots->shared_world,client,&actual,error)) return false;
        *out=actual.state.generic1;return true;
    }
    if(source && source->kind==APPLICATION_PROVIDER_Q3 && client>=0 && client<64) {
        qa_q3_entity state;qa_q3_wire_visibility visibility;
        if(!qa_q3_wire_entity_read(source->state.q3,(uint32_t)client,&state,&visibility,error)) return false;
        *out=state.generic1;return true;
    }
    return application_fail(error,QA_ERROR_UNSUPPORTED,"bot cube status requires its actual source entity words");
}
static bool bot_source_player(void *opaque,int32_t client,qa_bot_source_player *out,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) {
        application_bot_world_entity actual;
        if(!application_bot_world_read(bots->shared_world,client,&actual,error)) return false;
        *out=(qa_bot_source_player){.present=actual.present,.bot=actual.bot,.origin=actual.origin};return true;
    }
    application_provider *source=bot_source(opaque);qa_q3_wire_client_view actual;
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3 || client<0 || client>=QA_Q3_SOURCE_CLIENTS)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"bot policy requires its actual fixed native Q3 client source");
    if(!qa_q3_wire_client_read(source->state.q3,(uint32_t)client,&actual,error)) return false;
    if(actual.present) {
        qa_q3_player ps;
        if(!qa_q3_wire_player_read(source->state.q3,(uint32_t)client,&ps,error)) return false;
        actual.origin=qa_v3(ps.origin[0],ps.origin[1],ps.origin[2]);
    }
    *out=(qa_bot_source_player){.present=actual.present,.bot=actual.bot,.origin=actual.origin};return true;
}
static bool bot_source_intermission(void *opaque,bool *out,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) {
        int32_t time,intermission;
        if(!application_bot_world_clock(bots->shared_world,&time,&intermission,error)) return false;
        *out=intermission!=0;return true;
    }
    application_provider *source=bot_source(opaque);qa_q3_source_match_state match;
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"bot intermission requires its actual native source match clock");
    if(!qa_q3_source_match_state_read(source->state.q3,&match,error)) return false;
    *out=match.intermission_time_ms!=0;return true;
}
static bool bot_source_player_state(void *opaque,int32_t client,qa_bot_source_player_state *out,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) {
        application_bot_world_entity actual;
        if(!application_bot_world_read(bots->shared_world,client,&actual,error)) return false;
        *out=(qa_bot_source_player_state){.present=actual.present,.has_player=actual.has_player,
            .pm_type=actual.player.pmType,.score=actual.player.persistant[0],.last_hurt_client=0,.last_hurt_mod=0};return true;
    }
    application_provider *source=bot_source(opaque);qa_q3_bot_player_state actual;
    if(!out || !source || source->kind!=APPLICATION_PROVIDER_Q3 || client<0 || client>=QA_Q3_SOURCE_CLIENTS)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot chat requires its real fixed Q3 source client");
    if(!qa_q3_client_bot_state_read(source->state.q3,(uint32_t)client,&actual,error)) return false;
    *out=(qa_bot_source_player_state){.present=actual.present,.has_player=actual.has_player,
        .pm_type=actual.pm_type,.score=actual.score,.last_hurt_client=actual.last_hurt_client,
        .last_hurt_mod=actual.last_hurt_mod};return true;
}
static bool bot_source_row_count(void *opaque,uint32_t *out,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) {*out=application_bot_world_entity_count(bots->shared_world);return true;}
    application_provider *source=bot_source(opaque);
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"bot source rows require their actual native Q3 owner");
    return qa_q3_source_entity_count(source->state.q3,out,error);
}
static bool bot_source_row(void *opaque,int32_t number,qa_bot_source_row *out,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) {
        application_bot_world_entity actual;
        if(!application_bot_world_read(bots->shared_world,number,&actual,error)) return false;
        *out=(qa_bot_source_row){.present=actual.present,.classname=actual.classname,.state=actual.state};return true;
    }
    application_provider *source=bot_source(opaque);qa_q3_source_binding binding;qa_q3_wire_visibility visibility;
    *out=(qa_bot_source_row){0};
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3 || number<0 || number>=QA_Q3_SOURCE_ENTITIES)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"bot source row requires its actual native Q3 entity");
    if(!qa_q3_source_binding_read(source->state.q3,(uint32_t)number,&binding,error)) return false;
    out->present=binding.in_use;out->classname=binding.classname;
    if(!out->present) return true;
    return qa_q3_wire_entity_read(source->state.q3,(uint32_t)number,&out->state,&visibility,error);
}
static bool bot_snapshot_entity(void *opaque,qa_actor_id actor,int32_t index,int32_t *number,bool *present,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) {
        int32_t client;
        if(!bot_source_client(bots,actor,&client,error) ||
           !application_bot_transport_snapshot(bots->transport,(uint32_t)client,index,number,error)) return false;
        *present=*number!=-1;return true;
    }
    application_provider *source=bot_source(opaque);
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"bot snapshots require their actual native Q3 source owner");
    return application_native_q3_bot_snapshot_entity(source,actor,index,number,present,error);
}
static bool bot_source_entity(void *opaque,int32_t number,qa_q3_entity *out,bool *available,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) {
        application_bot_world_entity actual;
        if(!application_bot_world_read(bots->shared_world,number,&actual,error)) return false;
        *available=actual.present && actual.linked && !actual.hidden;
        *out=*available?actual.state:(qa_q3_entity){0};return true;
    }
    application_provider *source=bot_source(opaque);qa_q3_source_binding binding;qa_q3_wire_visibility visibility;
    *out=(qa_q3_entity){0};*available=false;
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3 || number<0 || number>=QA_Q3_SOURCE_ENTITIES)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"bot event entity requires its actual fixed native Q3 source");
    if(!qa_q3_source_binding_read(source->state.q3,(uint32_t)number,&binding,error)) return false;
    if(!binding.in_use) return true;
    if(!qa_q3_wire_entity_read(source->state.q3,(uint32_t)number,out,&visibility,error)) return false;
    *available=visibility.linked && !(visibility.server_flags&1);
    if(!*available) *out=(qa_q3_entity){0};
    else if(number<QA_Q3_SOURCE_CLIENTS) {
        int32_t phase;
        if(!application_bot_source_weapon(opaque,binding.actor,&out->weapon,&phase,error)) return false;
    }
    return true;
}
static bool bot_source_event_time(void *opaque,int32_t number,int32_t *out,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) {
        application_bot_world_entity actual;
        if(!application_bot_world_read(bots->shared_world,number,&actual,error)) return false;
        *out=0;return true;
    }
    application_provider *source=bot_source(opaque);
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3 || number<0 || number>=QA_Q3_SOURCE_ENTITIES)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"bot event time requires its actual fixed native Q3 source");
    return qa_q3_wire_entity_event_time(source->state.q3,(uint32_t)number,out,error);
}
static bool bot_print(void *opaque,const char *text,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    if(!source || !source->constructed || !source->attached || source->close_pending || !text)
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot Print source has retired");
    if(source->kind==APPLICATION_PROVIDER_Q3) return application_native_q3_console_print(source,text,error);
    qa_command_context command={.owner=source->owner,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_SERVER};
    if(!qa_application_capture_command_context(bots->application,&command,&command,error)) return false;
    application_console_print(bots->application,&command,text);return true;
}
static void bot_diagnostic(void *opaque,qa_script_severity severity,const char *text) {
    (void)severity;qa_error error={0};
    (void)bot_print(opaque,text,&error);
}
static bool bot_source_client(void *opaque,qa_actor_id actor,int32_t *out,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    uint32_t slot;
    if(source && source->kind==APPLICATION_PROVIDER_Q3) {
        if(!qa_q3_native_client_slot(source->state.q3,actor,&slot,error)) return false;
    } else if(source && (source->kind==APPLICATION_PROVIDER_QVM || source->kind==APPLICATION_PROVIDER_NATIVE) &&
              source->product && source->product->family==QA_GAME_Q3) {
        if(!application_q3_guest_actor_client(source,actor,&slot))
            return application_fail(error,QA_ERROR_NOT_FOUND,"bot has no actual Q3 source client binding");
    } else {
        application_player_record *record=NULL;
        for(size_t i=0;bots->application->players && i<bots->application->players->count;++i) {
            application_player_record *candidate=bots->application->players->records+i;
            if(!candidate->retiring && qa_actor_id_equal(candidate->actor,actor)) {record=candidate;break;}
        }
        if(!record) return application_fail(error,QA_ERROR_NOT_FOUND,"bot has no actual shared player client binding");
        slot=record->client_slot;
    }
    if(slot>INT32_MAX) return application_fail(error,QA_ERROR_ARGUMENT,"bot source client exceeds signed extent");
    *out=(int32_t)slot;return true;
}
static qa_actor_id bot_source_actor(void *opaque,int32_t client) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    if(client<0) return (qa_actor_id){0};
    if(source && source->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_source_binding binding;
        return qa_q3_source_binding_read(source->state.q3,(uint32_t)client,&binding,NULL)?binding.actor:(qa_actor_id){0};
    }
    if(source && (source->kind==APPLICATION_PROVIDER_QVM || source->kind==APPLICATION_PROVIDER_NATIVE) &&
       source->product && source->product->family==QA_GAME_Q3) {
        struct application_q3_guest *engine=q3g_engine(source);
        qa_actor_id actor=engine && client<64?engine->clients[client].actor:(qa_actor_id){0};
        return qa_actors_get(qa_session_actors(bots->application->session),actor)?actor:(qa_actor_id){0};
    }
    for(size_t i=0;bots->application->players && i<bots->application->players->count;++i) {
        application_player_record *record=bots->application->players->records+i;
        if(!record->retiring && record->client_slot==(uint32_t)client &&
           qa_actors_get(qa_session_actors(bots->application->session),record->actor)) return record->actor;
    }
    return (qa_actor_id){0};
}
static qa_cvars *bot_configuration(void *opaque) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    if(source && source->kind==APPLICATION_PROVIDER_Q3)
        return application_native_q3_console_registry(source);
    if(source && (source->kind==APPLICATION_PROVIDER_QVM || source->kind==APPLICATION_PROVIDER_NATIVE) &&
       source->product && source->product->family==QA_GAME_Q3) {
        struct application_q3_guest *engine=q3g_engine(source);
        if(engine && engine->game) {
            qa_cvars *configuration=NULL;
            qa_q3_host_console(engine->game->host,&configuration,NULL);return configuration;
        }
    }
    return bots->application->cvars;
}
static bool bot_register_cvar(void *opaque,const char *name,const char *value,uint32_t flags,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    if(!source || !source->constructed || !source->attached || source->close_pending || bots->restoring)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot cvar registration requires its actual admitted source owner");
    bool native=source->kind==APPLICATION_PROVIDER_Q3;
    if(native && !application_native_q3_console_borrow(source,error)) return false;
    qa_cvars *configuration=bot_configuration(bots);
    bool ok=configuration?qa_cvars_register(configuration,name,value,flags,source->owner,NULL,error):
        application_fail(error,QA_ERROR_NOT_FOUND,"bot cvar source registry is unavailable");
    if(native) application_native_q3_console_release(source);
    return ok;
}
static bool bot_configstring(void *opaque,uint32_t index,char *out,size_t capacity,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    if(bots->shared_world) return application_bot_world_configstring(bots->shared_world,index,out,capacity,error);
    if(!out || !capacity) return application_fail(error,QA_ERROR_ARGUMENT,"bot configstring needs bounded output");
    const char *text=NULL;
    if(source && source->kind==APPLICATION_PROVIDER_Q3) {
        if(!qa_q3_configstring_read(source->state.q3,index,&text,error)) return false;
    } else if(source && (source->kind==APPLICATION_PROVIDER_QVM || source->kind==APPLICATION_PROVIDER_NATIVE) &&
              source->product && source->product->family==QA_GAME_Q3) {
        struct application_q3_guest *engine=q3g_engine(source);
        if(!engine || index>=QA_Q3_CONFIGSTRINGS)
            return application_fail(error,QA_ERROR_ARGUMENT,"bot Q3 source configstring is absent");
        text=qa_q3_configstring(&engine->gamestate,index);
    } else if(index>=544 && index<608) {
        qa_actor_id actor=bot_source_actor(bots,(int32_t)(index-544));
        qa_builtin_services shared=application_builtin_services(bots->application,bots->application->world,bots->application->physics);
        qa_builtin_player_info player;
        if(!actor.registry || !shared.player_info(shared.context,actor,&player)) {out[0]=0;return true;}
        char information[1024]={0};
        if(!qa_q3_info_set(information,sizeof(information),"n",player.name?player.name:"",error) ||
           !qa_q3_info_set(information,sizeof(information),"t",player.spectator?"3":"0",error) ||
           !qa_q3_info_set(information,sizeof(information),"model",player.skin?player.skin:"",error)) return false;
        size_t size=strlen(information);if(size>=capacity) size=capacity-1;
        memcpy(out,information,size);out[size]=0;return true;
    } else return application_fail(error,QA_ERROR_UNSUPPORTED,"shared bot source has no retained non-player configstring owner");
    size_t size=strlen(text);if(size>=capacity) size=capacity-1;
    memcpy(out,text,size);out[size]=0;return true;
}
static bool bot_console(void *opaque,qa_actor_id actor,char *out,size_t capacity,bool *found,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    if(bots->shared_world) {
        int32_t client;if(!bot_source_client(bots,actor,&client,error)) return false;
        return application_bot_transport_console(bots->transport,(uint32_t)client,out,capacity,found,error);
    }
    if(source && source->kind==APPLICATION_PROVIDER_Q3)
        return application_native_q3_bot_console(source,actor,out,capacity,found,error);
    *found=false;return true;
}
static bool bot_get_userinfo(void *opaque,qa_actor_id actor,char *out,size_t capacity,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    int32_t client;if(!bot_source_client(bots,actor,&client,error)) return false;
    const char *actual=NULL;
    if(bots->shared_world) actual=application_bot_world_userinfo(bots->shared_world,(uint32_t)client);
    else if(source && source->kind==APPLICATION_PROVIDER_Q3) {
        if(!application_native_q3_wire_userinfo_read(source,(uint32_t)client,&actual,error)) return false;
    } else if(source && (source->kind==APPLICATION_PROVIDER_QVM || source->kind==APPLICATION_PROVIDER_NATIVE) &&
              source->product && source->product->family==QA_GAME_Q3) {
        struct application_q3_guest *engine=q3g_engine(source);
        if(!engine || client<0 || client>=64 || !engine->clients[client].connected)
            return application_fail(error,QA_ERROR_NOT_FOUND,"bot source userinfo client is absent");
        actual=engine->clients[client].userinfo?engine->clients[client].userinfo:"";
    } else return application_fail(error,QA_ERROR_UNSUPPORTED,"bot userinfo requires its actual Q3 source client");
    if(!out || !capacity) return application_fail(error,QA_ERROR_ARGUMENT,"bot userinfo requires bounded output");
    size_t size=strlen(actual);if(size>=capacity) size=capacity-1;
    memcpy(out,actual,size);out[size]=0;return true;
}
static bool bot_set_userinfo(void *opaque,qa_actor_id actor,const char *info,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    int32_t client;if(!bot_source_client(bots,actor,&client,error)) return false;
    if(!source) return application_fail(error,QA_ERROR_NOT_FOUND,"bot raw userinfo has no source owner");
    if(bots->shared_world) return application_bot_world_userinfo_set(bots->shared_world,(uint32_t)client,info,error) &&
        application_bot_world_userinfo_changed(bots->shared_world,(uint32_t)client,error);
    if(source->kind==APPLICATION_PROVIDER_Q3)
        return application_native_q3_wire_userinfo(source,(uint32_t)client,info,error) &&
            application_native_q3_client_userinfo_changed(source,actor,error);
    if((source->kind==APPLICATION_PROVIDER_QVM || source->kind==APPLICATION_PROVIDER_NATIVE) &&
       source->product && source->product->family==QA_GAME_Q3)
        return application_q3_guest_client_userinfo(source,(uint32_t)client,info,error);
    return application_fail(error,QA_ERROR_UNSUPPORTED,"bot raw userinfo has no source setter");
}
static bool bot_userinfo(void *opaque,qa_actor_id actor,const char *key,const char *value,qa_error *error) {
    char info[1024];
    return bot_get_userinfo(opaque,actor,info,sizeof(info),error) &&
        qa_q3_info_set(info,sizeof(info),key,value,error) && bot_set_userinfo(opaque,actor,info,error);
}
static bool bot_game_type(void *opaque,int32_t *out,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) {*out=0;return true;}
    application_provider *source=bot_source(opaque);
    if(source && source->kind==APPLICATION_PROVIDER_Q3)
        return application_native_q3_settings_integer(source,"g_gametype",out,error);
    return application_fail(error,QA_ERROR_UNSUPPORTED,"bot setup requires its actual GAME copied game type");
}
static bool bot_exit_level(void *opaque,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) return application_bot_world_exit_level(bots->shared_world,error);
    application_provider *source=bot_source(opaque);
    if(source && source->kind==APPLICATION_PROVIDER_Q3)
        return application_native_q3_match_exit_level(source,error);
    return application_fail(error,QA_ERROR_UNSUPPORTED,"bot interbreeding requires its actual source ExitLevel");
}
static bool bot_insert_command(void *opaque,const char *text,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->shared_world) return application_bot_world_console(bots->shared_world,text,error);
    application_provider *source=bot_source(opaque);qa_console *console;
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3 ||
       !application_native_q3_console_at(source,&console,NULL,error)) return false;
    qa_command_context context={.owner=source->owner,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_SERVER};
    return qa_console_insert(console,&context,text,error);
}
static bool bot_random(void *opaque,float *out,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=bot_source(bots);
    if(!source || !source->constructed || !source->attached || source->close_pending || bots->restoring)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot AI source random owner has retired or is restoring");
    if(source && source->kind==APPLICATION_PROVIDER_Q3) return qa_q3_game_random(source->state.q3,out,error);
    if(source && source->kind==APPLICATION_PROVIDER_Q1) {*out=qa_q1_game_random(source->state.q1);return true;}
    if(source && source->kind==APPLICATION_PROVIDER_Q2) return qa_q2_game_random(source->state.q2,out,error);
    return application_fail(error,QA_ERROR_UNSUPPORTED,"bot AI requires its actual admitted source GAME random owner");
}
static bool guest_bot_provider(const application_provider *provider) {
    uint64_t presentation=QA_ROLE_BIT(QA_ROLE_HUD)|QA_ROLE_BIT(QA_ROLE_MENU)|
        QA_ROLE_BIT(QA_ROLE_EFFECTS)|QA_ROLE_BIT(QA_ROLE_AUDIO)|QA_ROLE_BIT(QA_ROLE_MUSIC);
    return provider->attached && provider->constructed && provider->product &&
        provider->product->family==QA_GAME_Q3 &&
        (provider->kind==APPLICATION_PROVIDER_QVM || provider->kind==APPLICATION_PROVIDER_NATIVE) &&
        (provider->launch->roles & ~presentation)!=0;
}
qa_actor_id application_bot_client_actor(application_bots *bots,int32_t client) {
    if(client<0) return (qa_actor_id){0};
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next) {
        if((uint32_t)client<guest->client_base || (uint32_t)client-guest->client_base>=64) continue;
        struct application_q3_guest *engine=q3g_engine(guest->provider);
        qa_actor_id actor=engine?engine->clients[(uint32_t)client-guest->client_base].actor:(qa_actor_id){0};
        return qa_actors_get(qa_session_actors(bots->application->session),actor)?actor:(qa_actor_id){0};
    }
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!bots->seats[i].retired && bots->seats[i].actor.registry &&
           bots->seats[i].library_client==(uint32_t)client)
            return qa_actors_get(qa_session_actors(bots->application->session),bots->seats[i].actor)?
                bots->seats[i].actor:(qa_actor_id){0};
    uint32_t cursor=(uint32_t)client;const qa_actor_record *record;
    return qa_actors_next(qa_session_actors(bots->application->session),&cursor,&record) &&
        record->id.slot==(uint32_t)client?record->id:(qa_actor_id){0};
}
static bool bot_command(void *opaque,int32_t client,const char *text,qa_error *error) {
    application_bots *bots=opaque;
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        if(client>=0 && (uint32_t)client>=guest->client_base && (uint32_t)client-guest->client_base<64)
            return application_q3_guest_client_command(guest->provider,(uint32_t)client-guest->client_base,text,error);
    qa_actor_id actor=application_bot_client_actor(bots,client);
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!bots->seats[i].retired && qa_actor_id_equal(bots->seats[i].actor,actor)) {
            application_provider *source=bot_source(bots);
            return source && source->kind==APPLICATION_PROVIDER_Q3?
                application_native_q3_client_text(source,actor,text,error):
                qa_application_actor_command(bots->application,actor,text,error);
        }
    return application_fail(error,QA_ERROR_NOT_FOUND,"bot command client has no current roster owner");
}
bool application_bots_guest_bind(qa_application *application,application_provider *provider,
                                  qa_q3_host *host,qa_error *error) {
    application_bots *bots=application->bots;
    if(!bots) return application_fail(error,QA_ERROR_NOT_FOUND,"guest shared bot library was not prepared");
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        if(guest->provider==provider)
            return qa_q3_host_attach_bots(host,bots->runtime,guest->client_base,guest->entity_base,true,error);
    return application_fail(error,QA_ERROR_NOT_FOUND,"guest bot library namespace was not admitted");
}
static bool close_bots(application_bots *bots,qa_error *error) {
    if(!bots) return true;
    if(bots->calls || bots->arsenal_leases || bots->pickup_borrowed || bots->mover_borrowed ||
       !qa_bots_can_destroy(bots->population) || !qa_bot_runtime_can_destroy(bots->runtime) ||
       !application_bot_world_can_destroy(bots->shared_world) || !application_bot_transport_can_destroy(bots->transport))
        return application_fail(error,QA_ERROR_ARGUMENT,"application bot owners are executing a callback");
    if(application_q3_guest_bots_borrowed(bots->application,bots->runtime))
        return application_fail(error,QA_ERROR_ARGUMENT,"original GAME hosts still borrow the actual bot runtime");
    if(!qa_bots_destroy(bots->population,error)) return false;
    bots->population=NULL;
    if(!qa_bot_runtime_destroy(bots->runtime,error)) return false;
    bots->runtime=NULL;
    if(!application_bot_transport_destroy(bots->transport,error)) return false;
    bots->transport=NULL;
    if(!application_bot_world_destroy(bots->shared_world,error)) return false;
    bots->shared_world=NULL;
    application_bot_world_binding_destroy(bots->shared_binding);bots->shared_binding=NULL;
    qa_bot_navigation_destroy(bots->map_navigation);
    if(bots->seats) for(uint32_t i=0;i<bots->capacity;++i) qa_bot_navigation_destroy(bots->seats[i].navigation);
    while(bots->targets) {application_bot_target *target=bots->targets;bots->targets=target->next;
        qa_bot_navigation_destroy(target->navigation);free(target);}
    while(bots->graphs) {application_bot_graph *graph=bots->graphs;bots->graphs=graph->next;
        qa_navigation_destroy(graph->navigation);qa_nav_graph_release(graph->graph);
        qa_resource_release(graph->asset_resource);free(graph);}
    while(bots->guests) {application_bot_guest *guest=bots->guests;bots->guests=guest->next;free(guest);}
    qa_builtin_snapshot_free(&bots->pickup_snapshot);qa_entities_free(&bots->entities);
    qa_resource_release(bots->source_map_resource);qa_resource_release(bots->map_resource);qa_vfs_destroy(bots->files);
    qa_bots_save_requirements_free(&bots->saved_requirements);
    application_bots_round_detach(bots->round);
    application_bots_original_detach(bots->original);
    free(bots->saved_file_references);
    free(bots->train_stops);free(bots->seats);free(bots);return true;
}
bool application_bots_can_destroy(const qa_application *application) {
    application_bots *bots=application?application->bots:NULL;
    return !bots || (!bots->calls && !bots->arsenal_leases && !bots->pickup_borrowed && !bots->mover_borrowed &&
        qa_bots_can_destroy(bots->population) && qa_bot_runtime_can_destroy(bots->runtime) &&
        application_bot_world_can_destroy(bots->shared_world) && application_bot_transport_can_destroy(bots->transport));
}
bool application_bots_destroy(qa_application *application,qa_error *error) {
    if(!application || !application->bots) return true;
    if(!close_bots(application->bots,error)) return false;
    application->bots=NULL;return true;
}
bool application_bots_shutdown(qa_application *application,bool restart,qa_error *error) {
    application_bots *bots=application?application->bots:NULL;
    if(!bots || !bots->population) return true;
    if(bots->restoring || bots->producing || bots->calls || !application_bots_can_destroy(application) ||
       bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot source shutdown requires the admitted idle application roster");
    ++bots->calls;bool ok=qa_bots_shutdown(bots->population,restart,error);--bots->calls;
    return ok;
}
bool application_bots_client_shutdown(qa_application *application,qa_actor_id actor,bool restart,qa_error *error) {
    application_bots *bots=application?application->bots:NULL;
    if(!bots || !bots->population) return true;
    qa_bot_view view;if(!qa_bots_read(bots->population,actor,&view,NULL)) return true;
    bool source_drop=application_bot_world_drop_admitted(bots->shared_world);
    if(bots->restoring || bots->producing || bots->calls ||
       (!source_drop && !application_bots_can_destroy(application)) ||
       (source_drop && (!qa_bots_can_destroy(bots->population) || !qa_bot_runtime_can_destroy(bots->runtime))) ||
       bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot client shutdown requires its admitted idle source roster");
    ++bots->calls;bool ok=qa_bots_shutdown_client(bots->population,actor,restart,error);--bots->calls;
    return ok;
}
bool application_bots_actor(const qa_application *application,qa_actor_id actor) {
    const application_bots *bots=application?application->bots:NULL;
    if(!bots || !qa_actors_get(qa_session_actors(application->session),actor)) return false;
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!bots->seats[i].retired && qa_actor_id_equal(bots->seats[i].actor,actor)) return true;
    return false;
}
bool application_bots_actor_released(qa_application *application,qa_actor_record actor,qa_error *error) {
    application_bots *bots=application->bots;if(!bots) return true;
    for(uint32_t i=0;i<bots->capacity;++i)
        if(qa_actor_id_equal(bots->seats[i].actor,actor.id)) bots->seats[i].retired=true;
    return !bots->population || qa_bots_actor_released(bots->population,&actor,error);
}
bool application_bots_frame_at(qa_application *application,const qa_source_frame *frames,
                                size_t count,uint64_t host_ns,qa_error *error) {
    application_bots *bots=application->bots;if(!bots) return true;
    if(bots->round_phase==APPLICATION_BOT_ROUND_FAILED)
        return application_fail(error,QA_ERROR_ARGUMENT,"application bot round failed before completing its source lifetime");
    if(bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE) return true;
    if(bots->calls || bots->restoring || bots->producing)
        return application_fail(error,QA_ERROR_ARGUMENT,"application bot frame reentered or restoring");
    application_provider *source=application->players?application->players->map_provider:NULL;
    if(!source || (!frames && count))
        return application_fail(error,QA_ERROR_ARGUMENT,"application bot producer has no admitted map source");
    const qa_source_frame *frame=NULL;
    for(size_t i=0;i<count;++i)
        if(frames[i].provider==source->owner) {frame=frames+i;break;}
    if(!frame) return true;
    qa_source_frame admitted;uint64_t actual_host;
    if(!qa_session_active_frame(application->session,frame->provider,&admitted) ||
       !qa_session_frame_host_time(application->session,&actual_host) || actual_host!=host_ns ||
       admitted.kind!=frame->kind || admitted.number!=frame->number ||
       admitted.start_ns!=frame->start_ns || admitted.elapsed_ns!=frame->elapsed_ns ||
       admitted.time_ns!=frame->time_ns)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot input requires its genuine current source admission");
    application_bot_target **link=&bots->targets;
    while(*link) {
        application_bot_target *target=*link;
        if(qa_actors_get(qa_session_actors(application->session),target->actor)) {link=&target->next;continue;}
        *link=target->next;qa_bot_navigation_destroy(target->navigation);free(target);
    }
    bots->producer_frame=admitted;bots->producer_host_ns=host_ns;bots->producing=true;
    if(bots->shared_world && (!application_bot_transport_frame(bots->transport,(double)admitted.elapsed_ns/1e6,error) ||
        !application_bot_world_refresh(bots->shared_world,error))) {
        bots->producing=false;bots->producer_frame=(qa_source_frame){0};bots->producer_host_ns=0;return false;
    }
    ++bots->calls;
    application_native_q3_bot_cycle *cycle=NULL;
    bool ok=!bots->population || source->kind!=APPLICATION_PROVIDER_Q3 ||
        application_native_q3_bot_cycle_begin(source,&admitted,host_ns,&cycle,error);
    if(ok && bots->population) ok=qa_bots_frame(bots->population,source_milliseconds(&admitted),error);
    application_native_q3_bot_cycle_end(&cycle);
    --bots->calls;bots->producing=false;bots->producer_frame=(qa_source_frame){0};bots->producer_host_ns=0;
    return ok;
}
qa_bot_services application_bots_services(application_bots *bots) {
    qa_application *application=bots->application;
    application_provider *source=bot_source(bots);
    return (qa_bot_services){.context=bots,
        .team_arena=source && source->product && source->product->family==QA_GAME_Q3 &&
            !strcmp(source->product->campaign,"missionpack"),
        .shared=application_builtin_services(application,application->world,application->physics),
        .modes=application->modes,.player=application_bot_player,.inventory=application_bot_inventory_update,
        .entity=application_bot_entity,
        .entity_extent=bot_entity_extent,.entity_list=bot_entity_list,
        .arsenal=application_bot_arsenal,.arsenal_end=application_bot_arsenal_end,.submit=application_bot_submit,
        .source_client=bot_source_client,.source_actor=bot_source_actor,.configuration=bot_configuration,.register_cvar=bot_register_cvar,
        .configstring=bot_configstring,.source_generic1=bot_source_generic1,
        .source_player=bot_source_player,.source_player_state=bot_source_player_state,.source_intermission=bot_source_intermission,
        .source_row_count=bot_source_row_count,.source_row=bot_source_row,
        .snapshot_entity=bot_snapshot_entity,.source_entity=bot_source_entity,
        .source_event_time=bot_source_event_time,.print=bot_print,
        .console=bot_console,.userinfo=bot_userinfo,.get_userinfo=bot_get_userinfo,.set_userinfo=bot_set_userinfo,
        .source_game_type=bot_game_type,.exit_level=bot_exit_level,.insert_console_command=bot_insert_command,.random=bot_random,
        .controls=controls,.set_think_time=think_time,.activation=application_bot_activation,
        .predict_motion=application_bot_predict_motion};
}
bool application_bots_prepare(qa_application *application,const qa_launch_choices *choices,
                               const qa_bsp_view *geometry,const qa_entities *entities,qa_error *error) {
    (void)entities;uint32_t capacity=0;size_t count=0;size_t guest_count=0;
    if(choices->seat_count>INT32_MAX)
        return application_fail(error,QA_ERROR_ARGUMENT,"native bot roster exceeds source handle range");
    for(size_t i=0;i<choices->seat_count;++i) if(choices->seats[i].bot) {++count;capacity=(uint32_t)i+1;}
    for(size_t i=0;i<application->provider_count;++i) {
        application_provider *provider=application->providers[i];
        if(guest_bot_provider(provider)) ++guest_count;
    }
    application_provider *actual_source=application_world_provider(application,QA_ROLE_ENTITIES,"");
    bool native_q3=actual_source && actual_source->kind==APPLICATION_PROVIDER_Q3;
    if(!count && !guest_count && !native_q3) return true;
    if(application->bots) return application->bots->map_resource==application->map_resource ||
        application_fail(error,QA_ERROR_ARGUMENT,"retire the previous shared bot library before map preparation");
    if(count>=UINT32_MAX)
        return application_fail(error,QA_ERROR_ARGUMENT,"native bot roster exceeds source handle range");
    application_bots *bots=calloc(1,sizeof(*bots));
    if(!bots) return application_fail(error,QA_ERROR_MEMORY,"allocating application native bot owner");
    bots->application=application;bots->source=actual_source;bots->capacity=capacity;bots->geometry=*geometry;
    uint32_t actor_capacity=qa_actors_capacity(qa_session_actors(application->session));
    uint32_t entity_base=actor_capacity>QA_Q3_SOURCE_ENTITIES?actor_capacity:QA_Q3_SOURCE_ENTITIES;
    if(actor_capacity>=INT32_MAX || guest_count>(INT32_MAX-entity_base)/QA_Q3_SOURCE_ENTITIES ||
       count+guest_count*64>=INT32_MAX) {
        application_fail(error,QA_ERROR_ARGUMENT,"shared bot namespaces exceed signed source range");goto fail;
    }
    uint32_t ordinal=0;
    for(size_t i=0;i<application->provider_count;++i) {
        application_provider *provider=application->providers[i];
        if(!guest_bot_provider(provider)) continue;
        application_bot_guest *guest=calloc(1,sizeof(*guest));
        if(!guest) {application_fail(error,QA_ERROR_MEMORY,"allocating guest bot namespace");goto fail;}
        *guest=(application_bot_guest){.provider=provider,.client_base=actor_capacity+ordinal*64,
            .entity_base=entity_base+ordinal*QA_Q3_SOURCE_ENTITIES,.next=bots->guests};
        bots->guests=guest;++ordinal;
    }
    bots->map_resource=application->map_resource;qa_resource_retain(bots->map_resource);
    bots->controls=(qa_bot_controls){.think_time_ms=100,.rocket_jump=true};
    if(!register_controls(bots,error)) goto fail;
    bots->seats=capacity?calloc(capacity,sizeof(*bots->seats)):NULL;
    if(capacity && !bots->seats) {application_fail(error,QA_ERROR_MEMORY,"allocating bot roster navigation bindings");goto fail;}
    const qa_launch_snapshot *snapshot=application->routing_snapshot?application->routing_snapshot:qa_application_launch(application);
    bots->files_launch=true;
    bots->files=qa_vfs_clone(qa_launch_snapshot_mounts(snapshot),error);
    if(!bots->files) goto fail;
    qa_resource *probe=NULL;qa_error local={0};
    bool have_assets=qa_vfs_acquire(bots->files,"botfiles/bots/default_c.c",&probe,NULL,&local);
    qa_resource_release(probe);
    if(!have_assets) {
        if(local.code!=QA_ERROR_NOT_FOUND) {if(error)*error=local;goto fail;}
        bool opened=false;
        for(size_t i=0;i<qa_catalog_count(application->catalog);++i) {
            const qa_product *product=qa_catalog_at(application->catalog,i);
            if(product->family!=QA_GAME_Q3 || product->availability!=QA_CONTENT_INSTALLED) continue;
            qa_vfs *files=NULL;if(!qa_catalog_open(application->catalog,product->id,&files,error)) goto fail;
            probe=NULL;local=(qa_error){0};have_assets=qa_vfs_acquire(files,"botfiles/bots/default_c.c",&probe,NULL,&local);
            qa_resource_release(probe);
            if(have_assets) {
                if(!qa_strings_intern_cstr(qa_session_strings(application->session),product->id,&bots->files_product,error)) {
                    qa_vfs_destroy(files);goto fail;
                }
                qa_vfs_destroy(bots->files);bots->files=files;bots->files_launch=false;opened=true;break;
            }
            qa_vfs_destroy(files);if(local.code!=QA_ERROR_NOT_FOUND) {if(error)*error=local;goto fail;}
        }
        if(!opened && (count || native_q3)) {application_fail(error,QA_ERROR_NOT_FOUND,"native bots require installed bot personality resources");goto fail;}
    }
    if(!qa_entities_parse(geometry->lumps[QA_BSP_ENTITIES].bytes,
            geometry->family==QA_BSP_Q3?QA_ENTITY_Q3:QA_ENTITY_Q1,&bots->entities,error)) goto fail;
    if(!application_bot_navigation_prepare(bots,error)) goto fail;
    qa_bot_runtime_options options={.library={.scripts={.context=bots,.read=read_file,.release=release_file},
        .preprocessor={.include_path="botfiles",.builtins=true}},.maximum_states=(uint32_t)((native_q3?64:count)+guest_count*64)+1,
        .minimum_clients=actor_capacity+(uint32_t)guest_count*64,
        .observations=guest_count?QA_BOT_OBSERVATION_MODULE:QA_BOT_OBSERVATION_NATIVE};
    qa_bot_random_source random={bots,random_word};
    qa_bot_runtime_services services={.context=bots,.random=random,.navigation=application_bot_navigation,.command=bot_command,
        .diagnostic=bot_diagnostic,
        .goals={.context=bots,.navigation=application_bot_navigation,.pickups=pickup_list,.pickups_end=pickup_end,
            .pickup=pickup,.owns_item=owns_item},
        .movement={.context=bots,.navigation=application_bot_navigation,.actor=application_bot_actor,
            .entity_number=entity_number,.model=application_bot_travel_model,
            .travel_weapon=application_bot_travel_weapon,.grapple_state=application_bot_grapple_state,.random=random}};
    if(!qa_bot_runtime_create(&options,&services,&bots->runtime,error)) goto fail;
    char clients[32],source_entities[32];snprintf(clients,sizeof(clients),"%u",actor_capacity);
    snprintf(source_entities,sizeof(source_entities),"%u",entity_base);
    if(!qa_bot_library_variable_set(qa_bot_runtime_library(bots->runtime),"maxclients",clients,error) ||
       !qa_bot_library_variable_set(qa_bot_runtime_library(bots->runtime),"maxentities",source_entities,error)) goto fail;
    const char *name=qa_strings_cstr(qa_session_strings(application->session),application->current_map);
    qa_bot_runtime_map map={.name=name,.entities=&bots->entities,.navigation=application_bot_navigation(bots,-1)};
    if(!qa_bot_runtime_attach_map(bots->runtime,&map,error)) goto fail;
    application->bots=bots;return true;
fail:
    {qa_error cleanup={0};if(!close_bots(bots,&cleanup)) {if(error && !error->code)*error=cleanup;}}
    return false;
}
bool application_bots_construct_restored(application_bots *bots,qa_error *error) {
    qa_application *application=bots->application;
    qa_bot_runtime_options options=bots->saved_requirements.runtime;
    options.library.scripts.context=bots;
    options.library.scripts.read=read_file; options.library.scripts.release=release_file;
    qa_bot_random_source random={bots,random_word};
    qa_bot_runtime_services services={.context=bots,.random=random,.navigation=application_bot_navigation,.command=bot_command,
        .diagnostic=bot_diagnostic,
        .goals={.context=bots,.navigation=application_bot_navigation,.pickups=pickup_list,.pickups_end=pickup_end,
            .pickup=pickup,.owns_item=owns_item},
        .movement={.context=bots,.navigation=application_bot_navigation,.actor=application_bot_actor,
            .entity_number=entity_number,.model=application_bot_travel_model,
            .travel_weapon=application_bot_travel_weapon,.grapple_state=application_bot_grapple_state,.random=random}};
    if(!register_controls(bots,error) || !qa_bot_runtime_create(&options,&services,&bots->runtime,error)) return false;
    if(!bots->saved_requirements.population) return true;
    qa_bot_services ai=application_bots_services(bots);
    return qa_bots_create_restored(bots->runtime,&ai,bots->saved_requirements.population_client_capacity,
                                   &bots->population,error);
}
qa_bot_runtime *application_bots_runtime(qa_application *application) {
    return application && application->bots?application->bots->runtime:NULL;
}
bool application_bots_shared_construct(application_bots *bots,bool restoring,qa_error *error) {
    application_provider *source=bot_source(bots);
    if(!source || (source->kind!=APPLICATION_PROVIDER_Q1 && source->kind!=APPLICATION_PROVIDER_Q2)) return true;
    if(bots->shared_binding || bots->shared_world || bots->transport)
        return application_fail(error,QA_ERROR_ARGUMENT,"shared bot source owners were already constructed");
    application_bot_world_services services;
    if(!application_bot_world_binding_create(bots,&bots->shared_binding,error) ||
       !application_bot_world_binding_services(bots->shared_binding,&services,error) ||
       !application_bot_world_create(&services,restoring,&bots->shared_world,error)) return false;
    application_bot_world_binding_holder(bots->shared_binding,bots->shared_world);
    application_bot_transport_services transport={.context=bots->shared_binding,.world=bots->shared_world,
        .session=bots->application->session,.client=application_bot_world_binding_transport_client,
        .drop=application_bot_world_binding_transport_drop};
    if(!application_bot_transport_create(&transport,&bots->transport,error)) return false;
    if(restoring) return true;
    const char *map=qa_strings_cstr(qa_session_strings(bots->application->session),bots->application->current_map);
    if(!map) return application_fail(error,QA_ERROR_NOT_FOUND,"shared bot source lacks its actual map name");
    char maximum[32],gravity[48];snprintf(maximum,sizeof(maximum),"%u",services.max_clients);
    snprintf(gravity,sizeof(gravity),"%.9g",(double)bots->application->physics->gravity);
    const struct {const char *name,*value;} values[]={
        {"sv_maxclients",maximum},{"g_gametype","0"},{"mapname",map},{"sv_mapname",map},
        {"g_spSkill","2"},{"bot_enable","1"},{"bot_minplayers","0"},{"dedicated","1"},{"g_gravity",gravity}};
    for(size_t i=0;i<sizeof(values)/sizeof(*values);++i)
        if(!qa_cvars_register(bots->application->cvars,values[i].name,values[i].value,0,source->owner,NULL,error)) return false;
    return qa_cvars_set(bots->application->cvars,"mapname",map,true,error) &&
        qa_cvars_set(bots->application->cvars,"sv_mapname",map,true,error);
}
bool application_bots_publish(qa_application *application,const qa_launch_choices *choices,
                               const qa_bsp_view *geometry,const qa_entities *entities,qa_error *error) {
    if(!application_bots_prepare(application,choices,geometry,entities,error)) return false;
    application_bots *bots=application->bots;bool native=false;
    application_provider *source=bots?bot_source(bots):NULL;
    if(source && (source->kind==APPLICATION_PROVIDER_QVM || source->kind==APPLICATION_PROVIDER_NATIVE) &&
       source->product && source->product->family==QA_GAME_Q3 && q3g_engine(source)) return true;
    if(source && source->kind==APPLICATION_PROVIDER_Q3)
        return application_bots_native_q3_initialize(source,error);
    for(size_t i=0;i<choices->seat_count;++i) native=native || choices->seats[i].bot;
    if(!native) return true;
    if(!bots || bots->population) return application_fail(error,QA_ERROR_ARGUMENT,"native bot population was already published");
    if(!application_bots_shared_construct(bots,false,error)) return false;
    if(!qa_bot_runtime_initialized(bots->runtime)) {
        qa_cvars *configuration=bot_configuration(bots);
        const qa_cvar_view *game_type=configuration?qa_cvars_find(configuration,"g_gametype"):NULL;
        if(!qa_bot_library_variable_set(qa_bot_runtime_library(bots->runtime),"g_gametype",
                game_type?game_type->value:"0",error)) return false;
        int32_t result;
        if(!qa_bot_runtime_setup(bots->runtime,&result,error)) return false;
        if(result) return application_fail(error,QA_ERROR_NOT_FOUND,"native shared bot library setup rejected required resources");
    }
    const char *name=qa_strings_cstr(qa_session_strings(application->session),application->current_map);
    if(!qa_bot_runtime_loaded(bots->runtime) && !qa_bot_runtime_load_map(bots->runtime,name,error)) return false;
    for(uint32_t i=0;i<bots->capacity;++i) {
        bots->seats[i].seat=choices->seats[i].id;
        if(!choices->seats[i].bot) continue;
        if(!qa_application_player_actor(application,choices->seats[i].id,&bots->seats[i].actor) ||
           !application_bot_navigation_bind(bots,&bots->seats[i],error)) return false;
        bots->seats[i].library_client=bots->seats[i].actor.slot;
    }
    if(!qa_bot_runtime_weapon_allocate(bots->runtime,&bots->metadata_weapon,error) || !bots->metadata_weapon) return false;
    qa_bot_services ai=application_bots_services(bots);
    if(!qa_bots_create(bots->runtime,&ai,&bots->population,error)) return false;
    for(uint32_t i=0;i<bots->capacity;++i) {
        if(!choices->seats[i].bot) continue;
        if(bots->shared_world) {
            qa_actor_id actor=bots->seats[i].actor;int32_t client;
            if(!bot_source_client(bots,actor,&client,error) ||
               !application_bot_transport_open(bots->transport,(uint32_t)client,actor,error)) return false;
            const char *actual="";
            for(size_t n=0;application->players && n<application->players->count;++n)
                if(qa_actor_id_equal(application->players->records[n].actor,actor)) {
                    actual=application->players->records[n].userinfo?application->players->records[n].userinfo:"";break;
                }
            char info[1024],character[160],skill[48];size_t length=strlen(actual);
            if(length>=sizeof(info)) length=sizeof(info)-1;
            memcpy(info,actual,length);info[length]=0;
            if(!character_path(bots,choices->seats[i].name,character,error)) return false;
            snprintf(skill,sizeof(skill),"%.9g",(double)choices->seats[i].bot_skill);
            if(!qa_q3_info_set(info,sizeof(info),"characterfile",character,error) ||
               !qa_q3_info_set(info,sizeof(info),"skill",skill,error) ||
               !qa_q3_info_set(info,sizeof(info),"team",choices->seats[i].team?choices->seats[i].team:"",error) ||
               !application_bot_world_userinfo_set(bots->shared_world,(uint32_t)client,info,error) ||
               !application_bot_world_activate(bots->shared_world,(uint32_t)client,error)) return false;
            const char *rejection=NULL;
            if(!application_bot_world_connect_client(bots->shared_world,(uint32_t)client,true,true,&rejection,error)) return false;
            if(rejection) {
                if(!application_bot_world_drop(bots->shared_world,(uint32_t)client,rejection,error)) return false;
                continue;
            }
            if(!application_bot_world_begin(bots->shared_world,(uint32_t)client,error)) return false;
            continue;
        }
        char character[160];
        if(!character_path(bots,choices->seats[i].name,character,error)) return false;
        application_provider *source=bot_source(bots);
        const qa_product *product=source?source->product:NULL;
        int32_t entity;if(!application_bot_entity_number(bots,bots->seats[i].actor,&entity,error)) return false;
        qa_bot_admission admission={.actor=bots->seats[i].actor,.client=bots->seats[i].library_client,.entity=entity,
            .character_file=character,.name=choices->seats[i].name?choices->seats[i].name:"bot",
            .skill=choices->seats[i].bot_skill,.team=choices->seats[i].team,.mode=application->primary_mode,
            .team_arena=product && product->family==QA_GAME_Q3 && !strcmp(product->campaign,"missionpack")};
        if(!qa_bots_admit(bots->population,&admission,error)) return false;
    }
    return true;
}

bool application_bots_native_q3_initialize(application_provider *provider,qa_error *error) {
    qa_application *app=provider?provider->application:NULL;
    application_bots *bots=app?app->bots:NULL;
    if(!bots || provider->kind!=APPLICATION_PROVIDER_Q3 || bot_source(bots)!=provider ||
       !provider->constructed || !provider->attached || !provider->map_bound ||
       provider->close_pending || bots->restoring || bots->calls || bots->producing)
        return application_fail(error,QA_ERROR_ARGUMENT,"native source BotAISetup requires its actual live prepared map owner");
    if(bots->population) return true;
    if(!qa_bot_runtime_initialized(bots->runtime)) {
        const char *game_type;int32_t result;
        if(!application_native_q3_settings_string(provider,"g_gametype",&game_type,error) ||
           !qa_bot_library_variable_set(qa_bot_runtime_library(bots->runtime),"g_gametype",game_type,error) ||
           !qa_bot_runtime_setup(bots->runtime,&result,error)) return false;
        if(result) return application_fail(error,QA_ERROR_NOT_FOUND,"native source BotAISetup rejected required bot library resources");
    }
    const char *name=qa_strings_cstr(qa_session_strings(app->session),app->current_map);
    if(!qa_bot_runtime_loaded(bots->runtime) && !qa_bot_runtime_load_map(bots->runtime,name,error)) return false;
    if(!bots->metadata_weapon &&
       (!qa_bot_runtime_weapon_allocate(bots->runtime,&bots->metadata_weapon,error) || !bots->metadata_weapon)) return false;
    qa_bot_services services=application_bots_services(bots);
    return qa_bots_create(bots->runtime,&services,&bots->population,error);
}

static float bot_source_atof(const char *text) {
    size_t cursor=0;
    while(text[cursor] && ((unsigned char)text[cursor]>=128 || (unsigned char)text[cursor]<=32)) ++cursor;
    int sign=1;
    if(text[cursor]=='+' || text[cursor]=='-') {sign=text[cursor]=='-'?-1:1;++cursor;}
    volatile float value=0;
    while(text[cursor]>='0' && text[cursor]<='9') {
        volatile float product=value*10.0f;
        value=product+(float)(text[cursor++]-'0');
    }
    if(text[cursor]=='.') {
        ++cursor;
        volatile float fraction=.1f;
        while(text[cursor]>='0' && text[cursor]<='9') {
            volatile float product=(float)(text[cursor++]-'0')*fraction;
            value=value+product;fraction=fraction*.1f;
        }
    }
    return value*(float)sign;
}
bool application_bots_shared_connect(application_bots *bots,uint32_t client,bool restart,bool *accepted,qa_error *error) {
    if(!bots || !bots->shared_world || !bots->population || !accepted || bots->restoring || bots->calls || bots->producing)
        return application_fail(error,QA_ERROR_ARGUMENT,"shared G_BotConnect requires its actual initialized population");
    *accepted=false;
    qa_actor_id actor=application_bot_transport_actor(bots->transport,client);
    if(!actor.registry) return application_fail(error,QA_ERROR_NOT_FOUND,"shared G_BotConnect lacks its actual local connection");
    application_bot_seat *seat=NULL;
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!bots->seats[i].retired && qa_actor_id_equal(bots->seats[i].actor,actor)) {seat=bots->seats+i;break;}
    if(!seat) return application_fail(error,QA_ERROR_NOT_FOUND,"shared G_BotConnect lacks its actual navigation seat");
    const char *info=application_bot_world_userinfo(bots->shared_world,client);
    char character[144],team[144],skill_text[1024],name[144];
    qa_q3_client_info_value(info,"characterfile",character,sizeof(character));
    qa_q3_client_info_value(info,"team",team,sizeof(team));
    qa_q3_client_info_value(info,"skill",skill_text,sizeof(skill_text));
    qa_q3_client_info_value(info,"name",name,sizeof(name));
    float skill=bot_source_atof(skill_text);
    if(!isfinite(skill)) return application_fail(error,QA_ERROR_FORMAT,"shared bot skill is outside its finite source domain");
    qa_bot_admission admission={.actor=actor,.client=seat->library_client,.entity=(int32_t)client,
        .character_file=character,.name=name,.team=team,.skill=skill,.mode=bots->application->primary_mode,.restart=restart};
    ++bots->calls;qa_error setup_error={0};bool okay=qa_bots_admit(bots->population,&admission,&setup_error);--bots->calls;
    if(okay) {*accepted=true;return true;}
    if(qa_bots_setup_failed(bots->population,actor)) return true;
    if(error) *error=setup_error;return false;
}

bool application_bots_native_q3_connect(application_provider *provider,qa_actor_id actor,
    bool restart,bool *accepted,qa_error *error) {
    qa_application *app=provider?provider->application:NULL;
    application_bots *bots=app?app->bots:NULL;
    uint32_t source_client;
    if(!accepted || !bots || provider->kind!=APPLICATION_PROVIDER_Q3 || bot_source(bots)!=provider ||
       !bots->population || !qa_bot_runtime_initialized(bots->runtime) || !qa_bot_runtime_loaded(bots->runtime) ||
       bots->restoring || bots->calls || bots->producing || provider->close_pending ||
       !qa_q3_native_client_slot(provider->state.q3,actor,&source_client,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"G_BotConnect requires the initialized source bot population");
    *accepted=false;
    const char *actual;
    if(!application_native_q3_wire_userinfo_read(provider,source_client,&actual,error)) return false;
    char info[1024],character[144],team[144],skill_text[1024],name[144];
    size_t size=strlen(actual);if(size>=sizeof(info)) size=sizeof(info)-1;
    memcpy(info,actual,size);info[size]=0;
    qa_q3_client_info_value(info,"characterfile",character,sizeof(character));
    qa_q3_client_info_value(info,"team",team,sizeof(team));
    qa_q3_client_info_value(info,"skill",skill_text,sizeof(skill_text));
    qa_q3_client_info_value(info,"name",name,sizeof(name));
    float skill=bot_source_atof(skill_text);
    if(!isfinite(skill)) return application_fail(error,QA_ERROR_FORMAT,"bot source skill is outside its finite character domain");
    application_bot_seat *seat=NULL;
    uint32_t round_seat=0,library_client=actor.slot;bool retained=false;
    if(!application_bots_round_client_binding(bots,source_client,&round_seat,&library_client,&retained,error)) return false;
    if(retained) seat=bots->seats+round_seat;
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!bots->seats[i].retired && qa_actor_id_equal(bots->seats[i].actor,actor)) {seat=bots->seats+i;break;}
    if(!seat || !qa_actor_id_equal(seat->actor,actor)) {
        if(!seat) {
        for(uint32_t i=0;i<bots->capacity;++i)
            if(!bots->seats[i].actor.registry || bots->seats[i].retired) {seat=bots->seats+i;break;}
        if(!seat) {
            if(bots->capacity==UINT32_MAX || (size_t)bots->capacity+1>SIZE_MAX/sizeof(*bots->seats))
                return application_fail(error,QA_ERROR_MEMORY,"native bot navigation roster exceeds its extent");
            application_bot_seat *seats=realloc(bots->seats,((size_t)bots->capacity+1)*sizeof(*seats));
            if(!seats) return application_fail(error,QA_ERROR_MEMORY,"growing native bot navigation roster");
            bots->seats=seats;seat=bots->seats+bots->capacity++;*seat=(application_bot_seat){0};
        }
        }
        uint32_t local_seat=UINT32_MAX;
        for(size_t i=0;app->players && i<app->players->count;++i)
            if(qa_actor_id_equal(app->players->records[i].actor,actor)) {local_seat=app->players->records[i].seat;break;}
        qa_bot_navigation_destroy(seat->navigation);
        *seat=(application_bot_seat){.actor=actor,.seat=local_seat,.library_client=library_client};
        if(!application_bot_navigation_bind(bots,seat,error)) return false;
    }
    qa_bot_admission admission={.actor=actor,.client=seat->library_client,.entity=(int32_t)source_client,
        .character_file=character,.name=name,.team=team,.skill=skill,.mode=app->primary_mode,
        .team_arena=bots->population && provider->product && !strcmp(provider->product->campaign,"missionpack"),
        .restart=restart};
    ++bots->calls;
    qa_error setup_error={0};bool okay=qa_bots_admit(bots->population,&admission,&setup_error);
    --bots->calls;
    if(okay) {*accepted=true;return true;}
    if(qa_bots_setup_failed(bots->population,actor))
        return application_native_q3_wire_drop(provider,source_client,"BotAISetupClient failed",error);
    if(error) *error=setup_error;
    return false;
}

bool application_native_q3_match_bots_end(application_provider *provider,qa_error *error) {
    application_bots *bots=provider && provider->application?provider->application->bots:NULL;
    if(!bots || bot_source(bots)!=provider || provider->kind!=APPLICATION_PROVIDER_Q3 ||
       !bots->population || bots->restoring || provider->close_pending)
        return application_fail(error,QA_ERROR_ARGUMENT,"source ExitLevel requires its real bot match owner");
    bool nested=qa_bots_interbreed_end_admitted(bots->population);
    if((bots->calls || bots->producing) && !nested)
        return application_fail(error,QA_ERROR_ARGUMENT,"source bot match end has no actual nested ExitLevel admission");
    ++bots->calls;bool okay=qa_bots_interbreed_end_match(bots->population,error);--bots->calls;return okay;
}

bool application_bots_test_aas(application_provider *provider,qa_vec3 origin,qa_error *error) {
    application_bots *bots=provider && provider->application?provider->application->bots:NULL;
    if(!bots || bot_source(bots)!=provider || provider->kind!=APPLICATION_PROVIDER_Q3 ||
       !bots->population || bots->restoring || bots->calls || bots->producing || provider->close_pending)
        return application_fail(error,QA_ERROR_ARGUMENT,"BotTestAAS requires its real initialized source bot owner");
    ++bots->calls;bool okay=qa_bots_test_aas(bots->population,origin,error);--bots->calls;return okay;
}
