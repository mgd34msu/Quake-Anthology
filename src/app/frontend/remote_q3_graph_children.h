#ifndef QA_FRONTEND_REMOTE_Q3_GRAPH_CHILDREN_H
#define QA_FRONTEND_REMOTE_Q3_GRAPH_CHILDREN_H

#include "remote_q3_services.h"

typedef struct frontend_remote_q3_graph_children frontend_remote_q3_graph_children;
typedef enum frontend_remote_q3_graph_child {
    FRONTEND_REMOTE_Q3_CHILD_CLIENT,
    FRONTEND_REMOTE_Q3_CHILD_SOURCE,
    FRONTEND_REMOTE_Q3_CHILD_MEDIA,
    FRONTEND_REMOTE_Q3_CHILD_CLIENTS,
    FRONTEND_REMOTE_Q3_CHILD_RUNTIME,
    FRONTEND_REMOTE_Q3_CHILD_FRAME,
    FRONTEND_REMOTE_Q3_CHILD_COUNT
} frontend_remote_q3_graph_child;

/* The compiled owner uses its genuine frontend and asset capture leases.
 * Shared image/model/world/audio owners are encoded by their dictionaries;
 * these children retain private continuation and immutable animation refs. */
bool frontend_remote_q3_graph_children_checkpoint(frontend_remote_q3 *,qa_buffer *,qa_error *);
/* Complete envelope and resource decoding precedes service allocation. The
 * input bytes, actual isolated parent and content graph outlive this plan.
 * Lower child codecs validate their opaque continuations at their real stages. */
bool frontend_remote_q3_graph_children_decode(frontend_remote_q3 *,qa_bytes,
    frontend_remote_q3_graph_children **,qa_error *);
bool frontend_remote_q3_graph_child_read(const frontend_remote_q3_graph_children *,
    frontend_remote_q3_graph_child,qa_bytes *,qa_error *);
bool frontend_remote_q3_graph_client_refs(frontend_remote_q3_graph_children *,
    q3n_client_refs *,qa_error *);
void frontend_remote_q3_graph_children_destroy(frontend_remote_q3_graph_children *);

#endif
