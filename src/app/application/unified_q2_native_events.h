#ifndef QA_APPLICATION_UNIFIED_Q2_NATIVE_EVENTS_H
#define QA_APPLICATION_UNIFIED_Q2_NATIVE_EVENTS_H

#include "internal.h"
#include "qa/application_native_q2_delivery.h"

void application_q2_inventory_counts(const qa_q2_game *, const qa_q2_player_event *, int16_t counts[256]);

bool application_unified_q2_native_builtin(qa_application *, const qa_builtin_event *,
    const qa_application_q2_audience *, qa_error *);
bool application_unified_q2_native_map(application_provider *, const qa_q2_map_event *,
    const qa_application_q2_audience *, qa_error *);
bool application_unified_q2_native_player(application_provider *, const qa_q2_player_event *,
    qa_error *);
bool application_unified_q2_native_visual(application_provider *, qa_actor_id,
    const qa_entity_visual *, qa_error *);
bool application_unified_q2_native_item_visibility(void *, qa_actor_id, qa_actor_id,
    bool, qa_error *);

#endif
