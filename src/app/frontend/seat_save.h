#ifndef QA_FRONTEND_SEAT_SAVE_H
#define QA_FRONTEND_SEAT_SAVE_H
#include "internal.h"
#include "qa/input_save.h"
#include "qa/console_seat_save.h"
#include "qa/application_client.h"

/* Exact installed service bindings on each stable heap seat. Descriptor
 * decode observes prepared owners and invokes no input/console/UI callbacks. */
qa_input_checkpoint_refs frontend_seat_input_refs(frontend_seat *);
qa_seat_console_save_resolvers frontend_seat_console_refs(frontend_seat *);
bool frontend_seat_hud_options(frontend_seat *, qa_hud_options *, qa_error *);
bool frontend_seat_ui_clock_ready(void *,const qa_ui *,double (*)(void *),void *,qa_error *);
bool frontend_seat_client_recipient_ready(qa_frontend *,uint32_t,const qa_application_client_source *,qa_error *);
bool frontend_seat_client_recipient_ready_is(const qa_frontend *,uint32_t,const qa_application_client_source *);
void frontend_seat_client_recipient_publish(qa_frontend *,uint32_t,const qa_application_client_source *);
bool frontend_seats_recipients_restore(qa_frontend *,qa_error *);
#endif
