#ifndef QA_COLLISION_INTERNAL_H
#define QA_COLLISION_INTERNAL_H

#include "qa/collision.h"
#include "qa/stamp.h"
#include <string.h>

typedef enum qa_leaf_visit { QA_LEAF_CONTINUE, QA_LEAF_STOP, QA_LEAF_FAILED } qa_leaf_visit;
typedef qa_leaf_visit (*qa_leaf_visit_fn)(void *, const qa_collision_leaf *, qa_error *);
bool qa_collision_walk_leaves(const qa_collision_geometry *, qa_trace_scratch *, qa_bounds, bool q1_touched,
    qa_leaf_visit_fn, void *, qa_leaf_list *, qa_error *);

typedef struct qa_collision_ops {
    void (*destroy)(void *);
    /* Results retain canonical contents and surface flags; the shared layer adapts contact metadata.
     * For Q1 policies on Q2/Q3 maps, also classify the reached segment's media. */
    bool (*trace)(const void *, void *, const qa_trace_query *, qa_trace_result *, qa_error *);
    bool (*point_contents)(const void *, void *, const qa_point_query *, qa_point_contents *, qa_error *);
    void *(*create_scratch)(const void *, qa_error *);
    void (*destroy_scratch)(void *);
} qa_collision_ops;
typedef struct qa_collision_kernel { void *state; const qa_collision_ops *ops; } qa_collision_kernel;
typedef struct qa_collision_node { uint32_t plane; int32_t children[2]; } qa_collision_node;
/* Geometry owns these immutable tables for the lifetime of its kernel. */
typedef struct qa_collision_topology {
    const qa_collision_plane *planes;
    const qa_collision_node *nodes;
    size_t plane_count, node_count;
} qa_collision_topology;
typedef struct qa_collision_side_distances { float first, last; } qa_collision_side_distances;
typedef qa_collision_side_distances (*qa_collision_side_distances_fn)(void *, size_t, bool);
typedef struct qa_collision_brush_contact { size_t side, secondary; } qa_collision_brush_contact;
/* Format readers supply shape distances; one clipper keeps native contact rules. */
bool qa_collision_trace_brush(void *, qa_collision_side_distances_fn,
    size_t first_side, size_t side_count, bool stationary,
    const qa_collision_brush_rules *, qa_collision_bits contents,
    qa_trace_result *, qa_collision_brush_contact *);

typedef struct qa_collision_trace_frame {
    int32_t child;
    float first, last;
    qa_vec3 start, end;
} qa_collision_trace_frame;
/* Contact rules belong to the caller; BSP readers supply geometry only.
 * NULL selects Q1 hull rules for internal medium probes. */
qa_collision_trace_rules qa_collision_rules(const qa_trace_policy *);
typedef struct qa_collision_tree_trace {
    const qa_collision_plane *planes;
    const qa_collision_node *nodes;
    qa_collision_trace_frame *stack;
    qa_vec3 start, end;
    qa_collision_tree_rules rules;
    qa_trace_result *result;
    void *context;
    float (*extent)(void *, uint32_t plane);
    void (*leaf)(void *, uint32_t leaf);
} qa_collision_tree_trace;
void qa_collision_trace_tree(const qa_collision_tree_trace *, int32_t headnode);
bool qa_q1_collision_create(const qa_bsp_view *, const qa_collision_topology *, qa_collision_kernel *, qa_error *);
bool qa_q2_collision_create(const qa_bsp_view *, const qa_collision_topology *, qa_collision_kernel *, qa_error *);
bool qa_q2_collision_set_material(void *, uint32_t texinfo, qa_bytes, qa_error *);
bool qa_q3_collision_create(const qa_bsp_view *, const qa_collision_topology *, qa_collision_kernel *, qa_error *);
/* Temporary actor geometry. Q1 preserves recursive box-hull behavior; Q3
 * implements boxes and capsules, including capsule-vs-capsule sweeps. */
bool qa_q1_trace_box(const qa_trace_query *, qa_bounds target, qa_vec3 origin, qa_trace_result *, qa_error *);
bool qa_q3_trace_shape(const qa_trace_query *, qa_shape_kind target_kind, qa_bounds target, qa_vec3 origin, qa_collision_bits contents, qa_trace_result *, qa_error *);
bool qa_q3_trace_model_source(const void *, void *, const qa_trace_query *, uint32_t, bool,
                               qa_trace_result *, qa_error *);
bool qa_q3_trace_box_source(const qa_trace_query *, qa_bounds, bool, qa_trace_result *, qa_error *);
bool qa_q3_trace_capsule_source(const qa_trace_query *, qa_bounds, bool transformed,
                                const void *replacement_map, void *scratch, qa_trace_result *, qa_error *);
void qa_collision_adapt_point(qa_point_contents *, const qa_trace_policy *);

static inline qa_vec3 qa_bsp_to_vec(qa_bsp_vec3 v) { return qa_v3(v.x,v.y,v.z); }
static inline qa_bounds qa_bsp_to_bounds(qa_bsp_bounds b) { return (qa_bounds){qa_bsp_to_vec(b.min),qa_bsp_to_vec(b.max)}; }
static inline qa_collision_plane qa_collision_make_plane(qa_vec3 n, float distance, int32_t type) {
    return (qa_collision_plane){n,distance,type,(uint8_t)((n.x<0.0f?1u:0u)|(n.y<0.0f?2u:0u)|(n.z<0.0f?4u:0u))};
}
static inline qa_collision_plane qa_collision_bsp_plane(qa_bsp_plane p) { return qa_collision_make_plane(qa_bsp_to_vec(p.normal),p.distance,p.type); }
static inline qa_trace_result qa_collision_empty_trace(const qa_trace_query *q, qa_game_family family) {
    qa_trace_result result={0}; result.family=family; result.fraction=1.0f; result.end=q->end;
    result.contents=(qa_collision_bits){0}; result.model=q->target.inline_model?q->target.model:0; return result;
}
static inline float qa_vec_component(qa_vec3 v, unsigned axis) { return axis==0?v.x:axis==1?v.y:v.z; }
static inline float qa_collision_plane_distance(qa_vec3 point, const qa_collision_plane *plane) {
    return (plane->type >= 0 && plane->type < 3
        ? qa_vec_component(point, (unsigned)plane->type) : qa_vec_dot(point, plane->normal)) - plane->distance;
}
static inline void qa_vec_set_component(qa_vec3 *v, unsigned axis, float value) { if(axis==0)v->x=value;else if(axis==1)v->y=value;else v->z=value; }
static inline float qa_collision_clamp_fraction(float fraction) { return fmaxf(0, fminf(1, fraction)); }
typedef struct qa_collision_role_rules {
    bool rotates, inverse_normal, sphere_bounds;
} qa_collision_role_rules;
extern const qa_collision_role_rules qa_collision_roles[QA_RULESET_Q3 + 1];
static inline bool qa_collision_pose_rotates(const qa_collision_target *target) {
    return qa_collision_roles[target->pose_rules].rotates &&
        (target->angles.x != 0 || target->angles.y != 0 || target->angles.z != 0);
}
void qa_collision_pose_basis(const qa_collision_target *, bool transformed, qa_vec3 basis[3]);
qa_vec3 qa_collision_pose_normal(qa_vec3, const qa_collision_target *, bool transformed, const qa_vec3 basis[3]);

/* Quake angle basis: forward, negative-right, up. */
static inline void qa_collision_basis(qa_vec3 angles, qa_vec3 basis[3]) {
    const float radians=0.017453292519943295769f;
    float sy=sinf(angles.y*radians),cy=cosf(angles.y*radians),sp=sinf(angles.x*radians),cp=cosf(angles.x*radians),sr=sinf(angles.z*radians),cr=cosf(angles.z*radians);
    basis[0]=qa_v3(cp*cy,cp*sy,-sp);
    basis[1]=qa_v3(sr*sp*cy-cr*sy,sr*sp*sy+cr*cy,sr*cp);
    basis[2]=qa_v3(cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp);
}
static inline qa_vec3 qa_collision_to_local(qa_vec3 v,const qa_vec3 basis[3]) { return qa_v3(qa_vec_dot(v,basis[0]),qa_vec_dot(v,basis[1]),qa_vec_dot(v,basis[2])); }
static inline qa_vec3 qa_collision_from_local(qa_vec3 v,const qa_vec3 basis[3]) { return qa_vec_add(qa_vec_add(qa_vec_scale(basis[0],v.x),qa_vec_scale(basis[1],v.y)),qa_vec_scale(basis[2],v.z)); }

#endif
