#ifndef QA_Q3_HOST_COLLISION_H
#define QA_Q3_HOST_COLLISION_H

#include "qa/q3_host.h"

/* Addresses and record offsets come only from the exact CGAME artifact's
 * collisionScene declaration. They describe retained CG RAM, not CL history. */
typedef struct qa_q3_host_collision_profile {
    bool present;
    uint32_t snapshot, next_snapshot, time, physics_time;
    uint32_t this_frame_teleport, next_frame_teleport, processed_snapshot;
    uint32_t solid_count, solids, solid_capacity;
    uint32_t entities, entity_count, entity_stride;
    uint32_t current_state, next_state, lerp_origin, lerp_angles;
    uint32_t snapshot_server_time;
    uint32_t build_solids_entry, trace_entry, point_contents_entry;
    float trajectory_gravity;
} qa_q3_host_collision_profile;

typedef struct qa_q3_host_collision_scene qa_q3_host_collision_scene;
typedef struct qa_q3_host_collision_view {
    const qa_q3_host *host;
    const qa_qvm *vm;
    const qa_qvm_image *image;
    qa_session *session;
    qa_world *world;
    const qa_vfs *content;
    const void *frontend_lifetime;
    qa_collision_geometry *geometry;
    const struct qa_resource *map_resource;
    qa_actor_owner receiver;
    uint64_t service_owner, map_identity;
    uint32_t snapshot_address, next_snapshot_address;
    int32_t snapshot_time, next_snapshot_time, time, physics_time, processed_snapshot;
    bool this_frame_teleport, next_frame_teleport;
    size_t solid_count;
} qa_q3_host_collision_view;

typedef bool (*qa_q3_host_collision_current_fn)(void *, const qa_q3_host *,
    const qa_qvm *, const qa_qvm_image *, const qa_collision_geometry *, qa_error *);

bool qa_q3_host_collision_profile_qualify(const qa_qvm_image *, qa_qvm_role,
    qa_qvm_abi, const qa_q3_host_collision_profile *, qa_error *);
/* Bind before CG Init or after genuine saved RAM reconstruction. The parent
 * qualifies its actual module, content, source/client and map namespace. */
bool qa_q3_host_collision_scene_bind(qa_q3_host *, qa_qvm *, const qa_qvm_image *,
    const qa_q3_host_collision_profile *, qa_q3_host_collision_current_fn, void *, qa_error *);
bool qa_q3_host_collision_held(const qa_q3_host *);
bool qa_q3_host_collision_hold(qa_q3_host *, qa_q3_host_collision_scene **, qa_error *);
bool qa_q3_host_collision_current(const qa_q3_host_collision_scene *);
bool qa_q3_host_collision_read(const qa_q3_host_collision_scene *,
    qa_q3_host_collision_view *, bool *present, qa_error *);
bool qa_q3_host_collision_trace(qa_q3_host_collision_scene *, const qa_trace_query *,
    qa_trace_result *, qa_error *);
bool qa_q3_host_collision_point_contents(qa_q3_host_collision_scene *, const qa_point_query *,
    qa_point_contents *, qa_error *);
void qa_q3_host_collision_release(qa_q3_host_collision_scene *);

#endif
