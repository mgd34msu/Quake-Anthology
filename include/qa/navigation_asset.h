#ifndef QA_NAVIGATION_ASSET_H
#define QA_NAVIGATION_ASSET_H

#include "qa/common.h"
#include "qa/math.h"

typedef enum qa_nav_asset_kind { QA_NAV_AAS, QA_NAV_NAV2, QA_NAV_NAV3 } qa_nav_asset_kind;
typedef enum qa_aas_lump {
    QA_AAS_BOXES,
    QA_AAS_VERTICES,
    QA_AAS_PLANES,
    QA_AAS_EDGES,
    QA_AAS_EDGE_INDEX,
    QA_AAS_FACES,
    QA_AAS_FACE_INDEX,
    QA_AAS_AREAS,
    QA_AAS_SETTINGS,
    QA_AAS_REACHABILITY,
    QA_AAS_NODES,
    QA_AAS_PORTALS,
    QA_AAS_PORTAL_INDEX,
    QA_AAS_CLUSTERS,
    QA_AAS_LUMP_COUNT
} qa_aas_lump;
typedef struct qa_aas_box {
    int32_t presence, flags;
    qa_bounds bounds;
} qa_aas_box;
typedef struct qa_aas_plane {
    qa_vec3 normal;
    float distance;
    int32_t type;
} qa_aas_plane;
typedef struct qa_aas_edge {
    int32_t vertices[2];
} qa_aas_edge;
typedef struct qa_aas_face {
    int32_t plane, flags, edge_count, first_edge, front_area, back_area;
} qa_aas_face;
typedef struct qa_aas_area {
    int32_t number, face_count, first_face;
    qa_bounds bounds;
    qa_vec3 center;
} qa_aas_area;
typedef struct qa_aas_setting {
    int32_t contents, flags, presence, cluster, cluster_area, reach_count, first_reach;
} qa_aas_setting;
typedef struct qa_aas_reach {
    int32_t area, face, edge;
    qa_vec3 start, end;
    int32_t travel_type;
    uint16_t travel_time, padding;
} qa_aas_reach;
typedef struct qa_aas_node {
    int32_t plane, children[2];
} qa_aas_node;
typedef struct qa_aas_portal {
    int32_t area, front_cluster, back_cluster, cluster_areas[2];
} qa_aas_portal;
typedef struct qa_aas_cluster {
    int32_t area_count, reachable_area_count, portal_count, first_portal;
} qa_aas_cluster;
typedef struct qa_aas_view {
    uint32_t version;
    int32_t bsp_checksum;
    size_t count[QA_AAS_LUMP_COUNT];
    const qa_aas_box *boxes;
    const qa_vec3 *vertices;
    const qa_aas_plane *planes;
    const qa_aas_edge *edges;
    const int32_t *edge_index;
    const qa_aas_face *faces;
    const int32_t *face_index;
    const qa_aas_area *areas;
    const qa_aas_setting *settings;
    const qa_aas_reach *reachability;
    const qa_aas_node *nodes;
    const qa_aas_portal *portals;
    const int32_t *portal_index;
    const qa_aas_cluster *clusters;
} qa_aas_view;
typedef struct qa_nav_hint {
    qa_vec3 funnel, start, end, ladder_plane;
    bool has_ladder_plane;
} qa_nav_hint;
typedef struct qa_nav_source_node {
    uint16_t flags, first_link, link_count, radius;
    qa_vec3 origin;
} qa_nav_source_node;
typedef struct qa_nav_source_link {
    uint16_t target, traversal;
    uint8_t type, flags, stored_flags;
} qa_nav_source_link;
typedef struct qa_nav_source_entity {
    uint16_t link;
    int32_t model, tail[2];
    uint8_t tail_count;
    bool has_model;
    qa_bounds bounds;
} qa_nav_source_entity;
typedef struct qa_nav_source_view {
    qa_nav_asset_kind kind;
    uint32_t version;
    float heuristic;
    size_t node_count, link_count, traversal_count, entity_count;
    const qa_nav_source_node *nodes;
    const qa_nav_source_link *links;
    const qa_nav_hint *traversals;
    const qa_nav_source_entity *entities;
} qa_nav_source_view;
typedef struct qa_nav_asset qa_nav_asset;
/* The returned asset owns decoded records; input bytes may be released.
 * Views remain immutable and valid while any retained asset reference exists.
 */
bool qa_nav_asset_read(qa_bytes, const int32_t *expected_bsp_checksum, qa_nav_asset **, qa_error *);
void qa_nav_asset_retain(qa_nav_asset *);
void qa_nav_asset_release(qa_nav_asset *);
qa_nav_asset_kind qa_nav_asset_type(const qa_nav_asset *);
const qa_aas_view *qa_nav_asset_aas(const qa_nav_asset *);
const qa_nav_source_view *qa_nav_asset_kex(const qa_nav_asset *);
bool qa_aas_write(const qa_aas_view *, qa_buffer *, qa_error *);
bool qa_aas_validate(const qa_aas_view *, qa_error *);
typedef struct qa_aas_query qa_aas_query;
typedef struct qa_aas_crossing {
    uint32_t area;
    qa_vec3 point;
} qa_aas_crossing;
/* Zero-initialize once, then reuse. Collection resets count on failure and
 * retains storage. Results belong to the caller, independent of query scratch. */
typedef struct qa_nav_crossings {
    qa_aas_crossing *data;
    size_t count, capacity;
} qa_nav_crossings;
void qa_nav_crossings_free(qa_nav_crossings *);
bool qa_aas_query_create(qa_aas_query **, qa_error *);
void qa_aas_query_destroy(qa_aas_query *);
bool qa_aas_point_area(const qa_aas_view *, qa_vec3, uint32_t *, qa_error *);
bool qa_aas_trace_areas(const qa_aas_view *, qa_aas_query *, qa_vec3 start, qa_vec3 end,
                        qa_aas_crossing *, size_t capacity, size_t *count, qa_error *);
bool qa_aas_trace_collect(const qa_aas_view *, qa_aas_query *, qa_vec3 start, qa_vec3 end,
                          size_t maximum, qa_nav_crossings *, qa_error *);
bool qa_aas_bbox_areas(const qa_aas_view *, qa_aas_query *, qa_bounds, uint32_t *, size_t capacity,
                       size_t *count, qa_error *);

#endif
