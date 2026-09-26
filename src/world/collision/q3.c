#include "q3/shared.h"
#include <float.h>
#include <stdlib.h>

typedef struct q3_side { uint32_t plane; int32_t flags, contents; } q3_side;
typedef struct q3_brush {
    qa_bsp_range sides;
    qa_bounds bounds;
    int32_t contents;
    uint32_t visited;
} q3_brush;
typedef struct q3_patch_record {
    qa_q3_patch *collide;
    int32_t contents, flags;
    uint32_t visited;
} q3_patch_record;
typedef struct q3_node { uint32_t plane; int32_t children[2]; } q3_node;
typedef struct q3_leaf { qa_bsp_range brushes, surfaces; } q3_leaf;
typedef struct q3_model {
    uint32_t *brushes, *surfaces;
    size_t brush_count, surface_count;
} q3_model;
typedef struct q3_step { int32_t node; float first, last; qa_vec3 start, end; } q3_step;
typedef struct q3_interval { float first, last; } q3_interval;
typedef struct q3_map {
    qa_collision_plane *planes;
    q3_side *sides;
    q3_brush *brushes;
    q3_patch_record *patches;
    q3_node *nodes;
    q3_leaf *leaves;
    uint32_t *leaf_brushes, *leaf_surfaces;
    q3_model *models;
    size_t plane_count, side_count, brush_count, patch_count, node_count, leaf_count, model_count;
    uint32_t generation;
    q3_step *steps;
    int32_t *pending;
    q3_interval *intervals;
} q3_map;
typedef struct q3_work {
    q3_map *map;
    const qa_trace_query *query;
    qa_trace_result result;
    qa_q3_shape shape;
    qa_vec3 start, end;
    qa_bounds position_bounds;
    uint32_t mask;
    bool stationary, point_trace, curves, player_curves;
} q3_work;

static void *q3_alloc(size_t count, size_t size, qa_error *error) {
    if (count == 0) return NULL;
    if (count > SIZE_MAX / size || count * size > (size_t)PTRDIFF_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Q3 collision allocation is too large");
        return NULL;
    }
    void *memory = calloc(count, size);
    if (memory == NULL) qa_error_set(error, QA_ERROR_MEMORY, 0, "Q3 collision allocation failed");
    return memory;
}

static void q3_destroy(void *state) {
    q3_map *map = state;
    if (map == NULL) return;
    if (map->models != NULL) for (size_t i = 0; i < map->model_count; ++i) {
        free(map->models[i].brushes);
        free(map->models[i].surfaces);
    }
    if (map->patches != NULL) for (size_t i = 0; i < map->patch_count; ++i)
        qa_q3_patch_destroy(map->patches[i].collide);
    free(map->planes); free(map->sides); free(map->brushes); free(map->patches);
    free(map->nodes); free(map->leaves); free(map->leaf_brushes); free(map->leaf_surfaces);
    free(map->models); free(map->steps); free(map->pending); free(map->intervals); free(map);
}

static void q3_next_generation(q3_map *map) {
    if (++map->generation != 0) return;
    for (size_t i = 0; i < map->brush_count; ++i) map->brushes[i].visited = 0;
    for (size_t i = 0; i < map->patch_count; ++i) map->patches[i].visited = 0;
    map->generation = 1;
}

static float q3_plane_distance(qa_collision_plane plane, qa_vec3 point) {
    return (plane.type < 3 ? qa_vec_component(point, (unsigned)plane.type)
                          : qa_vec_dot(point, plane.normal)) - plane.distance;
}

static unsigned q3_box_side(qa_bounds bounds, qa_collision_plane plane) {
    if (plane.type < 3) {
        unsigned axis = (unsigned)plane.type;
        if (plane.distance <= qa_vec_component(bounds.mins, axis)) return 1;
        if (plane.distance >= qa_vec_component(bounds.maxs, axis)) return 2;
        return 3;
    }
    uint8_t signs = plane.signbits;
    qa_vec3 near = qa_v3((signs & 1u) != 0 ? bounds.maxs.x : bounds.mins.x,
                        (signs & 2u) != 0 ? bounds.maxs.y : bounds.mins.y,
                        (signs & 4u) != 0 ? bounds.maxs.z : bounds.mins.z);
    qa_vec3 far = qa_v3((signs & 1u) != 0 ? bounds.mins.x : bounds.maxs.x,
                       (signs & 2u) != 0 ? bounds.mins.y : bounds.maxs.y,
                       (signs & 4u) != 0 ? bounds.mins.z : bounds.maxs.z);
    return (qa_vec_dot(far, plane.normal) >= plane.distance ? 1u : 0u)
         | (qa_vec_dot(near, plane.normal) < plane.distance ? 2u : 0u);
}

typedef bool (*q3_leaf_visit)(void *, uint32_t);
static void q3_visit_box(q3_map *map, qa_bounds bounds, q3_leaf_visit visit, void *context) {
    size_t count = 0;
    map->pending[count++] = map->node_count != 0 ? 0 : -1;
    while (count != 0) {
        int32_t index = map->pending[--count];
        if (index < 0) {
            if (!visit(context, (uint32_t)(-1 - index))) return;
            continue;
        }
        const q3_node *node = &map->nodes[index];
        unsigned side = q3_box_side(bounds, map->planes[node->plane]);
        if (side != 1) map->pending[count++] = node->children[1];
        if (side != 2) map->pending[count++] = node->children[0];
    }
}

static void q3_trace_brush(q3_work *work, uint32_t index) {
    q3_map *map = work->map;
    q3_brush *brush = &map->brushes[index];
    if (brush->visited == map->generation) return;
    brush->visited = map->generation;
    if (((uint32_t)brush->contents & work->mask) == 0 || brush->sides.count == 0) return;
    if (work->stationary && !qa_bounds_overlap(work->position_bounds, brush->bounds)) return;
    float enter = -1.0f, leave = 1.0f;
    bool start_out = false, get_out = false;
    const q3_side *lead = NULL;
    for (uint32_t i = work->stationary && brush->sides.count >= 6 ? 6u : 0u; i < brush->sides.count; ++i) {
        const q3_side *side = &map->sides[brush->sides.first + i];
        qa_collision_plane plane = map->planes[side->plane];
        float first = q3_shape_distance(&work->shape, work->start, plane);
        if (work->stationary) { if (first > 0) return; continue; }
        float last = q3_shape_distance(&work->shape, work->end, plane);
        if (first > 0) start_out = true;
        if (last > 0) get_out = true;
        if (first > 0 && (last >= 0.125f || last >= first)) return;
        if (first <= 0 && last <= 0) continue;
        if (first > last) {
            float fraction = fmaxf(0, (first - 0.125f) / (first - last));
            if (fraction > enter) { enter = fraction; lead = side; }
        } else leave = fminf(leave, fminf(1, (first + 0.125f) / (first - last)));
    }
    if (!start_out) {
        work->result.start_solid = true;
        if (!get_out) {
            work->result.all_solid = true;
            work->result.fraction = 0;
            work->result.contents = brush->contents;
        }
    } else if (enter < leave && enter > -1 && enter < work->result.fraction && lead != NULL) {
        work->result.fraction = fmaxf(0, enter);
        work->result.plane = map->planes[lead->plane];
        work->result.contents = brush->contents;
        work->result.surface_flags = lead->flags;
    }
}

static void q3_trace_patch(q3_work *work, uint32_t index) {
    q3_patch_record *patch = &work->map->patches[index];
    if (patch->collide == NULL || patch->visited == work->map->generation) return;
    patch->visited = work->map->generation;
    if (((uint32_t)patch->contents & work->mask) == 0) return;
    qa_q3_shape shape = work->shape;
    if (work->stationary) {
        if (shape.kind == QA_SHAPE_POINT) shape.kind = QA_SHAPE_BOX;
        if (qa_q3_patch_position(patch->collide, work->start, &shape)) {
            work->result.all_solid = work->result.start_solid = true;
            work->result.fraction = 0;
            work->result.contents = patch->contents;
        }
    } else {
        if (work->point_trace) shape.kind = QA_SHAPE_POINT;
        if (shape.kind == QA_SHAPE_POINT && !work->player_curves) return;
        if (qa_q3_patch_trace(patch->collide, work->start, work->end, &shape,
                             &work->result.fraction, &work->result.plane)) {
            work->result.contents = patch->contents;
            work->result.surface_flags = patch->flags;
        }
    }
}

static bool q3_trace_leaf(void *context, uint32_t index) {
    q3_work *work = context;
    const q3_leaf *leaf = &work->map->leaves[index];
    for (uint32_t i = 0; i < leaf->brushes.count; ++i) {
        q3_trace_brush(work, work->map->leaf_brushes[leaf->brushes.first + i]);
        if (work->result.fraction == 0) return false;
    }
    if (work->curves) for (uint32_t i = 0; i < leaf->surfaces.count; ++i) {
        q3_trace_patch(work, work->map->leaf_surfaces[leaf->surfaces.first + i]);
        if (work->result.fraction == 0) return false;
    }
    return true;
}

static void q3_trace_tree(q3_work *work) {
    q3_map *map = work->map;
    size_t count = 0;
    map->steps[count++] = (q3_step){map->node_count != 0 ? 0 : -1, 0, 1, work->start, work->end};
    while (count != 0) {
        q3_step step = map->steps[--count];
        if (work->result.fraction <= step.first) continue;
        if (step.node < 0) { (void)q3_trace_leaf(work, (uint32_t)(-1 - step.node)); continue; }
        const q3_node *node = &map->nodes[step.node];
        qa_collision_plane plane = map->planes[node->plane];
        float first = q3_plane_distance(plane, step.start), last = q3_plane_distance(plane, step.end);
        float offset = 0;
        if (!work->point_trace && work->shape.kind != QA_SHAPE_POINT)
            offset = plane.type < 3 ? qa_vec_component(work->shape.extents, (unsigned)plane.type) : 2048.0f;
        if (first >= offset + 1 && last >= offset + 1) {
            step.node = node->children[0]; map->steps[count++] = step; continue;
        }
        if (first < -offset - 1 && last < -offset - 1) {
            step.node = node->children[1]; map->steps[count++] = step; continue;
        }
        unsigned side = 0;
        float near_fraction = 1, far_fraction = 0;
        if (first < last) {
            side = 1;
            far_fraction = (first + offset + 0.125f) / (first - last);
            near_fraction = (first - offset + 0.125f) / (first - last);
        } else if (first > last) {
            far_fraction = (first - offset - 0.125f) / (first - last);
            near_fraction = (first + offset + 0.125f) / (first - last);
        }
        near_fraction = fmaxf(0, fminf(1, near_fraction));
        far_fraction = fmaxf(0, fminf(1, far_fraction));
        map->steps[count++] = (q3_step){node->children[side ^ 1u],
            step.first + (step.last - step.first) * far_fraction, step.last,
            qa_vec_lerp(step.start, step.end, far_fraction), step.end};
        map->steps[count++] = (q3_step){node->children[side], step.first,
            step.first + (step.last - step.first) * near_fraction,
            step.start, qa_vec_lerp(step.start, step.end, near_fraction)};
    }
}

typedef struct q3_position_visit { q3_work *work; size_t count; } q3_position_visit;
static bool q3_position_leaf(void *context, uint32_t index) {
    q3_position_visit *visit = context;
    if (visit->count++ == 1024) return false;
    return q3_trace_leaf(visit->work, index);
}

static int q3_compare_interval(const void *first, const void *second) {
    const q3_interval *a = first, *b = second;
    return a->first < b->first ? -1 : a->first > b->first ? 1 : 0;
}

static bool q3_uncovered(q3_interval interval, const q3_interval *parts, size_t count) {
    float position = interval.first;
    if (interval.first == interval.last) {
        for (size_t i = 0; i < count; ++i)
            if (parts[i].first <= position && parts[i].last >= position) return false;
        return true;
    }
    for (size_t i = 0; i < count; ++i) {
        if (parts[i].last < position) continue;
        if (parts[i].first > position) return true;
        position = fmaxf(position, parts[i].last);
        if (position >= interval.last) return false;
    }
    return position < interval.last;
}

typedef struct q3_media_work { q3_work *trace; size_t solid_count, liquid_count; } q3_media_work;
static void q3_media_brush(q3_media_work *media, uint32_t index) {
    q3_work *trace = media->trace;
    q3_map *map = trace->map;
    q3_brush *brush = &map->brushes[index];
    if (brush->visited == map->generation) return;
    brush->visited = map->generation;
    int32_t contents = qa_collision_convert_contents(brush->contents, QA_COLLISION_Q3, QA_COLLISION_Q1);
    if (contents == -1 || brush->sides.count == 0) return;
    q3_interval interval = {0, trace->result.fraction};
    for (uint32_t i = 0; i < brush->sides.count; ++i) {
        qa_collision_plane plane = map->planes[map->sides[brush->sides.first + i].plane];
        float first = q3_shape_distance(&trace->shape, trace->start, plane);
        float last = q3_shape_distance(&trace->shape, trace->end, plane);
        if (first > 0 && last > 0) return;
        if (first <= 0 && last <= 0) continue;
        float crossing = first / (first - last);
        if (first > last) interval.first = fmaxf(interval.first, crossing);
        else interval.last = fminf(interval.last, crossing);
        if (interval.first > interval.last) return;
    }
    if (contents == -2) map->intervals[media->solid_count++] = interval;
    else map->intervals[map->brush_count + media->liquid_count++] = interval;
}

static bool q3_media_leaf(void *context, uint32_t index) {
    q3_media_work *media = context;
    q3_map *map = media->trace->map;
    const q3_leaf *leaf = &map->leaves[index];
    for (uint32_t i = 0; i < leaf->brushes.count; ++i)
        q3_media_brush(media, map->leaf_brushes[leaf->brushes.first + i]);
    return true;
}

static void q3_trace_media(q3_work *work, uint32_t model) {
    q3_map *map = work->map;
    q3_media_work media = {work, 0, 0};
    q3_next_generation(map);
    if (model != 0) {
        const q3_model *members = &map->models[model];
        for (size_t i = 0; i < members->brush_count; ++i) q3_media_brush(&media, members->brushes[i]);
    } else {
        qa_vec3 reached = qa_vec_lerp(work->start, work->end, work->result.fraction);
        qa_bounds envelope = qa_bounds_union(q3_shape_bounds(&work->shape, work->start), q3_shape_bounds(&work->shape, reached));
        envelope.mins = qa_vec_sub(envelope.mins, qa_v3(1, 1, 1));
        envelope.maxs = qa_vec_add(envelope.maxs, qa_v3(1, 1, 1));
        q3_visit_box(map, envelope, q3_media_leaf, &media);
    }
    if (media.solid_count > 1) qsort(map->intervals, media.solid_count, sizeof(*map->intervals), q3_compare_interval);
    work->result.in_water = false;
    for (size_t i = 0; i < media.liquid_count; ++i)
        if (q3_uncovered(map->intervals[map->brush_count + i], map->intervals, media.solid_count)) {
            work->result.in_water = true; break;
        }
    if (media.liquid_count != 0)
        memmove(map->intervals + media.solid_count, map->intervals + map->brush_count,
                media.liquid_count * sizeof(*map->intervals));
    size_t count = media.solid_count + media.liquid_count;
    if (count > 1) qsort(map->intervals, count, sizeof(*map->intervals), q3_compare_interval);
    work->result.in_open = q3_uncovered((q3_interval){0, work->result.fraction}, map->intervals, count);
}

static bool q3_trace(void *state, const qa_trace_query *query, qa_trace_result *out, qa_error *error) {
    q3_map *map = state;
    uint32_t model = query->target.inline_model ? query->target.model : 0;
    if (model >= map->model_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, model, "Q3 collision model index is out of range"); return false;
    }
    q3_work work = {0};
    work.map = map; work.query = query;
    work.result = qa_collision_empty_trace(query, QA_COLLISION_Q3);
    qa_vec3 center, basis[3];
    work.shape = q3_prepare_shape(query->shape, &center);
    work.start = qa_vec_add(query->start, center); work.end = qa_vec_add(query->end, center);
    work.stationary = q3_same_point(query->start, query->end);
    bool rotated = query->target.inline_model && !q3_same_point(query->target.angles, qa_v3(0, 0, 0));
    if (query->target.inline_model) {
        work.start = qa_vec_sub(work.start, query->target.origin);
        work.end = qa_vec_sub(work.end, query->target.origin);
        if (rotated) {
            qa_collision_basis(query->target.angles, basis);
            work.start = qa_collision_to_local(work.start, basis); work.end = qa_collision_to_local(work.end, basis);
            if (work.shape.kind == QA_SHAPE_CAPSULE) q3_rotate_capsule(&work.shape, basis);
        }
        work.stationary = q3_same_point(work.start, work.end);
        center = qa_vec_scale(qa_vec_add(work.shape.mins, work.shape.extents), 0.5f);
        work.shape.mins = qa_vec_sub(work.shape.mins, center); work.shape.extents = qa_vec_sub(work.shape.extents, center);
        work.start = qa_vec_add(work.start, center); work.end = qa_vec_add(work.end, center);
    }
    work.point_trace = !work.stationary && q3_same_point(work.shape.mins, qa_v3(0, 0, 0));
    work.position_bounds = q3_shape_bounds(&work.shape, work.start);
    work.mask = qa_collision_geometry_mask(&query->policy, QA_COLLISION_Q3);
    work.curves = query->policy.family != QA_COLLISION_Q3 || query->policy.curves;
    work.player_curves = query->policy.family != QA_COLLISION_Q3 || query->policy.player_curve_clip;
    q3_next_generation(map);
    if (model != 0) {
        const q3_model *members = &map->models[model];
        for (size_t i = 0; i < members->brush_count && work.result.fraction != 0; ++i)
            q3_trace_brush(&work, members->brushes[i]);
        if (work.curves) for (size_t i = 0; i < members->surface_count && work.result.fraction != 0; ++i)
            q3_trace_patch(&work, members->surfaces[i]);
    } else if (work.stationary) {
        qa_bounds bounds = {qa_vec_sub(qa_vec_add(work.start, work.shape.mins), qa_v3(1, 1, 1)),
                            qa_vec_add(qa_vec_add(work.start, work.shape.extents), qa_v3(1, 1, 1))};
        q3_position_visit visit = {&work, 0};
        q3_visit_box(map, bounds, q3_position_leaf, &visit);
    } else q3_trace_tree(&work);
    if (query->policy.family == QA_COLLISION_Q1) q3_trace_media(&work, model);
    if (rotated && work.result.fraction != 1)
        work.result.plane.normal = qa_collision_from_local(work.result.plane.normal, basis);
    q3_finish_trace(query, &work.result);
    *out = work.result;
    return true;
}

static int32_t q3_brush_point(const q3_map *map, uint32_t index, qa_vec3 point) {
    const q3_brush *brush = &map->brushes[index];
    for (uint32_t i = 0; i < brush->sides.count; ++i) {
        qa_collision_plane plane = map->planes[map->sides[brush->sides.first + i].plane];
        if (qa_vec_dot(point, plane.normal) > plane.distance) return 0;
    }
    return brush->contents;
}

static bool q3_point_contents(void *state, const qa_point_query *query, qa_point_contents *out, qa_error *error) {
    const q3_map *map = state;
    uint32_t model = query->target.inline_model ? query->target.model : 0;
    if (model >= map->model_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, model, "Q3 collision model index is out of range"); return false;
    }
    qa_vec3 point = query->point;
    if (query->target.inline_model) {
        point = qa_vec_sub(point, query->target.origin);
        if (!q3_same_point(query->target.angles, qa_v3(0, 0, 0))) {
            qa_vec3 basis[3]; qa_collision_basis(query->target.angles, basis);
            point = qa_collision_to_local(point, basis);
        }
    }
    int32_t contents = 0;
    if (model != 0) {
        const q3_model *members = &map->models[model];
        for (size_t i = 0; i < members->brush_count; ++i) contents |= q3_brush_point(map, members->brushes[i], point);
    } else {
        int32_t index = map->node_count != 0 ? 0 : -1;
        while (index >= 0) {
            const q3_node *node = &map->nodes[index];
            index = node->children[q3_plane_distance(map->planes[node->plane], point) < 0 ? 1 : 0];
        }
        const q3_leaf *leaf = &map->leaves[-1 - index];
        for (uint32_t i = 0; i < leaf->brushes.count; ++i)
            contents |= q3_brush_point(map, map->leaf_brushes[leaf->brushes.first + i], point);
    }
    *out = (qa_point_contents){QA_COLLISION_Q3, contents, contents, contents};
    return true;
}

static bool q3_load_indices(const qa_bsp_view *bsp, qa_bsp_lump_kind kind, uint32_t **out, qa_error *error) {
    size_t count = qa_bsp_record_count(bsp, kind);
    uint32_t *indices = q3_alloc(count, sizeof(*indices), error);
    if (count != 0 && indices == NULL) return false;
    for (size_t i = 0; i < count; ++i) {
        int64_t index;
        if (!qa_bsp_read_index(bsp, kind, i, &index, error)) { free(indices); return false; }
        indices[i] = (uint32_t)index;
    }
    *out = indices;
    return true;
}

static bool q3_load_model(q3_map *map, const qa_bsp_model *source, q3_model *model, qa_error *error) {
    if (!source->membership_from_tree) {
        model->brush_count = source->brushes.count; model->surface_count = source->faces.count;
        model->brushes = q3_alloc(model->brush_count, sizeof(*model->brushes), error);
        model->surfaces = q3_alloc(model->surface_count, sizeof(*model->surfaces), error);
        if ((model->brush_count != 0 && model->brushes == NULL) || (model->surface_count != 0 && model->surfaces == NULL)) return false;
        for (size_t i = 0; i < model->brush_count; ++i) model->brushes[i] = source->brushes.first + (uint32_t)i;
        for (size_t i = 0; i < model->surface_count; ++i) model->surfaces[i] = source->faces.first + (uint32_t)i;
        return true;
    }
    uint8_t *nodes = q3_alloc(map->node_count, 1, error);
    if (map->node_count != 0 && nodes == NULL) return false;
    q3_next_generation(map);
    size_t count = 0;
    map->pending[count++] = source->headnodes[0];
    while (count != 0) {
        int32_t index = map->pending[--count];
        if (index >= 0) {
            if (nodes[index] != 0) continue;
            nodes[index] = 1;
            map->pending[count++] = map->nodes[index].children[1];
            map->pending[count++] = map->nodes[index].children[0];
        } else {
            const q3_leaf *leaf = &map->leaves[-1 - index];
            for (uint32_t i = 0; i < leaf->brushes.count; ++i)
                map->brushes[map->leaf_brushes[leaf->brushes.first + i]].visited = map->generation;
            for (uint32_t i = 0; i < leaf->surfaces.count; ++i)
                map->patches[map->leaf_surfaces[leaf->surfaces.first + i]].visited = map->generation;
        }
    }
    free(nodes);
    for (size_t i = 0; i < map->brush_count; ++i) if (map->brushes[i].visited == map->generation) ++model->brush_count;
    for (size_t i = 0; i < map->patch_count; ++i) if (map->patches[i].visited == map->generation) ++model->surface_count;
    model->brushes = q3_alloc(model->brush_count, sizeof(*model->brushes), error);
    model->surfaces = q3_alloc(model->surface_count, sizeof(*model->surfaces), error);
    if ((model->brush_count != 0 && model->brushes == NULL) || (model->surface_count != 0 && model->surfaces == NULL)) return false;
    size_t brush = 0, surface = 0;
    for (size_t i = 0; i < map->brush_count; ++i) if (map->brushes[i].visited == map->generation) model->brushes[brush++] = (uint32_t)i;
    for (size_t i = 0; i < map->patch_count; ++i) if (map->patches[i].visited == map->generation) model->surfaces[surface++] = (uint32_t)i;
    return true;
}

bool qa_q3_collision_create(const qa_bsp_view *bsp, qa_collision_kernel *out, qa_error *error) {
    static const qa_collision_ops ops = {q3_destroy, q3_trace, q3_point_contents};
    q3_map *map = q3_alloc(1, sizeof(*map), error);
    if (map == NULL) return false;
    map->plane_count = qa_bsp_record_count(bsp, QA_BSP_PLANES);
    map->side_count = qa_bsp_record_count(bsp, QA_BSP_BRUSH_SIDES);
    map->brush_count = qa_bsp_record_count(bsp, QA_BSP_BRUSHES);
    map->patch_count = qa_bsp_record_count(bsp, QA_BSP_SURFACES);
    map->node_count = qa_bsp_record_count(bsp, QA_BSP_NODES);
    map->leaf_count = qa_bsp_record_count(bsp, QA_BSP_LEAVES);
    map->model_count = qa_bsp_record_count(bsp, QA_BSP_MODELS);
    if (map->leaf_count == 0 || map->model_count == 0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q3 collision map has no leaves or models"); goto fail;
    }
#define Q3_ALLOC(member, count) do { map->member = q3_alloc((count), sizeof(*map->member), error); if ((count) != 0 && map->member == NULL) goto fail; } while (0)
    Q3_ALLOC(planes, map->plane_count); Q3_ALLOC(sides, map->side_count);
    Q3_ALLOC(brushes, map->brush_count); Q3_ALLOC(patches, map->patch_count);
    Q3_ALLOC(nodes, map->node_count); Q3_ALLOC(leaves, map->leaf_count);
    Q3_ALLOC(models, map->model_count); Q3_ALLOC(steps, map->node_count + 1);
    Q3_ALLOC(pending, map->node_count + 1);
    if (map->brush_count > SIZE_MAX / 2) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Q3 media interval count overflow"); goto fail; }
    Q3_ALLOC(intervals, map->brush_count * 2);
#undef Q3_ALLOC
    if (!q3_load_indices(bsp, QA_BSP_LEAF_BRUSHES, &map->leaf_brushes, error)
        || !q3_load_indices(bsp, QA_BSP_LEAF_FACES, &map->leaf_surfaces, error)) goto fail;
    for (size_t i = 0; i < map->plane_count; ++i) {
        qa_bsp_plane plane;
        if (!qa_bsp_read_plane(bsp, i, &plane, error)) goto fail;
        int32_t type = plane.normal.x == 1 ? 0 : plane.normal.y == 1 ? 1 : plane.normal.z == 1 ? 2 : 3;
        map->planes[i] = qa_collision_make_plane(qa_bsp_to_vec(plane.normal), plane.distance, type);
    }
    for (size_t i = 0; i < map->side_count; ++i) {
        qa_bsp_brush_side side;
        if (!qa_bsp_read_brush_side(bsp, i, &side, error)) goto fail;
        int32_t flags = side.flags;
        if (bsp->format != QA_BSP_IBSP44) {
            qa_bsp_shader shader;
            if (!qa_bsp_read_shader(bsp, (size_t)side.shader, &shader, error)) goto fail;
            flags = shader.surface_flags;
        }
        map->sides[i] = (q3_side){side.plane, flags, 0};
    }
    for (size_t i = 0; i < map->brush_count; ++i) {
        qa_bsp_brush source;
        if (!qa_bsp_read_brush(bsp, i, &source, error)) goto fail;
        q3_brush *brush = &map->brushes[i];
        brush->sides = source.sides; brush->contents = source.contents;
        if (bsp->format != QA_BSP_IBSP44) {
            qa_bsp_shader shader;
            if (!qa_bsp_read_shader(bsp, (size_t)source.shader, &shader, error)) goto fail;
            brush->contents = shader.content_flags;
        }
        for (uint32_t side = 0; side < source.sides.count; ++side)
            map->sides[source.sides.first + side].contents = brush->contents;
        brush->bounds = (qa_bounds){qa_v3(-FLT_MAX, -FLT_MAX, -FLT_MAX), qa_v3(FLT_MAX, FLT_MAX, FLT_MAX)};
        if (source.sides.count >= 6) for (unsigned axis = 0; axis < 3; ++axis) {
            uint32_t first = source.sides.first + axis * 2u;
            qa_vec_set_component(&brush->bounds.mins, axis, -map->planes[map->sides[first].plane].distance);
            qa_vec_set_component(&brush->bounds.maxs, axis, map->planes[map->sides[first + 1].plane].distance);
        }
    }
    for (size_t i = 0; i < map->node_count; ++i) {
        qa_bsp_node node;
        if (!qa_bsp_read_node(bsp, i, &node, error)) goto fail;
        map->nodes[i] = (q3_node){node.plane, {node.children[0], node.children[1]}};
    }
    for (size_t i = 0; i < map->leaf_count; ++i) {
        qa_bsp_leaf leaf;
        if (!qa_bsp_read_leaf(bsp, i, &leaf, error)) goto fail;
        map->leaves[i] = (q3_leaf){leaf.brushes, leaf.faces};
    }
    for (size_t i = 0; i < map->model_count; ++i) {
        qa_bsp_model model;
        if (!qa_bsp_read_model(bsp, i, &model, error) || !q3_load_model(map, &model, &map->models[i], error)) goto fail;
    }
    for (size_t i = 0; i < map->patch_count; ++i) {
        qa_bsp_surface surface;
        if (!qa_bsp_read_surface(bsp, i, &surface, error)) goto fail;
        if (surface.type != QA_BSP_SURFACE_PATCH) continue;
        q3_patch_record *patch = &map->patches[i];
        if (bsp->format == QA_BSP_IBSP44) {
            if (surface.brush_side >= 0) {
                const q3_side *side = &map->sides[surface.brush_side];
                patch->flags = side->flags; patch->contents = side->contents;
            }
        } else {
            qa_bsp_shader shader;
            if (!qa_bsp_read_shader(bsp, (size_t)surface.shader, &shader, error)) goto fail;
            patch->flags = shader.surface_flags; patch->contents = shader.content_flags;
        }
        qa_vec3 *points = q3_alloc(surface.vertices.count, sizeof(*points), error);
        if (points == NULL) goto fail;
        for (uint32_t vertex = 0; vertex < surface.vertices.count; ++vertex) {
            qa_bsp_vertex source;
            if (!qa_bsp_read_vertex(bsp, surface.vertices.first + vertex, &source, error)) { free(points); goto fail; }
            points[vertex] = qa_bsp_to_vec(source.position);
        }
        bool generated = qa_q3_patch_create((uint32_t)surface.patch_width, (uint32_t)surface.patch_height, points, &patch->collide, error);
        free(points);
        if (!generated) goto fail;
    }
    *out = (qa_collision_kernel){map, &ops};
    return true;
fail:
    q3_destroy(map);
    return false;
}
