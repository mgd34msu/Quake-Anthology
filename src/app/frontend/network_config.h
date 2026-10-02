#ifndef QA_FRONTEND_NETWORK_CONFIG_H
#define QA_FRONTEND_NETWORK_CONFIG_H
#include "qa/frontend.h"
#include "qa/console.h"
#include "remote_config.h"
#include "qa/application_client.h"
#include "qa/application_client_prepare.h"
bool frontend_network_client_configuration_primary(const qa_frontend *,const qa_application_client_source *);
bool frontend_network_client_configuration_advance(qa_frontend *,qa_application_client_preparation *,
    bool *complete,qa_error *);

/* Borrow the installed CLIENT preparation registry. Absence before network
 * construction grants no alternate GAME or ENGINE timing authority. */
bool frontend_network_client_time_cvars_read(const qa_frontend *,
    const qa_cvars **, bool *present, qa_error *);
bool frontend_network_client_configuration(const qa_frontend *, uint32_t authored_seat,
    frontend_remote_config_view *, qa_error *);
bool frontend_network_client_configuration_read(const qa_frontend *, uint32_t authored_seat,
    frontend_remote_config_view *, bool *present, qa_error *);
/* The old published receiver remains the archive authority while a different
 * candidate routing snapshot is temporarily borrowed for configuration. */
bool frontend_network_client_previous_configuration_read(const qa_frontend *, uint32_t authored_seat,
    frontend_remote_config_view *, bool *present, qa_error *);
#endif
