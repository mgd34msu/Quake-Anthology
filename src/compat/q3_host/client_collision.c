#include "internal.h"

enum { BOX_HANDLE = 255, CAPSULE_HANDLE = 254, BODY_CONTENTS = 0x02000000 };

static bool resolve(qa_collision_geometry *geometry, int32_t handle, bool *temporary,
                      qa_error *error)
{
    size_t count = qa_collision_model_count(geometry);
    if (handle >= 0 && (uint32_t)handle < count) { *temporary = false; return true; }
    if (handle == BOX_HANDLE) { *temporary = true; return true; }
    return q3_fail(error, QA_ERROR_ARGUMENT, (uint32_t)handle, "CM_ClipHandleToModel: bad handle");
}

static bool bounds_vector(q3_call *call, uint64_t address, qa_vec3 *value, qa_error *error)
{
    if (!address) { *value = qa_v3(0, 0, 0); return true; }
    return q3_vector(call, address, value, error);
}

static bool temp_box(qa_q3_host *host, qa_bounds bounds, bool capsule, qa_error *error)
{
    host->clip_bounds = bounds;
    if (capsule) return true;
    if (!qa_vec_finite(bounds.mins) || !qa_vec_finite(bounds.maxs))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "temporary box requires finite plane distances");
    host->clip_brush = bounds;
    return true;
}

static bool point(q3_call *call, qa_collision_geometry *geometry, bool nodes,
                     bool transformed, int32_t *result, qa_error *error)
{
    if (!transformed && !nodes) { *result = 0; return true; }
    int32_t handle = q3_integer(call, 1);
    qa_point_query query = {.policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    if (!q3_vector(call, call->arguments[0], &query.point, error)) return false;
    if (transformed) {
        if (!q3_vector(call, call->arguments[2], &query.target.origin, error) ||
            (handle != BOX_HANDLE &&
             !q3_vector(call, call->arguments[3], &query.target.angles, error))) return false;
    }
    if (!nodes) { *result = 0; return true; }
    bool temporary;
    if (!resolve(geometry, handle, &temporary, error)) return false;
    if (temporary) {
        if (!qa_vec_finite(query.point) || !qa_vec_finite(query.target.origin))
            return q3_fail(error, QA_ERROR_ARGUMENT, 0, "point contents requires finite coordinates");
        qa_vec3 p = qa_vec_sub(query.point, query.target.origin);
        qa_bounds b = call->host->clip_brush;
        *result = p.x >= b.mins.x && p.y >= b.mins.y && p.z >= b.mins.z &&
                  p.x <= b.maxs.x && p.y <= b.maxs.y && p.z <= b.maxs.z ? BODY_CONTENTS : 0;
        return true;
    }
    query.target.inline_model = transformed || handle != 0;
    query.target.model = (uint32_t)handle;
    qa_point_contents contents;
    if (!qa_collision_point_contents(geometry, &query, &contents, error)) return false;
    *result = contents.contents; return true;
}

static bool trace(q3_call *call, qa_collision_geometry *geometry, bool nodes,
                     bool transformed, bool capsule, qa_error *error)
{
    q3_record output;
    if (!q3_record_open(call, call->arguments[0], 56, &output, error)) return false;
    int32_t handle = q3_integer(call, 5);
    bool temporary;
    qa_trace_result result = {.family = QA_COLLISION_Q3, .fraction = 1};
    if (!transformed && !nodes) {
        if (!resolve(geometry, handle, &temporary, error)) return false;
        return qa_q3_abi_write_trace(&output.abi, 0, &result, 0, error);
    }
    qa_trace_query query = {.policy = qa_collision_default_policy(QA_COLLISION_Q3),
        .shape.kind = capsule ? QA_SHAPE_CAPSULE : QA_SHAPE_BOX};
    query.policy.contents_mask = (uint32_t)q3_integer(call, 6);
    if (!q3_vector(call, call->arguments[1], &query.start, error) ||
        !q3_vector(call, call->arguments[2], &query.end, error) ||
        !bounds_vector(call, call->arguments[3], &query.shape.bounds.mins, error) ||
        !bounds_vector(call, call->arguments[4], &query.shape.bounds.maxs, error)) return false;
    if (transformed) {
        if (!q3_vector(call, call->arguments[7], &query.target.origin, error) ||
            (handle != BOX_HANDLE &&
             !q3_vector(call, call->arguments[8], &query.target.angles, error))) return false;
    }
    if (!resolve(geometry, handle, &temporary, error)) return false;
    if (!nodes) result.end = query.end;
    else if (handle == CAPSULE_HANDLE) {
        qa_bounds bounds;
        if (!qa_collision_model_bounds(geometry, (uint32_t)handle, &bounds, error)) return false;
        if (!capsule) {
            qa_vec3 center = qa_vec_scale(qa_vec_add(query.shape.bounds.mins, query.shape.bounds.maxs), 0.5f);
            qa_bounds moving = {qa_vec_sub(query.shape.bounds.mins, center),
                                qa_vec_sub(query.shape.bounds.maxs, center)};
            if (transformed) {
                center = qa_vec_scale(qa_vec_add(moving.mins, moving.maxs), 0.5f);
                moving.mins = qa_vec_sub(moving.mins, center);
                moving.maxs = qa_vec_sub(moving.maxs, center);
            }
            if (!temp_box(call->host, moving, false, error)) return false;
        }
        if (!qa_collision_trace_q3_capsule(geometry, &query, bounds, transformed, &result, error)) return false;
    } else if (temporary) {
        if (!qa_collision_trace_q3_box(&query, call->host->clip_brush,
                                        transformed, &result, error)) return false;
    } else if (!qa_collision_trace_q3_model(geometry, &query, (uint32_t)handle,
                                            transformed, &result, error)) return false;
    return qa_q3_abi_write_trace(&output.abi, 0, &result, 0, error);
}

q3_service_result q3_client_collision(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_CGAME) return Q3_UNHANDLED;
    int32_t service = call->service;
    if (service != 18 && service != 19 && service != 20 && service != 22 &&
        service != 23 && service != 24 && service != 25 && service != 26 &&
        service != 82 && service != 83 && service != 84) return Q3_UNHANDLED;
    qa_q3_host_collision_services *services = &call->host->options.collision;
    if (service == 18) {
        qa_buffer name = {0};
        if (!q3_string(call, call->arguments[0], &name, error)) return Q3_FAILED;
        bool ok = services->load_map ? services->load_map(services->context, (const char *)name.data, error) :
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 client map owner is unbound");
        qa_buffer_free(&name);
        return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    qa_collision_geometry *geometry = services->geometry ? services->geometry(services->context) : NULL;
    if (!geometry) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 client collision map is unbound"); return Q3_FAILED;
    }
    bool nodes = qa_bsp_record_count(qa_collision_bsp(geometry), QA_BSP_NODES) != 0;
    if (service == 19) { *result = (int32_t)qa_collision_model_count(geometry); return Q3_COMPLETED; }
    if (service == 20) {
        int32_t model = q3_integer(call, 0);
        if (model < 0 || (uint32_t)model >= qa_collision_model_count(geometry)) {
            q3_fail(error, QA_ERROR_ARGUMENT, (uint32_t)model, "CM_InlineModel: bad number"); return Q3_FAILED;
        }
        *result = model; return Q3_COMPLETED;
    }
    bool ok;
    if (service == 22 || service == 82) {
        qa_bounds bounds;
        ok = q3_vector(call, call->arguments[0], &bounds.mins, error) &&
             q3_vector(call, call->arguments[1], &bounds.maxs, error) &&
             temp_box(call->host, bounds, service == 82, error);
        *result = service == 82 ? CAPSULE_HANDLE : BOX_HANDLE;
    } else if (service == 23 || service == 24)
        ok = point(call, geometry, nodes, service == 24, result, error);
    else ok = trace(call, geometry, nodes, service == 26 || service == 84,
                     service == 83 || service == 84, error);
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
