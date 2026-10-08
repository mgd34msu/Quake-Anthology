#ifndef QA_APPLICATION_CLIENT_EVENTS_H
#define QA_APPLICATION_CLIENT_EVENTS_H

#include "internal.h"

/* Publish the primary Source's returned dictionary. Pending clients retain it
 * for their existing Begin admission; admitted clients notify shared listeners. */
bool application_client_userinfo_publish(qa_application *, qa_actor_id, const char *, qa_error *);
/* The Source has already published the canonical userinfo and completed its
 * own callback. Notify the other physical, admitted client listeners once. */
bool application_client_userinfo_changed(qa_application *, qa_actor_id, qa_error *);
/* Publish this actual secondary native Q3 client's selected media at binding,
 * respawn or a returned userinfo change, without invoking guest Begin hooks. */
bool application_client_native_q3_userinfo(qa_application *, application_provider *,
    qa_actor_id, const char *userinfo, int32_t game_type, qa_error *);
/* The primary Source has completed disconnect; both canonical handles and
 * declared client projections remain live until these callbacks return. */
bool application_client_declared_disconnect(qa_application *, qa_actor_id, qa_error *);

#endif
