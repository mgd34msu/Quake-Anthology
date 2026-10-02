#ifndef QA_APPLICATION_NATIVE_Q2_PICKUPS_H
#define QA_APPLICATION_NATIVE_Q2_PICKUPS_H
#include "native_q2_protection.h"
#include "qa/source_save.h"

typedef struct application_native_q2_pickups application_native_q2_pickups;
typedef struct application_native_q2_pickups_options {
    application_native_q2_callbacks *callbacks;
    application_native_q2_protection *protection;
    qa_session *session;
    qa_pickups *pickups;
    qa_actor_owner owner;
} application_native_q2_pickups_options;

bool application_native_q2_pickups_create(const application_native_q2_pickups_options *,
    application_native_q2_pickups **, qa_error *);
bool application_native_q2_pickups_activate(application_native_q2_pickups *, qa_error *);
bool application_native_q2_pickups_bind(application_native_q2_pickups *, qa_actor_id, qa_error *);
bool application_native_q2_pickups_release(application_native_q2_pickups *, qa_actor_id, qa_error *);
bool application_native_q2_pickups_drain(application_native_q2_pickups *, qa_error *);
bool application_native_q2_pickups_idle(const application_native_q2_pickups *);
bool application_native_q2_pickups_destroy(application_native_q2_pickups **, qa_error *);
/* Canonical pickup persistence owns the registrations and resource serials.
 * This resolver only borrows matching retained actor rule contexts. */
bool application_native_q2_pickups_saved_rule(application_native_q2_pickups *, qa_actor_id,
    qa_actor_owner, uint64_t serial, uint32_t rule, qa_pickup_rule *, qa_error *);
bool application_native_q2_pickups_checkpoint(application_native_q2_pickups *, qa_buffer *, qa_error *);
bool application_native_q2_pickups_restore(application_native_q2_pickups *, qa_bytes, qa_error *);
bool application_native_q2_pickups_finish_restore(application_native_q2_pickups *, qa_error *);
#endif
