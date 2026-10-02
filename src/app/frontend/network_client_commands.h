#ifndef QA_FRONTEND_NETWORK_CLIENT_COMMANDS_H
#define QA_FRONTEND_NETWORK_CLIENT_COMMANDS_H
#include "qa/frontend.h"
bool frontend_network_client_commands_owned(const qa_frontend *,const qa_application *,
    const qa_application_console_scope *,const qa_console *);
bool frontend_network_client_commands_capture(qa_frontend *,qa_application *,
    const qa_application_console_scope *,const qa_console *,qa_buffer *,qa_error *);
bool frontend_network_client_commands_restore(qa_frontend *,qa_application *,
    const qa_application_console_scope *,qa_console *,qa_bytes,qa_error *);
bool frontend_network_client_sources_finish_restore(qa_frontend *,qa_error *);
#endif
