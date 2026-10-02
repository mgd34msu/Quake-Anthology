#ifndef QA_FRONTEND_REMOTE_Q3_SESSION_H
#define QA_FRONTEND_REMOTE_Q3_SESSION_H

#include "remote_q3_runtime.h"
#include "remote_q3_modules.h"
#include "network_content_q3.h"

/* The received SDK recipe selects the actual executor. Constructor resources
 * remain reachable through out after any entered allocation or Init failure. */
bool frontend_remote_q3_session_create(qa_frontend *,const frontend_network_client_domain *,
    const qa_application_q3_remote_init *,frontend_remote_q3 **,qa_error *);
/* Connecting UI has its own actual absent-map resources and transport. Both
 * outputs retain partial construction for modules-first checked retirement. */
bool frontend_remote_q3_session_create_initial(qa_frontend *,const frontend_network_client_attempt *,
    frontend_remote_q3_initial **,frontend_remote_q3_modules **,qa_error *);
bool frontend_remote_q3_session_retire_initial(frontend_remote_q3_initial **,
    frontend_remote_q3_modules **,qa_error *);
typedef struct frontend_remote_q3_session_view {
    frontend_remote_q3_resources resources;
    frontend_remote_q3_modules *modules;
    application_native_q3_client_modules *module_owner;
    frontend_remote_q3_runtime *runtime;
    frontend_remote_q3_frame *frames;
    bool pure;
} frontend_remote_q3_session_view;
/* Pure observation supplies real parents to Network's paired predictor
 * constructor. It does not admit a Draw, transport send, or media publication. */
bool frontend_remote_q3_session_read(const frontend_remote_q3 *,frontend_remote_q3_session_view *,qa_error *);
bool frontend_remote_q3_session_current(const frontend_remote_q3_session_view *);
/* Actual content callbacks borrow the retained role media array. They do not
 * recollect journals, replace a module artifact, or change content readiness. */
bool frontend_remote_q3_session_native_media_read(frontend_remote_q3 *,
    frontend_q3_content_native_receipt *,frontend_q3_content_role_receipt *,qa_error *);
bool frontend_remote_q3_session_modules_media_read(frontend_remote_q3 *,
    const application_native_q3_client_modules **,frontend_q3_content_role_receipt *,
    frontend_q3_content_role_receipt *,qa_error *);
/* The caller supplies its genuine paired predictor. Pure acquired CGAME uses
 * its own SDK prediction; compiled CGAME replays the supplied native owner. */
bool frontend_remote_q3_session_draw(frontend_remote_q3 *,frontend_remote_prediction *,uint32_t stereo,
    qa_audio_listener *,bool *has_listener,qa_error *);

#endif
