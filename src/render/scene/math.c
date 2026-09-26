#include "qa/scene.h"

#include <math.h>

void qa_scene_matrix_identity(qa_scene_matrix *out)
{
    if (out == NULL) return;
    *out = (qa_scene_matrix){.m = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
}

qa_scene_matrix qa_scene_matrix_multiply(qa_scene_matrix a, qa_scene_matrix b)
{
    qa_scene_matrix result;
    for (size_t column = 0; column < 4; ++column) {
        for (size_t row = 0; row < 4; ++row) {
            float value = a.m[row] * b.m[column * 4];
            value += a.m[4 + row] * b.m[column * 4 + 1];
            value += a.m[8 + row] * b.m[column * 4 + 2];
            value += a.m[12 + row] * b.m[column * 4 + 3];
            result.m[column * 4 + row] = value;
        }
    }
    return result;
}

qa_scene_matrix qa_scene_view_matrix(const qa_scene_view *view)
{
    qa_vec3 f = view->axis[0], l = view->axis[1], u = view->axis[2];
    return (qa_scene_matrix){.m = {-l.x, u.x, -f.x, 0, -l.y, u.y, -f.y, 0,
        -l.z, u.z, -f.z, 0, qa_vec_dot(view->origin, l), -qa_vec_dot(view->origin, u),
        qa_vec_dot(view->origin, f), 1}};
}

qa_scene_matrix qa_scene_projection(float fov_x, float fov_y, float near_clip, float far_clip)
{
    const double radians = 3.14159265358979323846 / 360.0;
    float width = 2.0f * (float)((double)near_clip * tan((double)fov_x * radians));
    float height = 2.0f * (float)((double)near_clip * tan((double)fov_y * radians));
    float depth = far_clip - near_clip;
    return (qa_scene_matrix){.m = {2 * near_clip / width,0,0,0, 0,2 * near_clip / height,0,0,
        0,0,-(far_clip + near_clip) / depth,-1, 0,0,-2 * far_clip * near_clip / depth,0}};
}

qa_scene_vec4 qa_scene_matrix_point(qa_scene_matrix matrix, qa_vec3 point)
{
    qa_scene_vec4 result;
    float *out[4] = {&result.x, &result.y, &result.z, &result.w};
    for (size_t row = 0; row < 4; ++row) {
        float value = point.x * matrix.m[row];
        value += point.y * matrix.m[4 + row];
        value += point.z * matrix.m[8 + row];
        value += matrix.m[12 + row];
        *out[row] = value;
    }
    return result;
}

qa_scene_matrix qa_scene_model_matrix(const qa_model_transform *transform)
{
    qa_scene_matrix result;
    qa_scene_matrix_identity(&result);
    for (size_t column = 0; column < 3; ++column)
        for (size_t row = 0; row < 3; ++row)
            result.m[column * 4 + row] = transform->axes[column][row] * transform->scale[column];
    for (size_t row = 0; row < 3; ++row) result.m[12 + row] = transform->origin[row];
    return result;
}

size_t qa_scene_frustum(const qa_scene_view *view, qa_scene_plane planes[6])
{
    for (size_t i = 0; i < 4; ++i) {
        size_t axis = i / 2 + 1;
        float scale = view->projection.m[(axis-1)*5];
        float inverse = (float)(1.0 / hypot(1.0, scale));
        float sign = (i & 1u) == 0 ? 1.0f : -1.0f;
        qa_vec3 normal = qa_vec_add(qa_vec_scale(view->axis[0], inverse),
            qa_vec_scale(view->axis[axis], scale * inverse * sign));
        planes[i] = (qa_scene_plane){normal, qa_vec_dot(view->origin, normal)};
    }
    if (view->clip_enabled) { planes[4] = view->clip_plane; return 5; }
    return 4;
}

bool qa_scene_bounds_visible(qa_bounds bounds, const qa_scene_plane *planes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        qa_vec3 n = planes[i].normal;
        qa_vec3 front = {n.x < 0 ? bounds.mins.x : bounds.maxs.x,
            n.y < 0 ? bounds.mins.y : bounds.maxs.y, n.z < 0 ? bounds.mins.z : bounds.maxs.z};
        if (qa_vec_dot(front, n) < planes[i].distance) return false;
    }
    return true;
}
