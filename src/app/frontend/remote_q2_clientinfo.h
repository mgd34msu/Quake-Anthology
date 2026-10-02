#ifndef QA_FRONTEND_REMOTE_Q2_CLIENTINFO_H
#define QA_FRONTEND_REMOTE_Q2_CLIENTINFO_H
#include "remote_q2_client.h"
typedef struct remote_q2_clientinfo {
    char model[256], skin[256], weapon[256];
    bool valid;
} remote_q2_clientinfo;
bool remote_q2_clientinfo_read(frontend_remote_q2 *, uint32_t player, uint32_t weapon,
    remote_q2_clientinfo *, qa_error *);
bool remote_q2_clientinfo_prepare(frontend_remote_q2 *, qa_error *);
#endif
