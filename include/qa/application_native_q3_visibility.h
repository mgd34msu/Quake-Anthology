#ifndef QA_APPLICATION_NATIVE_Q3_VISIBILITY_H
#define QA_APPLICATION_NATIVE_Q3_VISIBILITY_H

#include "qa/application_native_q3_presentation.h"
#include "qa/network_q3.h"

typedef struct qa_application_native_q3_view {
    uint32_t physical_client;
    qa_actor_id actor;
    qa_q3_player player;
    qa_q3_visible_entities visible;
} qa_application_native_q3_view;

/* One current local recipient's genuine source view. The retained native
 * presentation keeps no GAME S/PS array or transport snapshot history. */
bool qa_application_native_q3_presentation_visible(qa_application *,
    const qa_application_native_q3_presentation *, uint32_t seat,
    qa_application_native_q3_view *, bool *found, qa_error *);

#endif
