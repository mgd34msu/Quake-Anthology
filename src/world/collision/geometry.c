#include "internal.h"
#include "qa/binary.h"
#include "qa/vfs.h"

#include <limits.h>
#include <stdlib.h>

typedef struct geometry_portal { uint32_t contributions; bool known, primary; } geometry_portal;
struct qa_trace_scratch {
    const qa_collision_geometry *geometry;
    const qa_collision_ops *ops;
    void *kernel;
    int32_t *nodes;
    qa_stamp_set leaves;
    uint8_t *visibility_pending;
    size_t visibility_capacity;
};

struct qa_collision_geometry {
    size_t references;
    qa_resource *resource;
    qa_bsp_view bsp;
    qa_collision_family family;
    qa_collision_kernel kernel;
    qa_collision_plane *planes;
    qa_collision_node *nodes;
    qa_collision_leaf *leaves;
    qa_bounds *model_bounds;
    size_t plane_count, node_count, leaf_count, model_count;
    int32_t root;

    uint32_t cluster_count;
    size_t visibility_bytes, visibility_slots;
    uint8_t **pvs, **phs;
    uint8_t *pvs_rows, *phs_rows;

    uint32_t area_count;
    uint32_t *flood, *area_stack, *area_pairs;
    size_t area_pair_count;
    qa_bsp_area *areas;
    qa_bsp_area_portal *area_portals;
    size_t area_portal_count, known_portal_count;
    geometry_portal *portals;
    bool no_areas;
    uint64_t map_identity;
};

static bool load_visibility_rows(qa_collision_geometry *, qa_error *);

static bool geometry_fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

static void *geometry_array(size_t count, size_t width, qa_error *error)
{
    if (count == 0) return NULL;
    if (count > (size_t)PTRDIFF_MAX / width) {
        geometry_fail(error, QA_ERROR_MEMORY, "Collision geometry table is too large");
        return NULL;
    }
    void *array = calloc(count, width);
    if (array == NULL) geometry_fail(error, QA_ERROR_MEMORY, "Cannot allocate collision geometry table");
    return array;
}

bool qa_trace_scratch_create(const qa_collision_geometry *geometry,
    qa_trace_scratch **out, qa_error *error)
{
    if (!geometry || !out)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Trace scratch requires loaded geometry and output");
    qa_trace_scratch *scratch = geometry_array(1, sizeof(*scratch), error);
    if (!scratch) return false;
    scratch->geometry = geometry;
    scratch->ops = geometry->kernel.ops;
    scratch->visibility_capacity=((size_t)geometry->cluster_count+7u)/8u;
    scratch->visibility_pending=geometry_array(scratch->visibility_capacity,1,error);
    if(scratch->visibility_capacity && !scratch->visibility_pending) goto failed;
    scratch->nodes = geometry_array(geometry->node_count + 1, sizeof(*scratch->nodes), error);
    if (!scratch->nodes) goto failed;
    size_t leaves = geometry->family == QA_COLLISION_Q1 ? geometry->leaf_count : 0;
    uint32_t *marks = geometry_array(leaves, sizeof(*marks), error);
    if (leaves && !marks) goto failed;
    qa_stamp_set_init(&scratch->leaves, marks, leaves);
    scratch->kernel = scratch->ops->create_scratch(geometry->kernel.state, error);
    if (!scratch->kernel) goto failed;
    *out = scratch;
    return true;
failed:
    qa_trace_scratch_destroy(scratch);
    return false;
}

void qa_trace_scratch_destroy(qa_trace_scratch *scratch)
{
    if (!scratch) return;
    if (scratch->kernel) scratch->ops->destroy_scratch(scratch->kernel);
    free(scratch->visibility_pending);
    free(scratch->nodes);
    free(scratch->leaves.marks);
    free(scratch);
}

static size_t leaf_index(int32_t child)
{
    return (size_t)(-(int64_t)child - 1);
}

static size_t bit_bytes(uint32_t count)
{
    return (size_t)(((uint64_t)count + 7u) / 8u);
}

static bool bit_test(qa_bytes row, size_t bit)
{
    return bit / 8u < row.size && (row.data[bit / 8u] & (uint8_t)(1u << (bit % 8u))) != 0;
}

static bool portal_open(const geometry_portal *portal)
{
    return portal->primary || portal->contributions != 0;
}

static void flood_areas(qa_collision_geometry *geometry)
{
    if (geometry->area_count == 0) return;
    memset(geometry->flood, 0, (size_t)geometry->area_count * sizeof(*geometry->flood));
    uint32_t next_flood = 0;
    uint32_t first = geometry->family == QA_COLLISION_Q2 ? 1u : 0u;
    for (uint32_t area = first; area < geometry->area_count; ++area) {
        if (geometry->flood[area] != 0) continue;
        ++next_flood;
        size_t count = 1;
        geometry->area_stack[0] = area;
        geometry->flood[area] = next_flood;
        while (count != 0) {
            uint32_t current = geometry->area_stack[--count];
            if (geometry->family == QA_COLLISION_Q2) {
                qa_bsp_range range = geometry->areas[current].portals;
                for (size_t i = 0; i < (size_t)range.count; ++i) {
                    qa_bsp_area_portal edge = geometry->area_portals[(size_t)range.first + i];
                    if (!portal_open(&geometry->portals[edge.portal]) || geometry->flood[edge.other_area] != 0) continue;
                    geometry->flood[edge.other_area] = next_flood;
                    geometry->area_stack[count++] = edge.other_area;
                }
            } else if (geometry->family == QA_COLLISION_Q3) {
                for (uint32_t other = geometry->area_count; other != 0;) {
                    --other;
                    if (geometry->area_pairs[(size_t)current * geometry->area_count + other] == 0
                        || geometry->flood[other] != 0) continue;
                    geometry->flood[other] = next_flood;
                    geometry->area_stack[count++] = other;
                }
            }
        }
    }
}

void qa_collision_destroy(qa_collision_geometry *geometry)
{
    if (geometry == NULL) return;
    if (geometry->references > 1) { --geometry->references; return; }
    if (geometry->kernel.ops != NULL) geometry->kernel.ops->destroy(geometry->kernel.state);
    free(geometry->pvs_rows);
    free(geometry->phs_rows);
    free(geometry->pvs);
    free(geometry->phs);
    free(geometry->planes);
    free(geometry->nodes);
    free(geometry->leaves);
    free(geometry->model_bounds);
    free(geometry->flood);
    free(geometry->area_stack);
    free(geometry->area_pairs);
    free(geometry->areas);
    free(geometry->area_portals);
    free(geometry->portals);
    qa_resource_release(geometry->resource);
    free(geometry);
}

bool qa_collision_retain(qa_collision_geometry *geometry, qa_error *error)
{
    if (!geometry || !geometry->references || geometry->references == SIZE_MAX)
        return geometry_fail(error, QA_ERROR_MEMORY, "Retaining actual collision geometry");
    ++geometry->references; return true;
}
bool qa_collision_bind_resource(qa_collision_geometry *geometry, qa_resource *resource, qa_error *error)
{
    if (!geometry || !resource)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Collision source requires its actual geometry and resource");
    qa_bytes bytes = qa_resource_bytes(resource);
    if (bytes.data != geometry->bsp.source.data || bytes.size != geometry->bsp.source.size ||
        (geometry->resource && geometry->resource != resource))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Collision source differs from its genuine immutable BSP resource");
    if (!geometry->resource) { qa_resource_retain(resource); geometry->resource = resource; }
    return true;
}
const qa_resource *qa_collision_resource(const qa_collision_geometry *geometry)
{ return geometry ? geometry->resource : NULL; }

static bool load_topology(qa_collision_geometry *geometry, qa_error *error)
{
    const qa_bsp_view *bsp = &geometry->bsp;
    geometry->plane_count = qa_bsp_record_count(bsp, QA_BSP_PLANES);
    geometry->node_count = qa_bsp_record_count(bsp, QA_BSP_NODES);
    geometry->leaf_count = qa_bsp_record_count(bsp, QA_BSP_LEAVES);
    geometry->model_count = qa_bsp_record_count(bsp, QA_BSP_MODELS);
    if (geometry->leaf_count == 0 || geometry->model_count == 0)
        return geometry_fail(error, QA_ERROR_FORMAT, "Collision geometry requires a world model and leaves");
    if (geometry->node_count > (size_t)INT32_MAX + 1u || geometry->leaf_count > (size_t)INT32_MAX + 1u
        || geometry->model_count > UINT32_MAX)
        return geometry_fail(error, QA_ERROR_FORMAT, "Collision geometry indices exceed their native ranges");
#define GEOMETRY_ALLOC(member, count) do { \
    geometry->member = geometry_array((count), sizeof(*geometry->member), error); \
    if ((count) != 0 && geometry->member == NULL) return false; \
} while (0)
    GEOMETRY_ALLOC(planes, geometry->plane_count);
    GEOMETRY_ALLOC(nodes, geometry->node_count);
    GEOMETRY_ALLOC(leaves, geometry->leaf_count);
    GEOMETRY_ALLOC(model_bounds, geometry->model_count);
#undef GEOMETRY_ALLOC
    for (size_t i = 0; i < geometry->plane_count; ++i) {
        qa_bsp_plane plane;
        if (!qa_bsp_read_plane(bsp, i, &plane, error)) return false;
        geometry->planes[i] = qa_collision_bsp_plane(plane);
        if (geometry->family == QA_COLLISION_Q3)
            geometry->planes[i].type = plane.normal.x == 1 ? 0 : plane.normal.y == 1 ? 1 : plane.normal.z == 1 ? 2 : 3;
    }
    for (size_t i = 0; i < geometry->node_count; ++i) {
        qa_bsp_node node;
        if (!qa_bsp_read_node(bsp, i, &node, error)) return false;
        geometry->nodes[i] = (qa_collision_node){node.plane, {node.children[0], node.children[1]}};
    }
    qa_bytes vis = bsp->lumps[QA_BSP_VISIBILITY].bytes;
    for (size_t i = 0; i < geometry->leaf_count; ++i) {
        qa_bsp_leaf leaf;
        if (!qa_bsp_read_leaf(bsp, i, &leaf, error)) return false;
        int64_t cluster = leaf.cluster, area = leaf.area;
        if (geometry->family == QA_COLLISION_Q1) {
            cluster = i == 0 ? -1 : (int64_t)i - 1;
            area = 0;
        } else if (geometry->family == QA_COLLISION_Q2 && vis.size == 0 && cluster != -1) cluster = 0;
        qa_collision_terminal terminal = geometry->family == QA_COLLISION_Q1 ?
            qa_collision_q1_terminal(leaf.contents) : (qa_collision_terminal){
                qa_collision_contents_decode(leaf.contents, geometry->family), 0};
        geometry->leaves[i] = (qa_collision_leaf){(uint32_t)i, cluster, area,
            terminal.bits, terminal.opaque_token};
        if (cluster >= 0 && (uint64_t)cluster + 1u > geometry->cluster_count) {
            if ((uint64_t)cluster >= UINT32_MAX)
                return geometry_fail(error, QA_ERROR_FORMAT, "BSP cluster count exceeds the collision range");
            geometry->cluster_count = (uint32_t)cluster + 1u;
        }
        if (geometry->family == QA_COLLISION_Q3 && area >= 0 && (uint64_t)area + 1u > geometry->area_count) {
            if ((uint64_t)area >= UINT32_MAX)
                return geometry_fail(error, QA_ERROR_FORMAT, "BSP area count exceeds the collision range");
            geometry->area_count = (uint32_t)area + 1u;
        }
    }
    for (size_t i = 0; i < geometry->model_count; ++i) {
        qa_bsp_model model;
        if (!qa_bsp_read_model(bsp, i, &model, error)) return false;
        qa_bounds bounds = qa_bsp_to_bounds(model.bounds);
        bounds.mins = qa_vec_sub(bounds.mins, qa_v3(1, 1, 1));
        bounds.maxs = qa_vec_add(bounds.maxs, qa_v3(1, 1, 1));
        if (!qa_bounds_valid(bounds))
            return geometry_fail(error, QA_ERROR_FORMAT, "Invalid collision model bounds");
        geometry->model_bounds[i] = bounds;
        if (i == 0) {
            geometry->root = geometry->family == QA_COLLISION_Q3
                ? (geometry->node_count == 0 ? -1 : 0) : model.headnodes[0];
            if (geometry->family == QA_COLLISION_Q1) geometry->visibility_bytes = bit_bytes((uint32_t)model.visible_leaves);
        }
    }
    if (geometry->family != QA_COLLISION_Q1) {
        if (vis.size != 0) geometry->cluster_count = qa_load_u32le(vis.data);
        geometry->visibility_bytes = bit_bytes(geometry->cluster_count);
    }
    geometry->visibility_slots = geometry->family == QA_COLLISION_Q1 ? geometry->leaf_count : geometry->cluster_count;
    if (geometry->visibility_slots != 0 && geometry->visibility_bytes != 0) {
        if (geometry->family != QA_COLLISION_Q3) {
            geometry->pvs = geometry_array(geometry->visibility_slots, sizeof(*geometry->pvs), error);
            if (geometry->pvs == NULL) return false;
        }
        geometry->phs = geometry_array(geometry->visibility_slots, sizeof(*geometry->phs), error);
        if (geometry->phs == NULL) return false;
    }
    return true;
}

static bool load_areas(qa_collision_geometry *geometry, qa_error *error)
{
    if (geometry->family == QA_COLLISION_Q1) geometry->area_count = 1;
    if (geometry->family == QA_COLLISION_Q2) {
        size_t area_count = qa_bsp_record_count(&geometry->bsp, QA_BSP_AREAS);
        if (area_count > UINT32_MAX) return geometry_fail(error, QA_ERROR_FORMAT, "Too many Q2 collision areas");
        geometry->area_count = (uint32_t)area_count;
        geometry->area_portal_count = qa_bsp_record_count(&geometry->bsp, QA_BSP_AREA_PORTALS);
        geometry->areas = geometry_array(area_count, sizeof(*geometry->areas), error);
        geometry->area_portals = geometry_array(geometry->area_portal_count, sizeof(*geometry->area_portals), error);
        geometry->portals = geometry_array(geometry->area_portal_count, sizeof(*geometry->portals), error);
        if ((area_count != 0 && geometry->areas == NULL)
            || (geometry->area_portal_count != 0 && (geometry->area_portals == NULL || geometry->portals == NULL))) return false;
        for (size_t i = 0; i < area_count; ++i)
            if (!qa_bsp_read_area(&geometry->bsp, i, &geometry->areas[i], error)) return false;
        for (size_t i = 0; i < geometry->area_portal_count; ++i) {
            if (!qa_bsp_read_area_portal(&geometry->bsp, i, &geometry->area_portals[i], error)) return false;
            geometry_portal *portal = &geometry->portals[geometry->area_portals[i].portal];
            if (!portal->known) { portal->known = true; ++geometry->known_portal_count; }
        }
    } else if (geometry->family == QA_COLLISION_Q3) {
        size_t count = geometry->area_count;
        if (count != 0 && count > SIZE_MAX / count)
            return geometry_fail(error, QA_ERROR_MEMORY, "Q3 area-pair table is too large");
        geometry->area_pair_count = count * count;
        geometry->area_pairs = geometry_array(geometry->area_pair_count, sizeof(*geometry->area_pairs), error);
        if (geometry->area_pair_count != 0 && geometry->area_pairs == NULL) return false;
    }
    geometry->flood = geometry_array(geometry->area_count, sizeof(*geometry->flood), error);
    geometry->area_stack = geometry_array(geometry->area_count, sizeof(*geometry->area_stack), error);
    if (geometry->area_count != 0 && (geometry->flood == NULL || geometry->area_stack == NULL)) return false;
    flood_areas(geometry);
    return true;
}

bool qa_collision_create(const qa_bsp_view *bsp, qa_collision_geometry **out, qa_error *error)
{
    if (bsp == NULL || out == NULL || bsp->family < QA_BSP_Q1 || bsp->family > QA_BSP_Q3)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Collision creation requires a BSP view and output");
    if (!qa_bsp_validate(bsp, error)) return false;
    qa_collision_geometry *geometry = calloc(1, sizeof(*geometry));
    if (geometry == NULL) return geometry_fail(error, QA_ERROR_MEMORY, "Cannot allocate collision geometry");
    geometry->references = 1;
    geometry->bsp = *bsp;
    geometry->family = bsp->family == QA_BSP_Q1 ? QA_COLLISION_Q1 : bsp->family == QA_BSP_Q2 ? QA_COLLISION_Q2 : QA_COLLISION_Q3;
    geometry->map_identity = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < bsp->source.size; ++i) {
        geometry->map_identity ^= bsp->source.data[i];
        geometry->map_identity *= UINT64_C(1099511628211);
    }
    if (!load_topology(geometry, error) || !load_areas(geometry, error)) goto fail;
    const qa_collision_topology topology = {geometry->planes, geometry->nodes,
        geometry->plane_count, geometry->node_count};
    bool created = geometry->family == QA_COLLISION_Q1 ? qa_q1_collision_create(bsp, &topology, &geometry->kernel, error)
        : geometry->family == QA_COLLISION_Q2 ? qa_q2_collision_create(bsp, &topology, &geometry->kernel, error)
        : qa_q3_collision_create(bsp, &topology, &geometry->kernel, error);
    if (!created || !load_visibility_rows(geometry, error)) goto fail;
    *out = geometry;
    return true;
fail:
    qa_collision_destroy(geometry);
    return false;
}

const qa_bsp_view *qa_collision_bsp(const qa_collision_geometry *geometry)
{
    return geometry == NULL ? NULL : &geometry->bsp;
}

qa_collision_family qa_collision_geometry_family(const qa_collision_geometry *geometry)
{
    return geometry == NULL ? (qa_collision_family)0 : geometry->family;
}

uint64_t qa_collision_map_identity(const qa_collision_geometry *geometry)
{
    return geometry == NULL ? 0 : geometry->map_identity;
}

size_t qa_collision_model_count(const qa_collision_geometry *geometry)
{
    return geometry == NULL ? 0 : geometry->model_count;
}

bool qa_collision_model_bounds(const qa_collision_geometry *geometry, uint32_t model, qa_bounds *out, qa_error *error)
{
    if (geometry == NULL || out == NULL || (size_t)model >= geometry->model_count)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid collision model bounds query");
    *out = geometry->model_bounds[model];
    return true;
}

bool qa_collision_set_surface_material(qa_collision_geometry *geometry, uint32_t texinfo, qa_bytes bytes, qa_error *error)
{
    if (geometry == NULL || geometry->family != QA_COLLISION_Q2)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Material sidecars require Q2 collision geometry");
    return qa_q2_collision_set_material(geometry->kernel.state, texinfo, bytes, error);
}

static bool valid_policy(const qa_trace_policy *policy)
{
    return policy->family >= QA_COLLISION_Q1 && policy->family <= QA_COLLISION_Q3
        && (policy->family != QA_COLLISION_Q1 || ((unsigned)policy->q1_move <= (unsigned)QA_Q1_MOVE_MISSILE
            && policy->q1_hull >= -1 && policy->q1_hull < 3));
}

static bool valid_target(const qa_collision_geometry *geometry, const qa_collision_target *target)
{
    return !target->inline_model || ((size_t)target->model < geometry->model_count
        && qa_vec_finite(target->origin) && qa_vec_finite(target->angles));
}

bool qa_collision_trace(const qa_collision_geometry *geometry, qa_trace_scratch *scratch, const qa_trace_query *query, qa_trace_result *out, qa_error *error)
{
    if (geometry == NULL || query == NULL || out == NULL || !qa_vec_finite(query->start)
        || !qa_vec_finite(query->end) || !valid_policy(&query->policy) || !valid_target(geometry, &query->target)
        || (unsigned)query->shape.kind > (unsigned)QA_SHAPE_CAPSULE
        || (query->shape.kind != QA_SHAPE_POINT && !qa_bounds_valid(query->shape.bounds)))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid geometry trace query");
    qa_trace_query local = *query;
    if (local.shape.kind == QA_SHAPE_BOX && local.shape.bounds.mins.x == 0 && local.shape.bounds.mins.y == 0
        && local.shape.bounds.mins.z == 0 && local.shape.bounds.maxs.x == 0 && local.shape.bounds.maxs.y == 0
        && local.shape.bounds.maxs.z == 0) local.shape.kind = QA_SHAPE_POINT;
    qa_trace_result result;
    if (!geometry->kernel.ops->trace(geometry->kernel.state, scratch->kernel, &local, &result, error)) return false;
    qa_collision_adapt_trace(&result, &local.policy);
    *out = result;
    return true;
}

bool qa_collision_point_contents(const qa_collision_geometry *geometry, qa_trace_scratch *scratch, const qa_point_query *query, qa_point_contents *out, qa_error *error)
{
    if (geometry == NULL || query == NULL || out == NULL || !qa_vec_finite(query->point)
        || !valid_policy(&query->policy) || !valid_target(geometry, &query->target))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid geometry point-contents query");
    qa_point_contents result;
    if (!geometry->kernel.ops->point_contents(geometry->kernel.state, scratch->kernel, query, &result, error)) return false;
    qa_collision_adapt_point(&result, &query->policy);
    if (result.family == QA_COLLISION_Q2) result.contents = query->policy.q2_merged_contents ? result.merged : result.stored;
    *out = result;
    return true;
}

bool qa_collision_trace_q3_capsule(const qa_collision_geometry *geometry, qa_trace_scratch *scratch, const qa_trace_query *query,
                                   qa_bounds bounds, bool transformed,
                                   qa_trace_result *out, qa_error *error)
{
    if (geometry == NULL || query == NULL || out == NULL || query->policy.family != QA_COLLISION_Q3
        || !valid_policy(&query->policy))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid source Q3 capsule trace query");
    void *replacement = geometry->family == QA_COLLISION_Q3 && geometry->model_count > 255
                            ? geometry->kernel.state : NULL;
    qa_trace_query local = *query;
    local.target.pose_rules = QA_RULESET_Q3;
    return qa_q3_trace_capsule_source(&local, bounds, transformed, replacement, scratch->kernel, out, error);
}

bool qa_collision_trace_q3_model(const qa_collision_geometry *geometry, qa_trace_scratch *scratch, const qa_trace_query *query,
                                 uint32_t model, bool transformed, qa_trace_result *out, qa_error *error)
{
    if (geometry == NULL || query == NULL || out == NULL || model >= geometry->model_count
        || query->policy.family != QA_COLLISION_Q3 || !valid_policy(&query->policy)
        || !qa_vec_finite(query->start) || !qa_vec_finite(query->end)
        || (transformed && (!qa_vec_finite(query->target.origin) || !qa_vec_finite(query->target.angles)))
        || (unsigned)query->shape.kind > QA_SHAPE_CAPSULE
        || (query->shape.kind != QA_SHAPE_POINT && !qa_bounds_valid(query->shape.bounds)))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid source Q3 model trace query");
    qa_trace_query local = *query;
    local.target.inline_model = true;
    local.target.model = model;
    local.target.pose_rules = QA_RULESET_Q3;
    if (!transformed) local.target.origin = local.target.angles = qa_v3(0, 0, 0);
    if (geometry->family != QA_COLLISION_Q3) return qa_collision_trace(geometry, scratch, &local, out, error);
    qa_trace_result result;
    if (!qa_q3_trace_model_source(geometry->kernel.state, scratch->kernel, &local, model, transformed, &result, error)) return false;
    qa_collision_adapt_trace(&result, &local.policy);
    *out = result;
    return true;
}

bool qa_collision_trace_q3_box(const qa_trace_query *query, qa_bounds bounds, bool transformed,
                               qa_trace_result *out, qa_error *error)
{
    if (query == NULL || out == NULL || query->policy.family != QA_COLLISION_Q3 || !valid_policy(&query->policy))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid source Q3 box trace query");
    return qa_q3_trace_box_source(query, bounds, transformed, out, error);
}

bool qa_collision_leaf_at(const qa_collision_geometry *geometry, uint32_t index, qa_collision_leaf *out, qa_error *error)
{
    if (geometry == NULL || out == NULL || (size_t)index >= geometry->leaf_count)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid collision leaf index");
    *out = geometry->leaves[index];
    return true;
}

bool qa_collision_point_leaf(const qa_collision_geometry *geometry, qa_vec3 point, qa_leaf_query_rule rule, qa_collision_leaf *out, qa_error *error)
{
    if (geometry == NULL || out == NULL || !qa_vec_finite(point))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid point-leaf query");
    int32_t child = rule == QA_LEAF_Q1 ? (geometry->node_count == 0 ? -1 : 0) : geometry->root;
    while (child >= 0) {
        const qa_collision_node *node = &geometry->nodes[(size_t)child];
        const qa_collision_plane *plane = &geometry->planes[node->plane];
        float projection = rule == QA_LEAF_COLLISION && geometry->family == QA_COLLISION_Q3 && plane->type < 3
            ? qa_vec_component(point, (unsigned)plane->type) : qa_vec_dot(point, plane->normal);
        float distance = projection - plane->distance;
        child = node->children[rule == QA_LEAF_Q1 ? (distance > 0 ? 0 : 1) : (distance < 0 ? 1 : 0)];
    }
    *out = geometry->leaves[leaf_index(child)];
    return true;
}

static unsigned box_side(const qa_collision_geometry *geometry, qa_bounds bounds,
    const qa_collision_plane *plane, bool q1_touched)
{
    if (geometry->family == QA_COLLISION_Q1 && !q1_touched) {
        qa_vec3 center = qa_vec_scale(qa_vec_add(bounds.mins, bounds.maxs), 0.5f);
        qa_vec3 extents = qa_vec_scale(qa_vec_sub(bounds.maxs, bounds.mins), 0.5f);
        float distance = qa_vec_dot(center, plane->normal) - plane->distance;
        float radius = fabsf(plane->normal.x) * extents.x + fabsf(plane->normal.y) * extents.y + fabsf(plane->normal.z) * extents.z;
        return distance >= radius ? 1u : distance < -radius ? 2u : 3u;
    }
    if ((q1_touched || geometry->family == QA_COLLISION_Q3) && plane->type < 3) {
        unsigned axis = (unsigned)plane->type;
        return plane->distance <= qa_vec_component(bounds.mins, axis) ? 1u
            : plane->distance >= qa_vec_component(bounds.maxs, axis) ? 2u : 3u;
    }
    qa_vec3 normal = plane->normal;
    qa_vec3 far_corner = qa_v3(normal.x < 0 ? bounds.mins.x : bounds.maxs.x,
                               normal.y < 0 ? bounds.mins.y : bounds.maxs.y,
                               normal.z < 0 ? bounds.mins.z : bounds.maxs.z);
    qa_vec3 near_corner = qa_v3(normal.x < 0 ? bounds.maxs.x : bounds.mins.x,
                                normal.y < 0 ? bounds.maxs.y : bounds.mins.y,
                                normal.z < 0 ? bounds.maxs.z : bounds.mins.z);
    return (qa_vec_dot(far_corner, normal) >= plane->distance ? 1u : 0u)
        | (qa_vec_dot(near_corner, normal) < plane->distance ? 2u : 0u);
}

bool qa_collision_walk_leaves(const qa_collision_geometry *geometry, qa_trace_scratch *scratch, qa_bounds bounds, bool q1_touched,
    qa_leaf_visit_fn visit, void *context, qa_leaf_list *out, qa_error *error)
{
    if (!q1_touched && geometry->family == QA_COLLISION_Q1)
        qa_stamp_set_begin(&scratch->leaves);
    qa_leaf_list result = {.topnode = -1};
    size_t count = 1;
    scratch->nodes[0] = geometry->root;
    while (count != 0) {
        int32_t child = scratch->nodes[--count];
        if (child < 0) {
            size_t leaf = leaf_index(child);
            if (q1_touched) {
                if (qa_collision_bits_overlap(geometry->leaves[leaf].contents, qa_collision_bit(QA_CONTENT_SOLID))) continue;
            } else if (geometry->family == QA_COLLISION_Q1) {
                if (leaf == 0 || !qa_stamp_set_mark(&scratch->leaves, leaf)) continue;
            }
            if (geometry->family == QA_COLLISION_Q3 && geometry->leaves[leaf].cluster != -1)
                result.last_leaf = (uint32_t)leaf;
            ++result.count;
            qa_leaf_visit next = visit(context, &geometry->leaves[leaf], error);
            if (next == QA_LEAF_FAILED) return false;
            if (next == QA_LEAF_STOP) break;
            continue;
        }
        const qa_collision_node *node = &geometry->nodes[(size_t)child];
        unsigned side = box_side(geometry, bounds, &geometry->planes[node->plane], q1_touched);
        if ((side == 3 || (geometry->family == QA_COLLISION_Q3 && side == 0)) && result.topnode == -1) result.topnode = child;
        if (geometry->family == QA_COLLISION_Q3) {
            if (side != 1) scratch->nodes[count++] = node->children[1];
            if (side != 2) scratch->nodes[count++] = node->children[0];
        } else {
            if ((side & 2u) != 0) scratch->nodes[count++] = node->children[1];
            if ((side & 1u) != 0) scratch->nodes[count++] = node->children[0];
        }
    }
    *out = result;
    return true;
}

typedef struct leaf_output { uint32_t *leaves; size_t capacity, count; } leaf_output;
static qa_leaf_visit output_leaf(void *context, const qa_collision_leaf *leaf, qa_error *error)
{
    leaf_output *out = context;
    (void)error;
    if (out->count < out->capacity) out->leaves[out->count] = leaf->leaf;
    ++out->count;
    return QA_LEAF_CONTINUE;
}

bool qa_collision_box_leaves(const qa_collision_geometry *geometry, qa_trace_scratch *scratch, qa_bounds bounds, uint32_t *leaves,
                             size_t capacity, qa_leaf_list *out, qa_error *error)
{
    if (!geometry || !out || (capacity && !leaves) || !qa_bounds_valid(bounds))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid box-leaf query");
    leaf_output output = {leaves, capacity, 0};
    qa_leaf_list value;
    if (!qa_collision_walk_leaves(geometry, scratch, bounds, false, output_leaf, &output, &value, error)) return false;
    value.count = output.count < capacity ? output.count : capacity;
    value.overflow = output.count > capacity;
    *out = value;
    return true;
}

static bool load_visibility_rows(qa_collision_geometry *geometry, qa_error *error)
{
    size_t slots = geometry->visibility_slots, width = geometry->visibility_bytes;
    if (!width || !slots) return true;
    if (geometry->family == QA_COLLISION_Q3 &&
        !geometry->bsp.lumps[QA_BSP_VISIBILITY].bytes.size) return true;
    geometry->phs_rows = geometry_array(slots, width, error);
    if (!geometry->phs_rows) return false;
    if (geometry->family != QA_COLLISION_Q3) {
        geometry->pvs_rows = geometry_array(slots, width, error);
        if (!geometry->pvs_rows) return false;
        for (size_t i = 0; i < slots; ++i) {
            size_t written;
            geometry->pvs[i] = geometry->pvs_rows + i * width;
            if (!qa_bsp_visibility(&geometry->bsp, (int32_t)i, false,
                geometry->cluster_count, geometry->pvs[i], width, &written, error)) return false;
        }
    }
    for (size_t i = 0; i < slots; ++i) {
        uint8_t *row = geometry->phs[i] = geometry->phs_rows + i * width;
        if (geometry->family == QA_COLLISION_Q2) {
            size_t written;
            if (!qa_bsp_visibility(&geometry->bsp, (int32_t)i, true,
                geometry->cluster_count, row, width, &written, error)) return false;
        } else {
            qa_bytes vis = geometry->bsp.lumps[QA_BSP_VISIBILITY].bytes;
            size_t stride = geometry->family == QA_COLLISION_Q3 ? qa_load_u32le(vis.data + 4) : width;
            const uint8_t *pvs = geometry->family == QA_COLLISION_Q3 ?
                vis.data + 8 + i * stride : geometry->pvs[i];
            memcpy(row, pvs, width);
            size_t neighbors = geometry->family == QA_COLLISION_Q1 ?
                geometry->leaf_count - 1 : geometry->cluster_count;
            for (size_t neighbor = 0; neighbor < neighbors; ++neighbor) {
                if (!bit_test((qa_bytes){pvs, width}, neighbor)) continue;
                size_t index = geometry->family == QA_COLLISION_Q1 ? neighbor + 1 : neighbor;
                const uint8_t *adjacent = geometry->family == QA_COLLISION_Q3 ?
                    vis.data + 8 + index * stride : geometry->pvs[index];
                for (size_t byte = 0; byte < width; ++byte) row[byte] |= adjacent[byte];
            }
        }
    }
    return true;
}

static bool visibility_row(const qa_collision_geometry *geometry, size_t index,
    bool phs, qa_bytes *out, qa_error *error)
{
    (void)error;
    if (!geometry->visibility_bytes) { *out = (qa_bytes){0}; return true; }
    if (geometry->family == QA_COLLISION_Q3 && !phs) {
        qa_bytes vis = geometry->bsp.lumps[QA_BSP_VISIBILITY].bytes;
        size_t width = qa_load_u32le(vis.data + 4);
        *out = (qa_bytes){vis.data + 8 + index * width, width};
    } else {
        uint8_t *const *rows = phs ? geometry->phs : geometry->pvs;
        *out = (qa_bytes){rows[index], geometry->visibility_bytes};
    }
    return true;
}

size_t qa_collision_q1_pvs_bytes(const qa_collision_geometry *geometry)
{ return geometry && geometry->family == QA_COLLISION_Q1 ? geometry->visibility_bytes : 0; }

bool qa_collision_q1_fat_pvs(const qa_collision_geometry *geometry, qa_trace_scratch *scratch, qa_vec3 eye,
    uint8_t *bytes, size_t capacity, qa_error *error)
{
    if (!geometry || geometry->family != QA_COLLISION_Q1 || !qa_vec_finite(eye) ||
        capacity < geometry->visibility_bytes || (geometry->visibility_bytes && !bytes))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Q1 fat-PVS requires its actual geometry and complete row storage");
    if (geometry->visibility_bytes) memset(bytes, 0, geometry->visibility_bytes);
    size_t count = 1; scratch->nodes[0] = geometry->root;
    while (count) {
        int32_t child = scratch->nodes[--count];
        if (child < 0) {
            size_t leaf = leaf_index(child);
            if (qa_collision_bits_overlap(geometry->leaves[leaf].contents, qa_collision_bit(QA_CONTENT_SOLID))) continue;
            qa_bytes row;
            if (!visibility_row(geometry, leaf, false, &row, error)) return false;
            for (size_t i = 0; i < geometry->visibility_bytes; ++i) bytes[i] |= row.data[i];
            continue;
        }
        const qa_collision_node *node = &geometry->nodes[(size_t)child];
        const qa_collision_plane *plane = &geometry->planes[node->plane];
        float distance = qa_vec_dot(eye, plane->normal) - plane->distance;
        if (distance > 8) scratch->nodes[count++] = node->children[0];
        else if (distance < -8) scratch->nodes[count++] = node->children[1];
        else {
            scratch->nodes[count++] = node->children[1];
            scratch->nodes[count++] = node->children[0];
        }
    }
    return true;
}


typedef enum visibility_answer { VISIBILITY_NONE, VISIBILITY_ALL, VISIBILITY_ROW } visibility_answer;
static bool visibility_target_row(const qa_collision_geometry *geometry,int32_t from,int32_t to,
    bool phs,qa_bytes *row,visibility_answer *answer,qa_error *error)
{
    *answer=VISIBILITY_NONE;
    if(to<0) return true;
    qa_bytes vis=geometry->bsp.lumps[QA_BSP_VISIBILITY].bytes;
    size_t index;
    if(geometry->family==QA_COLLISION_Q1) {
        if(from<0) { *answer=VISIBILITY_ALL; return true; }
        index=(size_t)(uint32_t)from+1u;
        if(index>=geometry->leaf_count) return geometry_fail(error,QA_ERROR_ARGUMENT,"Invalid Q1 visibility leaf");
    } else if(geometry->family==QA_COLLISION_Q2) {
        if(vis.size==0) { *answer=VISIBILITY_ALL; return true; }
        if(from<0 || (uint32_t)from>=geometry->cluster_count || (uint32_t)to>=geometry->cluster_count) return true;
        index=(size_t)(uint32_t)from;
    } else {
        if((uint32_t)to>=geometry->cluster_count) return true;
        if(vis.size==0) { *answer=VISIBILITY_ALL; return true; }
        index=from<0 || (uint32_t)from>=geometry->cluster_count?0:(size_t)(uint32_t)from;
    }
    if(!visibility_row(geometry,index,phs,row,error)) return false;
    *answer=VISIBILITY_ROW;return true;
}
bool qa_collision_cluster_visible(const qa_collision_geometry *geometry,int32_t from,int32_t to,
    bool phs,bool *out,qa_error *error)
{
    if(!geometry || !out) return geometry_fail(error,QA_ERROR_ARGUMENT,"Invalid visibility query");
    qa_bytes row={0};visibility_answer answer;
    if(!visibility_target_row(geometry,from,to,phs,&row,&answer,error)) return false;
    *out=answer==VISIBILITY_ALL || (answer==VISIBILITY_ROW && bit_test(row,(size_t)(uint32_t)to));
    return true;
}
bool qa_collision_clusters_visible(const qa_collision_geometry *geometry,qa_trace_scratch *scratch,
    const int32_t *from,size_t from_count,qa_bytes targets,bool phs,bool *out,qa_error *error)
{
    if(!geometry || !out) return geometry_fail(error,QA_ERROR_ARGUMENT,"Invalid visibility query");
    *out=false;
    size_t count=targets.size<scratch->visibility_capacity?targets.size:scratch->visibility_capacity;
    uint8_t *pending=scratch->visibility_pending;
    if(count) memcpy(pending,targets.data,count);
    if(count && geometry->family!=QA_COLLISION_Q1 && count==bit_bytes(geometry->cluster_count) && geometry->cluster_count%8u)
        pending[count-1]&=(uint8_t)((1u<<(geometry->cluster_count%8u))-1u);
    for(size_t source=0;source<from_count;++source) {
        size_t first=0;
        while(first<count && !pending[first]) ++first;
        if(first==count) break;
        unsigned low=0;
        while(!(pending[first]&(1u<<low))) ++low;
        int32_t first_target=(int32_t)(first*8u+low);
        qa_bytes row={0};visibility_answer answer;
        if(!visibility_target_row(geometry,from[source],first_target,phs,&row,&answer,error)) return false;
        if(answer==VISIBILITY_ALL) { *out=true;break; }
        if(answer==VISIBILITY_NONE) continue;
        size_t bytes=row.size<count?row.size:count;
        for(size_t i=first;i<bytes;++i) {
            uint8_t hit=pending[i]&row.data[i];
            if(hit) { *out=true;pending[i]&=(uint8_t)~hit; }
        }
    }
    return true;
}

uint32_t qa_collision_cluster_count(const qa_collision_geometry *geometry)
{ return geometry?geometry->cluster_count:0; }

uint32_t qa_collision_area_count(const qa_collision_geometry *geometry)
{
    return geometry == NULL ? 0 : geometry->area_count;
}

void qa_collision_no_areas(qa_collision_geometry *geometry, bool enabled)
{
    if (geometry != NULL) geometry->no_areas = enabled;
}

bool qa_collision_areas_connected(const qa_collision_geometry *geometry, int32_t first, int32_t second,
                                  bool *out, qa_error *error)
{
    if (geometry == NULL || out == NULL) return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid area connectivity query");
    if (geometry->family == QA_COLLISION_Q1) { *out = first == 0 && second == 0; return true; }
    if (geometry->family == QA_COLLISION_Q3) {
        if (geometry->no_areas) { *out = true; return true; }
        if (first < 0 || second < 0) { *out = false; return true; }
    }
    if (first < 0 || second < 0 || (uint32_t)first >= geometry->area_count || (uint32_t)second >= geometry->area_count)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Area connectivity index is outside the map");
    *out = geometry->flood[(uint32_t)first] == geometry->flood[(uint32_t)second];
    return true;
}

bool qa_collision_area_bits(const qa_collision_geometry *geometry, int32_t area, uint8_t *output,
                            size_t capacity, size_t *written, qa_error *error)
{
    if (geometry == NULL || written == NULL || (capacity != 0 && output == NULL))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid area-bit output");
    size_t bytes = bit_bytes(geometry->area_count);
    *written = bytes;
    if (capacity < bytes) return geometry_fail(error, QA_ERROR_ARGUMENT, "Area-bit output is too small");
    if (geometry->family == QA_COLLISION_Q1) { output[0] = 1; return true; }
    bool all = geometry->family == QA_COLLISION_Q3 && (geometry->no_areas || area == -1);
    if (!all && (area < 0 || (uint32_t)area >= geometry->area_count))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Area-bit index is outside the map");
    if (bytes == 0) return true;
    memset(output, all ? 255 : 0, bytes);
    if (all) return true;
    uint32_t flood = geometry->flood[(uint32_t)area];
    for (uint32_t other = 0; other < geometry->area_count; ++other) {
        if ((geometry->family == QA_COLLISION_Q2 && area == 0) || geometry->flood[other] == flood)
            output[other / 8u] |= (uint8_t)(1u << (other % 8u));
    }
    return true;
}

static bool valid_portal(const qa_collision_geometry *geometry, uint32_t portal, qa_error *error)
{
    if (geometry == NULL || geometry->family != QA_COLLISION_Q2)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Numbered area portals require Q2 geometry");
    if ((size_t)portal >= geometry->area_portal_count || !geometry->portals[portal].known)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Unknown Q2 area portal");
    return true;
}

bool qa_collision_set_portal(qa_collision_geometry *geometry, uint32_t portal, bool open, qa_error *error)
{
    if (!valid_portal(geometry, portal, error)) return false;
    geometry_portal *state = &geometry->portals[portal];
    bool before = portal_open(state);
    state->primary = open;
    if (before != portal_open(state)) flood_areas(geometry);
    return true;
}

bool qa_collision_adjust_portal(qa_collision_geometry *geometry, uint32_t portal, int delta, qa_error *error)
{
    if (!valid_portal(geometry, portal, error)) return false;
    geometry_portal *state = &geometry->portals[portal];
    int64_t count = (int64_t)state->contributions + (int64_t)delta;
    if (count < 0 || (uint64_t)count > UINT32_MAX)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Q2 portal contribution count overflow or underflow");
    bool before = portal_open(state);
    state->contributions = (uint32_t)count;
    if (before != portal_open(state)) flood_areas(geometry);
    return true;
}

bool qa_collision_portal_state(const qa_collision_geometry *geometry, uint32_t portal, bool *primary,
                               uint32_t *contributions, qa_error *error)
{
    if (primary == NULL || contributions == NULL)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Missing Q2 portal state outputs");
    if (!valid_portal(geometry, portal, error)) return false;
    *primary = geometry->portals[portal].primary;
    *contributions = geometry->portals[portal].contributions;
    return true;
}

bool qa_collision_adjust_area_pair(qa_collision_geometry *geometry, int32_t first, int32_t second, bool open, qa_error *error)
{
    if (geometry == NULL || geometry->family != QA_COLLISION_Q3)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 area-pair portal");
    if (first < 0 || second < 0) return true;
    if ((uint32_t)first >= geometry->area_count || (uint32_t)second >= geometry->area_count)
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Q3 area-pair index is outside the map");
    size_t index = (size_t)(uint32_t)first * geometry->area_count + (uint32_t)second;
    size_t reverse = (size_t)(uint32_t)second * geometry->area_count + (uint32_t)first;
    uint32_t amount = first == second ? 2u : 1u;
    uint32_t count = geometry->area_pairs[index];
    if ((open && count > (uint32_t)INT32_MAX - amount) || (!open && count < amount))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Q3 area-pair reference count overflow or underflow");
    uint32_t next = open ? count + amount : count - amount;
    geometry->area_pairs[index] = next;
    geometry->area_pairs[reverse] = next;
    if ((count == 0) != (next == 0)) flood_areas(geometry);
    return true;
}

void qa_collision_portal_checkpoint_free(qa_collision_portal_checkpoint *checkpoint)
{
    if (checkpoint == NULL) return;
    free(checkpoint->portals);
    free(checkpoint->area_pairs);
    *checkpoint = (qa_collision_portal_checkpoint){0};
}

bool qa_collision_capture_portals(const qa_collision_geometry *geometry, qa_collision_portal_checkpoint *out, qa_error *error)
{
    if (geometry == NULL || out == NULL) return geometry_fail(error, QA_ERROR_ARGUMENT, "Missing portal checkpoint output");
    qa_collision_portal_checkpoint checkpoint = {.family = geometry->family, .format = geometry->bsp.format,
        .map_identity = geometry->map_identity, .area_count = geometry->area_count, .no_areas = geometry->no_areas,
        .portal_count = geometry->known_portal_count, .area_pair_count = geometry->area_pair_count};
    checkpoint.portals = geometry_array(checkpoint.portal_count, sizeof(*checkpoint.portals), error);
    checkpoint.area_pairs = geometry_array(checkpoint.area_pair_count, sizeof(*checkpoint.area_pairs), error);
    if ((checkpoint.portal_count != 0 && checkpoint.portals == NULL)
        || (checkpoint.area_pair_count != 0 && checkpoint.area_pairs == NULL)) {
        qa_collision_portal_checkpoint_free(&checkpoint);
        return false;
    }
    size_t index = 0;
    for (size_t portal = 0; portal < geometry->area_portal_count; ++portal) {
        const geometry_portal *state = &geometry->portals[portal];
        if (state->known) checkpoint.portals[index++] = (qa_collision_saved_portal){(uint32_t)portal, state->contributions, state->primary};
    }
    if (checkpoint.area_pair_count != 0)
        memcpy(checkpoint.area_pairs, geometry->area_pairs, checkpoint.area_pair_count * sizeof(*checkpoint.area_pairs));
    *out = checkpoint;
    return true;
}

bool qa_collision_restore_portals(qa_collision_geometry *geometry, const qa_collision_portal_checkpoint *checkpoint, qa_error *error)
{
    if (geometry == NULL || checkpoint == NULL || checkpoint->family != geometry->family
        || checkpoint->format != geometry->bsp.format || checkpoint->map_identity != geometry->map_identity
        || checkpoint->area_count != geometry->area_count || checkpoint->portal_count != geometry->known_portal_count
        || checkpoint->area_pair_count != geometry->area_pair_count
        || (checkpoint->portal_count != 0 && checkpoint->portals == NULL)
        || (checkpoint->area_pair_count != 0 && checkpoint->area_pairs == NULL))
        return geometry_fail(error, QA_ERROR_ARGUMENT, "Portal checkpoint belongs to different collision geometry");
    size_t index = 0;
    for (size_t portal = 0; portal < geometry->area_portal_count; ++portal) {
        if (!geometry->portals[portal].known) continue;
        if (checkpoint->portals[index++].portal != portal)
            return geometry_fail(error, QA_ERROR_FORMAT, "Portal checkpoint IDs do not match the map");
    }
    if (geometry->family == QA_COLLISION_Q3) {
        for (uint32_t first = 0; first < geometry->area_count; ++first) {
            for (uint32_t second = first; second < geometry->area_count; ++second) {
                uint32_t count = checkpoint->area_pairs[(size_t)first * geometry->area_count + second];
                if (count > (uint32_t)INT32_MAX || (first == second && (count & 1u) != 0)
                    || count != checkpoint->area_pairs[(size_t)second * geometry->area_count + first])
                    return geometry_fail(error, QA_ERROR_FORMAT, "Invalid symmetric Q3 area-pair checkpoint");
            }
        }
    }
    for (size_t i = 0; i < checkpoint->portal_count; ++i) {
        qa_collision_saved_portal saved = checkpoint->portals[i];
        geometry->portals[saved.portal].primary = saved.primary;
        geometry->portals[saved.portal].contributions = saved.contributions;
    }
    if (checkpoint->area_pair_count != 0)
        memcpy(geometry->area_pairs, checkpoint->area_pairs, checkpoint->area_pair_count * sizeof(*checkpoint->area_pairs));
    geometry->no_areas = checkpoint->no_areas;
    flood_areas(geometry);
    return true;
}
