#ifndef QA_FRONTEND_SEAT_SAVE_H
#define QA_FRONTEND_SEAT_SAVE_H
#include "internal.h"
#include "qa/input_save.h"
#include "qa/application_client.h"
#include "qa/input_release.h"

/* Input readiness observes installed services on each physical seat. */
qa_input_checkpoint_refs frontend_seat_input_refs(frontend_seat *);
bool frontend_seat_client_recipient_ready(qa_frontend *,uint32_t,const qa_application_client_source *,qa_error *);
bool frontend_seat_client_recipient_ready_is(const qa_frontend *,uint32_t,const qa_application_client_source *);
void frontend_seat_client_recipient_publish(qa_frontend *,uint32_t,const qa_application_client_source *);
bool frontend_seat_engine_recipient_ready(qa_frontend *,uint32_t,qa_command_context *,qa_error *);
bool frontend_seat_engine_recipient_retirement_ready(qa_frontend *,uint32_t,const qa_input_release *,
    qa_console_release_disposition,qa_console_release_retirement_fn,void *,qa_command_context *,qa_error *);
void frontend_seat_engine_recipient_publish(qa_frontend *,uint32_t,const qa_command_context *);
#endif
