#include "guest_native_q2_private.h"
#include "guest_native_q2_combat.h"
#include "native_q2_callbacks.h"
#include "native_q2_client_stages.h"
#include "native_q2_visibility.h"
#include "native_q2_inventory_scanner.h"
#include "control_frame.h"
#include "guest_native_q2_input.h"
#include "guest_native_q2_attack.h"
#include "qa/native_host_q2_wire.h"
#include <math.h>

static uint32_t source_client_slot(const application_provider *provider, qa_actor_id actor)
{
    const struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q2_engine : NULL;
    if (!engine || engine->profile == QA_NATIVE_Q2_CGAME_API2023) return 0;
    uint32_t slot = 0;
    for (uint32_t i = 1; i < 257; ++i)
        if (engine->clients[i].connected && engine->clients[i].begun &&
            qa_actor_id_equal(engine->clients[i].actor, actor)) {
            if (slot) return 0;
            slot = i;
        }
    return slot;
}

bool application_native_q2_source_client(const application_provider *provider, qa_actor_id actor)
{
    return source_client_slot(provider, actor) != 0 && !provider->state.native.q2_engine->callbacks;
}

bool application_native_q2_declared_source_client(const application_provider *provider,qa_actor_id actor)
{
    if(!source_client_slot(provider,actor)) return false;
    struct application_native_q2 *engine=provider->state.native.q2_engine;
    if(!engine->callbacks) return false;
    const qa_json_document *doc=application_native_q2_callbacks_document(engine->callbacks);
    qa_json_id root=qa_json_root(doc),entity=qa_json_get(doc,root,"entityRecord");
    return qa_json_type(doc,entity)==QA_JSON_STRING &&
        qa_json_type(doc,qa_json_get(doc,root,"clients"))==QA_JSON_OBJECT;
}

static bool declared_raw_recipe(const qa_json_document *doc,const char *entity_name)
{
    qa_json_id root=qa_json_root(doc),entity=qa_json_get(doc,root,"entityRecord"),
        clients=qa_json_get(doc,root,"clients"),input=qa_json_get(doc,clients,"input");
    if(qa_json_type(doc,entity)!=QA_JSON_STRING||qa_json_type(doc,input)!=QA_JSON_ARRAY||
        qa_json_size(doc,qa_json_get(doc,clients,"inputFields"))) return false;
    size_t producers=0;
    for(size_t i=0;i<qa_json_size(doc,input);++i) {
        qa_json_id binding=qa_json_at(doc,input,i),phase=qa_json_get(doc,binding,"phase");
        if(!qa_json_string_equal(doc,qa_json_get(doc,binding,"scope"),"client-command") ||
            qa_json_size(doc,qa_json_get(doc,binding,"outputs"))) return false;
        qa_json_id calls=qa_json_get(doc,binding,"calls");
        for(size_t j=0;j<qa_json_size(doc,calls);++j) {
            qa_json_id call=qa_json_at(doc,calls,j),entry=qa_json_get(doc,call,"entry");
            if(!qa_json_string_equal(doc,qa_json_get(doc,entry,"kind"),"game-export")||
                !qa_json_string_equal(doc,qa_json_get(doc,entry,"name"),"ClientThink")) continue;
            qa_json_id args=qa_json_get(doc,call,"arguments"),self=qa_json_at(doc,args,0),command=qa_json_at(doc,args,1);
            bool same=qa_json_string_equal(doc,qa_json_get(doc,self,"record"),entity_name);
            if(!same||!qa_json_string_equal(doc,phase,"before")||qa_json_size(doc,args)!=2||
                !qa_json_string_equal(doc,qa_json_get(doc,self,"kind"),"actor")||
                !qa_json_string_equal(doc,qa_json_get(doc,self,"input"),"self")||
                !qa_json_string_equal(doc,qa_json_get(doc,command,"kind"),"user-command")||
                !qa_json_string_equal(doc,qa_json_get(doc,call,"returns"),"void")||
                qa_json_size(doc,qa_json_get(doc,call,"skips"))) return false;
            ++producers;
        }
    }
    return producers==1;
}

bool application_native_q2_declared_input_prepare(struct application_native_q2 *engine,qa_error *error)
{
    if(!engine||!engine->callbacks) return true;
    const qa_json_document *doc=application_native_q2_callbacks_document(engine->callbacks);
    qa_json_id entity=qa_json_get(doc,qa_json_root(doc),"entityRecord");
    bool capable=false;
    if(qa_json_type(doc,entity)==QA_JSON_STRING) {
        qa_buffer name={0};
        if(!qa_json_string(doc,entity,&name,error)) return false;
        capable=!memchr(name.data,0,name.size)&&declared_raw_recipe(doc,(const char *)name.data);
        qa_buffer_free(&name);
    }
    engine->raw_input_document=doc; engine->raw_input_capable=capable;
    return true;
}

bool application_native_q2_declared_raw_capable(const application_provider *provider)
{
    const struct application_native_q2 *engine=provider&&provider->kind==APPLICATION_PROVIDER_NATIVE
        ?provider->state.native.q2_engine:NULL;
    return engine&&engine->callbacks&&engine->profile!=QA_NATIVE_Q2_CGAME_API2023&&
        engine->raw_input_capable&&engine->raw_input_document==
            application_native_q2_callbacks_document(engine->callbacks);
}

static qa_movement_state player_movement(const qa_q2_player *player,
    qa_ruleset_id kind, qa_vec3 origin)
{
    bool classic = kind == QA_RULESET_Q2_CLASSIC;
    qa_movement_state state = qa_movement_state_default(kind, origin);
    if (classic) {
        state.data.q2.type = player->pmove.type;
        for (size_t i = 0; i < 3; ++i) {
            state.data.q2.origin_eighths[i] = (int16_t)player->pmove.origin[i];
            state.data.q2.velocity_eighths[i] = (int16_t)player->pmove.velocity[i];
            state.data.q2.delta_angle_shorts[i] = player->pmove.delta_angles[i];
        }
        state.data.q2.flags = (uint32_t)player->pmove.flags;
        state.data.q2.time_eight_ms = (uint8_t)player->pmove.time;
        state.data.q2.gravity = (int16_t)player->pmove.gravity;
    } else {
        state.data.q2r.type = player->pmove.type;
        state.data.q2r.origin = qa_v3(player->pmove.origin_f[0],player->pmove.origin_f[1],player->pmove.origin_f[2]);
        state.data.q2r.velocity = qa_v3(player->pmove.velocity_f[0],player->pmove.velocity_f[1],player->pmove.velocity_f[2]);
        state.data.q2r.flags = (uint16_t)player->pmove.flags;
        state.data.q2r.time_ms = (uint16_t)player->pmove.time;
        state.data.q2r.gravity = (int16_t)player->pmove.gravity;
        state.data.q2r.delta_angles = qa_v3(player->pmove.delta_angles_f[0],player->pmove.delta_angles_f[1],player->pmove.delta_angles_f[2]);
        state.data.q2r.view_height = (float)player->pmove.viewheight;
    }
    return state;
}

static bool player_motion_finite(uint32_t slot, const qa_movement_state *state,
    qa_vec3 view, qa_vec3 offset, qa_error *error)
{
    qa_vec3 origin = qa_movement_origin(state), velocity = qa_movement_velocity(state);
    if (qa_vec_finite(origin) && qa_vec_finite(velocity) && qa_vec_finite(view) && qa_vec_finite(offset))
        return true;
    char message[256];
    snprintf(message, sizeof(message),
        "Native Q2 nonfinite motion slot %u: o=%.9g,%.9g,%.9g v=%.9g,%.9g,%.9g "
        "a=%.9g,%.9g,%.9g off=%.9g,%.9g,%.9g",
        slot, (double)origin.x, (double)origin.y, (double)origin.z,
        (double)velocity.x, (double)velocity.y, (double)velocity.z,
        (double)view.x, (double)view.y, (double)view.z,
        (double)offset.x, (double)offset.y, (double)offset.z);
    return application_fail(error, QA_ERROR_FORMAT, message);
}

static bool physical_input_read(application_provider *provider, qa_actor_id actor,
    qa_movement_result *out, qa_vec3 *command_angles, qa_error *error)
{
    qa_vec3 discarded_angles;
    if (!command_angles) command_angles = &discarded_angles;
    *command_angles = qa_v3(0, 0, 0);
    uint32_t slot = source_client_slot(provider, actor);
    struct application_native_q2 *engine = slot ? provider->state.native.q2_engine : NULL;
    bool reached=engine&&engine->input_arsenal&&engine->input_stage&&
        qa_actor_id_equal(engine->input_stage->actor,actor)&&
        engine->input_stage->current(engine->input_stage->context,actor);
    if (!out || !engine || !engine->initialized || !engine->map_ready || (engine->calls&&!reached) ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 input read requires its live idle physical client");
    qa_native_instance *native=qa_native_host_instance(provider->state.native.host);
    qa_native_slot_binding binding,after;
    if(!qa_native_slot(native,slot,&binding,error)) return false;
    if(binding.kind==QA_NATIVE_SLOT_FREE||binding.slot!=slot||binding.source_slot!=slot||
        binding.owner!=provider->owner||!qa_actor_id_equal(binding.actor,actor))
        return application_fail(error,QA_ERROR_NOT_FOUND,"Native Q2 physical input binding differs from its full Source actor");
    qa_q2_player player;
    if (!qa_native_host_q2_player(provider->state.native.host, slot, &player, error)) return false;
    bool classic = engine->profile == QA_NATIVE_Q2_GAME_API3;
    qa_body_state body;
    if(!qa_world_body_read(engine->world,actor,&body,error)||!qa_native_slot(native,slot,&after,error)) return false;
    if(after.kind!=binding.kind||after.slot!=binding.slot||after.owner!=binding.owner||
        after.source_slot!=binding.source_slot||!qa_actor_id_equal(after.actor,binding.actor))
        return application_fail(error,QA_ERROR_NOT_FOUND,"Native Q2 physical player read changed its Source binding");
    qa_movement_state state = player_movement(&player,
        classic ? QA_RULESET_Q2_CLASSIC : QA_RULESET_Q2_RERELEASE, body.origin);
    *out = (qa_movement_result){.status=QA_MOVEMENT_ACTIVE,.actor=actor,.state=state,
        .view_angles=qa_v3(player.viewangles[0],player.viewangles[1],player.viewangles[2]),
        .view_offset=qa_v3(player.viewoffset[0],player.viewoffset[1],player.viewoffset[2]),
        .bounds=body.bounds,
        .view_height=classic ? player.viewoffset[2] : (float)player.pmove.viewheight};
    qa_actor_id ground_actor = qa_actor_reference_resolve(qa_session_actors(engine->provider->application->session), body.ground);
    if (qa_actor_reference_present(body.ground)) out->ground = qa_actor_id_equal(ground_actor,engine->world_actor)
        ? (qa_movement_ground){.hit=QA_TRACE_HIT_WORLD}
        : (qa_movement_ground){.hit=QA_TRACE_HIT_ACTOR,.actor=ground_actor};
    if (!player_motion_finite(slot, &state, out->view_angles, out->view_offset, error)) return false;
    if(engine->callbacks || application_native_q2_whole_source(engine, actor)) return true;
    if(!application_native_q2_attack_input_fields(engine,slot,actor,out,command_angles,error)) return false;
    /* The profile publishes Source waterlevel; watertype is a genuine current
     * shared-world sample in that Source collision dialect. */
    qa_vec3 origin=qa_movement_origin(&out->state);
    qa_point_query query={.point=qa_v3(origin.x,origin.y,origin.z+body.bounds.mins.z+1.f),
        .policy=qa_collision_default_policy(QA_COLLISION_Q2),.pass_actor=actor};
    query.policy.q2_merged_contents=!classic;
    qa_point_contents contents;
    if(!qa_world_point_contents(engine->world,&query,&contents,error)) return false;
    int32_t native_contents=qa_collision_point_contents_export(contents.contents,QA_COLLISION_Q2,contents.q1_opaque_token);
    out->water_type=out->water_level && (native_contents & 56) ? native_contents : 0;
    return true;
}

bool application_native_q2_input_read(application_provider *provider,qa_actor_id actor,
    qa_movement_result *out,qa_vec3 *command_angles,qa_error *error)
{
    if(!application_native_q2_source_client(provider,actor))
        return application_fail(error,QA_ERROR_UNSUPPORTED,"Native Q2 input requires its Original physical Source producer");
    return physical_input_read(provider,actor,out,command_angles,error);
}
bool application_native_q2_declared_input_read(application_provider *provider,qa_actor_id actor,
    qa_movement_state *out,qa_error *error)
{
    if(!out||!application_native_q2_declared_source_client(provider,actor)||
        !application_native_q2_declared_raw_capable(provider))
        return application_fail(error,QA_ERROR_UNSUPPORTED,"Declared native Q2 has no raw physical ClientThink producer");
    if(application_provider_for(provider->application,actor,QA_ROLE_ARSENAL,NULL)!=provider)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"Declared raw Source lacks its reached isolated foreign arsenal capability");
    qa_movement_result physical;
    if(!physical_input_read(provider,actor,&physical,NULL,error)) return false;
    *out=physical.state; return true;
}

typedef struct declared_raw_call {
    struct application_native_q2 *engine;
    const application_native_callback_inputs *inputs;
} declared_raw_call;
static bool declared_raw_execute(void *context,qa_error *error)
{
    declared_raw_call *raw=context;
    struct application_native_q2 *engine=raw->engine;
    const qa_json_document *doc=application_native_q2_callbacks_document(engine->callbacks);
    qa_json_id bindings=qa_json_get(doc,qa_json_get(doc,qa_json_root(doc),"clients"),"input");
    static const char *const phases[]={"before","after"};
    for(size_t phase=0;phase<2;++phase) for(size_t i=0;i<qa_json_size(doc,bindings);++i) {
        if(!engine->input_stage->current(engine->input_stage->context,engine->input_stage->actor)) {
            if(!qa_actors_get(qa_session_actors(engine->provider->application->session),engine->input_stage->actor)) return true;
            return application_fail(error,QA_ERROR_NOT_FOUND,"Declared raw Source command changed its actual client stage");
        }
        qa_json_id binding=qa_json_at(doc,bindings,i);
        if(!qa_json_string_equal(doc,qa_json_get(doc,binding,"phase"),phases[phase])) continue;
        qa_json_id calls=qa_json_get(doc,binding,"calls");
        for(size_t j=0;j<qa_json_size(doc,calls);++j) {
            if(!engine->input_stage->current(engine->input_stage->context,engine->input_stage->actor))
                return application_fail(error,QA_ERROR_NOT_FOUND,"Declared raw Source changed its actual client before a call");
            double result;
            if(!application_native_q2_callbacks_call_scoped(engine->callbacks,qa_json_at(doc,calls,j),raw->inputs,&result,error)) return false;
            if(!qa_actors_get(qa_session_actors(engine->provider->application->session),engine->input_stage->actor)) return true;
        }
    }
    return true;
}
static bool declared_raw_think(struct application_native_q2 *engine,uint32_t slot,qa_bytes command,qa_error *error)
{
    application_provider *provider=engine->provider;
    qa_application *app=provider->application;
    qa_actor_id actor=engine->clients[slot].actor;
    if(!engine->input_stage||!engine->input_command||!application_native_q2_declared_raw_capable(provider)||
        !application_native_q2_declared_source_client(provider,actor)||engine->calls||
        !engine->input_stage->current(engine->input_stage->context,actor))
        return application_fail(error,QA_ERROR_UNSUPPORTED,"Declared native Source lacks its admitted raw ClientThink recipe");
    if(application_provider_for(app,actor,QA_ROLE_ARSENAL,NULL)!=provider)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"Declared raw Source lacks its reached isolated foreign arsenal capability");
    const qa_usercmd *raw=engine->input_command;
    qa_movement_state physical;
    if(!application_native_q2_declared_input_read(provider,actor,&physical,error)) return false;
    qa_input_command_basis from={.kind=physical.kind,.relative=true}, to={.kind=physical.kind};
    if(physical.kind==QA_RULESET_Q2_CLASSIC) {
        from.words=from.wrap_words=true;
        for(size_t i=0;i<3;++i) from.delta_words[i]=physical.data.q2.delta_angle_shorts[i];
    } else from.delta_angles=physical.data.q2r.delta_angles;
    qa_usercmd absolute;
    qa_input_command_convert(raw,NULL,&from,&to,(qa_input_axis_rule){0},&absolute);
    qa_vec3 aim=absolute.angles;
    application_native_callback_value values[]={
        {.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=actor},
        {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)engine->input_stage->time_ns/1e9},
        {.name="elapsed",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)raw->milliseconds/1000.},
        {.name="view-angles",.kind=APPLICATION_NATIVE_VALUE_VECTOR,.value.vector=aim},
        {.name="attack",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(raw->buttons&1u)!=0},
        {.name="jump",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=physical.kind==QA_RULESET_Q2_RERELEASE?(raw->buttons&8u)!=0:raw->up_move>0},
        {.name="impulse",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=raw->impulse},
        {.name="forward-move",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)raw->forward_move/200.},
        {.name="side-move",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)raw->side_move/200.},
        {.name="up-move",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=physical.kind==QA_RULESET_Q2_RERELEASE?
            (raw->buttons&8u)?1.:(raw->buttons&16u)?-1.:0.:(double)raw->up_move/200.}
    };
    application_native_callback_inputs inputs={values,sizeof(values)/sizeof(*values),command};
    declared_raw_call call={engine,&inputs};
    engine->current_client=slot;
    application_native_q2_visibility_invalidate(engine);
    engine->raw_inputs=&inputs;
    bool ok=application_native_q2_callbacks_transfer(engine->callbacks,declared_raw_execute,&call,error);
    engine->raw_inputs=NULL;
    engine->current_client=0; return ok;
}

bool application_native_q2_input_think(application_provider *provider, qa_actor_id actor,
    const qa_usercmd *command, const application_native_q2_input_stage *stage, qa_error *error)
{
    uint32_t slot = source_client_slot(provider, actor);
    struct application_native_q2 *engine = slot ? provider->state.native.q2_engine : NULL;
    if (!engine || !command || !stage || !stage->current || !stage->move ||
        !qa_actor_id_equal(stage->actor,actor) || !stage->current(stage->context,actor) ||
        engine->input_stage || engine->movement_stage || !engine->map_ready)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 raw input requires its current synchronous Source stage");
    bool classic = engine->profile == QA_NATIVE_Q2_GAME_API3;
    if (command->kind != (classic ? QA_RULESET_Q2_CLASSIC : QA_RULESET_Q2_RERELEASE) ||
        command->milliseconds > UINT8_MAX || command->buttons > UINT8_MAX ||
        !isfinite(command->forward_move) || !isfinite(command->side_move) ||
        !isfinite(command->up_move) || !qa_vec_finite(command->angles))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 raw command differs from its physical SDK dialect");
    if (classic) {
        const float axes[] = {command->forward_move, command->side_move, command->up_move};
        for (size_t i = 0; i < 3; ++i) if (axes[i] < INT16_MIN || axes[i] > INT16_MAX)
            return application_fail(error, QA_ERROR_ARGUMENT, "Classic Q2 raw axis exceeds its Source short");
    }
    uint8_t bytes[28]; size_t command_bytes = qa_native_q2_write_usercmd(command, bytes);
    engine->input_stage=stage; engine->input_command=command; engine->current_command_sequence=command->sequence;
    bool ok=application_native_q2_client_think(provider,slot,(qa_bytes){bytes,command_bytes},error);
    engine->input_stage=NULL; engine->input_command=NULL; engine->current_command_sequence=0;
    if (ok && qa_actors_get(qa_session_actors(provider->application->session),actor) &&
        !stage->current(stage->context,actor))
        ok=application_fail(error,QA_ERROR_NOT_FOUND,"Native Q2 raw input lost its actual Source command stage");
    qa_application *app=provider->application;
    if (ok && qa_actors_get(qa_session_actors(app->session),actor) &&
        application_provider_for(app,actor,QA_ROLE_MOVEMENT,NULL)==provider) {
        qa_movement_result physical;
        if (!physical_input_read(provider,actor,&physical,NULL,error)) return false;
        if (actor.slot>=app->control_capacity || !app->controls[actor.slot].active ||
            !qa_actor_id_equal(app->controls[actor.slot].player.actor,actor))
            return application_fail(error,QA_ERROR_NOT_FOUND,"Native Q2 Source completion lost its selected control generation");
        application_control_record *control=&app->controls[actor.slot];
        control->player.state=physical.state; control->player.bounds=physical.bounds;
        control->player.view_angles=physical.view_angles; control->player.view_offset=physical.view_offset;
        control->player.view_height=physical.view_height;
        qa_input_command_basis from={.kind=command->kind,.words=classic}, to={.kind=command->kind};
        qa_usercmd projected;
        qa_input_command_convert(command,NULL,&from,&to,(qa_input_axis_rule){0},&projected);
        control->player.command_angles=projected.angles;
        control->result.state=control->player.state; control->result.bounds=control->player.bounds;
        control->result.view_angles=control->player.view_angles; control->result.view_offset=control->player.view_offset;
        control->result.view_height=control->player.view_height;
    }
    return ok;
}

static bool native_move(application_provider *provider, qa_actor_id actor,
    const qa_usercmd *command, const application_control_external_stage *stage,
    bool *handled, qa_error *error)
{
    if (!handled || !command) return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 movement request is missing");
    *handled = false;
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || engine->profile == QA_NATIVE_Q2_CGAME_API2023) return true;
    if(engine->callbacks && application_provider_for(provider->application,actor,QA_ROLE_MOVEMENT,NULL)==provider)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"Declared native MOVEMENT needs its admitted physical Source ClientThink producer");
    qa_application *app = provider->application;
    if (application_provider_for(app, actor, QA_ROLE_MOVEMENT, NULL) != provider) return true;
    *handled = true;
    uint32_t slot = 0;
    for (uint32_t i = 1; i < 257; ++i)
        if (engine->clients[i].connected && engine->clients[i].begun &&
            qa_actor_id_equal(engine->clients[i].actor, actor)) { slot = i; break; }
    bool classic = engine->profile == QA_NATIVE_Q2_GAME_API3;
    if (!slot || !engine->map_ready || actor.slot >= app->control_capacity ||
        command->kind != (classic ? QA_RULESET_Q2_CLASSIC : QA_RULESET_Q2_RERELEASE) ||
        command->milliseconds > UINT8_MAX || command->buttons > UINT8_MAX ||
        !isfinite(command->forward_move) || !isfinite(command->side_move) ||
        !isfinite(command->up_move) || !qa_vec_finite(command->angles))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 movement lacks its source client or encodable command");
    application_control_record *control = &app->controls[actor.slot];
    if (!control->active || !qa_actor_id_equal(control->player.actor, actor) ||
        (stage ? !control->moving || !stage->current(stage) : control->moving))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 movement control is unavailable");
    if (classic) {
        const float axes[] = {command->forward_move, command->side_move, command->up_move};
        for (size_t i = 0; i < 3; ++i) if (axes[i] < INT16_MIN || axes[i] > INT16_MAX)
            return application_fail(error, QA_ERROR_ARGUMENT, "Classic Q2 movement axis exceeds its source short");
    }
    uint8_t bytes[28]; size_t command_bytes = qa_native_q2_write_usercmd(command, bytes);
    control->moving = true; engine->current_command_sequence = command->sequence;
    engine->movement_stage = stage;
    bool ok = application_native_q2_client_think(provider, slot,
        (qa_bytes){bytes, command_bytes}, error);
    engine->movement_stage = NULL; engine->current_command_sequence = 0;
    if (ok && qa_actors_get(qa_session_actors(app->session), actor)) {
        qa_q2_player player;
        ok = qa_native_host_q2_player(provider->state.native.host, slot, &player, error);
        if (ok) {
            qa_movement_state state = player_movement(&player, command->kind, qa_v3(0, 0, 0));
            qa_vec3 view = qa_v3(player.viewangles[0], player.viewangles[1], player.viewangles[2]);
            qa_vec3 offset = qa_v3(player.viewoffset[0], player.viewoffset[1], player.viewoffset[2]);
            qa_body_state body;
            ok = player_motion_finite(slot, &state, view, offset, error);
            if (ok) ok = qa_world_body_read(engine->world, actor, &body, error);
            if (ok) {
                control->player.state = state; control->player.bounds = body.bounds;
                control->player.view_angles = view; control->player.view_offset = offset;
                qa_input_command_basis from={.kind=command->kind,.words=classic}, to={.kind=command->kind};
                qa_usercmd projected;
                qa_input_command_convert(command,NULL,&from,&to,(qa_input_axis_rule){0},&projected);
                control->player.command_angles=projected.angles;
                control->player.view_height = classic ? offset.z : state.data.q2r.view_height;
                if (!stage) {
                    control->player.previous_buttons = control->player.buttons; control->player.buttons = command->buttons;
                    control->player.command_sequence = command->sequence; control->command_seen = true;
                } else {
                    qa_movement_result *result = &control->result;
                    result->status = QA_MOVEMENT_ACTIVE; result->actor = actor;
                    result->command_sequence = command->sequence; result->state = control->player.state;
                    result->bounds = control->player.bounds; result->ground = control->player.ground;
                    result->view_angles = control->player.view_angles; result->view_offset = control->player.view_offset;
                    result->view_height = control->player.view_height; result->water_level = control->player.water_level;
                    result->water_type = control->player.water_type; result->contact_count = 0;
                }
            }
        }
    }
    if (stage && ok && qa_actors_get(qa_session_actors(app->session), actor) && !stage->current(stage))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 movement lost its actual retained NQ turn");
    if (!stage) control->moving = false;
    return ok;
}

bool application_native_q2_move(application_provider *provider, qa_actor_id actor,
    const qa_usercmd *command, bool *handled, qa_error *error)
{ return native_move(provider, actor, command, NULL, handled, error); }

bool application_native_q2_stage_move(application_provider *provider, qa_actor_id actor,
    const qa_usercmd *command, const application_control_external_stage *stage,
    bool *handled, qa_error *error)
{
    if (!provider || !stage || stage->application != provider->application ||
        !qa_actor_id_equal(stage->actor, actor) || !stage->current || !stage->current(stage))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 movement requires its true retained source stage");
    return native_move(provider, actor, command, stage, handled, error);
}

static struct application_native_q2 *client_owner(application_provider *provider,
    uint32_t slot, bool connected, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !engine->initialized || engine->profile == QA_NATIVE_Q2_CGAME_API2023 ||
        !slot || slot >= 257 || engine->calls || !application_native_q2_idle(provider) ||
        (connected && !engine->clients[slot].connected)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 client requires its idle admitted source slot");
        return NULL;
    }
    return engine;
}

static bool declared_client(struct application_native_q2 *engine,uint32_t slot,
    const char *stage,qa_bytes command,bool *accepted,qa_error *error)
{
    qa_source_frame frame;
    if(!application_native_q2_stages_time_read(engine,&frame,error))return false;
    application_native_callback_value values[] = {
        {.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=engine->clients[slot].actor},
        {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)frame.time_ns/1e9},
        {.name="elapsed",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)frame.elapsed_ns/1e9}
    };
    application_native_callback_inputs inputs={values,3,command};
    return application_native_q2_callbacks_run(engine,stage,&inputs,accepted,error);
}

bool application_native_q2_client_admit(application_provider *provider, uint32_t slot,
    qa_actor_id actor, const char *userinfo, const char *social_id, bool bot,
    bool *accepted, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, false, error);
    if (!engine || !accepted || !userinfo) return false;
    *accepted = false;
    const qa_cvar_view *maximum = qa_cvars_find(engine->cvars, "maxclients");
    if (!maximum || maximum->integer < 1 || maximum->integer > 256 || slot > (uint32_t)maximum->integer)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 client exceeds the source maximum clients");
    if (!qa_actors_get(qa_session_actors(provider->application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 projected client generation is not live");
    application_native_q2_client *client = &engine->clients[slot];
    if (client->denied && qa_actor_id_equal(client->actor, actor)) return true;
    if (client->connected || (client->actor.registry && !qa_actor_id_equal(client->actor, actor)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 source client slot is occupied");
    client->actor = actor; client->bot = bot; client->disconnect_started = false;
    if (client->layout[0]) ++client->layout_revision;
    for (size_t i = 0; i < sizeof(client->inventory) / sizeof(*client->inventory); ++i)
        if (client->inventory[i]) { ++client->inventory_revision; break; }
    memset(client->layout, 0, sizeof(client->layout));
    memset(client->inventory, 0, sizeof(client->inventory));
    client->protocol_fog = (qa_q2_wire_fog){0};
    client->protocol_fog_actor = actor;
    client->userinfo_present = false; client->userinfo[0] = 0;
    if (engine->callbacks) {
        size_t bytes=strlen(userinfo);
        if(!application_native_q2_callbacks_userinfo_validate(engine,userinfo,error)||bytes>=sizeof(client->userinfo)) return false;
        memcpy(client->userinfo,userinfo,bytes+1); client->userinfo_present=true;
        bool ok=application_native_q2_callbacks_components_project(engine,actor,error)&&
            qa_native_host_client_retained_set(provider->state.native.host,slot,true,error)&&
            declared_client(engine,slot,"clients.admit",(qa_bytes){0},accepted,error);
        if(ok&&*accepted) {
            if(!qa_actor_id_equal(client->actor,actor)||!qa_actors_get(qa_session_actors(provider->application->session),actor))
                return application_fail(error,QA_ERROR_ARGUMENT,"Declared native admission retired its actual actor");
            client->connected=true;
            return application_native_q2_callbacks_components_admit(engine,actor,error);
        }
        if (ok) { client->denied = true; return true; }
        qa_error cleanup={0};
        bool detached=qa_native_host_client_retained_set(provider->state.native.host,slot,false,&cleanup)&&
            qa_native_host_detach_actor(provider->state.native.host,slot,actor,&cleanup);
        if(detached) { client->actor=(qa_actor_id){0}; client->userinfo_present=false; client->userinfo[0]=0;
            client->protocol_fog=(qa_q2_wire_fog){0}; client->protocol_fog_actor=(qa_actor_id){0}; }
        if(ok&&!detached&&error) *error=cleanup;
        return ok&&detached;
    }
    qa_native_host_client_request request = {.slot = slot, .userinfo = userinfo,
        .social_id = social_id ? social_id : "", .bot = bot};
    ++engine->calls;
    qa_buffer returned = {0};
    application_native_q2_visibility_invalidate(engine);
    bool ok = qa_native_host_client_connect_userinfo(provider->state.native.host,
        &request, accepted, &returned, error);
    --engine->calls;
    if (returned.data && qa_actor_id_equal(client->actor, actor)) {
        memcpy(client->userinfo, returned.data, returned.size + 1);
        client->userinfo_present = true;
    }
    qa_buffer_free(&returned);
    if (ok && (!qa_actor_id_equal(client->actor, actor) ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor)))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 connect replaced its actual client generation");
    if (ok && *accepted) client->connected = true;
    else {
        qa_error cleanup = {0};
        bool detached = qa_native_host_detach_actor(provider->state.native.host, slot, actor, &cleanup);
        if (!detached && ok) { ok = false; if (error) *error = cleanup; }
        if (detached) {
            client->actor = (qa_actor_id){0};
            client->protocol_fog = (qa_q2_wire_fog){0};
            client->protocol_fog_actor = (qa_actor_id){0};
            client->userinfo_present = false; client->userinfo[0] = 0;
        }
    }
    return ok;
}

bool application_native_q2_client_begin(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, true, error);
    if (!engine) return false;
    if (engine->clients[slot].begun) return application_native_q2_callbacks_components_begin(engine,engine->clients[slot].actor,error)&&
        application_native_q2_inventory_admit(engine, slot, error) &&
        application_native_q2_combat_admit(
            engine, slot, engine->clients[slot].actor, false, error);
    ++engine->calls;
    if(!engine->callbacks) application_native_q2_visibility_invalidate(engine);
    bool ok = engine->callbacks || qa_native_host_client_begin(provider->state.native.host, slot, error);
    --engine->calls;
    if (ok) engine->clients[slot].begun = true;
    return ok && application_native_q2_callbacks_components_begin(engine,engine->clients[slot].actor,error) &&
        application_native_q2_inventory_admit(engine, slot, error) &&
        application_native_q2_combat_admit(
            engine, slot, engine->clients[slot].actor, false, error);
}

bool application_native_q2_clients_reconnect(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !engine->initialized || !engine->map_ready ||
        engine->profile == QA_NATIVE_Q2_CGAME_API2023 || !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 reconnect requires its restored GAME and LEVEL owner");
    for (uint32_t slot = 1; slot < 257; ++slot) {
        application_native_q2_client *client = &engine->clients[slot];
        if (!client->connected) continue;
        qa_actor_id actor = client->actor;
        bool begun = client->begun, bot = client->bot;
        char userinfo[sizeof(client->userinfo)];
        memcpy(userinfo, client->userinfo, sizeof(userinfo));
        /* ReadLevel clears the module's connected state. Use the same public
         * admission path as a returning client, preserving its loaded edict. */
        client->connected = client->begun = false;
        bool accepted = false;
        if (!application_native_q2_client_admit(provider, slot, actor, userinfo, "", bot, &accepted, error)) return false;
        if (!accepted)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 GAME rejected its saved client during reconnect");
        if (begun && !application_native_q2_client_begin(provider, slot, error)) return false;
    }
    return true;
}

bool application_native_q2_client_userinfo(application_provider *provider, uint32_t slot,
    const char *userinfo, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !engine->initialized || engine->profile == QA_NATIVE_Q2_CGAME_API2023 ||
        !slot || slot >= 257 || engine->calls || !engine->clients[slot].connected ||
        !userinfo || !qa_world_idle(engine->world) ||
        !qa_native_host_destroy_ready(provider->state.native.host))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 userinfo requires its returned physical client call");
    qa_actor_id actor = engine->clients[slot].actor;
    if(engine->callbacks) {
        size_t bytes=strlen(userinfo);
        if(!application_native_q2_callbacks_userinfo_validate(engine,userinfo,error)||bytes>=sizeof(engine->clients[slot].userinfo)) return false;
        memcpy(engine->clients[slot].userinfo,userinfo,bytes+1); engine->clients[slot].userinfo_present=true;
        bool accepted;
        return declared_client(engine,slot,"clients.userinfo",(qa_bytes){0},&accepted,error);
    }
    qa_buffer returned = {0};
    ++engine->calls;
    application_native_q2_visibility_invalidate(engine);
    bool ok = qa_native_host_client_userinfo_result(provider->state.native.host,
        slot, userinfo, &returned, error);
    --engine->calls;
    if (!qa_actor_id_equal(engine->clients[slot].actor, actor) ||
        !engine->clients[slot].connected ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor)) {
        qa_buffer_free(&returned);
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 userinfo replaced its actual client generation");
    }
    if (returned.data) {
        memcpy(engine->clients[slot].userinfo, returned.data, returned.size + 1);
        engine->clients[slot].userinfo_present = true;
    }
    qa_buffer_free(&returned);
    return ok;
}

bool application_native_q2_client_disconnect(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, false, error);
    if (!engine) return false;
    application_native_q2_client *client = &engine->clients[slot];
    if (!client->actor.registry) return true;
    qa_actor_id actor = client->actor;
    engine->disconnect_client = slot;
    bool ok = true; qa_error first = {0};
    if (client->connected && !client->disconnect_started) {
        client->disconnect_started = true;
        if (!qa_native_terminal(qa_native_host_instance(provider->state.native.host))) {
            ++engine->calls;
            if(engine->callbacks) {
                bool accepted;
                ok=declared_client(engine,slot,"clients.disconnect",(qa_bytes){0},&accepted,&first);
            } else {
                application_native_q2_visibility_invalidate(engine);
                ok = qa_native_host_client_disconnect(provider->state.native.host, slot, &first);
            }
            --engine->calls;
        }
    }
    if (!qa_actor_id_equal(client->actor, actor)) {
        engine->disconnect_client = 0;
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 disconnect replaced its entered Source generation");
    }
    application_native_q2_inventory_scanner_release(engine->inventory_scanner,actor);
    qa_error current = {0};
    if (!application_native_q2_combat_detach(engine, actor, &current) ||
        !application_native_q2_inventory_detach(engine, slot, &current) ||
        !application_native_q2_callbacks_release_actor(engine,actor,&current)) {
        engine->disconnect_client = 0;
        if (error) *error = ok ? current : first;
        return false;
    }
    if (!qa_native_host_detach_actor(provider->state.native.host, slot, actor, &current) ||
        !qa_native_host_source_reconcile(provider->state.native.host, &current)) {
        engine->disconnect_client = 0;
        if (error) *error = ok ? current : first;
        return false;
    }
    engine->disconnect_client = 0;
    client->connected = client->begun = false;
    client->denied = false;
    application_native_q2_visibility_released(engine, actor);
    client->actor = (qa_actor_id){0}; client->bot = false;
    client->protocol_fog = (qa_q2_wire_fog){0};
    client->protocol_fog_actor = (qa_actor_id){0};
    client->userinfo_present = false; client->userinfo[0] = 0;
    if (!ok && error) *error = first;
    return ok;
}

bool application_native_q2_actor_disconnect(application_provider *provider,
    qa_actor_id actor, qa_error *error)
{
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE ?
        provider->state.native.q2_engine : NULL;
    if (!engine || engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 disconnect requires its actual GAME owner");
    uint32_t slot = 0;
    for (uint32_t i = 1; i < 257; ++i) {
        if (!qa_actor_id_equal(engine->clients[i].actor, actor)) continue;
        if (slot) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 disconnect aliases two physical clients");
        slot = i;
    }
    return !slot || application_native_q2_client_disconnect(provider, slot, error);
}

bool application_native_q2_client_think(application_provider *provider, uint32_t slot,
    qa_bytes command, qa_error *error)
{
    struct application_native_q2 *declared = provider ? provider->state.native.q2_engine : NULL;
    if (declared && declared->callbacks) {
        if (!slot || slot >= 257 || !declared->clients[slot].connected || !declared->clients[slot].begun ||
            !qa_actors_get(qa_session_actors(provider->application->session), declared->clients[slot].actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Declared native input lost its admitted physical client");
        return declared_raw_think(declared,slot,command,error);
    }
    struct application_native_q2 *engine = client_owner(provider, slot, true, error);
    if (!engine || !engine->clients[slot].begun) return false;
    if (!engine->source_attack && application_provider_for(provider->application, engine->clients[slot].actor,
            QA_ROLE_ARSENAL, NULL) != provider)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Native Q2 ClientThink requires its declared weapon-dispatch boundary for a foreign arsenal");
    engine->current_client = slot;
    ++engine->calls;
    application_native_q2_visibility_invalidate(engine);
    bool ok = qa_native_host_client_think(provider->state.native.host, slot, command, error);
    --engine->calls; engine->current_client = 0;
    return ok;
}

static bool command_arguments_copy(const qa_command_invocation *command,
    qa_command_tokens *out, qa_error *error)
{
    if (!command || !command->argc || command->argc > INT32_MAX ||
        !command->argv || !command->args_text)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 command requires its actual entered arguments");
    size_t extent = 0;
    for (size_t i = 0; i < command->argc; ++i) {
        if (!command->argv[i])
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 GAME command has an absent argument");
        size_t length = strlen(command->argv[i]);
        if (length == SIZE_MAX || length + 1 > SIZE_MAX - extent)
            return application_fail(error, QA_ERROR_MEMORY, "Native Q2 GAME command argument extent overflows");
        extent += length + 1;
    }
    if (command->argc > SIZE_MAX / sizeof(char *))
        return application_fail(error, QA_ERROR_MEMORY, "Native Q2 GAME command argument table overflows");
    qa_command_tokens tokens = {.count = command->argc};
    tokens.values = calloc(command->argc, sizeof(*tokens.values));
    tokens.storage = malloc(extent);
    size_t args_size = strlen(command->args_text);
    if (args_size != SIZE_MAX) tokens.args_text = malloc(args_size + 1);
    if (!tokens.values || !tokens.storage || !tokens.args_text) {
        qa_command_tokens_free(&tokens);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining exact native Q2 GAME command arguments");
    }
    size_t offset = 0;
    for (size_t i = 0; i < command->argc; ++i) {
        size_t size = strlen(command->argv[i]) + 1;
        tokens.values[i] = tokens.storage + offset;
        memcpy(tokens.values[i], command->argv[i], size); offset += size;
    }
    memcpy(tokens.args_text, command->args_text, args_size + 1);
    *out = tokens;
    return true;
}

bool application_native_q2_client_command(application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, bool *handled, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    const qa_native_region_event *event = engine ?
        application_native_q2_inventory_scanner_command_event(engine->inventory_scanner, actor) : NULL;
    if (!handled || !command || !engine || !engine->initialized || (engine->calls && !event) ||
        !qa_world_idle(engine->world) || (!event && !qa_native_host_destroy_ready(provider->state.native.host)) ||
        engine->profile == QA_NATIVE_Q2_CGAME_API2023 ||
        !command->argc || command->argc > INT32_MAX || !command->argv || !command->args_text ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 ClientCommand requires its actual invocation and live client");
    *handled = false;
    uint32_t slot = 0;
    for (uint32_t i = 1; i < 257; ++i)
        if (engine->clients[i].connected && engine->clients[i].begun &&
            qa_actor_id_equal(engine->clients[i].actor, actor)) {
            if (slot) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 ClientCommand aliases two physical client slots");
            slot = i;
        }
    if (!slot) return true;
    qa_command_tokens tokens = {0};
    if (!command_arguments_copy(command, &tokens, error)) return false;
    qa_command_tokens prior = engine->arguments;
    uint32_t prior_client = engine->current_client;
    engine->arguments = tokens; engine->current_client = slot;
    ++engine->calls;
    bool ok;
    bool declared_handled=true;
    if(engine->callbacks) {
        const qa_json_document *d=application_native_q2_callbacks_document(engine->callbacks);
        qa_json_id clients=qa_json_get(d,qa_json_root(d),"clients");
        declared_handled=qa_json_size(d,qa_json_get(d,clients,"command"))!=0;
        bool accepted;
        ok=declared_client(engine,slot,"clients.command",(qa_bytes){0},&accepted,error);
    } else {
        application_native_q2_visibility_invalidate(engine);
        ok = event ? qa_native_host_client_command_region(provider->state.native.host, slot, event, error) :
            qa_native_host_client_command(provider->state.native.host, slot, error);
    }
    --engine->calls;
    engine->current_client = prior_client;
    qa_command_tokens_free(&engine->arguments); engine->arguments = prior;
    if (ok) *handled = declared_handled;
    return ok;
}

bool application_native_q2_game_command(application_provider *provider,
    const qa_command_invocation *command, bool *handled, qa_error *error)
{
    if (!provider || !command || !handled ||
        !qa_application_command_context_active(provider->application, &command->context))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 GAME command requires its actual invocation");
    if (command->context.actor.registry)
        return application_native_q2_client_command(provider, command->context.actor, command, handled, error);
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !engine->initialized || engine->calls ||
        engine->profile == QA_NATIVE_Q2_CGAME_API2023 || !qa_world_idle(engine->world) ||
        !qa_native_host_destroy_ready(provider->state.native.host))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 ServerCommand requires its returned initialized game");
    *handled = false;
    qa_command_tokens tokens = {0};
    if (!command_arguments_copy(command, &tokens, error)) return false;
    qa_command_tokens prior = engine->arguments;
    engine->arguments = tokens;
    ++engine->calls;
    application_native_q2_visibility_invalidate(engine);
    bool ok = qa_native_host_server_command(provider->state.native.host, error);
    --engine->calls;
    qa_command_tokens_free(&engine->arguments);
    engine->arguments = prior;
    if (ok) *handled = true;
    return ok;
}

bool application_native_q2_console_command(application_provider *provider, qa_actor_id actor,
    const char *text, bool *handled, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !engine->initialized || !handled || !text || engine->calls ||
        engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 console export requires an idle initialized game");
    *handled = false;
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(text, engine->command_context.dialect, false, &tokens, NULL, NULL, error)) return false;
    qa_command_context context = engine->command_context;
    context.actor = actor;
    if (!qa_application_capture_command_context(provider->application, &context, &context, error)) {
        qa_command_tokens_free(&tokens);
        return false;
    }
    qa_command_invocation command = {.console = engine->console, .context = context,
        .argc = tokens.count, .argv = (const char *const *)tokens.values,
        .args_text = tokens.args_text, .raw = text};
    bool ok = !tokens.count || application_native_q2_game_command(provider, &command, handled, error);
    qa_command_tokens_free(&tokens);
    return ok;
}
