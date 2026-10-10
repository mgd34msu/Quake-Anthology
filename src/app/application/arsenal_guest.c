#include "qa/input.h"
#include "guest_input_private.h"
#include "control_frame.h"
#include "client_outputs.h"
#include "guest_q3_control.h"
#include "guest_q3_weapons_services.h"
#include "guest_projection_private.h"
#include "qa/q3_abi.h"
#include "qa/qvm_save.h"
#include "qa/source_save.h"
#include "qa/text.h"
#include <math.h>

typedef struct guest_client_scope {
    struct guest_client_scope *previous;
    const qa_qvm_call *call;
    qa_actor_id actor;
    qa_q3_host_game_data data;
    uint32_t slot, player, movement, milliseconds;
    qa_q3_equipment_motion equipment;
    bool equipment_active;
    qa_usercmd application_command;
    bool weapon_slice, weapon_reached;
    bool spawning, input_active;
} guest_client_scope;

typedef struct application_guest_input {
    q3g_role *role;
    application_guest_input_profile profile;
    qa_qvm_binding bindings[5];
    size_t binding_count;
    application_guest_q3_control *body_control;
    application_q3_weapons *weapons;
    qa_qvm_binding body_binding;
    guest_client_scope *scope;
    qa_actor_id applying;
    qa_actor_id command_actor;
    const qa_usercmd *command;
    const application_control_external_stage *stage;
    qa_usercmd applied_command;
    qa_q3_usercmd projected_command;
    bool command_projected;
    bool input_applied;
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
    uint64_t address = data.entities_address + (uint64_t)scope->slot * data.entity_stride + input->profile.source->client_pointer;
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
    if (data.entity_stride != input->profile.source->entity_stride ||
        data.client_stride != input->profile.source->client_stride)
        return application_fail(error, QA_ERROR_FORMAT, "Guest input records changed their qualified layout");
    uint64_t entity = data.entities_address + (uint64_t)slot * data.entity_stride;
    uint64_t player = data.clients_address + (uint64_t)slot * data.client_stride;
    if (entity > UINT32_MAX - input->profile.source->client_pointer || player > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Guest input pointer leaves QVM memory");
    uint32_t pointer;
    if (!source_word(input, (uint32_t)entity + input->profile.source->client_pointer, &pointer, error)) return false;
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
    for (guest_client_scope *parent = input->scope; parent; parent = parent->previous)
        if (parent->spawning && parent->slot == scope.slot &&
            qa_actor_id_equal(parent->actor, scope.actor))
            return qa_qvm_proceed(call, result, error);
    input->scope = &scope;
    bool ok = qa_qvm_proceed(call, result, error);
    if (ok && !current(input, &scope, call)) ok = cancel_client(call, call, error);
    input->scope = scope.previous;
    return ok;
}

static bool replace_locomotion(void *context, const qa_qvm_call *call, bool *skip, qa_error *error)
{
    application_guest_input *input = context;
    guest_client_scope *scope = input->scope;
    *skip = false;
    if (!scope || scope->spawning) return true;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    if (scope->equipment_active && scope->equipment.fixed_pose) {
        uint32_t movement;
        if (!source_word(input, input->profile.source->movement_global, &movement, error)) return false;
        if (movement == scope->movement) {
            uint32_t axes = input->role->abi == QA_QVM_Q3_116N ? 20u : 21u;
            uint8_t zero[12] = {0};
            if (!qa_qvm_write(input->role->vm, scope->movement + 4 + axes,
                    (qa_bytes){zero, 3}, error) ||
                !qa_qvm_write(input->role->vm, scope->player + 32,
                    (qa_bytes){zero, sizeof(zero)}, error)) return false;
            *skip = true; return true;
        }
    }
    qa_application *app = input->role->engine->provider->application;
    if (application_provider_for(app, scope->actor, QA_ROLE_MOVEMENT, "") == input->role->engine->provider)
        return true;
    qa_q3_usercmd source;
    qa_q3_player player;
    if (!qa_qvm_read_usercmd(input->role->vm, (int32_t)(scope->movement + 4), &source, error) ||
        !qa_qvm_read_player(input->role->vm, (int32_t)scope->player, true, &player, error)) return false;
    qa_player_state control;
    if (!qa_application_control_read(app, scope->actor, &control))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Guest movement has no shared continuation");
    uint32_t remaining = scope->milliseconds;
    qa_usercmd raw;
    qa_usercmd_from_q3(&source, input->command ? input->command->sequence : control.command_sequence + 1, &raw);
    raw.milliseconds = remaining; raw.angles = qa_v3(player.viewangles[0], player.viewangles[1], player.viewangles[2]);
    memset(raw.angle_words, 0, sizeof(raw.angle_words)); raw.buttons &= 1u;
    raw.weapon = input->command ? input->command->weapon : source.weapon;
    raw.impulse = input->command && input->input_applied ? input->applied_command.impulse
                : input->command ? input->command->impulse : 0;
    qa_input_command_basis from = {.kind = QA_RULESET_Q3}, to = {.kind = control.state.kind};
    if (to.kind == QA_RULESET_Q3 || to.kind == QA_RULESET_Q2_CLASSIC) {
        to.words = to.relative = to.wrap_words = true;
        for (unsigned i = 0; i < 3; ++i) to.delta_words[i] = to.kind == QA_RULESET_Q3 ?
            control.state.data.q3.delta_angle_words[i] : control.state.data.q2.delta_angle_shorts[i];
    } else if (to.kind == QA_RULESET_Q2_RERELEASE) {
        to.relative = true; to.delta_angles = control.state.data.q2r.delta_angles;
    }
    qa_usercmd command;
    qa_input_command_convert(&raw, NULL, &from, &to, (qa_input_axis_rule){0}, &command);
    if (command.kind == QA_RULESET_Q3) command.buttons = (uint32_t)source.buttons;
    else if (command.kind == QA_RULESET_Q2_RERELEASE) {
        if (source.upmove > 0) command.buttons |= 8u;
        if (source.upmove < 0) command.buttons |= 16u;
        command.up_move = 0;
    } else if (command.kind == QA_RULESET_NETQUAKE && source.upmove > 0) command.buttons |= 2u;
    if ((command.kind == QA_RULESET_NETQUAKE || command.kind == QA_RULESET_QUAKEWORLD) &&
        input->command && input->input_applied && input->command_projected &&
        input->applied_command.kind == command.kind && source.upmove == input->projected_command.upmove) {
        /* Q3 carries jump and upward movement in one source byte. Retain the
         * independently consumed Q1 controls when reading our own projection. */
        command.up_move = input->applied_command.up_move;
        command.buttons = (command.buttons & ~2u) | (input->applied_command.buttons & 2u);
    }
    qa_actor_id previous = input->applying; input->applying = scope->actor;
    qa_usercmd applied = command;
    bool ok = input->stage ? input->stage->current(input->stage) &&
        input->stage->locomotion(input->stage, &command, &applied, error) :
        application_control_frames_apply_nested(app, scope->actor, &command, &applied, error);
    input->applying = previous;
    if (!ok) return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    if (input->command) { input->applied_command = applied; input->input_applied = true; }
    qa_q3_usercmd updated = source;
    updated.buttons = command.kind == QA_RULESET_Q3 ? (int32_t)applied.buttons
        : (int32_t)(((uint32_t)source.buttons & ~1u) | (applied.buttons & 1u));
    qa_input_command_basis applied_from = {.kind = applied.kind};
    if (applied.kind == QA_RULESET_Q2_CLASSIC || applied.kind == QA_RULESET_Q3) {
        applied_from.words = applied_from.relative = applied_from.wrap_words = applied_from.repack_words = true;
        for (unsigned i = 0; i < 3; ++i) applied_from.delta_words[i] = applied.kind == QA_RULESET_Q3 ?
            control.state.data.q3.delta_angle_words[i] : control.state.data.q2.delta_angle_shorts[i];
    } else if (applied.kind == QA_RULESET_Q2_RERELEASE) {
        applied_from.relative = true; applied_from.delta_angles = control.state.data.q2r.delta_angles;
    }
    qa_input_command_basis source_to = {.kind = QA_RULESET_Q3, .words = true, .relative = true};
    memcpy(source_to.delta_words, player.deltaAngles, sizeof(source_to.delta_words));
    qa_usercmd projected;
    qa_input_command_convert(&applied, NULL, &applied_from, &source_to,
        (qa_input_axis_rule){.quantization = QA_INPUT_AXIS_NEAREST, .clamp = true,
            .minimum = -128, .maximum = 127, .float_product = true}, &projected);
    projected.server_time_ms = source.serverTime; projected.buttons = (uint32_t)updated.buttons;
    projected.weapon = source.weapon;
    qa_usercmd output = projected;
    memcpy(output.angle_words, source.angles, sizeof(output.angle_words));
    qa_usercmd_to_q3(&output, &updated);
    if (applied.kind == QA_RULESET_Q2_RERELEASE) {
        if (applied.buttons & 8u) updated.upmove = 127;
        else if (applied.buttons & 16u) updated.upmove = -127;
    } else if (applied.kind == QA_RULESET_NETQUAKE || applied.kind == QA_RULESET_QUAKEWORLD) {
        if (applied.buttons & 2u) updated.upmove = 127;
        else if (updated.upmove > 0) updated.upmove = 0;
    }
    bool aim_changed = applied.angles.x != command.angles.x || applied.angles.y != command.angles.y ||
        applied.angles.z != command.angles.z || memcmp(applied.angle_words, command.angle_words, sizeof(command.angle_words));
    if (aim_changed) memcpy(updated.angles, projected.angle_words, sizeof(updated.angles));
    if (memcmp(&updated, &source, sizeof(source)) != 0 &&
        !qa_qvm_write_usercmd(input->role->vm, (int32_t)(scope->movement + 4), true, &updated, error))
        return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    if (input->command) { input->projected_command = updated; input->command_projected = true; }
    qa_body_state body;
    if (!qa_world_body_read(app->world, scope->actor, &body, error) ||
        !qa_application_control_read(app, scope->actor, &control)) return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    qa_actor_id ground_actor = qa_actor_reference_resolve(qa_session_actors(app->session), body.ground);
    bool grounded = control.ground.hit != QA_TRACE_HIT_NONE;
    uint32_t ground = grounded ? 1022u : 1023u;
    if (grounded && body.ground.kind == QA_ACTOR_REFERENCE_SOURCE && body.ground.value.source.owner == input->role->source_owner)
        ground = body.ground.value.source.slot;
    else if (grounded && qa_actor_reference_present(body.ground) && !qa_actor_id_equal(ground_actor, app->physics->world_actor) &&
        !qa_q3_host_actor_slot(input->role->host, ground_actor, &ground, error)) return false;
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
        uint32_t offset = i < 3 ? input->profile.source->movement_mins + (uint32_t)i * 4
                               : input->profile.source->movement_maxs + (uint32_t)(i - 3) * 4;
        if (!qa_qvm_write(input->role->vm, scope->movement + offset, (qa_bytes){bytes, 4}, error)) return false;
        if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    }
    uint8_t water[8]; qa_store_u32le(water, (uint32_t)control.water_level);
    qa_store_u32le(water + 4, (uint32_t)control.water_type);
    if (!qa_qvm_write(input->role->vm, scope->movement + input->profile.source->water_movement,
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
    qa_usercmd source = {.kind = QA_RULESET_Q3}, converted;
    for (size_t i = 0; i < 3; ++i)
        source.angle_words[i] = qa_input_signed_word((uint32_t)command->angles[i] + (uint32_t)player->deltaAngles[i]);
    if (source.angle_words[0] > 16000) source.angle_words[0] = 16000;
    else if (source.angle_words[0] < -16000) source.angle_words[0] = -16000;
    qa_input_command_basis from = {.kind = QA_RULESET_Q3, .words = true}, to = {.kind = QA_RULESET_Q3};
    qa_input_command_convert(&source, NULL, &from, &to, (qa_input_axis_rule){0}, &converted);
    return converted.angles;
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
    qa_player_state control;
    if (!qa_application_control_read(app, scope->actor, &control))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Guest source input lost its continuation");
    qa_movement_state state = qa_movement_state_default(QA_RULESET_Q3,
        qa_v3(player.origin[0], player.origin[1], player.origin[2]));
    memcpy(state.data.q3.delta_angle_words, player.deltaAngles, sizeof(player.deltaAngles));
    qa_usercmd command;
    qa_usercmd_from_q3(&source, input->command ? input->command->sequence : control.command_sequence + 1, &command);
    command.milliseconds = milliseconds;
    command.angles = qa_v3(player.viewangles[0], player.viewangles[1], player.viewangles[2]);
    command.impulse = input->command && input->input_applied ? input->applied_command.impulse
                    : input->command ? input->command->impulse : 0;
    input_scope->working_state = state;
    input_scope->working_command = command;
    qa_vec3 absolute_aim = source_view_angles(input, &source, &player);
    bool applied = input->stage ? input->stage->current(input->stage) &&
        input->stage->input(input->stage, &input_scope->working_state, &input_scope->working_command, &absolute_aim, input_scope,
            before, slice, (uint64_t)milliseconds * UINT64_C(1000000), error) :
        application_control_source_input(app, scope->actor, &input_scope->working_state, &input_scope->working_command, &absolute_aim, input_scope,
            before, slice, (uint64_t)milliseconds * UINT64_C(1000000), error);
    if (!applied) return false;
    command = input_scope->working_command;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    if (before) scope->application_command = command;
    qa_q3_usercmd updated;
    qa_input_command_basis basis = {.kind = QA_RULESET_Q3, .words = true};
    qa_usercmd projected;
    qa_input_command_convert(&command, NULL, &basis, &basis,
        (qa_input_axis_rule){.quantization = QA_INPUT_AXIS_NEAREST, .clamp = true,
            .minimum = -128, .maximum = 127, .float_product = true}, &projected);
    projected.server_time_ms = source.serverTime;
    qa_usercmd_to_q3(&projected, &updated);
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

typedef struct guest_weapon_preparation {
    application_guest_input *input;
    guest_client_scope *scope;
    uint32_t address, previous, projected;
    size_t bytes;
} guest_weapon_preparation;

static bool finish_weapon(void *context, qa_error *error)
{
    guest_weapon_preparation *prepared = context;
    bool okay = true;
    if (record_current(prepared->input, prepared->scope)) {
        uint8_t bytes[4];
        okay = qa_qvm_read(prepared->input->role->vm, prepared->address, bytes, prepared->bytes, error);
        if (okay && (prepared->bytes == 1 ? bytes[0] : qa_load_u32le(bytes)) == prepared->projected) {
            qa_store_u32le(bytes, prepared->previous);
            okay = qa_qvm_write(prepared->input->role->vm, prepared->address,
                (qa_bytes){bytes, prepared->bytes}, error);
        }
    }
    free(prepared); return okay;
}

bool application_guest_input_prepare_weapon(void *context, qa_actor_id actor,
    const qa_qvm_call *call, application_q3_weapon_preparation *out, qa_error *error)
{
    q3g_role *role = context;
    application_guest_input *input = role ? role->input : NULL;
    if (!out || !role || !input || input->role != role || !call || call->vm != role->vm)
        return application_fail(error, QA_ERROR_ARGUMENT, "Weapon preparation requires its actual input owner");
    *out = (application_q3_weapon_preparation){0};
    guest_client_scope *scope = input->scope;
    if (!scope || !scope->equipment_active || !scope->equipment.owns_holdable_input ||
        !qa_actor_id_equal(scope->actor, actor)) return true;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    uint32_t movement;
    if (!source_word(input, input->profile.source->movement_global, &movement, error)) return false;
    if (movement != scope->movement) return true;
    guest_weapon_preparation *prepared = calloc(1, sizeof(*prepared));
    if (!prepared) return application_fail(error, QA_ERROR_MEMORY, "Allocating original holdable input scope");
    bool legacy = role->abi == QA_QVM_Q3_116N;
    *prepared = (guest_weapon_preparation){.input = input, .scope = scope,
        .address = scope->movement + 4 + (legacy ? 4u : 16u), .bytes = legacy ? 1u : 4u};
    uint8_t bytes[4] = {0};
    if (!qa_qvm_read(role->vm, prepared->address, bytes, prepared->bytes, error)) { free(prepared); return false; }
    prepared->previous = legacy ? bytes[0] : qa_load_u32le(bytes);
    prepared->projected = prepared->previous & ~UINT32_C(4);
    qa_store_u32le(bytes, prepared->projected);
    if (!qa_qvm_write(role->vm, prepared->address, (qa_bytes){bytes, prepared->bytes}, error)) {
        free(prepared); return false;
    }
    *out = (application_q3_weapon_preparation){prepared, finish_weapon};
    return true;
}

bool application_guest_input_weapon_completed(void *context, qa_actor_id actor,
    bool reached, qa_error *error)
{
    q3g_role *role = context;
    application_guest_input *input = role ? role->input : NULL;
    if (!role || !input || input->role != role)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon decision lost its actual input owner");
    guest_client_scope *scope = input->scope;
    if (!scope || !scope->weapon_slice || !qa_actor_id_equal(scope->actor, actor)) return true;
    if (!current(input, scope, scope->call)) return cancel_client(scope->call, scope->call, error);
    scope->weapon_reached = reached;
    return true;
}

bool application_guest_input_weapon_slice(const qa_application *app, qa_actor_id actor)
{
    application_provider *source = app ? application_world_provider((qa_application *)app, QA_ROLE_ENTITIES, "") : NULL;
    struct application_q3_guest *engine = source && source->kind == APPLICATION_PROVIDER_QVM ? q3g_engine(source) : NULL;
    application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
    guest_client_scope *scope = input ? input->scope : NULL;
    return scope && scope->weapon_slice && scope->input_active &&
        qa_actor_id_equal(scope->actor, actor) && current(input, scope, scope->call);
}

static bool equipment_admit(application_guest_input *input, guest_client_scope *scope,
    const qa_qvm_call *call, qa_error *error)
{
    qa_application *app = input->role->engine->provider->application;
    if (!application_control_guest_equipment(app, input->role->engine->provider,
            scope->actor, input->weapons, &scope->equipment, error)) return false;
    if (!current(input, scope, call)) return cancel_client(call, scope->call, error);
    if (!isfinite(scope->equipment.speed_multiplier) || scope->equipment.speed_multiplier <= 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected original movement speed must be positive and finite");
    if (scope->equipment.fixed_pose && !application_guest_q3_control_supports_pose(input->body_control))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Original equipment pose has no declared duck function");
    uint32_t bits;
    if (!source_word(input, scope->player + 52, &bits, error)) return false;
    int32_t speed; memcpy(&speed, &bits, sizeof(speed));
    float value = (float)((double)speed * scope->equipment.speed_multiplier);
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)qa_source_float_to_i32(value));
    if (!qa_qvm_write(input->role->vm, scope->player + 52, (qa_bytes){bytes, sizeof(bytes)}, error)) return false;
    scope->equipment_active = true;
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
    qa_q3_equipment_motion previous_equipment = scope->equipment;
    bool previous_equipment_active = scope->equipment_active;
    qa_usercmd previous_application = scope->application_command;
    bool previous_weapon_slice = scope->weapon_slice, previous_weapon_reached = scope->weapon_reached;
    scope->movement = (uint32_t)movement;
    uint64_t end = (uint64_t)scope->movement + 28;
    const uint32_t offsets[] = {input->profile.source->movement_mins, input->profile.source->movement_maxs,
                               input->profile.source->water_movement};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        uint64_t limit = (uint64_t)scope->movement + offsets[i] + (i == 2 ? 8u : 12u);
        if (limit > end) end = limit;
    }
    if (end > qa_qvm_memory_size(input->role->vm)) {
        scope->movement = previous;
        return application_fail(error, QA_ERROR_FORMAT, "Guest movement projection leaves source memory");
    }
    bool slice = call->instruction == input->profile.source->movement_slice;
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
    bool ok = true, cancelled = false;
    scope->weapon_slice = slice && input->weapons && input->weapons == input->role->weapons &&
        application_world_provider(input->role->engine->provider->application, QA_ROLE_ENTITIES, "") ==
            input->role->engine->provider &&
        application_provider_for(input->role->engine->provider->application, scope->actor, QA_ROLE_ARSENAL, "") !=
            input->role->engine->provider;
    scope->weapon_reached = false;
    if (!slice) {
        scope->equipment = (qa_q3_equipment_motion){0}; scope->equipment_active = false;
        if (application_world_provider(input->role->engine->provider->application, QA_ROLE_ENTITIES, "") ==
            input->role->engine->provider)
            ok = equipment_admit(input, scope, call, error);
    }
    if (ok) ok = qa_qvm_call_cancelled(call, &cancelled, error);
    if (ok && !cancelled) ok = source_input(input, scope, call, &input_scope, true, slice, scope->milliseconds, error);
    scope->input_active = ok && !cancelled;
    if (ok) ok = qa_qvm_call_cancelled(call, &cancelled, error);
    if (ok && !current(input, scope, call)) {
        qa_error cleanup = {0};
        (void)application_control_source_abort(&input_scope, &cleanup);
        scope->movement = previous; scope->milliseconds = previous_ms; scope->input_active = previous_active;
        scope->equipment = previous_equipment; scope->equipment_active = previous_equipment_active;
        scope->application_command = previous_application;
        scope->weapon_slice = previous_weapon_slice; scope->weapon_reached = previous_weapon_reached;
        return cancel_client(call, scope->call, error);
    }
    if (ok && !cancelled && slice && (input->profile.source && input->profile.source->present)) {
        qa_qvm_region_binding binding = {.entry = input->profile.source->locomotion.entry,
            .join = input->profile.source->locomotion.join, .enter = replace_locomotion, .context = input};
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
    qa_movement_environment equipment = {.fixed_pose = scope->equipment.fixed_pose,
        .fixed_crouched = scope->equipment.fixed_pose, .pose = scope->equipment.pose};
    if (ok && !cancelled && !slice)
        ok = application_guest_q3_control_begin(input->body_control, call, scope->call, scope->actor,
            scope->slot, scope->player, scope->movement, scope->equipment_active ? &equipment : NULL,
            &body_scope, error);
    if (ok && !cancelled && !body_scope.retired && !body_scope.cancelled) ok = qa_qvm_proceed(call, result, error);
    if (ok) ok = qa_qvm_call_cancelled(call, &cancelled, error);
    qa_error body_cleanup = {0};
    if (!application_guest_q3_control_end(input->body_control, &body_scope, &body_cleanup)) {
        if (ok && error) *error = body_cleanup;
        ok = false;
    }
    if (ok && !cancelled && !body_scope.retired && !body_scope.cancelled && current(input, scope, call))
        ok = source_input(input, scope, call, &input_scope, false, slice, scope->milliseconds, error);
    if (ok && !cancelled && scope->weapon_slice && !body_scope.retired && !body_scope.cancelled &&
        current(input, scope, call)) {
        qa_q3_player completed;
        ok = qa_qvm_read_player(input->role->vm, (int32_t)scope->player, true, &completed, error) &&
            application_q3_weapons_services_slice_finish(input->role, scope->actor,
                &scope->application_command, &completed, scope->weapon_reached, error);
    }
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
    scope->movement = previous; scope->milliseconds = previous_ms; scope->input_active = previous_active;
    scope->equipment = previous_equipment; scope->equipment_active = previous_equipment_active;
    scope->application_command = previous_application;
    scope->weapon_slice = previous_weapon_slice; scope->weapon_reached = previous_weapon_reached;
    return ok;
}

bool application_guest_input_attach(q3g_role *role, qa_bytes primary, qa_error *error)
{
    if (!role || role->kind != QA_QVM_GAME || role->input)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest input hooks require an unattached game role");
    application_guest_input *input = calloc(1, sizeof(*input));
    if (!input) return application_fail(error, QA_ERROR_MEMORY, "Allocating guest input owner");
    input->role = role;
    if (!application_guest_input_profile_read(role, primary, &input->profile, error)) { free(input); return false; }
    if (role->weapons) {
        const application_q3_weapon_profile *source = application_q3_weapons_profile(role->weapons);
        qa_qvm_saved_function descriptors[6];
        bool qualified = role->vm && !qa_qvm_active(role->vm) &&
            application_q3_weapons_idle(role->weapons) && source &&
            input->profile.source == source && (!source->present || input->profile.input_present);
        if (!qualified)
            application_fail(error, QA_ERROR_ARGUMENT, "Original input must borrow its actual idle weapon declaration");
        if (!qualified ||
            application_q3_weapons_descriptor_count(role->weapons) != (source->present ? 6u : 0u) ||
            !application_q3_weapons_descriptors(role->weapons, descriptors, source->present ? 6u : 0u, error)) {
            application_guest_input_profile_free(&input->profile); free(input); return false;
        }
        input->weapons = role->weapons;
    }
    role->input = input;
    if (!input->profile.input_present) return true;
    const uint32_t entries[] = {input->profile.client_think, input->profile.run_client,
        input->profile.client_spawn, input->profile.source->movement_move, input->profile.source->movement_slice};
    size_t count = 5;
    for (size_t i = 0; i < count; ++i) {
        qa_qvm_function_hook hook = i < 3 ? envelope : source_move;
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
    input->weapons = NULL;
    application_guest_input_profile_free(&input->profile); free(input); role->input = NULL;
    return true;
}

bool application_guest_input_descriptors(q3g_role *role,
    qa_qvm_saved_function descriptors[6], size_t *out_count, qa_error *error)
{
    application_guest_input *input = role ? role->input : NULL;
    bool idle = (role && role->vm && !qa_qvm_active(role->vm) &&
        (!input || (!input->scope && !input->in_command && !input->command &&
                    !input->applying.registry && !input->command_actor.registry))) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 input checkpoint requires completed source command scopes");
    if (!idle) return false;
    size_t count = input && input->profile.input_present ? 5u : 0;
    if (input && input->binding_count != count)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 input callback count differs from its source declaration");
    if (count) {
        const uint32_t entries[] = {input->profile.client_think, input->profile.run_client,
            input->profile.client_spawn, input->profile.source->movement_move, input->profile.source->movement_slice};
        for (size_t i = 0; i < count; ++i)
            descriptors[i] = (qa_qvm_saved_function){input->bindings[i], entries[i], true,
                i < 3 ? envelope : source_move, input};
    }
    if (input) for (size_t i = count; i < 5; ++i)
        if (input->bindings[i])
            return application_fail(error, QA_ERROR_FORMAT, "Q3 input retains an undeclared callback identity");
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
    qa_qvm_saved_function descriptors[6] = {0};
    size_t count;
    return application_guest_input_descriptors(role, descriptors, &count, error);
}

static bool input_command_fields(qa_source_save_io *io, qa_usercmd *value)
{
    uint32_t kind = value->kind;
    if (!qa_source_save_u32(io, &kind)) return false;
    if (kind > QA_RULESET_Q3)
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
    if (ok && io->direction == QA_SOURCE_SAVE_READ) value->kind = (qa_ruleset_id)kind;
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
    bool present = input != NULL;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, expected, sizeof(magic)) ||
        !qa_source_save_bool(io, &present) || present != (input != NULL))
        return application_fail(io->error, QA_ERROR_FORMAT, "Q3 input checkpoint declaration differs");
    if (!present) return true;
    if (!qa_source_save_count(io, &input->binding_count, 5)) return false;
    for (size_t i = 0; i < 5; ++i)
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
    qa_qvm_saved_function descriptors[6] = {0};
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
    for (size_t i = input ? input->binding_count : 0; ok && i < 5; ++i)
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
        if (provider->component.clock.kind != QA_RULESET_Q3) continue;
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
        if (provider->component.clock.kind != QA_RULESET_Q3) continue;
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
              provider->component.clock.kind == QA_RULESET_Q3)) continue;
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
        (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->component.clock.kind == QA_RULESET_Q3));
}

static bool weapons_capable(const application_guest_input *input)
{
    const application_q3_weapon_profile *profile = input && input->weapons &&
        input->weapons == input->role->weapons ? application_q3_weapons_profile(input->weapons) : NULL;
    return profile && profile->present && profile->predicate_count &&
        application_q3_weapons_descriptor_count(input->weapons) == 6;
}

bool application_guest_input_source_weapons(qa_application *app, qa_actor_id actor)
{
    application_provider *primary = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    struct application_q3_guest *engine = primary ? q3g_engine(primary) : NULL;
    application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
    return primary && primary->kind == APPLICATION_PROVIDER_QVM && primary->constructed &&
        primary->attached && !primary->close_pending && input && input->role == engine->game &&
        input->profile.input_present && input->binding_count == 5 && weapons_capable(input) &&
        qa_actors_get(qa_session_actors(app->session), actor) &&
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != primary;
}

bool application_arsenal_guest_equipment_handoff_ready(application_provider *provider,
    qa_actor_id actor, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_QVM || !provider->application ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor) ||
        application_provider_for(provider->application, actor, QA_ROLE_ARSENAL, "") != provider)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Original Q3 equipment handoff requires its live selected arsenal owner");
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_role *role = engine ? engine->game : NULL;
    application_guest_input *input = role ? role->input : NULL;
    if (!engine || engine->provider != provider || engine->restore_pending || !role ||
        role->engine != engine || role->kind != QA_QVM_GAME || !role->initialized || !role->ready ||
        !role->committed || role->retired || role->source_cleared || !role->vm || !role->image || !role->host ||
        !input || input->role != role || !input->profile.input_present ||
        !weapons_capable(input) || input->binding_count != 5)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Original Q3 equipment handoff requires its retained GAME weapon profile");
    for (size_t i = 0; i < input->binding_count; ++i)
        if (!input->bindings[i])
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Original Q3 equipment handoff lost its GAME callback owner");
    uint32_t slot, bound_slot;
    if (!application_q3_guest_actor_client(provider, actor, &slot) ||
        !qa_q3_host_actor_slot(role->host, actor, &bound_slot, error) || bound_slot != slot)
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "Original Q3 equipment handoff lost its physical source client");
    guest_client_scope scope = {0};
    bool present;
    if (!slot_player(input, slot, &scope, &present, error)) return false;
    if (!present || !qa_actor_id_equal(scope.actor, actor) || !record_current(input, &scope))
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "Original Q3 equipment handoff lost its located source player");
    qa_q3_player player;
    return qa_qvm_read_player(role->vm, (int32_t)scope.player, true, &player, error);
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
    const qa_q3_usercmd *source, const qa_usercmd *command,
    const application_control_external_stage *stage, qa_error *error)
{
    application_provider *guest = input->role->engine->provider;
    qa_application *app = guest->application;
    struct application_q3_guest *engine = input->role->engine;
    application_provider *movement = application_provider_for(app, actor, QA_ROLE_MOVEMENT, "");
    qa_q3_player player;
    if (!qa_q3_host_source_player(input->role->host, slot, &player, error)) return false;
    int32_t before_time = player.commandTime;
    qa_q3_usercmd raw = *source;
    bool primary = application_world_provider(app, QA_ROLE_ENTITIES, "") == guest;
    bool request_pending = false;
    if (primary && input->role->weapon_services) {
        int32_t requested; bool pending;
        if (!application_q3_weapons_services_request(input->role->weapon_services,
                actor, &requested, &pending, error)) return false;
        if (pending) raw.weapon = (uint8_t)requested;
        request_pending = pending;
    }
    const application_control_context *context = application_control_frame_current(app, actor);
    if (context && context->unified_command && !request_pending &&
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == guest &&
        ((context->unified_intent && !context->weapon) ||
         (!context->unified_intent && command->kind != QA_RULESET_Q3))) {
        application_q3_weapon_prediction current_weapon;
        if (!input->weapons || !application_q3_weapons_prediction_read(input->weapons,
                actor, &current_weapon, error)) return false;
        raw.weapon = (uint8_t)current_weapon.source_weapon;
    }
    if (context && context->unified_intent) {
        application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
        if (!arsenal || arsenal->owner != context->arsenal)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original command lost its unified arsenal intent");
        raw.buttons = (raw.buttons & ~4) | (context->unified_holdable ? 4 : 0);
        if (context->weapon && arsenal == guest) {
            int32_t selection;
            if (!input->weapons || !application_q3_weapons_selection_for_item(input->weapons,
                    actor, context->weapon, &selection, error)) return false;
            raw.weapon = (uint8_t)selection;
        }
    }
    if (!qa_q3_host_player_motion(input->role->host, slot, true, error)) return false;
    input->in_command = true; input->command_actor = actor;
    input->command = command; input->stage = stage; input->input_applied = false;
    input->command_projected = false;
    bool ok = application_q3_guest_client_think(guest, slot, &raw, error);
    if (ok && primary && input->role->weapon_services &&
        qa_actors_get(qa_session_actors(app->session), actor) &&
        engine->clients[slot].connected && !engine->clients[slot].pending_retirement &&
        qa_actor_id_equal(engine->clients[slot].actor, actor))
        ok = application_q3_weapons_services_request_completed(input->role->weapon_services, actor, error);
    if (ok && qa_actors_get(qa_session_actors(app->session), actor) && movement == guest &&
        engine->clients[slot].connected && !engine->clients[slot].pending_retirement &&
        qa_actor_id_equal(engine->clients[slot].actor, actor)) {
        ok = qa_q3_host_source_player(input->role->host, slot, &player, error);
        if (ok && engine->clients[slot].connected && !engine->clients[slot].pending_retirement &&
            qa_actor_id_equal(engine->clients[slot].actor, actor)) {
            qa_usercmd applied = input->input_applied ? input->applied_command : *command;
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
    const qa_usercmd *command, qa_error *error)
{
    application_provider *source = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    const application_control_context *context = application_control_frame_current(app, actor);
    if (!is_guest(source) || !command || command->kind != QA_RULESET_Q3 || !context ||
        !context->source_guestcmd || !context->command_only || context->command.provider != source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 command has no genuine source admission");
    struct application_q3_guest *engine = q3g_engine(source);
    application_guest_input *input = engine && engine->game ? engine->game->input : NULL;
    application_provider *movement = application_provider_for(app, actor, QA_ROLE_MOVEMENT, "");
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    uint32_t slot;
    if (!input || input->in_command || (movement != source && !(input->profile.source && input->profile.source->present)) ||
        (arsenal != source && !weapons_capable(input)))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Original Q3 command requires qualified movement and weapon source regions");
    if ((is_guest(movement) && movement != source) ||
        !qa_q3_host_actor_slot(engine->game->host, actor, &slot, error) || slot >= 64 ||
        !engine->clients[slot].connected || !engine->clients[slot].begun ||
        engine->clients[slot].pending_retirement || !qa_actor_id_equal(engine->clients[slot].actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 command has no current source client");
    qa_q3_usercmd raw;
    qa_usercmd_to_q3(command, &raw);
    return guest_command(input, actor, slot, &raw, command, NULL, error);
}

static bool guest_move(qa_application *app, qa_actor_id actor,
                                     const qa_usercmd *command, bool *handled,
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
    if ((movement != guest && !(input->profile.source && input->profile.source->present)) ||
        (arsenal != guest && !weapons_capable(input)))
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
        .buttons = (int32_t)(command->kind == QA_RULESET_Q3 ? command->buttons : command->buttons & 5u),
        .weapon = arsenal == guest ? command->weapon : (uint8_t)player.weapon};
    qa_player_state control;
    if (!qa_application_control_read(app, actor, &control))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Guest command has no shared control view");
    qa_input_command_basis from = {.kind = command->kind}, to = {
        .kind = QA_RULESET_Q3, .words = true, .relative = true, .wrap_words = true};
    if (command->kind == QA_RULESET_Q3 || command->kind == QA_RULESET_Q2_CLASSIC) {
        from.words = from.relative = from.wrap_words = from.repack_words = true;
        for (unsigned i = 0; i < 3; ++i) from.delta_words[i] = command->kind == QA_RULESET_Q3 ?
            control.state.data.q3.delta_angle_words[i] : control.state.data.q2.delta_angle_shorts[i];
    } else if (command->kind == QA_RULESET_Q2_RERELEASE) {
        from.relative = true; from.delta_angles = control.state.data.q2r.delta_angles;
    }
    memcpy(to.delta_words, player.deltaAngles, sizeof(to.delta_words));
    qa_usercmd projected;
    qa_input_command_convert(command, NULL, &from, &to,
        (qa_input_axis_rule){.quantization = QA_INPUT_AXIS_NEAREST, .clamp = true,
            .minimum = -127, .maximum = 127, .float_product = true}, &projected);
    projected.server_time_ms = source.serverTime; projected.buttons = (uint32_t)source.buttons;
    projected.weapon = source.weapon;
    if (command->kind == QA_RULESET_Q3 && movement == guest) {
        projected.server_time_ms = command->server_time_ms;
        memcpy(projected.angle_words, command->angle_words, sizeof(projected.angle_words));
    }
    qa_usercmd_to_q3(&projected, &source);
    if (command->kind == QA_RULESET_Q2_RERELEASE) {
        if (command->buttons & 8u) source.upmove = 127;
        if (command->buttons & 16u) source.upmove = -127;
    } else if ((command->buttons & 2u) && source.upmove == 0) source.upmove = 127;
    *handled = true;
    return guest_command(input, actor, slot, &source, command, stage, error);
}

bool application_arsenal_guest_move(qa_application *app, qa_actor_id actor,
    const qa_usercmd *command, bool *handled, qa_error *error)
{ return guest_move(app, actor, command, handled, NULL, error); }

bool application_arsenal_guest_stage_move(qa_application *app, qa_actor_id actor,
    const qa_usercmd *command, const application_control_external_stage *stage,
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
        (movement == guest || (input->profile.source && input->profile.source->present)) &&
        (arsenal == guest || weapons_capable(input)) &&
        qa_q3_host_actor_slot(engine->game->host, actor, &slot, NULL) && slot < 64 &&
        engine->clients[slot].connected && engine->clients[slot].begun &&
        !engine->clients[slot].pending_retirement && qa_actor_id_equal(engine->clients[slot].actor, actor);
}
