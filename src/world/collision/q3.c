#include "q3/shared.h"
#include "qa/stamp.h"
#include "qa/collision_bits.h"
#include <float.h>
#include <stdlib.h>

typedef struct q3_side { uint32_t plane; qa_collision_bits flags, contents; } q3_side;
typedef struct q3_brush {
    qa_bsp_range sides;
    qa_bounds bounds;
    qa_collision_bits contents;
} q3_brush;
typedef struct q3_patch_record {
    qa_q3_patch *collide;
    qa_collision_bits contents, flags;
} q3_patch_record;
typedef struct q3_leaf { qa_bsp_range brushes, surfaces; } q3_leaf;
typedef struct q3_model {
    uint32_t *brushes, *surfaces;
    size_t brush_count, surface_count;
} q3_model;
typedef struct q3_interval { float first, last; } q3_interval;
typedef struct q3_map {
    const qa_collision_plane *planes;
    q3_side *sides;
    q3_brush *brushes;
    q3_patch_record *patches;
    const qa_collision_node *nodes;
    q3_leaf *leaves;
    uint32_t *leaf_brushes, *leaf_surfaces;
    q3_model *models;
    size_t plane_count, side_count, brush_count, patch_count, node_count, leaf_count, model_count;
} q3_map;
typedef struct q3_scratch {
    qa_stamp_set visited;
    qa_collision_trace_frame *steps;
    int32_t *pending;
    q3_interval *intervals;
} q3_scratch;
typedef struct q3_model_scratch {
    qa_stamp_set visited;
    int32_t *pending;
} q3_model_scratch;
typedef struct q3_work {
    const q3_map *map;
    q3_scratch *scratch;
    const qa_trace_query *query;
    qa_trace_result result;
    qa_q3_shape shape;
    qa_collision_trace_rules rules;
    qa_vec3 start, end;
    qa_bounds position_bounds;
    qa_collision_bits mask;
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
    free(map->sides); free(map->brushes); free(map->patches);
    free(map->leaves); free(map->leaf_brushes); free(map->leaf_surfaces);
    free(map->models); free(map);
}

static void q3_destroy_scratch(void *state) {
    q3_scratch *scratch = state;
    if (scratch == NULL) return;
    free(scratch->visited.marks); free(scratch->steps);
    free(scratch->pending); free(scratch->intervals); free(scratch);
}

static void *q3_create_scratch(const void *state, qa_error *error) {
    const q3_map *map = state;
    if (map->brush_count > SIZE_MAX - map->patch_count || map->brush_count > SIZE_MAX / 2
        || map->node_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Q3 collision scratch count overflow");
        return NULL;
    }
    q3_scratch *scratch = q3_alloc(1, sizeof(*scratch), error);
    if (scratch == NULL) return NULL;
    size_t count = map->brush_count + map->patch_count;
    uint32_t *marks = q3_alloc(count, sizeof(*marks), error);
    qa_stamp_set_init(&scratch->visited, marks, marks != NULL ? count : 0);
    if (count != 0 && marks == NULL) goto fail;
    scratch->steps = q3_alloc(map->node_count + 1, sizeof(*scratch->steps), error);
    scratch->pending = q3_alloc(map->node_count + 1, sizeof(*scratch->pending), error);
    scratch->intervals = q3_alloc(map->brush_count * 2, sizeof(*scratch->intervals), error);
    if (scratch->steps == NULL || scratch->pending == NULL
        || (map->brush_count != 0 && scratch->intervals == NULL)) goto fail;
    return scratch;
fail:
    q3_destroy_scratch(scratch);
    return NULL;
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
static void q3_visit_box(const q3_map *map, q3_scratch *scratch, qa_bounds bounds, q3_leaf_visit visit, void *context) {
    size_t count = 0;
    scratch->pending[count++] = map->node_count != 0 ? 0 : -1;
    while (count != 0) {
        int32_t index = scratch->pending[--count];
        if (index < 0) {
            if (!visit(context, (uint32_t)(-1 - index))) return;
            continue;
        }
        const qa_collision_node *node = &map->nodes[index];
        unsigned side = q3_box_side(bounds, map->planes[node->plane]);
        if (side != 1) scratch->pending[count++] = node->children[1];
        if (side != 2) scratch->pending[count++] = node->children[0];
    }
}

static qa_collision_side_distances q3_brush_distances(void *context, size_t index, bool need_last) {
    q3_work *work = context;
    qa_collision_plane plane = work->map->planes[work->map->sides[index].plane];
    return (qa_collision_side_distances){q3_shape_distance(&work->shape, work->start, plane),
        need_last ? q3_shape_distance(&work->shape, work->end, plane) : 0};
}

static void q3_trace_brush(q3_work *work, uint32_t index) {
    const q3_map *map = work->map;
    const q3_brush *brush = &map->brushes[index];
    if (!qa_stamp_set_mark(&work->scratch->visited, index)) return;
    if (!qa_collision_bits_overlap(brush->contents, work->mask) || brush->sides.count == 0) return;
    if (work->stationary && !qa_bounds_overlap(work->position_bounds, brush->bounds)) return;
    uint32_t skipped = work->stationary && brush->sides.count >= 6 ? 6u : 0u;
    qa_collision_brush_contact contact;
    if (!qa_collision_trace_brush(work, q3_brush_distances,
            (size_t)brush->sides.first + skipped, brush->sides.count - skipped,
            work->stationary, &work->rules.brush, brush->contents, &work->result, &contact)) return;
    const q3_side *lead = &map->sides[contact.side];
    work->result.plane = map->planes[lead->plane];
    work->result.surface_flags = lead->flags;
    if (contact.secondary != SIZE_MAX) {
        work->result.has_secondary = true;
        work->result.secondary_plane = map->planes[map->sides[contact.secondary].plane];
    }
}

static void q3_trace_patch(q3_work *work, uint32_t index) {
    const q3_patch_record *patch = &work->map->patches[index];
    if (patch->collide == NULL
        || !qa_stamp_set_mark(&work->scratch->visited, work->map->brush_count + index)) return;
    if (!qa_collision_bits_overlap(patch->contents, work->mask)) return;
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
        if (qa_q3_patch_trace(patch->collide, work->start, work->end, &shape, work->rules.brush.epsilon,
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

static float q3_tree_extent(void *context, uint32_t index) {
    q3_work *work = context;
    if (work->point_trace || work->shape.kind == QA_SHAPE_POINT) return 0;
    const qa_collision_plane *plane = &work->map->planes[index];
    if (plane->type < 3) return qa_vec_component(work->shape.extents, (unsigned)plane->type);
    if (work->rules.conservative_extent) return 2048.0f;
    return fabsf(work->shape.extents.x * plane->normal.x)
        + fabsf(work->shape.extents.y * plane->normal.y)
        + fabsf(work->shape.extents.z * plane->normal.z);
}

static void q3_tree_leaf(void *context, uint32_t leaf) {
    (void)q3_trace_leaf(context, leaf);
}

static void q3_trace_tree(q3_work *work) {
    const q3_map *map = work->map;
    const qa_collision_tree_trace trace = {
        map->planes, map->nodes, work->scratch->steps, work->start, work->end,
        work->rules.tree, &work->result, work, q3_tree_extent, q3_tree_leaf};
    qa_collision_trace_tree(&trace, map->node_count != 0 ? 0 : -1);
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
    const q3_map *map = trace->map;
    const q3_brush *brush = &map->brushes[index];
    if (!qa_stamp_set_mark(&trace->scratch->visited, index)) return;
    int32_t medium = qa_collision_q1_medium_class(brush->contents);
    bool solid = medium == -2;
    if ((medium == -1) || brush->sides.count == 0) return;
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
    if (solid) trace->scratch->intervals[media->solid_count++] = interval;
    else trace->scratch->intervals[map->brush_count + media->liquid_count++] = interval;
}

static bool q3_media_leaf(void *context, uint32_t index) {
    q3_media_work *media = context;
    const q3_map *map = media->trace->map;
    const q3_leaf *leaf = &map->leaves[index];
    for (uint32_t i = 0; i < leaf->brushes.count; ++i)
        q3_media_brush(media, map->leaf_brushes[leaf->brushes.first + i]);
    return true;
}

static void q3_trace_media(q3_work *work, uint32_t model) {
    const q3_map *map = work->map;
    q3_scratch *scratch = work->scratch;
    q3_media_work media = {work, 0, 0};
    qa_stamp_set_begin(&scratch->visited);
    if (model != 0) {
        const q3_model *members = &map->models[model];
        for (size_t i = 0; i < members->brush_count; ++i) q3_media_brush(&media, members->brushes[i]);
    } else {
        qa_vec3 reached = qa_vec_lerp(work->start, work->end, work->result.fraction);
        qa_bounds envelope = qa_bounds_union(q3_shape_bounds(&work->shape, work->start), q3_shape_bounds(&work->shape, reached));
        envelope.mins = qa_vec_sub(envelope.mins, qa_v3(1, 1, 1));
        envelope.maxs = qa_vec_add(envelope.maxs, qa_v3(1, 1, 1));
        q3_visit_box(map, scratch, envelope, q3_media_leaf, &media);
    }
    if (media.solid_count > 1) qsort(scratch->intervals, media.solid_count, sizeof(*scratch->intervals), q3_compare_interval);
    work->result.in_water = false;
    for (size_t i = 0; i < media.liquid_count; ++i)
        if (q3_uncovered(scratch->intervals[map->brush_count + i], scratch->intervals, media.solid_count)) {
            work->result.in_water = true; break;
        }
    if (media.liquid_count != 0)
        memmove(scratch->intervals + media.solid_count, scratch->intervals + map->brush_count,
                media.liquid_count * sizeof(*scratch->intervals));
    size_t count = media.solid_count + media.liquid_count;
    if (count > 1) qsort(scratch->intervals, count, sizeof(*scratch->intervals), q3_compare_interval);
    work->result.in_open = q3_uncovered((q3_interval){0, work->result.fraction}, scratch->intervals, count);
}

static void q3_trace_model(q3_work *work, uint32_t model) {
    const q3_model *members = &work->map->models[model];
    for (size_t i = 0; i < members->brush_count && work->result.fraction != 0; ++i)
        q3_trace_brush(work, members->brushes[i]);
    if (work->curves) for (size_t i = 0; i < members->surface_count && work->result.fraction != 0; ++i)
        q3_trace_patch(work, members->surfaces[i]);
}

bool qa_q3_trace_capsule_replacement(const void *state, void *scratch_state, const qa_trace_query *query,
                                     qa_vec3 start, qa_vec3 end, qa_q3_shape shape,
                                     qa_bounds position_bounds, bool stationary,
                                     bool point_trace, qa_trace_result *out, qa_error *error) {
    const q3_map *map = state;
    q3_scratch *scratch = scratch_state;
    if (map == NULL || map->model_count <= 255) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 255, "Q3 capsule replacement model is absent");
        return false;
    }
    q3_work work = {.map = map, .scratch = scratch, .query = query,
        .rules = qa_collision_rules(&query->policy),
        .result = qa_collision_empty_trace(query, QA_GAME_Q3),
        .shape = shape, .start = start, .end = end, .position_bounds = position_bounds,
        .mask = query->policy.contents_mask,
        .stationary = stationary, .point_trace = point_trace,
        .curves = query->policy.curves, .player_curves = query->policy.player_curve_clip};
    qa_stamp_set_begin(&scratch->visited);
    q3_trace_model(&work, 255);
    *out = work.result;
    return true;
}

bool qa_q3_trace_model_source(const void *state, void *scratch_state, const qa_trace_query *query, uint32_t model,
                               bool transformed, qa_trace_result *out, qa_error *error) {
    const q3_map *map = state;
    q3_scratch *scratch = scratch_state;
    if (model >= map->model_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, model, "Q3 collision model index is out of range"); return false;
    }
    q3_work work = {0};
    work.map = map; work.scratch = scratch; work.query = query;
    work.rules = qa_collision_rules(&query->policy);
    work.result = qa_collision_empty_trace(query, QA_GAME_Q3);
    work.result.model = model;
    qa_vec3 center, basis[3];
    work.shape = q3_prepare_shape(query->shape, &center);
    work.start = qa_vec_add(query->start, center); work.end = qa_vec_add(query->end, center);
    work.stationary = q3_same_point(query->start, query->end);
    bool rotated = transformed && qa_collision_pose_rotates(&query->target);
    if (rotated) qa_collision_pose_basis(&query->target, true, basis);
    if (transformed) {
        work.start = qa_vec_sub(work.start, query->target.origin);
        work.end = qa_vec_sub(work.end, query->target.origin);
        if (rotated) {
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
    work.mask = query->policy.contents_mask;
    work.curves = query->policy.curves;
    work.player_curves = query->policy.player_curve_clip;
    qa_stamp_set_begin(&scratch->visited);
    if (model != 0) {
        q3_trace_model(&work, model);
    } else if (work.stationary) {
        qa_bounds bounds = {qa_vec_sub(qa_vec_add(work.start, work.shape.mins), qa_v3(1, 1, 1)),
                            qa_vec_add(qa_vec_add(work.start, work.shape.extents), qa_v3(1, 1, 1))};
        q3_position_visit visit = {&work, 0};
        q3_visit_box(map, scratch, bounds, q3_position_leaf, &visit);
    } else q3_trace_tree(&work);
    if (query->policy.behavior->hull_boxes) q3_trace_media(&work, model);
    if (rotated && work.result.fraction != 1)
        work.result.plane.normal = qa_collision_pose_normal(work.result.plane.normal, &query->target, true, basis);
    q3_finish_trace(query, &work.result);
    *out = work.result;
    return true;
}

static bool q3_trace(const void *state, void *scratch, const qa_trace_query *query, qa_trace_result *out, qa_error *error) {
    return qa_q3_trace_model_source(state, scratch, query,
        query->target.inline_model ? query->target.model : 0,
        query->target.inline_model, out, error);
}

static qa_collision_bits q3_brush_point(const q3_map *map, uint32_t index, qa_vec3 point) {
    const q3_brush *brush = &map->brushes[index];
    for (uint32_t i = 0; i < brush->sides.count; ++i) {
        qa_collision_plane plane = map->planes[map->sides[brush->sides.first + i].plane];
        if (qa_vec_dot(point, plane.normal) > plane.distance) return (qa_collision_bits){0};
    }
    return brush->contents;
}

static bool q3_point_contents(const void *state, void *scratch, const qa_point_query *query, qa_point_contents *out, qa_error *error) {
    (void)scratch;
    const q3_map *map = state;
    uint32_t model = query->target.inline_model ? query->target.model : 0;
    if (model >= map->model_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, model, "Q3 collision model index is out of range"); return false;
    }
    qa_vec3 point = query->point;
    if (query->target.inline_model) {
        point = qa_vec_sub(point, query->target.origin);
        if (qa_collision_pose_rotates(&query->target)) {
            qa_vec3 basis[3]; qa_collision_pose_basis(&query->target, true, basis);
            point = qa_collision_to_local(point, basis);
        }
    }
    qa_collision_bits contents = {0};
    if (model != 0) {
        const q3_model *members = &map->models[model];
        for (size_t i = 0; i < members->brush_count; ++i) contents = qa_collision_bits_union(contents, q3_brush_point(map, members->brushes[i], point));
    } else {
        int32_t index = qa_collision_tree_point(map->planes, map->nodes,
            map->node_count != 0 ? 0 : -1, point, true, true);
        const q3_leaf *leaf = &map->leaves[qa_collision_leaf_index(index)];
        for (uint32_t i = 0; i < leaf->brushes.count; ++i)
            contents = qa_collision_bits_union(contents, q3_brush_point(map, map->leaf_brushes[leaf->brushes.first + i], point));
    }
    *out = (qa_point_contents){.family = QA_GAME_Q3, .contents = contents,
        .stored = contents, .merged = contents};
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

static bool q3_load_model(const q3_map *map, const qa_bsp_model *source, q3_model *model,
                          q3_model_scratch *scratch, qa_error *error) {
    if (!source->membership_from_tree) {
        model->brush_count = source->brushes.count; model->surface_count = source->faces.count;
        model->brushes = q3_alloc(model->brush_count, sizeof(*model->brushes), error);
        model->surfaces = q3_alloc(model->surface_count, sizeof(*model->surfaces), error);
        if ((model->brush_count != 0 && model->brushes == NULL) || (model->surface_count != 0 && model->surfaces == NULL)) return false;
        for (size_t i = 0; i < model->brush_count; ++i) model->brushes[i] = source->brushes.first + (uint32_t)i;
        for (size_t i = 0; i < model->surface_count; ++i) model->surfaces[i] = source->faces.first + (uint32_t)i;
        return true;
    }
    qa_stamp_set_begin(&scratch->visited);
    size_t count = 0;
    scratch->pending[count++] = source->headnodes[0];
    while (count != 0) {
        int32_t index = scratch->pending[--count];
        if (index >= 0) {
            if (!qa_stamp_set_mark(&scratch->visited, map->brush_count + map->patch_count + (size_t)index)) continue;
            scratch->pending[count++] = map->nodes[index].children[1];
            scratch->pending[count++] = map->nodes[index].children[0];
        } else {
            const q3_leaf *leaf = &map->leaves[-1 - index];
            for (uint32_t i = 0; i < leaf->brushes.count; ++i)
                (void)qa_stamp_set_mark(&scratch->visited, map->leaf_brushes[leaf->brushes.first + i]);
            for (uint32_t i = 0; i < leaf->surfaces.count; ++i)
                (void)qa_stamp_set_mark(&scratch->visited, map->brush_count + map->leaf_surfaces[leaf->surfaces.first + i]);
        }
    }
    for (size_t i = 0; i < map->brush_count; ++i) if (qa_stamp_set_test(&scratch->visited, i)) ++model->brush_count;
    for (size_t i = 0; i < map->patch_count; ++i) if (qa_stamp_set_test(&scratch->visited, map->brush_count + i)) ++model->surface_count;
    model->brushes = q3_alloc(model->brush_count, sizeof(*model->brushes), error);
    model->surfaces = q3_alloc(model->surface_count, sizeof(*model->surfaces), error);
    if ((model->brush_count != 0 && model->brushes == NULL) || (model->surface_count != 0 && model->surfaces == NULL)) return false;
    size_t brush = 0, surface = 0;
    for (size_t i = 0; i < map->brush_count; ++i) if (qa_stamp_set_test(&scratch->visited, i)) model->brushes[brush++] = (uint32_t)i;
    for (size_t i = 0; i < map->patch_count; ++i) if (qa_stamp_set_test(&scratch->visited, map->brush_count + i)) model->surfaces[surface++] = (uint32_t)i;
    return true;
}

bool qa_q3_collision_create(const qa_bsp_view *bsp, const qa_collision_topology *topology, qa_collision_kernel *out, qa_error *error) {
    static const qa_collision_ops ops = {
        .destroy = q3_destroy, .trace = q3_trace, .point_contents = q3_point_contents,
        .create_scratch = q3_create_scratch, .destroy_scratch = q3_destroy_scratch};
    q3_model_scratch load = {0};
    q3_map *map = q3_alloc(1, sizeof(*map), error);
    if (map == NULL) return false;
    map->planes = topology->planes;
    map->nodes = topology->nodes;
    map->plane_count = topology->plane_count;
    map->side_count = qa_bsp_record_count(bsp, QA_BSP_BRUSH_SIDES);
    map->brush_count = qa_bsp_record_count(bsp, QA_BSP_BRUSHES);
    map->patch_count = qa_bsp_record_count(bsp, QA_BSP_SURFACES);
    map->node_count = topology->node_count;
    map->leaf_count = qa_bsp_record_count(bsp, QA_BSP_LEAVES);
    map->model_count = qa_bsp_record_count(bsp, QA_BSP_MODELS);
    if (map->leaf_count == 0 || map->model_count == 0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q3 collision map has no leaves or models"); goto fail;
    }
#define Q3_ALLOC(member, count) do { map->member = q3_alloc((count), sizeof(*map->member), error); if ((count) != 0 && map->member == NULL) goto fail; } while (0)
    Q3_ALLOC(sides, map->side_count);
    Q3_ALLOC(brushes, map->brush_count); Q3_ALLOC(patches, map->patch_count);
    Q3_ALLOC(leaves, map->leaf_count);
    Q3_ALLOC(models, map->model_count);
#undef Q3_ALLOC
    if (!q3_load_indices(bsp, QA_BSP_LEAF_BRUSHES, &map->leaf_brushes, error)
        || !q3_load_indices(bsp, QA_BSP_LEAF_FACES, &map->leaf_surfaces, error)) goto fail;
    for (size_t i = 0; i < map->side_count; ++i) {
        qa_bsp_brush_side side;
        if (!qa_bsp_read_brush_side(bsp, i, &side, error)) goto fail;
        int32_t flags = side.flags;
        if (bsp->format != QA_BSP_IBSP44) {
            qa_bsp_shader shader;
            if (!qa_bsp_read_shader(bsp, (size_t)side.shader, &shader, error)) goto fail;
            flags = shader.surface_flags;
        }
        map->sides[i] = (q3_side){.plane = side.plane,
            .flags = qa_collision_surface_decode(flags, QA_GAME_Q3)};
    }
    for (size_t i = 0; i < map->brush_count; ++i) {
        qa_bsp_brush source;
        if (!qa_bsp_read_brush(bsp, i, &source, error)) goto fail;
        q3_brush *brush = &map->brushes[i];
        brush->sides = source.sides;
        int32_t contents = source.contents;
        if (bsp->format != QA_BSP_IBSP44) {
            qa_bsp_shader shader;
            if (!qa_bsp_read_shader(bsp, (size_t)source.shader, &shader, error)) goto fail;
            contents = shader.content_flags;
        }
        brush->contents = qa_collision_contents_decode(contents, QA_GAME_Q3);
        for (uint32_t side = 0; side < source.sides.count; ++side)
            map->sides[source.sides.first + side].contents = brush->contents;
        brush->bounds = (qa_bounds){qa_v3(-FLT_MAX, -FLT_MAX, -FLT_MAX), qa_v3(FLT_MAX, FLT_MAX, FLT_MAX)};
        if (source.sides.count >= 6) for (unsigned axis = 0; axis < 3; ++axis) {
            uint32_t first = source.sides.first + axis * 2u;
            qa_vec_set_component(&brush->bounds.mins, axis, -map->planes[map->sides[first].plane].distance);
            qa_vec_set_component(&brush->bounds.maxs, axis, map->planes[map->sides[first + 1].plane].distance);
        }
    }
    for (size_t i = 0; i < map->leaf_count; ++i) {
        qa_bsp_leaf leaf;
        if (!qa_bsp_read_leaf(bsp, i, &leaf, error)) goto fail;
        map->leaves[i] = (q3_leaf){leaf.brushes, leaf.faces};
    }
    if (bsp->format == QA_BSP_IBSP44) {
        if (map->brush_count > SIZE_MAX - map->patch_count
            || map->brush_count + map->patch_count > SIZE_MAX - map->node_count
            || map->node_count == SIZE_MAX) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Q3 model membership scratch count overflow"); goto fail;
        }
        size_t count = map->brush_count + map->patch_count + map->node_count;
        uint32_t *marks = q3_alloc(count, sizeof(*marks), error);
        qa_stamp_set_init(&load.visited, marks, marks != NULL ? count : 0);
        load.pending = q3_alloc(map->node_count + 1, sizeof(*load.pending), error);
        if ((count != 0 && marks == NULL) || load.pending == NULL) goto fail;
    }
    for (size_t i = 0; i < map->model_count; ++i) {
        qa_bsp_model model;
        if (!qa_bsp_read_model(bsp, i, &model, error) || !q3_load_model(map, &model, &map->models[i], &load, error)) goto fail;
    }
    free(load.visited.marks); free(load.pending);
    load = (q3_model_scratch){0};
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
            patch->flags = qa_collision_surface_decode(shader.surface_flags, QA_GAME_Q3);
            patch->contents = qa_collision_contents_decode(shader.content_flags, QA_GAME_Q3);
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
    free(load.visited.marks); free(load.pending);
    q3_destroy(map);
    return false;
}
