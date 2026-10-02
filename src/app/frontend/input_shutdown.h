#ifndef QA_FRONTEND_INPUT_SHUTDOWN_H
#define QA_FRONTEND_INPUT_SHUTDOWN_H
#include "internal.h"
#include "qa/application_engine_shutdown.h"

typedef struct frontend_input_shutdown frontend_input_shutdown;
/* Prepare before the actual ENGINE detach. Every constructed physical seat
 * retains its genuine all-input release; failure may return a retained owner. */
bool frontend_input_shutdown_prepare(qa_frontend *,double now_ms,
    frontend_input_shutdown **,qa_error *);
/* Pure actual all-scope lease association for enclosing ENGINE detach. This
 * admits only the retained physical owner, not source history consumption. */
bool frontend_input_shutdown_ready(const frontend_input_shutdown *,const qa_frontend *,qa_error *);
/* Actual successful ALL capture receipt; it does not qualify parent ownership. */
bool frontend_input_shutdown_prepared(const frontend_input_shutdown *);
/* Execute only at a returned live-source boundary. Waits report complete=false;
 * a failed entered programme remains retained and is never replayed. */
bool frontend_input_shutdown_advance(frontend_input_shutdown *,bool *complete,qa_error *);
/* Final ENGINE detach disposes retained history under the actual loan, after
 * pure qualification of every seat and captured programme. No dispatch runs. */
bool frontend_input_shutdown_retire(frontend_input_shutdown *,
    const qa_application_engine_shutdown *,qa_error *);
bool frontend_input_shutdown_abort(frontend_input_shutdown *,qa_error *);
bool frontend_input_shutdown_destroy(frontend_input_shutdown **,qa_error *);
#endif
