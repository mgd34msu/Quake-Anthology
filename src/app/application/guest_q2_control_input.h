#ifndef QA_APPLICATION_GUEST_Q2_CONTROL_INPUT_H
#define QA_APPLICATION_GUEST_Q2_CONTROL_INPUT_H
#include "guest_native_q2_input.h"
#include <math.h>

/* Published Windows x64 API2023 pmove_t, from rerelease/game.h. */
enum { RR_PM_BYTES=3320,RR_PM_CMD=52,RR_PM_TOUCHES=88,RR_PM_TRACE=96,
    RR_PM_VIEW=3168,RR_PM_GROUND=3208,RR_PM_PLANE=3216,RR_PM_WATER=3236,
    RR_PM_WATERLEVEL=3240,RR_PM_OFFSET=3280,RR_PM_BLEND=3292,RR_PM_RENDER=3308,
    RR_PM_JUMP=3309,RR_PM_STEP=3310,RR_PM_IMPACT=3312 };

static bool raw_fail(qa_error *error,qa_status status,const char *message)
{ application_fail(error,status,message); return false; }
static bool raw_entity(control_client *client,qa_movement_ground ground,qa_native_address *out,qa_error *error)
{
    if(ground.hit==QA_TRACE_HIT_NONE) { *out=0; return true; }
    if(ground.hit==QA_TRACE_HIT_WORLD) return qa_native_entity_address(client->native,0,out,error);
    qa_native_entity_table table;
    if(!qa_native_entity_table_get(client->native,&table,error)) return false;
    for(uint32_t slot=0;slot<table.count;++slot) {
        qa_native_slot_binding binding;
        if(!qa_native_slot(client->native,slot,&binding,error)) return false;
        if(binding.kind!=QA_NATIVE_SLOT_FREE&&binding.owner==client->engine->provider->owner&&
            binding.source_slot==slot&&qa_actor_id_equal(binding.actor,ground.actor))
            return qa_native_entity_address(client->native,slot,out,error);
    }
    return raw_fail(error,QA_ERROR_NOT_FOUND,"Selected movement ground has no physical Source entity");
}
static bool raw_surface(control_client *client,qa_native_address address,qa_collision_surface *out,qa_error *error)
{
    uint8_t bytes[64];
    if(!qa_native_read(client->native,address,bytes,sizeof(bytes),error)) return false;
    memset(out,0,sizeof(*out)); memcpy(out->name,bytes,32); out->name[32]=0;
    out->flags=qa_collision_surface_decode(qa_load_i32le(bytes+32),QA_COLLISION_Q2); out->value=qa_load_i32le(bytes+36);
    memcpy(out->material,bytes+44,sizeof(out->material)); return true;
}
static qa_collision_plane raw_plane(const uint8_t *bytes)
{ return (qa_collision_plane){.normal=load_vector(bytes),.distance=qa_load_f32le(bytes+12),.type=bytes[16],.signbits=bytes[17]}; }
static bool raw_trace(control_client *client,const uint8_t *bytes,qa_trace_result *out,qa_error *error)
{
    qa_trace_result trace={.family=QA_COLLISION_Q2,.all_solid=bytes[0]!=0,.start_solid=bytes[1]!=0,
        .fraction=qa_load_f32le(bytes+4),.end=load_vector(bytes+8),.plane=raw_plane(bytes+20),
        .contents=qa_collision_contents_decode(qa_load_i32le(bytes+48),QA_COLLISION_Q2)};
    trace.contact=trace.fraction<1||trace.start_solid||trace.all_solid; trace.contact_plane=trace.plane;
    qa_native_address entity=qa_load_u64le(bytes+56),surface=qa_load_u64le(bytes+40),secondary=qa_load_u64le(bytes+88),world;
    if(!qa_native_entity_address(client->native,0,&world,error)) return false;
    if(entity) {
        if(entity==world) trace.hit=QA_TRACE_HIT_WORLD;
        else {
            if(!qa_native_host_source_actor(client->host,entity,true,&trace.actor,error)) return false;
            if(!trace.actor.registry) return raw_fail(error,QA_ERROR_NOT_FOUND,"Native Pmove touch names an inactive physical actor");
            trace.hit=QA_TRACE_HIT_ACTOR;
        }
    }
    if(surface) {
        if(!raw_surface(client,surface,&trace.surface,error)) return false;
        trace.has_surface=true; trace.surface_flags=trace.surface.flags;
    }
    trace.secondary_plane=raw_plane(bytes+64);
    trace.has_secondary=secondary||qa_vec_dot(trace.secondary_plane.normal,trace.secondary_plane.normal)>0;
    if(secondary) {
        if(!raw_surface(client,secondary,&trace.secondary_surface,error)) return false;
        trace.secondary_has_surface=true;
    }
    if(!isfinite(trace.fraction)||trace.fraction<0||trace.fraction>1||!qa_vec_finite(trace.end)||
        !qa_vec_finite(trace.plane.normal)||!isfinite(trace.plane.distance))
        return raw_fail(error,QA_ERROR_FORMAT,"Native Pmove touch returned invalid physical trace geometry");
    *out=trace; return true;
}
static bool raw_result(control_frame *frame,const uint8_t *bytes,qa_movement_result *out,qa_error *error)
{
    qa_movement_result result={.status=QA_MOVEMENT_ACTIVE,.actor=frame->client.actor,
        .command_sequence=frame->client.engine->current_command_sequence,
        .state={.kind=QA_MOVEMENT_Q2_RERELEASE},.bounds={load_vector(bytes+3180),load_vector(bytes+3192)},
        .view_angles=load_vector(bytes+RR_PM_VIEW),.view_offset=load_vector(bytes+RR_PM_OFFSET),
        .view_height=(float)(int8_t)bytes[48],.water_type=qa_load_i32le(bytes+RR_PM_WATER),
        .water_level=bytes[RR_PM_WATERLEVEL],.render_flags=bytes[RR_PM_RENDER],
        .jump_sound=bytes[RR_PM_JUMP]!=0,.step_clip=bytes[RR_PM_STEP]!=0,.impact_delta=qa_load_f32le(bytes+RR_PM_IMPACT)};
    qa_q2r_movement_state *state=&result.state.data.q2r;
    state->type=qa_load_i32le(bytes); state->origin=load_vector(bytes+4); state->velocity=load_vector(bytes+16);
    state->flags=qa_load_u16le(bytes+28); state->time_ms=qa_load_u16le(bytes+30);
    state->gravity=(int16_t)qa_load_u16le(bytes+32); state->delta_angles=load_vector(bytes+36); state->view_height=result.view_height;
    for(size_t i=0;i<4;++i) result.screen_blend[i]=qa_load_f32le(bytes+RR_PM_BLEND+i*4);
    qa_native_address ground=qa_load_u64le(bytes+RR_PM_GROUND),world;
    if(!qa_native_entity_address(frame->client.native,0,&world,error)) return false;
    if(ground==world) result.ground.hit=QA_TRACE_HIT_WORLD;
    else if(ground) {
        result.ground.hit=QA_TRACE_HIT_ACTOR;
        if(!qa_native_host_source_actor(frame->client.host,ground,true,&result.ground.actor,error)) return false;
        if(!result.ground.actor.registry) return raw_fail(error,QA_ERROR_NOT_FOUND,"Native Pmove returned inactive physical ground");
    }
    size_t count=qa_load_u32le(bytes+RR_PM_TOUCHES);
    if(count>32) return raw_fail(error,QA_ERROR_FORMAT,"Native Pmove touch count exceeds its SDK array");
    if(count) {
        result.contacts=calloc(count,sizeof(*result.contacts));
        if(!result.contacts) return raw_fail(error,QA_ERROR_MEMORY,"Retaining actual Source Pmove traces");
        result.contact_count=result.contact_capacity=count;
        for(size_t i=0;i<count;++i) if(!raw_trace(&frame->client,bytes+RR_PM_TOUCHES+8+i*RR_PM_TRACE,&result.contacts[i].trace,error)) {
            qa_movement_result_free(&result); return false;
        }
    }
    *out=result; return true;
}
static bool raw_store(control_frame *frame,uint8_t *bytes,const qa_movement_result *result,qa_error *error)
{
    store_vector(bytes+4,qa_movement_origin(&result->state)); store_vector(bytes+16,qa_movement_velocity(&result->state));
    if(!isfinite(result->view_height)||result->view_height<INT8_MIN||result->view_height>INT8_MAX||
        result->water_level<0||result->water_level>3||!valid_bounds(result->bounds))
        return raw_fail(error,QA_ERROR_FORMAT,"Selected movement output exceeds its physical Source SDK domain");
    bytes[48]=(uint8_t)(int8_t)result->view_height;
    store_vector(bytes+RR_PM_VIEW,result->view_angles);
    store_vector(bytes+3180,result->bounds.mins); store_vector(bytes+3192,result->bounds.maxs);
    qa_native_address ground;
    if(!raw_entity(&frame->client,result->ground,&ground,error)) return false;
    qa_store_u64le(bytes+RR_PM_GROUND,ground);
    if(result->ground.hit!=QA_TRACE_HIT_NONE) {
        qa_vec3 origin=qa_movement_origin(&result->state);
        qa_trace_query query={.start=origin,.end=qa_v3(origin.x,origin.y,origin.z-.25f),
            .shape={.kind=QA_SHAPE_BOX,.bounds=result->bounds},.pass_actor=result->actor,
            .policy=qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.q2_merged_contents=true;
        qa_trace_result trace;
        if(!qa_world_trace(frame->client.engine->world,&query,&trace,error)) return false;
        if(trace.hit!=result->ground.hit || (trace.hit==QA_TRACE_HIT_ACTOR&&!qa_actor_id_equal(trace.actor,result->ground.actor)))
            return raw_fail(error,QA_ERROR_NOT_FOUND,"Selected movement ground plane lost its actual shared collision contact");
        store_vector(bytes+RR_PM_PLANE,trace.plane.normal); qa_store_f32le(bytes+RR_PM_PLANE+12,trace.plane.distance);
        bytes[RR_PM_PLANE+16]=(uint8_t)trace.plane.type; bytes[RR_PM_PLANE+17]=trace.plane.signbits;
    }
    qa_store_u32le(bytes+RR_PM_WATER,(uint32_t)result->water_type); bytes[RR_PM_WATERLEVEL]=(uint8_t)result->water_level;
    size_t count=result->contact_count>32?32:result->contact_count;
    qa_store_u32le(bytes+RR_PM_TOUCHES,(uint32_t)count);
    for(size_t i=0;i<count;++i)
        if(!qa_native_host_q2_trace_encode(frame->client.host,&result->contacts[i].trace,
            (qa_buffer){bytes+RR_PM_TOUCHES+8+i*RR_PM_TRACE,RR_PM_TRACE},error)) return false;
    store_vector(bytes+RR_PM_OFFSET,result->view_offset);
    for(size_t i=0;i<4;++i) qa_store_f32le(bytes+RR_PM_BLEND+i*4,result->screen_blend[i]);
    if(result->render_flags>UINT8_MAX) return raw_fail(error,QA_ERROR_FORMAT,"Selected movement render flags exceed Source byte");
    bytes[RR_PM_RENDER]=(uint8_t)result->render_flags; bytes[RR_PM_JUMP]=result->jump_sound?1u:0u;
    bytes[RR_PM_STEP]=result->step_clip?1u:0u; qa_store_f32le(bytes+RR_PM_IMPACT,result->impact_delta);
    return qa_native_write(frame->client.native,frame->address,(qa_bytes){bytes,RR_PM_BYTES},error);
}
static bool raw_move(control_frame *frame,bool *handled,qa_error *error)
{
    struct application_native_q2 *engine=frame->client.engine;
    const application_native_q2_input_stage *stage=engine->input_stage;
    *handled=false;
    if(!stage||application_provider_for(engine->provider->application,frame->client.actor,QA_ROLE_MOVEMENT,NULL)==engine->provider) return true;
    *handled=true;
    if(!qa_actor_id_equal(stage->actor,frame->client.actor)||!stage->current(stage->context,frame->client.actor)||!client_live(&frame->client,error))
        return raw_fail(error,QA_ERROR_NOT_FOUND,"Original Pmove lost its genuine foreign movement Source stage");
    uint8_t bytes[RR_PM_BYTES];
    if(!qa_native_read(frame->client.native,frame->address,bytes,sizeof(bytes),error)) return false;
    qa_movement_input input=qa_movement_input_default(QA_MOVEMENT_Q2_RERELEASE,frame->client.actor);
    qa_movement_result physical={0};
    /* Read only the incoming state; touch/view outputs are not initialized yet. */
    input.state.data.q2r=(qa_q2r_movement_state){.type=qa_load_i32le(bytes),.origin=load_vector(bytes+4),
        .velocity=load_vector(bytes+16),.flags=qa_load_u16le(bytes+28),.time_ms=qa_load_u16le(bytes+30),
        .gravity=(int16_t)qa_load_u16le(bytes+32),.delta_angles=load_vector(bytes+36),.view_height=(float)(int8_t)bytes[48]};
    input.command=(qa_movement_command){.kind=QA_MOVEMENT_Q2_RERELEASE,.milliseconds=bytes[52],.buttons=bytes[53],
        .angles=load_vector(bytes+56),.forward_move=qa_load_f32le(bytes+68),.side_move=qa_load_f32le(bytes+72),
        .server_frame=(int32_t)qa_load_u32le(bytes+76)};
    input.snap_initial=bytes[80]!=0; input.current_bounds=frame->accepted; input.has_current_bounds=true;
    input.shape=(qa_trace_shape){.kind=QA_SHAPE_BOX,.bounds=frame->accepted};
    qa_q2_player player;
    bool present=qa_native_host_q2_player(frame->client.host,frame->client.slot,&player,error)&&
        client_live(&frame->client,error);
    if(present) input.view_offset=qa_v3(player.viewoffset[0],player.viewoffset[1],player.viewoffset[2]);
    if(!present) return false;
    qa_native_host_movement_services movement=application_native_q2_movement_services(engine);
    qa_movement_services services=movement.kernel;
    bool ok=movement.prepare(movement.context,frame->client.host,frame->address,&input,error)&&
        movement.execute(movement.context,frame->client.host,frame->address,&input,&services,&physical,error);
    if(ok&&physical.status==QA_MOVEMENT_ACTOR_REMOVED&&
        !qa_actors_get(qa_session_actors(engine->provider->application->session),physical.actor)) {
        qa_movement_result_free(&physical); return true;
    }
    if(ok) ok=frame_live(engine->source_control,frame,error)&&raw_store(frame,bytes,&physical,error);
    if(ok) ok=movement.commit(movement.context,frame->client.host,frame->address,&physical,error);
    qa_movement_result_free(&physical); return ok;
}
static bool raw_native_commit(control_frame *frame,qa_error *error)
{
    if(!frame->client.engine->input_stage) return true;
    uint8_t bytes[RR_PM_BYTES];
    if(!qa_native_read(frame->client.native,frame->address,bytes,sizeof(bytes),error)) return false;
    qa_movement_result result={0};
    if(!raw_result(frame,bytes,&result,error)) return false;
    qa_native_host_movement_services movement=application_native_q2_movement_services(frame->client.engine);
    bool ok=movement.commit(movement.context,frame->client.host,frame->address,&result,error);
    qa_movement_result_free(&result); return ok;
}
#endif
