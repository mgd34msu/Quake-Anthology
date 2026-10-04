#ifndef QA_APPLICATION_CLIENT_EVENTS_H
#define QA_APPLICATION_CLIENT_EVENTS_H

#include "internal.h"

/* Publish the primary Source's returned dictionary. Pending clients retain it
 * for their existing Begin admission; admitted clients notify shared listeners. */
bool application_client_userinfo_publish(qa_application *, qa_actor_id, const char *, qa_error *);
/* The Source has already published the canonical userinfo and completed its
 * own callback. Notify the other physical, admitted client listeners once. */
bool application_client_userinfo_changed(qa_application *, qa_actor_id, qa_error *);
/* The primary Source has completed disconnect; both canonical handles and
 * declared client projections remain live until these callbacks return. */
bool application_client_declared_disconnect(qa_application *, qa_actor_id, qa_error *);

#endif
