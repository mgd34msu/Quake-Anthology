#include "internal.h"
static void cross(const float a[3], const float b[3], float out[3]) {
    model_v3_store(out, qa_vec_cross(model_v3(a), model_v3(b)));
}
static float dot(const float a[3], const float b[3]) {
    return qa_vec_dot(model_v3(a), model_v3(b));
}
void qa_model_transform_identity(qa_model_transform *out) {
    memset(out, 0, sizeof(*out));
    for (unsigned i = 0; i < 3; ++i) {
        out->axes[i][i] = 1;
        out->scale[i] = 1;
    }
}
void qa_model_transform_direction(const qa_model_transform *t, const float value[3], float out[3]) {
    float scaled[3], result[3];
    for (unsigned i = 0; i < 3; ++i)
        scaled[i] = value[i] * t->scale[i];
    for (unsigned i = 0; i < 3; ++i)
        result[i] =
            t->axes[0][i] * scaled[0] + t->axes[1][i] * scaled[1] + t->axes[2][i] * scaled[2];
    memcpy(out, result, sizeof(result));
}
void qa_model_transform_point(const qa_model_transform *t, const float value[3], float out[3]) {
    float result[3];
    qa_model_transform_direction(t, value, result);
    for (unsigned i = 0; i < 3; ++i)
        result[i] += t->origin[i];
    memcpy(out, result, sizeof(result));
}
void qa_model_transform_compose(const qa_model_transform *parent, const qa_model_transform *child,
                                qa_model_transform *out) {
    qa_model_transform result;
    qa_model_transform_point(parent, child->origin, result.origin);
    for (unsigned i = 0; i < 3; ++i) {
        float column[3];
        for (unsigned j = 0; j < 3; ++j)
            column[j] = child->axes[i][j] * child->scale[i];
        qa_model_transform_direction(parent, column, result.axes[i]);
        result.scale[i] = 1;
    }
    *out = result;
}
bool qa_model_transform_inverse(const qa_model_transform *t, qa_model_transform *out) {
    float columns[3][3], cofactors[3][3];
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            columns[i][j] = t->axes[i][j] * t->scale[i];
    cross(columns[1], columns[2], cofactors[0]);
    cross(columns[2], columns[0], cofactors[1]);
    cross(columns[0], columns[1], cofactors[2]);
    float determinant = dot(columns[0], cofactors[0]);
    if (determinant == 0 || !isfinite(determinant))
        return false;
    qa_model_transform result;
    for (unsigned i = 0; i < 3; ++i) {
        result.scale[i] = 1;
        for (unsigned j = 0; j < 3; ++j)
            result.axes[i][j] = cofactors[j][i] / determinant;
        result.origin[i] = -dot(t->origin, cofactors[i]) / determinant;
    }
    *out = result;
    return true;
}
bool qa_model_attachment_align(const qa_model_transform *grip, const qa_model_transform *socket,
                               qa_model_transform *out) {
    qa_model_transform inverse;
    if (!qa_model_transform_inverse(grip, &inverse))
        return false;
    qa_model_transform_compose(socket, &inverse, out);
    return true;
}
bool qa_model_triangle_attachment(const qa_model_vertex triangle[3], qa_model_transform *out) {
    qa_model_transform result;
    float edge[3];
    for (unsigned i = 0; i < 3; ++i) {
        result.origin[i] = triangle[0].position[i];
        result.axes[0][i] = triangle[1].position[i] - triangle[0].position[i];
        edge[i] = triangle[2].position[i] - triangle[0].position[i];
        result.scale[i] = 1;
    }
    cross(result.axes[0], edge, result.axes[2]);
    if (dot(result.axes[0], result.axes[0]) == 0 || dot(result.axes[2], result.axes[2]) == 0)
        return false;
    model_normalize(result.axes[0]);
    model_normalize(result.axes[2]);
    cross(result.axes[2], result.axes[0], result.axes[1]);
    *out = result;
    return true;
}
void qa_model_transform_bounds(const qa_model_transform *transform, const qa_model_bounds *bounds,
                               qa_model_bounds *out) {
    qa_model_bounds result;
    model_bounds_clear(&result);
    for (unsigned corner = 0; corner < 8; ++corner) {
        float point[3], world[3];
        for (unsigned axis = 0; axis < 3; ++axis)
            point[axis] = corner & (1u << axis) ? bounds->max[axis] : bounds->min[axis];
        qa_model_transform_point(transform, point, world);
        model_bounds_add(&result, world);
    }
    *out = result;
}
