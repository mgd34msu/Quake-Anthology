#include "guest_native_q2_private.h"
#include "guest_native_q2_combat.h"
#include "native_q2_callbacks.h"
#include "native_q2_inventory_scanner.h"
#include "control_frame.h"
#include <math.h>

static void store_float(uint8_t *data, float value)
{
    uint32_t bits; memcpy(&bits, &value, sizeof(bits)); qa_store_u32le(data, bits);
}

static bool native_move(application_provider *provider, qa_actor_id actor,
    const qa_movement_command *command, const application_control_external_stage *stage,
    bool *handled, qa_error *error)
{
    if (!handled || !command) return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 movement request is missing");
    *handled = false;
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || engine->profile == QA_NATIVE_Q2_CGAME_API2023) return true;
    qa_application *app = provider->application;
    if (application_provider_for(app, actor, QA_ROLE_MOVEMENT, NULL) != provider) return true;
    *handled = true;
    uint32_t slot = 0;
    for (uint32_t i = 1; i < 257; ++i)
        if (engine->clients[i].connected && engine->clients[i].begun &&
            qa_actor_id_equal(engine->clients[i].actor, actor)) { slot = i; break; }
    bool classic = engine->profile == QA_NATIVE_Q2_GAME_API3;
    if (!slot || !engine->map_ready || actor.slot >= app->control_capacity ||
        command->kind != (classic ? QA_MOVEMENT_Q2_CLASSIC : QA_MOVEMENT_Q2_RERELEASE) ||
        command->milliseconds > UINT8_MAX || command->buttons > UINT8_MAX ||
        !isfinite(command->forward_move) || !isfinite(command->side_move) ||
        !isfinite(command->up_move) || !qa_vec_finite(command->angles))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 movement lacks its source client or encodable command");
    application_control_record *control = &app->controls[actor.slot];
    if (!control->active || !qa_actor_id_equal(control->actor, actor) ||
        (stage ? !control->moving || !stage->current(stage) : control->moving))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 movement control is unavailable");
    uint8_t bytes[28] = {0}; bytes[0] = (uint8_t)command->milliseconds; bytes[1] = (uint8_t)command->buttons;
    if (classic) {
        float axes[] = {command->forward_move, command->side_move, command->up_move};
        for (size_t i = 0; i < 3; ++i) {
            if (axes[i] < INT16_MIN || axes[i] > INT16_MAX)
                return application_fail(error, QA_ERROR_ARGUMENT, "Classic Q2 movement axis exceeds its source short");
            qa_store_u16le(bytes + 2 + i * 2, (uint16_t)command->angle_words[i]);
            qa_store_u16le(bytes + 8 + i * 2, (uint16_t)(int16_t)axes[i]);
        }
        bytes[14] = command->impulse; bytes[15] = command->light_level;
    } else {
        store_float(bytes + 4, command->angles.x); store_float(bytes + 8, command->angles.y);
        store_float(bytes + 12, command->angles.z); store_float(bytes + 16, command->forward_move);
        store_float(bytes + 20, command->side_move); qa_store_u32le(bytes + 24, (uint32_t)command->server_frame);
    }
    control->moving = true; engine->current_command_sequence = command->sequence;
    engine->movement_stage = stage;
    bool ok = application_native_q2_client_think(provider, slot,
        (qa_bytes){bytes, classic ? 16u : 28u}, error);
    engine->movement_stage = NULL; engine->current_command_sequence = 0;
    if (ok && qa_actors_get(qa_session_actors(app->session), actor)) {
        qa_buffer player = {0};
        ok = qa_native_host_q2_player_state(provider->state.native.host, slot, &player, error);
        if (ok) {
            qa_movement_state state = qa_movement_state_default(command->kind, qa_v3(0, 0, 0));
            const uint8_t *data = player.data;
            if (classic) {
                state.data.q2.type = qa_load_i32le(data);
                for (size_t i = 0; i < 3; ++i) {
                    state.data.q2.origin_eighths[i] = (int16_t)qa_load_u16le(data + 4 + i * 2);
                    state.data.q2.velocity_eighths[i] = (int16_t)qa_load_u16le(data + 10 + i * 2);
                    state.data.q2.delta_angle_shorts[i] = (int16_t)qa_load_u16le(data + 20 + i * 2);
                }
                state.data.q2.flags = data[16]; state.data.q2.time_eight_ms = data[17];
                state.data.q2.gravity = (int16_t)qa_load_u16le(data + 18);
            } else {
                state.data.q2r.type = data[0];
                state.data.q2r.origin = qa_v3(qa_load_f32le(data + 4), qa_load_f32le(data + 8), qa_load_f32le(data + 12));
                state.data.q2r.velocity = qa_v3(qa_load_f32le(data + 16), qa_load_f32le(data + 20), qa_load_f32le(data + 24));
                state.data.q2r.flags = qa_load_u16le(data + 28);
                state.data.q2r.time_ms = qa_load_u16le(data + 30);
                state.data.q2r.gravity = (int16_t)qa_load_u16le(data + 32);
                state.data.q2r.delta_angles = qa_v3(qa_load_f32le(data + 36), qa_load_f32le(data + 40), qa_load_f32le(data + 44));
                state.data.q2r.view_height = (int8_t)data[48];
            }
            size_t angles = classic ? 28 : 52;
            qa_vec3 view = qa_v3(qa_load_f32le(data + angles), qa_load_f32le(data + angles + 4), qa_load_f32le(data + angles + 8));
            qa_vec3 offset = qa_v3(qa_load_f32le(data + angles + 12), qa_load_f32le(data + angles + 16), qa_load_f32le(data + angles + 20));
            qa_body_state body;
            ok = qa_vec_finite(qa_movement_origin(&state)) && qa_vec_finite(qa_movement_velocity(&state)) &&
                 qa_vec_finite(view) && qa_vec_finite(offset);
            if (!ok) application_fail(error, QA_ERROR_FORMAT, "Native Q2 client returned invalid public movement state");
            if (ok) ok = qa_world_body_read(engine->world, actor, &body, error);
            if (ok) {
                control->state = state; control->bounds = body.bounds;
                control->view_angles = view; control->view_offset = offset;
                control->command_angles = classic
                    ? qa_v3((float)command->angle_words[0] * (360.f / 65536.f),
                            (float)command->angle_words[1] * (360.f / 65536.f),
                            (float)command->angle_words[2] * (360.f / 65536.f))
                    : command->angles;
                control->view_height = classic ? offset.z : state.data.q2r.view_height;
                if (!stage) {
                    control->previous_buttons = control->buttons; control->buttons = command->buttons;
                    control->command_sequence = command->sequence; control->command_seen = true;
                } else {
                    qa_movement_result *result = &control->result;
                    result->status = QA_MOVEMENT_ACTIVE; result->actor = actor;
                    result->command_sequence = command->sequence; result->state = control->state;
                    result->bounds = control->bounds; result->ground = control->ground;
                    result->view_angles = control->view_angles; result->view_offset = control->view_offset;
                    result->view_height = control->view_height; result->water_level = control->water_level;
                    result->water_type = control->water_type; result->contact_count = 0;
                }
            }
        }
        qa_buffer_free(&player);
    }
    if (stage && ok && qa_actors_get(qa_session_actors(app->session), actor) && !stage->current(stage))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 movement lost its actual retained NQ turn");
    if (!stage) control->moving = false;
    return ok;
}

bool application_native_q2_move(application_provider *provider, qa_actor_id actor,
    const qa_movement_command *command, bool *handled, qa_error *error)
{ return native_move(provider, actor, command, NULL, handled, error); }

bool application_native_q2_stage_move(application_provider *provider, qa_actor_id actor,
    const qa_movement_command *command, const application_control_external_stage *stage,
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
    application_native_callback_value values[] = {
        {.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=engine->clients[slot].actor},
        {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)engine->frame.time_ns/1e9},
        {.name="elapsed",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)engine->frame.elapsed_ns/1e9}
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
    if (client->connected || (client->actor.registry && !qa_actor_id_equal(client->actor, actor)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 source client slot is occupied");
    client->actor = actor; client->bot = bot; client->disconnect_started = false;
    memset(client->layout, 0, sizeof(client->layout));
    memset(client->inventory, 0, sizeof(client->inventory));
    client->protocol_fog = (qa_q2_wire_fog){0};
    client->protocol_fog_actor = actor;
    client->userinfo_present = false; client->userinfo[0] = 0;
    if (engine->callbacks) {
        size_t bytes=strlen(userinfo);
        if(!application_native_q2_callbacks_userinfo_validate(engine,userinfo,error)||bytes>=sizeof(client->userinfo)) return false;
        memcpy(client->userinfo,userinfo,bytes+1); client->userinfo_present=true;
        bool ok=qa_native_host_client_retained_set(provider->state.native.host,slot,true,error)&&
            declared_client(engine,slot,"clients.admit",(qa_bytes){0},accepted,error);
        if(ok&&*accepted) {
            if(!qa_actor_id_equal(client->actor,actor)||!qa_actors_get(qa_session_actors(provider->application->session),actor))
                return application_fail(error,QA_ERROR_ARGUMENT,"Declared native admission retired its actual actor");
            client->connected=true; return true;
        }
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
    if (engine->clients[slot].begun) return application_native_q2_inventory_admit(engine, slot, error) &&
        application_native_q2_combat_admit(
            engine, slot, engine->clients[slot].actor, false, error);
    ++engine->calls;
    bool ok = engine->callbacks || qa_native_host_client_begin(provider->state.native.host, slot, error);
    --engine->calls;
    if (ok) engine->clients[slot].begun = true;
    return ok && application_native_q2_inventory_admit(engine, slot, error) &&
        application_native_q2_combat_admit(
            engine, slot, engine->clients[slot].actor, false, error);
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
    bool ok = true; qa_error first = {0};
    if (client->connected && !client->disconnect_started) {
        client->disconnect_started = true;
        if (!qa_native_terminal(qa_native_host_instance(provider->state.native.host))) {
            engine->disconnect_client = slot;
            ++engine->calls;
            if(engine->callbacks) {
                bool accepted;
                ok=declared_client(engine,slot,"clients.disconnect",(qa_bytes){0},&accepted,&first);
                qa_error release_error = {0};
                if (!qa_native_host_client_retained_set(provider->state.native.host,slot,false,&release_error)) {
                    if (ok) first = release_error;
                    ok = false;
                }
            } else ok = qa_native_host_client_disconnect(provider->state.native.host, slot, &first);
            --engine->calls;
            engine->disconnect_client = 0;
        }
    }
    if (!qa_actor_id_equal(client->actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 disconnect replaced its entered Source generation");
    client->connected = client->begun = false;
    application_native_q2_inventory_scanner_release(engine->inventory_scanner,actor);
    qa_error current = {0};
    if (!application_native_q2_callbacks_release_actor(engine,actor,&current) ||
        !application_native_q2_combat_detach(engine, actor, &current) ||
        !application_native_q2_inventory_detach(engine, slot, &current)) {
        if (error) *error = ok ? current : first;
        return false;
    }
    if (!qa_native_host_detach_actor(provider->state.native.host, slot, actor, &current)) {
        if (error) *error = ok ? current : first;
        return false;
    }
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
    struct application_native_q2 *engine = client_owner(provider, slot, true, error);
    if (!engine || !engine->clients[slot].begun) return false;
    if (application_provider_for(provider->application, engine->clients[slot].actor,
            QA_ROLE_ARSENAL, NULL) != provider)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Native Q2 ClientThink requires its declared weapon-dispatch boundary for a foreign arsenal");
    engine->current_client = slot;
    ++engine->calls;
    bool ok = qa_native_host_client_think(provider->state.native.host, slot, command, error);
    --engine->calls; engine->current_client = 0;
    return ok;
}

bool application_native_q2_client_command(application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, bool *handled, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!handled || !command || !engine || !engine->initialized || engine->calls ||
        !qa_world_idle(engine->world) || !qa_native_host_destroy_ready(provider->state.native.host) ||
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
    size_t extent = 0;
    for (size_t i = 0; i < command->argc; ++i) {
        if (!command->argv[i])
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 ClientCommand has an absent argument");
        size_t length = strlen(command->argv[i]);
        if (length == SIZE_MAX || length + 1 > SIZE_MAX - extent)
            return application_fail(error, QA_ERROR_MEMORY, "Native Q2 ClientCommand argument extent overflows");
        extent += length + 1;
    }
    if (command->argc > SIZE_MAX / sizeof(char *))
        return application_fail(error, QA_ERROR_MEMORY, "Native Q2 ClientCommand argument table overflows");
    qa_command_tokens tokens = {.count = command->argc};
    tokens.values = calloc(command->argc, sizeof(*tokens.values));
    tokens.storage = malloc(extent);
    size_t args_size = strlen(command->args_text);
    if (args_size != SIZE_MAX) tokens.args_text = malloc(args_size + 1);
    if (!tokens.values || !tokens.storage || !tokens.args_text) {
        qa_command_tokens_free(&tokens);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining exact native Q2 ClientCommand arguments");
    }
    size_t offset = 0;
    for (size_t i = 0; i < command->argc; ++i) {
        size_t size = strlen(command->argv[i]) + 1;
        tokens.values[i] = tokens.storage + offset;
        memcpy(tokens.values[i], command->argv[i], size); offset += size;
    }
    memcpy(tokens.args_text, command->args_text, args_size + 1);
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
    } else ok = qa_native_host_client_command(provider->state.native.host, slot, error);
    --engine->calls;
    engine->current_client = prior_client;
    qa_command_tokens_free(&engine->arguments); engine->arguments = prior;
    if (ok) *handled = declared_handled;
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
    uint32_t slot = 0;
    if (actor.registry) {
        if (!qa_actors_get(qa_session_actors(provider->application->session), actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 command actor generation retired");
        for (uint32_t i = 1; i < 257; ++i)
            if (engine->clients[i].connected && engine->clients[i].begun &&
                qa_actor_id_equal(engine->clients[i].actor, actor)) { slot = i; break; }
        if (!slot) return true;
    }
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(text, engine->command_context.dialect, false, &tokens, error)) return false;
    qa_command_tokens prior = engine->arguments; engine->arguments = tokens;
    ++engine->calls;
    bool ok;
    bool declared_handled=true;
    if(slot&&engine->callbacks) {
        const qa_json_document *d=application_native_q2_callbacks_document(engine->callbacks);
        qa_json_id clients=qa_json_get(d,qa_json_root(d),"clients");
        declared_handled=qa_json_size(d,qa_json_get(d,clients,"command"))!=0;
        bool accepted;
        ok=declared_client(engine,slot,"clients.command",(qa_bytes){0},&accepted,error);
    } else ok = slot ? qa_native_host_client_command(provider->state.native.host, slot, error) :
        qa_native_host_server_command(provider->state.native.host, error);
    --engine->calls;
    qa_command_tokens_free(&engine->arguments); engine->arguments = prior;
    if (ok) *handled = declared_handled;
    return ok;
}
