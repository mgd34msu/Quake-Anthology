#ifndef QA_COLLISION_H
#define QA_COLLISION_H

#include "qa/actors.h"
#include "qa/bsp.h"
#include "qa/math.h"
#include "qa/collision_bits.h"
#include "qa/ruleset.h"

typedef enum qa_shape_kind { QA_SHAPE_POINT, QA_SHAPE_BOX, QA_SHAPE_CAPSULE } qa_shape_kind;
typedef struct qa_trace_shape { qa_shape_kind kind; qa_bounds bounds; } qa_trace_shape;
typedef enum qa_q1_move_kind { QA_Q1_MOVE_NORMAL, QA_Q1_MOVE_NO_MONSTERS, QA_Q1_MOVE_MISSILE } qa_q1_move_kind;
typedef struct qa_collision_brush_rules {
    float epsilon;
    bool clamp_fractions, secondary_plane, zero_all_solid, zero_stationary;
} qa_collision_brush_rules;
typedef struct qa_collision_tree_rules { float epsilon, margin; bool reciprocal; } qa_collision_tree_rules;
typedef struct qa_collision_trace_rules {
    qa_collision_brush_rules brush;
    qa_collision_tree_rules tree;
    bool conservative_extent;
} qa_collision_trace_rules;
typedef struct qa_trace_behavior {
    qa_game_family contents_format; /* Native output boundary, not a second family identity. */
    qa_collision_trace_rules trace;
    bool hull_boxes, legacy_boxes, owner_pairs, occupancy_replaces, merged_contents;
} qa_trace_behavior;
extern const qa_trace_behavior qa_trace_behaviors[QA_RULESET_Q3 + 1];
typedef struct qa_trace_policy {
    const qa_trace_behavior *behavior;
    qa_collision_bits contents_mask;
    qa_q1_move_kind q1_move;
    int32_t q1_hull; /* -1 selects the Q1 box hull from its X width. */
    bool curves, player_curve_clip;
} qa_trace_policy;
typedef struct qa_collision_target {
    bool inline_model;
    uint32_t model;
    qa_vec3 origin, angles;
    qa_ruleset_id pose_rules; /* Linked entity role; independent of geometry and caller trace rules. */
} qa_collision_target;
typedef struct qa_trace_query {
    qa_vec3 start, end;
    qa_trace_shape shape;
    qa_trace_policy policy;
    qa_collision_target target;
    qa_actor_id pass_actor; /* Zero registry means no actor. */
    qa_actor_reference pass_source; /* Optional literal CLIENT source number. */
} qa_trace_query;
typedef struct qa_collision_plane { qa_vec3 normal; float distance; int32_t type; uint8_t signbits; } qa_collision_plane;
typedef struct qa_collision_surface { char name[64]; qa_collision_bits flags; int32_t value; char material[16]; } qa_collision_surface;
typedef enum qa_trace_hit { QA_TRACE_HIT_NONE, QA_TRACE_HIT_WORLD, QA_TRACE_HIT_ACTOR } qa_trace_hit;
typedef struct qa_trace_result {
    qa_game_family family;
    float fraction;
    qa_vec3 end;
    bool start_solid, all_solid, in_open, in_water, contact;
    qa_collision_plane plane; /* Retained source plane, including no-contact results. */
    qa_collision_plane contact_plane; /* Contact in world space; Q1 inline distance can differ. */
    qa_trace_hit hit;
    uint32_t model;
    qa_actor_id actor;
    qa_collision_bits contents, surface_flags;
    int32_t q1_opaque_token;
    bool has_surface, has_secondary;
    qa_collision_surface surface;
    qa_collision_plane secondary_plane;
    bool secondary_has_surface;
    qa_collision_surface secondary_surface;
} qa_trace_result;
typedef struct qa_point_query {
    qa_vec3 point;
    qa_collision_target target;
    qa_trace_policy policy;
    qa_actor_id pass_actor;
    qa_actor_reference pass_source;
    /* Q3 server traps query temporary Q3 brushes as BODY (capsule handles
     * rotate the point); ordinary shared queries use actor contents. */
    bool q3_server_entities;
} qa_point_query;
typedef struct qa_point_contents {
    qa_game_family family;
    qa_collision_bits contents, stored, merged;
    int32_t q1_opaque_token;
} qa_point_contents;
typedef struct qa_collision_leaf { uint32_t leaf; int64_t cluster, area; qa_collision_bits contents; int32_t q1_opaque_token; } qa_collision_leaf;
typedef struct qa_leaf_list {
    size_t count;
    int32_t topnode;
    bool overflow;
    /* Q3 last visited leaf with cluster != -1, including overflow; else 0. */
    uint32_t last_leaf;
} qa_leaf_list;
typedef struct qa_collision_geometry qa_collision_geometry;
typedef struct qa_trace_scratch qa_trace_scratch;
struct qa_resource;

/* Source Q2 solid and svflags select the temporary box contents; BSP models
 * retain their authored brushes. Rerelease adds player/projectile masks. */
qa_collision_bits qa_collision_q2_source_contents(uint32_t solid, uint32_t svflags, bool rerelease);

/* Retains the BSP view and derived collision data; source bytes must outlive it.
 * One geometry serves every gameplay policy. Queries only read it. */
bool qa_collision_create(const qa_bsp_view *, qa_collision_geometry **out, qa_error *);
void qa_collision_destroy(qa_collision_geometry *);
/* Each caller creates scratch during map load and uses it with that geometry.
 * Independent scratch owners can query one geometry concurrently. */
bool qa_trace_scratch_create(const qa_collision_geometry *, qa_trace_scratch **, qa_error *);
void qa_trace_scratch_destroy(qa_trace_scratch *);
bool qa_collision_retain(qa_collision_geometry *, qa_error *);
/* Production map owners bind their actual immutable source, without lookup. */
bool qa_collision_bind_resource(qa_collision_geometry *, struct qa_resource *, qa_error *);
const struct qa_resource *qa_collision_resource(const qa_collision_geometry *);
const qa_bsp_view *qa_collision_bsp(const qa_collision_geometry *);
qa_game_family qa_collision_geometry_family(const qa_collision_geometry *);
uint64_t qa_collision_map_identity(const qa_collision_geometry *);
size_t qa_collision_model_count(const qa_collision_geometry *);
bool qa_collision_model_bounds(const qa_collision_geometry *, uint32_t model, qa_bounds *, qa_error *);
/* Q2 .mat sidecar contents: first 15 bytes up to NUL. Invalid ASCII material
 * names clear the previous value and report a format error. */
bool qa_collision_set_surface_material(qa_collision_geometry *, uint32_t texinfo, qa_bytes, qa_error *);
bool qa_collision_trace(const qa_collision_geometry *, qa_trace_scratch *, const qa_trace_query *, qa_trace_result *, qa_error *);
/* Sweeps against one temporary body without requiring spatial publication.
 * Hosts use this for source APIs that explicitly name an entity to clip. */
bool qa_collision_trace_body(const qa_trace_query *, qa_game_family actor_family,
                             qa_shape_kind target_kind, qa_bounds target, qa_vec3 origin,
                             qa_collision_bits contents, qa_trace_result *, qa_error *);
/* Source Q3 capsule handle: target supplies the optional origin/angles. The
 * box-through-capsule swap resolves real Q3 submodel 255 when present. Handle
 * admission, no-node early returns and temporary-box state belong to the host. */
bool qa_collision_trace_q3_capsule(const qa_collision_geometry *, qa_trace_scratch *, const qa_trace_query *,
                                   qa_bounds capsule_bounds, bool transformed,
                                   qa_trace_result *, qa_error *);
/* Model selection is independent of source TransformedBoxTrace semantics. */
bool qa_collision_trace_q3_model(const qa_collision_geometry *, qa_trace_scratch *, const qa_trace_query *,
                                 uint32_t model, bool transformed, qa_trace_result *, qa_error *);
bool qa_collision_trace_q3_box(const qa_trace_query *, qa_bounds, bool transformed,
                               qa_trace_result *, qa_error *);
bool qa_collision_point_contents(const qa_collision_geometry *, qa_trace_scratch *, const qa_point_query *, qa_point_contents *, qa_error *);
typedef enum qa_leaf_query_rule { QA_LEAF_COLLISION, QA_LEAF_Q1 } qa_leaf_query_rule;
/* Mod_PointInLeaf starts node0 and uses float d > 0; collision keeps the
 * authored world root and ties front. */
bool qa_collision_point_leaf(const qa_collision_geometry *, qa_vec3, qa_leaf_query_rule, qa_collision_leaf *, qa_error *);
bool qa_collision_leaf_at(const qa_collision_geometry *, uint32_t, qa_collision_leaf *, qa_error *);
bool qa_collision_box_leaves(const qa_collision_geometry *, qa_trace_scratch *, qa_bounds, uint32_t *leaves, size_t capacity, qa_leaf_list *, qa_error *);
bool qa_collision_cluster_visible(const qa_collision_geometry *, int32_t from, int32_t to, bool phs, bool *, qa_error *);
/* Same visibility rules/rows as scalar targets; bits name nonnegative clusters. */
bool qa_collision_clusters_visible(const qa_collision_geometry *, qa_trace_scratch *,
    const int32_t *from, size_t from_count, qa_bytes targets, bool phs, bool *, qa_error *);
uint32_t qa_collision_cluster_count(const qa_collision_geometry *);
/* Original Q1 fat-PVS unions source leaf rows within signed plane distance 8.
 * Query the exact output extent first; no other-family visibility is implied.
 * The resulting caller-owned row can serve every entity in one source frame. */
size_t qa_collision_q1_pvs_bytes(const qa_collision_geometry *);
bool qa_collision_q1_fat_pvs(const qa_collision_geometry *, qa_trace_scratch *, qa_vec3 eye,
    uint8_t *, size_t capacity, qa_error *);
bool qa_collision_areas_connected(const qa_collision_geometry *, int32_t, int32_t, bool *, qa_error *);
/* Overwrites the required output bytes; written reports their count even when
 * capacity is insufficient. */
bool qa_collision_area_bits(const qa_collision_geometry *, int32_t area, uint8_t *, size_t capacity, size_t *written, qa_error *);
void qa_collision_no_areas(qa_collision_geometry *, bool);
/* Q2 portal state and contributions share one effective open state. Q3 keeps
 * counted symmetric area-pair references. Underflow fails without mutation. */
bool qa_collision_set_portal(qa_collision_geometry *, uint32_t portal, bool open, qa_error *);
bool qa_collision_adjust_portal(qa_collision_geometry *, uint32_t portal, int delta, qa_error *);
bool qa_collision_portal_state(const qa_collision_geometry *, uint32_t portal, bool *primary, uint32_t *contributions, qa_error *);
bool qa_collision_adjust_area_pair(qa_collision_geometry *, int32_t first, int32_t second, bool open, qa_error *);
uint32_t qa_collision_area_count(const qa_collision_geometry *);
typedef struct qa_collision_saved_portal {
    uint32_t portal, contributions;
    bool primary;
} qa_collision_saved_portal;
typedef struct qa_collision_portal_checkpoint {
    qa_game_family family;
    qa_bsp_format format;
    uint64_t map_identity; /* Stable fingerprint of the immutable BSP bytes. */
    uint32_t area_count;
    bool no_areas;
    qa_collision_saved_portal *portals; /* Q2, in ascending portal-ID order. */
    size_t portal_count;
    uint32_t *area_pairs; /* Q3, row-major symmetric reference counts. */
    size_t area_pair_count;
} qa_collision_portal_checkpoint;
/* Capture owns its arrays and leaves output unchanged on failure. Restore
 * validates the complete checkpoint before changing any live portal state. */
bool qa_collision_capture_portals(const qa_collision_geometry *, qa_collision_portal_checkpoint *, qa_error *);
void qa_collision_portal_checkpoint_free(qa_collision_portal_checkpoint *);
bool qa_collision_restore_portals(qa_collision_geometry *, const qa_collision_portal_checkpoint *, qa_error *);
/* Applies source contact conventions without converting canonical fields. */
void qa_collision_adapt_trace(qa_trace_result *, const qa_trace_policy *);
qa_trace_policy qa_collision_default_policy(qa_game_family);
qa_ruleset_id qa_collision_source_rules(qa_game_family);
/* Source link expansion only; loaded BSP model bounds and explicit host bounds stay separate. */
qa_bounds qa_collision_link_bounds(qa_bounds, qa_vec3 origin, qa_vec3 angles,
    bool rotated_brush, qa_vec3 padding, qa_ruleset_id);

#endif
