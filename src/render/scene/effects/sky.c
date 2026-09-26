#include "internal.h"
#include "qa/scene_effects.h"

#include <string.h>

qa_vec3 qa_effect_sky_vector(unsigned face, float s, float t, float radius)
{
    float horizontal = s * radius, vertical = t * radius;
    switch (face) {
    case 0: return qa_v3(radius, -horizontal, vertical);
    case 1: return qa_v3(-radius, horizontal, vertical);
    case 2: return qa_v3(horizontal, radius, vertical);
    case 3: return qa_v3(-horizontal, -radius, vertical);
    case 4: return qa_v3(-vertical, -horizontal, radius);
    case 5: return qa_v3(vertical, -horizontal, -radius);
    default: return qa_v3(0, 0, 0);
    }
}

void qa_scene_sky_bounds_reset(qa_scene_sky_bounds bounds[6])
{
    for (size_t i = 0; i < 6; ++i)
        bounds[i] = (qa_scene_sky_bounds){9999, 9999, -9999, -9999};
}

static void project_polygon(const qa_vec3 *points, size_t count, qa_scene_sky_bounds bounds[6])
{
    qa_vec3 sum = qa_v3(0, 0, 0);
    for (size_t i = 0; i < count; ++i) sum = qa_vec_add(sum, points[i]);
    float x = fabsf(sum.x), y = fabsf(sum.y), z = fabsf(sum.z);
    unsigned face = x > y && x > z ? (sum.x < 0 ? 1 : 0) :
                    y > z && y > x ? (sum.y < 0 ? 3 : 2) : (sum.z < 0 ? 5 : 4);
    qa_scene_sky_bounds *range = &bounds[face];
    for (size_t i = 0; i < count; ++i) {
        qa_vec3 p = points[i];
        float divisor, s, t;
        switch (face) {
        case 0: divisor = p.x; s = -p.y; t = p.z; break;
        case 1: divisor = -p.x; s = p.y; t = p.z; break;
        case 2: divisor = p.y; s = p.x; t = p.z; break;
        case 3: divisor = -p.y; s = -p.x; t = p.z; break;
        case 4: divisor = p.z; s = -p.y; t = -p.x; break;
        default: divisor = -p.z; s = -p.y; t = p.x; break;
        }
        if (divisor < 0.001f) continue;
        s /= divisor;
        t /= divisor;
        range->min_s = fminf(range->min_s, s);
        range->min_t = fminf(range->min_t, t);
        range->max_s = fmaxf(range->max_s, s);
        range->max_t = fmaxf(range->max_t, t);
    }
}

static bool clip_polygon(const qa_vec3 *points, size_t count, unsigned stage,
                          qa_scene_sky_bounds bounds[6], qa_error *error)
{
    static const qa_vec3 planes[6] = {{1, 1, 0}, {1, -1, 0}, {0, -1, 1},
                                     {0, 1, 1}, {1, 0, 1}, {-1, 0, 1}};
    if (count > 62) {
        qa_error_set(error, QA_ERROR_FORMAT, count, "Sky polygon exceeds source clip limit");
        return false;
    }
    if (stage == 6) {
        project_polygon(points, count, bounds);
        return true;
    }
    float distance[62];
    int sides[62];
    bool positive = false, negative = false;
    for (size_t i = 0; i < count; ++i) {
        distance[i] = qa_vec_dot(points[i], planes[stage]);
        sides[i] = distance[i] > 0.1f ? 1 : distance[i] < -0.1f ? -1 : 0;
        positive |= sides[i] > 0;
        negative |= sides[i] < 0;
    }
    if (!positive || !negative) return clip_polygon(points, count, stage + 1, bounds, error);
    qa_vec3 front[64], back[64];
    size_t front_count = 0, back_count = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t next = (i + 1) % count;
        if (sides[i] >= 0) front[front_count++] = points[i];
        if (sides[i] <= 0) back[back_count++] = points[i];
        if (sides[i] == 0 || sides[next] == 0 || sides[i] == sides[next]) continue;
        float fraction = distance[i] / (distance[i] - distance[next]);
        qa_vec3 intersection = qa_vec_add(points[i], qa_vec_scale(qa_vec_sub(points[next], points[i]), fraction));
        if (front_count >= 64 || back_count >= 64) {
            qa_error_set(error, QA_ERROR_FORMAT, count, "Sky clipping output exceeds source limit");
            return false;
        }
        front[front_count++] = back[back_count++] = intersection;
    }
    return clip_polygon(front, front_count, stage + 1, bounds, error) &&
           clip_polygon(back, back_count, stage + 1, bounds, error);
}

bool qa_scene_sky_clip(const qa_scene_mesh *meshes, size_t count, qa_vec3 origin,
                       qa_scene_sky_bounds bounds[6], qa_error *error)
{
    if ((!meshes && count) || !bounds || !qa_vec_finite(origin)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid sky clip input");
        return false;
    }
    for (size_t m = 0; m < count; ++m) {
        const qa_scene_mesh *mesh = &meshes[m];
        if (mesh->index_count % 3 != 0 || (!mesh->indices && mesh->index_count) ||
            (!mesh->vertices && mesh->vertex_count)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, m, "Sky clipping requires indexed triangles");
            return false;
        }
        for (size_t i = 0; i < mesh->index_count; i += 3) {
            qa_vec3 points[3];
            for (size_t j = 0; j < 3; ++j) {
                uint32_t index = mesh->indices[i + j];
                if (index >= mesh->vertex_count) {
                    qa_error_set(error, QA_ERROR_FORMAT, i + j, "Sky vertex index outside mesh");
                    return false;
                }
                points[j] = qa_vec_sub(mesh->vertices[index].position, origin);
            }
            if (!clip_polygon(points, 3, 0, bounds, error)) return false;
        }
    }
    return true;
}

static bool visible(qa_scene_sky_bounds bounds)
{
    return bounds.min_s < bounds.max_s && bounds.min_t < bounds.max_t;
}

static bool submit_face(qa_scene_frame *frame, const qa_scene_view *view,
                         const qa_scene_image *image, unsigned face,
                         qa_scene_sky_bounds range, float radius, float rotation,
                         qa_vec3 axis, float seam, qa_scene_vec4 color, qa_error *error)
{
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, 4, 6, &mesh, &vertices, &indices, error)) return false;
    float horizontal[4] = {range.min_s, range.min_s, range.max_s, range.max_s};
    float vertical[4] = {range.min_t, range.max_t, range.min_t, range.max_t};
    for (size_t i = 0; i < 4; ++i) {
        qa_vec3 direction = qa_effect_sky_vector(face, horizontal[i], vertical[i], radius);
        if (rotation != 0 && qa_vec_dot(axis, axis) != 0)
            direction = qa_effect_rotate(direction, axis, rotation);
        vertices[i].position = qa_vec_add(view->origin, direction);
        vertices[i].normal = qa_vec_normalize(qa_vec_scale(direction, -1));
        vertices[i].texcoord = (qa_scene_vec2){
            fmaxf(seam, fminf(1 - seam, (horizontal[i] + 1) * 0.5f)),
            1 - fmaxf(seam, fminf(1 - seam, (vertical[i] + 1) * 0.5f))};
        vertices[i].color = color;
    }
    const uint32_t order[6] = {0, 1, 2, 2, 1, 3};
    memcpy(indices, order, sizeof(order));
    qa_effect_bounds(&mesh);
    qa_scene_draw draw;
    qa_effect_draw(&draw, view, &mesh, image, false);
    draw.state.blend_source = QA_BLEND_ONE;
    draw.state.blend_destination = QA_BLEND_ZERO;
    draw.state.depth_write = true;
    draw.state.depth_near = draw.state.depth_far = 1;
    return qa_scene_frame_draw(frame, &draw, error);
}

bool qa_scene_sky(qa_scene_frame *frame, const qa_scene_view *view,
                  const qa_scene_image *const images[6], float radius, float rotation,
                  qa_vec3 axis, qa_scene_vec4 color, qa_error *error)
{
    if (!frame || !view || !images || !isfinite(radius) || radius <= 0 || !isfinite(rotation) || !qa_vec_finite(axis)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid sky parameters");
        return false;
    }
    axis = qa_vec_normalize(axis);
    for (unsigned face = 0; face < 6; ++face)
        if (!submit_face(frame, view, images[face], face, (qa_scene_sky_bounds){-1, -1, 1, 1},
                         radius, rotation, axis, 0, color, error)) return false;
    return true;
}

bool qa_scene_q2_sky(qa_scene_frame *frame, const qa_scene_view *view,
                     const qa_scene_image *const images[6], const qa_scene_sky_bounds bounds[6],
                     float rotation, qa_vec3 axis, bool rotating, qa_scene_vec4 color, qa_error *error)
{
    if (!frame || !view || !images || !bounds || !isfinite(rotation) || !qa_vec_finite(axis)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 sky parameters");
        return false;
    }
    bool any = false;
    for (unsigned face = 0; face < 6; ++face) any |= visible(bounds[face]);
    if (!any) return true;
    axis = qa_vec_normalize(axis);
    for (unsigned face = 0; face < 6; ++face) {
        qa_scene_sky_bounds range = rotating ? (qa_scene_sky_bounds){-1, -1, 1, 1} : bounds[face];
        if (visible(range) && !submit_face(frame, view, images[face], face, range, 2300,
                                           rotation, axis, rotating ? 1.0f / 256 : 1.0f / 512,
                                           color, error)) return false;
    }
    return true;
}

static qa_scene_vec2 cloud_coordinate(unsigned face, float s, float t, float height)
{
    qa_vec3 direction = qa_effect_sky_vector(face, s, t, 1024.0f / 1.75f);
    float radius = 4096, xx = direction.x * direction.x, yy = direction.y * direction.y;
    float zz = direction.z * direction.z, hh = height * height;
    float discriminant = zz * (radius * radius);
    discriminant += ((2 * xx) * radius) * height;
    discriminant += xx * hh;
    discriminant += ((2 * yy) * radius) * height;
    discriminant += yy * hh;
    discriminant += ((2 * zz) * radius) * height;
    discriminant += zz * hh;
    float inverse = 1 / (2 * qa_vec_dot(direction, direction));
    float p = (float)(inverse * ((-2 * direction.z) * radius + 2 * sqrt(discriminant)));
    qa_vec3 intersection = qa_v3(direction.x * p, direction.y * p, direction.z * p + radius);
    float length = qa_vec_length(intersection);
    /* Negative source cloud heights can produce NaN. Do not turn that into a
     * zero direction before the material boundary checks its coordinates. */
    if (length != 0) intersection = qa_vec_scale(intersection, 1 / length);
    return (qa_scene_vec2){(float)acos(intersection.x), (float)acos(intersection.y)};
}

bool qa_scene_q3_sky_geometry(qa_scene_frame *frame, qa_vec3 origin, float far_clip,
                              float cloud_height, const qa_scene_sky_bounds bounds[6],
                              qa_scene_sky_geometry *out, qa_error *error)
{
    if (!frame || !out || !bounds || !qa_vec_finite(origin) || !isfinite(far_clip) || far_clip <= 0 || !isfinite(cloud_height)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 sky geometry parameters");
        return false;
    }
    qa_scene_sky_geometry result = {0};
    size_t cloud_vertex_count = 0, cloud_index_count = 0;
    for (unsigned face = 0; face < 6; ++face) {
        if (!visible(bounds[face])) continue;
        int min_s = (int)fmaxf(-4, fminf(4, floorf(bounds[face].min_s * 4)));
        int min_t = (int)fmaxf(-4, fminf(4, floorf(bounds[face].min_t * 4)));
        int max_s = (int)fmaxf(-4, fminf(4, ceilf(bounds[face].max_s * 4)));
        int max_t = (int)fmaxf(-4, fminf(4, ceilf(bounds[face].max_t * 4)));
        if (min_s >= max_s || min_t >= max_t) continue;
        size_t width = (size_t)(max_s - min_s + 1), height = (size_t)(max_t - min_t + 1);
        qa_scene_mesh *mesh = &result.faces[face];
        qa_scene_vertex *vertices;
        uint32_t *indices;
        if (!qa_effect_mesh(frame, width * height, (width - 1) * (height - 1) * 6,
                            mesh, &vertices, &indices, error)) return false;
        size_t cursor = 0;
        for (int t = min_t; t <= max_t; ++t) for (int s = min_s; s <= max_s; ++s) {
            qa_vec3 direction = qa_effect_sky_vector(face, (float)s / 4, (float)t / 4, far_clip / 1.75f);
            vertices[cursor].position = qa_vec_add(origin, direction);
            vertices[cursor].normal = qa_vec_normalize(qa_vec_scale(direction, -1));
            vertices[cursor].texcoord = (qa_scene_vec2){((float)s / 4 + 1) * 0.5f, 1 - ((float)t / 4 + 1) * 0.5f};
            vertices[cursor++].color = (qa_scene_vec4){1, 1, 1, 1};
        }
        cursor = 0;
        for (size_t t = 0; t + 1 < height; ++t) for (size_t s = 0; s + 1 < width; ++s) {
            uint32_t index = (uint32_t)(s + t * width), stride = (uint32_t)width;
            const uint32_t quad[6] = {index, index + stride, index + 1, index + stride, index + stride + 1, index + 1};
            memcpy(indices + cursor, quad, sizeof(quad));
            cursor += 6;
        }
        qa_effect_bounds(mesh);
        result.visible[face] = true;
        if (face != 5) {
            cloud_vertex_count += mesh->vertex_count;
            cloud_index_count += mesh->index_count;
        }
    }
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, cloud_vertex_count, cloud_index_count, &result.clouds, &vertices, &indices, error)) return false;
    size_t vertex_cursor = 0, index_cursor = 0;
    for (unsigned face = 0; face < 5; ++face) {
        const qa_scene_mesh *mesh = &result.faces[face];
        for (size_t i = 0; i < mesh->vertex_count; ++i) {
            vertices[vertex_cursor + i] = mesh->vertices[i];
            vertices[vertex_cursor + i].texcoord = cloud_coordinate(face,
                mesh->vertices[i].texcoord.x * 2 - 1, 1 - mesh->vertices[i].texcoord.y * 2, cloud_height);
        }
        for (size_t i = 0; i < mesh->index_count; ++i)
            indices[index_cursor++] = (uint32_t)vertex_cursor + mesh->indices[i];
        vertex_cursor += mesh->vertex_count;
    }
    qa_effect_bounds(&result.clouds);
    *out = result;
    return true;
}
