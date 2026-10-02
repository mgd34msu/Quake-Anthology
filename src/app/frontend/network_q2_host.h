#ifndef QA_FRONTEND_NETWORK_Q2_HOST_H
#define QA_FRONTEND_NETWORK_Q2_HOST_H
#include "internal.h"
#include "qa/application_network_q2.h"
#include "qa/network_q2_bootstrap.h"
#include "qa/server_admin.h"
#include "qa/network_q2_kex.h"
typedef struct frontend_network_q2_host frontend_network_q2_host;
typedef struct frontend_network_q2_host_options {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_server_admin *admin;
    qa_net_protocol_id protocol;
    qa_sha256_digest composition;
    void *context;
    bool (*current)(void *,const frontend_network_q2_host *);
    qa_q2_random_fn random;
    qa_kex_lan *lobby;
} frontend_network_q2_host_options;
bool frontend_network_q2_host_create(const frontend_network_q2_host_options *,frontend_network_q2_host **,qa_error *);
bool frontend_network_q2_host_receive(frontend_network_q2_host *,const qa_net_datagram *,bool *,qa_error *);
bool frontend_network_q2_host_admit(frontend_network_q2_host *,const qa_net_connect *,bool *,qa_error *);
bool frontend_network_q2_host_tick(frontend_network_q2_host *,uint64_t,qa_error *);
bool frontend_network_q2_host_publish(frontend_network_q2_host *,uint64_t,qa_error *);
void frontend_network_q2_host_disconnected(frontend_network_q2_host *,qa_net_client_id);
bool frontend_network_q2_host_idle(const frontend_network_q2_host *);
bool frontend_network_q2_host_destroy(frontend_network_q2_host **,qa_error *);
#endif
