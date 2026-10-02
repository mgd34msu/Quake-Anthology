#ifndef QA_FRONTEND_RESTART_BINDING_H
#define QA_FRONTEND_RESTART_BINDING_H
#include "internal.h"
bool frontend_restart_binding_create(qa_frontend *,qa_error *);
bool frontend_restart_binding_destroy(qa_frontend *,qa_error *);
bool frontend_restart_binding_checkpoint(const qa_frontend *,qa_buffer *,qa_error *);
/* Create and import before actual COMMANDS decoding registers the handlers. */
bool frontend_restart_binding_restore(qa_frontend *,qa_bytes,qa_error *);
void frontend_restart_binding_rebind(qa_frontend *);
#endif
