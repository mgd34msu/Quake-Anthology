#include "internal.h"
#include "visual_actor.h"
#include "qa/qc_visual.h"
#include <math.h>

static bool scalar(const qa_qc_instance *vm, uint32_t slot, const qa_qc_definition *field,
    bool optional, float *out, qa_error *error)
{
    *out = 0;
    if (!field && optional) return true;
    if (!field || field->type != QA_QC_FLOAT)
        return qc_fail(error, QA_ERROR_FORMAT, slot, "QC appearance field has no source float definition");
    if (!qc_entity_range(vm, slot, field->offset, 1, error)) return false;
    *out = qc_load_float(qc_entity_words_const(vm, slot), field->offset);
    return isfinite(*out) || qc_fail(error, QA_ERROR_FORMAT, slot, "QC appearance field is nonfinite");
}

bool qa_qc_visual_read(const qa_qc_instance *vm, uint32_t slot, qa_actor_id expected,
    qa_qc_visual *out, qa_error *error)
{
    if (!out) return qc_fail(error, QA_ERROR_ARGUMENT, slot, "QC appearance requires its output");
    if (!qc_visual_actor(vm, slot, expected, error)) return false;
    const qa_qc_definition *field = vm->program->engine_fields.model;
    if (!field || field->type != QA_QC_STRING)
        return qc_fail(error, QA_ERROR_FORMAT, slot, "QC appearance has no source model string definition");
    if (!qc_entity_range(vm, slot, field->offset, 1, error)) return false;
    qa_qc_visual value = {0};
    if (!qa_qc_string(vm, qc_load_int(qc_entity_words_const(vm, slot), field->offset), &value.model, error) ||
        !scalar(vm, slot, vm->program->engine_fields.modelindex, false, &value.model_index, error) ||
        !scalar(vm, slot, vm->program->engine_fields.frame, false, &value.frame, error) ||
        !scalar(vm, slot, vm->program->engine_fields.skin, false, &value.skin, error) ||
        !scalar(vm, slot, vm->program->engine_fields.colormap, false, &value.colormap, error) ||
        !scalar(vm, slot, vm->program->engine_fields.effects, false, &value.effects, error) ||
        !scalar(vm, slot, vm->program->engine_fields.alpha, true, &value.alpha, error) ||
        !scalar(vm, slot, vm->program->engine_fields.scale, true, &value.scale, error)) return false;
    *out = value;
    return true;
}

bool qa_qc_alpha_read(const qa_qc_instance *vm, uint32_t slot, qa_actor_id expected,
    float *out, bool *field_present, qa_error *error)
{
    if (!out || !field_present)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "QC alpha requires its opacity and field outputs");
    if (!qc_retained_actor(vm, slot, expected, error)) return false;
    const qa_qc_definition *field = vm->program->engine_fields.alpha;
    float value;
    if (!scalar(vm, slot, field, true, &value, error)) return false;
    *field_present = field != NULL;
    *out = value == 0 ? 1 : fmaxf(0, fminf(1, value));
    return true;
}

bool qa_qc_alpha_write(qa_qc_instance *vm, uint32_t slot, qa_actor_id expected,
    float value, bool *field_present, qa_error *error)
{
    if (!field_present || !isfinite(value))
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "QC alpha requires finite opacity and its field result");
    if (!qc_retained_actor(vm, slot, expected, error)) return false;
    const qa_qc_definition *field = vm->program->engine_fields.alpha;
    *field_present = field != NULL;
    if (!field) return true;
    if (field->type != QA_QC_FLOAT)
        return qc_fail(error, QA_ERROR_FORMAT, slot, "QC alpha extension is not a float field");
    int32_t reference;
    /* QC zero means default opaque. Quakespasm uses -1 for invisible. */
    return qa_qc_slot_reference(vm, slot, &reference, error) &&
        qa_qc_set_entity_float(vm, reference, field->offset,
            value <= 0 ? -1 : fminf(1, value), error);
}
