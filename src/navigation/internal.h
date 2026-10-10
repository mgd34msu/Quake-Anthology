#ifndef QA_NAVIGATION_INTERNAL_H
#define QA_NAVIGATION_INTERNAL_H
#include "asset_internal.h"
#include "qa/navigation.h"
#include "qa/pool.h"
#include <float.h>

struct qa_nav_graph {
    atomic_uint references;
    qa_nav_graph_view view;
    qa_nav_node *nodes;
    qa_nav_edge *edges;
    size_t node_capacity, edge_capacity;
    uint32_t *node_lookup, *edge_lookup, *outgoing, *first_out, *incoming, *first_in, *clusters;
    size_t node_lookup_count, edge_lookup_count;
    qa_nav_rejection *rejected;
    uint16_t *portal_maxima;
    uint32_t *mover_outgoing, *first_mover_out;
};
typedef struct nav_queue_entry {
    uint32_t node;
    float cost;
} nav_queue_entry;
typedef struct nav_crossing {
    qa_aas_crossing crossing;
    float fraction;
    uint32_t ordinal;
} nav_crossing;
typedef struct nav_estimate_cache nav_estimate_cache;
struct qa_navigation {
    qa_nav_graph *graph;
    qa_navigation_services services;
    int8_t *enabled;
    uint8_t *blocked;
    float *admission_seconds;
    uint64_t generation, world_revision, topology_revision;
    nav_estimate_cache *estimates;
    qa_arena prediction_storage;
    qa_pool prediction_results;
};
struct qa_nav_workspace {
    qa_aas_query *aas;
    float *costs;
    uint32_t *parents, *path, *repair;
    size_t *queue_positions;
    int8_t *grounded, *waiting;
    uint8_t *rejected;
    nav_queue_entry *queue;
    nav_crossing *crossings;
    size_t node_capacity, edge_capacity, queue_capacity, queue_count, crossing_capacity;
};
typedef struct nav_prediction {
    qa_navigation *navigation;
    qa_movement_result *result;
    size_t result_slot;
    qa_movement_input input;
    qa_movement_services supplied, services;
    qa_actor_id pass_actor;
    void *lease;
    qa_vec3 pml_origin;
    uint64_t sequence;
    bool initialized, has_lease, has_traversal;
    bool has_trace, damaging_fall, world_only;
    qa_trace_query last_query;
    qa_trace_result last_trace;
} nav_prediction;
bool nav_workspace_prepare(qa_nav_workspace *, const qa_nav_graph *, qa_error *);
bool nav_reserve(void **, size_t *, size_t, size_t, qa_error *);
bool nav_queue_push(qa_nav_workspace *, nav_queue_entry, qa_error *);
bool nav_queue_pop(qa_nav_workspace *, nav_queue_entry *);
void nav_queue_clear(qa_nav_workspace *);
void nav_queue_remove(qa_nav_workspace *, uint32_t);
void nav_refresh(qa_navigation *);
void nav_estimates_free(qa_navigation *);
bool nav_aas_estimate_topology(qa_nav_graph *, qa_error *);
bool nav_static_node(const qa_navigation *, uint32_t, const qa_nav_route_query *);
bool nav_static_edge(const qa_navigation *, uint32_t, const qa_nav_route_query *);
bool nav_node_allowed(qa_navigation *, qa_actor_id, uint32_t, const qa_nav_route_query *, bool,
                      bool *, qa_error *);
bool nav_edge_allowed(qa_navigation *, qa_nav_workspace *, qa_actor_id, uint32_t,
                      const qa_nav_route_query *, bool *, qa_error *);
bool nav_entity(qa_navigation *, const qa_nav_edge *, qa_nav_entity_state *, bool *, qa_error *);
bool nav_boarding(qa_navigation *, uint32_t, bool train, const qa_nav_edge **,
                  qa_nav_entity_state *, qa_nav_train_ride *, bool *, qa_error *);
bool nav_route_point(qa_nav_route *, qa_vec3, qa_error *);
void nav_prediction_close(nav_prediction *);
bool nav_predict(nav_prediction *, qa_actor_id, qa_vec3, qa_vec3, qa_nav_travel, qa_nav_route *,
                 bool *, qa_error *);
bool nav_profile_valid(const qa_nav_profile *, qa_error *);
bool nav_services_valid(const qa_navigation_services *, bool prediction, qa_error *);
bool nav_trace(const qa_navigation_services *, const qa_nav_profile *, qa_actor_id, qa_vec3,
               qa_vec3, bool geometry_only, qa_trace_result *, qa_error *);
bool nav_clear(const qa_navigation_services *, const qa_nav_profile *, qa_actor_id, qa_vec3,
               qa_vec3, bool geometry_only, bool *, qa_error *);
bool nav_contents(const qa_navigation_services *, const qa_nav_profile *, qa_actor_id, qa_vec3,
                  bool geometry_only, uint32_t *, qa_error *);
bool nav_crouch_profile(const qa_nav_profile *, qa_nav_profile *);
bool nav_node_profile(const qa_nav_profile *, const qa_nav_node *, qa_nav_profile *);
bool nav_graph_new(const qa_nav_map *, const qa_nav_profile *, qa_nav_graph **, qa_error *);
bool nav_graph_node(qa_nav_graph *, const qa_nav_node *, qa_error *);
bool nav_graph_edge(qa_nav_graph *, const qa_nav_edge *, qa_error *);
bool nav_graph_finish(qa_nav_graph *, qa_error *);
qa_nav_edge nav_asset_aas_edge(const qa_aas_view *,uint32_t from,uint32_t link);
qa_nav_edge nav_asset_kex_edge(const qa_nav_graph *,const qa_nav_source_view *,uint32_t from,
                              uint32_t link,const qa_nav_source_entity *);
static inline float nav_distance(qa_vec3 a, qa_vec3 b) { return qa_vec_length(qa_vec_sub(a, b)); }
static inline qa_vec3 nav_midpoint(qa_vec3 a, qa_vec3 b) {
    return qa_vec_scale(qa_vec_add(a, b), 0.5f);
}
static inline uint32_t nav_node_index(const qa_nav_graph *g, uint32_t id) {
    return id < g->node_lookup_count ? g->node_lookup[id] : QA_NAV_NO_INDEX;
}
static inline uint32_t nav_edge_index(const qa_nav_graph *g, uint32_t id) {
    return id < g->edge_lookup_count ? g->edge_lookup[id] : QA_NAV_NO_INDEX;
}
#endif
