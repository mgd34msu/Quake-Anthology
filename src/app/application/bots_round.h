#ifndef APPLICATION_BOTS_ROUND_H
#define APPLICATION_BOTS_ROUND_H

#include "internal.h"
#include "qa/application_q3_round.h"

typedef struct application_bots_round application_bots_round;
bool application_bots_round_prepare(qa_application *,application_provider *,
    const qa_application_q3_round_client *,size_t,application_bots_round **,qa_error *);
bool application_bots_round_begin(application_bots_round *,qa_error *);
bool application_bots_round_bind(application_bots_round *,qa_error *);
/* The actual source owner has already completed Connect(false)/Begin. */
bool application_bots_round_reconnect(application_bots_round *,
    const qa_application_q3_round_client *,qa_actor_id,qa_error *);
bool application_bots_round_reject(application_bots_round *,
    const qa_application_q3_round_client *,qa_error *);
bool application_bots_round_resume(application_bots_round *,qa_error *);
void application_bots_round_dispose(application_bots_round *);
/* Application destruction detaches an externally owned cut before free. */
void application_bots_round_detach(application_bots_round *);

#endif
