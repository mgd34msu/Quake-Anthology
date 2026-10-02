#ifndef QA_FRONTEND_NETWORK_Q3_RESTART_H
#define QA_FRONTEND_NETWORK_Q3_RESTART_H
#include "qa/frontend.h"
#include "qa/application_q3_client.h"
bool frontend_network_q3_client_context_read(const qa_frontend *, qa_actor_owner,
    uint32_t, qa_application_q3_client_context *, qa_error *);
bool frontend_network_q3_client_context_current(const qa_frontend *,
    const qa_application_q3_client_context *);

bool frontend_network_q3_client_effect(qa_frontend *,
    const qa_application_q3_client_context *, qa_application_q3_client_effect,
    const char *, qa_error *);
#include "qa/network_q3_round.h"

/* Actual absent hosting returns a NULL cut. An installed incompatible client
 * or hosting dialect fails. A nonnull cut owns complete source userinfo copies
 * and fixes the real frontend/network/native client inventory until disposal.
 * Every fallible mutation below requires idle frontend/runtime/application.
 * Source reset/readmission occurs between these calls in its own operation.
 * Once begin calls mark, any subsequent failure requires actual application
 * fault/retirement; disposal frees the cut and cannot undo source or wire state. */
bool frontend_network_q3_round_prepare(qa_frontend *, qa_network_q3_round **empty, qa_error *);
size_t frontend_network_q3_round_clients(const qa_network_q3_round *,
                                         const qa_network_q3_round_client **borrowed);
qa_actor_owner frontend_network_q3_round_source_owner(const qa_network_q3_round *);
uint8_t frontend_network_q3_round_snapshot_bit(const qa_network_q3_round *);
/* After delivering the old frame, qualify the unchanged source admissions and
 * refresh only their real protocol histories before the first mutation. */
bool frontend_network_q3_round_refresh(qa_network_q3_round *, qa_error *);
bool frontend_network_q3_round_begin(qa_network_q3_round *, void (*mark)(void *),
                                    void *mark_context, qa_error *);
bool frontend_network_q3_round_bind(qa_network_q3_round *, qa_error *);
/* Queue the real map_restart reliable command before this client's source
 * reconnect. Activate only after canonical source admission/ClientBegin. */
bool frontend_network_q3_round_queue_client(qa_network_q3_round *, qa_net_client_id, qa_error *);
bool frontend_network_q3_round_activate_client(qa_network_q3_round *, qa_net_client_id, qa_error *);
bool frontend_network_q3_round_reject_client(qa_network_q3_round *, qa_net_client_id,
                                            const char *reason, qa_error *);
bool frontend_network_q3_round_finish(qa_network_q3_round *, qa_error *);
void frontend_network_q3_round_dispose(qa_network_q3_round *);

#endif
