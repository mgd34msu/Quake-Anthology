#ifndef QA_NAVIGATION_H
#define QA_NAVIGATION_H

#include "qa/bsp.h"
#include "qa/movement.h"
#include "qa/navigation_asset.h"
#include "qa/strings.h"

typedef enum qa_nav_travel {
    QA_NAV_WALK,
    QA_NAV_CROUCH,
    QA_NAV_JUMP,
    QA_NAV_DROP,
    QA_NAV_SWIM,
    QA_NAV_WATER_JUMP,
    QA_NAV_LADDER,
    QA_NAV_TELEPORT,
    QA_NAV_MOVER,
    QA_NAV_JUMP_PAD,
    QA_NAV_ROCKET_JUMP,
    QA_NAV_BFG_JUMP,
    QA_NAV_GRAPPLE,
    QA_NAV_DOUBLE_JUMP,
    QA_NAV_RAMP_JUMP,
    QA_NAV_STRAFE_JUMP,
    QA_NAV_UNKNOWN,
    QA_NAV_TRAVEL_COUNT
} qa_nav_travel;
#define QA_NAV_CAPABILITY(mode) (UINT32_C(1) << (mode))
#define QA_NAV_NO_INDEX UINT32_MAX
enum { QA_NAV_WATER = 1, QA_NAV_SLIME = 2, QA_NAV_LAVA = 4, QA_NAV_CONTENTS_LADDER = 8 };
typedef enum qa_nav_origin_kind {
    QA_NAV_ORIGIN_AAS,
    QA_NAV_ORIGIN_NAV2,
    QA_NAV_ORIGIN_NAV3,
    QA_NAV_ORIGIN_CONSTRUCTED
} qa_nav_origin_kind;
typedef struct qa_nav_origin {
    qa_nav_origin_kind kind;
    uint32_t node, link, surface, leaf;
} qa_nav_origin;
typedef struct qa_nav_map {
    qa_string_id name;
    qa_bsp_format format;
    uint8_t digest[32];
} qa_nav_map;
typedef struct qa_nav_profile {
    qa_movement_profile movement;
    qa_trace_shape shape, crouched_shape;
    qa_trace_policy policy;
    uint32_t capabilities;
    float maximum_step, minimum_floor_normal, maximum_drop;
    uint8_t team; /* 0 independent, 1 source red, 2 source blue. */
    bool monster, has_crouched_shape;
} qa_nav_profile;
typedef struct qa_nav_binding {
    bool has_model;
    int32_t model;
    qa_bounds bounds;
    int32_t raw[2];
    uint8_t raw_count;
} qa_nav_binding;
typedef struct qa_nav_node {
    uint32_t id;
    qa_vec3 origin;
    qa_bounds bounds;
    float radius;
    uint32_t contents, flags, presence;
    int32_t source_cluster;
    qa_nav_origin source;
} qa_nav_node;
typedef struct qa_nav_edge {
    uint32_t id, from, to;
    qa_nav_travel mode;
    qa_vec3 start, end;
    float travel_seconds;
    uint32_t source_travel_type, source_flags;
    bool has_hint, has_entity;
    qa_nav_hint hint;
    qa_nav_binding entity;
    qa_nav_origin source;
} qa_nav_edge;
typedef enum qa_nav_mover_phase {
    QA_NAV_MOVER_BOTTOM,
    QA_NAV_MOVER_UP,
    QA_NAV_MOVER_TOP,
    QA_NAV_MOVER_DOWN
} qa_nav_mover_phase;
typedef struct qa_nav_train_stop {
    uint32_t id, next;
    qa_vec3 origin;
    float wait;
    bool teleport;
} qa_nav_train_stop;
typedef enum qa_nav_entity_kind {
    QA_NAV_ENTITY_GENERIC,
    QA_NAV_ENTITY_ELEVATOR,
    QA_NAV_ENTITY_TRAIN
} qa_nav_entity_kind;
typedef struct qa_nav_entity_state {
    qa_actor_id actor;
    bool enabled, locked, has_destination;
    qa_bounds bounds;
    qa_vec3 velocity, destination;
    qa_nav_entity_kind kind;
    union {
        struct {
            qa_vec3 origin, bottom, top;
            qa_nav_mover_phase phase;
        } elevator;
        struct {
            qa_vec3 origin;
            bool running;
            const qa_nav_train_stop *stops;
            size_t count;
        } train;
    } data;
} qa_nav_entity_state;
typedef struct qa_navigation_services {
    void *context;
    qa_world *world;
    uint64_t (*revision)(void *);
    bool (*hazard)(void *, qa_bounds);
    bool (*entity)(void *, const qa_nav_binding *, qa_nav_entity_state *, bool *found, qa_error *);
    /* Read the actor's selected continuation for a detached prediction. The
     * navigation owner copies it; prediction never commits gameplay effects. */
    bool (*movement_input)(void *, qa_actor_id, qa_movement_input *, qa_error *);
    /* Optional detached selected gameplay hooks. The lease and every hook's
     * mutable state belong solely to this prediction. Missing trace/contents
     * hooks use the shared world. End is paired with each successful begin. */
    bool (*prediction_begin)(void *, qa_actor_id, qa_movement_services *, void **lease, qa_error *);
    void (*prediction_end)(void *, void *lease);
} qa_navigation_services;
typedef struct qa_nav_graph qa_nav_graph;
typedef struct qa_navigation qa_navigation;
typedef struct qa_nav_workspace qa_nav_workspace;
typedef struct qa_nav_rejection {
    uint32_t connection;
    bool missing_start, missing_end;
} qa_nav_rejection;
typedef struct qa_nav_graph_view {
    qa_nav_map map;
    qa_nav_profile profile;
    const qa_nav_asset *asset;
    const qa_nav_node *nodes;
    const qa_nav_edge *edges;
    size_t node_count, edge_count, cluster_count;
    /* Directed strongly connected component index, one per node ordinal. */
    const uint32_t *clusters;
    const qa_nav_rejection *rejected;
    size_t rejected_count;
} qa_nav_graph_view;
typedef struct qa_nav_connection {
    uint32_t id, source_travel_type;
    qa_nav_travel mode;
    qa_vec3 from, to;
    float travel_seconds;
    bool has_hint, has_entity;
    qa_nav_hint hint;
    qa_nav_binding entity;
} qa_nav_connection;
typedef struct qa_nav_construction {
    const qa_bsp_view *geometry;
    qa_nav_map map;
    qa_nav_profile profile;
    float spacing, link_distance;
    size_t maximum_nodes;
    const qa_nav_connection *connections;
    size_t connection_count;
} qa_nav_construction;
bool qa_nav_graph_from_asset(const qa_nav_map *, qa_nav_asset *, const qa_nav_profile *,
                             const qa_navigation_services *, qa_nav_graph **, qa_error *);
bool qa_nav_graph_construct(const qa_nav_construction *, const qa_navigation_services *,
                            qa_nav_graph **, qa_error *);
void qa_nav_graph_retain(qa_nav_graph *);
void qa_nav_graph_release(qa_nav_graph *);
const qa_nav_graph_view *qa_nav_graph_read(const qa_nav_graph *);
qa_nav_travel qa_nav_aas_travel(uint32_t);
qa_nav_travel qa_nav_kex_travel(uint32_t);
uint32_t qa_nav_aas_travel_flag(uint32_t);
uint32_t qa_nav_area_travel_flags(const qa_aas_setting *);
uint32_t qa_nav_edge_travel_flag(const qa_nav_edge *);
bool qa_navigation_create(qa_nav_graph *, const qa_navigation_services *, qa_navigation **,
                          qa_error *);
void qa_navigation_destroy(qa_navigation *);
bool qa_nav_workspace_create(qa_nav_workspace **, qa_error *);
void qa_nav_workspace_destroy(qa_nav_workspace *);
bool qa_navigation_enable(qa_navigation *, uint32_t area, bool enabled, bool *previous, qa_error *);
bool qa_navigation_enabled(const qa_navigation *, uint32_t area, bool *enabled, bool *overridden);
bool qa_navigation_block(qa_navigation *, uint32_t edge, bool blocked, qa_error *);
uint64_t qa_navigation_generation(qa_navigation *);
const qa_nav_node *qa_navigation_node(const qa_navigation *, uint32_t);
const qa_nav_edge *qa_navigation_edge(const qa_navigation *, uint32_t);
/* Immutable borrowed graph/adjacency views; valid until runtime destruction. */
const qa_nav_graph_view *qa_navigation_graph(const qa_navigation *);
size_t qa_navigation_outgoing_count(const qa_navigation *, uint32_t node);
const qa_nav_edge *qa_navigation_outgoing(const qa_navigation *, uint32_t node, size_t index);
/* Both directions in graph insertion order, retaining parallel edges. */
size_t qa_navigation_adjacent_count(const qa_navigation *, uint32_t node);
typedef struct qa_nav_adjacency_cursor { uint32_t outgoing, incoming; } qa_nav_adjacency_cursor;
/* Zero the cursor before visiting a node. Both incident lists merge in graph
 * insertion order without scanning previously visited edges. */
const qa_nav_edge *qa_navigation_adjacent_next(const qa_navigation *, uint32_t node,
                                               qa_nav_adjacency_cursor *);
bool qa_navigation_area(qa_navigation *, qa_actor_id, qa_vec3, uint32_t *, bool *found, qa_error *);
bool qa_navigation_nearest(qa_navigation *, qa_actor_id, qa_vec3, float radius, uint32_t *,
                           bool *found, qa_error *);
bool qa_navigation_trace_areas(qa_navigation *, qa_nav_workspace *, qa_vec3, qa_vec3,
                               qa_aas_crossing *, size_t, size_t *, qa_error *);
bool qa_navigation_bbox_areas(qa_navigation *, qa_nav_workspace *, qa_bounds, uint32_t *, size_t,
                              size_t *, qa_error *);
bool qa_navigation_edge_allowed(qa_navigation *, qa_actor_id, uint32_t, bool *, qa_error *);
typedef struct qa_nav_train_ride {
    qa_nav_train_stop boarding, arrival;
} qa_nav_train_ride;
bool qa_navigation_train_ride(const qa_nav_entity_state *, const qa_nav_edge *,
                              const qa_nav_profile *, qa_nav_train_ride *);
typedef enum qa_nav_train_stage {
    QA_NAV_TRAIN_ABSENT,
    QA_NAV_TRAIN_APPROACH,
    QA_NAV_TRAIN_EXIT,
    QA_NAV_TRAIN_WAIT,
    QA_NAV_TRAIN_RIDE,
    QA_NAV_TRAIN_UNAVAILABLE
} qa_nav_train_stage;
typedef struct qa_nav_train_step {
    qa_nav_train_stage stage;
    qa_vec3 target;
} qa_nav_train_step;
bool qa_navigation_train_step(qa_navigation *, qa_nav_workspace *, qa_actor_id, uint32_t edge,
                              qa_vec3 origin, qa_actor_id ground, qa_nav_train_step *, qa_error *);
typedef struct qa_nav_route_query {
    qa_actor_id actor;
    qa_vec3 start, goal;
    uint32_t start_node, goal_node; /* QA_NAV_NO_INDEX requests spatial resolution. */
    uint32_t travel_flags;
    bool has_travel_flags;
    const uint32_t *disabled_areas;
    size_t disabled_count;
    void *context;
    bool (*edge_filter)(void *, const qa_nav_edge *);
} qa_nav_route_query;
typedef struct qa_nav_route {
    qa_nav_graph *graph; /* Retained on success; released by route_free/reuse. */
    uint32_t *nodes, *edges;
    qa_vec3 *points;
    size_t node_count, edge_count, point_count;
    size_t node_capacity, edge_capacity, point_capacity;
    float travel_seconds;
    uint64_t generation;
    bool found;
} qa_nav_route;
/* Routes reuse caller storage. A false found value is a valid unreachable
 * result; false return reports an invalid input or failed engine operation. */
bool qa_navigation_route(qa_navigation *, qa_nav_workspace *, const qa_nav_route_query *,
                         qa_nav_route *, qa_error *);
void qa_nav_route_free(qa_nav_route *);
/* Admits one authored connection through the selected real movement kernel.
 * Reuses the route's trajectory storage and returns found=false if rejected. */
bool qa_navigation_admit_edge(qa_navigation *, qa_actor_id, uint32_t edge, qa_vec3 origin,
                              qa_nav_route *, qa_error *);
/* Direct movement admission does not impose graph area/edge enablement. It is
 * used for geometric probes such as suspended goals and barrier jumps. */
bool qa_navigation_admit_movement(qa_navigation *, qa_actor_id, qa_vec3 from, qa_vec3 to,
                                  qa_nav_travel, qa_nav_route *, qa_error *);
bool qa_navigation_route_valid(qa_navigation *, qa_actor_id, const qa_nav_route *, bool *,
                               qa_error *);
typedef struct qa_nav_estimate {
    bool found;
    uint32_t travel_time, first_edge;
} qa_nav_estimate;
bool qa_navigation_aas_area_time(const qa_aas_setting *, qa_vec3, qa_vec3, uint16_t *, qa_error *);
bool qa_navigation_estimate(qa_navigation *, qa_nav_workspace *, uint32_t start, uint32_t goal,
                            const qa_vec3 *origin, uint32_t travel_flags, qa_nav_estimate *,
                            qa_error *);
typedef struct qa_nav_saved_area {
    uint32_t id;
    bool enabled;
} qa_nav_saved_area;
typedef struct qa_nav_saved_admission {
    uint32_t id;
    float seconds;
} qa_nav_saved_admission;
typedef struct qa_nav_checkpoint {
    uint32_t version;
    qa_nav_map map;
    uint64_t generation, world_revision;
    qa_nav_saved_area *enabled;
    uint32_t *blocked;
    qa_nav_saved_admission *admissions;
    size_t enabled_count, blocked_count, admission_count;
    size_t enabled_capacity, blocked_capacity, admission_capacity;
} qa_nav_checkpoint;
bool qa_navigation_capture(const qa_navigation *, qa_nav_checkpoint *, qa_error *);
bool qa_navigation_restore(qa_navigation *, const qa_nav_checkpoint *, qa_error *);
void qa_nav_checkpoint_free(qa_nav_checkpoint *);
typedef struct qa_nav_prediction_query {
    qa_actor_id actor;
    qa_vec3 origin, velocity, command_move;
    uint32_t presence, command_frames, maximum_frames, frame_ms, stop_events,
        stop_area; /* NO_INDEX disables target-area stop. */
    bool on_ground;
} qa_nav_prediction_query;
typedef struct qa_nav_prediction_result {
    qa_vec3 end, velocity;
    uint32_t end_area, frames, stop_event;
    float seconds;
    bool grounded, has_bounds, has_trace;
    int32_t water_level;
    qa_bounds bounds;
    qa_trace_query last_query;
    qa_trace_result last_trace;
    qa_vec3 *trajectory;
    size_t trajectory_count, trajectory_capacity;
} qa_nav_prediction_result;
bool qa_navigation_predict(qa_navigation *, qa_nav_workspace *, const qa_nav_prediction_query *,
                           qa_nav_prediction_result *, qa_error *);
void qa_nav_prediction_result_free(qa_nav_prediction_result *);

#endif
