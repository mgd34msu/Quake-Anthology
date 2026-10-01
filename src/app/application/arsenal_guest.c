#include "guest_input_private.h"
#include "control_frame.h"
#include "client_outputs.h"
#include "guest_q3_control.h"
#include "guest_projection_private.h"
#include "qa/q3_abi.h"
#include "qa/qvm_save.h"
#include "qa/source_save.h"
#include <math.h>

typedef struct guest_client_scope {
    struct guest_client_scope *previous;
    const qa_qvm_call *call;
    qa_actor_id actor;
    qa_q3_host_game_data data;
    uint32_t slot, player, movement, milliseconds;
    bool spawning, input_active;
} guest_client_scope;

typedef struct guest_weapon_branch_context {
    struct application_guest_input *input;
    bool unselected;
} guest_weapon_branch_context;

typedef struct application_guest_input {
    q3g_role *role;
    application_guest_input_profile profile;
    qa_qvm_binding bindings[6];
    size_t binding_count;
    application_guest_q3_control *body_control;
    qa_qvm_binding body_binding;
    guest_client_scope *scope;
    qa_actor_id applying;
    qa_actor_id command_actor;
    const qa_movement_command *command;
    const application_control_external_stage *stage;
    qa_movement_command applied_command;
    qa_q3_usercmd projected_command;
    bool command_projected;
    bool input_applied;
    qa_qvm_branch_binding *weapon_bindings;
    guest_weapon_branch_context *weapon_contexts;
    bool in_command;
} application_guest_input;

static bool cancel_client(const qa_qvm_call *call, const qa_qvm_call *client, qa_error *error)
{
    bool cancelled;
    return qa_qvm_call_cancelled(call, &cancelled, error) &&
        (cancelled || qa_qvm_cancel(client, error));
}

static bool record_current(application_guest_input *input, const guest_client_scope *scope)
{
    qa_actor_id actor; qa_q3_host_game_data data;
    if (!(qa_actors_get(qa_session_actors(input->role->engine->provider->application->session), scope->actor) &&
        qa_q3_host_actor(input->role->host, scope->slot, false, &actor, NULL) &&
        qa_actor_id_equal(actor, scope->actor) && qa_q3_host_game_data_read(input->role->host, &data) &&
        data.entities_address == scope->data.entities_address && data.clients_address == scope->data.clients_address &&
        data.entity_stride == scope->data.entity_stride && data.client_stride == scope->data.client_stride &&
        scope->slot < data.entity_count && scope->slot < data.client_count)) return false;
    uint8_t bytes[4];
    uint64_t address = data.entities_address + (uint64_t)scope->slot * data.entity_stride + input->profile.client_pointer;
    if (address > UINT32_MAX || !qa_qvm_read(input->role->vm, (uint32_t)address, bytes, sizeof(bytes), NULL) ||
        qa_load_u32le(bytes) != scope->player) return false;
    return !scope->movement || (qa_qvm_read(input->role->vm, scope->movement, bytes, sizeof(bytes), NULL) &&
        qa_load_u32le(bytes) == scope->player);
}

static bool current(application_guest_input *input, const guest_client_scope *scope, const qa_qvm_call *call)
{
    bool cancelled;
    return qa_qvm_call_cancelled(call, &cancelled, NULL) && !cancelled && record_current(input, scope);
}

static bool source_word(application_guest_input *input, uint32_t address,
                         uint32_t *value, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_qvm_read(input->role->vm, address, bytes, sizeof(bytes), error)) return false;
    *value = qa_load_u32le(bytes); return true;
}

static bool slot_player(application_guest_input *input, uint32_t slot,
                         guest_client_scope *scope, bool *present, qa_error *error)
{
    *present = false;
    qa_q3_host_game_data data;
    if (!qa_q3_host_game_data_read(input->role->host, &data) || slot >= data.client_count)
        return true;
    if (data.entity_stride != input->profile.entity_stride ||
        data.client_stride != input->profile.client_stride)
        return application_fail(error, QA_ERROR_FORMAT, "Guest input records changed their qualified layout");
    uint64_t entity = data.entities_address + (uint64_t)slot * data.entity_stride;
    uint64_t player = data.clients_address + (uint64_t)slot * data.client_stride;
    if (entity > UINT32_MAX - input->profile.client_pointer || player > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Guest input pointer leaves QVM memory");
    uint32_t pointer;
    if (!source_word(input, (uint32_t)entity + input->profile.client_pointer, &pointer, error)) return false;
    if (pointer != player)
        return application_fail(error, QA_ERROR_FORMAT, "Guest client does not own its located player record");
    qa_actor_id actor;
    if (!qa_q3_host_actor(input->role->host, slot, false, &actor, error)) return false;
    if (!actor.registry) return true;
    scope->slot = slot; scope->player = pointer; scope->actor = actor; scope->data = data;
    *present = true; return true;
}

static bool envelope(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_guest_input *input = context;
    int32_t argument;
    if (!qa_qvm_call_argument(call, 0, &argument, error)) return false;
    uint32_t slot;
    bool spawning = call->instruction == input->profile.client_spawn;
    if (call->instruction == input->profile.client_think) {
        if (argument < 0) return qa_qvm_proceed(call, result, error);
        slot = (uint32_t)argument;
    } else {
        qa_q3_host_game_data data;
        uint64_t pointer = (uint32_t)argument;
        if (!qa_q3_host_game_data_read(input->role->host, &data) ||
            !data.entity_stride || pointer < data.entities_address ||
            (pointer - data.entities_address) % data.entity_stride)
            return qa_qvm_proceed(call, result, error);
        uint64_t number = (pointer - data.entities_address) / data.entity_stride;
        if (number > UINT32_MAX) return qa_qvm_proceed(call, result, error);
        slot = (uint32_t)number;
    }
    guest_client_scope scope = {.previous = input->scope, .call = call, .spawning = spawning};
    bool present;
    if (!slot_player(input, slot, &scope, &present, error)) return false;
    if (!present) return qa_qvm_proceed(call, result, error);
    input->scope = &scope;
    bool ok = qa_qvm_proceed(call, result, error);
    if (ok && !current(input, &scope, call)) ok = cancel_client(call, call, error);
    input->scope = scope.previous;
    return ok;
}

static bool weapon_branch(void *context, const qa_qvm_call *call, bool original,
                           bool *taken, qa_error *error)
{
    guest_weapon_branch_context *branch = context;
    application_guest_input *input = branch->input;
    guest_client_scope *scope = input->scope;
    if (!scope || scope->spawning) { *taken = original; return true; }
    if (!current(input, scope, call)) {
        *taken = original; return cancel_client(call, scope->call, error);
    }
    qa_application *app = input->role->engine->provider->application;
    if (application_provider_for(app, scope->actor, QA_ROLE_ARSENAL, "") == input->role->engine->provider) {
        *taken = original; return true;
    }
    (void)call;
    *taken = branch->unselected; return true;
}

static bool weapon_stage(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_guest_input *input = context;
    if (!input->scope || input->scope->spawning) return qa_qvm_proceed(call, result, error);
    uint32_t player;
    if (input->profile.weapon_pointer_global) {
        if (!source_word(input, input->profile.weapon_pointer_base, &player, error)) return false;
    } else {
        int32_t argument;
        if (!qa_qvm_call_argument(call, input->profile.weapon_pointer_base, &argument, error)) return false;
        player = (uint32_t)argument;
    }
    for (size_t i = 0; i < input->profile.weapon_indirection_count; ++i) {
        uint32_t offset = input->profile.weapon_indirections[i];
        if (player > UINT32_MAX - offset)
            return application_fail(error, QA_ERROR_FORMAT, "Guest weapon pointer path overflowed");
        if (!source_word(input, player + offset, &player, error)) return false;
    }
    if (player > UINT32_MAX - input->profile.weapon_pointer_offset)
        return application_fail(error, QA_ERROR_FORMAT, "Guest weapon actor pointer overflowed");
    player += input->profile.weapon_pointer_offset;
    if (player != input->scope->player) return qa_qvm_proceed(call, result, error);
    bool ok = qa_qvm_bind_branches(call, input->weapon_bindings,
                                    input->profile.weapon_branch_count, error);
    return ok && qa_qvm_proceed(call, result, error);
}

static int32_t angle_word(float angle)
{
    return (int32_t)(uint16_t)(int32_t)(fmod((double)angle, 360.0) * 65536.0 / 360.0);
}

static float move_scale(qa_movement_kind kind)
{
    return kind == QA_MOVEMENT_Q3 ? 127.0f
           : kind == QA_MOVEMENT_NETQUAKE || kind == QA_MOVEMENT_QUAKEWORLD ? 320.0f : 200.0f;
}

static int8_t axis(float value)
{
    if (value > 127) return 127;
    if (value < -127) return -127;
    return (int8_t)lrintf(value);
}

static int8_t source_axis(float value)
{
    if (value > 127) return 127;
    if (value < -128) return -128;
    return (int8_t)lrintf(value);
}

static bool replace_locomotion(void *context, const qa_qvm_call *call, bool *skip, qa_error *error)
{
    application_guest_input *input = context;
    guest_client_scope *scope = input->scope;
    *skip = false;
    if (!scope || scope->spawning) return true;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    qa_application *app = input->role->engine->provider->application;
    if (application_provider_for(app, scope->actor, QA_ROLE_MOVEMENT, "") == input->role->engine->provider)
        return true;
    qa_q3_usercmd source;
    qa_q3_player player;
    if (!qa_qvm_read_usercmd(input->role->vm, (int32_t)(scope->movement + 4), &source, error) ||
        !qa_qvm_read_player(input->role->vm, (int32_t)scope->player, true, &player, error)) return false;
    qa_application_control_view control;
    if (!qa_application_control_read(app, scope->actor, &control))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Guest movement has no shared continuation");
    uint32_t remaining = scope->milliseconds;
    double scale = move_scale(control.state.kind);
    qa_movement_command command = {.kind = control.state.kind,
        .sequence = input->command ? input->command->sequence : control.command_sequence + 1,
        .milliseconds = remaining, .server_time_ms = source.serverTime,
        .angles = {player.viewangles[0], player.viewangles[1], player.viewangles[2]},
        .forward_move = (float)((double)source.forwardmove * scale / 127.0),
        .side_move = (float)((double)source.rightmove * scale / 127.0),
        .up_move = (float)((double)source.upmove * scale / 127.0), .buttons = (uint32_t)source.buttons & 1u,
        .weapon = input->command ? input->command->weapon : source.weapon,
        .impulse = input->command && input->input_applied ? input->applied_command.impulse
                 : input->command ? input->command->impulse : 0};
    const float aim[] = {command.angles.x, command.angles.y, command.angles.z};
    if (command.kind == QA_MOVEMENT_Q3) {
        command.buttons = (uint32_t)source.buttons;
        command.forward_move = source.forwardmove; command.side_move = source.rightmove;
        command.up_move = source.upmove;
        for (size_t i = 0; i < 3; ++i)
            command.angle_words[i] = (uint16_t)(angle_word(aim[i]) -
                                               control.state.data.q3.delta_angle_words[i]);
    } else if (command.kind == QA_MOVEMENT_Q2_CLASSIC) {
        for (size_t i = 0; i < 3; ++i)
            command.angle_words[i] = (uint16_t)(angle_word(aim[i]) -
                                               control.state.data.q2.delta_angle_shorts[i]);
    } else if (command.kind == QA_MOVEMENT_Q2_RERELEASE) {
        command.angles = qa_vec_sub(command.angles, control.state.data.q2r.delta_angles);
        if (source.upmove > 0) command.buttons |= 8u;
        if (source.upmove < 0) command.buttons |= 16u;
        command.up_move = 0;
    } else if (command.kind == QA_MOVEMENT_NETQUAKE && source.upmove > 0) command.buttons |= 2u;
    if ((command.kind == QA_MOVEMENT_NETQUAKE || command.kind == QA_MOVEMENT_QUAKEWORLD) &&
        input->command && input->input_applied && input->command_projected &&
        input->applied_command.kind == command.kind && source.upmove == input->projected_command.upmove) {
        /* Q3 carries jump and upward movement in one source byte. Retain the
         * independently consumed Q1 controls when reading our own projection. */
        command.up_move = input->applied_command.up_move;
        command.buttons = (command.buttons & ~2u) | (input->applied_command.buttons & 2u);
    }
    qa_actor_id previous = input->applying; input->applying = scope->actor;
    qa_movement_command applied = command;
    bool ok = input->stage ? input->stage->current(input->stage) &&
        input->stage->locomotion(input->stage, &command, &applied, error) :
        application_control_frames_apply_nested(app, scope->actor, &command, &applied, error);
    input->applying = previous;
    if (!ok) return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    if (input->command) { input->applied_command = applied; input->input_applied = true; }
    qa_q3_usercmd updated = source;
    updated.buttons = command.kind == QA_MOVEMENT_Q3 ? (int32_t)applied.buttons
        : (int32_t)(((uint32_t)source.buttons & ~1u) | (applied.buttons & 1u));
    float source_scale = 127.0f / move_scale(applied.kind);
    updated.forwardmove = source_axis(applied.forward_move * source_scale);
    updated.rightmove = source_axis(applied.side_move * source_scale);
    updated.upmove = source_axis(applied.up_move * source_scale);
    if (applied.kind == QA_MOVEMENT_Q2_RERELEASE) {
        if (applied.buttons & 8u) updated.upmove = 127;
        else if (applied.buttons & 16u) updated.upmove = -127;
    } else if (applied.kind == QA_MOVEMENT_NETQUAKE || applied.kind == QA_MOVEMENT_QUAKEWORLD) {
        if (applied.buttons & 2u) updated.upmove = 127;
        else if (updated.upmove > 0) updated.upmove = 0;
    }
    qa_vec3 applied_aim = applied.angles;
    if (applied.kind == QA_MOVEMENT_Q2_RERELEASE)
        applied_aim = qa_vec_add(applied_aim, control.state.data.q2r.delta_angles);
    if (applied.kind == QA_MOVEMENT_Q2_CLASSIC || applied.kind == QA_MOVEMENT_Q3) {
        float decoded[3];
        for (size_t i = 0; i < 3; ++i) {
            int32_t delta = applied.kind == QA_MOVEMENT_Q3 ? control.state.data.q3.delta_angle_words[i]
                                                          : control.state.data.q2.delta_angle_shorts[i];
            decoded[i] = (float)(uint16_t)((uint32_t)applied.angle_words[i] + (uint32_t)delta) *
                         (360.0f / 65536.0f);
        }
        applied_aim = qa_v3(decoded[0], decoded[1], decoded[2]);
    }
    const float applied_angles[] = {applied_aim.x, applied_aim.y, applied_aim.z};
    bool aim_changed = applied.angles.x != command.angles.x || applied.angles.y != command.angles.y ||
        applied.angles.z != command.angles.z || memcmp(applied.angle_words, command.angle_words, sizeof(command.angle_words));
    if (aim_changed) for (size_t i = 0; i < 3; ++i) {
        uint32_t bits = (uint32_t)angle_word(applied_angles[i]) - (uint32_t)player.deltaAngles[i];
        memcpy(&updated.angles[i], &bits, sizeof(bits));
    }
    if (memcmp(&updated, &source, sizeof(source)) != 0 &&
        !qa_qvm_write_usercmd(input->role->vm, (int32_t)(scope->movement + 4), true, &updated, error))
        return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    if (input->command) { input->projected_command = updated; input->command_projected = true; }
    qa_body_state body;
    if (!qa_world_body_read(app->world, scope->actor, &body, error) ||
        !qa_application_control_read(app, scope->actor, &control)) return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    uint32_t ground = body.ground.registry ? 1022u : 1023u;
    if (body.ground.registry && !qa_actor_id_equal(body.ground, app->physics->world_actor) &&
        !qa_q3_host_actor_slot(input->role->host, body.ground, &ground, error)) return false;
    const float motion[] = {body.origin.x, body.origin.y, body.origin.z,
                            body.velocity.x, body.velocity.y, body.velocity.z,
                            control.view_angles.x, control.view_angles.y, control.view_angles.z};
    for (size_t i = 0; i < sizeof(motion) / sizeof(motion[0]); ++i) {
        uint32_t bits; memcpy(&bits, &motion[i], sizeof(bits)); uint8_t bytes[4]; qa_store_u32le(bytes, bits);
        uint32_t offset = i < 6 ? 20u + (uint32_t)i * 4 : 152u + (uint32_t)(i - 6) * 4;
        if (!qa_qvm_write(input->role->vm, scope->player + offset, (qa_bytes){bytes, 4}, error)) return false;
        if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    }
    uint8_t word[4]; qa_store_u32le(word, ground);
    if (!qa_qvm_write(input->role->vm, scope->player + 68, (qa_bytes){word, 4}, error)) return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    if (!isfinite(control.view_height) || control.view_height < (float)INT32_MIN ||
        (double)control.view_height > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Guest view height exceeds its source word");
    qa_store_u32le(word, (uint32_t)(int32_t)control.view_height);
    if (!qa_qvm_write(input->role->vm, scope->player + 164, (qa_bytes){word, 4}, error)) return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    const float bounds[] = {body.bounds.mins.x, body.bounds.mins.y, body.bounds.mins.z,
                            body.bounds.maxs.x, body.bounds.maxs.y, body.bounds.maxs.z};
    for (size_t i = 0; i < 6; ++i) {
        uint32_t bits; memcpy(&bits, &bounds[i], sizeof(bits)); uint8_t bytes[4]; qa_store_u32le(bytes, bits);
        uint32_t offset = i < 3 ? input->profile.movement_mins + (uint32_t)i * 4
                               : input->profile.movement_maxs + (uint32_t)(i - 3) * 4;
        if (!qa_qvm_write(input->role->vm, scope->movement + offset, (qa_bytes){bytes, 4}, error)) return false;
        if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    }
    uint8_t water[8]; qa_store_u32le(water, (uint32_t)control.water_level);
    qa_store_u32le(water + 4, (uint32_t)control.water_type);
    if (!qa_qvm_write(input->role->vm, scope->movement + input->profile.movement_water,
                       (qa_bytes){water, sizeof(water)}, error)) return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    *skip = true; return true;
}

static qa_vec3 source_view_angles(const application_guest_input *input,
    const qa_q3_usercmd *command, const qa_q3_player *player)
{
    qa_vec3 previous = qa_v3(player->viewangles[0], player->viewangles[1], player->viewangles[2]);
    for (size_t i = 0; i < input->profile.intermission_count; ++i)
        if (player->pmType == input->profile.intermission_modes[i]) return previous;
    if (player->pmType != 2 && player->stats[0] <= 0) return previous;
    float angles[3];
    for (size_t i = 0; i < 3; ++i) {
        uint32_t word = ((uint32_t)command->angles[i] + (uint32_t)player->deltaAngles[i]) & UINT32_C(65535);
        int32_t signed_word = word >= UINT32_C(32768) ? (int32_t)word - 65536 : (int32_t)word;
        if (i == 0) {
            if (signed_word > 16000) signed_word = 16000;
            else if (signed_word < -16000) signed_word = -16000;
        }
        angles[i] = (float)((double)signed_word * (360.0 / 65536.0));
    }
    return qa_v3(angles[0], angles[1], angles[2]);
}

static bool source_input(application_guest_input *input, guest_client_scope *scope, const qa_qvm_call *call,
                           application_source_input_scope *input_scope,
                           bool before, bool slice, uint32_t milliseconds, qa_error *error)
{
    qa_application *app = input->role->engine->provider->application;
    qa_q3_usercmd source;
    qa_q3_player player;
    if (!qa_qvm_read_usercmd(input->role->vm, (int32_t)(scope->movement + 4), &source, error) ||
        !qa_qvm_read_player(input->role->vm, (int32_t)scope->player, true, &player, error)) return false;
    qa_application_control_view control;
    if (!qa_application_control_read(app, scope->actor, &control))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Guest source input lost its continuation");
    qa_movement_state state = qa_movement_state_default(QA_MOVEMENT_Q3,
        qa_v3(player.origin[0], player.origin[1], player.origin[2]));
    memcpy(state.data.q3.delta_angle_words, player.deltaAngles, sizeof(player.deltaAngles));
    qa_movement_command command = {.kind = QA_MOVEMENT_Q3,
        .sequence = input->command ? input->command->sequence : control.command_sequence + 1,
        .milliseconds = milliseconds, .server_time_ms = source.serverTime,
        .angles = {player.viewangles[0], player.viewangles[1], player.viewangles[2]},
        .forward_move = source.forwardmove, .side_move = source.rightmove, .up_move = source.upmove,
        .buttons = (uint32_t)source.buttons, .weapon = source.weapon,
        .impulse = input->command && input->input_applied ? input->applied_command.impulse
                 : input->command ? input->command->impulse : 0};
    memcpy(command.angle_words, source.angles, sizeof(command.angle_words));
    qa_vec3 absolute_aim = source_view_angles(input, &source, &player);
    bool applied = input->stage ? input->stage->current(input->stage) &&
        input->stage->input(input->stage, &state, &command, &absolute_aim, input_scope,
            before, slice, (uint64_t)milliseconds * UINT64_C(1000000), error) :
        application_control_source_input(app, scope->actor, &state, &command, &absolute_aim, input_scope,
            before, slice, (uint64_t)milliseconds * UINT64_C(1000000), error);
    if (!applied) return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    qa_q3_usercmd updated = source;
    memcpy(updated.angles, command.angle_words, sizeof(updated.angles));
    updated.buttons = (int32_t)command.buttons; updated.weapon = command.weapon;
    updated.forwardmove = source_axis(command.forward_move);
    updated.rightmove = source_axis(command.side_move); updated.upmove = source_axis(command.up_move);
    if (before && slice) {
        application_client_outputs outputs;
        if (!application_control_outputs(app, scope->actor, &outputs, error)) return false;
        if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
        if (outputs.has_stance) updated.upmove = outputs.crouched ?
            (int8_t)-fmax(1, fabs((double)updated.upmove)) : updated.upmove < 0 ? 0 : updated.upmove;
    }
    if (memcmp(&updated, &source, sizeof(source)) != 0 &&
        !qa_qvm_write_usercmd(input->role->vm, (int32_t)(scope->movement + 4), true, &updated, error))
        return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    if (input->command) {
        input->applied_command = command;
        input->input_applied = true;
    }
    return true;
}

static bool source_move(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_guest_input *input = context;
    guest_client_scope *scope = input->scope;
    if (!scope || scope->spawning) return qa_qvm_proceed(call, result, error);
    int32_t movement; uint32_t player;
    if (!qa_qvm_call_argument(call, 0, &movement, error) ||
        !source_word(input, (uint32_t)movement, &player, error)) return false;
    if (player != scope->player) return qa_qvm_proceed(call, result, error);
    uint32_t previous = scope->movement, previous_ms = scope->milliseconds;
    bool previous_active = scope->input_active;
    scope->movement = (uint32_t)movement;
    uint64_t end = (uint64_t)scope->movement + 28;
    const uint32_t offsets[] = {input->profile.movement_mins, input->profile.movement_maxs,
                               input->profile.movement_water};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        uint64_t limit = (uint64_t)scope->movement + offsets[i] + (i == 2 ? 8u : 12u);
        if (limit > end) end = limit;
    }
    if (end > qa_qvm_memory_size(input->role->vm)) {
        scope->movement = previous;
        return application_fail(error, QA_ERROR_FORMAT, "Guest movement projection leaves source memory");
    }
    bool slice = call->instruction == input->profile.slice;
    qa_q3_usercmd command; qa_q3_player state;
    if (!qa_qvm_read_usercmd(input->role->vm, (int32_t)(scope->movement + 4), &command, error) ||
        !qa_qvm_read_player(input->role->vm, (int32_t)scope->player, true, &state, error)) {
        scope->movement = previous; return false;
    }
    int64_t elapsed = (int64_t)command.serverTime - (int64_t)state.commandTime;
    scope->milliseconds = slice ? elapsed > 200 ? 200 : elapsed < 1 ? 1 : (uint32_t)elapsed
                                : elapsed > 1000 ? 1000 : elapsed < 0 ? 0 : (uint32_t)elapsed;
    if (slice) {
        qa_application *app = input->role->engine->provider->application;
        if (application_provider_for(app, scope->actor, QA_ROLE_MOVEMENT, "") !=
            input->role->engine->provider &&
            (!input->profile.has_modes || state.pmType != input->profile.normal_mode)) {
            scope->movement = previous;
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Mixed guest movement mode requires its own qualified source region");
        }
    }
    application_source_input_scope input_scope = {0};
    application_guest_q3_control_scope body_scope = {0};
    uint32_t before_type = 0, projected_type = 0;
    bool type_projected = false;
    bool ok = source_input(input, scope, call, &input_scope, true, slice, scope->milliseconds, error);
    scope->input_active = ok;
    bool cancelled = false;
    if (ok) ok = qa_qvm_call_cancelled(call, &cancelled, error);
    if (ok && !current(input, scope, call)) {
        qa_error cleanup = {0};
        (void)application_control_source_abort(&input_scope, &cleanup);
        scope->movement = previous; scope->milliseconds = previous_ms; scope->input_active = previous_active;
        return cancel_client(call, scope->call, error);
    }
    if (ok && !cancelled && slice && input->profile.has_locomotion) {
        qa_qvm_region_binding binding = {.entry = input->profile.locomotion_entry,
            .join = input->profile.locomotion_join, .enter = replace_locomotion, .context = input};
        ok = qa_qvm_bind_regions(call, &binding, 1, error);
    }
    qa_application *app = input->role->engine->provider->application;
    if (ok && !cancelled && slice) {
        application_client_outputs outputs;
        ok = application_control_outputs(app, scope->actor, &outputs, error);
        if (ok && !current(input, scope, call)) ok = cancel_client(call, scope->call, error);
        else if (ok && outputs.has_mode) {
            if (!input->profile.has_modes)
                ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Original movement mode has no exact source declaration");
            else {
                projected_type = (uint32_t)(outputs.mode == QA_MOVEMENT_MODE_NORMAL ? input->profile.normal_mode :
                    outputs.mode == QA_MOVEMENT_MODE_NOCLIP ? input->profile.noclip_mode : input->profile.freeze_mode);
                ok = source_word(input, scope->player + 4, &before_type, error);
                uint8_t bytes[4]; qa_store_u32le(bytes, projected_type);
                type_projected = ok;
                if (ok) ok = qa_qvm_write(input->role->vm, scope->player + 4, (qa_bytes){bytes, sizeof(bytes)}, error);
            }
        }
    }
    if (ok) ok = qa_qvm_call_cancelled(call, &cancelled, error);
    if (ok && !cancelled && !slice && application_provider_for(app, scope->actor, QA_ROLE_MOVEMENT, "") ==
        input->role->engine->provider)
        ok = application_guest_q3_control_begin(input->body_control, call, scope->call, scope->actor,
            scope->slot, scope->player, scope->movement, NULL, &body_scope, error);
    if (ok && !cancelled && !body_scope.retired && !body_scope.cancelled) ok = qa_qvm_proceed(call, result, error);
    if (ok) ok = qa_qvm_call_cancelled(call, &cancelled, error);
    qa_error body_cleanup = {0};
    if (!application_guest_q3_control_end(input->body_control, &body_scope, &body_cleanup)) {
        if (ok && error) *error = body_cleanup;
        ok = false;
    }
    if (ok && !cancelled && !body_scope.retired && !body_scope.cancelled && current(input, scope, call))
        ok = source_input(input, scope, call, &input_scope, false, slice, scope->milliseconds, error);
    qa_error cleanup = {0};
    if (!application_control_source_abort(&input_scope, &cleanup)) {
        if (ok && error) *error = cleanup;
        ok = false;
    }
    if (type_projected && record_current(input, scope)) {
        qa_error restore = {0}; uint32_t value;
        bool restored = source_word(input, scope->player + 4, &value, &restore);
        if (restored && value == projected_type) {
            uint8_t bytes[4]; qa_store_u32le(bytes, before_type);
            restored = qa_qvm_write(input->role->vm, scope->player + 4, (qa_bytes){bytes, sizeof(bytes)}, &restore);
        }
        if (!restored) { if (ok && error) *error = restore; ok = false; }
    }
    if (ok && (body_scope.retired || body_scope.cancelled || !current(input, scope, call)))
        ok = cancel_client(call, scope->call, error);
    scope->movement = previous; scope->milliseconds = previous_ms; scope->input_active = previous_active; return ok;
}

bool application_guest_input_attach(q3g_role *role, qa_bytes primary, qa_error *error)
{
    if (!role || role->kind != QA_QVM_GAME || role->input)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest input hooks require an unattached game role");
    application_guest_input *input = calloc(1, sizeof(*input));
    if (!input) return application_fail(error, QA_ERROR_MEMORY, "Allocating guest input owner");
    input->role = role;
    if (!application_guest_input_profile_read(role, primary, &input->profile, error)) { free(input); return false; }
    role->input = input;
    if (!input->profile.input_present) return true;
    size_t branch_count = input->profile.weapon_branch_count;
    if (branch_count) {
        input->weapon_bindings = calloc(branch_count, sizeof(*input->weapon_bindings));
        input->weapon_contexts = calloc(branch_count, sizeof(*input->weapon_contexts));
        if (!input->weapon_bindings || !input->weapon_contexts) {
            application_fail(error, QA_ERROR_MEMORY, "Allocating retained guest weapon predicates");
            qa_error ignored = {0}; (void)application_guest_input_detach(role, &ignored); return false;
        }
        for (size_t i = 0; i < branch_count; ++i) {
            input->weapon_contexts[i] = (guest_weapon_branch_context){input,
                input->profile.weapon_branches[i].unselected};
            input->weapon_bindings[i] = (qa_qvm_branch_binding){
                input->profile.weapon_branches[i].instruction, weapon_branch,
                &input->weapon_contexts[i]};
        }
    }
    const uint32_t entries[] = {input->profile.client_think, input->profile.run_client,
        input->profile.client_spawn, input->profile.move, input->profile.slice, input->profile.weapon_dispatcher};
    size_t count = input->profile.has_weapons ? 6 : 5;
    for (size_t i = 0; i < count; ++i) {
        qa_qvm_function_hook hook = i < 3 ? envelope : i < 5 ? source_move : weapon_stage;
        if (!qa_qvm_bind_function(role->vm, entries[i], true, hook, input,
                                   &input->bindings[input->binding_count], error)) {
            qa_error ignored = {0}; (void)application_guest_input_detach(role, &ignored); return false;
        }
        ++input->binding_count;
    }
    if (!application_guest_q3_control_attach(role, &input->profile, &input->body_control, error)) {
        qa_error ignored = {0}; (void)application_guest_input_detach(role, &ignored); return false;
    }
    input->body_binding = application_guest_q3_control_binding(input->body_control);
    return true;
}

bool application_guest_input_detach(q3g_role *role, qa_error *error)
{
    application_guest_input *input = role ? role->input : NULL;
    if (!input) return true;
    if (input->scope || input->in_command || (role->vm && qa_qvm_active(role->vm)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest input hooks still own a source invocation");
    if (!application_guest_q3_control_detach(&input->body_control, error)) return false;
    input->body_binding = 0;
    while (input->binding_count) {
        qa_qvm_binding binding = input->bindings[input->binding_count - 1];
        if (!qa_qvm_unbind(role->vm, binding, error)) return false;
        --input->binding_count;
    }
    free(input->weapon_contexts); free(input->weapon_bindings);
    application_guest_input_profile_free(&input->profile); free(input); role->input = NULL;
    return true;
}

bool application_guest_input_descriptors(q3g_role *role,
    qa_qvm_saved_function descriptors[7], size_t *out_count, qa_error *error)
{
    application_guest_input *input = role ? role->input : NULL;
    bool idle = (role && role->vm && !qa_qvm_active(role->vm) &&
        (!input || (!input->scope && !input->in_command && !input->command &&
                    !input->applying.registry && !input->command_actor.registry))) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 input checkpoint requires completed source command scopes");
    if (!idle) return false;
    size_t count = input && input->profile.input_present ? (input->profile.has_weapons ? 6u : 5u) : 0;
    if (input && input->binding_count != count)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 input callback count differs from its source declaration");
    if (input) {
        const uint32_t entries[] = {input->profile.client_think, input->profile.run_client,
            input->profile.client_spawn, input->profile.move, input->profile.slice, input->profile.weapon_dispatcher};
        for (size_t i = 0; i < count; ++i)
            descriptors[i] = (qa_qvm_saved_function){input->bindings[i], entries[i], true,
                i < 3 ? envelope : i < 5 ? source_move : weapon_stage, input};
        for (size_t i = count; i < 6; ++i)
            if (input->bindings[i])
                return application_fail(error, QA_ERROR_FORMAT, "Q3 input retains an undeclared callback identity");
    }
    if (input && input->body_binding) {
        if (input->body_binding != application_guest_q3_control_binding(input->body_control) ||
            !application_guest_q3_control_descriptor(input->body_control, &descriptors[count], error)) return false;
        ++count;
    } else if (input && input->body_control)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 body callback identity differs from its source owner");
    *out_count = count;
    return true;
}

static bool input_checkpoint_idle(q3g_role *role, qa_error *error)
{
    qa_qvm_saved_function descriptors[7] = {0};
    size_t count;
    return application_guest_input_descriptors(role, descriptors, &count, error);
}

static bool input_command_fields(qa_source_save_io *io, qa_movement_command *value)
{
    uint32_t kind = value->kind;
    if (!qa_source_save_u32(io, &kind)) return false;
    if (kind > QA_MOVEMENT_Q3)
        return application_fail(io->error, QA_ERROR_FORMAT, "Q3 input command has an invalid movement dialect");
    bool ok =
        qa_source_save_u64(io, &value->sequence) && qa_source_save_u32(io, &value->milliseconds) &&
        qa_source_save_i32(io, &value->server_time_ms) && qa_source_save_i32(io, &value->server_frame) &&
        qa_source_save_f64(io, &value->acknowledged_server_seconds) && qa_source_save_vec3(io, &value->angles);
    for (size_t i = 0; ok && i < 3; ++i) ok = qa_source_save_i32(io, &value->angle_words[i]);
    ok = ok && qa_source_save_f32(io, &value->forward_move) && qa_source_save_f32(io, &value->side_move) &&
        qa_source_save_f32(io, &value->up_move) && qa_source_save_u32(io, &value->buttons) &&
        qa_source_save_u8(io, &value->impulse) && qa_source_save_u8(io, &value->light_level) &&
        qa_source_save_u8(io, &value->weapon);
    if (ok && io->direction == QA_SOURCE_SAVE_READ) value->kind = (qa_movement_kind)kind;
    return ok;
}

static bool input_record_write(void *context, size_t offset, qa_bytes bytes, qa_error *error)
{
    (void)error;
    memcpy((uint8_t *)context + offset, bytes.data, bytes.size);
    return true;
}

static bool input_fields(qa_source_save_io *io, application_guest_input *input)
{
    uint8_t magic[8] = {'Q','A','G','3','I','N',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','I','N',0,0};
    uint32_t version = 2;
    bool present = input != NULL;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || !qa_source_save_u32(io, &version) ||
        memcmp(magic, expected, sizeof(magic)) || version != 2 ||
        !qa_source_save_bool(io, &present) || present != (input != NULL))
        return application_fail(io->error, QA_ERROR_FORMAT, "Q3 input checkpoint declaration differs");
    if (!present) return true;
    if (!qa_source_save_count(io, &input->binding_count, 6)) return false;
    for (size_t i = 0; i < 6; ++i)
        if (!qa_source_save_u64(io, &input->bindings[i])) return false;
    if (!qa_source_save_u64(io, &input->body_binding)) return false;
    uint8_t bytes[24] = {0};
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN,
        .bytes = {bytes, sizeof(bytes)}, .context = bytes, .write = input_record_write};
    return input_command_fields(io, &input->applied_command) &&
        (io->direction != QA_SOURCE_SAVE_WRITE ||
         qa_q3_abi_write_usercmd(&record, 0, false, &input->projected_command, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_usercmd(&record, 0, &input->projected_command, io->error)) &&
        qa_source_save_bool(io, &input->command_projected) && qa_source_save_bool(io, &input->input_applied);
}

bool application_guest_input_checkpoint(q3g_role *role, qa_buffer *out, qa_error *error)
{
    if (!out || !input_checkpoint_idle(role, error)) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) &&
        input_fields(&io, role->input) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool application_guest_input_prepare_restore(q3g_role *role, qa_bytes bytes,
    application_guest_input_saved *out, qa_error *error)
{
    qa_qvm_saved_function descriptors[7] = {0};
    size_t count;
    if (!out || !application_guest_input_descriptors(role, descriptors, &count, error)) return false;
    if (!role->engine->restore_pending || role->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 input restore requires an isolated source candidate");
    application_guest_input *input = role->input;
    application_guest_input candidate = {0};
    if (input) candidate = *input;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) &&
        input_fields(&io, input ? &candidate : NULL) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok && input && (candidate.binding_count != input->binding_count ||
        (!!candidate.body_binding != !!input->body_binding)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Q3 input callback inventory differs from its source constructor");
    for (size_t i = input ? input->binding_count : 0; ok && i < 6; ++i)
        if (candidate.bindings[i])
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 input retains an undeclared saved callback identity");
    application_guest_input_saved saved = {.binding_count = count};
    if (input) {
        memcpy(saved.bindings, candidate.bindings, input->binding_count * sizeof(*saved.bindings));
        if (candidate.body_binding) saved.bindings[input->binding_count] = candidate.body_binding;
        saved.applied_command = candidate.applied_command;
        saved.projected_command = candidate.projected_command;
        saved.command_projected = candidate.command_projected;
        saved.input_applied = candidate.input_applied;
    }
    if (ok) *out = saved;
    return ok;
}

void application_guest_input_adopt_restore(q3g_role *role,
    const application_guest_input_saved *saved)
{
    application_guest_input *input = role->input;
    if (!input) return;
    memcpy(input->bindings, saved->bindings, input->binding_count * sizeof(*input->bindings));
    if (input->body_control) {
        input->body_binding = saved->bindings[input->binding_count];
        application_guest_q3_control_restore_binding(input->body_control, input->body_binding);
    }
    input->applied_command = saved->applied_command;
    input->projected_command = saved->projected_command;
    input->command_projected = saved->command_projected;
    input->input_applied = saved->input_applied;
}

bool application_guest_input_applying(const qa_application *app, qa_actor_id actor)
{
    if (!app) return false;
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider->component.clock.kind != QA_CLOCK_Q3) continue;
        struct application_q3_guest *engine = q3g_engine(provider);
        application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
        if (input && qa_actor_id_equal(input->applying, actor)) return true;
    }
    return false;
}

bool application_guest_input_interval(const qa_application *app, qa_actor_id actor, uint64_t *out)
{
    if (!app || !out) return false;
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider->component.clock.kind != QA_CLOCK_Q3) continue;
        struct application_q3_guest *engine = q3g_engine(provider);
        application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
        if (input && qa_actor_id_equal(input->applying, actor) && input->scope && input->scope->input_active &&
            qa_actor_id_equal(input->scope->actor, actor) && record_current(input, input->scope)) {
            *out = (uint64_t)input->scope->milliseconds * UINT64_C(1000000);
            return true;
        }
    }
    return false;
}

bool application_guest_input_actor_idle(const qa_application *app, qa_actor_id actor)
{
    if (!app) return false;
    for (application_provider *provider = app->live_providers; provider;
         provider = provider->next_live) {
        if (provider->kind != APPLICATION_PROVIDER_QVM &&
            !(provider->kind == APPLICATION_PROVIDER_NATIVE &&
              provider->component.clock.kind == QA_CLOCK_Q3)) continue;
        struct application_q3_guest *engine = q3g_engine(provider);
        for (q3g_role *role = engine ? engine->roles : NULL; role; role = role->next) {
            application_guest_input *input = role->input;
            if (!input) continue;
            if ((input->in_command && qa_actor_id_equal(input->command_actor, actor)) ||
                qa_actor_id_equal(input->applying, actor)) return false;
            for (guest_client_scope *scope = input->scope; scope; scope = scope->previous)
                if (qa_actor_id_equal(scope->actor, actor)) return false;
        }
    }
    return true;
}

static bool is_guest(const application_provider *provider)
{
    return provider && (provider->kind == APPLICATION_PROVIDER_QVM ||
        (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->component.clock.kind == QA_CLOCK_Q3));
}

bool application_arsenal_guest_output_admit(application_provider *provider, uint8_t channels,
    qa_error *error)
{
    if (!is_guest(provider) || !provider->constructed || !provider->attached || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 outputs need an attached source owner");
    struct application_q3_guest *engine = q3g_engine(provider);
    const application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
    if ((channels & (1u << APPLICATION_CLIENT_MOVEMENT_MODE)) &&
        (!input || !input->profile.has_modes))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Original Q3 movement modes need an artifact-qualified input declaration");
    if ((channels & (1u << APPLICATION_CLIENT_BODY_SHAPE)) &&
        (!input || !application_guest_q3_control_supports_body(input->body_control)))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Original Q3 movement has no admitted body trace interface");
    return true;
}

bool application_arsenal_guest_outputs(application_provider *provider, qa_actor_id actor,
    application_client_outputs *out, qa_error *error)
{
    if (!out || !is_guest(provider) || !provider->constructed || !provider->attached || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 output use lost its actual source owner");
    struct application_q3_guest *engine = q3g_engine(provider);
    application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
    uint32_t slot; qa_q3_player player;
    if (!engine || !engine->game || !application_q3_guest_actor_client(provider, actor, &slot) ||
        !qa_q3_host_source_player(engine->game->host, slot, &player, error)) return false;
    *out = (application_client_outputs){0};
    if (input) for (size_t i = 0; i < input->profile.intermission_count; ++i)
        if (player.pmType == input->profile.intermission_modes[i]) return true;
    return application_qc_control_outputs(provider->application, actor, out, error);
}

bool application_arsenal_guest_crouched(application_provider *provider, qa_actor_id actor,
    bool *out, qa_error *error)
{
    struct application_q3_guest *engine = is_guest(provider) ? q3g_engine(provider) : NULL;
    uint32_t slot; qa_q3_player player;
    if (!out || !engine || !engine->game || !application_q3_guest_actor_client(provider, actor, &slot) ||
        !qa_q3_host_source_player(engine->game->host, slot, &player, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original view stance lost its actual source client");
    *out = ((uint32_t)player.pmFlags & 1u) != 0;
    return true;
}

static bool guest_command(application_guest_input *input, qa_actor_id actor, uint32_t slot,
    const qa_q3_usercmd *source, const qa_movement_command *command,
    const application_control_external_stage *stage, qa_error *error)
{
    application_provider *guest = input->role->engine->provider;
    qa_application *app = guest->application;
    struct application_q3_guest *engine = input->role->engine;
    application_provider *movement = application_provider_for(app, actor, QA_ROLE_MOVEMENT, "");
    qa_q3_player player;
    if (!qa_q3_host_source_player(input->role->host, slot, &player, error)) return false;
    int32_t before_time = player.commandTime;
    if (!qa_q3_host_player_motion(input->role->host, slot, true, error)) return false;
    input->in_command = true; input->command_actor = actor;
    input->command = command; input->stage = stage; input->input_applied = false;
    input->command_projected = false;
    bool ok = application_q3_guest_client_think(guest, slot, source, error);
    if (ok && qa_actors_get(qa_session_actors(app->session), actor) && movement == guest &&
        engine->clients[slot].connected && !engine->clients[slot].pending_retirement &&
        qa_actor_id_equal(engine->clients[slot].actor, actor)) {
        ok = qa_q3_host_source_player(input->role->host, slot, &player, error);
        if (ok && engine->clients[slot].connected && !engine->clients[slot].pending_retirement &&
            qa_actor_id_equal(engine->clients[slot].actor, actor)) {
            qa_movement_command applied = input->input_applied ? input->applied_command : *command;
            uint32_t elapsed = (uint32_t)player.commandTime - (uint32_t)before_time;
            if (elapsed > INT32_MAX) elapsed = 0;
            applied.milliseconds = elapsed > 1000 ? 1000 : elapsed;
            ok = stage ? stage->current(stage) && stage->complete(stage, &applied, &player, error) :
                application_control_guest_complete(app, actor, &applied, &player, error);
        }
    }
    qa_error unwind = {0};
    if (!qa_q3_host_player_motion(input->role->host, slot, false, &unwind)) {
        if (ok && error) *error = unwind;
        ok = false;
    }
    input->command = NULL; input->stage = NULL; input->in_command = false;
    input->command_actor = (qa_actor_id){0};
    qa_error drain = {0};
    if (!application_guest_clients_drain(guest, &drain)) {
        if (ok && error) *error = drain;
        ok = false;
    }
    return ok;
}

bool application_arsenal_guest_source_command(qa_application *app, qa_actor_id actor,
    const qa_movement_command *command, qa_error *error)
{
    application_provider *source = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    const application_control_context *context = application_control_frame_current(app, actor);
    if (!is_guest(source) || !command || command->kind != QA_MOVEMENT_Q3 || !context ||
        !context->source_guestcmd || !context->command_only || context->command.provider != source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 command has no genuine source admission");
    struct application_q3_guest *engine = q3g_engine(source);
    application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
    application_provider *movement = application_provider_for(app, actor, QA_ROLE_MOVEMENT, "");
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    uint32_t slot;
    if (!input || input->in_command || (movement != source && !input->profile.has_locomotion) ||
        (arsenal != source && (!input->profile.has_weapons || !input->profile.weapon_branch_count)))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Original Q3 command requires qualified movement and weapon source regions");
    if ((is_guest(movement) && movement != source) ||
        !qa_q3_host_actor_slot(engine->game->host, actor, &slot, error) || slot >= 64 ||
        !engine->clients[slot].connected || !engine->clients[slot].begun ||
        engine->clients[slot].pending_retirement || !qa_actor_id_equal(engine->clients[slot].actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 command has no current source client");
    qa_q3_usercmd raw = {.serverTime = command->server_time_ms,
        .buttons = (int32_t)command->buttons, .weapon = command->weapon,
        .forwardmove = (int8_t)command->forward_move, .rightmove = (int8_t)command->side_move,
        .upmove = (int8_t)command->up_move};
    memcpy(raw.angles, command->angle_words, sizeof(raw.angles));
    return guest_command(input, actor, slot, &raw, command, NULL, error);
}

static bool guest_move(qa_application *app, qa_actor_id actor,
                                     const qa_movement_command *command, bool *handled,
                                     const application_control_external_stage *stage, qa_error *error)
{
    *handled = false;
    if (!app || !command || !qa_vec_finite(command->angles) ||
        !isfinite(command->forward_move) || !isfinite(command->side_move) || !isfinite(command->up_move))
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest command contains invalid controls");
    if (application_guest_input_applying(app, actor)) return true;
    application_provider *movement = application_provider_for(app, actor, QA_ROLE_MOVEMENT, "");
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    application_provider *guest = is_guest(arsenal) ? arsenal : is_guest(movement) ? movement : NULL;
    if (!guest) return true;
    struct application_q3_guest *engine = q3g_engine(guest);
    application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
    if (!input || input->in_command)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest input owner is missing or already active");
    if ((movement != guest && !input->profile.has_locomotion) ||
        (arsenal != guest && (!input->profile.has_weapons || !input->profile.weapon_branch_count)))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Mixed Q3 guest roles require qualified source movement and weapon contracts");
    if (is_guest(movement) && movement != guest)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Two Q3 guest movement owners require an explicit cross-artifact contract");
    uint32_t slot;
    if (!qa_q3_host_actor_slot(engine->game->host, actor, &slot, error)) return false;
    if (slot >= 64 || !engine->clients[slot].connected ||
        !qa_actor_id_equal(engine->clients[slot].actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest command targets an unconnected source client");
    qa_q3_player player;
    if (!qa_q3_host_source_player(engine->game->host, slot, &player, error)) return false;
    qa_q3_usercmd source = {.serverTime = (int32_t)((uint32_t)player.commandTime + command->milliseconds),
        .buttons = (int32_t)(command->kind == QA_MOVEMENT_Q3 ? command->buttons : command->buttons & 5u),
        .weapon = arsenal == guest ? command->weapon : (uint8_t)player.weapon};
    qa_application_control_view control;
    if (!qa_application_control_read(app, actor, &control))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Guest command has no shared control view");
    float angles[] = {command->angles.x, command->angles.y, command->angles.z};
    if (command->kind == QA_MOVEMENT_Q3 || command->kind == QA_MOVEMENT_Q2_CLASSIC) {
        for (size_t i = 0; i < 3; ++i) {
            int32_t delta = command->kind == QA_MOVEMENT_Q3 ? control.state.data.q3.delta_angle_words[i]
                                                          : control.state.data.q2.delta_angle_shorts[i];
            angles[i] = (float)(uint16_t)((uint32_t)command->angle_words[i] + (uint32_t)delta) * (360.0f / 65536.0f);
        }
    } else if (command->kind == QA_MOVEMENT_Q2_RERELEASE) {
        qa_vec3 aim = qa_vec_add(command->angles, control.state.data.q2r.delta_angles);
        angles[0] = aim.x; angles[1] = aim.y; angles[2] = aim.z;
    }
    for (size_t i = 0; i < 3; ++i)
        source.angles[i] = (uint16_t)(angle_word(angles[i]) - player.deltaAngles[i]);
    if (command->kind == QA_MOVEMENT_Q3 && movement == guest) {
        source.serverTime = command->server_time_ms;
        memcpy(source.angles, command->angle_words, sizeof(source.angles));
    }
    float scale = 127.0f / move_scale(command->kind);
    source.forwardmove = axis(command->forward_move * scale);
    source.rightmove = axis(command->side_move * scale);
    source.upmove = axis(command->up_move * scale);
    if (command->kind == QA_MOVEMENT_Q2_RERELEASE) {
        if (command->buttons & 8u) source.upmove = 127;
        if (command->buttons & 16u) source.upmove = -127;
    } else if ((command->buttons & 2u) && source.upmove == 0) source.upmove = 127;
    *handled = true;
    return guest_command(input, actor, slot, &source, command, stage, error);
}

bool application_arsenal_guest_move(qa_application *app, qa_actor_id actor,
    const qa_movement_command *command, bool *handled, qa_error *error)
{ return guest_move(app, actor, command, handled, NULL, error); }

bool application_arsenal_guest_stage_move(qa_application *app, qa_actor_id actor,
    const qa_movement_command *command, const application_control_external_stage *stage,
    bool *handled, qa_error *error)
{
    if (!stage || stage->application != app || !qa_actor_id_equal(stage->actor, actor) ||
        !stage->current || !stage->input || !stage->locomotion || !stage->complete || !stage->current(stage))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original movement lost its retained NQ stage owner");
    return guest_move(app, actor, command, handled, stage, error);
}

bool application_arsenal_guest_stage_ready(qa_application *app, qa_actor_id actor)
{
    application_provider *movement = application_provider_for(app, actor, QA_ROLE_MOVEMENT, "");
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    application_provider *guest = is_guest(arsenal) ? arsenal : is_guest(movement) ? movement : NULL;
    if (!guest) return true;
    struct application_q3_guest *engine = q3g_engine(guest);
    application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
    uint32_t slot;
    return guest->constructed && guest->attached && !guest->close_pending && input && !input->in_command &&
        !(is_guest(movement) && movement != guest) &&
        (movement == guest || input->profile.has_locomotion) &&
        (arsenal == guest || (input->profile.has_weapons && input->profile.weapon_branch_count)) &&
        qa_q3_host_actor_slot(engine->game->host, actor, &slot, NULL) && slot < 64 &&
        engine->clients[slot].connected && engine->clients[slot].begun &&
        !engine->clients[slot].pending_retirement && qa_actor_id_equal(engine->clients[slot].actor, actor);
}
