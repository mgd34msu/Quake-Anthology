#ifndef QA_APPLICATION_GUEST_Q3_COMBAT_SOURCE_H
#define QA_APPLICATION_GUEST_Q3_COMBAT_SOURCE_H

#include "guest_q3_combat_state.h"

typedef bool (*application_q3_damage_words_fn)(void *, const int32_t *, size_t, qa_error *);
uint32_t application_q3_combat_canonical_flags(const application_q3_combat_definition *, uint32_t);
uint32_t application_q3_combat_source_flags(const application_q3_combat_definition *, const qa_damage_request *, uint32_t);
bool application_q3_combat_source_damage(const application_q3_combat_actor *,
    const qa_qvm_call *parent, const qa_damage_request *, uint32_t original_flags,
    application_q3_damage_words_fn, void *, qa_error *);

#endif
