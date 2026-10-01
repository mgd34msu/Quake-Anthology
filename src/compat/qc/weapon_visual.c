#include "visual_actor.h"
#include "qa/qc_weapon_visual.h"

static const qa_qc_definition *field(const qa_qc_instance *vm, uint32_t slot,
    const char *name, qa_qc_value_type type, uint32_t words, bool *present, qa_error *error)
{
    const qa_qc_definition *value = qa_qc_program_find_field(vm->program, name);
    *present = value != NULL;
    if (!value) return NULL;
    if (value->type != type) {
        qc_fail(error, QA_ERROR_FORMAT, slot, "QC weapon appearance has an incompatible source field");
        return NULL;
    }
    if (!qc_entity_range(vm, slot, value->offset, words, error)) return NULL;
    return value;
}
bool qa_qc_weapon_visual_read(const qa_qc_instance *vm, uint32_t slot,
    qa_actor_id expected, qa_qc_weapon_visual *out, qa_error *error)
{
    if (!out) return qc_fail(error, QA_ERROR_ARGUMENT, slot, "QC weapon appearance requires its output");
    if (!qc_visual_actor(vm, slot, expected, error)) return false;
    qa_qc_weapon_visual value = {0};
    const uint8_t *words = qc_entity_words_const(vm, slot);
    const qa_qc_definition *source = field(vm, slot, "weaponmodel", QA_QC_STRING, 1, &value.has_model, error);
    if (value.has_model && (!source || !qa_qc_string(vm, qc_load_int(words, source->offset), &value.model, error))) return false;
    source = field(vm, slot, "weaponframe", QA_QC_FLOAT, 1, &value.has_frame, error);
    if (value.has_frame) {
        if (!source) return false;
        value.frame = qc_load_float(words, source->offset);
        if (!isfinite(value.frame)) return qc_fail(error, QA_ERROR_FORMAT, slot, "QC weapon frame is nonfinite");
    }
    source = field(vm, slot, "punchangle", QA_QC_VECTOR, 3, &value.has_punch_angle, error);
    if (value.has_punch_angle) {
        if (!source) return false;
        value.punch_angle = qa_v3(qc_load_float(words, source->offset), qc_load_float(words, source->offset + 1),
            qc_load_float(words, source->offset + 2));
        if (!qa_vec_finite(value.punch_angle)) return qc_fail(error, QA_ERROR_FORMAT, slot, "QC weapon punch angle is nonfinite");
    }
    *out = value; return true;
}
