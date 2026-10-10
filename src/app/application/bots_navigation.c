#include "bots_private.h"
#include "native_q3_console.h"
#include "qa/bots_memory.h"
#include "qa/game_q3_wire.h"
#include "qa/game_q3_source.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

qa_bot_navigation *application_bot_navigation(void *opaque,int32_t client) {
    application_bots *bots=opaque;
    if(client<0) {
        return bots->map_navigation;
    }
    qa_actor_id actor=application_bot_client_actor(bots,client);
    if(!actor.registry) return NULL;
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!bots->seats[i].retired && bots->seats[i].actor.registry &&
           qa_actor_id_equal(bots->seats[i].actor,actor)) return bots->seats[i].navigation;
    for(application_bot_target *target=bots->targets;target;target=target->next)
        if(qa_actor_id_equal(target->actor,actor) &&
           qa_actors_get(qa_session_actors(bots->application->session),target->actor)) return target->navigation;
    return NULL;
}
bool application_bot_movement_input(void *opaque,qa_actor_id actor,qa_movement_input *out,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;
    qa_player_state view;
    if(!qa_application_control_read(application,actor,&view))
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot prediction actor has no selected movement");
    application_control_record *control=&application->controls[actor.slot];
    qa_combat_state combat;qa_body_state body;
    if(!qa_combat_read_traits(application->combat,actor,&combat,error) ||
       !qa_world_body_read(application->world,actor,&body,error)) return false;
    *out=(qa_movement_input){.actor=actor,.state=view.state,.profile=view.profile,
        .shape={QA_SHAPE_BOX,view.bounds},.current_bounds=view.bounds,.has_current_bounds=true,
        .standing={control->player.standing_bounds,view.view_height},
        .crouched={view.bounds,view.view_height},.dead={view.bounds,view.view_height},
        .time_ns=qa_session_elapsed(application->session),.prediction=true,.view_offset=view.view_offset,
        .q2r_pml_origin=&control->q2r_pml_origin,.environment=qa_movement_environment_default()};
    out->environment.health=combat.health;
    out->environment.flight=view.flight;
    out->environment.gravity_multiplier=view.gravity_multiplier;
    out->environment.has_body_bounds=true;out->environment.body_bounds=body.bounds;
    return true;
}
static qa_actor_id source_actor(void *opaque,int32_t number) {
    application_bots *bots=opaque;
    if(number<0) return (qa_actor_id){0};
    uint32_t cursor=(uint32_t)number;const qa_actor_record *actor;
    return qa_actors_next(qa_session_actors(bots->application->session),&cursor,&actor) &&
        actor->id.slot==(uint32_t)number?actor->id:(qa_actor_id){0};
}
static int32_t source_model(void *opaque,qa_actor_id actor) {
    application_bots *bots=opaque;qa_actor_collision collision;
    return qa_world_get_collision(bots->application->world,actor,&collision,NULL) && collision.inline_model?
        (int32_t)collision.model:0;
}
static uint64_t revision(void *opaque) {
    application_bots *bots=opaque;
    return qa_session_elapsed(bots->application->session);
}
typedef struct application_bot_mover {
    qa_actor_id activation;
    qa_nav_entity_state navigation;
    qa_bot_model_kind kind;
    uint32_t inline_model;
    bool has_inline_model,shootable,useable;
} application_bot_mover;
static bool native_mover(application_bots *bots,qa_actor_id actor,application_bot_mover *out,bool *found,qa_error *error) {
    qa_application *application=bots->application;*found=false;
    const qa_actor_record *record=qa_actors_get(qa_session_actors(application->session),actor);
    if(!record) return true;
    qa_actor_owner owner=record->owner;
    application_provider **providers=application->routing_providers?application->routing_providers:application->providers;
    size_t count=application->routing_providers?application->routing_provider_count:application->provider_count;
    for(size_t i=0;i<count;++i) {
        application_provider *provider=providers[i];
        if(provider->owner!=owner || (provider->kind!=APPLICATION_PROVIDER_Q1 &&
                                     provider->kind!=APPLICATION_PROVIDER_Q2)) continue;
        if(bots->mover_borrowed)
            return application_fail(error,QA_ERROR_ARGUMENT,"bot mover observation is already borrowed");
        size_t capacity=qa_actors_capacity(qa_session_actors(application->session));
        if(capacity>bots->train_capacity) {
            if(capacity>SIZE_MAX/sizeof(*bots->train_stops))
                return application_fail(error,QA_ERROR_MEMORY,"bot train stop storage overflow");
            qa_nav_train_stop *stops=realloc(bots->train_stops,capacity*sizeof(*stops));
            if(!stops) return application_fail(error,QA_ERROR_MEMORY,"retaining bot authored train stops");
            bots->train_stops=stops;bots->train_capacity=capacity;
        }
        size_t stops;bool ok;
        bots->mover_borrowed=true;
        if(provider->kind==APPLICATION_PROVIDER_Q1) {
            qa_q1_map_mover_view view;
            ok=qa_q1_game_map_mover_read(provider->state.q1,actor,&view,bots->train_stops,bots->train_capacity,&stops,found,error);
            if(ok && *found) *out=(application_bot_mover){.activation=view.activation,.navigation=view.navigation,
                .kind=view.kind==QA_Q1_MOVER_ELEVATOR?QA_BOT_MODEL_ELEVATOR:
                    view.kind==QA_Q1_MOVER_TRAIN?QA_BOT_MODEL_TRAIN:view.kind==QA_Q1_MOVER_BOBBING?QA_BOT_MODEL_BOBBING:
                    view.kind==QA_Q1_MOVER_DOOR?QA_BOT_MODEL_DOOR:QA_BOT_MODEL_STATIC,
                .inline_model=view.inline_model,.has_inline_model=view.has_inline_model,
                .shootable=view.shootable,.useable=view.useable};
        } else {
            qa_q2_map_mover_view view;
            ok=qa_q2_entity_mover_read(provider->state.q2,actor,&view,bots->train_stops,bots->train_capacity,&stops,found,error);
            if(ok && *found) *out=(application_bot_mover){.activation=view.activation,.navigation=view.navigation,
                .kind=view.kind==QA_Q2_MOVER_ELEVATOR?QA_BOT_MODEL_ELEVATOR:
                    view.kind==QA_Q2_MOVER_TRAIN?QA_BOT_MODEL_TRAIN:view.kind==QA_Q2_MOVER_DOOR?QA_BOT_MODEL_DOOR:QA_BOT_MODEL_STATIC,
                .inline_model=view.inline_model,.has_inline_model=view.has_inline_model,
                .shootable=view.shootable,.useable=view.useable};
        }
        bots->mover_borrowed=false;
        if(!qa_actors_get(qa_session_actors(application->session),actor)) *found=false;
        return ok;
    }
    return true;
}
bool application_bot_static_ground(void *opaque,int32_t entity,bool *out,qa_error *error) {
    application_bots *bots=opaque;*out=false;
    application_provider *source=application_bot_source(bots);
    if(!source || !source->constructed || !source->attached || source->close_pending || bots->restoring)
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot static ground lost its actual Source owner");
    qa_actor_id actor=application_bot_actor(bots,entity);
    if(!actor.registry) return true;
    qa_application *application=bots->application;
    qa_physics *physics=application->physics;
    if(!physics || !physics->services.read)
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot static ground has no actual physics observation");
    qa_physics_properties state;
    bool found=physics->services.read(physics->services.context,actor,&state);
    if(source!=application_bot_source(bots) || !source->constructed || !source->attached ||
       source->close_pending || bots->restoring)
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot static ground Source owner retired during observation");
    if(!found || !qa_actors_get(qa_session_actors(application->session),actor)) return true;
    *out=state.motion==QA_PHYSICS_STATIONARY &&
        (state.solid==QA_PHYSICS_BOX || state.solid==QA_PHYSICS_BRUSH) &&
        !(state.flags&(QA_PHYSICS_PLAYER|QA_PHYSICS_MONSTER|QA_PHYSICS_DEAD));
    return true;
}
bool application_bot_travel_model(void *opaque,int32_t model,qa_bot_travel_model *out,bool *found,qa_error *error) {
    application_bots *bots=opaque;*found=false;
    const qa_actor_registry *actors=qa_session_actors(bots->application->session);
    uint32_t cursor=0;const qa_actor_record *record;
    while(qa_actors_next(actors,&cursor,&record)) {
        qa_actor_id actor=record->id;application_bot_mover mover;bool observed;
        if(!native_mover(bots,actor,&mover,&observed,error)) return false;
        if(!observed || !mover.has_inline_model || mover.inline_model!=(uint32_t)model) continue;
        qa_body_state body;if(!qa_world_body_read(bots->application->world,actor,&body,error)) return false;
        *out=(qa_bot_travel_model){.actor=actor,.entity=(int32_t)actor.slot,.origin=body.origin,
            .bounds=body.bounds,.kind=mover.kind};
        *found=qa_actors_get(actors,actor)!=NULL;return true;
    }
    return true;
}
bool application_bot_activation(void *opaque,qa_actor_id bot,int32_t number,qa_bot_activation *out,bool *found,qa_error *error) {
    application_bots *bots=opaque;*found=false;
    qa_actor_id blocker=application_bot_actor(bots,number);if(!blocker.registry) return true;
    application_bot_mover mover;bool observed;
    if(!native_mover(bots,blocker,&mover,&observed,error)) return false;
    if(!observed || !mover.activation.registry || (!mover.shootable && !mover.useable)) return true;
    qa_body_state body,target;
    if(!qa_world_body_read(bots->application->world,blocker,&body,error) ||
       !qa_world_body_read(bots->application->world,mover.activation,&target,error)) return false;
    application_bot_seat *seat=NULL;
    for(uint32_t i=0;i<bots->capacity;++i) if(qa_actor_id_equal(bots->seats[i].actor,bot)) {seat=&bots->seats[i];break;}
    if(!seat || seat->retired) return true;
    qa_vec3 center=qa_vec_add(target.origin,qa_vec_scale(qa_vec_add(target.bounds.mins,target.bounds.maxs),.5f));
    qa_bounds local={qa_vec_sub(qa_vec_add(target.origin,target.bounds.mins),center),
                     qa_vec_sub(qa_vec_add(target.origin,target.bounds.maxs),center)};
    qa_vec3 origin;uint32_t area;
    if(!qa_bot_navigation_best(seat->navigation,center,local,&origin,&area,error)) return false;
    if(!area) return true;
    *out=(qa_bot_activation){.blocker=blocker,.target=mover.activation,.blocker_origin=body.origin,
        .target_origin=target.origin,.aim=center,.shoot=mover.shootable,
        .goal={.origin=origin,.area=(int32_t)area,.mins=qa_vec_sub(qa_vec_add(target.origin,target.bounds.mins),origin),
            .maxs=qa_vec_sub(qa_vec_add(target.origin,target.bounds.maxs),origin),.entity=(int32_t)mover.activation.slot}};
    *found=qa_actors_get(qa_session_actors(bots->application->session),blocker) &&
           qa_actors_get(qa_session_actors(bots->application->session),mover.activation);return true;
}
typedef struct source_activation_call {
    application_bots *bots;
    application_provider *source;
    qa_q3_game *game;
    qa_actor_id actor;
    qa_bot_navigation *navigation;
    const qa_entities *entities;
    const qa_bot_activation_query *query;
} source_activation_call;
static bool activation_current(const source_activation_call *call,qa_error *error) {
    application_provider *source=call->source;
    if(call->bots->source!=source || source->kind!=APPLICATION_PROVIDER_Q3 ||
       !source->constructed || !source->attached || source->close_pending ||
       source->state.q3!=call->game || call->bots->restoring ||
       (call->actor.registry && !qa_actors_get(qa_session_actors(call->bots->application->session),call->actor)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Source activation lost its actual GAME owner");
    return true;
}
bool application_bot_source_model_bounds(void *opaque,int32_t model,int32_t type,int32_t contents,
    qa_vec3 *mins,qa_vec3 *maxs,int32_t *entity,qa_error *error) {
    application_bots *bots=opaque;
    if(!bots || !entity || !bots->runtime || bots->calls==SIZE_MAX)
        return application_fail(error,QA_ERROR_ARGUMENT,"Source model lookup requires its actual GAME owner");
    application_provider *source=bots->source;
    source_activation_call call={.bots=bots,.source=source,
        .game=source && source->kind==APPLICATION_PROVIDER_Q3?source->state.q3:NULL};
    if(!source || !activation_current(&call,error) || !qa_bot_runtime_lease_begin(bots->runtime,error)) return false;
    ++bots->calls;bool okay=true,matched=false;
    for(uint32_t slot=0;okay;++slot) {
        uint32_t count;qa_q3_source_binding binding;
        if(!qa_q3_source_entity_count(call.game,&count,error)) {okay=false;break;}
        if(slot>=count) break;
        if(!qa_q3_source_binding_read(call.game,slot,&binding,error)) {okay=false;break;}
        if(!binding.in_use) continue;
        qa_q3_source_model value;
        if(!qa_q3_source_model_read(call.game,slot,&value,error)) {okay=false;break;}
        if(type && value.type!=type) continue;
        if(contents) {
            int32_t actual;
            if(!qa_q3_source_contents_read(call.game,slot,&actual,error) ||
               !activation_current(&call,error)) {okay=false;break;}
            if(actual!=contents) continue;
            /* The reached contents owner may change s.modelindex. */
            if(!qa_q3_source_model_read(call.game,slot,&value,error)) {okay=false;break;}
        }
        if(value.model!=model) continue;
        if(mins || maxs) {
            if(!qa_q3_source_model_bounds_read(call.game,slot,mins,maxs,error) ||
               !activation_current(&call,error)) {okay=false;break;}
        }
        *entity=(int32_t)slot;matched=true;break;
    }
    if(okay && !matched) {
        if(mins) *mins=(qa_vec3){0};
        if(maxs) *maxs=(qa_vec3){0};
        *entity=0;
    }
    --bots->calls;qa_bot_runtime_lease_end(bots->runtime);return okay;
}
static bool activation_string(const qa_entities *entities,int32_t entity,const char *key,
                              char *out,size_t capacity) {
    qa_bytes value;out[0]=0;
    if(!qa_bot_bsp_value(entities,entity,key,&value)) return false;
    size_t n=0;
    while(n<value.size && n+1<capacity && value.data[n]) {out[n]=(char)value.data[n];++n;}
    out[n]=0;return true;
}
static bool activation_model(const qa_entities *entities,int32_t entity,size_t capacity,
                             int32_t *model,qa_error *error) {
    char text[1024];*model=0;
    if(!activation_string(entities,entity,"model",text,capacity) || !text[0]) return true;
    return qa_bot_bsp_parse_integer((qa_bytes){(const uint8_t *)text+1,strlen(text+1)},model,error);
}
static bool activation_vector(const source_activation_call *call,
    bool (*read)(void *,qa_vec3 *,qa_error *),qa_vec3 *out,qa_error *error) {
    return read(call->query->context,out,error) && activation_current(call,error);
}
static bool activation_area(const source_activation_call *call,int32_t *out,qa_error *error) {
    return call->query->area(call->query->context,out,error) && activation_current(call,error);
}
static bool activation_reachable(const source_activation_call *call,int32_t area) {
    return area>0 && qa_bot_navigation_area(call->navigation,(uint32_t)area).reach_count!=0;
}
static bool activation_stand(const source_activation_call *call,int32_t entity,
                             qa_bot_source_activation *out,qa_error *error) {
    out->goal.entity=entity;out->goal.number=0;out->goal.flags=0;
    if(!activation_vector(call,call->query->origin,&out->goal.origin,error) ||
       !activation_area(call,&out->goal.area,error)) return false;
    out->goal.mins=(qa_vec3){-8,-8,-8};out->goal.maxs=(qa_vec3){8,8,8};return true;
}
static void activation_expand(qa_bot_goal *goal,qa_vec3 direction) {
    goal->mins=qa_vec_add(goal->mins,(qa_vec3){direction.x<0?0:fabsf(direction.x),
        direction.y<0?0:fabsf(direction.y),direction.z<0?0:fabsf(direction.z)});
    goal->maxs=qa_vec_add(goal->maxs,(qa_vec3){direction.x<0?fabsf(direction.x):0,
        direction.y<0?fabsf(direction.y):0,direction.z<0?fabsf(direction.z):0});
}
static bool activation_button(const source_activation_call *call,int32_t bsp,
                              qa_bot_source_activation *out,bool *found,qa_error *error) {
    *found=false;out->shoot=false;out->target=(qa_vec3){0};
    int32_t model,entity;qa_vec3 mins,maxs;
    if(!activation_model(call->entities,bsp,128,&model,error)) return false;
    if(!model) return true;
    if(!application_bot_source_model_bounds(call->bots,model,4,0,&mins,&maxs,&entity,error) ||
       !activation_current(call,error)) return false;
    float lip,angle,health;bool present;
    if(!qa_bot_bsp_float(call->entities,bsp,"lip",&lip,&present,error) ||
       !qa_bot_bsp_float(call->entities,bsp,"angle",&angle,&present,error)) return false;
    float radians=angle*(float)(3.14159265358979323846*2.0/360.0);
    qa_vec3 direction=angle==-1?(qa_vec3){0,0,1}:angle==-2?(qa_vec3){0,0,-1}:
        (qa_vec3){(float)cos((double)radians),(float)sin((double)radians),-0.0f};
    qa_vec3 size=qa_vec_sub(maxs,mins),origin=qa_vec_scale(qa_vec_add(mins,maxs),.5f);
    float distance=(fabsf(direction.x)*size.x+fabsf(direction.y)*size.y+fabsf(direction.z)*size.z)*.5f;
    if(!qa_bot_bsp_float(call->entities,bsp,"health",&health,&present,error)) return false;
    if(health!=0) {
        out->target=qa_vec_add(origin,qa_vec_scale(direction,-distance));out->shoot=true;
        qa_vec3 eye;qa_trace_result trace;
        if(!activation_vector(call,call->query->eye,&eye,error) ||
           !qa_bot_navigation_trace(call->navigation,eye,out->target,NULL,call->actor,0x06000001u,&trace,error) ||
           !activation_current(call,error)) return false;
        uint32_t hit=trace.hit==QA_TRACE_HIT_WORLD?QA_Q3_SOURCE_WORLD:QA_Q3_SOURCE_NONE;
        if(trace.hit==QA_TRACE_HIT_ACTOR && !qa_q3_source_actor_slot(call->game,trace.actor,&hit,error)) return false;
        if(trace.fraction>=1 || hit==(uint32_t)entity) {
            if(!activation_stand(call,entity,out,error)) return false;
            *found=true;return true;
        }
    }
    qa_bounds bounds=qa_bot_navigation_presence(call->navigation,4);
    distance+=fabsf(direction.x)*fabsf(direction.x<0?bounds.maxs.x:bounds.mins.x);
    distance+=fabsf(direction.y)*fabsf(direction.y<0?bounds.maxs.y:bounds.mins.y);
    distance+=fabsf(direction.z)*fabsf(direction.z<0?bounds.maxs.z:bounds.mins.z);
    qa_vec3 start=qa_vec_add(origin,qa_vec_scale(direction,-distance));start.z+=24;
    qa_vec3 end=start;end.z-=health!=0?512:100;
    qa_aas_crossing crossings[10];size_t count;
    if(!qa_bot_navigation_trace_areas(call->navigation,start,end,crossings,10,&count,error) ||
       !activation_current(call,error)) return false;
    for(size_t i=0;i<count;++i) {
        size_t index=health!=0?count-1-i:i;
        if(!activation_reachable(call,(int32_t)crossings[index].area)) continue;
        out->goal.origin=health!=0?crossings[index].point:origin;
        out->goal.area=(int32_t)crossings[index].area;
        out->goal.mins=health!=0?(qa_vec3){8,8,8}:qa_vec_sub(mins,origin);
        out->goal.maxs=health!=0?(qa_vec3){-8,-8,-8}:qa_vec_sub(maxs,origin);
        activation_expand(&out->goal,direction);
        out->goal.entity=entity;out->goal.number=0;out->goal.flags=0;*found=true;break;
    }
    return true;
}
static bool activation_trigger(const source_activation_call *call,int32_t bsp,
                               qa_bot_source_activation *out,bool *found,qa_error *error) {
    *found=false;out->shoot=false;out->target=(qa_vec3){0};
    int32_t model,entity;qa_vec3 mins,maxs;
    if(!activation_model(call->entities,bsp,128,&model,error)) return false;
    if(!model) return true;
    if(!application_bot_source_model_bounds(call->bots,model,0,0x40000000,&mins,&maxs,&entity,error) ||
       !activation_current(call,error)) return false;
    qa_vec3 origin=qa_vec_scale(qa_vec_add(mins,maxs),.5f),start=origin;start.z+=24;
    qa_vec3 end=start;end.z-=100;
    qa_aas_crossing crossings[10];size_t count;
    if(!qa_bot_navigation_trace_areas(call->navigation,start,end,crossings,10,&count,error) ||
       !activation_current(call,error)) return false;
    for(size_t i=0;i<count;++i) if(activation_reachable(call,(int32_t)crossings[i].area)) {
        out->goal.origin=origin;out->goal.area=(int32_t)crossings[i].area;
        out->goal.mins=qa_vec_sub(mins,origin);out->goal.maxs=qa_vec_sub(maxs,origin);
        out->goal.entity=entity;out->goal.number=0;out->goal.flags=0;*found=true;break;
    }
    return true;
}
static bool activation_report(const source_activation_call *call,bool developer,
                              const char *format,const char *value,qa_error *error) {
    if(developer) {
        bool enabled;
        if(!call->query->developer(call->query->context,&enabled,error) ||
           !activation_current(call,error)) return false;
        if(!enabled) return true;
    }
    char text[1280];snprintf(text,sizeof(text),format,value);
    return application_native_q3_console_print(call->source,text,error) && activation_current(call,error);
}
static bool activation_construct(const source_activation_call *call,int32_t blocker,
                                qa_bot_source_activation *out,int32_t *result,qa_error *error) {
    memset(out,0,sizeof(*out));*result=0;
    qa_bot_entity_info cached;bool found;
    if(!qa_bot_runtime_entity(call->bots->runtime,blocker,&cached,&found,error)) return false;
    char model[1024],text[128],classname[128];
    snprintf(model,sizeof(model),"*%d",cached.state.model_index);
    int32_t bsp=qa_bot_bsp_next(call->entities,0);
    for(;bsp;bsp=qa_bot_bsp_next(call->entities,bsp))
        if(activation_string(call->entities,bsp,"model",text,sizeof(text)) && !strcmp(text,model)) break;
    if(!bsp) return activation_report(call,false,"^1Error: BotGetActivateGoal: no entity found with model %s\n",model,error);
    activation_string(call->entities,bsp,"classname",classname,sizeof(classname));
    if(!strcmp(classname,"func_door")) {
        float health;bool present;
        if(!qa_bot_bsp_float(call->entities,bsp,"health",&health,&present,error)) return false;
        if(present && health!=0) {
            int32_t number,entity;qa_vec3 mins,maxs;
            if(!activation_model(call->entities,bsp,1024,&number,error)) return false;
            if(number) {
                if(!application_bot_source_model_bounds(call->bots,number,4,0,&mins,&maxs,&entity,error) ||
                   !activation_current(call,error)) return false;
                out->target=qa_vec_scale(qa_vec_add(mins,maxs),.5f);out->shoot=true;
                if(!activation_stand(call,entity,out,error)) return false;
            }
            *result=bsp;return true;
        }
        int32_t flags;qa_vec3 origin;
        if(!qa_bot_bsp_integer(call->entities,bsp,"spawnflags",&flags,&present,error)) return false;
        if(flags&1) return true;
        if(!qa_bot_bsp_vector(call->entities,bsp,"origin",&origin,&present,error)) return false;
        if(origin.x!=cached.state.origin.x || origin.y!=cached.state.origin.y || origin.z!=cached.state.origin.z) return true;
        activation_string(call->entities,bsp,"model",model,sizeof(model));
        int32_t number;
        if(!activation_model(call->entities,bsp,1024,&number,error)) return false;
        if(number) {
            qa_vec3 mins,maxs;int32_t entity;uint32_t areas[64];size_t count;
            if(!application_bot_source_model_bounds(call->bots,number,4,0,&mins,&maxs,&entity,error) ||
               !activation_current(call,error) ||
               !qa_bot_navigation_bbox_areas(call->navigation,(qa_bounds){mins,maxs},areas,64,&count,error) ||
               !activation_current(call,error)) return false;
            for(size_t pass=0;pass<2;++pass) for(size_t i=0;i<count && out->area_count<32;++i) {
                qa_bot_nav_area area=qa_bot_navigation_area(call->navigation,areas[i]);
                if((area.reach_count!=0)==(pass==0) && (area.contents&1024u))
                    out->areas[out->area_count++]=(int32_t)areas[i];
            }
        }
    }
    if(!strcmp(classname,"func_button")) return true;
    char targets[10][128];int32_t next[10];
    if(!activation_string(call->entities,bsp,"targetname",targets[0],sizeof(targets[0])))
        return activation_report(call,true,"^1Error: BotGetActivateGoal: entity with model \"%s\" has no targetname\n",model,error);
    next[0]=qa_bot_bsp_next(call->entities,0);
    for(int depth=0;depth>=0 && depth<10;) {
        int32_t candidate=next[depth];
        for(;candidate;candidate=qa_bot_bsp_next(call->entities,candidate))
            if(activation_string(call->entities,candidate,"target",text,sizeof(text)) && !strcmp(text,targets[depth])) {
                next[depth]=qa_bot_bsp_next(call->entities,candidate);break;
            }
        if(!candidate) {
            if(!activation_report(call,true,"^1Error: BotGetActivateGoal: no entity with target \"%s\"\n",targets[depth],error)) return false;
            --depth;continue;
        }
        if(!activation_string(call->entities,candidate,"classname",classname,sizeof(classname))) {
            if(!activation_report(call,true,"^1Error: BotGetActivateGoal: entity with target \"%s\" has no classname\n",targets[depth],error)) return false;
            continue;
        }
        if(!strcmp(classname,"func_button") || !strcmp(classname,"trigger_multiple")) {
            bool made;
            bool okay=!strcmp(classname,"func_button")?activation_button(call,candidate,out,&made,error):
                activation_trigger(call,candidate,out,&made,error);
            if(!okay) return false;
            if(!made) continue;
            qa_bot_source_activation top;bool has_top;
            if(!call->query->top(call->query->context,&top,&has_top,error) || !activation_current(call,error)) return false;
            if(has_top && top.inuse && top.goal.entity==out->goal.entity && top.time>call->query->time &&
               top.start_time<call->query->time-2.0f) continue;
            int32_t area;
            if(!activation_area(call,&area,error)) return false;
            if(activation_reachable(call,area)) {
                if(!out->areas_disabled) {
                    for(int32_t i=0;i<out->area_count;++i) {
                        bool previous;
                        if(!qa_bot_navigation_enable(call->navigation,(uint32_t)out->areas[i],false,&previous,error) ||
                           !activation_current(call,error)) return false;
                    }
                    out->areas_disabled=true;
                }
                qa_vec3 origin;uint32_t flags;qa_bot_nav_route route;
                if(!activation_area(call,&area,error) || !activation_vector(call,call->query->origin,&origin,error) ||
                   !call->query->travel_flags(call->query->context,&flags,error) || !activation_current(call,error) ||
                   !qa_bot_navigation_route(call->navigation,&(qa_bot_nav_route_query){.area=(uint32_t)area,
                       .goal_area=(uint32_t)out->goal.area,.travel_flags=flags,.origin=origin,.has_origin=true},&route,error) ||
                   !activation_current(call,error)) return false;
                if(!route.travel_time) continue;
                out->time=(call->query->time+(float)route.travel_time*.01f)+5.0f;
            }
            *result=candidate;return true;
        }
        if(!strcmp(classname,"target_relay") || !strcmp(classname,"target_delay")) {
            if(activation_string(call->entities,candidate,"targetname",text,sizeof(text))) {
                if(depth==9) return application_fail(error,QA_ERROR_ARGUMENT,"BotGetActivateGoal activation chain exceeds the source ten-level target allocation");
                ++depth;memcpy(targets[depth],text,sizeof(text));next[depth]=qa_bot_bsp_next(call->entities,0);
            }
        }
    }
    const qa_cvar_view *debug=qa_cvars_find(application_native_q3_console_registry(call->source),"com_botObstacleDebug");
    if(debug && debug->integer)
        return activation_report(call,false,"^1Error: BotGetActivateGoal: no valid activator for entity with target \"%s\"\n",targets[0],error);
    return true;
}
bool application_bot_source_activation(void *opaque,qa_actor_id actor,int32_t blocker,
    const qa_bot_activation_query *query,qa_bot_source_activation *out,int32_t *bsp_entity,qa_error *error) {
    application_bots *bots=opaque;
    if(!bots || !query || !out || !bsp_entity || !query->origin || !query->eye || !query->area ||
       !query->travel_flags || !query->top || !query->developer || !bots->runtime || bots->calls==SIZE_MAX)
        return application_fail(error,QA_ERROR_ARGUMENT,"Source activation requires its actual state readers");
    application_provider *source=bots->source;
    source_activation_call call={.bots=bots,.source=source,.actor=actor,.query=query,
        .game=source && source->kind==APPLICATION_PROVIDER_Q3?source->state.q3:NULL,
        .entities=qa_bot_runtime_bsp(bots->runtime)};
    if(!source || !activation_current(&call,error)) return false;
    uint32_t client;
    if(!qa_q3_native_client_slot(call.game,actor,&client,error)) return false;
    call.navigation=qa_bot_runtime_navigation(bots->runtime,(int32_t)client);
    if(!call.navigation)
        return application_fail(error,QA_ERROR_NOT_FOUND,"Source activation has no actual client routing graph");
    if(!qa_bot_runtime_lease_begin(bots->runtime,error)) return false;
    ++bots->calls;
    bool okay=activation_construct(&call,blocker,out,bsp_entity,error);
    --bots->calls;qa_bot_runtime_lease_end(bots->runtime);return okay;
}
static bool entity(void *opaque,const qa_nav_binding *binding,qa_nav_entity_state *out,bool *found,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;
    *found=false;
    const qa_actor_registry *actors=qa_session_actors(application->session);
    uint32_t cursor=0;const qa_actor_record *record;
    while(qa_actors_next(actors,&cursor,&record)) {
        qa_actor_id actor=record->id;qa_actor_collision collision;
        application_bot_mover mover;bool observed;
        if(!native_mover(bots,actor,&mover,&observed,error)) return false;
        if(observed && ((binding->has_model && mover.has_inline_model && mover.inline_model==(uint32_t)binding->model) ||
                       (!binding->has_model && qa_bounds_overlap(binding->bounds,mover.navigation.bounds)))) {
            *out=mover.navigation;*found=true;return true;
        }
        if(!qa_world_get_collision(application->world,actor,&collision,NULL) ||
           (binding->has_model && (!collision.inline_model || collision.model!=(uint32_t)binding->model))) continue;
        qa_linked_body linked;
        if(!qa_world_linked(application->world,actor,&linked) ||
           (!binding->has_model && !qa_bounds_overlap(binding->bounds,linked.absolute_bounds))) continue;
        qa_body_state body;
        if(!qa_world_body_read(application->world,actor,&body,error)) return false;
        if(!qa_actors_get(actors,actor)) continue;
        *out=(qa_nav_entity_state){.actor=actor,.enabled=true,.bounds=linked.absolute_bounds,
            .velocity=body.velocity,.kind=QA_NAV_ENTITY_GENERIC};
        *found=true;return true;
    }
    return true;
}
static bool navigation_resource(application_bots *bots,qa_resource **saved_resource,
                                qa_vfs_acquisition *saved_acquisition,qa_error *error) {
    qa_launch_resource_origin origin;
    if(!qa_application_map_origin_read(bots->application,&origin) || !origin.catalog ||
       !origin.content || !origin.acquisition ||
       origin.acquisition->resource_id!=qa_resource_id(bots->map_resource))
        return application_fail(error,QA_ERROR_ARGUMENT,"Bot navigation lost its actual geometry acquisition");
    if(!bots->navigation_files && !qa_catalog_open(origin.catalog,origin.product,&bots->navigation_files,error)) return false;
    const char *name=origin.acquisition->path;
    if(!name) return application_fail(error,QA_ERROR_ARGUMENT,"Bot navigation lacks its actual map path");
    size_t stem=strlen(name);
    if(stem>=5 && (name[0]=='m'||name[0]=='M') && (name[1]=='a'||name[1]=='A') &&
       (name[2]=='p'||name[2]=='P') && (name[3]=='s'||name[3]=='S') && name[4]=='/') {
        name+=5;stem-=5;
    }
    if(stem>=4 && name[stem-4]=='.' && (name[stem-3]=='b'||name[stem-3]=='B') &&
       (name[stem-2]=='s'||name[stem-2]=='S') && (name[stem-1]=='p'||name[stem-1]=='P')) stem-=4;
    if(!stem || stem>SIZE_MAX-22) return application_fail(error,QA_ERROR_ARGUMENT,"Invalid navigation map resource path");
    for(size_t start=0;start<=stem;) {
        size_t end=start;
        while(end<stem && name[end]!='/') ++end;
        size_t length=end-start;
        if(!length || (length==1 && name[start]=='.') ||
           (length==2 && name[start]=='.' && name[start+1]=='.'))
            return application_fail(error,QA_ERROR_ARGUMENT,"Invalid navigation map resource path");
        if(end==stem) break;
        start=end+1;
    }
    char *path=malloc(stem+22);
    if(!path) return application_fail(error,QA_ERROR_MEMORY,"Allocating bot navigation resource path");
    bool ok=true;
    for(size_t i=0;i<2;++i) {
        bool aas=bots->geometry.family==QA_BSP_Q3?i==0:i==1;
        const char *prefix=aas?"maps/":"bots/navigation/";
        size_t prefix_size=strlen(prefix);
        memcpy(path,prefix,prefix_size);memcpy(path+prefix_size,name,stem);
        strcpy(path+prefix_size+stem,aas?".aas":".nav");
        qa_resource *resource=NULL;qa_error local={0};qa_vfs_acquisition acquisition={0};
        bool acquired=qa_vfs_acquire_receipt(bots->navigation_files,path,&resource,&acquisition,&local);
        if(!acquired) {
            if(local.code==QA_ERROR_NOT_FOUND) continue;
            if(error) *error=local;
            ok=false;break;
        }
        if(!aas) {
            qa_product_id product;qa_mount_id physical;
            if(!qa_catalog_product_mount_origin(origin.catalog,origin.product,bots->navigation_files,
                acquisition.mount,&product,&physical)) {
                qa_resource_release(resource);qa_vfs_acquisition_dispose(&acquisition);
                ok=application_fail(error,QA_ERROR_ARGUMENT,"Bot NAV lost its genuine mount content");break;
            }
            if(product!=origin.product) {
                qa_resource_release(resource);qa_vfs_acquisition_dispose(&acquisition);continue;
            }
        }
        *saved_resource=resource;*saved_acquisition=acquisition;
        break;
    }
    free(path);return ok;
}
bool application_bot_navigation_rebuild(application_bots *bots,application_bot_graph *graph,qa_error *error) {
    qa_nav_profile profile={.movement=graph->profile,.shape={QA_SHAPE_BOX,graph->bounds},
        .crouched_shape={QA_SHAPE_BOX,graph->bounds},.has_crouched_shape=true,
        .capabilities=QA_NAV_CAPABILITY(QA_NAV_WALK)|QA_NAV_CAPABILITY(QA_NAV_CROUCH)|
        QA_NAV_CAPABILITY(QA_NAV_JUMP)|QA_NAV_CAPABILITY(QA_NAV_DROP)|QA_NAV_CAPABILITY(QA_NAV_SWIM)|
        QA_NAV_CAPABILITY(QA_NAV_WATER_JUMP)|QA_NAV_CAPABILITY(QA_NAV_LADDER)|QA_NAV_CAPABILITY(QA_NAV_TELEPORT)|
        QA_NAV_CAPABILITY(QA_NAV_MOVER)|QA_NAV_CAPABILITY(QA_NAV_JUMP_PAD)|
        QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP)|QA_NAV_CAPABILITY(QA_NAV_BFG_JUMP)|QA_NAV_CAPABILITY(QA_NAV_GRAPPLE),
        .maximum_step=18,.minimum_floor_normal=.7f,.maximum_drop=400};
    profile.crouched_shape.bounds.maxs.z=fminf(graph->bounds.maxs.z,16);
    profile.policy=(qa_trace_policy){.family=graph->profile.kind==QA_RULESET_Q3?QA_COLLISION_Q3:
        graph->profile.kind==QA_RULESET_NETQUAKE || graph->profile.kind==QA_RULESET_QUAKEWORLD?QA_COLLISION_Q1:QA_COLLISION_Q2,
        .q1_hull=-1,.curves=true,.player_curve_clip=true};
    profile.policy.contents_mask=qa_collision_contents_mask(0x2010001,profile.policy.family);
    qa_nav_map map={.name=bots->application->current_map,.format=bots->geometry.format};
    qa_navigation_services services=application_bot_navigation_services(bots);
    services.topology_geometry_only=true;
    bool ok;
    if(graph->asset_resource) {
        qa_nav_asset *asset=NULL;
        uint32_t checksum_word=qa_block_checksum(bots->geometry.source);int32_t checksum;
        memcpy(&checksum,&checksum_word,sizeof(checksum));
        ok=qa_nav_asset_read(qa_resource_bytes(graph->asset_resource),&checksum,&asset,error);
        if(ok) ok=qa_nav_graph_from_asset(&map,asset,&profile,&services,&graph->graph,error);
        qa_nav_asset_release(asset);
    } else {
        qa_nav_construction construction={.geometry=&bots->geometry,.map=map,.profile=profile};
        ok=qa_nav_graph_construct(&construction,&services,&graph->graph,error);
    }
    services.topology_geometry_only=false;
    if (!ok || !qa_navigation_create(graph->graph,&services,&graph->navigation,error)) return false;
    size_t edges=qa_nav_graph_read(graph->graph)->edge_count;
    if (bots->runtime && !qa_bot_runtime_prepare_navigation(bots->runtime,edges,error)) return false;
    for (application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        if (guest->runtime && !qa_bot_runtime_prepare_navigation(guest->runtime,edges,error)) return false;
    return true;
}
static bool navigation_graph(application_bots *bots,application_provider *movement,
                              const qa_movement_profile *movement_profile,qa_bounds bounds,
                              application_bot_graph **out,qa_error *error) {
    application_bot_graph *shared=bots->graphs;
    while(shared && (shared->movement!=movement || memcmp(&shared->bounds,&bounds,sizeof(bounds)) ||
                    memcmp(&shared->profile,movement_profile,sizeof(*movement_profile)))) shared=shared->next;
    if(!shared) {
        shared=calloc(1,sizeof(*shared));
        if(!shared) return application_fail(error,QA_ERROR_MEMORY,"allocating shared bot navigation graph owner");
        shared->movement=movement;shared->bounds=bounds;shared->profile=*movement_profile;
        bool ok=navigation_resource(bots,&shared->asset_resource,&shared->asset_acquisition,error) &&
            application_bot_navigation_rebuild(bots,shared,error);
        if(!ok) {qa_navigation_destroy(shared->navigation);qa_nav_graph_release(shared->graph);qa_resource_release(shared->asset_resource);
            qa_vfs_acquisition_dispose(&shared->asset_acquisition);free(shared);return false;}
        shared->next=bots->graphs;bots->graphs=shared;
    }
    *out=shared;return true;
}
qa_navigation_services application_bot_navigation_services(application_bots *bots) {
    return (qa_navigation_services){.context=bots,.world=bots->application->world,.revision=revision,
        .entity=entity,.movement_input=application_bot_movement_input};
}
bool application_bot_navigation_restore_binding(application_bots *bots,qa_navigation *runtime,qa_actor_id actor,
                                                qa_bot_navigation **out,qa_error *error) {
    qa_bot_navigation_observations observations={.context=bots,.actor=source_actor,.model=source_model};
    return qa_bot_navigation_create_restored(runtime,bots->application->world,actor,&observations,out,error);
}
bool application_bot_navigation_prepare(application_bots *bots,qa_error *error) {
    /* Engine map queries use the original Q3 standing/crouching hull before
     * clients exist. Admitted clients bind their actual selected policy below. */
    qa_movement_profile profile=qa_movement_profile_default(QA_RULESET_Q3);
    qa_bounds bounds={qa_v3(-15,-15,-24),qa_v3(15,15,32)};
    application_bot_graph *shared;
    if(!navigation_graph(bots,NULL,&profile,bounds,&shared,error)) return false;
    qa_bot_navigation_observations observations={.context=bots,.actor=source_actor,.model=source_model};
    return qa_bot_navigation_create(shared->navigation,bots->application->world,(qa_actor_id){0},
                                    &observations,&bots->map_navigation,error);
}
bool application_bot_navigation_bind(application_bots *bots,application_bot_seat *seat,qa_error *error) {
    qa_application *application=bots->application;qa_player_state control;
    if(!qa_application_control_read(application,seat->actor,&control))
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot has no selected movement control");
    application_provider *movement=application_provider_for(application,seat->actor,QA_ROLE_MOVEMENT,NULL);
    application_bot_graph *shared;
    if(!navigation_graph(bots,movement,&control.profile,control.bounds,&shared,error)) return false;
    qa_bot_navigation_observations observations={.context=bots,.actor=source_actor,.model=source_model};
    return qa_bot_navigation_create(shared->navigation,application->world,seat->actor,&observations,&seat->navigation,error);
}
bool application_bots_guest_admit(qa_application *application,qa_actor_id actor,qa_error *error) {
    application_bots *bots=application->bots;
    if(!bots) return application_fail(error,QA_ERROR_NOT_FOUND,"guest bot shared library is absent");
    for(application_bot_target *target=bots->targets;target;target=target->next)
        if(qa_actor_id_equal(target->actor,actor)) return true;
    qa_player_state control;
    if(!qa_application_control_read(application,actor,&control))
        return application_fail(error,QA_ERROR_NOT_FOUND,"guest bot selected movement has not been admitted");
    application_bot_target *target=calloc(1,sizeof(*target));
    if(!target) return application_fail(error,QA_ERROR_MEMORY,"retaining guest bot navigation owner");
    application_bot_seat seat={.actor=actor};
    if(!application_bot_navigation_bind(bots,&seat,error)) {free(target);return false;}
    target->actor=actor;target->movement=application_provider_for(application,actor,QA_ROLE_MOVEMENT,NULL);
    target->profile=control.profile;target->bounds=control.bounds;target->navigation=seat.navigation;
    target->next=bots->targets;bots->targets=target;return true;
}
bool application_bot_predict_motion(void *opaque,qa_actor_id actor,
                                     const qa_bot_movement_prediction_query *query,
                                     qa_bot_movement_prediction *out,bool *available,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;*available=false;
    qa_player_state control;
    if(!qa_application_control_read(application,actor,&control)) return true;
    application_provider *movement=application_provider_for(application,actor,QA_ROLE_MOVEMENT,NULL);
    if(!movement || (movement->kind!=APPLICATION_PROVIDER_Q1 && movement->kind!=APPLICATION_PROVIDER_Q2 &&
                     movement->kind!=APPLICATION_PROVIDER_Q3)) return true;
    application_bot_target *target=bots->targets;
    while(target && !(qa_actor_id_equal(target->actor,actor) && target->movement==movement &&
                      !memcmp(&target->profile,&control.profile,sizeof(control.profile)) &&
                      !memcmp(&target->bounds,&control.bounds,sizeof(control.bounds)))) target=target->next;
    if(!target) {
        target=calloc(1,sizeof(*target));
        if(!target) return application_fail(error,QA_ERROR_MEMORY,"retaining selected target movement prediction");
        application_bot_seat seat={.actor=actor};
        if(!application_bot_navigation_bind(bots,&seat,error)) {free(target);return false;}
        target->actor=actor;target->movement=movement;target->profile=control.profile;
        target->bounds=control.bounds;target->navigation=seat.navigation;
        target->next=bots->targets;bots->targets=target;
    }
    if(target->predicting)
        return application_fail(error,QA_ERROR_ARGUMENT,"target movement prediction reentered its workspace");
    target->predicting=true;
    ++bots->calls;
    bool ok=qa_bot_navigation_predict_movement(target->navigation,query,out,error);
    --bots->calls;
    target->predicting=false;
    *available=qa_actors_get(qa_session_actors(application->session),actor)!=NULL;
    if(!*available) {if(error)*error=(qa_error){0};return true;}
    return ok;
}
