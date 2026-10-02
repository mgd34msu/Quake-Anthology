#ifndef QA_FRONTEND_NETWORK_ADMIN_H
#define QA_FRONTEND_NETWORK_ADMIN_H
#include "qa/frontend.h"
#include "qa/console.h"
bool frontend_network_source_admin_bind(qa_frontend *,qa_console *,uint64_t,size_t *registered,qa_error *);
void frontend_network_source_admin_unbind(qa_console *,uint64_t,size_t registered);
#endif
