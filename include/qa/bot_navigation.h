#ifndef QA_BOT_NAVIGATION_H
#define QA_BOT_NAVIGATION_H

#include "qa/navigation.h"

typedef struct qa_bot_navigation qa_bot_navigation;
typedef struct qa_bot_navigation_observations {
    void *context;
    qa_actor_id (*actor)(void *, int32_t source_entity);
    int32_t (*model)(void *, qa_actor_id);
} qa_bot_navigation_observations;
/* Each query owner retains scratch storage and borrows one selected movement
 * runtime and the canonical world. Graphs and route caches remain shared.
 * Source area/reachability zero means absent; foreign graph IDs gain one. */
bool qa_bot_navigation_create(qa_navigation *, qa_world *, qa_actor_id,
                              const qa_bot_navigation_observations *,
                              qa_bot_navigation **, qa_error *);
/* Candidate-only construction also retains a retired saved actor binding.
 * Query scratch is fresh; actual graph and actor provenance are required. */
bool qa_bot_navigation_create_restored(qa_navigation *, qa_world *, qa_actor_id,
                                      const qa_bot_navigation_observations *,
                                      qa_bot_navigation **, qa_error *);
void qa_bot_navigation_destroy(qa_bot_navigation *);
bool qa_bot_navigation_bind(qa_bot_navigation *, qa_navigation *, qa_actor_id, qa_error *);
qa_navigation *qa_bot_navigation_runtime(const qa_bot_navigation *);
qa_actor_id qa_bot_navigation_actor(const qa_bot_navigation *);
uint32_t qa_bot_navigation_source_area(const qa_bot_navigation *, uint32_t node);
uint32_t qa_bot_navigation_node(const qa_bot_navigation *, uint32_t source_area);
const qa_nav_edge *qa_bot_navigation_reachability(const qa_bot_navigation *, uint32_t source_reach);
typedef struct qa_bot_nav_area {
    uint32_t contents, flags, presence;
    int32_t cluster;
    size_t reach_count;
} qa_bot_nav_area;
qa_bot_nav_area qa_bot_navigation_area(const qa_bot_navigation *, uint32_t source_area);
typedef struct qa_bot_nav_area_info {
    qa_bot_nav_area area;
    qa_bounds bounds;
    qa_vec3 origin;
} qa_bot_nav_area_info;
bool qa_bot_navigation_area_info(const qa_bot_navigation *, uint32_t, qa_bot_nav_area_info *);
/* A NULL point returns the source reachable-area count. */
bool qa_bot_navigation_reachability_index(qa_bot_navigation *, const qa_vec3 *, int32_t *, qa_error *);
qa_bounds qa_bot_navigation_presence(const qa_bot_navigation *, uint32_t presence);
bool qa_bot_navigation_point(qa_bot_navigation *, qa_vec3, uint32_t *source_area, qa_error *);
bool qa_bot_navigation_trace_areas(qa_bot_navigation *, qa_vec3 start, qa_vec3 end,
                                  qa_aas_crossing *, size_t capacity, size_t *, qa_error *);
bool qa_bot_navigation_trace_collect(qa_bot_navigation *, qa_vec3 start, qa_vec3 end,
                                     size_t maximum, qa_nav_crossings *, qa_error *);
bool qa_bot_navigation_bbox_areas(qa_bot_navigation *, qa_bounds, uint32_t *, size_t capacity,
                                 size_t *, qa_error *);
bool qa_bot_navigation_enable(qa_bot_navigation *, uint32_t source_area, bool, bool *previous,
                             qa_error *);
bool qa_bot_navigation_trace(qa_bot_navigation *, qa_vec3 start, qa_vec3 end,
                             const qa_bounds *, qa_actor_id pass, uint32_t q3_mask,
                             qa_trace_result *, qa_error *);
bool qa_bot_navigation_contents(qa_bot_navigation *, qa_vec3, int32_t *q3_contents, qa_error *);
bool qa_bot_navigation_selected_contents(qa_bot_navigation *, qa_vec3, qa_point_contents *, qa_error *);
bool qa_bot_navigation_swimming(qa_bot_navigation *, qa_vec3, bool *, qa_error *);
bool qa_bot_navigation_drop(qa_bot_navigation *, qa_vec3, qa_bounds, qa_vec3 *, bool *, qa_error *);
bool qa_bot_navigation_best(qa_bot_navigation *, qa_vec3, qa_bounds, qa_vec3 *, uint32_t *, qa_error *);
bool qa_bot_navigation_fuzzy(qa_bot_navigation *, qa_vec3, uint32_t *, qa_error *);
bool qa_bot_navigation_reachable(qa_bot_navigation *, qa_vec3, uint32_t *, qa_error *);
bool qa_bot_navigation_jump_pad(qa_bot_navigation *, qa_vec3, qa_bounds, uint32_t *, qa_error *);
typedef struct qa_bot_nav_route_query {
    uint32_t area, goal_area, travel_flags;
    qa_vec3 origin;
    bool has_origin;
} qa_bot_nav_route_query;
typedef struct qa_bot_nav_route {
    bool found;
    uint32_t travel_time, next_reachability;
} qa_bot_nav_route;
bool qa_bot_navigation_route(qa_bot_navigation *, const qa_bot_nav_route_query *,
                             qa_bot_nav_route *, qa_error *);
typedef enum qa_bot_route_stop {
    QA_BOT_ROUTE_NO_ROUTE = 1, QA_BOT_ROUTE_TRAVEL = 2,
    QA_BOT_ROUTE_CONTENTS = 4, QA_BOT_ROUTE_AREA = 8
} qa_bot_route_stop;
typedef struct qa_bot_route_prediction_query {
    qa_bot_nav_route_query route;
    int32_t maximum_areas, maximum_time;
    uint32_t stop_events, stop_contents, stop_travel_flags, stop_area;
} qa_bot_route_prediction_query;
typedef struct qa_bot_route_prediction {
    bool succeeded;
    uint32_t stop_event, end_area, end_contents, end_travel_flags;
    qa_vec3 end_position;
    int32_t time;
} qa_bot_route_prediction;
bool qa_bot_navigation_predict_route(qa_bot_navigation *, const qa_bot_route_prediction_query *,
                                     qa_bot_route_prediction *, qa_error *);
typedef enum qa_bot_alternative_type {
    QA_BOT_ALTERNATIVE_ALL = 1, QA_BOT_ALTERNATIVE_CLUSTER = 2, QA_BOT_ALTERNATIVE_VIEW = 4
} qa_bot_alternative_type;
typedef struct qa_bot_alternative_goal {
    qa_vec3 origin;
    uint32_t area, start_time, goal_time;
    uint16_t extra_time;
} qa_bot_alternative_goal;
bool qa_bot_navigation_alternatives(qa_bot_navigation *, const qa_bot_nav_route_query *,
                                    uint32_t types, int32_t maximum_goals,
                                    qa_bot_alternative_goal *, size_t capacity,
                                    size_t *, qa_error *);
typedef struct qa_bot_movement_prediction_query {
    qa_actor_id pass_actor;
    qa_vec3 origin, velocity, command_move;
    uint32_t presence, stop_events, stop_area;
    int32_t command_frames, maximum_frames;
    float frame_time;
    bool on_ground, world_only;
} qa_bot_movement_prediction_query;
typedef struct qa_bot_movement_prediction {
    bool succeeded;
    qa_vec3 end, velocity;
    uint32_t end_area, presence, stop_event, frames;
    int32_t end_contents;
    float time;
    struct {
        bool start_solid;
        float fraction;
        qa_vec3 end;
        qa_actor_id actor; /* Zero means the source trace entity number is zero. */
        uint32_t last_area, area, plane;
    } trace;
} qa_bot_movement_prediction;
/* Replays selected movement on detached state. An actorless map binding uses
 * its graph profile; pass_actor only excludes that actor from collision traces. */
bool qa_bot_navigation_predict_movement(qa_bot_navigation *,
                                        const qa_bot_movement_prediction_query *,
                                        qa_bot_movement_prediction *, qa_error *);

#endif
