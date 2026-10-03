#ifndef QA_APPLICATION_GUEST_Q3_SAVE_H
#define QA_APPLICATION_GUEST_Q3_SAVE_H

#include "guest_q3_private.h"
#include "qa/save.h"

/* Actual application-side Q3 client/server continuation. This nested owner
 * does not replace the role executors, host handles or shared services. */
bool application_guest_q3_state_capture(application_provider *, qa_buffer *, qa_error *);
bool application_guest_q3_state_restore(application_provider *, qa_bytes, qa_error *);
struct qa_application_native_resource_refs;
bool application_guest_q3_save_capture(application_provider *,
    const struct qa_application_native_resource_refs *, qa_buffer *, qa_error *);
bool application_guest_q3_save_matches(application_provider *, qa_bytes,
    const struct qa_application_native_resource_refs *, qa_error *);
/* Actual retained capability recipe and process capsule of a decoded role. */
bool application_guest_q3_native_restore_recipe(q3g_role *,
    const qa_native_process_resources **, qa_bytes *, qa_bytes *, qa_error *);
bool application_guest_q3_save_restore(application_provider *, qa_bytes, qa_error *);
/* Observe the actual imported GAME client while live entry remains blocked
 * until whole-candidate qualification. No lifecycle callback is dispatched. */
bool application_guest_q3_save_actor_client(application_provider *, qa_actor_id, uint32_t *slot);
bool application_guest_q3_save_prepare(application_provider *, qa_world *, const qa_product *,
    const qa_launch_choices *, const qa_save_record *, qa_error *);
/* After WORLD/session, roster/control/modes/bots, primary lease promotion and
 * frontend service restoration. This also requires collective portal admission
 * and rechecks the complete private owner bytes before allowing guest entry. */
bool application_guest_q3_save_finish(application_provider *, qa_error *);

#endif
