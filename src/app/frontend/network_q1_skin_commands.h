#ifndef QA_FRONTEND_NETWORK_Q1_SKIN_COMMANDS_H
#define QA_FRONTEND_NETWORK_Q1_SKIN_COMMANDS_H
#include "remote_q1_client.h"

/* Called by the genuine physical CLIENT console source dispatcher. The
 * invocation keeps its captured caller origin; this helper never forwards it
 * to a local GAME or constructs a replacement console context. */
qa_command_result frontend_network_q1_skin_command(frontend_remote_q1 *,
    const qa_command_invocation *, qa_error *);
#endif
