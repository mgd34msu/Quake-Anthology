#include "internal.h"
#include <math.h>

static bool source_slot(q3_call *call, uint32_t number, q3_record *record,
                          qa_qvm_entity_shared *shared, qa_error *error)
{
    return q3_game_entity_record(call, number, record, error) &&
        qa_q3_abi_read_shared_entity(&record->abi, 0, shared, error);
}

static bool same_actor(q3_call *call, uint32_t number, qa_actor_id expected, qa_error *error)
{
    return (qa_actor_id_equal(call->host->game->slots[number].actor, expected) &&
            (!expected.registry || qa_actors_get(qa_session_actors(call->host->options.session), expected))) ||
        q3_fail(error, QA_ERROR_NOT_FOUND, number, "Q3 actor changed during spatial operation");
}

static bool actor_word(q3_call *call, uint32_t number, qa_actor_id actor,
                         uint64_t address, uint32_t word, qa_error *error)
{
    return q3_write_word(call, address, word, error) && same_actor(call, number, actor, error);
}

static bool actor_vector(q3_call *call, uint32_t number, qa_actor_id actor,
                           uint64_t address, qa_vec3 vector, qa_error *error)
{
    return q3_write_vector(call, address, vector, error) && same_actor(call, number, actor, error);
}

static qa_trace_policy policy(qa_q3_host *host, uint32_t contents)
{
    qa_trace_policy result = qa_collision_default_policy(QA_GAME_Q3);
    result.contents_mask = qa_collision_contents_mask(contents, QA_GAME_Q3);
    const qa_cvar_view *curves = qa_cvars_read(host->options.cvars, host->no_curves);
    const qa_cvar_view *player = qa_cvars_read(host->options.cvars, host->player_curve_clip);
    result.curves = !curves || curves->number == 0;
    result.player_curve_clip = !player || player->integer != 0;
    return result;
}

static bool pass_actor(q3_call *call, int32_t number, qa_actor_id *out, qa_error *error)
{
    *out = (qa_actor_id){0};
    if (number < 0 || number >= 1022 || call->host->game->slots[number].input_retired) return true;
    return qa_q3_host_actor(call->host, (uint32_t)number, true, out, error);
}

static bool trace(q3_call *call, bool capsule, qa_error *error)
{
    qa_trace_query query = {.shape.kind = capsule ? QA_SHAPE_CAPSULE : QA_SHAPE_BOX,
        .policy = policy(call->host, (uint32_t)q3_integer(call, 6))};
    if (!q3_vector(call, call->arguments[1], &query.start, error) ||
        !q3_vector(call, call->arguments[4], &query.end, error) ||
        (call->arguments[2] && !q3_vector(call, call->arguments[2], &query.shape.bounds.mins, error)) ||
        (call->arguments[3] && !q3_vector(call, call->arguments[3], &query.shape.bounds.maxs, error)) ||
        !pass_actor(call, q3_integer(call, 5), &query.pass_actor, error)) return false;
    qa_trace_result hit;
    if (!qa_world_trace(call->host->options.world, &query, &hit, error)) return false;
    uint32_t number = hit.fraction == 1 ? 1023 : 1022;
    if (hit.hit == QA_TRACE_HIT_ACTOR && !qa_q3_host_actor_slot(call->host, hit.actor, &number, error)) return false;
    q3_record output;
    return q3_record_open(call, call->arguments[0], 56, &output, error) &&
        qa_q3_abi_write_trace(&output.abi, 0, &hit, (int32_t)number, error);
}

static bool point_contents(q3_call *call, int32_t *result, qa_error *error)
{
    qa_point_query query = {.policy = qa_collision_default_policy(QA_GAME_Q3), .q3_server_entities = true};
    if (!q3_vector(call, call->arguments[0], &query.point, error)) return false;
    int32_t excluded = q3_integer(call, 1);
    if (excluded >= 0 && excluded < 1022) query.pass_actor = call->host->game->slots[excluded].actor;
    qa_point_contents contents;
    if (!qa_world_point_contents(call->host->options.world, &query, &contents, error)) return false;
    *result = qa_collision_point_contents_export(contents.contents, QA_GAME_Q3, contents.q1_opaque_token); return true;
}

static bool contact(q3_call *call, bool capsule, int32_t *result, qa_error *error)
{
    qa_trace_query query = {.shape.kind = capsule ? QA_SHAPE_CAPSULE : QA_SHAPE_BOX,
        .policy = policy(call->host, UINT32_MAX)};
    uint32_t number;
    if (!q3_vector(call, call->arguments[0], &query.shape.bounds.mins, error) ||
        !q3_vector(call, call->arguments[1], &query.shape.bounds.maxs, error) ||
        !q3_game_pointer_slot(call, call->arguments[2], &number, error)) return false;
    q3_record record; qa_qvm_entity_shared shared;
    if (!source_slot(call, number, &record, &shared, error)) return false;
    query.target.origin = shared.origin; query.target.angles = shared.angles;
    query.target.pose_rules = QA_RULESET_Q3;
    qa_collision_geometry *geometry = qa_world_geometry(call->host->options.world);
    qa_trace_scratch *scratch = qa_world_trace_scratch(call->host->options.world, geometry);
    qa_trace_result hit; bool ok;
    if (shared.inline_model) {
        query.target.inline_model = true;
        query.target.model = qa_load_u32le(record.abi.bytes.data + 160);
        ok = qa_collision_trace(geometry, scratch, &query, &hit, error);
    } else if (shared.server_flags & 1024)
        ok = qa_collision_trace_q3_capsule(geometry, scratch, &query, shared.local_bounds, true, &hit, error);
    else ok = qa_collision_trace_q3_box(&query, shared.local_bounds, true, &hit, error);
    if (ok) *result = hit.start_solid || hit.all_solid;
    return ok;
}

static bool area_entities(q3_call *call, int32_t *result, qa_error *error)
{
    qa_bounds bounds;
    if (!q3_vector(call, call->arguments[0], &bounds.mins, error) ||
        !q3_vector(call, call->arguments[1], &bounds.maxs, error)) return false;
    size_t capacity = qa_actors_count(qa_session_actors(call->host->options.session));
    qa_actor_id *actors = capacity ? qa_arena_alloc(&call->host->scratch,
        capacity * sizeof(*actors), _Alignof(qa_actor_id), error) : NULL;
    if (capacity && !actors) return false;
    size_t count; bool overflow;
    if (!qa_world_query(call->host->options.world, bounds, QA_COLLISION_BOTH,
                         actors, capacity, &count, &overflow, error)) return false;
    if (overflow) return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 actor query changed during snapshot");
    int32_t maximum = q3_integer(call, 3);
    if (maximum >= 0 && count > (size_t)maximum) count = (size_t)maximum;
    if (!count) return true;
    uint32_t *numbers = qa_arena_alloc(&call->host->scratch, count * sizeof(*numbers), _Alignof(uint32_t), error);
    if (!numbers) return false;
    for (size_t i = 0; i < count; ++i)
        if (!qa_q3_host_actor_slot(call->host, actors[i], &numbers[i], error)) return false;
    q3_record output;
    if (!q3_record_open(call, call->arguments[2], count * 4, &output, error)) return false;
    for (size_t i = 0; i < count; ++i)
        if (!q3_write_word(call, call->arguments[2] + i * 4, numbers[i], error)) return false;
    *result = (int32_t)count; return true;
}

static uint32_t solid_byte(float value)
{
    if (isnan(value)) return 0;
    return value < 1 ? 1 : value > 255 ? 255 : (uint32_t)value;
}

bool qa_q3_host_link(qa_q3_host *host, uint32_t number, qa_error *error)
{
    q3_call call;
    if (!q3_game_begin(host, &call, error)) return false;
    q3_record record; qa_qvm_entity_shared shared;
    if (!source_slot(&call, number, &record, &shared, error)) return q3_game_end(&call, false);
    size_t linked_offset = q3_shared_offset(host->options.abi, 416);
    if (host->game->slots[number].input_retired)
        return q3_game_end(&call, actor_word(&call, number, host->game->slots[number].actor,
                                             record.address + linked_offset, 0, error));
    qa_actor_id actor;
    if (!qa_q3_host_actor(host, number, true, &actor, error)) return q3_game_end(&call, false);
    q3_entity_slot *slot = &host->game->slots[number];
    if ((!slot->borrowed && !qa_world_unlink(host->options.world, actor, error)) ||
        !same_actor(&call, number, actor, error) || !q3_game_entity_record(&call, number, &record, error) ||
        !actor_word(&call, number, actor, record.address + linked_offset, 0, error) ||
        !source_slot(&call, number, &record, &shared, error)) return q3_game_end(&call, false);
    uint32_t solid = shared.inline_model ? 0xffffffu : !(shared.contents & 0x02000001) ? 0 :
        (solid_byte(shared.local_bounds.maxs.z + 32) << 16) |
        (solid_byte(-shared.local_bounds.mins.z) << 8) | solid_byte(shared.local_bounds.maxs.x);
    if (!actor_word(&call, number, actor, record.address + 176, solid, error) ||
        !source_slot(&call, number, &record, &shared, error)) return q3_game_end(&call, false);
    qa_bounds bounds = qa_collision_link_bounds(shared.local_bounds, shared.origin, shared.angles,
        shared.inline_model, qa_v3(1, 1, 1), QA_RULESET_Q3);
    if (!actor_vector(&call, number, actor, record.address + q3_shared_offset(host->options.abi, 464), bounds.mins, error) ||
        !actor_vector(&call, number, actor, record.address + q3_shared_offset(host->options.abi, 476), bounds.maxs, error) ||
        !source_slot(&call, number, &record, &shared, error)) return q3_game_end(&call, false);
    bounds = shared.absolute_bounds;
    qa_collision_geometry *geometry = qa_world_geometry(host->options.world);
    bool has_leaves;
    qa_q3_visibility_entity visibility={0};
    if(!qa_q3_leaf_visibility(host->options.world,actor,&bounds,
        qa_world_trace_scratch(host->options.world,geometry),&visibility,slot->clusters,&has_leaves,error))
        return q3_game_end(&call,false);
    slot->has_visibility=true;slot->area=visibility.area;slot->area2=visibility.area2;
    slot->last_cluster=visibility.last_cluster;slot->cluster_count=(uint32_t)visibility.cluster_count;
    if(!has_leaves) return q3_game_end(&call,true);
    if ((!slot->borrowed && !qa_world_link_bounds(host->options.world, actor, &bounds, error)) ||
        !same_actor(&call, number, actor, error) ||
        !source_slot(&call, number, &record, &shared, error)) return q3_game_end(&call, false);
    bool ok = actor_word(&call, number, actor, record.address + q3_shared_offset(host->options.abi, 420),
                           (uint32_t)shared.linkcount + 1u, error) &&
        actor_word(&call, number, actor, record.address + linked_offset, 1, error);
    return q3_game_end(&call, ok);
}

bool qa_q3_host_unlink(qa_q3_host *host, uint32_t number, qa_error *error)
{
    q3_call call;
    if (!q3_game_begin(host, &call, error)) return false;
    q3_record record;
    if (!q3_game_entity_record(&call, number, &record, error)) return q3_game_end(&call, false);
    q3_entity_slot *slot = &host->game->slots[number];
    qa_actor_id actor = slot->actor; bool borrowed = slot->borrowed;
    bool ok = actor_word(&call, number, actor, record.address + q3_shared_offset(host->options.abi, 416), 0, error);
    if (ok && actor.registry && !borrowed)
        ok = qa_world_unlink(host->options.world, actor, error) && same_actor(&call, number, actor, error);
    return q3_game_end(&call, ok);
}

bool qa_q3_host_visibility_read(qa_q3_host *host, uint32_t number,
                                 qa_q3_host_visibility *out, bool *present, qa_error *error)
{
    q3_call call;
    if (!out || !present || !q3_game_begin(host, &call, error)) return false;
    q3_record record;
    bool ok = q3_game_entity_record(&call, number, &record, error);
    if (ok) {
        q3_entity_slot *slot = &host->game->slots[number];
        *present = slot->has_visibility;
        if (*present) {
            *out = (qa_q3_host_visibility){.area = slot->area, .area2 = slot->area2,
                .last_cluster = slot->last_cluster, .cluster_count = slot->cluster_count};
            memcpy(out->clusters, slot->clusters, slot->cluster_count * sizeof(*slot->clusters));
        }
    }
    return q3_game_end(&call, ok);
}

static bool brush_model(q3_call *call, qa_error *error)
{
    qa_bytes name = {0};
    if (!q3_string(call, call->arguments[1], &name, error)) return false;
    if (name.data[0] != '*') {
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "SV_SetBrushModel requires an inline model name");
    }
    uint32_t bits = (uint32_t)(unsigned long)strtol((const char *)name.data + 1, NULL, 10);

    uint32_t number; q3_record record; qa_bounds bounds;
    if (!q3_game_pointer_slot(call, call->arguments[0], &number, error) ||
        !qa_collision_model_bounds(qa_world_geometry(call->host->options.world), bits, &bounds, error) ||
        !q3_game_entity_record(call, number, &record, error)) return false;
    qa_qvm_abi abi = call->host->options.abi;
    qa_actor_id actor = call->host->game->slots[number].actor;
    if (!actor_word(call, number, actor, record.address + 160, bits, error) ||
        !actor_word(call, number, actor, record.address + q3_shared_offset(abi, 432), 1, error) ||
        !actor_vector(call, number, actor, record.address + q3_shared_offset(abi, 436), bounds.mins, error) ||
        !actor_vector(call, number, actor, record.address + q3_shared_offset(abi, 448), bounds.maxs, error) ||
        !actor_word(call, number, actor, record.address + q3_shared_offset(abi, 460), UINT32_MAX, error)) return false;
    return qa_q3_host_link(call->host, number, error);
}

q3_service_result q3_game_spatial(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_GAME) return Q3_UNHANDLED;
    int32_t code = call->service;
    if ((code < 23 || code > 33) && code != 43 && code != 44) return Q3_UNHANDLED;
    bool scoped = code == 24 || code == 25 || code == 32 || code == 43;
    q3_entity_span span;
    if (scoped && !q3_entity_span_begin(call, &span, error)) return Q3_FAILED;
    bool ok = true; qa_collision_geometry *geometry = qa_world_geometry(call->host->options.world);
    switch (code) {
    case 23: ok = brush_model(call, error); break;
    case 24: case 43: ok = trace(call, code == 43, error); break;
    case 25: ok = point_contents(call, result, error); break;
    case 26: case 27: {
        qa_vec3 first, second; qa_collision_leaf a, b; bool visible, connected = true;
        ok = q3_vector(call, call->arguments[0], &first, error) &&
            q3_vector(call, call->arguments[1], &second, error) &&
            qa_collision_point_leaf(geometry, first, QA_LEAF_COLLISION, &a, error) && qa_collision_point_leaf(geometry, second, QA_LEAF_COLLISION, &b, error) &&
            qa_collision_cluster_visible(geometry, (int32_t)a.cluster, (int32_t)b.cluster, false, &visible, error);
        if (ok && visible && code == 26)
            ok = qa_collision_areas_connected(geometry, (int32_t)a.area, (int32_t)b.area, &connected, error);
        if (ok) *result = visible && connected;
        break;
    }
    case 29: {
        bool connected;
        ok = qa_collision_areas_connected(geometry, q3_integer(call, 0), q3_integer(call, 1), &connected, error);
        if (ok) *result = connected;
        break;
    }
    case 28: {
        uint32_t number; qa_q3_host_visibility visibility; bool present;
        ok = q3_game_pointer_slot(call, call->arguments[0], &number, error) &&
            qa_q3_host_visibility_read(call->host, number, &visibility, &present, error);
        if (ok && present && visibility.area2 != -1)
            ok = q3_game_portal(call->host, visibility.area, visibility.area2, q3_integer(call, 1) != 0, error);
        break;
    }
    case 30: case 31: {
        uint32_t number;
        ok = q3_game_pointer_slot(call, call->arguments[0], &number, error) &&
            (code == 30 ? qa_q3_host_link(call->host, number, error) : qa_q3_host_unlink(call->host, number, error)); break;
    }
    case 32: ok = area_entities(call, result, error); break;
    case 33: case 44: ok = contact(call, code == 44, result, error); break;
    default: return Q3_UNHANDLED;
    }
    if (scoped) ok = q3_entity_span_end(call, &span, ok, error);
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
