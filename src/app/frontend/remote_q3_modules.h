#ifndef QA_FRONTEND_REMOTE_Q3_MODULES_H
#define QA_FRONTEND_REMOTE_Q3_MODULES_H

#include "remote_q3_client.h"
#include "qa/application_native_q3_client_modules.h"

typedef struct frontend_remote_q3_modules frontend_remote_q3_modules;
typedef struct frontend_remote_q3_module_media {
    const frontend_remote_q3_modules *owner;
    qa_application_q3_role_receipt receipt;
    qa_q3_presentation *presentation;
    qa_q3_presentation_assets *assets;
    qa_vfs *mounts;
    uint32_t physical_seat;
} frontend_remote_q3_module_media;

/* Decoded resources precede this child. Construction retains partial outputs
 * for checked cleanup and does not enter source Init. */
bool frontend_remote_q3_modules_create(frontend_remote_q3 *, frontend_remote_q3_modules **, qa_error *);
frontend_remote_q3 *frontend_remote_q3_modules_parent(const frontend_remote_q3_modules *);
application_native_q3_client_modules *frontend_remote_q3_modules_owner(const frontend_remote_q3_modules *);
bool frontend_remote_q3_modules_idle(const frontend_remote_q3_modules *);
bool frontend_remote_q3_modules_retired(const frontend_remote_q3_modules *);
bool frontend_remote_q3_modules_destroy(frontend_remote_q3_modules **, qa_error *);
bool frontend_remote_q3_modules_media_read(const frontend_remote_q3_modules *, qa_qvm_role,
    frontend_remote_q3_module_media *, qa_error *);
bool frontend_remote_q3_modules_media_current(const frontend_remote_q3_module_media *);
/* The frame owner clears this contribution immediately before its real role
 * Draw entry, then observes only listener callbacks made by that entry. */
bool frontend_remote_q3_modules_listener_begin(frontend_remote_q3_modules *, qa_qvm_role, qa_error *);
bool frontend_remote_q3_modules_listener_read(const frontend_remote_q3_modules *, qa_qvm_role,
    qa_audio_listener *, bool *present, qa_error *);

#endif
