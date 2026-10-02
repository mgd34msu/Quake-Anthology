#ifndef QA_FRONTEND_NETWORK_RESTORE_PUBLICATION_H
#define QA_FRONTEND_NETWORK_RESTORE_PUBLICATION_H
#include "network_restore.h"

/* Read-only transport observations for the actually staged native graph.
 * These never execute reliable commands or fabricate a completed media cut. */
bool frontend_network_restore_publication_read(const qa_frontend *,
    struct q3n_remote_publication *, qa_error *);
bool frontend_network_restore_publication_current(const qa_frontend *,
    const struct q3n_remote_publication *);
bool frontend_network_restore_command_current(const qa_frontend *,
    const struct q3n_remote_command *);
#endif
