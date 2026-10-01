#ifndef QA_APPLICATION_NATIVE_Q2_CLIENT_H
#define QA_APPLICATION_NATIVE_Q2_CLIENT_H

#include "qa/application_native_q2_presentation.h"

typedef struct qa_application_native_q2_client {
    qa_actor_id actor;
    uint32_t seat, client_slot;
} qa_application_native_q2_client;

/* client_slot is the actual zero-based GAME client index. Missing, remote,
 * bot, retired and pre-Begin seats return found=false. No score, body or GAME
 * callback runs, and the output remains unchanged without a physical row. */
bool qa_application_native_q2_presentation_local(qa_application *,
    const qa_application_native_q2_presentation *, uint32_t seat,
    qa_application_native_q2_client *, bool *found, qa_error *);

#endif
