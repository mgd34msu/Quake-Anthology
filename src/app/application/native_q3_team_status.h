#ifndef QA_APPLICATION_NATIVE_Q3_TEAM_STATUS_H
#define QA_APPLICATION_NATIVE_Q3_TEAM_STATUS_H

#include "internal.h"

/* Allocation installs no source capability. Genuine Init binds the owner;
 * candidate reconnect binds restored state without running Init or outputs. */
bool application_native_q3_team_status_create(application_provider *, qa_error *);
bool application_native_q3_team_status_initialize(application_provider *, qa_error *);
bool application_native_q3_team_status_reconnect(application_provider *, qa_error *);
bool application_native_q3_team_status_destroy(application_provider *, qa_error *);
bool application_native_q3_team_status_idle(const application_provider *);
bool application_native_q3_team_status_bound(const application_provider *);

/* These producers use the native GAME's fixed clients, source level clock,
 * published entity fields and linked target_location records. */
bool application_native_q3_team_status(application_provider *, qa_error *);
bool application_native_q3_team_location(application_provider *, qa_actor_id,
    char *, size_t capacity, bool *found, qa_error *);

#endif
