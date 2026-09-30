#include "bots_private.h"
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
    qa_application_control_view view;
    if(!qa_application_control_read(application,actor,&view))
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot prediction actor has no selected movement");
    application_control_record *control=&application->controls[actor.slot];
    qa_combat_state combat;qa_body_state body;
    if(!qa_combat_read_traits(application->combat,actor,&combat,error) ||
       !qa_world_body_read(application->world,actor,&body,error)) return false;
    *out=(qa_movement_input){.actor=actor,.state=view.state,.profile=view.profile,
        .shape={QA_SHAPE_BOX,view.bounds},.current_bounds=view.bounds,.has_current_bounds=true,
        .standing={control->standing_bounds,view.view_height},
        .crouched={view.bounds,view.view_height},.dead={view.bounds,view.view_height},
        .time_ns=qa_session_elapsed(application->session),.prediction=true,.view_offset=view.view_offset,
        .q2r_pml_origin=&control->q2r_pml_origin};
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
static bool asset_graph(application_bots *bots,const qa_nav_map *map,const qa_nav_profile *profile,
                         const qa_navigation_services *services,qa_nav_graph **out,bool *found,qa_error *error) {
    const qa_launch_snapshot *snapshot=bots->application->routing_snapshot?
        bots->application->routing_snapshot:qa_application_launch(bots->application);
    qa_vfs *files=snapshot?qa_launch_snapshot_mounts(snapshot):NULL;
    *found=false;
    if(!files) return true;
    const char *name=qa_strings_cstr(qa_session_strings(bots->application->session),map->name);
    if(!name) return application_fail(error,QA_ERROR_NOT_FOUND,"bot map name is absent");
    const char *extensions[]={"aas","nav2","nav3"};
    for(size_t i=0;i<3;++i) {
        size_t length=strlen(name)+strlen(extensions[i])+7;
        char *path=malloc(length);
        if(!path) return application_fail(error,QA_ERROR_MEMORY,"allocating bot navigation resource path");
        snprintf(path,length,"maps/%s.%s",name,extensions[i]);
        qa_resource *resource=NULL;qa_error local={0};
        bool acquired=qa_vfs_acquire(files,path,&resource,NULL,&local);free(path);
        if(!acquired) {
            if(local.code==QA_ERROR_NOT_FOUND) continue;
            if(error) *error=local;return false;
        }
        qa_nav_asset *asset=NULL;
        uint32_t checksum=qa_block_checksum(bots->geometry.source);
        bool ok=qa_nav_asset_read(qa_resource_bytes(resource),i==0?&checksum:NULL,&asset,error);
        qa_resource_release(resource);
        if(ok) ok=qa_nav_graph_from_asset(map,asset,profile,services,out,error);
        qa_nav_asset_release(asset);
        if(!ok) return false;
        *found=true;return true;
    }
    return true;
}
static bool navigation_graph(application_bots *bots,application_provider *movement,
                              const qa_movement_profile *movement_profile,qa_bounds bounds,
                              application_bot_graph **out,qa_error *error) {
    qa_application *application=bots->application;
    application_bot_graph *shared=bots->graphs;
    while(shared && (shared->movement!=movement || memcmp(&shared->bounds,&bounds,sizeof(bounds)) ||
                    memcmp(&shared->profile,movement_profile,sizeof(*movement_profile)))) shared=shared->next;
    if(!shared) {
        shared=calloc(1,sizeof(*shared));
        if(!shared) return application_fail(error,QA_ERROR_MEMORY,"allocating shared bot navigation graph owner");
        qa_nav_profile profile={.movement=*movement_profile,.shape={QA_SHAPE_BOX,bounds},
            .crouched_shape={QA_SHAPE_BOX,bounds},.has_crouched_shape=true,
            .capabilities=QA_NAV_CAPABILITY(QA_NAV_WALK)|QA_NAV_CAPABILITY(QA_NAV_CROUCH)|
                QA_NAV_CAPABILITY(QA_NAV_JUMP)|QA_NAV_CAPABILITY(QA_NAV_DROP)|QA_NAV_CAPABILITY(QA_NAV_SWIM)|
                QA_NAV_CAPABILITY(QA_NAV_WATER_JUMP)|QA_NAV_CAPABILITY(QA_NAV_LADDER)|QA_NAV_CAPABILITY(QA_NAV_TELEPORT)|
                QA_NAV_CAPABILITY(QA_NAV_MOVER)|QA_NAV_CAPABILITY(QA_NAV_JUMP_PAD)|
                QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP)|QA_NAV_CAPABILITY(QA_NAV_BFG_JUMP)|QA_NAV_CAPABILITY(QA_NAV_GRAPPLE),
            .maximum_step=18,.minimum_floor_normal=.7f,.maximum_drop=400};
        profile.crouched_shape.bounds.maxs.z=fminf(bounds.maxs.z,16);
        profile.policy=(qa_trace_policy){.family=movement_profile->kind==QA_MOVEMENT_Q3?QA_COLLISION_Q3:
            movement_profile->kind==QA_MOVEMENT_NETQUAKE || movement_profile->kind==QA_MOVEMENT_QUAKEWORLD?QA_COLLISION_Q1:QA_COLLISION_Q2,
            .contents_mask=0x2010001,.q1_hull=-1,.curves=true,.player_curve_clip=true};
        qa_nav_map map={.name=application->current_map,.format=bots->geometry.format};
        qa_sha256_digest digest;qa_sha256(bots->geometry.source,&digest);memcpy(map.digest,digest.bytes,sizeof(map.digest));
        qa_navigation_services services={.context=bots,.world=application->world,.revision=revision,
            .entity=entity,.movement_input=application_bot_movement_input};
        bool found;
        bool ok=asset_graph(bots,&map,&profile,&services,&shared->graph,&found,error);
        if(ok && !found) {
            qa_nav_construction construction={.geometry=&bots->geometry,.map=map,.profile=profile};
            ok=qa_nav_graph_construct(&construction,&services,&shared->graph,error);
        }
        if(ok) ok=qa_navigation_create(shared->graph,&services,&shared->navigation,error);
        if(!ok) {qa_nav_graph_release(shared->graph);free(shared);return false;}
        shared->movement=movement;shared->bounds=bounds;shared->profile=*movement_profile;
        shared->next=bots->graphs;bots->graphs=shared;
    }
    *out=shared;return true;
}
bool application_bot_navigation_prepare(application_bots *bots,qa_error *error) {
    /* Engine map queries use the original Q3 standing/crouching hull before
     * clients exist. Admitted clients bind their actual selected policy below. */
    qa_movement_profile profile=qa_movement_profile_default(QA_MOVEMENT_Q3);
    qa_bounds bounds={qa_v3(-15,-15,-24),qa_v3(15,15,32)};
    application_bot_graph *shared;
    if(!navigation_graph(bots,NULL,&profile,bounds,&shared,error)) return false;
    qa_bot_navigation_observations observations={.context=bots,.actor=source_actor,.model=source_model};
    return qa_bot_navigation_create(shared->navigation,bots->application->world,(qa_actor_id){0},
                                    &observations,&bots->map_navigation,error);
}
bool application_bot_navigation_bind(application_bots *bots,application_bot_seat *seat,qa_error *error) {
    qa_application *application=bots->application;qa_application_control_view control;
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
    qa_application_control_view control;
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
    qa_application_control_view control;
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
