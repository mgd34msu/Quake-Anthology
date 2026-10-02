#ifndef QA_FRONTEND_RESTART_H
#define QA_FRONTEND_RESTART_H
#include "internal.h"
#include "qa/settings.h"
#include "qa/source_save.h"

typedef struct frontend_restart frontend_restart;
typedef struct frontend_restart_options {
    qa_frontend *frontend;
    /* The actual physical client/device settings owner. Source GAME cvars do
     * not stand in for this registry. */
    qa_cvars *cvars;
    const char *const *latched[3];
    size_t latched_count[3];
    void *context;
    bool (*current)(void *,const qa_command_context *,qa_error *);
    /* The actual pending source or advancing captured release may stage this
     * invocation in its retained canonical ticket before native side effects.
     * A successful false receipt leaves the ordinary request path admitted. */
    bool (*stage_input)(void *,const qa_command_invocation *,bool *staged,qa_error *);
    bool (*save_context)(void *,qa_source_save_io *,qa_command_context *);
    bool (*prepare_video)(void *,void **ticket,qa_error *);
    bool (*validate_video)(void *,void *ticket,qa_error *);
    bool (*reopen_video)(void *,void *ticket,qa_error *);
    void (*release_video)(void *,void *ticket);
} frontend_restart_options;
frontend_restart *frontend_restart_create(const frontend_restart_options *,qa_error *);
bool frontend_restart_destroy(frontend_restart *,qa_error *);
bool frontend_restart_register(frontend_restart *,qa_console *,qa_error *);
/* Call only after callbacks/source frames and native presentation have
 * returned. Resource preparation finishes before window publication. */
bool frontend_restart_drain(frontend_restart *,qa_error *);
bool frontend_restart_idle(const frontend_restart *);
bool frontend_restart_checkpoint(const frontend_restart *,qa_buffer *,qa_error *);
bool frontend_restart_restore(frontend_restart *,qa_bytes,qa_error *);
void frontend_restart_rebind(frontend_restart *,qa_frontend *,void *callback_context);
#endif
