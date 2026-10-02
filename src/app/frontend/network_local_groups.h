#ifndef QA_FRONTEND_NETWORK_LOCAL_GROUPS_H
#define QA_FRONTEND_NETWORK_LOCAL_GROUPS_H
#include "qa/frontend.h"

/* Release the actual offline human transport groups before their GAME dies. */
bool frontend_network_local_groups_retire(qa_frontend *,qa_error *);
#endif
