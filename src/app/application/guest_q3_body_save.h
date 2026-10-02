#ifndef QA_APPLICATION_GUEST_Q3_BODY_SAVE_H
#define QA_APPLICATION_GUEST_Q3_BODY_SAVE_H
#include "guest_q3_body.h"

/* Pure typed transforms. The enclosing artifact graph owns and qualifies the
 * actual cgame-presentation.json resource/recipe. Import never parses JSON,
 * acquires content, enters source functions, or repeats CGAME Init. */
bool application_q3_body_profile_checkpoint(const qa_qvm_image *, qa_qvm_abi,
    const char *artifact_path, const application_q3_body_profile *, qa_buffer *, qa_error *);
bool application_q3_body_profile_restore(const qa_qvm_image *, qa_qvm_abi,
    const char *artifact_path, qa_bytes, application_q3_body_profile *, qa_error *);

/* This is the body subset, not a complete-executor admission. The outer owner
 * merges its IDs/descriptors with input/equipment/weapon/combat/pickup hooks
 * and qualifies/remaps the whole physical VM once before restoring RAM. */
bool application_q3_body_checkpoint(const application_q3_body *, qa_buffer *, qa_error *);
bool application_q3_body_saved_read(const application_q3_body_profile *, qa_bytes,
    application_q3_body_saved *, qa_error *);
void application_q3_body_saved_free(application_q3_body_saved *);

#endif
