#ifndef QA_FRONTEND_NETWORK_ADMIN_H
#define QA_FRONTEND_NETWORK_ADMIN_H
#include "qa/frontend.h"
#include "qa/console.h"
#include "qa/server_admin.h"
bool frontend_network_source_admin_bind(qa_frontend *,qa_console *,uint64_t,size_t *registered,qa_error *);
bool frontend_network_source_admin_dispatch(qa_frontend *,qa_server_admin *,void *context,
    bool (*send)(void *,const qa_net_address *,qa_bytes,qa_error *),
    bool (*save_filters)(void *,qa_error *),const qa_command_invocation *,size_t skip,bool *handled,qa_error *);
bool frontend_network_admin_send(qa_frontend *,const qa_net_address *,qa_bytes,qa_error *);
bool frontend_network_admin_adopt(qa_frontend *,qa_server_admin **,uint32_t source_rotation_random,qa_error *);
bool frontend_network_admin_resume(qa_frontend *,qa_error *);
void frontend_network_source_admin_unbind(qa_console *,uint64_t,size_t registered);
#endif
