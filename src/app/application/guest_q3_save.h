#ifndef QA_APPLICATION_GUEST_Q3_SAVE_H
#define QA_APPLICATION_GUEST_Q3_SAVE_H

#include "guest_q3_private.h"
#include "qa/save.h"

/* Actual application-side Q3 client/server continuation. This nested owner
 * does not replace the role executors, host handles or shared services. */
bool application_guest_q3_state_capture(application_provider *, qa_buffer *, qa_error *);
bool application_guest_q3_state_restore(application_provider *, qa_bytes, qa_error *);
/* Complete QVM provider schema qa.q3.qvm/1, backend qvm. Native modules require
 * their genuine private-data relocation owner and are not admitted here. */
bool application_guest_q3_save_capture(application_provider *, qa_buffer *, qa_error *);
bool application_guest_q3_save_restore(application_provider *, qa_bytes, qa_error *);
bool application_guest_q3_save_prepare(application_provider *, qa_world *, const qa_product *,
    const qa_launch_choices *, const qa_save_record *, qa_error *);
/* After WORLD/session, roster/control/modes/bots, primary lease promotion and
 * frontend service restoration. This also requires collective portal admission
 * and rechecks the complete private owner bytes before allowing guest entry. */
bool application_guest_q3_save_finish(application_provider *, qa_error *);

#endif
