#include "internal.h"
#include "qa/collision_bits.h"
#include "qa/stamp.h"

#include <limits.h>
#include <stdlib.h>

#define Q2_POSITION_LEAF_LIMIT 1024u

qa_collision_bits qa_collision_q2_source_contents(uint32_t solid, uint32_t svflags, bool rerelease)
{
    if (solid == 3) return qa_collision_bit(QA_CONTENT_SOLID);
    if (!solid || (solid == 1 && !rerelease)) return (qa_collision_bits){0};
    if (svflags & 2u) return qa_collision_bit(QA_CONTENT_CORPSE);
    if (rerelease && (svflags & 8u)) return qa_collision_bit(QA_CONTENT_PLAYER);
    if (rerelease && (svflags & 128u)) return qa_collision_bit(QA_CONTENT_PROJECTILE);
    return qa_collision_bit(QA_CONTENT_MONSTER);
}

typedef struct q2_leaf {
    qa_collision_bits stored, merged;
    qa_bsp_range brushes;
} q2_leaf;

typedef struct q2_brush {
    qa_bsp_range sides;
    qa_collision_bits contents;
} q2_brush;

typedef struct q2_side {
    uint32_t plane;
    int64_t texture;
} q2_side;

typedef struct q2_interval {
    float first, last;
    bool solid;
} q2_interval;

typedef struct q2_plane_support {
    float distance, extent;
    float first, last;
    bool has_last;
} q2_plane_support;

typedef struct q2_collision {
    const qa_collision_plane *planes;
    const qa_collision_node *nodes;
    q2_leaf *leaves;
    q2_brush *brushes;
    q2_side *sides;
    qa_collision_surface *surfaces;
    uint32_t *leaf_brushes;
    int32_t *headnodes;
    size_t plane_count, node_count, leaf_count, brush_count;
    size_t side_count, surface_count, leaf_brush_count, model_count;
} q2_collision;

typedef struct q2_scratch {
    int32_t *node_stack;
    qa_collision_trace_frame *trace_stack;
    q2_plane_support *expanded_planes;
    uint32_t *trace_storage, *expanded_storage;
    qa_stamp_set trace_marks, expanded_marks;
    qa_shape_kind expanded_kind;
    qa_bounds expanded_bounds;
    q2_interval *intervals, *solid_intervals;
} q2_scratch;

typedef struct q2_work {
    const q2_collision *collision;
    q2_scratch *scratch;
    const qa_trace_query *query;
    qa_vec3 start, end, extents;
    qa_bounds bounds;
    qa_collision_trace_rules rules;
    bool stationary, merged;
    qa_collision_bits mask;
    qa_trace_result result;
} q2_work;

static void q2_destroy(void *opaque)
{
    q2_collision *collision = opaque;
    if (collision == NULL) return;
    free(collision->leaves);
    free(collision->brushes);
    free(collision->sides);
    free(collision->surfaces);
    free(collision->leaf_brushes);
    free(collision->headnodes);
    free(collision);
}

static void q2_destroy_scratch(void *opaque)
{
    q2_scratch *scratch = opaque;
    if (scratch == NULL) return;
    free(scratch->node_stack);
    free(scratch->trace_stack);
    free(scratch->expanded_planes);
    free(scratch->trace_storage);
    free(scratch->expanded_storage);
    free(scratch->intervals);
    free(scratch->solid_intervals);
    free(scratch);
}

static void *q2_array(size_t count, size_t width, qa_error *error)
{
    if (count == 0) return NULL;
    if (count > (size_t)PTRDIFF_MAX / width) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Q2 collision table is too large");
        return NULL;
    }
    void *array = calloc(count, width);
    if (array == NULL)
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Unable to allocate Q2 collision table");
    return array;
}

static void *q2_create_scratch(const void *opaque, qa_error *error)
{
    const q2_collision *collision = opaque;
    q2_scratch *scratch = calloc(1, sizeof(*scratch));
    if (scratch == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Unable to allocate Q2 collision scratch");
        return NULL;
    }
#define Q2_SCRATCH_ALLOC(member, count) do { \
    scratch->member = q2_array((count), sizeof(*scratch->member), error); \
    if ((count) != 0 && scratch->member == NULL) goto fail; \
} while (0)
    Q2_SCRATCH_ALLOC(node_stack, collision->node_count + 1);
    Q2_SCRATCH_ALLOC(trace_stack, collision->node_count + 1);
    Q2_SCRATCH_ALLOC(expanded_planes, collision->plane_count);
    Q2_SCRATCH_ALLOC(trace_storage, collision->brush_count + collision->plane_count);
    Q2_SCRATCH_ALLOC(expanded_storage, collision->plane_count);
    Q2_SCRATCH_ALLOC(intervals, collision->brush_count);
    Q2_SCRATCH_ALLOC(solid_intervals, collision->brush_count);
#undef Q2_SCRATCH_ALLOC
    qa_stamp_set_init(&scratch->trace_marks, scratch->trace_storage,
        collision->brush_count + collision->plane_count);
    qa_stamp_set_init(&scratch->expanded_marks, scratch->expanded_storage,
        collision->plane_count);
    return scratch;
fail:
    q2_destroy_scratch(scratch);
    return NULL;
}

static bool q2_bad_reference(qa_error *error, const char *label, size_t index)
{
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Q2 collision %s at record %zu", label, index);
    return false;
}

static bool q2_range_valid(qa_bsp_range range, size_t count)
{
    return (size_t)range.first <= count && (size_t)range.count <= count - (size_t)range.first;
}

static size_t q2_leaf_index(int32_t child)
{
    return (size_t)(-(int64_t)child - 1);
}

static bool q2_child_valid(const q2_collision *collision, int32_t child)
{
    return child < 0 ? q2_leaf_index(child) < collision->leaf_count
                     : (size_t)child < collision->node_count;
}

static bool q2_validate_trees(const q2_collision *collision, qa_error *error)
{
    if (collision->node_count == 0) return true;
    uint8_t *colors = q2_array(collision->node_count, sizeof(*colors), error);
    if (colors == NULL) return false;
    int32_t *stack = q2_array(collision->node_count, sizeof(*stack), error);
    if (stack == NULL) { free(colors); return false; }
    for (size_t model = 0; model < collision->model_count; ++model) {
        int32_t root = collision->headnodes[model];
        if (root < 0 || colors[(size_t)root] == 2) continue;
        size_t depth = 1;
        stack[0] = root;
        while (depth != 0) {
            size_t index = (size_t)stack[depth - 1];
            colors[index] = 1;
            const qa_collision_node *node = &collision->nodes[index];
            bool descended = false;
            for (unsigned side = 0; side < 2; ++side) {
                int32_t child = node->children[side];
                if (child < 0) continue;
                uint8_t color = colors[(size_t)child];
                if (color == 1) {
                    free(colors);
                    free(stack);
                    return q2_bad_reference(error, "node cycle", (size_t)child);
                }
                if (color == 0) {
                    stack[depth++] = child;
                    descended = true;
                    break;
                }
            }
            if (!descended) {
                colors[index] = 2;
                --depth;
            }
        }
    }
    free(colors);
    free(stack);
    return true;
}

static qa_bounds q2_shape_bounds(const qa_trace_shape *shape)
{
    return shape->kind == QA_SHAPE_POINT
        ? (qa_bounds){qa_v3(0, 0, 0), qa_v3(0, 0, 0)} : shape->bounds;
}

static float q2_expand(const qa_collision_plane *plane, const qa_trace_shape *shape)
{
    qa_bounds bounds = q2_shape_bounds(shape);
    qa_vec3 normal = plane->normal;
    if (shape->kind == QA_SHAPE_CAPSULE) {
        qa_vec3 center = qa_vec_scale(qa_vec_add(bounds.mins, bounds.maxs), 0.5f);
        float halfheight = (bounds.maxs.z - bounds.mins.z) * 0.5f;
        float radius = fminf((bounds.maxs.x - bounds.mins.x) * 0.5f, halfheight);
        return radius + fabsf(normal.z) * (halfheight - radius) - qa_vec_dot(center, normal);
    }
    qa_vec3 corner = qa_v3(normal.x < 0 ? bounds.maxs.x : bounds.mins.x,
                           normal.y < 0 ? bounds.maxs.y : bounds.mins.y,
                           normal.z < 0 ? bounds.maxs.z : bounds.mins.z);
    return -qa_vec_dot(corner, normal);
}

static q2_plane_support *q2_expanded_plane(q2_work *work, uint32_t index)
{
    const q2_collision *collision = work->collision;
    q2_plane_support *expanded = &work->scratch->expanded_planes[index];
    if (qa_stamp_set_mark(&work->scratch->expanded_marks, index)) {
        const qa_collision_plane *plane = &collision->planes[index];
        expanded->distance =
            plane->distance + q2_expand(plane, &work->query->shape);
        expanded->extent = plane->type >= 0 && plane->type < 3
            ? qa_vec_component(work->extents, (unsigned)plane->type)
            : fabsf(work->extents.x * plane->normal.x)
                + fabsf(work->extents.y * plane->normal.y)
                + fabsf(work->extents.z * plane->normal.z);
    }
    return expanded;
}

static const q2_plane_support *q2_endpoint_distances(q2_work *work, uint32_t index,
                                                   bool need_last)
{
    const q2_collision *collision = work->collision;
    q2_plane_support *expanded = q2_expanded_plane(work, index);
    const qa_collision_plane *plane = &collision->planes[index];
    if (qa_stamp_set_mark(&work->scratch->trace_marks, collision->brush_count + index)) {
        expanded->first = qa_vec_dot(work->start, plane->normal) - expanded->distance;
        expanded->has_last = false;
    }
    if (need_last && !expanded->has_last) {
        expanded->last = qa_vec_dot(work->end, plane->normal) - expanded->distance;
        expanded->has_last = true;
    }
    return expanded;
}

static uint32_t q2_target(const qa_collision_target *target, qa_vec3 basis[3])
{
    if (target->inline_model)
        qa_collision_pose_basis(target, true, basis);
    return target->inline_model ? target->model : 0;
}

static qa_vec3 q2_local(qa_vec3 point, const qa_collision_target *target, const qa_vec3 basis[3])
{
    return target->inline_model
        ? qa_collision_to_local(qa_vec_sub(point, target->origin), basis) : point;
}

static bool q2_point_contents(const void *opaque, void *opaque_scratch, const qa_point_query *query,
                              qa_point_contents *out, qa_error *error)
{
    const q2_collision *collision = opaque;
    qa_vec3 basis[3];
    uint32_t model = q2_target(&query->target, basis);
    (void)error;
    (void)opaque_scratch;
    qa_vec3 point = q2_local(query->point, &query->target, basis);
    int32_t child = collision->headnodes[model];
    while (child >= 0) {
        const qa_collision_node *node = &collision->nodes[(size_t)child];
        float distance = qa_collision_plane_distance(point, &collision->planes[node->plane]);
        child = node->children[distance < 0 ? 1 : 0];
    }
    const q2_leaf *leaf = &collision->leaves[q2_leaf_index(child)];
    *out = (qa_point_contents){.family = QA_COLLISION_Q2,
        .contents = query->policy.family != QA_COLLISION_Q2 || query->policy.q2_merged_contents ? leaf->merged : leaf->stored,
        .stored = leaf->stored, .merged = leaf->merged};
    return true;
}

static qa_collision_side_distances q2_brush_distances(void *context, size_t index, bool need_last)
{
    q2_work *work = context;
    const q2_side *side = &work->collision->sides[index];
    const q2_plane_support *sample = q2_endpoint_distances(work, side->plane, need_last);
    return (qa_collision_side_distances){sample->first, need_last ? sample->last : 0};
}

static void q2_trace_brush(q2_work *work, uint32_t index)
{
    const q2_collision *collision = work->collision;
    if (!qa_stamp_set_mark(&work->scratch->trace_marks, index)) return;
    const q2_brush *brush = &collision->brushes[index];
    if (!qa_collision_bits_overlap(brush->contents, work->mask) || brush->sides.count == 0) return;
    qa_collision_brush_contact contact;
    if (!qa_collision_trace_brush(work, q2_brush_distances,
            brush->sides.first, brush->sides.count, work->stationary,
            &work->rules.brush, brush->contents, &work->result, &contact)) return;
    const q2_side *lead = &collision->sides[contact.side];
    work->result.plane = collision->planes[lead->plane];
    work->result.has_surface = lead->texture >= 0;
    work->result.surface = lead->texture >= 0 ? collision->surfaces[(size_t)lead->texture]
                                             : (qa_collision_surface){0};
    work->result.surface_flags = work->result.surface.flags;
    /* q2repro pairs the secondary plane with the primary surface. */
    if (contact.secondary != SIZE_MAX) {
        work->result.has_secondary = true;
        work->result.secondary_plane = collision->planes[collision->sides[contact.secondary].plane];
        work->result.secondary_has_surface = work->result.has_surface;
        work->result.secondary_surface = work->result.surface;
    }
}

static void q2_trace_leaf(q2_work *work, size_t index)
{
    const q2_leaf *leaf = &work->collision->leaves[index];
    qa_collision_bits contents = work->merged ? leaf->merged : leaf->stored;
    if (!qa_collision_bits_overlap(contents, work->mask)) return;
    for (size_t i = 0; i < (size_t)leaf->brushes.count; ++i) {
        q2_trace_brush(work, work->collision->leaf_brushes[(size_t)leaf->brushes.first + i]);
        if (work->result.fraction == 0) return;
    }
}

/* Push back first so the front child is visited first, including exact ties. */
static void q2_box_children(const q2_collision *collision, q2_scratch *scratch, const qa_collision_node *node,
                            qa_bounds bounds, size_t *depth)
{
    const qa_collision_plane *plane = &collision->planes[node->plane];
    qa_vec3 normal = plane->normal;
    qa_vec3 far_corner = qa_v3(normal.x < 0 ? bounds.mins.x : bounds.maxs.x,
                               normal.y < 0 ? bounds.mins.y : bounds.maxs.y,
                               normal.z < 0 ? bounds.mins.z : bounds.maxs.z);
    qa_vec3 near_corner = qa_v3(normal.x < 0 ? bounds.maxs.x : bounds.mins.x,
                                normal.y < 0 ? bounds.maxs.y : bounds.mins.y,
                                normal.z < 0 ? bounds.maxs.z : bounds.mins.z);
    if (qa_vec_dot(near_corner, normal) < plane->distance)
        scratch->node_stack[(*depth)++] = node->children[1];
    if (qa_vec_dot(far_corner, normal) >= plane->distance)
        scratch->node_stack[(*depth)++] = node->children[0];
}

static qa_bounds q2_envelope(qa_vec3 start, qa_vec3 end, qa_bounds bounds)
{
    return (qa_bounds){
        qa_v3(fminf(start.x, end.x) + bounds.mins.x - 1,
              fminf(start.y, end.y) + bounds.mins.y - 1,
              fminf(start.z, end.z) + bounds.mins.z - 1),
        qa_v3(fmaxf(start.x, end.x) + bounds.maxs.x + 1,
              fmaxf(start.y, end.y) + bounds.maxs.y + 1,
              fmaxf(start.z, end.z) + bounds.maxs.z + 1)};
}

static void q2_position_test(q2_work *work, int32_t headnode)
{
    const q2_collision *collision = work->collision;
    qa_bounds bounds = q2_envelope(work->start, work->start, work->bounds);
    size_t depth = 1, leaves = 0;
    work->scratch->node_stack[0] = headnode;
    while (depth != 0 && leaves < Q2_POSITION_LEAF_LIMIT) {
        int32_t child = work->scratch->node_stack[--depth];
        if (child < 0) {
            ++leaves;
            q2_trace_leaf(work, q2_leaf_index(child));
            if (work->result.all_solid) return;
        } else {
            q2_box_children(collision, work->scratch, &collision->nodes[(size_t)child], bounds, &depth);
        }
    }
}

static float q2_tree_extent(void *context, uint32_t plane)
{
    q2_work *work = context;
    if (work->rules.conservative_extent && work->query->shape.kind != QA_SHAPE_POINT
        && work->collision->planes[plane].type >= 3) return 2048.0f;
    return q2_expanded_plane(work, plane)->extent;
}

static void q2_tree_leaf(void *context, uint32_t leaf)
{
    q2_trace_leaf(context, leaf);
}

static void q2_sweep(q2_work *work, int32_t headnode)
{
    const q2_collision *collision = work->collision;
    const qa_collision_tree_trace trace = {
        collision->planes, collision->nodes, work->scratch->trace_stack,
        work->start, work->end, work->rules.tree,
        &work->result, work, q2_tree_extent, q2_tree_leaf};
    qa_collision_trace_tree(&trace, headnode);
}

static void q2_brush_medium(q2_work *work, uint32_t index, size_t *count)
{
    const q2_collision *collision = work->collision;
    if (!qa_stamp_set_mark(&work->scratch->trace_marks, index)) return;
    const q2_brush *brush = &collision->brushes[index];
    int32_t medium = qa_collision_q1_medium_class(brush->contents);
    if (medium == -1 || brush->sides.count == 0) return;
    float begin = 0, end = work->result.fraction;
    for (size_t i = 0; i < (size_t)brush->sides.count; ++i) {
        const q2_side *side = &collision->sides[(size_t)brush->sides.first + i];
        const q2_plane_support *distances = q2_endpoint_distances(work, side->plane, true);
        float first = distances->first;
        float last = distances->last;
        if (first > 0 && last > 0) return;
        if (first <= 0 && last <= 0) continue;
        float crossing = first / (first - last);
        if (first > last) begin = fmaxf(begin, crossing);
        else end = fminf(end, crossing);
        if (begin > end) return;
    }
    work->scratch->intervals[(*count)++] = (q2_interval){begin, end, medium == -2};
}

static void q2_interval_sift(q2_interval *intervals, size_t root, size_t count)
{
    while (root < count / 2) {
        size_t child = root * 2 + 1;
        if (child + 1 < count && intervals[child].first < intervals[child + 1].first) ++child;
        if (intervals[root].first >= intervals[child].first) return;
        q2_interval temporary = intervals[root];
        intervals[root] = intervals[child];
        intervals[child] = temporary;
        root = child;
    }
}

static void q2_interval_sort(q2_interval *intervals, size_t count)
{
    for (size_t root = count / 2; root != 0; --root)
        q2_interval_sift(intervals, root - 1, count);
    for (size_t end = count; end > 1; --end) {
        q2_interval temporary = intervals[0];
        intervals[0] = intervals[end - 1];
        intervals[end - 1] = temporary;
        q2_interval_sift(intervals, 0, end - 1);
    }
}

static bool q2_uncovered(float first, float last, const q2_interval *parts, size_t count)
{
    float position = first;
    for (size_t i = 0; i < count; ++i) {
        if (parts[i].last < position) continue;
        if (parts[i].first > position) return true;
        position = fmaxf(position, parts[i].last);
        if (position >= last) return false;
    }
    return first == last || position < last;
}

static void q2_trace_media(q2_work *work, int32_t headnode)
{
    const q2_collision *collision = work->collision;
    qa_vec3 reached = qa_vec_lerp(work->start, work->end, work->result.fraction);
    qa_bounds bounds = q2_envelope(work->start, reached, work->bounds);
    size_t depth = 1, leaves = 0, count = 0;
    work->scratch->node_stack[0] = headnode;
    qa_stamp_set_begin(&work->scratch->trace_marks);
    while (depth != 0 && leaves < collision->leaf_count) {
        int32_t child = work->scratch->node_stack[--depth];
        if (child < 0) {
            ++leaves;
            const q2_leaf *leaf = &collision->leaves[q2_leaf_index(child)];
            for (size_t i = 0; i < (size_t)leaf->brushes.count; ++i)
                q2_brush_medium(work, collision->leaf_brushes[(size_t)leaf->brushes.first + i], &count);
        } else {
            q2_box_children(collision, work->scratch, &collision->nodes[(size_t)child], bounds, &depth);
        }
    }
    q2_interval_sort(work->scratch->intervals, count);
    work->result.in_open = q2_uncovered(0, work->result.fraction, work->scratch->intervals, count);
    size_t solid_count = 0;
    for (size_t i = 0; i < count; ++i) {
        q2_interval interval = work->scratch->intervals[i];
        if (!interval.solid) continue;
        if (solid_count != 0 && interval.first <= work->scratch->solid_intervals[solid_count - 1].last)
            work->scratch->solid_intervals[solid_count - 1].last = fmaxf(work->scratch->solid_intervals[solid_count - 1].last, interval.last);
        else work->scratch->solid_intervals[solid_count++] = interval;
    }
    size_t solid = 0;
    for (size_t i = 0; i < count; ++i) {
        q2_interval liquid = work->scratch->intervals[i];
        if (liquid.solid) continue;
        while (solid < solid_count && work->scratch->solid_intervals[solid].last < liquid.first) ++solid;
        if (solid == solid_count || work->scratch->solid_intervals[solid].first > liquid.first
            || work->scratch->solid_intervals[solid].last < liquid.last) {
            work->result.in_water = true;
            return;
        }
    }
}

static bool q2_trace(const void *opaque, void *opaque_scratch, const qa_trace_query *query,
                     qa_trace_result *out, qa_error *error)
{
    const q2_collision *collision = opaque;
    q2_scratch *scratch = opaque_scratch;
    qa_vec3 basis[3];
    uint32_t model = q2_target(&query->target, basis);
    (void)error;
    q2_work work = {0};
    work.collision = collision;
    work.scratch = scratch;
    work.query = query;
    work.rules = qa_collision_rules(&query->policy);
    work.start = q2_local(query->start, &query->target, basis);
    work.end = q2_local(query->end, &query->target, basis);
    work.bounds = q2_shape_bounds(&query->shape);
    if (scratch->expanded_kind != query->shape.kind ||
        memcmp(&scratch->expanded_bounds.mins.x, &work.bounds.mins.x, sizeof(float)) != 0 ||
        memcmp(&scratch->expanded_bounds.mins.y, &work.bounds.mins.y, sizeof(float)) != 0 ||
        memcmp(&scratch->expanded_bounds.mins.z, &work.bounds.mins.z, sizeof(float)) != 0 ||
        memcmp(&scratch->expanded_bounds.maxs.x, &work.bounds.maxs.x, sizeof(float)) != 0 ||
        memcmp(&scratch->expanded_bounds.maxs.y, &work.bounds.maxs.y, sizeof(float)) != 0 ||
        memcmp(&scratch->expanded_bounds.maxs.z, &work.bounds.maxs.z, sizeof(float)) != 0) {
        qa_stamp_set_begin(&scratch->expanded_marks);
        scratch->expanded_kind = query->shape.kind;
        scratch->expanded_bounds = work.bounds;
    }
    work.stationary = work.start.x == work.end.x && work.start.y == work.end.y && work.start.z == work.end.z;
    work.merged = query->policy.family != QA_COLLISION_Q2 || query->policy.q2_merged_contents;
    work.mask = query->policy.contents_mask;
    work.extents = qa_v3(fmaxf(-work.bounds.mins.x, work.bounds.maxs.x),
                         fmaxf(-work.bounds.mins.y, work.bounds.maxs.y),
                         fmaxf(-work.bounds.mins.z, work.bounds.maxs.z));
    work.result = qa_collision_empty_trace(query, QA_COLLISION_Q2);
    int32_t headnode = collision->headnodes[model];
    qa_stamp_set_begin(&scratch->trace_marks);
    if (work.stationary) q2_position_test(&work, headnode);
    else q2_sweep(&work, headnode);
    if (query->target.inline_model && work.result.fraction != 1) {
        /* Restore only the primary normal using the linked entity role.
         * Plane distance/type/signbits and the secondary plane stay source-native. */
        work.result.plane.normal = qa_collision_pose_normal(work.result.plane.normal, &query->target, true, basis);
    }
    work.result.end = !query->target.inline_model && work.result.fraction == 1
        ? query->end : qa_vec_lerp(query->start, query->end, work.result.fraction);
    work.result.contact = work.result.fraction < 1 && !work.result.all_solid;
    work.result.contact_plane = work.result.plane;
    work.result.hit = work.result.fraction < 1 || work.result.start_solid ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_NONE;
    if (query->policy.family == QA_COLLISION_Q1) q2_trace_media(&work, headnode);
    *out = work.result;
    return true;
}

static const qa_collision_ops q2_ops = {.destroy = q2_destroy, .trace = q2_trace,
    .point_contents = q2_point_contents, .create_scratch = q2_create_scratch,
    .destroy_scratch = q2_destroy_scratch};

bool qa_q2_collision_set_material(void *opaque, uint32_t texinfo, qa_bytes bytes, qa_error *error)
{
    q2_collision *collision = opaque;
    if (collision == NULL || (size_t)texinfo >= collision->surface_count || (bytes.size != 0 && bytes.data == NULL)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 material sidecar target or bytes");
        return false;
    }
    char material[16] = {0};
    size_t length = bytes.size < sizeof(material) - 1 ? bytes.size : sizeof(material) - 1;
    for (size_t i = 0; i < length && bytes.data[i] != 0; ++i) {
        uint8_t value = bytes.data[i];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
              || (value >= '0' && value <= '9') || value == '_' || value == '-')) {
            memset(collision->surfaces[texinfo].material, 0, sizeof(material));
            qa_error_set(error, QA_ERROR_FORMAT, i, "Invalid Q2 material sidecar name");
            return false;
        }
        material[i] = (char)value;
    }
    memcpy(collision->surfaces[texinfo].material, material, sizeof(material));
    return true;
}

bool qa_q2_collision_create(const qa_bsp_view *map, const qa_collision_topology *topology, qa_collision_kernel *out, qa_error *error)
{
    if (map == NULL || out == NULL || map->family != QA_BSP_Q2) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 collision requires a Q2 BSP view");
        return false;
    }
    q2_collision *collision = calloc(1, sizeof(*collision));
    if (collision == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Unable to allocate Q2 collision state");
        return false;
    }
    collision->planes = topology->planes;
    collision->nodes = topology->nodes;
    collision->plane_count = topology->plane_count;
    collision->node_count = topology->node_count;
    collision->leaf_count = qa_bsp_record_count(map, QA_BSP_LEAVES);
    collision->brush_count = qa_bsp_record_count(map, QA_BSP_BRUSHES);
    collision->side_count = qa_bsp_record_count(map, QA_BSP_BRUSH_SIDES);
    collision->surface_count = qa_bsp_record_count(map, QA_BSP_TEXINFO);
    collision->leaf_brush_count = qa_bsp_record_count(map, QA_BSP_LEAF_BRUSHES);
    collision->model_count = qa_bsp_record_count(map, QA_BSP_MODELS);
    if (collision->plane_count == 0 || collision->node_count == 0 || collision->leaf_count == 0
        || collision->model_count == 0 || collision->node_count > (size_t)INT32_MAX + 1u
        || collision->leaf_count > (size_t)INT32_MAX + 1u) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 collision requires addressable planes, nodes, leaves and models");
        goto fail;
    }
#define Q2_ALLOC(member, count) do { \
    collision->member = q2_array((count), sizeof(*collision->member), error); \
    if ((count) != 0 && collision->member == NULL) goto fail; \
} while (0)
    Q2_ALLOC(leaves, collision->leaf_count);
    Q2_ALLOC(brushes, collision->brush_count);
    Q2_ALLOC(sides, collision->side_count);
    Q2_ALLOC(surfaces, collision->surface_count);
    Q2_ALLOC(leaf_brushes, collision->leaf_brush_count);
    Q2_ALLOC(headnodes, collision->model_count);
#undef Q2_ALLOC
    for (size_t i = 0; i < collision->node_count; ++i) {
        const qa_collision_node *node = &collision->nodes[i];
        if ((size_t)node->plane >= collision->plane_count
            || !q2_child_valid(collision, node->children[0]) || !q2_child_valid(collision, node->children[1])) {
            q2_bad_reference(error, "node", i);
            goto fail;
        }
    }
    for (size_t i = 0; i < collision->surface_count; ++i) {
        qa_bsp_texinfo texture;
        if (!qa_bsp_read_texinfo(map, i, &texture, error)) goto fail;
        qa_collision_surface *surface = &collision->surfaces[i];
        size_t length = texture.name.size;
        if (length >= sizeof(surface->name)) length = sizeof(surface->name) - 1;
        if (length != 0) memcpy(surface->name, texture.name.data, length);
        surface->name[length] = '\0';
        surface->flags = qa_collision_surface_decode(texture.flags, QA_COLLISION_Q2);
        surface->value = texture.value;
    }
    for (size_t i = 0; i < collision->side_count; ++i) {
        qa_bsp_brush_side side;
        if (!qa_bsp_read_brush_side(map, i, &side, error)) goto fail;
        if ((size_t)side.plane >= collision->plane_count || side.texinfo < -1
            || (side.texinfo >= 0 && (uint64_t)side.texinfo >= collision->surface_count)) {
            q2_bad_reference(error, "brush side", i);
            goto fail;
        }
        collision->sides[i] = (q2_side){side.plane, side.texinfo};
    }
    for (size_t i = 0; i < collision->brush_count; ++i) {
        qa_bsp_brush brush;
        if (!qa_bsp_read_brush(map, i, &brush, error)) goto fail;
        if (!q2_range_valid(brush.sides, collision->side_count)) {
            q2_bad_reference(error, "brush", i);
            goto fail;
        }
        collision->brushes[i] = (q2_brush){brush.sides,
            qa_collision_contents_decode(brush.contents, QA_COLLISION_Q2)};
    }
    for (size_t i = 0; i < collision->leaf_brush_count; ++i) {
        int64_t index;
        if (!qa_bsp_read_index(map, QA_BSP_LEAF_BRUSHES, i, &index, error)) goto fail;
        if (index < 0 || (uint64_t)index >= collision->brush_count || (uint64_t)index > UINT32_MAX) {
            q2_bad_reference(error, "leaf brush", i);
            goto fail;
        }
        collision->leaf_brushes[i] = (uint32_t)index;
    }
    for (size_t i = 0; i < collision->leaf_count; ++i) {
        qa_bsp_leaf leaf;
        if (!qa_bsp_read_leaf(map, i, &leaf, error)) goto fail;
        if (!q2_range_valid(leaf.brushes, collision->leaf_brush_count) || (i == 0 && leaf.contents != 1)) {
            q2_bad_reference(error, "leaf", i);
            goto fail;
        }
        qa_collision_bits stored = qa_collision_contents_decode(leaf.contents, QA_COLLISION_Q2);
        qa_collision_bits merged = stored;
        /* Q2 rerelease merges brush flags into every leaf except solid leaf 0. */
        if (i != 0) {
            for (size_t j = 0; j < (size_t)leaf.brushes.count; ++j) {
                uint32_t brush = collision->leaf_brushes[(size_t)leaf.brushes.first + j];
                merged = qa_collision_bits_union(merged, collision->brushes[brush].contents);
            }
        }
        collision->leaves[i] = (q2_leaf){stored, merged, leaf.brushes};
    }
    for (size_t i = 0; i < collision->model_count; ++i) {
        qa_bsp_model model;
        if (!qa_bsp_read_model(map, i, &model, error)) goto fail;
        if (!q2_child_valid(collision, model.headnodes[0])) {
            q2_bad_reference(error, "model headnode", i);
            goto fail;
        }
        collision->headnodes[i] = model.headnodes[0];
    }
    if (!q2_validate_trees(collision, error)) goto fail;
    *out = (qa_collision_kernel){collision, &q2_ops};
    return true;

fail:
    q2_destroy(collision);
    return false;
}
