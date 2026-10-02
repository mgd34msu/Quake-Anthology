#ifndef QA_APPLICATION_NATIVE_Q2_CLIENT_H
#define QA_APPLICATION_NATIVE_Q2_CLIENT_H

#include "qa/application_native_q2_presentation.h"

typedef struct qa_application_native_q2_client {
    qa_actor_id actor;
    uint32_t seat, client_slot;
} qa_application_native_q2_client;

typedef struct qa_application_native_q2_player_sample {
    qa_vec3 origin, view_angles, view_offset, gun_offset;
    bool present;
} qa_application_native_q2_player_sample;

/* client_slot is the actual zero-based GAME client index. Missing, remote,
 * bot, retired and pre-Begin seats return found=false. No score, body or GAME
 * callback runs, and the output remains unchanged without a physical row. */
bool qa_application_native_q2_presentation_local(qa_application *,
    const qa_application_native_q2_presentation *, uint32_t seat,
    qa_application_native_q2_client *, bool *found, qa_error *);
/* Reads only the supplied actual local physical client. An unbuilt VIEW
 * remains absent, without substituting a camera or canonical body. */
bool qa_application_native_q2_presentation_player(qa_application *,
    const qa_application_native_q2_presentation *, const qa_application_native_q2_client *,
    qa_application_native_q2_player_sample *, qa_error *);

#endif
