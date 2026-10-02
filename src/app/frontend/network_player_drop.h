#ifndef QA_FRONTEND_NETWORK_PLAYER_DROP_H
#define QA_FRONTEND_NETWORK_PLAYER_DROP_H
#include "qa/frontend.h"
/* Returned component retirement observes its actual full player binding.
 * Transport delivery precedes the component's Source disconnect callback. */
bool frontend_network_component_drop(void *frontend,qa_actor_owner component,
    qa_actor_id actor,const char *reason,qa_error *);
#endif
