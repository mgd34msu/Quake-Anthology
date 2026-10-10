#include "internal.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

static const qa_scene_vertex *point(const qa_scene_mesh *mesh, const uint32_t *indices, size_t i)
{ return mesh->vertices + indices[i]; }

static bool polygon(const qa_scene_mesh *mesh, const uint32_t *indices, size_t count,
                    const qa_scene_image *lightmap, qa_scene_brush_surface *out)
{
    if (count < 3 || count > UINT32_MAX) return false;
    const qa_scene_vertex *a = point(mesh, indices, 0), *b = NULL, *c = NULL;
    double determinant = 0, g11 = 0, g12 = 0, g22 = 0;
    qa_vec3 e1 = {0}, e2 = {0};
    for (size_t i = 1; i + 1 < count; ++i) {
        const qa_scene_vertex *p = point(mesh, indices, i), *q = point(mesh, indices, i + 1);
        qa_vec3 u = qa_vec_sub(p->position, a->position), v = qa_vec_sub(q->position, a->position);
        double uu = qa_vec_dot(u, u), uv = qa_vec_dot(u, v), vv = qa_vec_dot(v, v);
        double d = uu * vv - uv * uv;
        if (d > determinant) { determinant = d; b = p; c = q; e1 = u; e2 = v; g11 = uu; g12 = uv; g22 = vv; }
    }
    if (!(determinant > 0)) return false;
    qa_scene_brush_surface brush = {.present = true, .normalized_texture = true,
        .polygon_vertices = (uint32_t)count, .polygon_indices = indices,
        .texture_mins = {FLT_MAX, FLT_MAX}, .texture_maxs = {-FLT_MAX, -FLT_MAX}};
    qa_vec3 normal = qa_vec_normalize(qa_vec_cross(e1, e2));
    brush.plane = (qa_scene_plane){normal, qa_vec_dot(normal, a->position)};
    float au[2] = {a->texcoord.x, a->texcoord.y}, bu[2] = {b->texcoord.x, b->texcoord.y};
    float cu[2] = {c->texcoord.x, c->texcoord.y};
    for (unsigned axis = 0; axis < 2; ++axis) {
        double u = bu[axis] - au[axis], v = cu[axis] - au[axis];
        qa_vec3 projection = qa_vec_add(qa_vec_scale(e1, (float)((u * g22 - v * g12) / determinant)),
                                       qa_vec_scale(e2, (float)((v * g11 - u * g12) / determinant)));
        brush.texel_projection[axis][0] = projection.x;
        brush.texel_projection[axis][1] = projection.y;
        brush.texel_projection[axis][2] = projection.z;
        brush.texel_projection[axis][3] = au[axis] - qa_vec_dot(projection, a->position);
    }
    if (lightmap) {
        double u1 = bu[0] - au[0], v1 = bu[1] - au[1], u2 = cu[0] - au[0], v2 = cu[1] - au[1];
        double d = u1 * v2 - u2 * v1;
        if (d == 0 || !lightmap->level_count) return false;
        uint32_t size[2] = {lightmap->levels[0].width, lightmap->levels[0].height};
        brush.lightmap_rect = (qa_scene_rect){0, 0, size[0], size[1]};
        for (unsigned axis = 0; axis < 2; ++axis) {
            double la = axis ? a->lightmap.y : a->lightmap.x;
            double lb = axis ? b->lightmap.y : b->lightmap.x;
            double lc = axis ? c->lightmap.y : c->lightmap.x;
            double s = ((lb - la) * v2 - (lc - la) * v1) * size[axis] / d;
            double t = ((lc - la) * u1 - (lb - la) * u2) * size[axis] / d;
            brush.lightmap_from_texel[axis][0] = (float)s;
            brush.lightmap_from_texel[axis][1] = (float)t;
            brush.lightmap_from_texel[axis][2] = (float)(la * size[axis] - .5 - s * au[0] - t * au[1]);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const qa_scene_vertex *p = point(mesh, indices, i);
        qa_vec3 previous = point(mesh, indices, (i + count - 1) % count)->position;
        qa_vec3 next = point(mesh, indices, (i + 1) % count)->position;
        if (qa_vec_dot(qa_vec_cross(qa_vec_sub(p->position, previous), qa_vec_sub(next, p->position)), normal) < -1e-4f ||
            fabsf(qa_vec_dot(normal, p->position) - brush.plane.distance) > .01f) return false;
        float uv[2] = {p->texcoord.x, p->texcoord.y};
        for (unsigned axis = 0; axis < 2; ++axis) {
            const float *projection = brush.texel_projection[axis];
            float value = p->position.x * projection[0] + p->position.y * projection[1] +
                          p->position.z * projection[2] + projection[3];
            if (!isfinite(value) || fabsf(value - uv[axis]) > 1e-4f * fmaxf(1, fabsf(uv[axis]))) return false;
            brush.texture_mins[axis] = fminf(brush.texture_mins[axis], uv[axis]);
            brush.texture_maxs[axis] = fmaxf(brush.texture_maxs[axis], uv[axis]);
            if (lightmap) {
                const float *projection_light = brush.lightmap_from_texel[axis];
                float actual = axis ? p->lightmap.y : p->lightmap.x;
                float expected = uv[0] * projection_light[0] + uv[1] * projection_light[1] + projection_light[2];
                uint32_t size = axis ? brush.lightmap_rect.height : brush.lightmap_rect.width;
                if (!isfinite(expected) || fabsf(expected - (actual * (float)size - .5f)) > .01f) return false;
            }
        }
    }
    brush.identity = qa_scene_identity();
    *out = brush;
    return true;
}

/* Strip interior triangle edges once at load. The remaining directed edges
 * must form one convex winding; otherwise keep the exact original triangles. */
static size_t winding(const qa_scene_mesh *mesh, uint32_t *out)
{
    size_t count = 0;
    for (size_t i = 0; i < mesh->index_count; ++i) {
        size_t next = i - i % 3 + (i + 1) % 3;
        uint32_t a = mesh->indices[i], b = mesh->indices[next];
        bool inside = false;
        for (size_t j = 0; j < mesh->index_count; ++j) {
            size_t other = j - j % 3 + (j + 1) % 3;
            if (mesh->indices[j] == b && mesh->indices[other] == a) { inside = true; break; }
        }
        if (!inside) out[count++] = (uint32_t)i;
    }
    if (count < 3) return 0;
    for (size_t i = 1; i < count; ++i) {
        size_t edge = out[i - 1], next = edge - edge % 3 + (edge + 1) % 3;
        size_t j = i;
        while (j < count && mesh->indices[out[j]] != mesh->indices[next]) ++j;
        if (j == count) return 0;
        uint32_t swap = out[i]; out[i] = out[j]; out[j] = swap;
    }
    size_t last = out[count - 1], next = last - last % 3 + (last + 1) % 3;
    if (mesh->indices[next] != mesh->indices[out[0]]) return 0;
    for (size_t i = 0; i < count; ++i) out[i] = mesh->indices[out[i]];
    return count;
}

bool qaw_brush_prepare(const qa_scene_mesh *mesh, const qa_scene_image *lightmap,
                      qaw_brush_geometry *out, qa_error *error)
{
    if (!mesh->index_count || mesh->index_count % 3 || !mesh->identity || !mesh->geometry) return true;
    out->winding = malloc(mesh->index_count * sizeof(*out->winding));
    if (!out->winding) goto memory;
    size_t count = mesh->index_count <= 1536 ? winding(mesh, out->winding) : 0;
    if (count && polygon(mesh, out->winding, count, lightmap, &out->draw)) {
        qa_scene_geometry_brush_adopt(mesh->geometry, NULL, out->winding);
        return true;
    }
    free(out->winding); out->winding = NULL;
    count = mesh->index_count / 3;
    out->parts = calloc(count, sizeof(*out->parts));
    if (!out->parts) goto memory;
    for (size_t i = 0; i < count; ++i) {
        out->parts[i].polygon_indices = mesh->indices + i * 3;
        out->parts[i].polygon_vertices = 3;
        polygon(mesh, mesh->indices + i * 3, 3, lightmap, out->parts + i);
    }
    out->draw = (qa_scene_brush_surface){.present = true, .parts = out->parts, .part_count = count};
    qa_scene_geometry_brush_adopt(mesh->geometry, out->parts, NULL);
    return true;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating world surface span descriptors");
    return false;
}

void qaw_brush_destroy(qaw_brush_geometry *brush)
{
    *brush = (qaw_brush_geometry){0};
}
