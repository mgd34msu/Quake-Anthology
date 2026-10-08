#ifndef QA_FRONTEND_NETWORK_Q2_HOST_H
#define QA_FRONTEND_NETWORK_Q2_HOST_H
#include "internal.h"
#include "qa/application_network_q2.h"
#include "qa/network_q2_bootstrap.h"
#include "qa/server_admin.h"
#include "qa/network_q2_kex.h"
#include "qa/network_kex_transport.h"
#include "demo_service.h"
typedef struct frontend_network_q2_host frontend_network_q2_host;
typedef struct frontend_network_q2_host_options {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_server_admin *admin;
    qa_net_protocol_id protocol;
    uint64_t composition;
    void *context;
    bool (*current)(void *,const frontend_network_q2_host *);
    qa_q2_random_fn random;
    qa_kex_lan *lobby;
    qa_kex_transport *transport;
} frontend_network_q2_host_options;
bool frontend_network_q2_host_create(const frontend_network_q2_host_options *,frontend_network_q2_host **,qa_error *);
bool frontend_network_q2_host_receive(frontend_network_q2_host *,const qa_net_datagram *,bool *,qa_error *);
bool frontend_network_q2_host_admit(frontend_network_q2_host *,const qa_net_connect *,bool *,qa_error *);
bool frontend_network_q2_host_tick(frontend_network_q2_host *,uint64_t,qa_error *);
bool frontend_network_q2_host_publish(frontend_network_q2_host *,uint64_t,qa_error *);
bool frontend_network_q2_host_demo_record(frontend_network_q2_host *, qa_actor_id,
    qa_fs_root *, frontend_demo_record_source *, qa_error *);
void frontend_network_q2_host_disconnected(frontend_network_q2_host *,qa_net_client_id);
bool frontend_network_q2_host_idle(const frontend_network_q2_host *);
bool frontend_network_q2_host_capture_current(const frontend_network_q2_host *,qa_error *);
bool frontend_network_q2_host_import_retirement_idle(const frontend_network_q2_host *);
void frontend_network_q2_host_admin_rebind(frontend_network_q2_host *,qa_server_admin *);
bool frontend_network_q2_host_content_visit(const frontend_network_q2_host *,
    const qa_application_content_visitor *,qa_error *);
bool frontend_network_q2_host_configs(frontend_network_q2_host *,
    const qa_q2_config_entry **,size_t *,qa_error *);
bool frontend_network_q2_host_destroy(frontend_network_q2_host **,qa_error *);
bool frontend_network_q2_host_stop(frontend_network_q2_host *,uint64_t,bool *,qa_error *);
#endif
