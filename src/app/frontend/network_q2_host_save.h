#ifndef QA_FRONTEND_NETWORK_Q2_HOST_SAVE_H
#define QA_FRONTEND_NETWORK_Q2_HOST_SAVE_H
#include "network_q2_host.h"

bool frontend_network_q2_host_restore_admit(frontend_network_q2_host *,
    const qa_net_connect *,bool *recognized,qa_error *);
bool frontend_network_q2_host_qualified(const frontend_network_q2_host *,const qa_network_runtime *,
    bool complete,qa_error *);
bool frontend_network_q2_host_importing(const frontend_network_q2_host *);
bool frontend_network_q2_host_imported(const frontend_network_q2_host *);
bool frontend_network_q2_host_publication_ready(const frontend_network_q2_host *,qa_error *);
void frontend_network_q2_host_publish_import(frontend_network_q2_host *);
/* Disposes isolated import custody without Source admission/disconnect replay. */
void frontend_network_q2_host_restore_abort(frontend_network_q2_host **);
#endif
