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

static bool import_ready(qa_native_host *host, qa_error *error)
{
    return (host && host->kind == NATIVE_HOST_Q2_GAME && host->instance && host->edict &&
        host->callback_depth && !host->destroying && !host->restoring && !host->reconstruction &&
        !qa_native_terminal(host->instance) && host->world.session && host->world.world) ||
        native_host_fail(error, QA_ERROR_ARGUMENT, 0,
            "Q2 import observation requires its actual executing GAME callback");
}

typedef enum q2_observation {
    Q2_OBSERVE_IDLE, Q2_OBSERVE_IMPORT, Q2_OBSERVE_END_FRAME, Q2_OBSERVE_RETURNED
} q2_observation;
static bool source_returned(qa_native_host *, qa_error *);

static bool frame_equal(const qa_source_frame *a,const qa_source_frame *b)
{
    return a->provider==b->provider&&a->kind==b->kind&&a->phase==b->phase&&a->number==b->number&&
        a->start_ns==b->start_ns&&a->elapsed_ns==b->elapsed_ns&&a->time_ns==b->time_ns;
}

static bool stage_ready(qa_native_host *host, qa_source_frame *out, qa_error *error)
{
    qa_source_frame frame;
    const qa_source_frame *completed=host?host->q2_observation_frame:NULL;
    qa_actor_owner clock_owner=completed?completed->provider:host?host->world.owner:0;
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !host->instance || !host->edict ||
        host->destroying || host->restoring || host->reconstruction || host->callback_depth ||
        host->filter_depth || qa_native_terminal(host->instance) || !qa_native_can_destroy(host->instance) ||
        !host->world.session || !host->world.world ||
        !clock_owner || !qa_session_active_frame(host->world.session, clock_owner, &frame) ||
        frame.provider != clock_owner || frame.phase != (completed?QA_FRAME_EXIT:QA_CLIENT_END_FRAME) ||
        (completed&&!frame_equal(completed,&frame)))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
            "Q2 Source observation requires its returned end-frame GAME stage");
    if (out) *out = frame;
    return true;
}

static bool completed_begin(qa_native_host *host,const qa_source_frame *frame,qa_error *error)
{
    if(!host||!frame||frame->phase!=QA_FRAME_EXIT||host->q2_observation_frame)
        return native_host_fail(error,QA_ERROR_ARGUMENT,0,
            "Q2 completed observation requires its distinct actual primary exit scope");
    host->q2_observation_frame=frame;
    if(stage_ready(host,NULL,error))return true;
    host->q2_observation_frame=NULL;return false;
}

static bool stage_current(qa_native_host *host, const qa_source_frame *before, qa_error *error)
{
    qa_source_frame after;
    if (!stage_ready(host, &after, error)) return false;
    return (after.provider == before->provider && after.kind == before->kind &&
        after.phase == before->phase && after.number == before->number &&
        after.start_ns == before->start_ns && after.elapsed_ns == before->elapsed_ns &&
        after.time_ns == before->time_ns) || native_host_fail(error, QA_ERROR_ARGUMENT, 0,
            "Q2 Source end-frame changed during observation");
}

static bool observation_ready(qa_native_host *host, q2_observation mode, qa_error *error)
{
    if (mode == Q2_OBSERVE_IMPORT) return import_ready(host, error);
    if (mode == Q2_OBSERVE_RETURNED) return source_returned(host, error);
    if (mode == Q2_OBSERVE_END_FRAME) return stage_ready(host, NULL, error);
    return ready(host, error);
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
    q2_observation mode, qa_error *error)
{
    qa_native_entity_table after;
    return observation_ready(host, mode, error) &&
        qa_native_entity_table_get(host->instance, &after, error) &&
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

static bool entity_read(qa_native_host *host, uint32_t slot,
    qa_native_host_q2_entity *out, q2_observation mode, qa_error *error)
{
    qa_native_entity_table table;
    qa_native_host_q2_entity value = {0};
    bool importing = mode == Q2_OBSERVE_IMPORT;
    if (!out || !observation_ready(host, mode, error) ||
        !qa_native_entity_table_get(host->instance, &table, error)) return false;
    if (slot >= (importing ? table.capacity : table.count) || table.stride < host->edict->bytes)
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
            "Q2 wire edict is outside its physical source table");
    if (!binding_current(host, slot, &value.binding, error)) return false;
    if (value.binding.kind == QA_NATIVE_SLOT_FREE) {
        if (importing) return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
            "Q2 import entity has no actual Source actor binding");
        if (!table_current(host, &table, mode, error)) return false;
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
    value.linked = classic ? (slot < host->q2_lifetime_capacity &&
        qa_actor_id_equal(host->q2_lifetimes[slot].actor, value.binding.actor) && host->q2_lifetimes[slot].linked) :
        bytes[NATIVE_Q2_RR_LINKED] != 0;
    value.server_flags = qa_load_u32le(bytes + layout->flags);
    value.solid = classic ? qa_load_i32le(bytes + layout->solid) : bytes[layout->solid];
    value.link_count = qa_load_u32le(bytes + layout->linkcount);
    if (slot < host->q2_lifetime_capacity && host->q2_lifetimes[slot].present) {
        const native_host_q2_lifetime *lifetime = &host->q2_lifetimes[slot];
        if (!qa_actor_id_equal(lifetime->actor, value.binding.actor) || !qa_vec_finite(lifetime->creation_origin))
            return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 creation metadata lost its full Source actor");
        value.creation_frame = lifetime->creation_frame; value.creation_origin = lifetime->creation_origin;
        value.creation_present = true;
        memcpy(value.origins, lifetime->origins, sizeof(value.origins));
    }
    if (!classic && slot < host->q2_lifetime_capacity &&
        host->q2_lifetimes[slot].bot_registered &&
        qa_actor_id_equal(host->q2_lifetimes[slot].actor, value.binding.actor)) {
        const uint8_t *sv = bytes + NATIVE_Q2_RR_SV;
        value.bot = (qa_native_host_q2_bot_state){.registered = true,
            .player = qa_load_u64le(bytes + NATIVE_Q2_RR_CLIENT) != 0,
            .flags = qa_load_u64le(sv + 8), .item_id = qa_load_i32le(sv + 24),
            .armor = qa_load_i32le(sv + 32), .health = qa_load_i32le(sv + 36),
            .max_health = qa_load_i32le(sv + 40), .weapon = qa_load_i32le(sv + 48),
            .team = qa_load_i32le(sv + 52), .view_height = qa_load_i32le(sv + 64),
            .water_level = sv[72],
            .view_angles = qa_v3(qa_load_f32le(sv + 76), qa_load_f32le(sv + 80), qa_load_f32le(sv + 84)),
            .velocity = qa_v3(qa_load_f32le(sv + 100), qa_load_f32le(sv + 104), qa_load_f32le(sv + 108)),
            .classname = qa_load_u64le(sv + 152), .targetname = qa_load_u64le(sv + 160)};
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
    if (!table_current(host, &table, mode, error) || !binding_current(host, slot, &after, error)) return false;
    if (!binding_equal(value.binding, after))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
            "Q2 public entity binding changed during observation");
    *out = value;
    return true;
}

bool qa_native_host_q2_wire_entity(qa_native_host *host, uint32_t slot,
    qa_native_host_q2_entity *out, qa_error *error)
{ return entity_read(host, slot, out, Q2_OBSERVE_IDLE, error); }

bool qa_native_host_q2_bot_entity(qa_native_host *host, uint32_t slot,
    qa_native_host_q2_entity *out, qa_error *error)
{ return entity_read(host, slot, out, Q2_OBSERVE_RETURNED, error); }

bool qa_native_host_q2_wire_entity_import(qa_native_host *host, uint32_t slot,
    qa_native_host_q2_entity *out, qa_error *error)
{ return entity_read(host, slot, out, Q2_OBSERVE_IMPORT, error); }

bool qa_native_host_q2_wire_entity_stage(qa_native_host *host, uint32_t slot,
    qa_native_host_q2_entity *out, qa_error *error)
{
    qa_source_frame frame;
    qa_native_host_q2_entity value;
    if (!out || !stage_ready(host, &frame, error) ||
        !entity_read(host, slot, &value, Q2_OBSERVE_END_FRAME, error) ||
        !stage_current(host, &frame, error)) return false;
    *out = value; return true;
}

bool qa_native_host_q2_wire_entity_completed(qa_native_host *host,const qa_source_frame *frame,
    uint32_t slot,qa_native_host_q2_entity *out,qa_error *error)
{
    if(!completed_begin(host,frame,error))return false;
    bool ok=qa_native_host_q2_wire_entity_stage(host,slot,out,error);
    host->q2_observation_frame=NULL;return ok;
}

static bool visible_binding(qa_native_host *host, uint32_t slot, qa_actor_id actor,
    bool viewer, qa_native_slot_binding *binding, qa_native_address *address, qa_error *error)
{
    qa_native_entity_table table;
    uint8_t inuse, pointer[8];
    if (!qa_native_entity_table_get(host->instance, &table, error)) return false;
    if ((viewer && !slot) || slot >= table.count || table.stride < host->edict->bytes ||
        !binding_current(host, slot, binding, error) || binding->kind == QA_NATIVE_SLOT_FREE ||
        !qa_actor_id_equal(binding->actor, actor))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
            "Q2 visibility lost its actual full Source edict binding");
    if (!qa_native_entity_address(host->instance, slot, address, error) ||
        !native_host_read_u8(host, *address + host->edict->inuse, &inuse, error)) return false;
    if (!inuse) return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
        "Q2 visibility names an inactive Source edict");
    if (viewer) {
        if (!native_host_read(host, *address + NATIVE_Q2_RR_CLIENT, pointer, host->pointer_bytes, error)) return false;
        qa_native_address client = host->pointer_bytes == 4 ? qa_load_u32le(pointer) : qa_load_u64le(pointer);
        if (!client) return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
            "Q2 visibility viewer has no actual Source client");
    }
    return true;
}

bool qa_native_host_q2_entity_visible(qa_native_host *host, uint32_t entity_slot,
    qa_actor_id entity, uint32_t viewer_slot, qa_actor_id viewer, bool *out, qa_error *error)
{
    qa_source_frame frame;
    if (!out || !host || host->profile != QA_NATIVE_Q2_GAME_API2023 || !entity.registry || !viewer.registry ||
        !host->engine.source_frame || !stage_ready(host, &frame, error))
        return native_host_fail(error, QA_ERROR_ARGUMENT, entity_slot,
            "Q2 visibility requires its real API2023 Source end-frame");
    uint64_t source_frame = host->engine.source_frame(host->engine.context);
    if (!qa_native_host_source_reconcile(host, error) || !stage_current(host, &frame, error)) return false;
    if (host->engine.source_frame(host->engine.context) != source_frame)
        return native_host_fail(error, QA_ERROR_ARGUMENT, entity_slot,
            "Q2 visibility reconciliation changed its actual Source counter");
    qa_native_slot_binding entity_before, viewer_before, entity_after, viewer_after;
    qa_native_address entity_address, viewer_address, current_entity, current_viewer;
    if (!visible_binding(host, entity_slot, entity, false, &entity_before, &entity_address, error) ||
        !visible_binding(host, viewer_slot, viewer, true, &viewer_before, &viewer_address, error)) return false;
    qa_native_value arguments[] = {
        {.type = QA_NATIVE_ADDRESS, .as.address = entity_address},
        {.type = QA_NATIVE_ADDRESS, .as.address = viewer_address}
    }, result = {0};
    if (!qa_native_call(host->instance, "Entity_IsVisibleToPlayer", arguments, 2, &result, error) ||
        !qa_native_host_source_reconcile(host, error) ||
        !stage_current(host, &frame, error) ||
        !visible_binding(host, entity_slot, entity, false, &entity_after, &current_entity, error) ||
        !visible_binding(host, viewer_slot, viewer, true, &viewer_after, &current_viewer, error)) return false;
    if (host->engine.source_frame(host->engine.context) != source_frame ||
        !binding_equal(entity_before, entity_after) || !binding_equal(viewer_before, viewer_after) ||
        result.type != QA_NATIVE_U8)
        return native_host_fail(error, QA_ERROR_ARGUMENT, entity_slot,
            "Q2 visibility callback changed its actual Source frame or actors");
    *out = result.as.u8 != 0; return true;
}

bool qa_native_host_q2_entity_visible_completed(qa_native_host *host,const qa_source_frame *frame,
    uint32_t entity_slot,qa_actor_id entity,uint32_t viewer_slot,qa_actor_id viewer,
    bool *out,qa_error *error)
{
    if(!completed_begin(host,frame,error))return false;
    bool ok=qa_native_host_q2_entity_visible(host,entity_slot,entity,viewer_slot,viewer,out,error);
    host->q2_observation_frame=NULL;return ok;
}

static bool source_returned(qa_native_host *host, qa_error *error)
{
    return (host && host->kind == NATIVE_HOST_Q2_GAME && host->instance && host->edict &&
        !host->destroying && !host->restoring && !host->reconstruction && !host->callback_depth &&
        !host->filter_depth && !qa_native_terminal(host->instance) && qa_native_can_destroy(host->instance) &&
        host->world.session && host->world.world) ||
        native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Q2 public prefix requires its returned actual GAME owner");
}

bool qa_native_host_q2_character_frame(qa_native_host *host, uint32_t slot,
    qa_actor_id actor, double *out, qa_error *error)
{
    qa_native_entity_table table, after;
    qa_native_slot_binding binding, current;
    qa_native_address address; uint8_t frame[4], inuse[4] = {0};
    if (!out || !actor.registry || !source_returned(host, error) ||
        !qa_native_entity_table_get(host->instance, &table, error)) return false;
    if (slot >= table.count || table.stride < host->edict->bytes ||
        !binding_current(host, slot, &binding, error) || binding.kind == QA_NATIVE_SLOT_FREE ||
        !qa_actor_id_equal(binding.actor, actor))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 CHARACTER frame lost its real Source edict");
    bool classic = host->profile == QA_NATIVE_Q2_GAME_API3;
    if (!qa_native_entity_address(host->instance, slot, &address, error) ||
        !native_host_read(host, address + 56, frame, sizeof(frame), error) ||
        !native_host_read(host, address + host->edict->inuse, inuse, classic ? 4u : 1u, error)) return false;
    if (!(classic ? qa_load_i32le(inuse) != 0 : inuse[0] != 0))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 CHARACTER frame names an inactive Source edict");
    if (!source_returned(host, error) || !qa_native_entity_table_get(host->instance, &after, error) ||
        !binding_current(host, slot, &current, error)) return false;
    if (table.base != after.base || table.stride != after.stride || table.count != after.count ||
        table.capacity != after.capacity || !binding_equal(binding, current))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 CHARACTER frame changed its actual Source binding");
    *out = (double)qa_load_i32le(frame); return true;
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
    qa_q2_player value;
    if (!qa_native_host_q2_player(host, slot, &value, error)) return false;
    if (!player_finite(&value, host->profile == QA_NATIVE_Q2_GAME_API3))
        return native_host_fail(error, QA_ERROR_FORMAT, slot, "Q2 public player contains invalid Source wire fields");
    if (!table_current(host, &table, Q2_OBSERVE_IDLE, error) || !binding_current(host, slot, &after, error)) return false;
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
        !table_current(host, &table, Q2_OBSERVE_IDLE, error) || !binding_current(host, slot, &after, error)) return false;
    if (!binding_equal(before, after))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 ping lost its actual source player");
    *out = qa_load_i32le(bytes);
    return true;
}
