#include "internal.h"
#include "qa/qc_visual.h"
#include <math.h>

static bool scalar(const qa_qc_instance *vm, uint32_t slot, const char *name,
    bool optional, float *out, qa_error *error)
{
    const qa_qc_definition *field = qa_qc_program_find_field(vm->program, name);
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
    if (!vm || !out || !qa_qc_idle(vm) || vm->destroying || !slot || slot >= vm->entity_count ||
        !vm->options.host.session)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "QC appearance requires an idle physical actor row");
    const qc_slot *binding = &vm->slots[slot];
    const qa_actor_record *actor = qa_actors_get(qa_session_actors(vm->options.host.session), expected);
    if (!actor || (binding->kind != QA_QC_SLOT_OWNED && binding->kind != QA_QC_SLOT_BORROWED) ||
        !qa_actor_id_equal(binding->actor, expected) || binding->owner != actor->owner ||
        binding->source_slot != (actor->has_source ? actor->source_slot : 0) ||
        (binding->kind == QA_QC_SLOT_OWNED && (actor->owner != vm->options.host.owner ||
         !actor->has_source || actor->source_slot != slot)))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot, "QC appearance lost its full physical actor binding");
    if (!qc_entity_range(vm, slot, 0, 0, error)) return false;
    const uint8_t *edict = vm->entities + (size_t)slot * vm->layout.stride_bytes;
    if (qa_load_u32le(edict) != 0)
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot, "QC appearance physical edict is free");
    const qa_qc_definition *field = qa_qc_program_find_field(vm->program, "model");
    if (!field || field->type != QA_QC_STRING)
        return qc_fail(error, QA_ERROR_FORMAT, slot, "QC appearance has no source model string definition");
    if (!qc_entity_range(vm, slot, field->offset, 1, error)) return false;
    qa_qc_visual value = {0};
    if (!qa_qc_string(vm, qc_load_int(qc_entity_words_const(vm, slot), field->offset), &value.model, error) ||
        !scalar(vm, slot, "modelindex", false, &value.model_index, error) ||
        !scalar(vm, slot, "frame", false, &value.frame, error) ||
        !scalar(vm, slot, "skin", false, &value.skin, error) ||
        !scalar(vm, slot, "colormap", false, &value.colormap, error) ||
        !scalar(vm, slot, "effects", false, &value.effects, error) ||
        !scalar(vm, slot, "alpha", true, &value.alpha, error) ||
        !scalar(vm, slot, "scale", true, &value.scale, error)) return false;
    *out = value;
    return true;
}
