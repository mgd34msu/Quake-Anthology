#ifndef QA_FRONTEND_NETWORK_VIEW_H
#define QA_FRONTEND_NETWORK_VIEW_H
#include "qa/frontend.h"

/* Fan out the authored view preference into the actual registered CLIENT row.
 * A role that has not registered cg_fov has no recipient yet. */
bool frontend_network_client_field_of_view(qa_frontend *, double, qa_error *);
#endif
