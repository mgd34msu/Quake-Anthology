#include "internal.h"

static int16_t load_i16(const uint8_t *bytes, size_t offset)
{
    return (int16_t)qa_load_u16le(bytes + offset);
}

static void store_i16(uint8_t *bytes, size_t offset, int32_t value)
{
    qa_store_u16le(bytes + offset, (uint16_t)(int16_t)value);
}

static float load_f32(const uint8_t *bytes, size_t offset)
{
    return qa_load_f32le(bytes + offset);
}

static void store_f32_at(uint8_t *bytes, size_t offset, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    qa_store_u32le(bytes + offset, bits);
}

static qa_vec3 load_vec3(const uint8_t *bytes, size_t offset)
{
    return (qa_vec3){load_f32(bytes, offset), load_f32(bytes, offset + 4),
                     load_f32(bytes, offset + 8)};
}

static void store_vec3(uint8_t *bytes, size_t offset, qa_vec3 value)
{
    store_f32_at(bytes, offset, value.x);
    store_f32_at(bytes, offset + 4, value.y);
    store_f32_at(bytes, offset + 8, value.z);
}

static bool contact_address(qa_native_host *host, const qa_trace_result *trace,
                            qa_native_address *out, qa_error *error)
{
    if (trace->hit == QA_TRACE_HIT_NONE) {
        *out = 0;
        return true;
    }
    if (trace->hit == QA_TRACE_HIT_WORLD)
        return qa_native_entity_address(host->instance, 0, out, error);
    return native_host_address_for_actor(host, trace->actor, out, error);
}

typedef struct movement_bridge {
    qa_native_host *host;
    qa_movement_services source;
} movement_bridge;

static bool bridge_trace(void *context, const qa_trace_query *query,
                         qa_trace_result *result, qa_error *error)
{
    movement_bridge *bridge = context;
    if (bridge->source.trace)
        return bridge->source.trace(bridge->source.context, query, result, error);
    return qa_world_trace(bridge->host->world.world, query, result, error);
}

static bool bridge_contents(void *context, const qa_point_query *query,
                            qa_point_contents *result, qa_error *error)
{
    movement_bridge *bridge = context;
    if (bridge->source.point_contents)
        return bridge->source.point_contents(bridge->source.context, query, result, error);
    return qa_world_point_contents(bridge->host->world.world, query, result, error);
}

static qa_movement_control bridge_phase(void *context, qa_movement_phase phase,
                                        qa_movement_call *call, qa_error *error)
{
    movement_bridge *bridge = context;
    return bridge->source.phase
               ? bridge->source.phase(bridge->source.context, phase, call, error)
               : QA_MOVEMENT_CONTINUE;
}

static qa_movement_control bridge_touch(void *context, const qa_trace_result *trace,
                                        qa_movement_call *call, qa_error *error)
{
    movement_bridge *bridge = context;
    return bridge->source.touch
               ? bridge->source.touch(bridge->source.context, trace, call, error)
               : QA_MOVEMENT_CONTINUE;
}

static qa_movement_control bridge_effect(void *context, const qa_movement_effect *effect,
                                         qa_movement_call *call, qa_error *error)
{
    movement_bridge *bridge = context;
    return bridge->source.effect
               ? bridge->source.effect(bridge->source.context, effect, call, error)
               : QA_MOVEMENT_CONTINUE;
}

static bool bridge_firing(void *context, const qa_movement_call *call)
{
    movement_bridge *bridge = context;
    return bridge->source.firing && bridge->source.firing(bridge->source.context, call);
}

static bool bridge_is_bsp(void *context, const qa_trace_result *trace, bool *out, qa_error *error)
{
    movement_bridge *bridge = context;
    if (bridge->source.is_bsp)
        return bridge->source.is_bsp(bridge->source.context, trace, out, error);
    *out = trace->hit == QA_TRACE_HIT_WORLD;
    return true;
}

bool native_host_pmove(qa_native_host *host, qa_native_address address, qa_error *error)
{
    if (!host || host->profile != QA_NATIVE_Q2_GAME_API3 || !address)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "classic Q2 Pmove requires an API 3 source record");
    if (!host->movement.prepare)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                "classic Q2 Pmove input ownership is unbound");
    const native_host_classic_layout *layout = host->classic;
    uint8_t bytes[384];
    if (!native_host_read(host, address, bytes, layout->pmove.bytes, error))
        return false;
    qa_movement_input input = qa_movement_input_default(QA_MOVEMENT_Q2_CLASSIC,
                                                        (qa_actor_id){0});
    input.state.data.q2.type = qa_load_i32le(bytes);
    for (size_t index = 0; index < 3; ++index) {
        input.state.data.q2.origin_eighths[index] = load_i16(bytes, 4 + index * 2);
        input.state.data.q2.velocity_eighths[index] = load_i16(bytes, 10 + index * 2);
        input.state.data.q2.delta_angle_shorts[index] = load_i16(bytes, 20 + index * 2);
        input.command.angle_words[index] = load_i16(bytes, 30 + index * 2);
    }
    input.state.data.q2.flags = bytes[16];
    input.state.data.q2.time_eight_ms = bytes[17];
    input.state.data.q2.gravity = load_i16(bytes, 18);
    input.command.milliseconds = bytes[28];
    input.command.buttons = bytes[29];
    input.command.forward_move = load_i16(bytes, 36);
    input.command.side_move = load_i16(bytes, 38);
    input.command.up_move = load_i16(bytes, 40);
    input.command.impulse = bytes[42];
    input.command.light_level = bytes[43];
    input.snap_initial = qa_load_i32le(bytes + 44) != 0;
    input.current_bounds = (qa_bounds){load_vec3(bytes, layout->pmove.mins),
                                      load_vec3(bytes, layout->pmove.maxs)};
    input.has_current_bounds = true;
    input.shape = (qa_trace_shape){.kind = QA_SHAPE_BOX, .bounds = input.current_bounds};
    input.trace_policy = qa_collision_default_policy(QA_COLLISION_Q2);
    input.has_trace_policy = true;
    input.view_offset = (qa_vec3){0, 0, load_f32(bytes, layout->pmove.view_height)};
    if (!host->movement.prepare(host->movement.context, host, address, &input, error))
        return false;
    movement_bridge bridge = {.host = host, .source = host->movement.kernel};
    qa_movement_services services = {
        .context = &bridge,
        .trace = bridge_trace,
        .point_contents = bridge_contents,
        .phase = bridge_phase,
        .touch = bridge_touch,
        .effect = bridge_effect,
        .firing = bridge_firing,
        .is_bsp = bridge_is_bsp};
    qa_movement_result movement = {0};
    if (!qa_movement_move(&input, &services, &movement, error))
        return false;
    const qa_q2_movement_state *state = &movement.state.data.q2;
    qa_store_u32le(bytes, (uint32_t)state->type);
    for (size_t index = 0; index < 3; ++index) {
        store_i16(bytes, 4 + index * 2, state->origin_eighths[index]);
        store_i16(bytes, 10 + index * 2, state->velocity_eighths[index]);
        store_i16(bytes, 20 + index * 2, state->delta_angle_shorts[index]);
    }
    bytes[16] = (uint8_t)state->flags;
    bytes[17] = state->time_eight_ms;
    store_i16(bytes, 18, state->gravity);
    size_t contacts = movement.contact_count;
    if (contacts > 32)
        contacts = 32;
    qa_store_u32le(bytes + 48, (uint32_t)contacts);
    memset(bytes + layout->pmove.touches, 0, 32u * host->pointer_bytes);
    for (size_t index = 0; index < contacts; ++index) {
        qa_native_address entity;
        if (!contact_address(host, &movement.contacts[index].trace, &entity, error)) {
            qa_movement_result_free(&movement);
            return false;
        }
        if (!native_host_store_pointer(host, bytes + layout->pmove.touches +
                                                 index * host->pointer_bytes,
                                        entity, error)) {
            qa_movement_result_free(&movement);
            return false;
        }
    }
    store_vec3(bytes, layout->pmove.view_angles, movement.view_angles);
    store_f32_at(bytes, layout->pmove.view_height, movement.view_height);
    store_vec3(bytes, layout->pmove.mins, movement.bounds.mins);
    store_vec3(bytes, layout->pmove.maxs, movement.bounds.maxs);
    qa_native_address ground = 0;
    if (!contact_address(host, &movement.ground, &ground, error)) {
        qa_movement_result_free(&movement);
        return false;
    }
    if (!native_host_store_pointer(host, bytes + layout->pmove.ground, ground, error)) {
        qa_movement_result_free(&movement);
        return false;
    }
    qa_store_u32le(bytes + layout->pmove.water_type, (uint32_t)movement.water_type);
    qa_store_u32le(bytes + layout->pmove.water_level, (uint32_t)movement.water_level);
    bool ok = native_host_write(host, address, bytes, layout->pmove.bytes, error);
    if (ok && host->movement.commit)
        ok = host->movement.commit(host->movement.context, host, address, &movement, error);
    qa_movement_result_free(&movement);
    return ok;
}
