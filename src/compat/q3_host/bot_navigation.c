#include "bot_records.h"
#include "qa/bot_runtime.h"
#include "qa/bot_navigation_source.h"

typedef struct bsp_key { q3_call *call; uint64_t address; } bsp_key;

static bool compare_key(void *opaque, qa_bytes candidate, bool *equal, qa_error *error)
{
    bsp_key *key = opaque;
    if (!key->address) return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 BSP epair key is null");
    *equal = false;
    for (size_t i = 0; i <= candidate.size; ++i) {
        uint8_t byte;
        if (i > UINT64_MAX - key->address || !q3_read(key->call, key->address + i, &byte, 1, error)) return false;
        if (byte != (i == candidate.size ? 0 : candidate.data[i])) return true;
    }
    *equal = true; return true;
}

static bool bsp_query(q3_call *call, qa_bot_runtime *runtime, int32_t *result, qa_error *error)
{
    const qa_entities *entities = qa_bot_runtime_bsp(runtime);
    int32_t number = q3_integer(call, 0);
    if (call->service == 310) {
        if (number == INT32_MAX) return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 BSP entity increment overflows");
        *result = qa_bot_bsp_next(entities, number); return true;
    }
    uint64_t address = call->arguments[2];
    q3_record record;
    const uint8_t zero = 0;
    if (call->service == 311) {
        if (!q3_write(call, address, (qa_bytes){&zero, 1}, error)) return false;
    } else {
        if (!q3_record_open(call, address, call->service == 312 ? 12 : 4, &record, error)) return false;
        if (call->service == 312) {
            if (!q3_write_vector(call, address, qa_v3(0, 0, 0), error)) return false;
        } else if (!q3_write_word(call, address, 0, error)) return false;
    }
    bool admitted = entities && number > 0 && (size_t)number <= entities->count;
    if (!admitted && call->host->options.common.print)
        call->host->options.common.print(call->host->options.common.context, "bsp entity out of range\n");
    bsp_key key = {call, call->arguments[1]};
    qa_entity_property property; bool found = false;
    if (admitted && !qa_bot_bsp_lookup(entities, number, compare_key, &key, &property, &found, error)) return false;
    *result = found;
    if (call->service == 311) {
        if (!found) return true;
        int32_t capacity = q3_integer(call, 3);
        if (capacity <= 0) return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 matched BSP output has invalid capacity");
        if (!q3_record_open(call, address, (size_t)capacity, &record, error)) return false;
        size_t count = property.value.size;
        const uint8_t *end = count ? memchr(property.value.data, 0, count) : NULL;
        if (end) count = (size_t)(end - property.value.data);
        if (count >= (size_t)capacity) count = (size_t)capacity - 1;
        uint8_t *value = count ? qa_arena_alloc(&call->host->scratch, count, 1, error) : NULL;
        if (count && !value) return false;
        if (count) memcpy(value, property.value.data, count);
        uint8_t *cleared = qa_arena_alloc(&call->host->scratch, (size_t)capacity, 1, error);
        if (!cleared) return false;
        memset(cleared, 0, (size_t)capacity);
        if (!q3_write(call, address, (qa_bytes){cleared, (size_t)capacity}, error)) return false;
        for (size_t i = 0; i < count; ++i)
            if (!q3_write(call, address + i, (qa_bytes){value + i, 1}, error)) return false;
        return true;
    }
    if (call->service == 312) {
        qa_vec3 value = {0};
        return (!found || qa_bot_bsp_parse_vector(property.value, &value, error)) &&
            q3_write_vector(call, address, value, error);
    }
    if (call->service == 313) {
        float value = 0;
        return (!found || qa_bot_bsp_parse_float(property.value, &value, error)) &&
            q3_write_float(call, address, value, error);
    }
    int32_t value = 0;
    return (!found || qa_bot_bsp_parse_integer(property.value, &value, error)) &&
        q3_write_word(call, address, (uint32_t)value, error);
}

static bool predict_movement(q3_call *call, qa_bot_runtime *runtime, int32_t *result, qa_error *error)
{
    uint32_t presence = (uint32_t)q3_integer(call, 3);
    if (presence != 2 && presence != 4)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 prediction requires a supported presence type");
    qa_bot_movement_prediction_query query = {.presence = presence, .on_ground = q3_integer(call, 4) != 0,
        .command_frames = q3_integer(call, 7), .maximum_frames = q3_integer(call, 8),
        .frame_time = q3_float(call, 9), .stop_events = (uint32_t)q3_integer(call, 10),
        .stop_area = (uint32_t)q3_integer(call, 11)};
    qa_bot_movement_prediction prediction;
    int32_t client;
    if(!q3_bot_client_number(call,q3_integer(call,1),&client,error)) return false;
    if (!q3_vector(call, call->arguments[2], &query.origin, error) ||
        !q3_vector(call, call->arguments[5], &query.velocity, error) ||
        !q3_vector(call, call->arguments[6], &query.command_move, error) ||
        !qa_bot_runtime_predict_movement(runtime, client, &query, &prediction, error)) return false;
    uint32_t entity = 0;
    if (prediction.trace.actor.registry &&
        !qa_q3_host_actor_slot(call->host, prediction.trace.actor, &entity, error)) return false;
    uint64_t address = call->arguments[0]; q3_record record;
    if (!q3_record_open(call, address, 84, &record, error) ||
        !q3_write_vector(call, address, prediction.end, error) ||
        !q3_write_word(call, address + 12, prediction.end_area, error) ||
        !q3_write_vector(call, address + 16, prediction.velocity, error) ||
        !q3_write_word(call, address + 28, prediction.trace.start_solid, error) ||
        !q3_write_float(call, address + 32, prediction.trace.fraction, error) ||
        !q3_write_vector(call, address + 36, prediction.trace.end, error)) return false;
    const uint32_t words[] = {entity, prediction.trace.last_area, prediction.trace.area, prediction.trace.plane,
        prediction.presence, prediction.stop_event};
    for (size_t i = 0; i < 6; ++i)
        if (!q3_write_word(call, address + 48 + i * 4, words[i], error)) return false;
    bool ok = call->host->options.abi == QA_QVM_Q3_116N ?
        q3_write_float(call, address + 72, (float)prediction.end_contents, error) :
        q3_write_word(call, address + 72, (uint32_t)prediction.end_contents, error);
    if (!ok || !q3_write_float(call, address + 76, prediction.time, error) ||
        !q3_write_word(call, address + 80, prediction.frames, error)) return false;
    *result = prediction.succeeded; return true;
}

static bool entity_info(q3_call *call, qa_bot_runtime *runtime, qa_error *error)
{
    qa_bot_entity_info info; bool found;
    int32_t number;
    if(!q3_bot_entity_number(call,q3_integer(call,0),&number,error) ||
       !qa_bot_runtime_entity(runtime, number, &info, &found, error)) return false;
    q3_record record;
    uint64_t address = call->arguments[1];
    if (!q3_record_open(call, address, 140, &record, error)) return false;
    if (!found) {
        const uint8_t zero[140] = {0};
        return q3_write(call, address, (qa_bytes){zero, sizeof(zero)}, error);
    }
    const qa_bot_entity_update *state = &info.state;
    if (!q3_write_word(call, address, info.valid, error) ||
        !q3_write_word(call, address + 4, (uint32_t)state->type, error) ||
        !q3_write_word(call, address + 8, (uint32_t)state->flags, error) ||
        !q3_write_float(call, address + 12, info.last_update_time, error) ||
        !q3_write_float(call, address + 16, info.update_interval, error) ||
        !q3_write_word(call, address + 20, (uint32_t)q3_integer(call,0), error)) return false;
    const qa_vec3 vectors[] = {state->origin, state->angles, state->old_origin,
        info.last_visible_origin, state->mins, state->maxs};
    for (size_t i = 0; i < 6; ++i)
        if (!q3_write_vector(call, address + 24 + i * 12, vectors[i], error)) return false;
    const int32_t words[] = {state->ground_entity, state->solid, state->model_index, state->model_index2,
        state->frame, state->event, state->event_parameter, state->powerups, state->weapon,
        state->legs_animation, state->torso_animation};
    for (size_t i = 0; i < 11; ++i)
        if (!q3_write_word(call, address + 96 + i * 4, (uint32_t)words[i], error)) return false;
    return true;
}

static bool areas(q3_call *call, qa_bot_navigation *navigation, int32_t *result, qa_error *error)
{
    bool trace = call->service == 308;
    if (trace && !q3_write_word(call, call->arguments[2], 0, error)) return false;
    int32_t maximum = q3_integer(call, trace ? 4 : 3);
    const qa_nav_graph_view *graph = qa_navigation_graph(qa_bot_navigation_runtime(navigation));
    bool aas = qa_nav_asset_aas(graph->asset) != NULL;
    if (trace && maximum < 0) return q3_fail(error, QA_ERROR_ARGUMENT, 0, "negative Q3 area trace capacity");
    if (trace && aas && maximum == 0) { *result = 0; return true; }
    qa_vec3 first, second;
    if (!q3_vector(call, call->arguments[0], &first, error) ||
        !q3_vector(call, call->arguments[1], &second, error)) return false;
    size_t capacity = maximum > 0 ? (size_t)maximum : 0;
    q3_crossings *lease = NULL;
    uint32_t *storage = NULL;
    size_t count = 0;
    bool ok;
    if (trace) {
        lease = call->host->crossings;
        while (lease && lease->busy) lease = lease->next;
        if (!lease) {
            lease = calloc(1, sizeof(*lease));
            if (!lease) return q3_fail(error, QA_ERROR_MEMORY, 0, "retaining Q3 area trace output");
            lease->next = call->host->crossings; call->host->crossings = lease;
        }
        lease->busy = true;
        ok = qa_bot_navigation_trace_collect(navigation, first, second, capacity, &lease->storage, error);
        count = lease->storage.count;
    } else {
        if (capacity > graph->node_count) capacity = graph->node_count;
        if (capacity > SIZE_MAX / sizeof(*storage))
            return q3_fail(error, QA_ERROR_MEMORY, 0, "Q3 area query is too large");
        storage = capacity ? qa_arena_alloc(&call->host->scratch, capacity * sizeof(*storage),
            _Alignof(uint32_t), error) : NULL;
        if (capacity && !storage) return false;
        ok = qa_bot_navigation_bbox_areas(navigation, (qa_bounds){first, second}, storage, capacity, &count, error);
    }
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_aas_crossing *crossed = trace ? lease->storage.data + i : NULL;
        uint32_t area = trace ? crossed->area : storage[i];
        if (i > (UINT64_MAX - call->arguments[2]) / 4) {
            ok = q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 trace area offset exceeds allocation"); break;
        }
        ok = q3_write_word(call, call->arguments[2] + i * 4, area, error);
        if (!ok) break;
        if (trace && call->arguments[3]) {
            q3_record admitted;
            if (i > (UINT64_MAX - call->arguments[3]) / 12) {
                ok = q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 trace point offset exceeds allocation"); break;
            }
            uint64_t address = call->arguments[3] + i * 12;
            ok = q3_record_open(call, address, 12, &admitted, error) &&
                q3_write_vector(call, address, crossed->point, error);
        }
    }
    if (lease) lease->busy = false;
    if (ok) *result = (int32_t)count;
    return ok;
}

static bool info(q3_call *call, qa_bot_navigation *navigation, int32_t *result, qa_error *error)
{
    qa_bot_nav_area_info area;
    uint32_t number = (uint32_t)q3_integer(call, 0);
    if (!call->arguments[1] || !qa_bot_navigation_area_info(navigation, number, &area)) return true;
    uint64_t address = call->arguments[1]; q3_record record;
    if (!q3_record_open(call, address, 52, &record, error)) return false;
    const uint32_t words[] = {area.area.contents, area.area.flags, area.area.presence, (uint32_t)area.area.cluster};
    for (size_t i = 0; i < 4; ++i)
        if (!q3_write_word(call, address + i * 4, words[i], error)) return false;
    if (!q3_write_vector(call, address + 16, area.bounds.mins, error) ||
        !q3_write_vector(call, address + 28, area.bounds.maxs, error) ||
        !q3_write_vector(call, address + 40, area.origin, error)) return false;
    *result = 52; return true;
}

static bool route_prediction(q3_call *call, qa_bot_navigation *navigation,
                               int32_t *result, qa_error *error)
{
    qa_bot_route_prediction_query query = {.route = {.area = (uint32_t)q3_integer(call, 1),
        .goal_area = (uint32_t)q3_integer(call, 3), .travel_flags = (uint32_t)q3_integer(call, 4), .has_origin = true},
        .maximum_areas = q3_integer(call, 5), .maximum_time = q3_integer(call, 6),
        .stop_events = (uint32_t)q3_integer(call, 7), .stop_contents = (uint32_t)q3_integer(call, 8),
        .stop_travel_flags = (uint32_t)q3_integer(call, 9), .stop_area = (uint32_t)q3_integer(call, 10)};
    q3_bot_memory memory = {call, call->arguments[2]};
    qa_bot_vector_source origin = {.context = &memory, .read = q3_bot_vector_read};
    qa_bot_route_source_prediction source;
    if (!qa_bot_navigation_predict_route_from(navigation, &query, &origin, &source, error)) return false;
    qa_bot_route_prediction prediction = source.value;
    uint64_t address = call->arguments[0]; q3_record record;
    if (!q3_record_open(call, address, 36, &record, error)) return false;
    q3_bot_memory destination = {call, address};
    qa_bot_vector_target target = {.context = &destination, .write = q3_bot_vector_write};
    qa_bot_vector_source value = {.value = &prediction.end_position};
    if (!qa_bot_vector_write(&target, source.end_is_origin ? &origin : &value, error)) return false;
    const uint32_t words[] = {prediction.end_area, prediction.stop_event,
        prediction.end_contents, prediction.end_travel_flags};
    for (size_t i = 0; i < 4; ++i)
        if (!q3_write_word(call, address + 12 + i * 4, words[i], error)) return false;
    if (!q3_write_word(call, address + 32, (uint32_t)prediction.time, error)) return false;
    *result = prediction.succeeded; return true;
}

static bool alternatives(q3_call *call, qa_bot_navigation *navigation,
                            int32_t *result, qa_error *error)
{
    qa_bot_nav_route_query query = {.area = (uint32_t)q3_integer(call, 1),
        .goal_area = (uint32_t)q3_integer(call, 3), .travel_flags = (uint32_t)q3_integer(call, 4), .has_origin = true};
    if (!query.area || !query.goal_area) return true;
    if (!q3_vector(call, call->arguments[0], &query.origin, error)) return false;
    int32_t maximum = q3_integer(call, 6);
    size_t capacity = maximum > 0 ? (size_t)maximum : 1;
    const qa_nav_graph_view *graph = qa_navigation_graph(qa_bot_navigation_runtime(navigation));
    if (capacity > graph->node_count) capacity = graph->node_count;
    if (capacity > SIZE_MAX / sizeof(qa_bot_alternative_goal))
        return q3_fail(error, QA_ERROR_MEMORY, 0, "Q3 alternative route output is too large");
    qa_bot_alternative_goal *goals = capacity ? qa_arena_alloc(&call->host->scratch,
        capacity * sizeof(*goals), _Alignof(qa_bot_alternative_goal), error) : NULL;
    if (capacity && !goals) return false;
    size_t count;
    if (!qa_bot_navigation_alternatives(navigation, &query, (uint32_t)q3_integer(call, 7),
                                         maximum, goals, capacity, &count, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        uint64_t address = call->arguments[5] + i * 24; q3_record record;
        if (!q3_record_open(call, address, 22, &record, error) ||
            !q3_write_vector(call, address, goals[i].origin, error) ||
            !q3_write_word(call, address + 12, goals[i].area, error)) return false;
        const uint32_t times[] = {goals[i].start_time, goals[i].goal_time, goals[i].extra_time};
        for (size_t j = 0; j < 3; ++j) {
            uint8_t bytes[2]; qa_store_u16le(bytes, (uint16_t)times[j]);
            if (!q3_write(call, address + 16 + j * 2, (qa_bytes){bytes, sizeof(bytes)}, error)) return false;
        }
    }
    *result = (int32_t)count; return true;
}

q3_service_result q3_bot_navigation(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_GAME) return Q3_UNHANDLED;
    int32_t code = call->service;
    if ((code < 300 || code > 318) && (code < 575 || code > 577)) return Q3_UNHANDLED;
    qa_bot_runtime *runtime = q3_bot_runtime(call);
    if (code == 304 && !runtime) return Q3_COMPLETED;
    if (!runtime) { q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 bot runtime is unbound"); return Q3_FAILED; }
    if (code == 306) { *result = q3_float_bits(qa_bot_runtime_time(runtime)); return Q3_COMPLETED; }
    if (code == 303) return entity_info(call, runtime, error) ? Q3_COMPLETED : Q3_FAILED;
    if (code >= 310 && code <= 314) return bsp_query(call, runtime, result, error) ? Q3_COMPLETED : Q3_FAILED;
    if (code == 318) return predict_movement(call, runtime, result, error) ? Q3_COMPLETED : Q3_FAILED;
    if ((code == 304 || code == 316 || code == 577) &&
        (!qa_bot_runtime_initialized(runtime) || !qa_bot_runtime_loaded(runtime))) return Q3_COMPLETED;
    qa_bot_navigation *navigation = qa_bot_runtime_navigation(runtime, -1);
    if (code == 304 || code == 316 || code == 577) {
        bool ready = navigation && qa_navigation_graph(qa_bot_navigation_runtime(navigation))->node_count != 0;
        if (code == 304) { *result = ready; return Q3_COMPLETED; }
        if (!ready) return Q3_COMPLETED;
    }
    if (!navigation) { q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 bot navigation is unbound"); return Q3_FAILED; }
    bool ok = true; qa_vec3 point;
    switch (code) {
    case 300: {
        uint32_t number = (uint32_t)q3_integer(call, 0);
        bool previous, overridden;
        if (!qa_navigation_enabled(qa_bot_navigation_runtime(navigation),
                qa_bot_navigation_node(navigation, number), &previous, &overridden)) break;
        int32_t enabled = q3_integer(call, 1);
        if (enabled >= 0) ok = qa_bot_navigation_enable(navigation, number, enabled != 0, &previous, error);
        *result = previous; break;
    }
    case 301: case 308: ok = areas(call, navigation, result, error); break;
    case 302: ok = info(call, navigation, result, error); break;
    case 305: {
        int32_t presence = q3_integer(call, 0);
        if (presence != 2 && presence != 4 && call->host->options.common.print)
            call->host->options.common.print(call->host->options.common.context,
                "AAS_PresenceTypeBoundingBox: unknown presence type\n");
        qa_bounds bounds = qa_bot_navigation_presence(navigation, presence == 2 ? 2 : 4);
        q3_record record;
        ok = q3_record_open(call, call->arguments[1], 12, &record, error) &&
             q3_write_vector(call, call->arguments[1], bounds.mins, error) &&
             q3_record_open(call, call->arguments[2], 12, &record, error) &&
             q3_write_vector(call, call->arguments[2], bounds.maxs, error); break;
    }
    case 307: {
        uint32_t area;
        ok = q3_vector(call, call->arguments[0], &point, error) &&
             qa_bot_navigation_point(navigation, point, &area, error);
        if (ok) *result = (int32_t)area; break;
    }
    case 309:
        ok = q3_vector(call, call->arguments[0], &point, error) &&
             qa_bot_navigation_contents(navigation, point, result, error); break;
    case 315: *result = (int32_t)qa_bot_navigation_area(navigation, (uint32_t)q3_integer(call, 0)).reach_count; break;
    case 316: {
        if (!qa_bot_runtime_initialized(runtime)) break;
        qa_bot_nav_route_query query = {.area = (uint32_t)q3_integer(call, 0),
            .goal_area = (uint32_t)q3_integer(call, 2), .travel_flags = (uint32_t)q3_integer(call, 3),
            .has_origin = call->arguments[1] != 0};
        qa_navigation *selected = qa_bot_navigation_runtime(navigation);
        if (!query.area || !query.goal_area ||
            !qa_navigation_node(selected, qa_bot_navigation_node(navigation, query.area)) ||
            !qa_navigation_node(selected, qa_bot_navigation_node(navigation, query.goal_area))) break;
        qa_bot_nav_route route;
        ok = (!query.has_origin || q3_vector(call, call->arguments[1], &query.origin, error)) &&
             qa_bot_navigation_route(navigation, &query, &route, error);
        if (ok) *result = (int32_t)route.travel_time; break;
    }
    case 317: {
        bool swimming;
        ok = q3_vector(call, call->arguments[0], &point, error) &&
             qa_bot_navigation_swimming(navigation, point, &swimming, error);
        if (ok) *result = swimming; break;
    }
    case 575: ok = alternatives(call, navigation, result, error); break;
    case 576: ok = route_prediction(call, navigation, result, error); break;
    case 577:
        if (!qa_bot_runtime_initialized(runtime)) break;
        ok = (!call->arguments[0] || q3_vector(call, call->arguments[0], &point, error)) &&
             qa_bot_navigation_reachability_index(navigation, call->arguments[0] ? &point : NULL, result, error); break;
    default: return Q3_UNHANDLED;
    }
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
