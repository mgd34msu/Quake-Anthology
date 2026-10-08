#ifndef APPLICATION_NETWORK_Q1_SIGNON_H
#define APPLICATION_NETWORK_Q1_SIGNON_H
#include "internal.h"

/* Retain actual committed Q1 signon emissions by their page lease. Persistent
 * views use event_id zero and survive retirement of the transient lookup. */
bool application_q1_signon_retain(application_provider *, const qa_application_protocol_event *, qa_error *);
void application_q1_signon_reset(qa_application *);
void application_q1_signon_drop(qa_application *, qa_actor_owner);
void application_q1_signon_destroy(qa_application *);
bool application_q1_signon_capture(qa_application *, qa_buffer *, qa_error *);
bool application_q1_signon_restore(qa_application *, qa_bytes, qa_error *);
size_t application_q1_signon_count(const qa_application *, qa_actor_owner);
bool application_q1_signon_at(const qa_application *, qa_actor_owner, size_t,
    qa_application_protocol_event *, qa_error *);
#endif
