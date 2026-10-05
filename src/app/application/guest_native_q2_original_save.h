#ifndef QA_APPLICATION_GUEST_NATIVE_Q2_ORIGINAL_SAVE_H
#define QA_APPLICATION_GUEST_NATIVE_Q2_ORIGINAL_SAVE_H
#include "internal.h"
#include "qa/q2_save.h"
#include "qa/application_network_q2.h"

bool application_q2_original_source(const application_provider *);
/* These are the normal constructor's actual Init/Spawn boundaries. */
bool application_q2_original_prepare(application_provider *, qa_error *);
bool application_q2_original_game(application_provider *, qa_error *);
bool application_q2_original_level(application_provider *, qa_error *);
bool application_q2_original_player(application_provider *, uint32_t, qa_actor_id,
    bool *restored, qa_error *);
bool application_q2_original_configure(qa_application_network_q2 *, qa_error *);
void application_q2_original_dispose(qa_application *);
#endif
