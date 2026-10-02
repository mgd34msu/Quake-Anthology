#ifndef QA_APPLICATION_GUEST_Q3_VIDEO_H
#define QA_APPLICATION_GUEST_Q3_VIDEO_H
#include "qa/application.h"

typedef struct application_guest_q3_video application_guest_q3_video;
/* Publishes the retained recipe before any checked source Shutdown. A failed
 * preparation may leave a ticket which the returned caller must abort. */
bool application_guest_q3_video_prepare(qa_application *, application_guest_q3_video **, qa_error *);
bool application_guest_q3_video_current(const application_guest_q3_video *, qa_error *);
/* Constructs actual replacement hosts against the caller's current video owner.
 * Formerly initialized guests run fresh Init with their retained entered argv,
 * rechecking the genuine current gamestate. Cold guests remain uninitialized. */
bool application_guest_q3_video_reopen(application_guest_q3_video *, qa_error *);
bool application_guest_q3_video_finish(application_guest_q3_video **, qa_error *);
/* Restore the chosen video owner before abort. Missing constructors are retried;
 * refused cleanup keeps the ticket and its actual parents reachable. */
bool application_guest_q3_video_abort(application_guest_q3_video **, qa_error *);
#endif
