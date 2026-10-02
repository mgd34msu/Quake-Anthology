#ifndef QA_APPLICATION_NATIVE_Q1_COMPOSITION_DEATH_H
#define QA_APPLICATION_NATIVE_Q1_COMPOSITION_DEATH_H

#include "internal.h"

bool application_native_q1_source_before_reaction(qa_application *,
    const qa_damage_outcome *, qa_error *);
bool application_native_q1_source_fired(void *, qa_actor_id, qa_item_id, qa_error *);
bool application_native_q1_source_death_bound(void *, qa_mode_id, qa_actor_id,
    bool *bound, bool *ctf, qa_error *);

#endif
