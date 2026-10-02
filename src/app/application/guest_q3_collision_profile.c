#include "guest_q3_collision_profile.h"
#include "internal.h"
#include "qa/json.h"
#include <limits.h>
#include <float.h>
#include <math.h>

static bool field(const qa_json_document *document, qa_json_id root,
    const char *name, uint32_t *out, qa_error *error)
{
    uint64_t number;
    if (!qa_json_u64(document, qa_json_get(document, root, name), &number, error)) return false;
    if (number > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "CG collision declaration exceeds its source address domain");
    *out = (uint32_t)number;
    return true;
}

bool application_q3_collision_profile_read(const qa_qvm_image *image, qa_qvm_role role,
    qa_qvm_abi abi, qa_bytes declaration, qa_q3_host_collision_profile *out, qa_error *error)
{
    if (!out || !image)
        return application_fail(error, QA_ERROR_ARGUMENT, "CG collision profile needs its actual artifact");
    if (!declaration.size) { *out = (qa_q3_host_collision_profile){0}; return true; }
    qa_json_document *document = NULL;
    if (!qa_json_parse(declaration, &document, error)) return false;
    qa_json_id root = qa_json_root(document);
    qa_q3_host_collision_profile profile = {.present = true};
    uint32_t version;
    bool ok = qa_json_type(document, root) == QA_JSON_OBJECT &&
        field(document, root, "version", &version, error) && version == 1 &&
        qa_json_string_equal(document, qa_json_get(document, root, "algorithm"), "q3-cgame-solid-scene-v1");
#define READ(name, member) if (ok) ok = field(document, root, name, &profile.member, error)
    READ("snapshot", snapshot); READ("nextSnapshot", next_snapshot);
    READ("time", time); READ("physicsTime", physics_time);
    READ("thisFrameTeleport", this_frame_teleport); READ("nextFrameTeleport", next_frame_teleport);
    READ("processedSnapshot", processed_snapshot);
    READ("solidCount", solid_count); READ("solids", solids); READ("solidCapacity", solid_capacity);
    READ("entities", entities); READ("entityCount", entity_count); READ("entityStride", entity_stride);
    READ("currentState", current_state); READ("nextState", next_state);
    READ("lerpOrigin", lerp_origin); READ("lerpAngles", lerp_angles);
    READ("snapshotServerTime", snapshot_server_time);
    READ("buildSolidList", build_solids_entry); READ("trace", trace_entry);
    READ("pointContents", point_contents_entry);
#undef READ
    double gravity = 0;
    if (ok) ok = qa_json_number(document, qa_json_get(document, root, "trajectoryGravity"), &gravity, error) &&
        isfinite(gravity) && gravity >= -FLT_MAX && gravity <= FLT_MAX;
    if (ok) profile.trajectory_gravity = (float)gravity;
    if (ok) ok = qa_q3_host_collision_profile_qualify(image, role, abi, &profile, error);
    if (!ok && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_FORMAT, "Invalid CG collisionScene declaration");
    qa_json_destroy(document);
    if (ok) *out = profile;
    return ok;
}
