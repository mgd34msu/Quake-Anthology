#include "asset_internal.h"

typedef struct aas_frame {
    int32_t node;
    uint32_t depth;
    qa_vec3 start, end;
} aas_frame;
struct qa_aas_query {
    aas_frame *stack;
    uint32_t *areas;
    uint8_t *seen;
    size_t stack_capacity, area_capacity;
};
bool qa_aas_query_create(qa_aas_query **out, qa_error *e) {
    if (out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing AAS query output");
        return false;
    }
    qa_aas_query *q = calloc(1, sizeof(*q));
    if (q == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating AAS query");
        return false;
    }
    *out = q;
    return true;
}
void qa_aas_query_destroy(qa_aas_query *q) {
    if (q != NULL) {
        free(q->stack);
        free(q->areas);
        free(q->seen);
        free(q);
    }
}
static bool reserve(qa_aas_query *q, const qa_aas_view *v, qa_error *e) {
    if (v->count[QA_AAS_NODES] > INT32_MAX || v->count[QA_AAS_AREAS] > INT32_MAX)
        goto memory;
    size_t stack = v->count[QA_AAS_NODES] + 2, areas = v->count[QA_AAS_AREAS];
    if (stack > SIZE_MAX / sizeof(*q->stack) || areas > SIZE_MAX / sizeof(*q->areas))
        goto memory;
    if (stack > q->stack_capacity) {
        aas_frame *p = realloc(q->stack, stack * sizeof(*p));
        if (p == NULL)
            goto memory;
        q->stack = p;
        q->stack_capacity = stack;
    }
    if (areas > q->area_capacity) {
        uint32_t *p = malloc(areas * sizeof(*p));
        uint8_t *seen = malloc(areas);
        if (p == NULL || seen == NULL) {
            free(p);
            free(seen);
            goto memory;
        }
        free(q->areas);
        free(q->seen);
        q->areas = p;
        q->seen = seen;
        q->area_capacity = areas;
    }
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Growing AAS query workspace");
    return false;
}
static const qa_aas_plane *plane(const qa_aas_view *v, int32_t node, uint32_t depth, qa_error *e) {
    if (depth >= v->count[QA_AAS_NODES] || node < 1 || (size_t)node >= v->count[QA_AAS_NODES]) {
        qa_error_set(e, QA_ERROR_FORMAT, depth, "Cyclic or invalid AAS BSP node");
        return NULL;
    }
    int32_t p = v->nodes[node].plane;
    if (p < 0 || (size_t)p >= v->count[QA_AAS_PLANES]) {
        qa_error_set(e, QA_ERROR_FORMAT, node, "Invalid AAS BSP plane");
        return NULL;
    }
    return v->planes + p;
}
static float distance(const qa_aas_plane *p, qa_vec3 point) {
    return qa_vec_dot(p->normal, point) - p->distance;
}
bool qa_aas_point_area(const qa_aas_view *v, qa_vec3 point, uint32_t *out, qa_error *e) {
    if (v == NULL || out == NULL || !qa_vec_finite(point)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid AAS point query");
        return false;
    }
    int32_t node = 1;
    uint32_t depth = 0;
    while (node > 0) {
        const qa_aas_plane *p = plane(v, node, depth++, e);
        if (p == NULL)
            return false;
        node = v->nodes[node].children[distance(p, point) > 0 ? 0 : 1];
    }
    uint64_t area = (uint64_t)(-(int64_t)node);
    if (area >= v->count[QA_AAS_AREAS]) {
        qa_error_set(e, QA_ERROR_FORMAT, area, "Invalid AAS area leaf");
        return false;
    }
    *out = (uint32_t)area;
    return true;
}
bool qa_aas_trace_areas(const qa_aas_view *v, qa_aas_query *q, qa_vec3 start, qa_vec3 end,
                        qa_aas_crossing *out, size_t capacity, size_t *count, qa_error *e) {
    if (v == NULL || q == NULL || count == NULL || (capacity != 0 && out == NULL) ||
        !qa_vec_finite(start) || !qa_vec_finite(end)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid AAS line query");
        return false;
    }
    if (!reserve(q, v, e))
        return false;
    size_t used = 1, written = 0;
    q->stack[0] = (aas_frame){.node = 1, .start = start, .end = end};
    while (used != 0 && written < capacity) {
        aas_frame f = q->stack[--used];
        if (f.node <= 0) {
            uint64_t area = (uint64_t)(-(int64_t)f.node);
            if (area >= v->count[QA_AAS_AREAS]) {
                qa_error_set(e, QA_ERROR_FORMAT, area, "Invalid AAS trace leaf");
                return false;
            }
            if (area != 0)
                out[written++] = (qa_aas_crossing){(uint32_t)area, f.start};
            continue;
        }
        const qa_aas_plane *p = plane(v, f.node, f.depth, e);
        if (p == NULL)
            return false;
        const qa_aas_node *n = v->nodes + f.node;
        float front = distance(p, f.start), back = distance(p, f.end);
        ++f.depth;
        if (front > 0 && back > 0) {
            f.node = n->children[0];
            q->stack[used++] = f;
        } else if (front <= 0 && back <= 0) {
            f.node = n->children[1];
            q->stack[used++] = f;
        } else {
            float fraction = fmaxf(0, fminf(1, front / (front - back)));
            qa_vec3 middle = qa_vec_lerp(f.start, f.end, fraction);
            q->stack[used++] = (aas_frame){n->children[front < 0 ? 0 : 1], f.depth, middle, f.end};
            q->stack[used++] =
                (aas_frame){n->children[front < 0 ? 1 : 0], f.depth, f.start, middle};
        }
    }
    *count = written;
    return true;
}
bool qa_aas_bbox_areas(const qa_aas_view *v, qa_aas_query *q, qa_bounds bounds, uint32_t *out,
                       size_t capacity, size_t *count, qa_error *e) {
    if (v == NULL || q == NULL || count == NULL || (capacity != 0 && out == NULL) ||
        !nav_bounds_valid(bounds)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid AAS bounds query");
        return false;
    }
    if (!reserve(q, v, e))
        return false;
    if (v->count[QA_AAS_AREAS] != 0)
        memset(q->seen, 0, v->count[QA_AAS_AREAS]);
    size_t used = 1, found = 0;
    q->stack[0] = (aas_frame){.node = 1};
    while (used != 0) {
        aas_frame f = q->stack[--used];
        if (f.node <= 0) {
            uint64_t area = (uint64_t)(-(int64_t)f.node);
            if (area >= v->count[QA_AAS_AREAS]) {
                qa_error_set(e, QA_ERROR_FORMAT, area, "Invalid AAS bounds leaf");
                return false;
            }
            if (area != 0 && !q->seen[area]) {
                q->seen[area] = 1;
                q->areas[found++] = (uint32_t)area;
            }
            continue;
        }
        const qa_aas_plane *p = plane(v, f.node, f.depth, e);
        if (p == NULL)
            return false;
        const qa_aas_node *n = v->nodes + f.node;
        qa_vec3 front = qa_v3(p->normal.x < 0 ? bounds.mins.x : bounds.maxs.x,
                              p->normal.y < 0 ? bounds.mins.y : bounds.maxs.y,
                              p->normal.z < 0 ? bounds.mins.z : bounds.maxs.z);
        qa_vec3 back = qa_v3(p->normal.x < 0 ? bounds.maxs.x : bounds.mins.x,
                             p->normal.y < 0 ? bounds.maxs.y : bounds.mins.y,
                             p->normal.z < 0 ? bounds.maxs.z : bounds.mins.z);
        if (distance(p, front) >= 0)
            q->stack[used++] = (aas_frame){.node = n->children[0], .depth = f.depth + 1};
        if (distance(p, back) < 0)
            q->stack[used++] = (aas_frame){.node = n->children[1], .depth = f.depth + 1};
    }
    size_t written = found < capacity ? found : capacity;
    for (size_t i = 0; i < written; ++i)
        out[i] = q->areas[found - 1 - i];
    *count = written;
    return true;
}
