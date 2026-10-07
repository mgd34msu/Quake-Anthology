#ifndef QA_FRONTEND_SOURCE_ADMIN_H
#define QA_FRONTEND_SOURCE_ADMIN_H
#include "qa/frontend.h"
#include "qa/console.h"
#include "qa/settings.h"
typedef struct frontend_source_admin frontend_source_admin;
bool frontend_source_admin_create(qa_frontend *,qa_application *,qa_console *,qa_cvars *,
    const qa_command_context *,frontend_source_admin **,qa_error *);
bool frontend_source_admin_bind(frontend_source_admin *,qa_application *,qa_console *,qa_cvars *,
    const qa_command_context *,qa_error *);
bool frontend_source_admin_unbind(frontend_source_admin *,const qa_cvars *,qa_error *);
void frontend_source_admin_rebind(frontend_source_admin *,qa_frontend *);
bool frontend_source_admin_dispatch(frontend_source_admin *,const qa_command_invocation *,
    size_t skip,bool *handled,qa_error *);
bool frontend_source_admin_adopt(frontend_source_admin *,qa_error *);
bool frontend_source_admin_destroy(frontend_source_admin *,qa_error *);
const qa_console *frontend_source_admin_console(const frontend_source_admin *);
bool frontend_source_admin_checkpoint(const frontend_source_admin *,qa_application *,qa_console *,qa_cvars *,
    const qa_command_context *,qa_settings_store,qa_buffer *,qa_error *);
/* Pure private decode; the actual restored Source and directory capabilities
 * are joined only by finish_restore, without commands or preference reads. */
bool frontend_source_admin_restore(qa_frontend *,qa_bytes,frontend_source_admin **,qa_error *);
bool frontend_source_admin_finish_restore(frontend_source_admin *,qa_application *,qa_console *,qa_cvars *,
    const qa_command_context *,qa_settings_store,qa_error *);
#endif
