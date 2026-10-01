#ifndef QA_NETWORK_SERVICES_SAVE_H
#define QA_NETWORK_SERVICES_SAVE_H
#include "qa/server_browser.h"
#include "qa/server_admin.h"
#include "qa/network_q3.h"

/* Full service continuations. Candidate callbacks are borrowed and never run
 * during restore. Live HTTP masters retain their separate transport identity
 * and actual partial body; the HTTP owner is restored before this consumer. */
bool qa_server_browser_checkpoint(const qa_server_browser *, qa_buffer *, qa_error *);
bool qa_server_browser_restore_checkpoint(qa_bytes, qa_http *, uint32_t,
    const qa_browser_hooks *, qa_server_browser **, qa_error *);
bool qa_server_browser_http_handoff_ready(const qa_server_browser *active,
    const qa_server_browser *candidate, qa_error *);
bool qa_server_admin_checkpoint(const qa_server_admin *, qa_buffer *, qa_error *);
bool qa_server_admin_restore_checkpoint(qa_bytes, const qa_admin_options *, qa_server_admin **, qa_error *);
bool qa_q3_server_admission_checkpoint(const qa_q3_server_admission *, qa_buffer *, qa_error *);
bool qa_q3_server_admission_restore_checkpoint(qa_bytes, const qa_q3_admission_hooks *,
    qa_q3_server_admission **, qa_error *);
#endif
