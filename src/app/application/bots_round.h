#ifndef APPLICATION_BOTS_ROUND_H
#define APPLICATION_BOTS_ROUND_H

#include "internal.h"
#include "qa/application_q3_round.h"

typedef struct application_bots_round application_bots_round;
typedef struct application_bots_original application_bots_original;
bool application_bots_original_prepare(qa_application *,application_publication *,
    application_bots_original **,qa_error *);
bool application_bots_original_rebind(application_bots_original *,qa_error *);
bool application_bots_original_validate_source(application_bots_original *,
    application_provider *,const qa_cvars *,qa_error *);
void application_bots_original_dispose(application_bots_original *);
void application_bots_original_detach(application_bots_original *);
/* Invoked before actual source clients, routing and actor generations retire.
 * Candidate destruction and restoration disposal do not call this hook. */
bool application_bots_shutdown(qa_application *,bool restart,qa_error *);
bool application_bots_client_shutdown(qa_application *,qa_actor_id,bool restart,qa_error *);
/* Prepared restore seats are readable before runtime construction. */
bool application_bots_actor(const qa_application *,qa_actor_id);
bool application_bots_initial_settings(qa_application *,const qa_launch_seat *,
    char *,size_t,float *,qa_error *);
bool application_bots_round_prepare(qa_application *,application_provider *,
    const qa_application_q3_round_client *,size_t,application_bots_round **,qa_error *);
bool application_bots_round_begin(application_bots_round *,qa_error *);
bool application_bots_round_bind(application_bots_round *,qa_error *);
/* The actual source owner has already completed Connect(false)/Begin. */
bool application_bots_round_reconnect(application_bots_round *,
    const qa_application_q3_round_client *,qa_actor_id,qa_error *);
bool application_bots_round_client_binding(struct application_bots *,uint32_t,
    uint32_t *,uint32_t *,bool *,qa_error *);
bool application_bots_round_reject(application_bots_round *,
    const qa_application_q3_round_client *,qa_error *);
bool application_bots_round_resume(application_bots_round *,qa_error *);
void application_bots_round_dispose(application_bots_round *);
/* Application destruction detaches an externally owned cut before free. */
void application_bots_round_detach(application_bots_round *);

#endif
