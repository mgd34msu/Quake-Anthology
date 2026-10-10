#include "internal.h"
#include "qa/q3_host_collision.h"
#include "qa/physics.h"
#include "qa/scene_marks.h"
#include <math.h>

bool q3_collision_mark_fragments(q3_call *call, int32_t *result, qa_error *error)
{
    _Static_assert(sizeof(qa_vec3) == 12, "Q3 mark points contain three binary32 values");
    int32_t count = q3_integer(call, 0), point_capacity = q3_integer(call, 3);
    int32_t fragment_capacity = q3_integer(call, 5);
    qa_scene_world *world = call->host->options.scene_world;
    if (!world)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 mark projection world is unbound");
    if (count <= 0 || point_capacity < 0 || fragment_capacity < 0 ||
        (size_t)count > SIZE_MAX / sizeof(qa_vec3) ||
        (size_t)point_capacity > SIZE_MAX / sizeof(qa_vec3) ||
        (size_t)fragment_capacity > SIZE_MAX / sizeof(qa_scene_mark_fragment))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 mark projection capacities are invalid");
    size_t input_bytes = (size_t)count * 12;
    size_t point_bytes = (size_t)point_capacity * 12;
    size_t fragment_bytes = (size_t)fragment_capacity * 8;
    if (call->vm) {
        qa_bytes admitted;
        if (!q3_vm_span(call->vm, call->arguments[1], input_bytes, &admitted, error) ||
            !q3_vm_span(call->vm, call->arguments[2], 12, &admitted, error) ||
            (point_bytes && !q3_vm_span(call->vm, call->arguments[4], point_bytes, &admitted, error)) ||
            (fragment_bytes && !q3_vm_span(call->vm, call->arguments[6], fragment_bytes, &admitted, error)))
            return false;
    }
    qa_vec3 projection;
    if (!q3_vector(call, call->arguments[2], &projection, error)) return false;
    qa_vec3 *input = qa_arena_alloc(&call->host->scratch, input_bytes, _Alignof(qa_vec3), error);
    if (!input || !q3_read(call, call->arguments[1], input, input_bytes, error)) return false;
    for (size_t i = 0; i < (size_t)count; ++i) {
        const uint8_t *bytes = (const uint8_t *)(input + i);
        input[i] = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    }
    qa_vec3 *points = point_bytes ? qa_arena_alloc(&call->host->scratch,
        point_bytes, _Alignof(qa_vec3), error) : NULL;
    qa_scene_mark_fragment *fragments = fragment_capacity ? qa_arena_alloc(&call->host->scratch,
        (size_t)fragment_capacity * sizeof(*fragments), _Alignof(qa_scene_mark_fragment), error) : NULL;
    if ((point_bytes && !points) || (fragment_capacity && !fragments)) return false;
    qa_scene_mark_result marks;
    if (!qa_scene_world_mark_fragments(world, input, (size_t)count, projection,
        points, (size_t)point_capacity, fragments, (size_t)fragment_capacity, &marks, error)) return false;
    size_t written_points = marks.point_count * 12, written_fragments = marks.fragment_count * 8;
    size_t output_bytes = written_points > written_fragments ? written_points : written_fragments;
    uint8_t *output = output_bytes ? qa_arena_alloc(&call->host->scratch, output_bytes, 1, error) : NULL;
    if (output_bytes && !output) return false;
    for (size_t i = 0; i < marks.point_count; ++i) {
        qa_store_f32le(output + i * 12, points[i].x);
        qa_store_f32le(output + i * 12 + 4, points[i].y);
        qa_store_f32le(output + i * 12 + 8, points[i].z);
    }
    if (written_points && !q3_write(call, call->arguments[4], (qa_bytes){output, written_points}, error))
        return false;
    for (size_t i = 0; i < marks.fragment_count; ++i) {
        qa_store_u32le(output + i * 8, (uint32_t)fragments[i].first_point);
        qa_store_u32le(output + i * 8 + 4, (uint32_t)fragments[i].point_count);
    }
    if (written_fragments && !q3_write(call, call->arguments[6], (qa_bytes){output, written_fragments}, error))
        return false;
    *result = (int32_t)marks.fragment_count;
    return true;
}

typedef struct q3_collision_binding {
    qa_qvm *vm;
    const qa_qvm_image *image;
    qa_q3_host_collision_profile profile;
    qa_q3_host_collision_current_fn current;
    void *context;
} q3_collision_binding;

struct qa_q3_host_collision_scene {
    qa_q3_host *host;
    const q3_collision_binding *binding;
    qa_collision_geometry *geometry;
    qa_trace_scratch *scratch;
    unsigned readers;
};

typedef struct collision_row {
    uint32_t address;
    qa_q3_entity state;
    qa_vec3 origin, angles;
} collision_row;

typedef struct collision_capture {
    qa_q3_host_collision_view view;
    uint32_t words[10];
    uint8_t *records;
    collision_row *rows;
    size_t record_size;
} collision_capture;

static bool record_field(uint32_t offset, size_t size, uint32_t stride)
{ return !(offset & 3u) && offset <= stride && size <= (size_t)stride - offset; }

bool qa_q3_host_collision_profile_qualify(const qa_qvm_image *image, qa_qvm_role role,
    qa_qvm_abi abi, const qa_q3_host_collision_profile *profile, qa_error *error)
{
    if (!image || !profile || role != QA_QVM_CGAME)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Collision cache belongs to an actual CGAME artifact");
    if (!profile->present) return true;
    size_t code_count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &code_count);
    const uint32_t entries[] = {profile->build_solids_entry, profile->trace_entry, profile->point_contents_entry};
    for (size_t i = 0; i < sizeof(entries) / sizeof(*entries); ++i) {
        if (entries[i] >= code_count || code[entries[i]].opcode != QA_QVM_ENTER)
            return q3_fail(error, QA_ERROR_FORMAT, i, "CG collision declaration does not name an original function");
        for (size_t j = 0; j < i; ++j) if (entries[i] == entries[j])
            return q3_fail(error, QA_ERROR_FORMAT, i, "CG collision functions overlap ownership");
    }
    if (!isfinite(profile->trajectory_gravity))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "CG trajectory policy must be declared finite binary32");
    const uint32_t globals[] = {profile->snapshot, profile->next_snapshot, profile->time,
        profile->physics_time, profile->this_frame_teleport, profile->next_frame_teleport,
        profile->processed_snapshot, profile->solid_count};
    for (size_t i = 0; i < sizeof(globals) / sizeof(*globals); ++i)
        if (!qa_qvm_qualify_global_word(image, globals[i], error)) return false;
    size_t entity_bytes = qa_qvm_entity_bytes(abi), snapshot_bytes = qa_qvm_snapshot_bytes(abi);
    if (!entity_bytes || !snapshot_bytes || !profile->entity_count || profile->entity_count > QA_Q3_ENTITIES ||
        !profile->solid_capacity || profile->solid_capacity > QA_Q3_ENTITIES ||
        (profile->entity_stride & 3u) || !profile->entity_stride ||
        profile->entity_stride > qa_qvm_image_memory_size(image) / profile->entity_count ||
        !record_field(profile->current_state, entity_bytes, profile->entity_stride) ||
        !record_field(profile->next_state, entity_bytes, profile->entity_stride) ||
        !record_field(profile->lerp_origin, 12, profile->entity_stride) ||
        !record_field(profile->lerp_angles, 12, profile->entity_stride) ||
        (profile->snapshot_server_time & 3u) || profile->snapshot_server_time > snapshot_bytes - 4 ||
        (profile->entities & 3u) || (profile->solids & 3u))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Declared CG collision records exceed their source ABI");
    return qa_qvm_qualify_source_span(image, profile->entities,
        (size_t)profile->entity_count * profile->entity_stride, error) &&
        qa_qvm_qualify_source_span(image, profile->solids, (size_t)profile->solid_capacity * 4, error);
}

bool qa_q3_host_collision_scene_bind(qa_q3_host *host, qa_qvm *vm, const qa_qvm_image *image,
    const qa_q3_host_collision_profile *profile, qa_q3_host_collision_current_fn current,
    void *context, qa_error *error)
{
    if (!host || host->retired || host->restore_pending || host->calls || host->collision_scene ||
        host->collision_holds || !vm || host->vm != vm || qa_qvm_active(vm) || !image ||
        host->options.role != QA_QVM_CGAME || qa_qvm_get_role(vm) != QA_QVM_CGAME ||
        qa_qvm_get_abi(vm) != host->options.abi || !profile || !profile->present || !current ||
        !host->options.session || !host->options.owner || !host->options.service_owner ||
        !host->options.collision.geometry || !host->options.client.source_actor ||
        (qa_qvm_image_of(vm) != image))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Collision cache binding requires its idle declared CGAME module");
    if (!qa_q3_host_collision_profile_qualify(image, QA_QVM_CGAME, host->options.abi, profile, error)) return false;
    q3_collision_binding *binding = calloc(1, sizeof(*binding));
    if (!binding) return q3_fail(error, QA_ERROR_MEMORY, 0, "Retaining declared CG collision cache");
    *binding = (q3_collision_binding){vm, image, *profile, current, context};
    qa_qvm_image_retain((qa_qvm_image *)image);
    host->collision_scene = binding;
    return true;
}

void q3_collision_scene_close(qa_q3_host *host)
{
    if (!host || !host->collision_scene || host->collision_holds) return;
    qa_qvm_image_release((qa_qvm_image *)host->collision_scene->image);
    free(host->collision_scene);
    host->collision_scene = NULL;
}

bool q3_collision_scene_services(const qa_q3_host *host, qa_source_save_io *io)
{
    bool present = host->collision_scene != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    qa_q3_host_collision_profile profile = host->collision_scene->profile;
    bool current = host->collision_scene->current != NULL;
    bool context = host->collision_scene->context != NULL;
    if (!qa_source_save_bool(io, &current) || !qa_source_save_bool(io, &context)) return false;
#define WORD(member) if (!qa_source_save_u32(io, &profile.member)) return false
    WORD(snapshot); WORD(next_snapshot); WORD(time); WORD(physics_time);
    WORD(this_frame_teleport); WORD(next_frame_teleport); WORD(processed_snapshot);
    WORD(solid_count); WORD(solids); WORD(solid_capacity); WORD(entities); WORD(entity_count);
    WORD(entity_stride); WORD(current_state); WORD(next_state); WORD(lerp_origin); WORD(lerp_angles);
    WORD(snapshot_server_time); WORD(build_solids_entry); WORD(trace_entry); WORD(point_contents_entry);
#undef WORD
    return qa_source_save_f32(io, &profile.trajectory_gravity);
}

bool qa_q3_host_collision_held(const qa_q3_host *host)
{ return host && host->collision_holds != 0; }

static bool binding_current(const qa_q3_host_collision_scene *scene, qa_error *error)
{
    qa_q3_host *host = scene ? scene->host : NULL;
    const q3_collision_binding *binding = scene ? scene->binding : NULL;
    if (!host || host->retired || host->restore_pending || !host->collision_holds ||
        !binding || host->collision_scene != binding || host->vm != binding->vm ||
        qa_qvm_get_role(binding->vm) != QA_QVM_CGAME || qa_qvm_get_abi(binding->vm) != host->options.abi ||
        (qa_qvm_image_of(binding->vm) != binding->image))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Held CG collision module is no longer current");
    qa_collision_geometry *geometry = host->options.collision.geometry(host->options.collision.context);
    if (!geometry || geometry != scene->geometry)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Held CG collision scene changed its actual map owner");
    return binding->current(binding->context, host, binding->vm, binding->image, geometry, error);
}

bool qa_q3_host_collision_current(const qa_q3_host_collision_scene *scene)
{ return binding_current(scene, NULL); }

bool qa_q3_host_collision_hold(qa_q3_host *host, qa_q3_host_collision_scene **out, qa_error *error)
{
    if (!host || !out || *out || host->retired || host->restore_pending || !host->collision_scene ||
        host->collision_holds == SIZE_MAX)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "CGAME has no admitted private collisionScene capability");
    qa_q3_host_collision_scene *scene = calloc(1, sizeof(*scene));
    if (!scene) return q3_fail(error, QA_ERROR_MEMORY, 0, "Holding actual CG collision module");
    scene->host = host; scene->binding = host->collision_scene;
    scene->geometry = host->options.collision.geometry(host->options.collision.context);
    scene->scratch = host->options.collision.trace_scratch ?
        host->options.collision.trace_scratch(host->options.collision.context,scene->geometry) : NULL;
    if (!qa_collision_retain(scene->geometry, error)) { free(scene); return false; }
    ++host->collision_holds;
    if (!binding_current(scene, error)) {
        --host->collision_holds; qa_collision_destroy(scene->geometry); free(scene); return false;
    }
    *out = scene;
    return true;
}

void qa_q3_host_collision_release(qa_q3_host_collision_scene *scene)
{
    if (!scene || scene->readers) return;
    --scene->host->collision_holds;
    qa_collision_destroy(scene->geometry);
    free(scene);
}

static bool word(const q3_collision_binding *binding, uint32_t address,
    uint32_t *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_qvm_read(binding->vm, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_u32le(bytes);
    return true;
}

static int32_t signed_word(uint32_t value)
{ int32_t result; memcpy(&result, &value, sizeof(result)); return result; }

static qa_vec3 vector(const uint8_t *bytes)
{ return qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8)); }

static void capture_free(collision_capture *capture)
{ free(capture->records); free(capture->rows); *capture = (collision_capture){0}; }

static bool capture(const qa_q3_host_collision_scene *scene, collision_capture *out, qa_error *error)
{
    if (!binding_current(scene, error)) return false;
    qa_q3_host *host = scene->host;
    const q3_collision_binding *binding = scene->binding;
    const qa_q3_host_collision_profile *profile = &binding->profile;
    collision_capture result = {0};
    const uint32_t addresses[] = {profile->snapshot, profile->next_snapshot, profile->time,
        profile->physics_time, profile->processed_snapshot, profile->this_frame_teleport,
        profile->next_frame_teleport, profile->solid_count};
    for (size_t i = 0; i < sizeof(addresses) / sizeof(*addresses); ++i)
        if (!word(binding, addresses[i], result.words + i, error)) goto failed;
    if (result.words[7] > profile->solid_capacity)
        { q3_fail(error, QA_ERROR_FORMAT, 0, "CG solid list exceeds its declared extent"); goto failed; }
    for (unsigned i = 0; i < 2; ++i) if (result.words[i]) {
        if ((result.words[i] & 3u) || !qa_qvm_qualify_source_span(binding->image, result.words[i],
            qa_qvm_snapshot_bytes(host->options.abi), error) ||
            !word(binding, result.words[i] + profile->snapshot_server_time, result.words + 8 + i, error)) goto failed;
    }
    if (!result.words[0] && (result.words[1] || result.words[7]))
        { q3_fail(error, QA_ERROR_FORMAT, 0, "CG collision list has no current retail snapshot"); goto failed; }
    result.view = (qa_q3_host_collision_view){.host = host, .vm = binding->vm, .image = binding->image,
        .session = host->options.session, .world = host->options.world,
        .content = host->options.mounts, .frontend_lifetime = host->options.frontend_lifetime,
        .geometry = host->options.collision.geometry(host->options.collision.context),
        .receiver = host->options.owner, .service_owner = host->options.service_owner,
        .snapshot_address = result.words[0], .next_snapshot_address = result.words[1],
        .time = signed_word(result.words[2]), .physics_time = signed_word(result.words[3]),
        .processed_snapshot = signed_word(result.words[4]),
        .this_frame_teleport = result.words[5] != 0, .next_frame_teleport = result.words[6] != 0,
        .solid_count = result.words[7], .snapshot_time = signed_word(result.words[8]),
        .next_snapshot_time = signed_word(result.words[9])};
    if (!result.view.geometry)
        { q3_fail(error, QA_ERROR_ARGUMENT, 0, "CG collision cache lost its actual map parent"); goto failed; }
    result.view.map_identity = qa_collision_map_identity(result.view.geometry);
    result.view.map_resource = qa_collision_resource(result.view.geometry);
    size_t entity_bytes = qa_qvm_entity_bytes(host->options.abi);
    result.record_size = entity_bytes * 2 + 24;
    if (result.view.solid_count) {
        result.records = malloc(result.view.solid_count * result.record_size);
        result.rows = calloc(result.view.solid_count, sizeof(*result.rows));
        if (!result.records || !result.rows)
            { q3_fail(error, QA_ERROR_MEMORY, 0, "Reading actual CG solid cache"); goto failed; }
    }
    for (size_t i = 0; i < result.view.solid_count; ++i) {
        collision_row *row = result.rows + i;
        if (!word(binding, profile->solids + (uint32_t)i * 4, &row->address, error)) goto failed;
        if (row->address < profile->entities ||
            (row->address - profile->entities) % profile->entity_stride ||
            (row->address - profile->entities) / profile->entity_stride >= profile->entity_count)
            { q3_fail(error, QA_ERROR_FORMAT, i, "CG solid pointer is outside its declared centity array"); goto failed; }
        uint8_t *bytes = result.records + i * result.record_size;
        if (!qa_qvm_read(binding->vm, row->address + profile->current_state, bytes, entity_bytes, error) ||
            !qa_qvm_read(binding->vm, row->address + profile->next_state, bytes + entity_bytes, entity_bytes, error) ||
            !qa_qvm_read(binding->vm, row->address + profile->lerp_origin, bytes + entity_bytes * 2, 12, error) ||
            !qa_qvm_read(binding->vm, row->address + profile->lerp_angles, bytes + entity_bytes * 2 + 12, 12, error)) goto failed;
        qa_q3_abi_record record = {.abi = host->options.abi, .bytes = {bytes, entity_bytes}};
        if (!qa_q3_abi_read_entity(&record, 0, true, &row->state, error) ||
            row->state.number < 0 || row->state.number >= QA_Q3_ENTITIES)
            { q3_fail(error, QA_ERROR_FORMAT, i, "CG solid has an invalid actual Source entity number"); goto failed; }
        row->origin = vector(bytes + entity_bytes * 2);
        row->angles = vector(bytes + entity_bytes * 2 + 12);
    }
    if (!binding_current(scene, error)) goto failed;
    if (host->options.collision.geometry(host->options.collision.context) != result.view.geometry)
        { q3_fail(error, QA_ERROR_ARGUMENT, 0, "CG map owner changed during collision cache read"); goto failed; }
    *out = result;
    return true;
failed:
    capture_free(&result);
    return false;
}

static bool unchanged(const qa_q3_host_collision_scene *scene, const collision_capture *before,
    qa_error *error)
{
    collision_capture after = {0};
    if (!capture(scene, &after, error)) return false;
    bool same = before->view.geometry == after.view.geometry && before->view.map_resource == after.view.map_resource &&
        before->view.map_identity == after.view.map_identity &&
        before->view.solid_count == after.view.solid_count &&
        !memcmp(before->words, after.words, sizeof(before->words));
    for (size_t i = 0; same && i < before->view.solid_count; ++i)
        same = before->rows[i].address == after.rows[i].address &&
            !memcmp(before->records + i * before->record_size,
                after.records + i * after.record_size, before->record_size);
    capture_free(&after);
    return same || q3_fail(error, QA_ERROR_ARGUMENT, 0, "Actual CG collision cache changed during query");
}

bool qa_q3_host_collision_read(const qa_q3_host_collision_scene *scene,
    qa_q3_host_collision_view *out, bool *present, qa_error *error)
{
    if (!out || !present) return q3_fail(error, QA_ERROR_ARGUMENT, 0, "CG collision read needs its output receipt");
    collision_capture value = {0};
    if (!capture(scene, &value, error)) return false;
    *out = value.view; *present = value.view.snapshot_address != 0;
    capture_free(&value);
    return true;
}

static bool actor(const qa_q3_host_collision_scene *scene, uint32_t number,
    qa_actor_id *out, bool *present, qa_error *error)
{
    qa_q3_host *host = scene->host;
    if (!host->options.client.source_actor(host->options.client.context, number, out, present, error)) return false;
    return !*present || qa_actors_get(qa_session_actors(host->options.session), *out) ||
        q3_fail(error, QA_ERROR_ARGUMENT, number, "CG solid Source actor generation is no longer live");
}

static bool skip(const qa_q3_host_collision_scene *scene, const collision_row *row,
    qa_actor_id pass, bool *out, qa_error *error)
{
    *out = false;
    if (!pass.registry) return true;
    qa_actor_id mapped = {0}; bool present = false;
    if (!actor(scene, (uint32_t)row->state.number, &mapped, &present, error)) return false;
    *out = present && qa_actor_id_equal(pass, mapped);
    return true;
}

bool qa_q3_host_collision_trace(qa_q3_host_collision_scene *scene, const qa_trace_query *query,
    qa_trace_result *out, qa_error *error)
{
    if (!scene || !query || !out || scene->readers || query->policy.family != QA_COLLISION_Q3 ||
        !qa_vec_finite(query->start) || !qa_vec_finite(query->end) ||
        (query->pass_actor.registry && !qa_actors_get(qa_session_actors(scene->host->options.session), query->pass_actor)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "CG acoustic trace requires its actual listener and finite query");
    ++scene->readers;
    collision_capture cached = {0};
    bool ok = capture(scene, &cached, error);
    qa_trace_result result = {0};
    if (ok && !cached.view.snapshot_address) ok = q3_fail(error, QA_ERROR_NOT_FOUND, 0, "CG has no selected collision snapshot yet");
    qa_trace_query q = *query;
    q.target = (qa_collision_target){.pose_rules=QA_RULESET_Q3};
    q.policy.family = QA_COLLISION_Q3;
    if (ok) ok = qa_collision_trace_q3_model(cached.view.geometry, scene->scratch, &q, 0, false, &result, error);
    result.hit = result.fraction != 1 ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_NONE;
    result.actor = (qa_actor_id){0};
    for (size_t i = 0; ok && i < cached.view.solid_count; ++i) {
        const collision_row *row = cached.rows + i;
        bool ignored;
        ok = skip(scene, row, q.pass_actor, &ignored, error);
        if (!ok || ignored) continue;
        qa_trace_result hit;
        if (row->state.solid == 0xffffff) {
            const qa_q3_trajectory *position = &row->state.pos;
            qa_trajectory trajectory = {(qa_trajectory_type)position->type, position->time, position->duration,
                qa_v3(position->base[0], position->base[1], position->base[2]),
                qa_v3(position->delta[0], position->delta[1], position->delta[2])};
            if (row->state.modelindex < 0) { ok = q3_fail(error, QA_ERROR_FORMAT, i, "CG inline model is negative"); break; }
            ok = qa_trajectory_position(&trajectory, cached.view.physics_time,
                scene->binding->profile.trajectory_gravity, &q.target.origin, error);
            q.target.angles = row->angles;
            if (ok) ok = qa_collision_trace_q3_model(cached.view.geometry, scene->scratch, &q,
                (uint32_t)row->state.modelindex, true, &hit, error);
        } else {
            int32_t x = row->state.solid & 255, down = (row->state.solid >> 8) & 255;
            int32_t up = ((row->state.solid >> 16) & 255) - 32;
            qa_bounds bounds = {qa_v3(-(float)x, -(float)x, -(float)down), qa_v3((float)x, (float)x, (float)up)};
            q.target.origin = row->origin; q.target.angles = qa_v3(0, 0, 0);
            ok = qa_collision_trace_q3_box(&q, bounds, true, &hit, error);
        }
        if (!ok) break;
        if (hit.all_solid || hit.fraction < result.fraction) {
            qa_actor_id mapped = {0}; bool present = false;
            if (row->state.number != QA_Q3_ENTITY_WORLD) {
                ok = actor(scene, (uint32_t)row->state.number, &mapped, &present, error);
                if (ok && !present) ok = q3_fail(error, QA_ERROR_NOT_FOUND, i, "CG solid hit has no actual Source actor");
            }
            if (!ok) break;
            result = hit; result.actor = mapped;
            result.hit = row->state.number == QA_Q3_ENTITY_WORLD ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_ACTOR;
            result.model = row->state.solid == 0xffffff ? (uint32_t)row->state.modelindex : 0;
        } else if (hit.start_solid) result.start_solid = true;
        if (result.all_solid) break;
    }
    if (ok) ok = unchanged(scene, &cached, error);
    capture_free(&cached);
    --scene->readers;
    if (ok) *out = result;
    return ok;
}

bool qa_q3_host_collision_point_contents(qa_q3_host_collision_scene *scene, const qa_point_query *query,
    qa_point_contents *out, qa_error *error)
{
    if (!scene || !query || !out || scene->readers || query->policy.family != QA_COLLISION_Q3 ||
        !qa_vec_finite(query->point) || (query->pass_actor.registry &&
        !qa_actors_get(qa_session_actors(scene->host->options.session), query->pass_actor)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "CG contents query needs its actual held scene");
    ++scene->readers;
    collision_capture cached = {0};
    bool ok = capture(scene, &cached, error);
    qa_point_contents result = {0};
    if (ok && !cached.view.snapshot_address) ok = q3_fail(error, QA_ERROR_NOT_FOUND, 0, "CG has no selected collision snapshot yet");
    qa_point_query q = *query;
    q.target = (qa_collision_target){.pose_rules=QA_RULESET_Q3}; q.policy.family = QA_COLLISION_Q3;
    if (ok) ok = qa_collision_point_contents(cached.view.geometry, scene->scratch, &q, &result, error);
    for (size_t i = 0; ok && i < cached.view.solid_count; ++i) {
        const collision_row *row = cached.rows + i;
        bool ignored;
        ok = skip(scene, row, q.pass_actor, &ignored, error);
        if (!ok || ignored || row->state.solid != 0xffffff || !row->state.modelindex) continue;
        if (row->state.modelindex < 0) { ok = q3_fail(error, QA_ERROR_FORMAT, i, "CG contents inline model is negative"); break; }
        q.target = (qa_collision_target){true, (uint32_t)row->state.modelindex,
            qa_v3(row->state.origin[0], row->state.origin[1], row->state.origin[2]),
            qa_v3(row->state.angles[0], row->state.angles[1], row->state.angles[2]), QA_RULESET_Q3};
        qa_point_contents hit;
        ok = qa_collision_point_contents(cached.view.geometry, scene->scratch, &q, &hit, error);
        if (ok) { result.contents = qa_collision_bits_union(result.contents, hit.contents);
            result.stored = qa_collision_bits_union(result.stored, hit.stored);
            result.merged = qa_collision_bits_union(result.merged, hit.merged); }
    }
    if (ok) ok = unchanged(scene, &cached, error);
    capture_free(&cached);
    --scene->readers;
    if (ok) *out = result;
    return ok;
}
