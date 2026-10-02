#ifndef QA_FRONTEND_NETWORK_RECIPIENT_H
#define QA_FRONTEND_NETWORK_RECIPIENT_H
#include "qa/frontend.h"
#include "qa/application_client.h"
typedef struct frontend_network_client_recipient {
    qa_application_client_source source;
    bool ready;
} frontend_network_client_recipient;
bool frontend_network_client_recipient_read(const qa_frontend *,uint32_t physical_seat,
    frontend_network_client_recipient *,bool *present,qa_error *);
/* Cold input restores a retained ALL recipient through physical retirement
 * custody. This receipt grants no live connection or command authority. */
bool frontend_network_client_retired_recipient_read(const qa_frontend *,uint32_t physical_seat,
    frontend_network_client_recipient *,bool *present,qa_error *);
bool frontend_network_client_recipient_current(const qa_frontend *,uint32_t physical_seat,
    const frontend_network_client_recipient *);
bool frontend_network_client_retirement_current(const qa_frontend *,
    const qa_application_client_source *,const qa_console *,const qa_command_context *,qa_error *);
bool frontend_network_client_restore_abort_ready(const qa_frontend *,
    const qa_application_client_source *,qa_error *);
#endif
