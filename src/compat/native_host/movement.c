#include "internal.h"
#include <math.h>

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
    qa_native_address trace, contents, scratch;
} movement_bridge;

static qa_native_address bridge_pointer(const qa_native_host *host, const uint8_t *bytes)
{
    return host->pointer_bytes == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes);
}

static bool bridge_source_trace(movement_bridge *bridge, const qa_trace_query *query,
                                qa_trace_result *out, qa_error *error)
{
    qa_native_host *host = bridge->host;
    uint8_t vectors[48];
    store_vec3(vectors, 0, query->start); store_vec3(vectors, 12, query->shape.bounds.mins);
    store_vec3(vectors, 24, query->shape.bounds.maxs); store_vec3(vectors, 36, query->end);
    if (!native_host_write(host, bridge->scratch, vectors, sizeof(vectors), error)) return false;
    const qa_native_type parameters[4] = {
        {.kind = QA_NATIVE_ADDRESS, .count = 1}, {.kind = QA_NATIVE_ADDRESS, .count = 1},
        {.kind = QA_NATIVE_ADDRESS, .count = 1}, {.kind = QA_NATIVE_ADDRESS, .count = 1}};
    const qa_native_type fields[] = {
        {.kind = QA_NATIVE_I32, .count = 1}, {.kind = QA_NATIVE_I32, .count = 1},
        {.kind = QA_NATIVE_F32, .count = 1}, {.kind = QA_NATIVE_F32, .count = 3},
        {.kind = QA_NATIVE_F32, .count = 3}, {.kind = QA_NATIVE_F32, .count = 1},
        {.kind = QA_NATIVE_U8, .count = 1}, {.kind = QA_NATIVE_U8, .count = 1},
        {.kind = QA_NATIVE_U8, .count = 2}, {.kind = QA_NATIVE_ADDRESS, .count = 1},
        {.kind = QA_NATIVE_I32, .count = 1}, {.kind = QA_NATIVE_ADDRESS, .count = 1}};
    qa_native_module_info info = qa_native_module_describe(qa_native_get_module(host->instance));
    qa_native_signature signature = {.abi = info.image.target.abi,
        .parameters = parameters, .parameter_count = 4,
        .result = {.kind = QA_NATIVE_BYTES, .fields = fields,
                   .field_count = sizeof(fields) / sizeof(fields[0]), .count = 1}};
    qa_native_value arguments[4];
    for (size_t i = 0; i < 4; ++i)
        arguments[i] = (qa_native_value){.type = QA_NATIVE_ADDRESS,
                                       .as.address = bridge->scratch + i * 12};
    uint8_t bytes[72] = {0};
    qa_native_value result = {.type = QA_NATIVE_BYTES,
        .as.bytes = {bytes, host->classic->trace.bytes}};
    ++host->callback_depth;
    bool ok = qa_native_invoke(host->instance, bridge->trace, &signature, arguments, 4,
                               &result, error);
    --host->callback_depth;
    if (!ok) return false;
    qa_trace_result trace = {.family = QA_COLLISION_Q2,
        .all_solid = qa_load_i32le(bytes) != 0, .start_solid = qa_load_i32le(bytes + 4) != 0,
        .fraction = load_f32(bytes, 8), .end = load_vec3(bytes, 12),
        .plane = {.normal = load_vec3(bytes, 24), .distance = load_f32(bytes, 36),
                  .type = bytes[40], .signbits = bytes[41]},
        .contents = qa_load_i32le(bytes + host->classic->trace.contents)};
    if (!isfinite(trace.fraction) || trace.fraction < 0 || trace.fraction > 1 ||
        !qa_vec_finite(trace.end) || !qa_vec_finite(trace.plane.normal) ||
        !isfinite(trace.plane.distance))
        return native_host_fail(error, QA_ERROR_FORMAT, 0, "Q2 Pmove trace returned invalid geometry");
    trace.contact = trace.fraction < 1 || trace.all_solid || trace.start_solid;
    trace.contact_plane = trace.plane;
    qa_native_address entity = bridge_pointer(host, bytes + host->classic->trace.entity);
    if (entity) {
        uint32_t slot;
        if (!native_host_actor_for_address(host, entity, true, &trace.actor, &slot, error)) return false;
        trace.hit = slot ? QA_TRACE_HIT_ACTOR : QA_TRACE_HIT_WORLD;
    }
    qa_native_address surface = bridge_pointer(host, bytes + host->classic->trace.surface);
    if (surface) {
        uint8_t record[24];
        if (!native_host_read(host, surface, record, sizeof(record), error)) return false;
        memcpy(trace.surface.name, record, 16); trace.surface.name[16] = 0;
        trace.surface.flags = trace.surface_flags = qa_load_i32le(record + 16);
        trace.surface.value = qa_load_i32le(record + 20); trace.has_surface = true;
    }
    *out = trace;
    return true;
}

static bool bridge_trace(void *context, const qa_trace_query *query,
                         qa_trace_result *result, qa_error *error)
{
    movement_bridge *bridge = context;
    if (bridge->trace) return bridge_source_trace(bridge, query, result, error);
    if (bridge->source.trace)
        return bridge->source.trace(bridge->source.context, query, result, error);
    return qa_world_trace(bridge->host->world.world, query, result, error);
}

static bool bridge_contents(void *context, const qa_point_query *query,
                            qa_point_contents *result, qa_error *error)
{
    movement_bridge *bridge = context;
    if (bridge->contents) {
        uint8_t bytes[12]; store_vec3(bytes, 0, query->point);
        if (!native_host_write(bridge->host, bridge->scratch, bytes, sizeof(bytes), error)) return false;
        qa_native_module_info info = qa_native_module_describe(qa_native_get_module(bridge->host->instance));
        qa_native_type parameter = {.kind = QA_NATIVE_ADDRESS, .count = 1};
        qa_native_signature signature = {.abi = info.image.target.abi,
            .parameters = &parameter, .parameter_count = 1,
            .result = {.kind = QA_NATIVE_I32, .count = 1}};
        qa_native_value argument = {.type = QA_NATIVE_ADDRESS, .as.address = bridge->scratch};
        qa_native_value value = {.type = QA_NATIVE_I32};
        ++bridge->host->callback_depth;
        bool ok = qa_native_invoke(bridge->host->instance, bridge->contents, &signature,
                                  &argument, 1, &value, error);
        --bridge->host->callback_depth;
        if (!ok) return false;
        *result = (qa_point_contents){.family = QA_COLLISION_Q2,
            .contents = value.as.i32, .stored = value.as.i32, .merged = value.as.i32};
        return true;
    }
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
    bridge.trace = bridge_pointer(host, bytes + layout->pmove.bytes - 2 * host->pointer_bytes);
    bridge.contents = bridge_pointer(host, bytes + layout->pmove.bytes - host->pointer_bytes);
    if ((bridge.trace || bridge.contents) &&
        !qa_native_allocate(host->instance, 48, INT32_MIN + 8, &bridge.scratch, error)) return false;
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
    bool moved = qa_movement_move(&input, &services, &movement, error);
    if (bridge.scratch) {
        qa_error cleanup = {0};
        bool freed = qa_native_free(host->instance, bridge.scratch, &cleanup);
        if (!freed && moved) { moved = false; if (error) *error = cleanup; }
    }
    if (!moved) {
        qa_movement_result_free(&movement);
        return false;
    }
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
