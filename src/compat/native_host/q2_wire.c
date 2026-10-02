#include "internal.h"
#include "qa/native_host_q2_wire.h"
#include <math.h>

static bool ready(qa_native_host *host, qa_error *error)
{
    return (host && host->kind == NATIVE_HOST_Q2_GAME && host->instance &&
        host->edict && !host->destroying && !host->restoring && !host->reconstruction &&
        !host->callback_depth && !host->filter_depth &&
        !qa_native_terminal(host->instance) && qa_native_can_destroy(host->instance) &&
        host->world.session && host->world.world &&
        qa_session_safe(host->world.session) && qa_world_idle(host->world.world)) ||
        native_host_fail(error, QA_ERROR_ARGUMENT, 0,
            "Q2 wire observation requires its idle installed GAME owner");
}

static bool binding_equal(qa_native_slot_binding a, qa_native_slot_binding b)
{
    return a.kind == b.kind && a.slot == b.slot && a.owner == b.owner &&
        a.source_slot == b.source_slot && qa_actor_id_equal(a.actor, b.actor);
}

static bool binding_current(qa_native_host *host, uint32_t slot,
    qa_native_slot_binding *out, qa_error *error)
{
    qa_native_slot_binding binding;
    if (!qa_native_slot(host->instance, slot, &binding, error)) return false;
    if (binding.kind != QA_NATIVE_SLOT_FREE) {
        const qa_actor_record *actor = qa_actors_get(
            qa_session_actors(host->world.session), binding.actor);
        if (!actor || binding.slot != slot || binding.owner != host->world.owner ||
            binding.source_slot != slot ||
            (binding.kind == QA_NATIVE_SLOT_OWNED &&
                (actor->owner != binding.owner || !actor->has_source || actor->source_slot != slot)) ||
            (binding.kind == QA_NATIVE_SLOT_WORLD &&
                (slot || !qa_actor_id_equal(binding.actor, host->world.world_actor))))
            return native_host_fail(error, QA_ERROR_FORMAT, slot,
                "Q2 public edict lost its full physical source binding");
    }
    *out = binding;
    return true;
}

static bool table_current(qa_native_host *host, const qa_native_entity_table *before,
    qa_error *error)
{
    qa_native_entity_table after;
    return ready(host, error) && qa_native_entity_table_get(host->instance, &after, error) &&
        ((after.base == before->base && after.stride == before->stride &&
            after.count == before->count && after.capacity == before->capacity) ||
            native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                "Q2 public entity table changed during observation"));
}

bool qa_native_host_q2_wire_count(qa_native_host *host, uint32_t *out, qa_error *error)
{
    qa_native_entity_table table;
    if (!out || !ready(host, error) ||
        !qa_native_entity_table_get(host->instance, &table, error)) return false;
    if (table.stride < host->edict->bytes || table.count > table.capacity)
        return native_host_fail(error, QA_ERROR_FORMAT, table.stride,
            "Q2 wire table is smaller than its admitted public prefix");
    *out = table.count;
    return true;
}

static void vector(const uint8_t *bytes, float out[3])
{
    for (size_t i = 0; i < 3; ++i) out[i] = qa_load_f32le(bytes + i * 4);
}

static bool entity_finite(const qa_q2_entity *state, qa_bounds bounds)
{
    for (size_t i = 0; i < 3; ++i)
        if (!isfinite(state->origin[i]) || !isfinite(state->angles[i]) ||
            !isfinite(state->old_origin[i])) return false;
    return qa_vec_finite(bounds.mins) && qa_vec_finite(bounds.maxs) &&
        isfinite(state->alpha) && isfinite(state->scale) &&
        isfinite(state->loop_volume) && isfinite(state->loop_attenuation);
}

bool qa_native_host_q2_wire_entity(qa_native_host *host, uint32_t slot,
    qa_native_host_q2_entity *out, qa_error *error)
{
    qa_native_entity_table table;
    qa_native_host_q2_entity value = {0};
    if (!out || !ready(host, error) ||
        !qa_native_entity_table_get(host->instance, &table, error)) return false;
    if (slot >= table.count || table.stride < host->edict->bytes)
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
            "Q2 wire edict is outside its physical source table");
    if (!binding_current(host, slot, &value.binding, error)) return false;
    if (value.binding.kind == QA_NATIVE_SLOT_FREE) {
        if (!table_current(host, &table, error)) return false;
        *out = value;
        return true;
    }
    qa_native_address address;
    uint8_t bytes[NATIVE_Q2_RR_EDICT_BYTES];
    if (!qa_native_entity_address(host->instance, slot, &address, error) ||
        !native_host_read(host, address, bytes, host->edict->bytes, error)) return false;
    const native_host_edict_layout *layout = host->edict;
    bool classic = host->profile == QA_NATIVE_Q2_GAME_API3;
    value.in_use = classic ? qa_load_i32le(bytes + layout->inuse) != 0 : bytes[layout->inuse] != 0;
    qa_linked_body linked;
    value.linked = classic ? qa_world_linked(host->world.world, value.binding.actor, &linked)
        : bytes[NATIVE_Q2_RR_LINKED] != 0;
    value.server_flags = qa_load_u32le(bytes + layout->flags);
    value.link_count = qa_load_u32le(bytes + layout->linkcount);
    if (slot < host->q2_lifetime_capacity && host->q2_lifetimes[slot].present) {
        const native_host_q2_lifetime *lifetime = &host->q2_lifetimes[slot];
        if (!qa_actor_id_equal(lifetime->actor, value.binding.actor) || !qa_vec_finite(lifetime->creation_origin))
            return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 creation metadata lost its full Source actor");
        value.creation_frame = lifetime->creation_frame; value.creation_origin = lifetime->creation_origin;
        value.creation_present = true;
        memcpy(value.origins, lifetime->origins, sizeof(value.origins));
    }
    value.areas[0] = qa_load_i32le(bytes + layout->area);
    value.areas[1] = qa_load_i32le(bytes + layout->area2);
    value.absolute_bounds.mins = qa_v3(qa_load_f32le(bytes + layout->absmin),
        qa_load_f32le(bytes + layout->absmin + 4), qa_load_f32le(bytes + layout->absmin + 8));
    value.absolute_bounds.maxs = qa_v3(qa_load_f32le(bytes + layout->absmax),
        qa_load_f32le(bytes + layout->absmax + 4), qa_load_f32le(bytes + layout->absmax + 8));
    value.bounds.mins = qa_v3(qa_load_f32le(bytes + layout->mins),
        qa_load_f32le(bytes + layout->mins + 4), qa_load_f32le(bytes + layout->mins + 8));
    value.bounds.maxs = qa_v3(qa_load_f32le(bytes + layout->maxs),
        qa_load_f32le(bytes + layout->maxs + 4), qa_load_f32le(bytes + layout->maxs + 8));
    value.cluster_count = -1;
    if (classic) {
        value.cluster_count = qa_load_i32le(bytes + layout->cluster_count);
        value.headnode = qa_load_i32le(bytes + layout->headnode);
        if (value.cluster_count < -1 || value.cluster_count > 16)
            return native_host_fail(error, QA_ERROR_FORMAT, slot,
                "Q2 public edict has an invalid source cluster extent");
        for (int32_t i = 0; i < value.cluster_count; ++i)
            value.clusters[i] = qa_load_i32le(bytes + layout->clusters + (size_t)i * 4);
    }
    qa_native_address owner = host->pointer_bytes == 4
        ? qa_load_u32le(bytes + layout->owner) : qa_load_u64le(bytes + layout->owner);
    if (owner && !qa_native_entity_slot(host->instance, owner, &value.owner_slot, error)) return false;
    qa_q2_entity *state = &value.state;
    state->number = qa_load_u32le(bytes);
    if (value.in_use && state->number != slot)
        return native_host_fail(error, QA_ERROR_FORMAT, slot,
            "Q2 public entity number differs from its physical edict");
    vector(bytes + 4, state->origin); vector(bytes + 16, state->angles);
    vector(bytes + 28, state->old_origin);
    state->modelindex = qa_load_u32le(bytes + 40); state->modelindex2 = qa_load_u32le(bytes + 44);
    state->modelindex3 = qa_load_u32le(bytes + 48); state->modelindex4 = qa_load_u32le(bytes + 52);
    state->frame = qa_load_u32le(bytes + 56); state->skinnum = qa_load_u32le(bytes + 60);
    state->effects = classic ? qa_load_u32le(bytes + 64) : qa_load_u64le(bytes + 64);
    size_t tail = classic ? 68 : 72;
    state->renderfx = qa_load_u32le(bytes + tail); state->solid = qa_load_u32le(bytes + tail + 4);
    state->sound = qa_load_u32le(bytes + tail + 8);
    state->event = classic ? qa_load_u32le(bytes + 80) : bytes[84];
    if (!classic) {
        state->alpha = qa_load_f32le(bytes + 88); state->scale = qa_load_f32le(bytes + 92);
        state->instance_bits = bytes[96]; state->loop_volume = qa_load_f32le(bytes + 100);
        state->loop_attenuation = qa_load_f32le(bytes + 104);
        state->owner = qa_load_u32le(bytes + 108); state->old_frame = qa_load_u32le(bytes + 112);
        state->morefx = (uint32_t)(state->effects >> 32);
    }
    if (!entity_finite(state, value.absolute_bounds) || !qa_vec_finite(value.bounds.mins) || !qa_vec_finite(value.bounds.maxs))
        return native_host_fail(error, QA_ERROR_FORMAT, slot,
            "Q2 public entity contains non-finite wire coordinates");
    qa_native_slot_binding after;
    if (!table_current(host, &table, error) || !binding_current(host, slot, &after, error)) return false;
    if (!binding_equal(value.binding, after))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
            "Q2 public entity binding changed during observation");
    *out = value;
    return true;
}

static void classic_player(const uint8_t *bytes, qa_q2_player *state)
{
    state->pmove.type = qa_load_i32le(bytes);
    for (size_t i = 0; i < 3; ++i) {
        state->pmove.origin[i] = (int16_t)qa_load_u16le(bytes + 4 + i * 2);
        state->pmove.velocity[i] = (int16_t)qa_load_u16le(bytes + 10 + i * 2);
        state->pmove.origin_f[i] = (float)state->pmove.origin[i] * .125f;
        state->pmove.velocity_f[i] = (float)state->pmove.velocity[i] * .125f;
        state->pmove.delta_angles[i] = (int16_t)qa_load_u16le(bytes + 20 + i * 2);
    }
    state->pmove.flags = bytes[16]; state->pmove.time = bytes[17];
    state->pmove.gravity = (int16_t)qa_load_u16le(bytes + 18);
    vector(bytes + 28, state->viewangles); vector(bytes + 40, state->viewoffset);
    vector(bytes + 52, state->kick_angles); vector(bytes + 64, state->gunangles);
    vector(bytes + 76, state->gunoffset);
    state->gunindex = qa_load_u32le(bytes + 88); state->gunframe = qa_load_u32le(bytes + 92);
    for (size_t i = 0; i < 4; ++i) state->blend[i] = qa_load_f32le(bytes + 96 + i * 4);
    state->fov = qa_load_f32le(bytes + 112); state->rdflags = qa_load_u32le(bytes + 116);
    for (size_t i = 0; i < 32; ++i) state->stats[i] = (int16_t)qa_load_u16le(bytes + 120 + i * 2);
}

static void rerelease_player(const uint8_t *bytes, qa_q2_player *state)
{
    state->pmove.type = qa_load_i32le(bytes);
    vector(bytes + 4, state->pmove.origin_f); vector(bytes + 16, state->pmove.velocity_f);
    state->pmove.flags = qa_load_u16le(bytes + 28); state->pmove.time = qa_load_u16le(bytes + 30);
    state->pmove.gravity = (int16_t)qa_load_u16le(bytes + 32);
    vector(bytes + 36, state->pmove.delta_angles_f); state->pmove.float_delta_angles = true;
    state->pmove.viewheight = (int8_t)bytes[48];
    vector(bytes + 52, state->viewangles); vector(bytes + 64, state->viewoffset);
    vector(bytes + 76, state->kick_angles); vector(bytes + 88, state->gunangles);
    vector(bytes + 100, state->gunoffset);
    state->gunindex = qa_load_u32le(bytes + 112); state->gunskin = qa_load_u32le(bytes + 116);
    state->gunframe = qa_load_u32le(bytes + 120); state->gunrate = qa_load_u32le(bytes + 124);
    for (size_t i = 0; i < 4; ++i) {
        state->blend[i] = qa_load_f32le(bytes + 128 + i * 4);
        state->damage_blend[i] = qa_load_f32le(bytes + 144 + i * 4);
    }
    state->fov = qa_load_f32le(bytes + 160); state->rdflags = bytes[164];
    for (size_t i = 0; i < 64; ++i) state->stats[i] = (int16_t)qa_load_u16le(bytes + 166 + i * 2);
    state->team_id = bytes[294];
}

static bool player_finite(const qa_q2_player *state, bool classic)
{
    const float *vectors[] = {state->pmove.origin_f, state->pmove.velocity_f,
        state->pmove.delta_angles_f, state->viewangles, state->viewoffset,
        state->kick_angles, state->gunangles, state->gunoffset};
    for (size_t vector_index = 0; vector_index < sizeof(vectors) / sizeof(*vectors); ++vector_index)
        for (size_t axis = 0; axis < 3; ++axis)
            if (!isfinite(vectors[vector_index][axis])) return false;
    for (size_t i = 0; i < 4; ++i)
        if (!isfinite(state->blend[i]) || !isfinite(state->damage_blend[i])) return false;
    return isfinite(state->fov) && state->pmove.type >= 0 && state->pmove.type <= (classic ? 4 : 6);
}

bool qa_native_host_q2_wire_player(qa_native_host *host, uint32_t slot,
    qa_actor_id actor, qa_q2_player *out, qa_error *error)
{
    qa_native_slot_binding before, after;
    qa_native_entity_table table;
    if (!out || !slot || !ready(host, error) ||
        !qa_native_entity_table_get(host->instance, &table, error) ||
        !binding_current(host, slot, &before, error)) return false;
    if (before.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(before.actor, actor))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
            "Q2 player state has no matching physical client actor");
    qa_buffer bytes = {0};
    if (!qa_native_host_q2_player_state(host, slot, &bytes, error)) return false;
    qa_q2_player value = {.clientnum = (int32_t)(slot - 1)};
    if (host->profile == QA_NATIVE_Q2_GAME_API3) classic_player(bytes.data, &value);
    else rerelease_player(bytes.data, &value);
    qa_buffer_free(&bytes);
    if (!player_finite(&value, host->profile == QA_NATIVE_Q2_GAME_API3))
        return native_host_fail(error, QA_ERROR_FORMAT, slot, "Q2 public player contains invalid Source wire fields");
    if (!table_current(host, &table, error) || !binding_current(host, slot, &after, error)) return false;
    if (!binding_equal(before, after))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
            "Q2 public player binding changed during observation");
    *out = value;
    return true;
}

bool qa_native_host_q2_wire_ping(qa_native_host *host, uint32_t slot,
    qa_actor_id actor, int32_t *out, qa_error *error)
{
    qa_native_slot_binding before, after;
    qa_native_entity_table table;
    qa_native_address entity;
    uint8_t pointer[8], bytes[4];
    if (!out || !slot || !ready(host, error) ||
        !qa_native_entity_table_get(host->instance, &table, error) ||
        !binding_current(host, slot, &before, error) ||
        before.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(before.actor, actor) ||
        !qa_native_entity_address(host->instance, slot, &entity, error))
        return false;
    size_t client_offset = host->profile == QA_NATIVE_Q2_GAME_API2023 ? NATIVE_Q2_RR_CLIENT :
        host->pointer_bytes == 4 ? 84u : 88u;
    if (!native_host_read(host, entity + client_offset, pointer, host->pointer_bytes, error)) return false;
    qa_native_address client = host->pointer_bytes == 4 ? qa_load_u32le(pointer) : qa_load_u64le(pointer);
    size_t offset = host->profile == QA_NATIVE_Q2_GAME_API2023 ? 296u : 184u;
    if (!client || client > UINT64_MAX - offset ||
        !native_host_read(host, client + offset, bytes, sizeof(bytes), error) ||
        !table_current(host, &table, error) || !binding_current(host, slot, &after, error)) return false;
    if (!binding_equal(before, after))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 ping lost its actual source player");
    *out = qa_load_i32le(bytes);
    return true;
}
