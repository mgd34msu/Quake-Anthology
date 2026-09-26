#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct polygon_mesh {
    qa_scene_vertex *vertices;
    uint32_t *indices;
    size_t vertex_count, index_count;
    const qa_bsp_texinfo *info;
    const qawl_texture *texture;
    const qaw_surface *surface;
} polygon_mesh;

static float component(qa_vec3 p, unsigned axis)
{
    return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}

static float project(qa_vec3 p, const float vector[4])
{
    return p.x * vector[0] + p.y * vector[1] + p.z * vector[2] + vector[3];
}

static bool append_polygon(polygon_mesh *mesh, const qa_vec3 *points, size_t count, qa_error *error)
{
    if (count < 3 || count > UINT32_MAX - mesh->vertex_count ||
        count > SIZE_MAX / sizeof(*mesh->vertices) - mesh->vertex_count ||
        count - 2 > (SIZE_MAX / sizeof(*mesh->indices) - mesh->index_count) / 3) {
        qa_error_set(error, QA_ERROR_FORMAT, count, "Invalid subdivided brush polygon size");
        return false;
    }
    size_t vertices = mesh->vertex_count + count, indices = mesh->index_count + (count - 2) * 3;
    qa_scene_vertex *new_vertices = realloc(mesh->vertices, vertices * sizeof(*new_vertices));
    if (!new_vertices) {
        qa_error_set(error, QA_ERROR_MEMORY, count, "Cannot allocate brush vertices");
        return false;
    }
    mesh->vertices = new_vertices;
    uint32_t *new_indices = realloc(mesh->indices, indices * sizeof(*new_indices));
    if (!new_indices) {
        qa_error_set(error, QA_ERROR_MEMORY, count, "Cannot allocate brush indices");
        return false;
    }
    mesh->indices = new_indices;
    const qaw_legacy *legacy = mesh->surface->legacy;
    float scale = mesh->texture->quake64_shift > 0 && !legacy->warp && !mesh->surface->sky
        ? 2.0f * (float)mesh->texture->quake64_shift : 1.0f;
    for (size_t i = 0; i < count; ++i) {
        qa_scene_vec2 uv = {project(points[i], mesh->info->projection[0]),
                           project(points[i], mesh->info->projection[1])};
        if (!legacy->warp) {
            uv.x /= (float)mesh->texture->width * scale;
            uv.y /= (float)mesh->texture->height * scale;
        }
        mesh->vertices[mesh->vertex_count + i] = (qa_scene_vertex){
            .position = points[i], .normal = mesh->surface->plane.normal, .texcoord = uv,
            .lightmap = {(project(points[i], legacy->projection[0]) + 0.5f) / (float)legacy->width,
                         (project(points[i], legacy->projection[1]) + 0.5f) / (float)legacy->height},
            .color = {1, 1, 1, 1}
        };
        qa_scene_vec2 lightmap = mesh->vertices[mesh->vertex_count + i].lightmap;
        if (!isfinite(uv.x) || !isfinite(uv.y) || !isfinite(lightmap.x) || !isfinite(lightmap.y)) {
            qa_error_set(error, QA_ERROR_FORMAT, mesh->surface->source_index, "Brush texture projection overflows");
            return false;
        }
    }
    for (size_t i = 2; i < count; ++i) {
        mesh->indices[mesh->index_count++] = (uint32_t)mesh->vertex_count;
        mesh->indices[mesh->index_count++] = (uint32_t)(mesh->vertex_count + i - 1);
        mesh->indices[mesh->index_count++] = (uint32_t)(mesh->vertex_count + i);
    }
    mesh->vertex_count = vertices;
    return true;
}

static bool subdivide(polygon_mesh *mesh, const qa_vec3 *points, size_t count,
                      float size, unsigned depth, qa_error *error)
{
    if (depth > 128 || count < 3 || count > SIZE_MAX / sizeof(qa_vec3) - 2) {
        qa_error_set(error, QA_ERROR_FORMAT, count, "Invalid water subdivision geometry");
        return false;
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        float min = component(points[0], axis), max = min;
        for (size_t i = 1; i < count; ++i) {
            min = fminf(min, component(points[i], axis));
            max = fmaxf(max, component(points[i], axis));
        }
        float middle = size * floorf((min * 0.5f + max * 0.5f) / size + 0.5f);
        if (max - middle < 8 || middle - min < 8) continue;
        qa_vec3 *front = malloc((count + 2) * sizeof(*front));
        qa_vec3 *back = malloc((count + 2) * sizeof(*back));
        if (!front || !back) {
            free(front); free(back);
            qa_error_set(error, QA_ERROR_MEMORY, count, "Cannot allocate water subdivision");
            return false;
        }
        size_t nf = 0, nb = 0;
        bool valid = true;
        for (size_t i = 0; i < count && valid; ++i) {
            qa_vec3 p = points[i], next = points[(i + 1) % count];
            float distance = component(p, axis) - middle;
            float next_distance = component(next, axis) - middle;
            if (distance >= 0) { if (nf == count + 2) { valid = false; break; } front[nf++] = p; }
            if (distance <= 0) { if (nb == count + 2) { valid = false; break; } back[nb++] = p; }
            if (distance == 0 || next_distance == 0 || (distance > 0) == (next_distance > 0)) continue;
            if (nf == count + 2 || nb == count + 2) { valid = false; break; }
            front[nf++] = back[nb++] = qa_vec_lerp(p, next, distance / (distance - next_distance));
        }
        if (!valid) qa_error_set(error, QA_ERROR_FORMAT, count, "Nonconvex water polygon");
        bool result = valid && subdivide(mesh, front, nf, size, depth + 1, error) &&
                      subdivide(mesh, back, nb, size, depth + 1, error);
        free(front); free(back);
        return result;
    }
    return append_polygon(mesh, points, count, error);
}

bool qawl_geometry_build(qa_scene_world *world, qaw_surface *surface, const qa_bsp_face *face,
                         const qa_bsp_texinfo *info, qa_error *error)
{
    size_t count = face->edges.count;
    if (count < 3 || count > SIZE_MAX / sizeof(qa_vec3)) {
        qa_error_set(error, QA_ERROR_FORMAT, surface->source_index, "Brush face needs at least three edges");
        return false;
    }
    qa_vec3 *points = malloc(count * sizeof(*points));
    if (!points) {
        qa_error_set(error, QA_ERROR_MEMORY, count, "Cannot allocate brush face polygon");
        return false;
    }
    bool result = false;
    for (size_t i = 0; i < count; ++i) {
        int64_t signed_edge;
        qa_bsp_edge edge;
        qa_bsp_vertex vertex;
        if (!qa_bsp_read_index(&world->bsp, QA_BSP_SURFEDGES, (size_t)face->edges.first + i, &signed_edge, error) ||
            !qa_bsp_read_edge(&world->bsp, (size_t)(signed_edge < 0 ? -signed_edge : signed_edge), &edge, error) ||
            !qa_bsp_read_vertex(&world->bsp, edge.vertices[signed_edge < 0 ? 1 : 0], &vertex, error)) goto done;
        points[i] = vertex.position;
    }
    if (!qawl_light_setup(world, surface, face, info, points, count, error)) goto done;
    qawl_world *data = world->legacy_data;
    polygon_mesh mesh = {.info = info, .texture = &data->textures[surface->legacy->texture], .surface = surface};
    bool built = surface->legacy->warp
        ? subdivide(&mesh, points, count, world->bsp.family == QA_BSP_Q1 ? 128 : 64, 0, error)
        : append_polygon(&mesh, points, count, error);
    if (built && qaw_mesh_allocate(world, surface, mesh.vertex_count, mesh.index_count, error)) {
        memcpy(surface->vertices, mesh.vertices, mesh.vertex_count * sizeof(*mesh.vertices));
        memcpy(surface->indices, mesh.indices, mesh.index_count * sizeof(*mesh.indices));
        qaw_mesh_bounds(surface);
        result = true;
    }
    free(mesh.vertices); free(mesh.indices);
done:
    free(points);
    return result;
}
