#ifndef QA_SCENE_EFFECTS_INTERNAL_H
#define QA_SCENE_EFFECTS_INTERNAL_H

#include "qa/scene.h"
#define QA_EFFECT_PI 3.14159265358979323846

static inline qa_vec3 qa_effect_perpendicular(qa_vec3 direction)
{
    qa_vec3 seed;
    float x = fabsf(direction.x), y = fabsf(direction.y), z = fabsf(direction.z);
    if (x <= y && x <= z) seed = qa_v3(1, 0, 0);
    else if (y <= z) seed = qa_v3(0, 1, 0);
    else seed = qa_v3(0, 0, 1);
    return qa_vec_normalize(qa_vec_sub(seed, qa_vec_scale(direction, qa_vec_dot(seed, direction))));
}

static inline qa_vec3 qa_effect_rotate(qa_vec3 point, qa_vec3 axis, float degrees)
{
    float angle = (float)((double)degrees * QA_EFFECT_PI / 180.0);
    float sine = (float)sin(angle), cosine = (float)cos(angle);
    return qa_vec_add(qa_vec_add(qa_vec_scale(point, cosine),
                               qa_vec_scale(qa_vec_cross(axis, point), sine)),
                      qa_vec_scale(axis, qa_vec_dot(axis, point) * (1.0f - cosine)));
}

static inline qa_vec3 qa_effect_point(qa_scene_matrix matrix, qa_vec3 point)
{
    return qa_v3(matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12],
                 matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13],
                 matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14]);
}

bool qa_effect_mesh(qa_scene_frame *, size_t vertices, size_t indices,
                    qa_scene_mesh *, qa_scene_vertex **, uint32_t **, qa_error *);
void qa_effect_draw(qa_scene_draw *, const qa_scene_view *, const qa_scene_mesh *,
                    const qa_scene_image *, bool additive);
void qa_effect_bounds(qa_scene_mesh *);
qa_vec3 qa_effect_sky_vector(unsigned face, float s, float t, float radius);

#endif
