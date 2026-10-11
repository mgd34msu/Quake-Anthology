/* World mark projection from id Software's renderer/tr_marks.c.
 * Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/scene_marks.h"
#include "legacy/internal.h"
#include "q3/patch.h"

#include <string.h>

enum { MARK_CLIP_POINTS = 64, MARK_SURFACES = 64 };

typedef struct mark_candidates {
    const qa_scene_world *world;
    qa_bounds bounds;
    qa_vec3 direction;
    uint32_t surfaces[MARK_SURFACES];
    size_t count;
} mark_candidates;

static bool mark_fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}

static bool mark_eligible(const qa_scene_world *world, uint32_t index,
    bool *eligible, qa_error *error)
{
    const qaw_surface *surface = &world->surfaces[index];
    *eligible = false;
    if (world->bsp.family == QA_BSP_Q3) {
        if (!surface->patch && (surface->type != QA_BSP_SURFACE_PLANAR || !surface->has_plane)) return true;
        *eligible = (surface->source_flags & 0x30u) == 0 && (surface->source_contents & 64u) == 0;
        return true;
    }
    if (!surface->has_plane || !surface->legacy) return true;
    if (world->bsp.family == QA_BSP_Q1) {
        const qawl_world *data = world->legacy_data;
        const char *name = data->textures[surface->legacy->texture].name;
        *eligible = name[0] != '*' && strncmp(name, "sky", 3) != 0;
    } else {
        qa_bsp_face face;
        qa_bsp_texinfo info;
        if (!qa_bsp_read_face(&world->bsp, index, &face, error) ||
            !qa_bsp_read_texinfo(&world->bsp, face.texinfo, &info, error)) return false;
        *eligible = ((uint32_t)info.flags & (4u | 8u | 128u)) == 0;
    }
    return true;
}

static qa_leaf_visit mark_leaf_surfaces(void *context, const qa_collision_leaf *source,
                                        qa_error *error)
{
    mark_candidates *marks = context;
    const qa_scene_world *world = marks->world;
    const qa_bsp_leaf *leaf = &world->leaves[source->leaf];
    for (size_t i = 0; i < leaf->faces.count && marks->count < MARK_SURFACES; ++i) {
        uint32_t index = world->leaf_surfaces[(size_t)leaf->faces.first + i];
        size_t seen = 0;
        while (seen < marks->count && marks->surfaces[seen] != index) ++seen;
        if (seen < marks->count) continue;
        bool eligible;
        if (!mark_eligible(world, index, &eligible, error)) return QA_LEAF_FAILED;
        if (!eligible) continue;
        const qaw_surface *surface = &world->surfaces[index];
        if (!surface->patch) {
            qa_vec3 normal = surface->plane.normal;
            qa_collision_plane plane = {.normal = normal, .distance = surface->plane.distance,
                .type = normal.x == 1 ? 0 : normal.y == 1 ? 1 : normal.z == 1 ? 2 : 3};
            if (qa_collision_box_plane_side(marks->bounds, &plane, QA_BOX_AXIAL) != 3 ||
                qa_vec_dot(normal, marks->direction) > -0.5f) continue;
        }
        marks->surfaces[marks->count++] = index;
    }
    return marks->count == MARK_SURFACES ? QA_LEAF_STOP : QA_LEAF_CONTINUE;
}

/* R_ChopPolyBehindPlane keeps the front side, drops an all-on polygon, and
 * rejects 62 or more input vertices before attempting a split. */
static size_t mark_chop(const qa_vec3 *input, size_t count,
    qa_vec3 *output, qa_scene_plane plane)
{
    if (count >= MARK_CLIP_POINTS - 2) return 0;
    float distances[MARK_CLIP_POINTS];
    unsigned sides[MARK_CLIP_POINTS];
    bool front = false, back = false;
    for (size_t i = 0; i < count; ++i) {
        distances[i] = qa_vec_dot(input[i], plane.normal) - plane.distance;
        sides[i] = distances[i] > 0.5f ? 0u : distances[i] < -0.5f ? 1u : 2u;
        front |= sides[i] == 0;
        back |= sides[i] == 1;
    }
    if (!front) return 0;
    if (!back) { memcpy(output, input, count * sizeof(*output)); return count; }
    size_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t next = (i + 1) % count;
        qa_vec3 point = input[i];
        if (sides[i] == 2) { output[written++] = point; continue; }
        if (sides[i] == 0) output[written++] = point;
        if (sides[next] == 2 || sides[next] == sides[i]) continue;
        float difference = distances[i] - distances[next];
        float fraction = difference == 0 ? 0 : distances[i] / difference;
        output[written++] = qa_vec_add(point, qa_vec_scale(qa_vec_sub(input[next], point), fraction));
    }
    return written;
}

static void mark_append(const qa_vec3 triangle[3], const qa_scene_plane *planes,
    size_t plane_count, qa_vec3 *point_buffer, size_t max_points,
    qa_scene_mark_fragment *fragment_buffer, qa_scene_mark_result *result)
{
    qa_vec3 points[2][MARK_CLIP_POINTS];
    memcpy(points[0], triangle, 3 * sizeof(*triangle));
    size_t count = 3;
    unsigned current = 0;
    for (size_t i = 0; i < plane_count; ++i) {
        count = mark_chop(points[current], count, points[current ^ 1], planes[i]);
        current ^= 1;
        if (!count) return;
    }
    if (count > max_points - result->point_count) return;
    fragment_buffer[result->fragment_count++] = (qa_scene_mark_fragment){result->point_count, count};
    memcpy(point_buffer + result->point_count, points[current], count * sizeof(*point_buffer));
    result->point_count += count;
}

static void mark_bounds_point(qa_bounds *bounds, qa_vec3 point)
{
    bounds->mins = qa_v3(fminf(bounds->mins.x, point.x), fminf(bounds->mins.y, point.y), fminf(bounds->mins.z, point.z));
    bounds->maxs = qa_v3(fmaxf(bounds->maxs.x, point.x), fmaxf(bounds->maxs.y, point.y), fmaxf(bounds->maxs.z, point.z));
}

bool qa_scene_world_mark_fragments(const qa_scene_world *world,
    const qa_vec3 *points, size_t point_count, qa_vec3 projection,
    qa_vec3 *point_buffer, size_t max_points,
    qa_scene_mark_fragment *fragment_buffer, size_t max_fragments,
    qa_scene_mark_result *out, qa_error *error)
{
    if (!world || !points || !point_count || !out || !qa_vec_finite(projection) ||
        (max_points && !point_buffer) || (max_fragments && !fragment_buffer) ||
        point_count > SIZE_MAX / sizeof(*points) || max_points > SIZE_MAX / sizeof(*point_buffer) ||
        max_fragments > SIZE_MAX / sizeof(*fragment_buffer) || world->transaction_depth ||
        world->admission_change_count || world->checkpoint_active)
        return mark_fail(error, "Mark projection requires the actual prepared world and valid borrowed spans");
    mark_candidates marks = {.world = world, .direction = qa_vec_normalize(projection),
        .bounds = {qa_v3(99999, 99999, 99999), qa_v3(-99999, -99999, -99999)}};
    for (size_t i = 0; i < point_count; ++i) {
        if (!qa_vec_finite(points[i])) return mark_fail(error, "Mark projection input point is nonfinite");
        mark_bounds_point(&marks.bounds, points[i]);
        mark_bounds_point(&marks.bounds, qa_vec_add(points[i], projection));
        mark_bounds_point(&marks.bounds, qa_vec_add(points[i], qa_vec_scale(marks.direction, -20)));
    }
    qa_scene_mark_result result = {0};
    if (!max_points || !max_fragments || !world->leaf_count) { *out = result; return true; }
    size_t count = point_count < MARK_CLIP_POINTS ? point_count : MARK_CLIP_POINTS;
    qa_scene_plane planes[MARK_CLIP_POINTS + 2];
    for (size_t i = 0; i < count; ++i) {
        qa_vec3 edge = qa_vec_sub(points[(i + 1) % count], points[i]);
        qa_vec3 reverse = qa_vec_sub(points[i], qa_vec_add(points[i], projection));
        qa_vec3 normal = qa_vec_fast_normalize(qa_vec_cross(edge, reverse));
        planes[i] = (qa_scene_plane){normal, qa_vec_dot(normal, points[i])};
    }
    planes[count] = (qa_scene_plane){marks.direction, qa_vec_dot(marks.direction, points[0]) - 32};
    qa_vec3 inverse = qa_vec_scale(marks.direction, -1);
    planes[count + 1] = (qa_scene_plane){inverse, qa_vec_dot(inverse, points[0]) - 20};
    qa_leaf_list leaves;
    if (!qa_collision_walk_leaves(world->options.geometry, world->spatial_scratch, marks.bounds,
        QA_LEAF_RENDER, mark_leaf_surfaces, &marks, &leaves, error)) return false;
    for (size_t i = 0; i < marks.count; ++i) {
        const qaw_surface *surface = &world->surfaces[marks.surfaces[i]];
        const qa_scene_vertex *vertices = surface->mesh.vertices;
        if (surface->patch) {
            unsigned width = surface->patch->width, height = surface->patch->height;
            for (unsigned row = 0; row + 1 < height; ++row) for (unsigned column = 0; column + 1 < width; ++column) {
                size_t base = (size_t)row * width + column;
                size_t indices[2][3] = {{base, base + width, base + 1},
                    {base + 1, base + width, base + width + 1}};
                for (unsigned t = 0; t < 2; ++t) {
                    qa_vec3 triangle[3];
                    for (unsigned j = 0; j < 3; ++j) {
                        const qa_scene_vertex *vertex = &vertices[indices[t][j]];
                        triangle[j] = qa_vec_add(vertex->position, qa_vec_scale(vertex->normal, 0));
                    }
                    qa_vec3 normal = qa_vec_fast_normalize(qa_vec_cross(
                        qa_vec_sub(triangle[0], triangle[1]), qa_vec_sub(triangle[2], triangle[1])));
                    if (qa_vec_dot(normal, marks.direction) >= (t == 0 ? -0.1f : -0.05f)) continue;
                    mark_append(triangle, planes, count + 2, point_buffer, max_points, fragment_buffer, &result);
                    if (result.fragment_count == max_fragments) { *out = result; return true; }
                }
            }
        } else {
            if (qa_vec_dot(surface->plane.normal, marks.direction) > -0.5f) continue;
            for (size_t k = 0; k < surface->mesh.index_count; k += 3) {
                qa_vec3 triangle[3];
                for (unsigned j = 0; j < 3; ++j)
                    triangle[j] = qa_vec_add(vertices[surface->mesh.indices[k + j]].position,
                        qa_vec_scale(surface->plane.normal, 0));
                mark_append(triangle, planes, count + 2, point_buffer, max_points, fragment_buffer, &result);
                if (result.fragment_count == max_fragments) { *out = result; return true; }
            }
        }
    }
    *out = result;
    return true;
}
