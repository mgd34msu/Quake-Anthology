#ifndef QA_FRONTEND_NETWORK_QW_LOGFILE_H
#define QA_FRONTEND_NETWORK_QW_LOGFILE_H
#include "qa/settings.h"
#include "qa/console.h"
#include "qa/persistence_content.h"
typedef struct frontend_qw_logfile frontend_qw_logfile;
bool frontend_qw_logfile_toggle(qa_settings_store, qa_console *, const qa_command_context *,
    frontend_qw_logfile **, qa_error *);
bool frontend_qw_logfile_close(frontend_qw_logfile **, qa_error *);
bool frontend_qw_logfile_enabled(const frontend_qw_logfile *);
void frontend_qw_logfile_write(frontend_qw_logfile *, const char *);
bool frontend_qw_logfile_visit(const frontend_qw_logfile *, const qa_application_content_visitor *, qa_error *);
bool frontend_qw_logfile_checkpoint(const frontend_qw_logfile *, const qa_application_content_graph *, qa_buffer *, qa_error *);
/* Claims retained VFS authority without opening a writer before late finish. */
bool frontend_qw_logfile_restore(qa_application_content_graph *, qa_bytes, frontend_qw_logfile **, qa_error *);
bool frontend_qw_logfile_finish_restore(frontend_qw_logfile *, qa_error *);
#endif
