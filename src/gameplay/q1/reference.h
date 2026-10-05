#ifndef QA_Q1_REFERENCE_H
#define QA_Q1_REFERENCE_H

#include "qa/game_q1.h"

typedef qa_actor_reference q1_ref;
q1_ref q1_ref_from(const qa_q1_game *, qa_actor_id);
qa_actor_id q1_ref_actor(const qa_q1_game *, q1_ref);
q1_ref q1_ref_source(const qa_q1_game *, uint32_t);
static inline bool q1_ref_present(q1_ref reference) {
    return reference.kind == QA_ACTOR_REFERENCE_SOURCE ? reference.value.source.slot != 0 :
        qa_actor_reference_present(reference);
}
static inline uint32_t q1_ref_ordinal(q1_ref reference) {
    return reference.kind == QA_ACTOR_REFERENCE_SOURCE ? reference.value.source.slot : 0;
}
static inline bool q1_ref_equal(q1_ref left, q1_ref right) {
    return (!q1_ref_present(left) && !q1_ref_present(right)) ||
        qa_actor_reference_equal(left, right);
}

#endif
