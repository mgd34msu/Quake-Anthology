#include "guest_native_q2_private.h"
#include "guest_native_q2_combat.h"
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
    qa_native_host_client_request request = {.slot = slot, .userinfo = userinfo,
        .social_id = social_id ? social_id : "", .bot = bot};
    ++engine->calls;
    bool ok = qa_native_host_client_connect(provider->state.native.host, &request, accepted, error);
    --engine->calls;
    if (ok && *accepted) client->connected = true;
    else {
        qa_error cleanup = {0};
        bool detached = qa_native_host_detach_actor(provider->state.native.host, slot, actor, &cleanup);
        if (!detached && ok) { ok = false; if (error) *error = cleanup; }
        if (detached) client->actor = (qa_actor_id){0};
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
    bool ok = qa_native_host_client_begin(provider->state.native.host, slot, error);
    --engine->calls;
    if (ok) engine->clients[slot].begun = true;
    return ok && application_native_q2_inventory_admit(engine, slot, error) &&
        application_native_q2_combat_admit(
            engine, slot, engine->clients[slot].actor, false, error);
}

bool application_native_q2_client_userinfo(application_provider *provider, uint32_t slot,
    const char *userinfo, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, true, error);
    if (!engine || !userinfo) return false;
    ++engine->calls;
    bool ok = qa_native_host_client_userinfo(provider->state.native.host, slot, userinfo, error);
    --engine->calls;
    return ok;
}

bool application_native_q2_client_disconnect(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, false, error);
    if (!engine) return false;
    application_native_q2_client *client = &engine->clients[slot];
    if (!client->actor.registry) return true;
    if (!application_native_q2_combat_detach(engine, client->actor, error)) return false;
    if (!application_native_q2_inventory_detach(engine, slot, error)) return false;
    qa_actor_id actor = client->actor;
    bool ok = true; qa_error first = {0};
    if (client->connected && !client->disconnect_started) {
        client->disconnect_started = true; client->connected = client->begun = false;
        if (!qa_native_terminal(qa_native_host_instance(provider->state.native.host))) {
            ++engine->calls;
            ok = qa_native_host_client_disconnect(provider->state.native.host, slot, &first);
            --engine->calls;
        }
    }
    qa_error current = {0};
    if (!qa_native_host_detach_actor(provider->state.native.host, slot, actor, &current)) {
        if (error) *error = ok ? current : first;
        return false;
    }
    client->actor = (qa_actor_id){0}; client->bot = false;
    if (!ok && error) *error = first;
    return ok;
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
    bool ok = slot ? qa_native_host_client_command(provider->state.native.host, slot, error) :
        qa_native_host_server_command(provider->state.native.host, error);
    --engine->calls;
    qa_command_tokens_free(&engine->arguments); engine->arguments = prior;
    if (ok) *handled = true;
    return ok;
}
