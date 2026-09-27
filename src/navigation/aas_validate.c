#include "asset_internal.h"

static bool index_valid(int32_t value, size_t count) { return value >= 0 && (size_t)value < count; }
static bool oriented_valid(int32_t value, size_t count) {
    return (uint64_t)(value < 0 ? -(int64_t)value : value) < count;
}
static bool span_valid(int32_t first, int32_t count, size_t length) {
    return first >= 0 && count >= 0 && (size_t)first <= length &&
           (size_t)count <= length - (size_t)first;
}
static bool valid_topology(const qa_aas_view *v, qa_error *e) {
    typedef struct visit {
        uint32_t node, child;
    } visit;
    size_t count = v->count[QA_AAS_NODES];
    if (count < 2)
        return true;
    size_t size = 0, colors_at, stack_at;
    if (!nav_layout(&size, count, 1, &colors_at) ||
        !nav_layout(&size, count, sizeof(visit), &stack_at)) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "AAS topology workspace overflow");
        return false;
    }
    uint8_t *storage = calloc(size, 1);
    if (!storage) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating AAS topology workspace");
        return false;
    }
    uint8_t *colors = storage + colors_at;
    visit *stack = (void *)(storage + stack_at);
    for (size_t root = 1; root < count; ++root) {
        if (colors[root])
            continue;
        size_t used = 1;
        stack[0] = (visit){.node = (uint32_t)root};
        colors[root] = 1;
        while (used) {
            visit *current = &stack[used - 1];
            if (current->child == 2) {
                colors[current->node] = 2;
                --used;
                continue;
            }
            int32_t child = v->nodes[current->node].children[current->child++];
            if (child <= 0 || colors[child] == 2)
                continue;
            if (colors[child] == 1) {
                qa_error_set(e, QA_ERROR_FORMAT, current->node, "Cyclic AAS BSP nodes");
                free(storage);
                return false;
            }
            colors[child] = 1;
            stack[used++] = (visit){.node = (uint32_t)child};
        }
    }
    free(storage);
    return true;
}
bool qa_aas_validate(const qa_aas_view *v, qa_error *e) {
    if (v == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing AAS view");
        return false;
    }
    const void *fields[] = {v->boxes, v->vertices,   v->planes,       v->edges,    v->edge_index,
                            v->faces, v->face_index, v->areas,        v->settings, v->reachability,
                            v->nodes, v->portals,    v->portal_index, v->clusters};
    for (unsigned l = 0; l < QA_AAS_LUMP_COUNT; ++l)
        if (v->count[l] > INT32_MAX || (v->count[l] != 0 && fields[l] == NULL)) {
            qa_error_set(e, QA_ERROR_FORMAT, l, "Invalid AAS record table");
            return false;
        }
#define CHECK(condition, lump, ordinal)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            qa_error_set(e, QA_ERROR_FORMAT, ordinal, "Invalid AAS %s record", lump);              \
            return false;                                                                          \
        }                                                                                          \
    } while (0)
    CHECK(v->count[QA_AAS_AREAS] == v->count[QA_AAS_SETTINGS], "area/settings count", 0);
    for (size_t i = 0; i < v->count[QA_AAS_BOXES]; ++i)
        CHECK(nav_bounds_valid(v->boxes[i].bounds), "box", i);
    for (size_t i = 0; i < v->count[QA_AAS_VERTICES]; ++i)
        CHECK(qa_vec_finite(v->vertices[i]), "vertex", i);
    for (size_t i = 0; i < v->count[QA_AAS_PLANES]; ++i)
        CHECK(qa_vec_finite(v->planes[i].normal) && isfinite(v->planes[i].distance), "plane", i);
    for (size_t i = 0; i < v->count[QA_AAS_EDGES]; ++i) {
        const qa_aas_edge *p = v->edges + i;
        if (i == 0 && p->vertices[0] == 0 && p->vertices[1] == 0)
            continue;
        CHECK(index_valid(p->vertices[0], v->count[QA_AAS_VERTICES]) &&
                  index_valid(p->vertices[1], v->count[QA_AAS_VERTICES]),
              "edge", i);
    }
    for (size_t i = 0; i < v->count[QA_AAS_EDGE_INDEX]; ++i)
        CHECK(oriented_valid(v->edge_index[i], v->count[QA_AAS_EDGES]), "edge index", i);
    for (size_t i = 0; i < v->count[QA_AAS_FACES]; ++i) {
        const qa_aas_face *p = v->faces + i;
        CHECK(index_valid(p->plane, v->count[QA_AAS_PLANES]) &&
                  span_valid(p->first_edge, p->edge_count, v->count[QA_AAS_EDGE_INDEX]) &&
                  index_valid(p->front_area, v->count[QA_AAS_AREAS]) &&
                  index_valid(p->back_area, v->count[QA_AAS_AREAS]),
              "face", i);
    }
    for (size_t i = 0; i < v->count[QA_AAS_FACE_INDEX]; ++i)
        CHECK(oriented_valid(v->face_index[i], v->count[QA_AAS_FACES]), "face index", i);
    for (size_t i = 0; i < v->count[QA_AAS_AREAS]; ++i) {
        const qa_aas_area *p = v->areas + i;
        CHECK(p->number == (int32_t)i && nav_bounds_valid(p->bounds) && qa_vec_finite(p->center) &&
                  span_valid(p->first_face, p->face_count, v->count[QA_AAS_FACE_INDEX]),
              "area", i);
        const qa_aas_setting *s = v->settings + i;
        CHECK(span_valid(s->first_reach, s->reach_count, v->count[QA_AAS_REACHABILITY]),
              "area setting", i);
        CHECK(s->cluster >= 0
                  ? (s->cluster == 0 || index_valid(s->cluster, v->count[QA_AAS_CLUSTERS]))
                  : (uint64_t)(-(int64_t)s->cluster) < v->count[QA_AAS_PORTALS],
              "area cluster", i);
        CHECK(s->cluster_area >= 0, "cluster area", i);
    }
    for (size_t i = 0; i < v->count[QA_AAS_REACHABILITY]; ++i) {
        const qa_aas_reach *p = v->reachability + i;
        CHECK(index_valid(p->area, v->count[QA_AAS_AREAS]) && qa_vec_finite(p->start) &&
                  qa_vec_finite(p->end),
              "reachability", i);
    }
    for (size_t i = 1; i < v->count[QA_AAS_NODES]; ++i) {
        const qa_aas_node *p = v->nodes + i;
        CHECK(index_valid(p->plane, v->count[QA_AAS_PLANES]), "node plane", i);
        for (unsigned j = 0; j < 2; ++j)
            CHECK(oriented_valid(p->children[j],
                                 v->count[p->children[j] > 0 ? QA_AAS_NODES : QA_AAS_AREAS]),
                  "node child", i);
    }
    for (size_t i = 0; i < v->count[QA_AAS_PORTALS]; ++i) {
        const qa_aas_portal *p = v->portals + i;
        CHECK(index_valid(p->area, v->count[QA_AAS_AREAS]) &&
                  index_valid(p->front_cluster, v->count[QA_AAS_CLUSTERS]) &&
                  index_valid(p->back_cluster, v->count[QA_AAS_CLUSTERS]) &&
                  p->cluster_areas[0] >= 0 && p->cluster_areas[1] >= 0,
              "portal", i);
    }
    for (size_t i = 0; i < v->count[QA_AAS_PORTAL_INDEX]; ++i)
        CHECK(index_valid(v->portal_index[i], v->count[QA_AAS_PORTALS]), "portal index", i);
    for (size_t i = 0; i < v->count[QA_AAS_CLUSTERS]; ++i) {
        const qa_aas_cluster *p = v->clusters + i;
        CHECK(p->area_count >= 0 && p->reachable_area_count >= 0 &&
                  p->reachable_area_count <= p->area_count &&
                  span_valid(p->first_portal, p->portal_count, v->count[QA_AAS_PORTAL_INDEX]),
              "cluster", i);
    }
#undef CHECK
    return valid_topology(v, e);
}
