#include "visual_actor.h"
#include "qa/qc_observation.h"

bool qa_qc_actor_observation_slot(const qa_qc_instance *vm, qa_actor_id actor,
    uint32_t *out, qa_error *error)
{
    if (!vm || !out || !qa_qc_idle(vm) || vm->destroying)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "QC observation requires its returned instance");
    uint32_t found = 0;
    for (uint32_t slot = 1; slot < vm->entity_count; ++slot) {
        const qc_slot *binding = &vm->slots[slot];
        if ((binding->kind != QA_QC_SLOT_OWNED && binding->kind != QA_QC_SLOT_BORROWED) ||
            !qa_actor_id_equal(binding->actor, actor)) continue;
        if (found || !qc_visual_actor(vm, slot, actor, error))
            return qc_fail(error, QA_ERROR_FORMAT, slot, "QC observation has no unique retained actor binding");
        found = slot;
    }
    if (!found) return qc_fail(error, QA_ERROR_NOT_FOUND, 0, "QC observation actor has no retained projection");
    *out = found; return true;
}

bool qa_qc_actor_observation_float(const qa_qc_instance *vm, uint32_t slot,
    qa_actor_id actor, uint32_t word, float *out, qa_error *error)
{
    if (!out || !qc_visual_actor(vm, slot, actor, error) || !qc_entity_range(vm, slot, word, 1, error)) return false;
    float value = qc_load_float(qc_entity_words_const(vm, slot), word);
    if (!isfinite(value)) return qc_fail(error, QA_ERROR_FORMAT, word, "QC observed scalar is nonfinite");
    *out = value; return true;
}

bool qa_qc_actor_observation_vector(const qa_qc_instance *vm, uint32_t slot,
    qa_actor_id actor, uint32_t word, qa_vec3 *out, qa_error *error)
{
    if (!out || !qc_visual_actor(vm, slot, actor, error) || !qc_entity_range(vm, slot, word, 3, error)) return false;
    const uint8_t *words = qc_entity_words_const(vm, slot);
    qa_vec3 value = qa_v3(qc_load_float(words, word), qc_load_float(words, word + 1), qc_load_float(words, word + 2));
    if (!qa_vec_finite(value)) return qc_fail(error, QA_ERROR_FORMAT, word, "QC observed vector is nonfinite");
    *out = value; return true;
}
