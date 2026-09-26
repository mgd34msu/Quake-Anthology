/* SPDX-License-Identifier: GPL-2.0-or-later
 * Quake II brush collision, derived from id Software cmodel.c, q2repro,
 * and the Anthology TypeScript collision implementation.
 * Copyright (C) 1997-2005 Id Software, Inc. */
#include "internal.h"

#include <limits.h>
#include <stdlib.h>

#define Q2_DISTANCE_EPSILON 0.03125f
#define Q2_POSITION_LEAF_LIMIT 1024u

typedef struct q2_node {
    uint32_t plane;
    int32_t children[2];
} q2_node;

typedef struct q2_leaf {
    int32_t stored, merged;
    qa_bsp_range brushes;
} q2_leaf;

typedef struct q2_side {
    uint32_t plane;
    int64_t texture;
} q2_side;

typedef struct q2_frame {
    int32_t child;
    float first, last;
    qa_vec3 start, end;
} q2_frame;

typedef struct q2_interval {
    float first, last;
    bool solid;
} q2_interval;

typedef struct q2_collision {
    qa_collision_plane *planes;
    q2_node *nodes;
    q2_leaf *leaves;
    qa_bsp_brush *brushes;
    q2_side *sides;
    qa_collision_surface *surfaces;
    uint32_t *leaf_brushes;
    int32_t *headnodes;
    size_t plane_count, node_count, leaf_count, brush_count;
    size_t side_count, surface_count, leaf_brush_count, model_count;

    /* Queries have one owner; retain scratch instead of allocating per sweep. */
    int32_t *node_stack;
    q2_frame *trace_stack;
    uint32_t *brush_stamps;
    uint32_t stamp;
    q2_interval *intervals, *solid_intervals;
} q2_collision;

typedef struct q2_work {
    q2_collision *collision;
    const qa_trace_query *query;
    qa_vec3 start, end, extents;
    qa_bounds bounds;
    bool stationary, merged;
    uint32_t mask;
    qa_trace_result result;
} q2_work;

static void q2_destroy(void *opaque)
{
    q2_collision *collision = opaque;
    if (collision == NULL) return;
    free(collision->planes);
    free(collision->nodes);
    free(collision->leaves);
    free(collision->brushes);
    free(collision->sides);
    free(collision->surfaces);
    free(collision->leaf_brushes);
    free(collision->headnodes);
    free(collision->node_stack);
    free(collision->trace_stack);
    free(collision->brush_stamps);
    free(collision->intervals);
    free(collision->solid_intervals);
    free(collision);
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

static bool q2_validate_trees(q2_collision *collision, qa_error *error)
{
    if (collision->node_count == 0) return true;
    uint8_t *colors = q2_array(collision->node_count, sizeof(*colors), error);
    if (colors == NULL) return false;
    for (size_t model = 0; model < collision->model_count; ++model) {
        int32_t root = collision->headnodes[model];
        if (root < 0 || colors[(size_t)root] == 2) continue;
        size_t depth = 1;
        collision->node_stack[0] = root;
        while (depth != 0) {
            size_t index = (size_t)collision->node_stack[depth - 1];
            colors[index] = 1;
            const q2_node *node = &collision->nodes[index];
            bool descended = false;
            for (unsigned side = 0; side < 2; ++side) {
                int32_t child = node->children[side];
                if (child < 0) continue;
                uint8_t color = colors[(size_t)child];
                if (color == 1) {
                    free(colors);
                    return q2_bad_reference(error, "node cycle", (size_t)child);
                }
                if (color == 0) {
                    collision->node_stack[depth++] = child;
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
    return true;
}

static float q2_plane_distance(qa_vec3 point, const qa_collision_plane *plane)
{
    float distance = plane->type >= 0 && plane->type < 3
        ? qa_vec_component(point, (unsigned)plane->type) : qa_vec_dot(point, plane->normal);
    return distance - plane->distance;
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

static void q2_next_stamp(q2_collision *collision)
{
    ++collision->stamp;
    if (collision->stamp == 0) {
        if (collision->brush_count != 0)
            memset(collision->brush_stamps, 0, collision->brush_count * sizeof(*collision->brush_stamps));
        collision->stamp = 1;
    }
}

static bool q2_visit_brush(q2_collision *collision, uint32_t index)
{
    if (collision->brush_stamps[index] == collision->stamp) return false;
    collision->brush_stamps[index] = collision->stamp;
    return true;
}

static bool q2_target(const q2_collision *collision, const qa_collision_target *target,
                      uint32_t *model, qa_vec3 basis[3], qa_error *error)
{
    *model = target->inline_model ? target->model : 0;
    if ((size_t)*model >= collision->model_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 collision model %u", (unsigned)*model);
        return false;
    }
    if (target->inline_model) {
        if (!qa_vec_finite(target->origin) || !qa_vec_finite(target->angles)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Nonfinite Q2 collision model transform");
            return false;
        }
        qa_collision_basis(target->angles, basis);
    }
    return true;
}

static qa_vec3 q2_local(qa_vec3 point, const qa_collision_target *target, const qa_vec3 basis[3])
{
    return target->inline_model
        ? qa_collision_to_local(qa_vec_sub(point, target->origin), basis) : point;
}

static bool q2_point_contents(void *opaque, const qa_point_query *query,
                              qa_point_contents *out, qa_error *error)
{
    q2_collision *collision = opaque;
    if (query == NULL || out == NULL || !qa_vec_finite(query->point)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 point contents query");
        return false;
    }
    uint32_t model;
    qa_vec3 basis[3];
    if (!q2_target(collision, &query->target, &model, basis, error)) return false;
    qa_vec3 point = q2_local(query->point, &query->target, basis);
    int32_t child = collision->headnodes[model];
    while (child >= 0) {
        const q2_node *node = &collision->nodes[(size_t)child];
        float distance = q2_plane_distance(point, &collision->planes[node->plane]);
        child = node->children[distance < 0 ? 1 : 0];
    }
    const q2_leaf *leaf = &collision->leaves[q2_leaf_index(child)];
    *out = (qa_point_contents){QA_COLLISION_Q2,
        query->policy.family != QA_COLLISION_Q2 || query->policy.q2_merged_contents ? leaf->merged : leaf->stored,
        leaf->stored, leaf->merged};
    return true;
}

static void q2_trace_brush(q2_work *work, uint32_t index)
{
    q2_collision *collision = work->collision;
    if (!q2_visit_brush(collision, index)) return;
    const qa_bsp_brush *brush = &collision->brushes[index];
    if (((uint32_t)brush->contents & work->mask) == 0 || brush->sides.count == 0) return;
    float enter = -1, second_enter = -1, leave = 1;
    bool start_out = false, get_out = false;
    const q2_side *lead = NULL;
    const qa_collision_plane *second = NULL;
    for (size_t i = 0; i < (size_t)brush->sides.count; ++i) {
        const q2_side *side = &collision->sides[(size_t)brush->sides.first + i];
        const qa_collision_plane *plane = &collision->planes[side->plane];
        float distance = plane->distance + q2_expand(plane, &work->query->shape);
        float first = qa_vec_dot(work->start, plane->normal) - distance;
        if (work->stationary) {
            if (first > 0) return;
            continue;
        }
        float last = qa_vec_dot(work->end, plane->normal) - distance;
        if (first > 0) start_out = true;
        if (last > 0) get_out = true;
        if (first > 0 && (last >= Q2_DISTANCE_EPSILON || last >= first)) return;
        if (first <= 0 && last <= 0) continue;
        if (first > last) {
            float fraction = fmaxf(0, (first - Q2_DISTANCE_EPSILON) / (first - last));
            if (fraction > enter) {
                enter = fraction;
                lead = side;
            } else if (fraction > second_enter) {
                second_enter = fraction;
                second = plane;
            }
        } else {
            leave = fminf(leave, fminf(1, (first + Q2_DISTANCE_EPSILON) / (first - last)));
        }
    }
    if (!start_out) {
        work->result.start_solid = true;
        if (!get_out) {
            work->result.all_solid = true;
            if (work->stationary || work->merged) {
                work->result.fraction = 0;
                work->result.contents = brush->contents;
            }
        }
        return;
    }
    if (enter < leave && enter > -1 && enter < work->result.fraction && lead != NULL) {
        work->result.fraction = enter;
        work->result.plane = collision->planes[lead->plane];
        work->result.has_surface = lead->texture >= 0;
        work->result.surface = lead->texture >= 0 ? collision->surfaces[(size_t)lead->texture]
                                                 : (qa_collision_surface){0};
        work->result.surface_flags = work->result.surface.flags;
        work->result.contents = brush->contents;
        /* q2repro pairs the secondary plane with the primary surface. */
        if (second != NULL) {
            work->result.has_secondary = true;
            work->result.secondary_plane = *second;
            work->result.secondary_has_surface = work->result.has_surface;
            work->result.secondary_surface = work->result.surface;
        }
    }
}

static void q2_trace_leaf(q2_work *work, size_t index)
{
    const q2_leaf *leaf = &work->collision->leaves[index];
    int32_t contents = work->merged ? leaf->merged : leaf->stored;
    if (((uint32_t)contents & work->mask) == 0) return;
    for (size_t i = 0; i < (size_t)leaf->brushes.count; ++i) {
        q2_trace_brush(work, work->collision->leaf_brushes[(size_t)leaf->brushes.first + i]);
        if (work->result.fraction == 0) return;
    }
}

/* Push back first so the front child is visited first, including exact ties. */
static void q2_box_children(q2_collision *collision, const q2_node *node,
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
        collision->node_stack[(*depth)++] = node->children[1];
    if (qa_vec_dot(far_corner, normal) >= plane->distance)
        collision->node_stack[(*depth)++] = node->children[0];
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
    q2_collision *collision = work->collision;
    qa_bounds bounds = q2_envelope(work->start, work->start, work->bounds);
    size_t depth = 1, leaves = 0;
    collision->node_stack[0] = headnode;
    while (depth != 0 && leaves < Q2_POSITION_LEAF_LIMIT) {
        int32_t child = collision->node_stack[--depth];
        if (child < 0) {
            ++leaves;
            q2_trace_leaf(work, q2_leaf_index(child));
            if (work->result.all_solid) return;
        } else {
            q2_box_children(collision, &collision->nodes[(size_t)child], bounds, &depth);
        }
    }
}

static float q2_clamp_fraction(float fraction)
{
    return fmaxf(0, fminf(1, fraction));
}

static void q2_sweep(q2_work *work, int32_t headnode)
{
    q2_collision *collision = work->collision;
    size_t depth = 1;
    collision->trace_stack[0] = (q2_frame){headnode, 0, 1, work->start, work->end};
    while (depth != 0) {
        q2_frame frame = collision->trace_stack[--depth];
        if (work->result.fraction <= frame.first) continue;
        if (frame.child < 0) {
            q2_trace_leaf(work, q2_leaf_index(frame.child));
            continue;
        }
        const q2_node *node = &collision->nodes[(size_t)frame.child];
        const qa_collision_plane *plane = &collision->planes[node->plane];
        float first = q2_plane_distance(frame.start, plane);
        float last = q2_plane_distance(frame.end, plane);
        float offset = plane->type >= 0 && plane->type < 3
            ? qa_vec_component(work->extents, (unsigned)plane->type)
            : fabsf(work->extents.x * plane->normal.x)
                + fabsf(work->extents.y * plane->normal.y)
                + fabsf(work->extents.z * plane->normal.z);
        if (first >= offset && last >= offset) {
            frame.child = node->children[0];
            collision->trace_stack[depth++] = frame;
            continue;
        }
        if (first < -offset && last < -offset) {
            frame.child = node->children[1];
            collision->trace_stack[depth++] = frame;
            continue;
        }
        unsigned side = 0;
        float near_fraction = 1, far_fraction = 0;
        if (first < last) {
            float inverse = 1 / (first - last);
            side = 1;
            far_fraction = (first + offset + Q2_DISTANCE_EPSILON) * inverse;
            near_fraction = (first - offset + Q2_DISTANCE_EPSILON) * inverse;
        } else if (first > last) {
            float inverse = 1 / (first - last);
            far_fraction = (first - offset - Q2_DISTANCE_EPSILON) * inverse;
            near_fraction = (first + offset + Q2_DISTANCE_EPSILON) * inverse;
        }
        near_fraction = q2_clamp_fraction(near_fraction);
        far_fraction = q2_clamp_fraction(far_fraction);
        float span = frame.last - frame.first;
        collision->trace_stack[depth++] = (q2_frame){node->children[side ^ 1u],
            frame.first + span * far_fraction, frame.last,
            qa_vec_lerp(frame.start, frame.end, far_fraction), frame.end};
        collision->trace_stack[depth++] = (q2_frame){node->children[side],
            frame.first, frame.first + span * near_fraction,
            frame.start, qa_vec_lerp(frame.start, frame.end, near_fraction)};
    }
}

static void q2_brush_medium(q2_work *work, uint32_t index, size_t *count)
{
    q2_collision *collision = work->collision;
    if (!q2_visit_brush(collision, index)) return;
    const qa_bsp_brush *brush = &collision->brushes[index];
    int32_t contents = qa_collision_convert_contents(brush->contents, QA_COLLISION_Q2, QA_COLLISION_Q1);
    if (contents == -1 || brush->sides.count == 0) return;
    float begin = 0, end = work->result.fraction;
    for (size_t i = 0; i < (size_t)brush->sides.count; ++i) {
        const q2_side *side = &collision->sides[(size_t)brush->sides.first + i];
        const qa_collision_plane *plane = &collision->planes[side->plane];
        float distance = plane->distance + q2_expand(plane, &work->query->shape);
        float first = qa_vec_dot(work->start, plane->normal) - distance;
        float last = qa_vec_dot(work->end, plane->normal) - distance;
        if (first > 0 && last > 0) return;
        if (first <= 0 && last <= 0) continue;
        float crossing = first / (first - last);
        if (first > last) begin = fmaxf(begin, crossing);
        else end = fminf(end, crossing);
        if (begin > end) return;
    }
    collision->intervals[(*count)++] = (q2_interval){begin, end, contents == -2};
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
    q2_collision *collision = work->collision;
    qa_vec3 reached = qa_vec_lerp(work->start, work->end, work->result.fraction);
    qa_bounds bounds = q2_envelope(work->start, reached, work->bounds);
    size_t depth = 1, leaves = 0, count = 0;
    collision->node_stack[0] = headnode;
    q2_next_stamp(collision);
    while (depth != 0 && leaves < collision->leaf_count) {
        int32_t child = collision->node_stack[--depth];
        if (child < 0) {
            ++leaves;
            const q2_leaf *leaf = &collision->leaves[q2_leaf_index(child)];
            for (size_t i = 0; i < (size_t)leaf->brushes.count; ++i)
                q2_brush_medium(work, collision->leaf_brushes[(size_t)leaf->brushes.first + i], &count);
        } else {
            q2_box_children(collision, &collision->nodes[(size_t)child], bounds, &depth);
        }
    }
    q2_interval_sort(collision->intervals, count);
    work->result.in_open = q2_uncovered(0, work->result.fraction, collision->intervals, count);
    size_t solid_count = 0;
    for (size_t i = 0; i < count; ++i) {
        q2_interval interval = collision->intervals[i];
        if (!interval.solid) continue;
        if (solid_count != 0 && interval.first <= collision->solid_intervals[solid_count - 1].last)
            collision->solid_intervals[solid_count - 1].last = fmaxf(collision->solid_intervals[solid_count - 1].last, interval.last);
        else collision->solid_intervals[solid_count++] = interval;
    }
    size_t solid = 0;
    for (size_t i = 0; i < count; ++i) {
        q2_interval liquid = collision->intervals[i];
        if (liquid.solid) continue;
        while (solid < solid_count && collision->solid_intervals[solid].last < liquid.first) ++solid;
        if (solid == solid_count || collision->solid_intervals[solid].first > liquid.first
            || collision->solid_intervals[solid].last < liquid.last) {
            work->result.in_water = true;
            return;
        }
    }
}

static bool q2_trace(void *opaque, const qa_trace_query *query,
                     qa_trace_result *out, qa_error *error)
{
    q2_collision *collision = opaque;
    if (query == NULL || out == NULL || !qa_vec_finite(query->start) || !qa_vec_finite(query->end)
        || (unsigned)query->shape.kind > (unsigned)QA_SHAPE_CAPSULE
        || (query->shape.kind != QA_SHAPE_POINT && !qa_collision_bounds_valid(query->shape.bounds))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 trace query");
        return false;
    }
    uint32_t model;
    qa_vec3 basis[3];
    if (!q2_target(collision, &query->target, &model, basis, error)) return false;
    q2_work work = {0};
    work.collision = collision;
    work.query = query;
    work.start = q2_local(query->start, &query->target, basis);
    work.end = q2_local(query->end, &query->target, basis);
    work.bounds = q2_shape_bounds(&query->shape);
    work.stationary = work.start.x == work.end.x && work.start.y == work.end.y && work.start.z == work.end.z;
    work.merged = query->policy.family != QA_COLLISION_Q2 || query->policy.q2_merged_contents;
    work.mask = qa_collision_geometry_mask(&query->policy, QA_COLLISION_Q2);
    work.extents = qa_v3(fmaxf(-work.bounds.mins.x, work.bounds.maxs.x),
                         fmaxf(-work.bounds.mins.y, work.bounds.maxs.y),
                         fmaxf(-work.bounds.mins.z, work.bounds.maxs.z));
    work.result = qa_collision_empty_trace(query, QA_COLLISION_Q2);
    int32_t headnode = collision->headnodes[model];
    q2_next_stamp(collision);
    if (work.stationary) q2_position_test(&work, headnode);
    else q2_sweep(&work, headnode);
    if (query->target.inline_model && work.result.fraction != 1) {
        /* Q2 rotates by inverse Euler angles; only the primary normal changes.
         * Plane distance/type/signbits and the secondary plane stay source-native. */
        qa_vec3 inverse[3];
        qa_collision_basis(qa_vec_scale(query->target.angles, -1), inverse);
        work.result.plane.normal = qa_collision_to_local(work.result.plane.normal, inverse);
    }
    work.result.end = qa_vec_lerp(query->start, query->end, work.result.fraction);
    work.result.contact = work.result.fraction < 1 && !work.result.all_solid;
    work.result.contact_plane = work.result.plane;
    work.result.hit = work.result.fraction < 1 || work.result.start_solid ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_NONE;
    if (query->policy.family == QA_COLLISION_Q1) q2_trace_media(&work, headnode);
    *out = work.result;
    return true;
}

static const qa_collision_ops q2_ops = {q2_destroy, q2_trace, q2_point_contents};

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

bool qa_q2_collision_create(const qa_bsp_view *map, qa_collision_kernel *out, qa_error *error)
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
    collision->plane_count = qa_bsp_record_count(map, QA_BSP_PLANES);
    collision->node_count = qa_bsp_record_count(map, QA_BSP_NODES);
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
    Q2_ALLOC(planes, collision->plane_count);
    Q2_ALLOC(nodes, collision->node_count);
    Q2_ALLOC(leaves, collision->leaf_count);
    Q2_ALLOC(brushes, collision->brush_count);
    Q2_ALLOC(sides, collision->side_count);
    Q2_ALLOC(surfaces, collision->surface_count);
    Q2_ALLOC(leaf_brushes, collision->leaf_brush_count);
    Q2_ALLOC(headnodes, collision->model_count);
    Q2_ALLOC(node_stack, collision->node_count + 1);
    Q2_ALLOC(trace_stack, collision->node_count + 1);
    Q2_ALLOC(brush_stamps, collision->brush_count);
    Q2_ALLOC(intervals, collision->brush_count);
    Q2_ALLOC(solid_intervals, collision->brush_count);
#undef Q2_ALLOC
    for (size_t i = 0; i < collision->plane_count; ++i) {
        qa_bsp_plane plane;
        if (!qa_bsp_read_plane(map, i, &plane, error)) goto fail;
        collision->planes[i] = qa_collision_bsp_plane(plane);
    }
    for (size_t i = 0; i < collision->node_count; ++i) {
        qa_bsp_node node;
        if (!qa_bsp_read_node(map, i, &node, error)) goto fail;
        if ((size_t)node.plane >= collision->plane_count
            || !q2_child_valid(collision, node.children[0]) || !q2_child_valid(collision, node.children[1])) {
            q2_bad_reference(error, "node", i);
            goto fail;
        }
        collision->nodes[i] = (q2_node){node.plane, {node.children[0], node.children[1]}};
    }
    for (size_t i = 0; i < collision->surface_count; ++i) {
        qa_bsp_texinfo texture;
        if (!qa_bsp_read_texinfo(map, i, &texture, error)) goto fail;
        qa_collision_surface *surface = &collision->surfaces[i];
        size_t length = texture.name.size;
        if (length >= sizeof(surface->name)) length = sizeof(surface->name) - 1;
        if (length != 0) memcpy(surface->name, texture.name.data, length);
        surface->name[length] = '\0';
        surface->flags = texture.flags;
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
        if (!qa_bsp_read_brush(map, i, &collision->brushes[i], error)) goto fail;
        if (!q2_range_valid(collision->brushes[i].sides, collision->side_count)) {
            q2_bad_reference(error, "brush", i);
            goto fail;
        }
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
        int32_t merged = leaf.contents;
        /* Q2 rerelease merges brush flags into every leaf except solid leaf 0. */
        if (i != 0) {
            for (size_t j = 0; j < (size_t)leaf.brushes.count; ++j) {
                uint32_t brush = collision->leaf_brushes[(size_t)leaf.brushes.first + j];
                merged |= collision->brushes[brush].contents;
            }
        }
        collision->leaves[i] = (q2_leaf){leaf.contents, merged, leaf.brushes};
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
