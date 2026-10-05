#ifndef QA_APPLICATION_PORTALS_H
#define QA_APPLICATION_PORTALS_H
#include "internal.h"

bool application_portal_q2(application_provider *, uint32_t, bool, qa_error *);
bool application_portal_q3(application_provider *, uint32_t, uint32_t, bool, qa_error *);
/* A zero provider closes every native contribution before map replacement.
 * Failure retains the journal and leaves shared geometry unchanged. */
bool application_portals_close(qa_application *, qa_actor_owner, qa_error *);
void application_portals_destroy(qa_application *);
bool application_portals_capture(qa_application *, qa_buffer *, qa_error *);
bool application_portals_restore(qa_application *, qa_bytes, qa_error *);
/* Rebuild shared counts from all restored native/guest claims and reflood.
 * Primary portal booleans and no-areas policy are preserved. */
bool application_portals_reconnect(qa_application *, qa_error *);
/* Sum actual native and guest owners against every shared contribution. */
bool application_portals_validate(qa_application *, qa_error *);
#endif
