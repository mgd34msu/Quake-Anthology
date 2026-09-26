/* Scene effects adapted from id Software Quake renderers.
 * Copyright (C) 1996-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "effects/internal.h"
#include "qa/scene_effects.h"

#include <float.h>
#include <string.h>

bool qa_effect_mesh(qa_scene_frame *frame, size_t vertex_count, size_t index_count,
                    qa_scene_mesh *mesh, qa_scene_vertex **vertices,
                    uint32_t **indices, qa_error *error)
{
    if (vertex_count > UINT32_MAX || vertex_count > SIZE_MAX / sizeof(**vertices) ||
        index_count > SIZE_MAX / sizeof(**indices)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Effect geometry size overflow");
        return false;
    }
    memset(mesh, 0, sizeof(*mesh));
    *vertices = NULL;
    *indices = NULL;
    if (vertex_count != 0) {
        *vertices = qa_arena_alloc(&frame->storage, vertex_count * sizeof(**vertices),
                                  _Alignof(qa_scene_vertex), error);
        if (!*vertices) return false;
        memset(*vertices, 0, vertex_count * sizeof(**vertices));
    }
    if (index_count != 0) {
        *indices = qa_arena_alloc(&frame->storage, index_count * sizeof(**indices),
                                 _Alignof(uint32_t), error);
        if (!*indices) return false;
    }
    mesh->vertices = *vertices;
    mesh->indices = *indices;
    mesh->vertex_count = vertex_count;
    mesh->index_count = index_count;
    mesh->primitive = QA_SCENE_TRIANGLES;
    return true;
}

void qa_effect_bounds(qa_scene_mesh *mesh)
{
    if (mesh->vertex_count == 0) {
        mesh->bounds = (qa_bounds){qa_v3(0, 0, 0), qa_v3(0, 0, 0)};
        return;
    }
    mesh->bounds.mins = mesh->bounds.maxs = mesh->vertices[0].position;
    for (size_t i = 1; i < mesh->vertex_count; ++i) {
        qa_vec3 point = mesh->vertices[i].position;
        mesh->bounds = qa_bounds_union(mesh->bounds, (qa_bounds){point, point});
    }
}

void qa_effect_draw(qa_scene_draw *draw, const qa_scene_view *view,
                    const qa_scene_mesh *mesh, const qa_scene_image *image,
                    bool additive)
{
    memset(draw, 0, sizeof(*draw));
    draw->mesh = *mesh;
    qa_scene_matrix_identity(&draw->model);
    draw->mvp = qa_scene_matrix_multiply(view->projection, qa_scene_view_matrix(view));
    draw->textures[0] = image;
    draw->texture_count = image ? 1 : 0;
    draw->lighting = QA_LIGHT_VERTEX;
    qa_scene_state_default(&draw->state);
    draw->state.blend_source = QA_BLEND_SRC_ALPHA;
    draw->state.blend_destination = additive ? QA_BLEND_ONE : QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw->state.depth_write = false;
    draw->state.cull = QA_CULL_NONE;
}

static qa_vec3 portal_transform(qa_vec3 vector, const qa_vec3 surface[3], const qa_vec3 camera[3])
{
    qa_vec3 result = qa_v3(0, 0, 0);
    for (size_t i = 0; i < 3; ++i)
        result = qa_vec_add(result, qa_vec_scale(camera[i], qa_vec_dot(vector, surface[i])));
    return result;
}

bool qa_scene_portal_view(const qa_scene_view *view, qa_scene_plane plane,
                          const qa_scene_portal *portal, double seconds,
                          qa_scene_view *result, qa_vec3 *pvs_origin)
{
    if (!view || !portal || !result || !pvs_origin || !isfinite(seconds)) return false;
    float distance = qa_vec_dot(portal->origin, plane.normal) - plane.distance;
    if (fabsf(distance) > 64.0f) return false;
    qa_vec3 surface[3] = {plane.normal, qa_effect_perpendicular(plane.normal), {0, 0, 0}};
    surface[2] = qa_vec_cross(surface[0], surface[1]);
    bool mirror = portal->mirror ||
        (portal->origin.x == portal->old_origin.x && portal->origin.y == portal->old_origin.y &&
         portal->origin.z == portal->old_origin.z);
    qa_vec3 camera[3], surface_origin, camera_origin;
    if (mirror) {
        surface_origin = qa_vec_scale(plane.normal, plane.distance);
        camera_origin = surface_origin;
        camera[0] = qa_vec_scale(plane.normal, -1);
        camera[1] = surface[1];
        camera[2] = surface[2];
    } else {
        surface_origin = qa_vec_sub(portal->origin, qa_vec_scale(plane.normal, distance));
        camera_origin = portal->old_origin;
        camera[0] = qa_vec_scale(portal->axis[0], -1);
        camera[1] = qa_vec_scale(portal->axis[1], -1);
        camera[2] = portal->axis[2];
        float angle = portal->rotation_speed != 0 ? (float)seconds * portal->rotation_speed :
            portal->rotation_offset;
        if (portal->oscillate)
            angle = portal->rotation_offset + (float)sin((float)((float)(seconds * 1000.0) * 0.003f)) * 4;
        if (angle != 0 || portal->oscillate || portal->rotation_speed != 0) {
            camera[1] = qa_effect_rotate(camera[1], camera[0], angle);
            camera[2] = qa_vec_cross(camera[0], camera[1]);
        }
    }
    *result = *view;
    result->origin = qa_vec_add(portal_transform(qa_vec_sub(view->origin, surface_origin), surface, camera), camera_origin);
    for (size_t i = 0; i < 3; ++i) result->axis[i] = portal_transform(view->axis[i], surface, camera);
    result->clip_enabled = true;
    result->mirror = mirror;
    result->clip_plane.normal = qa_vec_scale(camera[0], -1);
    result->clip_plane.distance = qa_vec_dot(camera_origin, result->clip_plane.normal);
    *pvs_origin = portal->old_origin;
    return true;
}

bool qa_scene_portal_surface_visible(const qa_scene_mesh *mesh, const qa_scene_view *view,
                                     float range, bool mirror)
{
    qa_scene_matrix matrix = qa_scene_matrix_multiply(view->projection, qa_scene_view_matrix(view));
    unsigned common = 63;
    for (size_t i = 0; i < mesh->vertex_count; ++i) {
        qa_scene_vec4 clip = qa_scene_matrix_point(matrix, mesh->vertices[i].position);
        float components[3] = {clip.x, clip.y, clip.z};
        unsigned flags = 0;
        for (unsigned j = 0; j < 3; ++j) {
            if (components[j] >= clip.w) flags |= 1u << (j * 2);
            else if (components[j] <= -clip.w) flags |= 1u << (j * 2 + 1);
        }
        common &= flags;
    }
    if (common) return false;
    size_t front = 0;
    float shortest = 100000000.0f;
    for (size_t i = 0; i + 2 < mesh->index_count; i += 3) {
        const qa_scene_vertex *vertex = &mesh->vertices[mesh->indices[i]];
        qa_vec3 relative = qa_vec_sub(vertex->position, view->origin);
        shortest = fminf(shortest, qa_vec_dot(relative, relative));
        if (qa_vec_dot(relative, vertex->normal) < 0) ++front;
    }
    return front != 0 && (mirror || shortest <= range * range);
}

bool qa_scene_particle(qa_scene_frame *frame, const qa_scene_view *view, qa_vec3 origin,
                       float radius, float rotation, qa_scene_vec4 color,
                       const qa_scene_image *image, bool additive, qa_error *error)
{
    if (!frame || !view || !qa_vec_finite(origin) || !isfinite(radius) || !isfinite(rotation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid particle parameters");
        return false;
    }
    qa_vec3 left, up;
    if (rotation == 0) {
        left = qa_vec_scale(view->axis[1], radius);
        up = qa_vec_scale(view->axis[2], radius);
    } else {
        float angle = (float)(QA_EFFECT_PI * rotation / 180.0);
        float sine = (float)sin(angle), cosine = (float)cos(angle);
        left = qa_vec_add(qa_vec_scale(view->axis[1], cosine * radius), qa_vec_scale(view->axis[2], -sine * radius));
        up = qa_vec_add(qa_vec_scale(view->axis[2], cosine * radius), qa_vec_scale(view->axis[1], sine * radius));
    }
    if (view->mirror) left = qa_vec_scale(left, -1);
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, 4, 6, &mesh, &vertices, &indices, error)) return false;
    const float horizontal[4] = {1, -1, -1, 1}, vertical[4] = {1, 1, -1, -1};
    const qa_scene_vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (size_t i = 0; i < 4; ++i) {
        vertices[i].position = qa_vec_add(qa_vec_add(origin, qa_vec_scale(left, horizontal[i])), qa_vec_scale(up, vertical[i]));
        vertices[i].normal = qa_vec_scale(view->axis[0], -1);
        vertices[i].texcoord = vertices[i].lightmap = uv[i];
        vertices[i].color = color;
    }
    const uint32_t order[6] = {0, 1, 3, 3, 1, 2};
    memcpy(indices, order, sizeof(order));
    qa_effect_bounds(&mesh);
    qa_scene_draw draw;
    qa_effect_draw(&draw, view, &mesh, image, additive);
    return qa_scene_frame_draw(frame, &draw, error);
}

bool qa_scene_indexed_particle(qa_scene_frame *frame, const qa_scene_view *view,
                               qa_scene_family family, qa_vec3 origin, float size,
                               qa_scene_vec4 color, const qa_scene_image *image, qa_error *error)
{
    if (!frame || !view || (family != QA_SCENE_Q1 && family != QA_SCENE_Q2) ||
        !qa_vec_finite(origin) || !isfinite(size) || !isfinite(color.w)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid indexed particle parameters");
        return false;
    }
    float depth = qa_vec_dot(qa_vec_sub(origin, view->origin), view->axis[0]);
    float scale = (depth < 20 ? 1 : 1 + depth * 0.004f) * size;
    float uv = family == QA_SCENE_Q2 ? 0.0625f : 0;
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, 3, 3, &mesh, &vertices, &indices, error)) return false;
    if (family == QA_SCENE_Q1) color.w = 1;
    else color.w = (float)((uint32_t)(fmod(trunc((double)color.w * 255.0), 256.0) + 256.0) & 255u) / 255.0f;
    vertices[0].position = origin;
    vertices[1].position = qa_vec_add(origin, qa_vec_scale(view->axis[2], 1.5f * scale));
    vertices[2].position = qa_vec_add(origin, qa_vec_scale(view->axis[1], -1.5f * scale));
    for (size_t i = 0; i < 3; ++i) {
        vertices[i].normal = qa_vec_scale(view->axis[0], -1);
        vertices[i].texcoord = (qa_scene_vec2){uv + (i == 1 ? 1 : 0), uv + (i == 2 ? 1 : 0)};
        vertices[i].color = color;
        indices[i] = (uint32_t)i;
    }
    qa_effect_bounds(&mesh);
    qa_scene_draw draw;
    qa_effect_draw(&draw, view, &mesh, image, false);
    draw.state.depth_write = family == QA_SCENE_Q1;
    return qa_scene_frame_draw(frame, &draw, error);
}

bool qa_scene_beam(qa_scene_frame *frame, const qa_scene_view *view, qa_vec3 start,
                   qa_vec3 end, float width, qa_scene_vec4 color,
                   const qa_scene_image *image, qa_error *error)
{
    if (!frame || !view || !qa_vec_finite(start) || !qa_vec_finite(end) || !isfinite(width)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid beam parameters");
        return false;
    }
    qa_vec3 delta = qa_vec_sub(end, start);
    if (qa_vec_length(delta) == 0) return true;
    qa_vec3 direction = qa_vec_normalize(delta);
    qa_vec3 perpendicular = qa_vec_scale(qa_effect_perpendicular(direction), width * 0.5f);
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, 12, 36, &mesh, &vertices, &indices, error)) return false;
    for (uint32_t i = 0; i < 6; ++i) {
        qa_vec3 offset = qa_effect_rotate(perpendicular, direction, (float)i * 60);
        vertices[i * 2].position = qa_vec_add(start, offset);
        vertices[i * 2 + 1].position = qa_vec_add(vertices[i * 2].position, delta);
        vertices[i * 2].normal = vertices[i * 2 + 1].normal = qa_vec_normalize(offset);
        vertices[i * 2].color = vertices[i * 2 + 1].color = color;
        uint32_t a = i * 2, b = (i + 1) % 6 * 2;
        const uint32_t quad[6] = {a, a + 1, b, b, a + 1, b + 1};
        memcpy(indices + i * 6, quad, sizeof(quad));
    }
    qa_effect_bounds(&mesh);
    qa_scene_draw draw;
    qa_effect_draw(&draw, view, &mesh, image, false);
    return qa_scene_frame_draw(frame, &draw, error);
}
