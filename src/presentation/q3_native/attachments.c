#include "attachments.h"

#include <string.h>

bool q3n_attach(const qa_q3_presentation_assets *assets, qa_q3_ref_entity *child,
    const qa_q3_ref_entity *parent, qa_string_id name, q3n_attachment_rule rule,
    q3n_attachment_missing missing, bool *found, qa_error *error) {
    qa_model_tag tag; bool matched;
    float fraction = rule == Q3N_ATTACH_CHILD_POSE ? (1 + -parent->back_lerp)
        : 1.0f - parent->back_lerp;
    if (!qa_q3_presentation_tag(assets, parent->model, name, parent->old_frame,
            parent->frame, fraction, &tag, &matched, error)) return false;
    if (found) *found = matched;
    if (!matched && missing != Q3N_ATTACH_MISSING_IDENTITY) {
        if (missing == Q3N_ATTACH_MISSING_SKIP) return true;
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Selected Q3 hands model has no required weapon tag");
        return false;
    }
    qa_vec3 offset = qa_v3(tag.origin[0], tag.origin[1], tag.origin[2]), axes[3];
    for (unsigned i = 0; i < 3; ++i)
        axes[i] = qa_v3(tag.axes[i][0], tag.axes[i][1], tag.axes[i][2]);
    if (rule == Q3N_ATTACH_CHILD_POSE) {
        qa_vec3 tag_axes[3], result[3];
        for (unsigned i = 0; i < 3; ++i)
            tag_axes[i] = qa_axis_from_local(axes[i], parent->axis, QA_AXIS_ORDERED_SUM);
        child->origin = qa_vec_add(parent->origin,
            qa_axis_from_local(offset, parent->axis, QA_AXIS_ORDERED_SUM));
        child->old_origin = child->origin;
        for (unsigned i = 0; i < 3; ++i)
            result[i] = qa_axis_from_local(child->axis[i], tag_axes, QA_AXIS_ORDERED_SUM);
        memcpy(child->axis, result, sizeof(result));
        child->lighting_origin = parent->lighting_origin;
        return true;
    }
    child->origin = qa_vec_ma(qa_vec_ma(qa_vec_ma(parent->origin, offset.x, parent->axis[0]),
        offset.y, parent->axis[1]), offset.z, parent->axis[2]);
    if (rule == Q3N_ATTACH_ROTATED) {
        qa_vec3 local[3]; qa_axis_multiply(child->axis, axes, local);
        qa_axis_multiply(local, parent->axis, child->axis);
    } else {
        qa_axis_multiply(axes, parent->axis, child->axis);
        child->back_lerp = parent->back_lerp;
    }
    return true;
}
