#include "attachments.h"

#include <string.h>

bool q3n_attach(const qa_q3_presentation_assets *assets, qa_q3_ref_entity *child,
    const qa_q3_ref_entity *parent, const char *name, bool rotated, qa_error *error) {
    qa_model_tag tag; bool found;
    float fraction = 1.0f - parent->back_lerp;
    if (!qa_q3_presentation_tag(assets, parent->model, name, parent->old_frame,
            parent->frame, fraction, &tag, &found, error)) return false;
    qa_vec3 origin = parent->origin, axes[3];
    for (unsigned i = 0; i < 3; ++i) {
        origin.x = (origin.x + (parent->axis[i].x * tag.origin[i]));
        origin.y = (origin.y + (parent->axis[i].y * tag.origin[i]));
        origin.z = (origin.z + (parent->axis[i].z * tag.origin[i]));
        axes[i] = qa_v3(tag.axes[i][0], tag.axes[i][1], tag.axes[i][2]);
    }
    child->origin = origin;
    if (rotated) {
        qa_vec3 local[3]; qa_axis_multiply(child->axis, axes, local);
        qa_axis_multiply(local, parent->axis, child->axis);
    } else {
        qa_axis_multiply(axes, parent->axis, child->axis);
        child->back_lerp = parent->back_lerp;
    }
    return true;
}
