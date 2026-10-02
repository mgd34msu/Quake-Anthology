#ifndef QA_NETWORK_Q2_BOOTSTRAP_SAVE_H
#define QA_NETWORK_Q2_BOOTSTRAP_SAVE_H
#include "qa/network_q2_bootstrap.h"
#include "qa/source_save.h"
bool qa_network_q2_bootstrap_capture(qa_network_q2_bootstrap *, qa_buffer *, qa_error *);
/* Candidate options bind actual Source operations and the retained runtime.
 * These reconstruct no admission, identity callback, constructor or send. */
bool qa_network_q2_bootstrap_restore_server(qa_network_runtime *,
    const qa_q2_server_bootstrap_options *, qa_bytes, qa_network_q2_bootstrap **, qa_error *);
bool qa_network_q2_bootstrap_restore_client(qa_network_runtime *,
    const qa_q2_client_bootstrap_options *, qa_bytes, qa_network_q2_bootstrap **, qa_error *);
bool qa_q2_save_challenges(qa_source_save_io *, qa_q2_random_fn, void *, qa_q2_challenges **);
#endif
