#ifndef QA_APPLICATION_Q3_CAMPAIGN_RESULT_H
#define QA_APPLICATION_Q3_CAMPAIGN_RESULT_H

#include "qa/application_q3_campaign.h"

typedef struct qa_application_q3_campaign_result {
    qa_actor_id actor;
    uint32_t physical_client, leading_client;
    int32_t intermission_time_ms, team, score, opponent;
    bool won;
} qa_application_q3_campaign_result;

/* Observe the live source result for an admitted local human seat. Before
 * source intermission, without a viewing client, or for an external GAME,
 * succeeds with found=false. External GAME has no donor q3Source result view.
 * CTF and later compare actual source team scores; earlier modes use genuine
 * PERS_SCORE, all 64 fixed opponents, and the source sorted-client leader.
 * This reads no durable campaign result and performs no source mutation. */
bool qa_application_q3_campaign_result_read(qa_application *,
    const qa_application_q3_campaign *, uint32_t local_seat,
    qa_application_q3_campaign_result *, bool *found, qa_error *);

#endif
