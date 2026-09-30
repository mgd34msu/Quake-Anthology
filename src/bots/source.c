#include "qa/bot_source.h"

bool qa_bot_vector_component(const qa_bot_vector_source *source, unsigned axis, float *out,
                             qa_error *e) {
    if (!source || !out || axis > 2 || (!source->value && !source->read)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, axis, "Invalid bot vector component reader");
        return false;
    }
    if (source->value) {
        *out = axis == 0 ? source->value->x : axis == 1 ? source->value->y : source->value->z;
        return true;
    }
    return source->read(source->context, axis, out, e);
}
bool qa_bot_vector_read(const qa_bot_vector_source *source, qa_vec3 *out, qa_error *e) {
    if (!out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot vector output");
        return false;
    }
    if (source && source->value) {
        *out = *source->value;
        return true;
    }
    qa_vec3 value;
    if (!qa_bot_vector_component(source, 0, &value.x, e) ||
        !qa_bot_vector_component(source, 1, &value.y, e) ||
        !qa_bot_vector_component(source, 2, &value.z, e)) return false;
    *out = value;
    return true;
}
bool qa_bot_vector_write(const qa_bot_vector_target *target, const qa_bot_vector_source *source,
                         qa_error *e) {
    if (!target || (!target->value && !target->write)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot vector writer");
        return false;
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        float value;
        if (!target->value && target->admit && !target->admit(target->context, axis, e))
            return false;
        if (!qa_bot_vector_component(source, axis, &value, e)) return false;
        if (target->value) {
            if (axis == 0) target->value->x = value;
            else if (axis == 1) target->value->y = value;
            else target->value->z = value;
        } else if (!target->write(target->context, axis, value, e)) return false;
    }
    return true;
}
