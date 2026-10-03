#ifndef QA_QC_VISUAL_ACTOR_H
#define QA_QC_VISUAL_ACTOR_H

#include "internal.h"

static inline bool qc_retained_actor(const qa_qc_instance *vm, uint32_t slot,
    qa_actor_id expected, qa_error *error)
{
    if (!vm || vm->destroying || slot >= vm->entity_count ||
        !vm->options.host.session)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "QC observation requires its retained physical actor row");
    const qc_slot *binding = &vm->slots[slot];
    const qa_actor_record *actor = qa_actors_get(qa_session_actors(vm->options.host.session), expected);
    if (!actor || (binding->kind != QA_QC_SLOT_OWNED && binding->kind != QA_QC_SLOT_BORROWED) ||
        !qa_actor_id_equal(binding->actor, expected) || binding->owner != actor->owner ||
        binding->source_slot != (actor->has_source ? actor->source_slot : 0) ||
        (binding->kind == QA_QC_SLOT_OWNED && (actor->owner != vm->options.host.owner ||
         !actor->has_source || actor->source_slot != slot)))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot, "QC observation lost its full physical actor binding");
    if (!qc_entity_range(vm, slot, 0, 0, error)) return false;
    const uint8_t *edict = vm->entities + (size_t)slot * vm->layout.stride_bytes;
    return qa_load_u32le(edict) == 0 ||
        qc_fail(error, QA_ERROR_NOT_FOUND, slot, "QC observed physical edict is free");
}
static inline bool qc_visual_actor(const qa_qc_instance *vm, uint32_t slot,
    qa_actor_id expected, qa_error *error)
{
    if (!vm || !qa_qc_idle(vm) || !slot)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "QC appearance requires an idle physical actor row");
    return qc_retained_actor(vm, slot, expected, error);
}

#endif
