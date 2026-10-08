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
    qa_error *error;
} mark_candidates;

static bool mark_fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}

static qa_vec3 mark_fast_normalize(qa_vec3 value)
{
    float square = qa_vec_dot(value, value), inverse;
    uint32_t bits;
    memcpy(&bits, &square, sizeof(bits));
    bits = UINT32_C(0x5f3759df) - (bits >> 1);
    memcpy(&inverse, &bits, sizeof(inverse));
    float product = (square * 0.5f) * inverse;
    product *= inverse;
    inverse *= 1.5f - product;
    return qa_vec_scale(value, inverse);
}

static unsigned mark_box_side(qa_bounds bounds, qa_scene_plane plane)
{
    qa_vec3 n = plane.normal;
    int axis = n.x == 1 ? 0 : n.y == 1 ? 1 : n.z == 1 ? 2 : -1;
    if (axis >= 0) {
        float min = axis == 0 ? bounds.mins.x : axis == 1 ? bounds.mins.y : bounds.mins.z;
        float max = axis == 0 ? bounds.maxs.x : axis == 1 ? bounds.maxs.y : bounds.maxs.z;
        return plane.distance <= min ? 1u : plane.distance >= max ? 2u : 3u;
    }
    qa_vec3 front = qa_v3(n.x < 0 ? bounds.mins.x : bounds.maxs.x,
        n.y < 0 ? bounds.mins.y : bounds.maxs.y, n.z < 0 ? bounds.mins.z : bounds.maxs.z);
    qa_vec3 back = qa_v3(n.x < 0 ? bounds.maxs.x : bounds.mins.x,
        n.y < 0 ? bounds.maxs.y : bounds.mins.y, n.z < 0 ? bounds.maxs.z : bounds.mins.z);
    return (qa_vec_dot(front, n) - plane.distance >= 0 ? 1u : 0u) |
        (qa_vec_dot(back, n) - plane.distance < 0 ? 2u : 0u);
}

/* IBSP44's interned material table is temporary at world preparation. Read
 * its original flag derivation from the retained immutable BSP instead. The
 * last brush containing a side supplies that side's contents, as in the
 * actual qa_bsp_build_materials producer. */
static bool mark_q3_flags(const qa_scene_world *world, uint32_t index,
    uint32_t *flags, uint32_t *contents, qa_error *error)
{
    qa_bsp_surface source;
    if (!qa_bsp_read_surface(&world->bsp, index, &source, error)) return false;
    *flags = *contents = 0;
    if (world->bsp.format != QA_BSP_IBSP44) {
        qa_bsp_shader shader;
        if (!qa_bsp_read_shader(&world->bsp, (size_t)source.shader, &shader, error)) return false;
        *flags = (uint32_t)shader.surface_flags;
        *contents = (uint32_t)shader.content_flags;
    } else if (source.brush_side >= 0) {
        qa_bsp_brush_side side;
        if (!qa_bsp_read_brush_side(&world->bsp, (size_t)source.brush_side, &side, error)) return false;
        *flags = (uint32_t)side.flags;
        size_t count = qa_bsp_record_count(&world->bsp, QA_BSP_BRUSHES);
        for (size_t i = 0; i < count; ++i) {
            qa_bsp_brush brush;
            if (!qa_bsp_read_brush(&world->bsp, i, &brush, error)) return false;
            uint32_t member = (uint32_t)source.brush_side;
            if (member >= brush.sides.first && member - brush.sides.first < brush.sides.count)
                *contents = (uint32_t)brush.contents;
        }
    }
    return true;
}

static bool mark_eligible(const qa_scene_world *world, uint32_t index,
    bool *eligible, qa_error *error)
{
    const qaw_surface *surface = &world->surfaces[index];
    *eligible = false;
    if (world->bsp.family == QA_BSP_Q3) {
        if (!surface->patch && (surface->type != QA_BSP_SURFACE_PLANAR || !surface->has_plane)) return true;
        uint32_t flags, contents;
        if (!mark_q3_flags(world, index, &flags, &contents, error)) return false;
        *eligible = (flags & 0x30u) == 0 && (contents & 64u) == 0;
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

static bool mark_box_surfaces(mark_candidates *marks, int32_t child)
{
    const qa_scene_world *world = marks->world;
    while (child >= 0) {
        if (marks->count == MARK_SURFACES) return true;
        const qa_bsp_node *node = &world->nodes[child];
        const qa_bsp_plane *plane = &world->planes[node->plane];
        unsigned side = mark_box_side(marks->bounds, (qa_scene_plane){plane->normal, plane->distance});
        if (side == 1) child = node->children[0];
        else if (side == 2) child = node->children[1];
        else {
            if (!mark_box_surfaces(marks, node->children[0])) return false;
            child = node->children[1];
        }
    }
    const qa_bsp_leaf *leaf = &world->leaves[(size_t)(-1 - (int64_t)child)];
    for (size_t i = 0; i < leaf->faces.count && marks->count < MARK_SURFACES; ++i) {
        uint32_t index = world->leaf_surfaces[(size_t)leaf->faces.first + i];
        size_t seen = 0;
        while (seen < marks->count && marks->surfaces[seen] != index) ++seen;
        if (seen < marks->count) continue;
        bool eligible;
        if (!mark_eligible(world, index, &eligible, marks->error)) return false;
        if (!eligible) continue;
        const qaw_surface *surface = &world->surfaces[index];
        if (!surface->patch && (mark_box_side(marks->bounds, surface->plane) != 3 ||
            qa_vec_dot(surface->plane.normal, marks->direction) > -0.5f)) continue;
        marks->surfaces[marks->count++] = index;
    }
    return true;
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
        .bounds = {qa_v3(99999, 99999, 99999), qa_v3(-99999, -99999, -99999)}, .error = error};
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
        qa_vec3 normal = mark_fast_normalize(qa_vec_cross(edge, reverse));
        planes[i] = (qa_scene_plane){normal, qa_vec_dot(normal, points[i])};
    }
    planes[count] = (qa_scene_plane){marks.direction, qa_vec_dot(marks.direction, points[0]) - 32};
    qa_vec3 inverse = qa_vec_scale(marks.direction, -1);
    planes[count + 1] = (qa_scene_plane){inverse, qa_vec_dot(inverse, points[0]) - 20};
    if (!mark_box_surfaces(&marks, world->node_count ? 0 : -1)) return false;
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
                    qa_vec3 normal = mark_fast_normalize(qa_vec_cross(
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
