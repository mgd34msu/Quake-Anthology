#include "native_q3_control.h"
#include "native_q3_clients.h"
#include "native_q3_console.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "unified_q3_events.h"
#include "bots_private.h"
#include "control_frame.h"
#include "qa/game_q3_source.h"

#include <stdlib.h>
#include <string.h>

typedef struct native_q3_think_call {
    application_provider *provider;
    qa_actor_id actor;
    qa_q3_usercmd accepted, movement;
    int32_t milliseconds;
    uint32_t movement_milliseconds;
} native_q3_think_call;

static bool live(const qa_application *app, qa_actor_id actor)
{
    return qa_actors_get(qa_session_actors(app->session), actor) != NULL;
}

static int32_t signed_word(uint32_t bits)
{
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool inactivity(application_provider *provider, qa_actor_id actor,
    bool *allowed, qa_error *error)
{
    qa_q3_game *game = provider->state.q3;
    qa_q3_native_client client;
    uint32_t slot;
    int32_t now, seconds, deadline; bool warned;
    *allowed = true;
    if (!qa_q3_client_read(game, actor, &client, error) ||
        !qa_q3_native_client_slot(game, actor, &slot, error) ||
        !qa_q3_source_clock(game, &now, error) ||
        !application_native_q3_settings_integer(provider, "g_inactivity", &seconds, error) ||
        !qa_q3_client_inactivity_read(game, actor, &deadline, &warned, error)) return false;
    const qa_q3_usercmd *command = &client.command;
    if (!seconds || command->forwardmove || command->rightmove || command->upmove ||
        (command->buttons & 1)) {
        int32_t delay = seconds ? signed_word((uint32_t)seconds * 1000u) : 60000;
        return qa_q3_client_inactivity_write(game, actor,
            signed_word((uint32_t)now + (uint32_t)delay), false, error);
    }
    if (client.local_client) return true;
    if (now > deadline) {
        *allowed = false;
        return application_native_q3_wire_drop(provider, slot, "Dropped due to inactivity", error);
    }
    if (now > signed_word((uint32_t)deadline - 10000u) && !warned) {
        if (!qa_q3_client_inactivity_write(game, actor, deadline, true, error)) return false;
        return application_native_q3_send_command(provider, (int32_t)slot,
            "cp \"Ten seconds until inactivity drop!\n\"", error);
    }
    return true;
}

static bool touch(application_provider *provider, qa_actor_id self,
    qa_actor_id other, qa_error *error)
{
    qa_application *app = provider->application;
    if (!live(app, self) || !live(app, other)) return true;
    bool native, touchable, door;
    if (!qa_q3_client_touch_policy(provider->state.q3, self, &native, &touchable, &door, error)) return false;
    if (native && !touchable) return true;
    qa_touch_contact contact = {.self = self, .other = other};
    return !app->physics->services.touch ||
        app->physics->services.touch(app->physics->services.context, &contact, error);
}

static bool bot_touch(application_provider *provider, qa_actor_id actor,
    qa_actor_id other, qa_error *error)
{
    if (!live(provider->application, actor) || !live(provider->application, other)) return true;
    uint32_t slot, flags;
    bool native, touchable, door;
    if (!qa_q3_native_client_slot(provider->state.q3, actor, &slot, error) ||
        !qa_q3_client_server_flags(provider->state.q3, slot, &flags, error) ||
        !qa_q3_client_touch_policy(provider->state.q3, actor, &native, &touchable, &door, error)) return false;
    return !(flags & 8u) || !touchable || touch(provider, actor, other, error);
}

static bool impacts(application_provider *provider, qa_actor_id actor,
    const qa_actor_id *contacts, size_t count, qa_error *error)
{
    qa_application *app = provider->application;
    for (size_t i = 0; i < count && live(app, actor); ++i) {
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j)
            if (qa_actor_id_equal(contacts[j], contacts[i])) { duplicate = true; break; }
        if (duplicate || !live(app, contacts[i])) continue;
        if (!bot_touch(provider, actor, contacts[i], error) ||
            !touch(provider, contacts[i], actor, error)) return false;
    }
    return true;
}

static bool snapshot_contacts(application_provider *provider, const qa_movement_result *result,
    qa_actor_id *storage, size_t capacity, qa_actor_id **out, size_t *count, qa_error *error)
{
    if (result->contact_count > SIZE_MAX / sizeof(**out))
        return application_fail(error, QA_ERROR_MEMORY, "Native Q3 contact snapshot overflow");
    qa_actor_id *ids = result->contact_count <= capacity ? storage
        : malloc(result->contact_count * sizeof(*ids));
    if (result->contact_count && !ids)
        return application_fail(error, QA_ERROR_MEMORY, "Allocating native Q3 command contacts");
    size_t used = 0;
    for (size_t i = 0; i < result->contact_count; ++i) {
        if (result->contacts[i].trace.hit == QA_TRACE_HIT_ACTOR)
            ids[used++] = result->contacts[i].trace.actor;
        else if (result->contacts[i].trace.hit == QA_TRACE_HIT_WORLD) {
            qa_q3_source_binding world;
            if (!qa_q3_source_binding_read(provider->state.q3, QA_Q3_SOURCE_WORLD, &world, error)) {
                if (ids != storage) free(ids);
                return false;
            }
            if (world.in_use && world.actor.registry) ids[used++] = world.actor;
        }
    }
    *out = ids; *count = used;
    return true;
}

static bool contact_actor(qa_application *app, qa_bounds bounds,
    qa_actor_id actor, bool *contact, qa_error *error)
{
    *contact = false;
    qa_linked_body linked;
    if (!qa_world_linked(app->world, actor, &linked)) return true;
    qa_actor_collision collision;
    qa_error observed = {0};
    if (!qa_world_get_collision(app->world, actor, &collision, &observed)) {
        if (observed.code != QA_OK) { if (error) *error = observed; return false; }
        return true;
    }
    qa_body_state body;
    if (!qa_world_body_read(app->world, actor, &body, error)) return false;
    if (!live(app, actor)) return true;
    qa_trace_query query = {.shape = {QA_SHAPE_BOX, bounds},
        .policy = qa_collision_default_policy(QA_COLLISION_Q3),
        .target = {.inline_model = collision.inline_model, .model = collision.model,
            .origin = body.origin, .angles = body.angles}};
    query.policy.contents_mask = UINT32_MAX;
    qa_trace_result trace;
    bool ok = collision.inline_model
        ? qa_collision_trace(qa_world_geometry(app->world), &query, &trace, error)
        : qa_collision_trace_body(&query, collision.family, collision.shape,
            body.bounds, body.origin, collision.contents, &trace, error);
    if (ok) *contact = trace.start_solid;
    return ok;
}

static bool touches_item(qa_vec3 player_origin, const qa_q3_trajectory *position,
    int32_t time_ms, bool *contact, qa_error *error)
{
    qa_trajectory trajectory = {.type = (qa_trajectory_type)position->type,
        .time_ms = position->time, .duration_ms = position->duration,
        .base = {position->base[0], position->base[1], position->base[2]},
        .delta = {position->delta[0], position->delta[1], position->delta[2]}};
    qa_vec3 origin;
    if (!qa_trajectory_position(&trajectory, time_ms, 800, &origin, error)) return false;
    qa_vec3 delta = qa_vec_sub(player_origin, origin);
    *contact = !(delta.x > 44 || delta.x < -50 || delta.y > 36 || delta.y < -36 ||
        delta.z > 36 || delta.z < -36);
    return true;
}

static bool movement_bounds(application_provider *provider, qa_actor_id actor,
    qa_bounds bounds, qa_error *error)
{
    qa_application *app = provider->application;
    qa_body_state body;
    if (!qa_world_body_read(app->world, actor, &body, error)) return false;
    if (!live(app, actor)) return true;
    body.bounds = bounds;
    return qa_world_body_write(app->world, actor, &body, error);
}

static bool touch_triggers(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    qa_application *app = provider->application;
    qa_q3_game *game = provider->state.q3;
    uint32_t slot;
    qa_q3_player player;
    qa_body_state body;
    int32_t now;
    if (!qa_q3_native_client_slot(game, actor, &slot, error) ||
        !qa_q3_wire_player_read(game, slot, &player, error) ||
        !qa_world_body_read(app->world, actor, &body, error) ||
        !qa_q3_source_clock(game, &now, error)) return false;
    if (!live(app, actor) || player.stats[0] <= 0) return true;
    qa_vec3 origin = qa_v3(player.origin[0], player.origin[1], player.origin[2]);
    qa_vec3 range = qa_v3(40, 40, 52);
    qa_bounds area = {qa_vec_sub(origin, range), qa_vec_add(origin, range)};
    qa_bounds bounds = qa_bounds_translate(body.bounds, origin);
    qa_actor_id candidates[QA_Q3_SOURCE_ENTITIES];
    size_t count; bool overflow;
    if (!qa_world_query(app->world, area, QA_COLLISION_BOTH, candidates,
        QA_Q3_SOURCE_ENTITIES, &count, &overflow, error)) return false;
    for (size_t i = 0; i < count && live(app, actor); ++i) {
        qa_actor_id candidate = candidates[i];
        if (!live(app, candidate)) continue;
        bool native, touchable, door;
        if (!qa_q3_client_touch_policy(game, candidate, &native, &touchable, &door, error)) return false;
        if (native && !touchable) continue;
        qa_actor_collision collision;
        qa_error observed = {0};
        if (!qa_world_get_collision(app->world, candidate, &collision, &observed)) {
            if (observed.code != QA_OK) { if (error) *error = observed; return false; }
            continue;
        }
        qa_q3_entity entity = {0};
        if (native) {
            uint32_t source_slot;
            qa_q3_wire_visibility visibility;
            if (!qa_q3_source_actor_slot(game, candidate, &source_slot, error) ||
                !qa_q3_wire_entity_read(game, source_slot, &entity, &visibility, error)) return false;
            if (!(collision.contents & INT32_C(0x40000000))) continue;
        } else if (collision.role != QA_COLLISION_TRIGGER && collision.role != QA_COLLISION_BOTH) continue;
        qa_q3_client_session session;
        if (!qa_q3_client_session_read(game, actor, &session, error)) return false;
        if (session.team == 3 && (!native || (entity.eType != 9 && !door))) continue;
        bool contact;
        if (native && entity.eType == 2) {
            if (!qa_q3_wire_player_read(game, slot, &player, error)) return false;
            qa_vec3 current = qa_v3(player.origin[0], player.origin[1], player.origin[2]);
            if (!touches_item(current, &entity.pos, now, &contact, error)) return false;
        } else if (!contact_actor(app, bounds, candidate, &contact, error)) return false;
        if (contact && (!touch(provider, candidate, actor, error) ||
            !bot_touch(provider, actor, candidate, error))) return false;
    }
    return !live(app, actor) || qa_q3_client_jumppad_finish(game, actor, error);
}

static bool selected_command(native_q3_think_call *call,
    qa_movement_command *out, qa_error *error)
{
    qa_application *app = call->provider->application;
    application_control_record *record = &app->controls[call->actor.slot];
    const qa_q3_usercmd *raw = &call->movement;
    *out = (qa_movement_command){.kind = record->state.kind,
        .sequence = record->command_sequence,
        .milliseconds = call->movement_milliseconds,
        .server_time_ms = raw->serverTime, .buttons = (uint32_t)raw->buttons,
        .weapon = raw->weapon};
    if (out->kind == QA_MOVEMENT_Q3) {
        memcpy(out->angle_words, raw->angles, sizeof(out->angle_words));
        out->forward_move = raw->forwardmove; out->side_move = raw->rightmove;
        out->up_move = raw->upmove;
        return true;
    }
    uint32_t slot;
    qa_q3_player source;
    if (!qa_q3_native_client_slot(call->provider->state.q3, call->actor, &slot, error) ||
        !qa_q3_wire_player_read(call->provider->state.q3, slot, &source, error)) return false;
    out->angles = qa_v3(
        (float)(((double)raw->angles[0] + source.deltaAngles[0]) * 360 / 65536),
        (float)(((double)raw->angles[1] + source.deltaAngles[1]) * 360 / 65536),
        (float)(((double)raw->angles[2] + source.deltaAngles[2]) * 360 / 65536));
    double scale = out->kind == QA_MOVEMENT_NETQUAKE || out->kind == QA_MOVEMENT_QUAKEWORLD ? 320 : 200;
    out->forward_move = (float)(raw->forwardmove * scale / 127);
    out->side_move = (float)(raw->rightmove * scale / 127);
    out->up_move = (float)(raw->upmove * scale / 127);
    out->buttons &= 1u;
    if (out->kind == QA_MOVEMENT_NETQUAKE)
        out->acknowledged_server_seconds = (double)raw->serverTime / 1000;
    if (out->kind == QA_MOVEMENT_Q2_CLASSIC) {
        for (size_t i = 0; i < 3; ++i)
            out->angle_words[i] = (uint16_t)((uint32_t)raw->angles[i] + (uint32_t)source.deltaAngles[i] -
                (uint32_t)record->state.data.q2.delta_angle_shorts[i]);
    } else if (out->kind == QA_MOVEMENT_Q2_RERELEASE) {
        qa_vec3 delta = record->state.data.q2r.delta_angles;
        out->angles = qa_v3(
            (float)(((double)raw->angles[0] + source.deltaAngles[0]) * 360 / 65536 - delta.x),
            (float)(((double)raw->angles[1] + source.deltaAngles[1]) * 360 / 65536 - delta.y),
            (float)(((double)raw->angles[2] + source.deltaAngles[2]) * 360 / 65536 - delta.z));
        if (raw->upmove > 0) out->buttons |= 8u;
        if (raw->upmove < 0) out->buttons |= 16u;
        out->up_move = 0;
    } else if (out->kind == QA_MOVEMENT_NETQUAKE && raw->upmove > 0) out->buttons |= 2u;
    return true;
}

static bool movement_command(native_q3_think_call *call, bool spectator, qa_error *error)
{
    application_provider *provider = call->provider;
    call->movement = call->accepted;
    if (spectator || !provider->product || strcmp(provider->product->campaign, "missionpack")) return true;
    qa_q3_source_match_state match;
    int32_t single_player, now;
    if (!qa_q3_source_match_state_read(provider->state.q3, &match, error) ||
        !application_native_q3_settings_integer(provider, "ui_singlePlayerActive", &single_player, error) ||
        !qa_q3_source_clock(provider->state.q3, &now, error)) return false;
    if (!match.intermission_queued_ms || !single_player) return true;
    int32_t elapsed = signed_word((uint32_t)now - (uint32_t)match.intermission_queued_ms);
    if (elapsed < 1000) return true;
    call->movement.buttons = 0;
    call->movement.forwardmove = call->movement.rightmove = call->movement.upmove = 0;
    if (elapsed >= 2000 && elapsed <= 2500) {
        qa_console *console;
        qa_command_context context = {.owner = provider->owner, .dialect = QA_CONSOLE_Q3,
            .origin = QA_COMMAND_SERVER};
        if (!application_native_q3_console_at(provider, &console, NULL, NULL))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q3 intermission has no source console");
        if (!application_unified_q3_console(provider, false, "centerview\n", error) ||
            !qa_console_append(console, &context, "centerview\n", error)) return false;
    }
    qa_q3_wire_policy policy = {.pm_type = 6};
    return qa_q3_wire_player_policy_update(provider->state.q3, call->actor,
        QA_Q3_WIRE_PM_TYPE, &policy, error);
}

static bool client_think(void *opaque, qa_session *session,
    const qa_source_command *admission, qa_error *error)
{
    native_q3_think_call *call = opaque;
    application_provider *provider = call->provider;
    qa_application *app = provider->application;
    if (session != app->session || admission->provider != provider->owner ||
        !qa_actor_id_equal(admission->actor, call->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 think lost its actual source admission");
    bool handled;
    if (!application_native_q3_client_think_special(provider, call->actor,
        &call->accepted, call->milliseconds, &handled, error)) return false;
    if (handled || !live(app, call->actor)) return true;
    qa_q3_client_session source_session;
    if (!qa_q3_client_session_read(provider->state.q3, call->actor, &source_session, error)) return false;
    bool spectator = source_session.team == 3;
    if (!spectator) {
        bool allowed;
        if (!inactivity(provider, call->actor, &allowed, error)) return false;
        if (!allowed || !live(app, call->actor)) return true;
        if (!qa_q3_client_reward_expire(provider->state.q3, call->actor, error)) return false;
    }
    application_control_record *record = &app->controls[call->actor.slot];
    int32_t type, gravity, speed; bool projected_spectator;
    if (!application_native_q3_client_movement_parameters(provider, call->actor,
        &type, &gravity, &speed, &projected_spectator, error)) return false;
    if (record->state.kind == QA_MOVEMENT_Q3) {
        record->state.data.q3.movement_type = type;
        record->state.data.q3.gravity = gravity;
        record->state.data.q3.speed = speed;
    }
    uint32_t old_sequence = 0;
    if (!spectator && !qa_q3_client_think_prepare(provider->state.q3, call->actor,
        &call->accepted, &old_sequence, &call->accepted, error)) return false;
    if (!live(app, call->actor)) return true;
    if (!movement_command(call, spectator, error)) return false;
    {
        qa_movement_command selected;
        if (!selected_command(call, &selected, error) ||
            !application_control_frames_q3_move(app, admission, &selected,
                (call->movement.buttons & 4) != 0, error)) return false;
        if (!live(app, call->actor)) return true;
        const qa_movement_result *result = application_control_q3_result(provider, call->actor, error);
        if (!result) return false;
        int32_t water_level = result->water_level;
        int32_t water_type = result->water_type < 0
            ? result->water_type == -3 ? 32 : result->water_type == -4 ? 16 : result->water_type == -5 ? 8 : 0
            : result->water_type;
        qa_bounds bounds = result->bounds;
        int32_t completed_time = result->state.kind == QA_MOVEMENT_Q3
            ? result->state.data.q3.command_time_ms : call->accepted.serverTime;
        if (!qa_q3_client_movement_complete(provider->state.q3, call->actor,
            completed_time, result->view_angles, result->view_height,
            result->ground, error)) return false;
        if (spectator) {
            if (!qa_q3_client_spectator_origin(provider->state.q3, call->actor, error) ||
                !touch_triggers(provider, call->actor, error)) return false;
            if (live(app, call->actor) && !qa_world_unlink(app->world, call->actor, error)) return false;
        } else {
            qa_actor_id storage[32];
            qa_actor_id *contacts = NULL; size_t count = 0;
            if (!snapshot_contacts(provider, result, storage,
                sizeof(storage) / sizeof(*storage), &contacts, &count, error)) return false;
            bool ok = qa_q3_client_think_event_time(provider->state.q3, call->actor, old_sequence, error);
            int32_t smooth;
            if (ok) ok = application_native_q3_settings_integer(provider, "g_smoothClients", &smooth, error);
            if (ok) ok = qa_q3_wire_player_publish(provider->state.q3, call->actor, true,
                smooth != 0, completed_time, error);
            if (ok) ok = qa_q3_wire_player_pending(provider->state.q3, call->actor, error);
            if (ok && live(app, call->actor))
                ok = qa_q3_client_fire_held_finish(provider->state.q3, call->actor, error);
            if (ok && live(app, call->actor))
                ok = movement_bounds(provider, call->actor, bounds, error);
            if (ok && live(app, call->actor))
                ok = qa_q3_client_movement_water(provider->state.q3, call->actor,
                    water_level, water_type, error);
            bool origin_scope = false;
            if (ok && live(app, call->actor)) {
                qa_q3_wire_player_publication published;
                ok = qa_q3_wire_player_publication_read(provider->state.q3, call->actor, &published, error);
                if (ok) {
                    ok = qa_q3_client_current_origin(provider->state.q3, call->actor, published.position, error);
                    origin_scope = ok;
                    if (ok) ok = qa_q3_client_events(provider->state.q3, call->actor, old_sequence, error);
                }
            }
            if (ok && live(app, call->actor)) ok = qa_q3_wire_link(provider->state.q3, call->actor, NULL, error);
            qa_q3_player_state player;
            if (ok && live(app, call->actor)) {
                ok = qa_q3_player_read(provider->state.q3, call->actor, &player);
                if (!ok) application_fail(error, QA_ERROR_NOT_FOUND, "Native Q3 trigger turn lost its source player");
                if (ok && !player.noclip) ok = touch_triggers(provider, call->actor, error);
            }
            if (origin_scope && live(app, call->actor)) {
                qa_error cleanup = {0};
                bool restored = qa_q3_client_current_origin_finish(provider->state.q3, call->actor, &cleanup);
                if (!restored) { if (ok && error) *error = cleanup; ok = false; }
            }
            if (ok && live(app, call->actor)) {
                qa_vec3 origin;
                ok = qa_q3_source_current_origin_read(provider->state.q3, call->actor, &origin, error) &&
                    application_bots_test_aas(provider, origin, error);
            }
            if (ok && live(app, call->actor)) ok = impacts(provider, call->actor, contacts, count, error);
            if (contacts != storage) free(contacts);
            if (ok && live(app, call->actor))
                ok = qa_q3_client_think_event_time(provider->state.q3, call->actor, old_sequence, error);
            if (!ok || !live(app, call->actor)) return ok;
        }
    }
    if (spectator)
        return application_native_q3_client_spectator_buttons(provider, call->actor, &call->accepted, error);
    return qa_q3_client_think_finish(provider->state.q3, call->actor, call->milliseconds, error);
}

bool application_control_q3_client_think(application_provider *provider, qa_actor_id actor,
    const qa_q3_usercmd *received, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    uint32_t slot;
    if (!app || !received || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->constructed ||
        !provider->attached || provider->close_pending || !provider->state.q3 || app->destroy_requested ||
        (application_world_provider(app, QA_ROLE_ENTITIES, "") != provider &&
         !application_native_q3_source_command_actor_current(provider,actor)) ||
        (app->state != QA_APPLICATION_RUNNING &&
         !(app->state == QA_APPLICATION_READY && app->operation == APPLICATION_CONFIGURING)) ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING &&
         app->operation != APPLICATION_CONFIGURING) || actor.slot >= app->control_capacity || !live(app, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 think requires its current source client");
    if (!qa_q3_native_client_slot(provider->state.q3, actor, &slot, error)) return false;
    application_control_record *record = &app->controls[actor.slot];
    if (!record->active || record->retired || record->moving || !qa_actor_id_equal(record->actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 think requires its actual idle selected control");
    if (!application_native_q3_console_borrow(provider, error)) return false;
    application_operation previous = app->operation;
    if (previous == APPLICATION_IDLE) app->operation = APPLICATION_ADVANCING;
    native_q3_think_call call = {.provider = provider, .actor = actor};
    bool run = false;
    bool ok = application_native_q3_client_think_policy(provider, actor, received,
        &call.accepted, &call.milliseconds, &run, error);
    if (ok && run && live(app, actor)) {
        int32_t command_time;
        ok = qa_q3_client_command_time(provider->state.q3, actor, &command_time, error);
        if (ok) {
            int64_t movement = (int64_t)call.accepted.serverTime - command_time;
            call.movement_milliseconds = movement <= 0 ? 0u : movement > 200 ? 200u : (uint32_t)movement;
            uint64_t elapsed = (uint64_t)call.movement_milliseconds * UINT64_C(1000000);
            qa_source_command active;
            if (qa_session_active_command(app->session, provider->owner, &active) &&
                active.provider == provider->owner && active.kind == QA_CLOCK_Q3 &&
                active.phase == QA_CLIENT_COMMAND && qa_actor_id_equal(active.actor, actor))
                ok = client_think(&call, app->session, &active, error);
            else
                ok = qa_session_command_call(app->session, provider->owner, actor, elapsed,
                    client_think, &call, error);
        }
    }
    app->operation = previous;
    application_native_q3_console_release(provider);
    return ok;
}
