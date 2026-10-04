#include "guest_q3_control.h"
#include "client_outputs.h"
#include "control_frame.h"

struct application_guest_q3_control {
    q3g_role *role;
    qa_qvm *vm;
    qa_qvm_image *image;
    const application_guest_input_profile *profile;
    qa_qvm_binding binding;
    application_guest_q3_control_scope *scope;
};

static bool word(const application_guest_q3_control *owner, uint32_t address,
    uint32_t *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_qvm_read(owner->vm, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_u32le(bytes); return true;
}

static bool vector(const application_guest_q3_control *owner, uint32_t address,
    qa_vec3 *out, qa_error *error)
{
    uint8_t bytes[12];
    if (!qa_qvm_read(owner->vm, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    return true;
}

static bool current(const application_guest_q3_control *owner,
    const application_guest_q3_control_scope *scope)
{
    if (!owner || !scope || owner->role->retired || owner->role->vm != owner->vm ||
        owner->role->image != owner->image ||
        owner->profile->source != application_q3_weapons_profile(owner->role->weapons) ||
        !qa_actors_get(qa_session_actors(owner->role->engine->provider->application->session), scope->actor))
        return false;
    qa_actor_id actor; qa_q3_host_game_data data;
    bool live = qa_q3_host_actor(owner->role->host, scope->slot, false, &actor, NULL) &&
        qa_actor_id_equal(actor, scope->actor) &&
        qa_q3_host_game_data_read(owner->role->host, &data) &&
        data.entities_address == scope->data.entities_address &&
        data.clients_address == scope->data.clients_address &&
        data.entity_stride == scope->data.entity_stride &&
        data.client_stride == scope->data.client_stride &&
        scope->slot < data.entity_count && scope->slot < data.client_count;
    if (!live) return false;
    uint64_t entity = data.entities_address + (uint64_t)scope->slot * data.entity_stride;
    uint32_t player, movement_player;
    return entity <= UINT32_MAX - owner->profile->source->client_pointer &&
        word(owner, (uint32_t)entity + owner->profile->source->client_pointer, &player, NULL) && player == scope->player &&
        word(owner, scope->movement, &movement_player, NULL) && movement_player == scope->player;
}

static bool retain_current(const application_guest_q3_control *owner,
    application_guest_q3_control_scope *scope)
{
    if (!scope->retired && !current(owner, scope)) scope->retired = true;
    return !scope->retired;
}

static bool observe_cancel(const qa_qvm_call *call,
    application_guest_q3_control_scope *scope, qa_error *error)
{
    bool cancelled;
    if (!qa_qvm_call_cancelled(call, &cancelled, error)) return false;
    scope->cancelled = scope->cancelled || cancelled; return true;
}

static bool write_word(application_guest_q3_control *owner,
    application_guest_q3_control_scope *scope, const qa_qvm_call *call,
    uint32_t address, uint32_t value, qa_error *error)
{
    if (!observe_cancel(call, scope, error)) return false;
    if (scope->cancelled || !retain_current(owner, scope)) return true;
    uint8_t bytes[4]; qa_store_u32le(bytes, value);
    if (!qa_qvm_write(owner->vm, address, (qa_bytes){bytes, sizeof(bytes)}, error)) return false;
    (void)retain_current(owner, scope); return observe_cancel(call, scope, error);
}

static bool write_vector(application_guest_q3_control *owner,
    application_guest_q3_control_scope *scope, const qa_qvm_call *call,
    uint32_t address, qa_vec3 value, qa_error *error)
{
    const float components[] = {value.x, value.y, value.z};
    for (size_t i = 0; i < 3; ++i) {
        uint32_t bits; memcpy(&bits, components + i, sizeof(bits));
        if (!write_word(owner, scope, call, address + (uint32_t)i * 4, bits, error)) return false;
        if (scope->retired || scope->cancelled) break;
    }
    return true;
}

static bool write_bounds(application_guest_q3_control *owner,
    application_guest_q3_control_scope *scope, const qa_qvm_call *call,
    qa_bounds value, qa_error *error)
{
    return write_vector(owner, scope, call, scope->movement + owner->profile->source->movement_mins, value.mins, error) &&
        write_vector(owner, scope, call, scope->movement + owner->profile->source->movement_maxs, value.maxs, error);
}

typedef struct body_trace {
    application_guest_q3_control *owner;
    application_guest_q3_control_scope *scope;
    const qa_qvm_call *call;
    qa_bounds requested, previous;
    uint32_t previous_duck, previous_height;
} body_trace;

static bool apply_bounds(body_trace *trace, bool accepted, qa_error *error)
{
    application_guest_q3_control *owner = trace->owner;
    application_guest_q3_control_scope *scope = trace->scope;
    if (!observe_cancel(trace->call, scope, error)) return false;
    if (scope->cancelled || !retain_current(owner, scope)) return true;
    qa_bounds bounds = accepted ? trace->requested : trace->previous;
    if (!accepted) {
        uint32_t flags;
        if (!word(owner, scope->player + 12, &flags, error) ||
            !write_word(owner, scope, trace->call, scope->player + 12,
                (flags & ~UINT32_C(1)) | trace->previous_duck, error) ||
            !write_word(owner, scope, trace->call, scope->player + 164, trace->previous_height, error)) return false;
    }
    if (!scope->retired && !scope->cancelled) {
        scope->accepted_bounds = bounds; scope->accepted = true;
        return write_bounds(owner, scope, trace->call, bounds, error);
    }
    return true;
}

static bool perform_trace(void *context, const qa_qvm_call *call, uint32_t at,
    qa_error *error)
{
    body_trace *trace = context;
    application_guest_q3_control *owner = trace->owner;
    application_guest_q3_control_scope *scope = trace->scope;
    if (!observe_cancel(call, scope, error)) return false;
    if (scope->cancelled || !retain_current(owner, scope)) return true;
    qa_vec3 origin;
    if (!vector(owner, scope->player + 20, &origin, error) ||
        !write_vector(owner, scope, call, at + 56, origin, error) ||
        !write_vector(owner, scope, call, at + 68, trace->requested.mins, error) ||
        !write_vector(owner, scope, call, at + 80, trace->requested.maxs, error)) return false;
    if (scope->cancelled || !retain_current(owner, scope)) return true;
    uint32_t pointer, mask, client;
    if (!word(owner, scope->movement + owner->profile->source->movement_trace_callback, &pointer, error) ||
        !word(owner, scope->movement + owner->profile->source->movement_trace_mask, &mask, error) ||
        !word(owner, scope->player + 140, &client, error)) return false;
    int32_t callback, mask_word, client_word;
    memcpy(&callback, &pointer, 4); memcpy(&mask_word, &mask, 4); memcpy(&client_word, &client, 4);
    const int32_t arguments[] = {(int32_t)at, (int32_t)(at + 56), (int32_t)(at + 68),
        (int32_t)(at + 80), (int32_t)(at + 56), client_word, mask_word};
    int32_t result;
    if (!qa_qvm_invoke_source_callback(call, owner->image, callback, arguments,
        sizeof(arguments) / sizeof(arguments[0]), &result, error)) return false;
    if (!observe_cancel(call, scope, error)) return false;
    if (scope->cancelled || !retain_current(owner, scope)) return true;
    uint32_t all_solid;
    return word(owner, at, &all_solid, error) && apply_bounds(trace, all_solid == 0, error);
}

static bool duck(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_guest_q3_control *owner = context;
    application_guest_q3_control_scope *scope = owner->scope;
    if (!scope) return qa_qvm_proceed(call, result, error);
    if (!observe_cancel(call, scope, error)) return false;
    if (scope->cancelled) { *result = 0; return true; }
    if (!retain_current(owner, scope)) { *result = 0; return qa_qvm_cancel(scope->client_call, error); }
    uint32_t movement;
    if (!word(owner, owner->profile->source->movement_global, &movement, error)) return false;
    if (movement != scope->movement) return qa_qvm_proceed(call, result, error);
    bool ok;
    if (scope->fixed_pose) {
        uint32_t flags;
        double height = fmod(trunc((double)scope->pose.view_height), 4294967296.0);
        if (height < 0) height += 4294967296.0;
        ok = word(owner, scope->player + 12, &flags, error) &&
            write_word(owner, scope, call, scope->player + 12,
                (flags & ~UINT32_C(1)) | (scope->fixed_crouched ? 1u : 0u), error) &&
            write_word(owner, scope, call, scope->player + 164, (uint32_t)height, error) &&
            write_bounds(owner, scope, call, scope->pose.bounds, error);
        if (ok) *result = 0;
    } else {
        if (scope->requested && !owner->profile->source->body_trace)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Original requested body bounds require their declared source trace");
        if (!owner->profile->source->body_trace) return qa_qvm_proceed(call, result, error);
        body_trace trace = {.owner = owner, .scope = scope, .call = call};
        uint32_t flags;
        if (!word(owner, scope->player + 12, &flags, error) ||
            !word(owner, scope->player + 164, &trace.previous_height, error)) return false;
        trace.previous_duck = flags & 1u;
        ok = qa_qvm_proceed(call, result, error);
        if (ok) ok = observe_cancel(call, scope, error);
        if (ok && !scope->cancelled && retain_current(owner, scope)) {
            if (scope->requested) trace.requested = scope->requested_bounds;
            else ok = vector(owner, scope->movement + owner->profile->source->movement_mins, &trace.requested.mins, error) &&
                vector(owner, scope->movement + owner->profile->source->movement_maxs, &trace.requested.maxs, error);
            trace.previous = scope->accepted ? scope->accepted_bounds : scope->current_bounds;
            bool expands = trace.requested.mins.x < trace.previous.mins.x ||
                trace.requested.mins.y < trace.previous.mins.y || trace.requested.mins.z < trace.previous.mins.z ||
                trace.requested.maxs.x > trace.previous.maxs.x || trace.requested.maxs.y > trace.previous.maxs.y ||
                trace.requested.maxs.z > trace.previous.maxs.z;
            if (ok) ok = expands ? qa_qvm_source_scratch(call, owner->image, 92,
                perform_trace, &trace, error) : apply_bounds(&trace, true, error);
        }
    }
    if (ok) {
        (void)retain_current(owner, scope);
        ok = observe_cancel(call, scope, error);
    }
    if (ok && scope->retired && !scope->cancelled) ok = qa_qvm_cancel(scope->client_call, error);
    return ok;
}

bool application_guest_q3_control_attach(q3g_role *role, const application_guest_input_profile *profile,
    application_guest_q3_control **out, qa_error *error)
{
    if (!role || !profile || !out || *out || role->kind != QA_QVM_GAME)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original body control requires an unattached GAME owner");
    if (!profile->source || !profile->source->present) return true;
    if (!profile->input_present || profile->source != application_q3_weapons_profile(role->weapons) ||
        !role->vm || !role->image || role->retired || qa_qvm_get_role(role->vm) != QA_QVM_GAME ||
        qa_qvm_get_abi(role->vm) != role->abi ||
        !qa_sha256_equal(qa_qvm_digest(role->vm), qa_qvm_image_digest(role->image)))
        return application_fail(error, QA_ERROR_FORMAT, "Original body control has no admitted QVM movement declaration");
    uint32_t scratch;
    if (profile->source->body_trace &&
        !qa_qvm_source_scratch_qualify(role->image, 92, &scratch, error)) return false;
    application_guest_q3_control *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating original body control owner");
    *owner = (application_guest_q3_control){.role = role, .vm = role->vm,
        .image = role->image, .profile = profile};
    if (!qa_qvm_bind_function(owner->vm, owner->profile->source->movement_duck, true, duck, owner, &owner->binding, error)) {
        free(owner); return false;
    }
    *out = owner; return true;
}

bool application_guest_q3_control_detach(application_guest_q3_control **address, qa_error *error)
{
    application_guest_q3_control *owner = address ? *address : NULL;
    if (!owner) return true;
    if (owner->scope || qa_qvm_active(owner->vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original body control still owns a source movement scope");
    if (!qa_qvm_unbind(owner->vm, owner->binding, error)) return false;
    free(owner); *address = NULL; return true;
}

bool application_guest_q3_control_supports_body(const application_guest_q3_control *owner)
{
    return owner && owner->profile->source->body_trace && application_guest_q3_control_supports_pose(owner);
}

bool application_guest_q3_control_supports_pose(const application_guest_q3_control *owner)
{
    return owner && owner->binding && !owner->role->retired &&
        owner->role->vm == owner->vm && owner->role->image == owner->image &&
        owner->profile->source == application_q3_weapons_profile(owner->role->weapons);
}

qa_qvm_binding application_guest_q3_control_binding(const application_guest_q3_control *owner)
{
    return owner ? owner->binding : 0;
}

bool application_guest_q3_control_descriptor(const application_guest_q3_control *owner,
    qa_qvm_saved_function *out, qa_error *error)
{
    if (!out || (owner && (owner->scope || !application_guest_q3_control_supports_pose(owner))))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original body control descriptor requires its idle admitted owner");
    *out = owner ? (qa_qvm_saved_function){owner->binding, owner->profile->source->movement_duck, true, duck, (void *)owner}
                 : (qa_qvm_saved_function){0};
    return true;
}

void application_guest_q3_control_restore_binding(application_guest_q3_control *owner,
    qa_qvm_binding binding)
{
    owner->binding = binding;
}

bool application_guest_q3_control_begin(application_guest_q3_control *owner, const qa_qvm_call *call,
    const qa_qvm_call *client,
    qa_actor_id actor, uint32_t slot, uint32_t player, uint32_t movement,
    const qa_movement_environment *equipment, application_guest_q3_control_scope *scope, qa_error *error)
{
    if (!scope) return application_fail(error, QA_ERROR_ARGUMENT, "Original body scope requires its destination");
    *scope = (application_guest_q3_control_scope){0};
    if (!owner) return true;
    if (!application_guest_q3_control_supports_pose(owner) || !call || call->vm != owner->vm ||
        !client || client->vm != owner->vm ||
        (client->instruction != owner->profile->client_think && client->instruction != owner->profile->run_client) ||
        call->instruction != owner->profile->source->movement_move || !movement || (movement & 3u) || player > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original body scope has no genuine source move");
    int32_t argument;
    if (!qa_qvm_call_argument(call, 0, &argument, error)) return false;
    if (!observe_cancel(call, scope, error)) return false;
    if (scope->cancelled) return true;
    if ((uint32_t)argument != movement || !qa_q3_host_game_data_read(owner->role->host, &scope->data) ||
        slot >= scope->data.client_count || slot >= scope->data.entity_count ||
        scope->data.entity_stride != owner->profile->source->entity_stride || scope->data.client_stride != owner->profile->source->client_stride ||
        scope->data.clients_address + (uint64_t)slot * owner->profile->source->client_stride != player)
        return application_fail(error, QA_ERROR_FORMAT, "Original body scope differs from its located source client");
    uint64_t entity = scope->data.entities_address + (uint64_t)slot * owner->profile->source->entity_stride;
    uint32_t pointer;
    if (entity > UINT32_MAX - owner->profile->source->client_pointer)
        return application_fail(error, QA_ERROR_FORMAT, "Original body source entity leaves QVM memory");
    if (!word(owner, (uint32_t)entity + owner->profile->source->client_pointer, &pointer, error)) return false;
    if (pointer != player)
        return application_fail(error, QA_ERROR_FORMAT, "Original body scope does not own its source player pointer");
    if (!word(owner, movement, &pointer, error)) return false;
    if (pointer != player)
        return application_fail(error, QA_ERROR_FORMAT, "Original body scope does not own its source player pointer");
    const uint32_t offsets[] = {owner->profile->source->movement_mins, owner->profile->source->movement_maxs, owner->profile->source->movement_trace_callback, owner->profile->source->movement_trace_mask};
    size_t offset_count = owner->profile->source->body_trace ? sizeof(offsets) / sizeof(offsets[0]) : 2;
    for (size_t i = 0; i < offset_count; ++i)
        if ((uint64_t)movement + offsets[i] + (i < 2 ? 12u : 4u) > qa_qvm_memory_size(owner->vm))
            return application_fail(error, QA_ERROR_FORMAT, "Original body scope leaves its source movement record");
    scope->actor = actor; scope->slot = slot; scope->player = player; scope->movement = movement;
    scope->client_call = client;
    if (!current(owner, scope))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original body scope actor is no longer current");
    qa_application *app = owner->role->engine->provider->application;
    qa_body_state body; application_client_outputs outputs;
    if (!qa_world_body_read(owner->role->engine->world, actor, &body, error) ||
        !observe_cancel(call, scope, error)) return false;
    if (scope->cancelled || !retain_current(owner, scope)) return true;
    if (!application_control_outputs(app, actor, &outputs, error)) return false;
    scope->current_bounds = body.bounds;
    scope->requested = outputs.has_body_bounds; scope->requested_bounds = outputs.body_bounds;
    if (equipment && equipment->fixed_pose) {
        if (!qa_vec_finite(equipment->pose.bounds.mins) || !qa_vec_finite(equipment->pose.bounds.maxs) ||
            !isfinite(equipment->pose.view_height))
            return application_fail(error, QA_ERROR_ARGUMENT, "Original fixed movement pose must be finite");
        scope->fixed_pose = true; scope->fixed_crouched = equipment->fixed_crouched; scope->pose = equipment->pose;
    }
    if (!observe_cancel(call, scope, error)) return false;
    if (scope->cancelled || !retain_current(owner, scope)) return true;
    scope->previous = owner->scope; scope->active = true; owner->scope = scope;
    return true;
}

bool application_guest_q3_control_end(application_guest_q3_control *owner,
    application_guest_q3_control_scope *scope, qa_error *error)
{
    if (!scope || !scope->active) return true;
    if (!owner || owner->scope != scope)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original body movement scopes completed out of order");
    owner->scope = scope->previous; scope->active = false; return true;
}
