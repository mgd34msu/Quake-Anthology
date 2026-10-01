#include "attachments.h"

#include <string.h>

static float add(float a, float b) { volatile float value = a + b; return value; }
static float mul(float a, float b) { volatile float value = a * b; return value; }
static float dot(qa_vec3 a, qa_vec3 b) {
    return add(add(mul(a.x, b.x), mul(a.y, b.y)), mul(a.z, b.z));
}
static void multiply(const qa_vec3 left[3], const qa_vec3 right[3], qa_vec3 out[3]) {
    qa_vec3 columns[3] = {{right[0].x, right[1].x, right[2].x},
        {right[0].y, right[1].y, right[2].y}, {right[0].z, right[1].z, right[2].z}};
    qa_vec3 result[3];
    for (unsigned i = 0; i < 3; ++i)
        result[i] = qa_v3(dot(left[i], columns[0]), dot(left[i], columns[1]), dot(left[i], columns[2]));
    memcpy(out, result, sizeof(result));
}

bool q3n_attach(const qa_q3_presentation_assets *assets, qa_q3_ref_entity *child,
    const qa_q3_ref_entity *parent, const char *name, bool rotated, qa_error *error) {
    qa_model_tag tag; bool found;
    volatile float fraction = 1.0f - parent->back_lerp;
    if (!qa_q3_presentation_tag(assets, parent->model, name, parent->old_frame,
            parent->frame, fraction, &tag, &found, error)) return false;
    qa_vec3 origin = parent->origin, axes[3];
    for (unsigned i = 0; i < 3; ++i) {
        origin.x = add(origin.x, mul(parent->axis[i].x, tag.origin[i]));
        origin.y = add(origin.y, mul(parent->axis[i].y, tag.origin[i]));
        origin.z = add(origin.z, mul(parent->axis[i].z, tag.origin[i]));
        axes[i] = qa_v3(tag.axes[i][0], tag.axes[i][1], tag.axes[i][2]);
    }
    child->origin = origin;
    if (rotated) {
        qa_vec3 local[3]; multiply(child->axis, axes, local);
        multiply(local, parent->axis, child->axis);
    } else {
        multiply(axes, parent->axis, child->axis);
        child->back_lerp = parent->back_lerp;
    }
    return true;
}
