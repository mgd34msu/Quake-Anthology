#ifndef QA_FRONTEND_NETWORK_QW_H
#define QA_FRONTEND_NETWORK_QW_H
#include "internal.h"
#include "qa/application_network_qw.h"
#include "qa/network_qw_runtime.h"
#include "qa/server_admin.h"
typedef struct frontend_qw_host frontend_qw_host;
bool frontend_qw_create(qa_frontend *, qa_network_runtime *, qa_server_admin *,
    const qa_sha256_digest *, frontend_qw_host **, qa_error *);
void frontend_qw_destroy(frontend_qw_host *);
void frontend_qw_disconnected(frontend_qw_host *, qa_net_client_id);
bool frontend_qw_receive(frontend_qw_host *, const qa_net_datagram *, bool *, qa_error *);
bool frontend_qw_prepare(frontend_qw_host *, qa_error *);
bool frontend_qw_pump(frontend_qw_host *, qa_error *);
bool frontend_qw_publish(frontend_qw_host *, qa_error *);
bool frontend_qw_idle(const frontend_qw_host *);
bool frontend_qw_command_realtime(const frontend_qw_host *,qa_actor_owner,qa_actor_id,uint64_t *,qa_error *);
bool frontend_qw_qualified(const frontend_qw_host *, bool complete, qa_error *);
bool frontend_qw_checkpoint(frontend_qw_host *, qa_buffer *, qa_error *);
bool frontend_qw_restore(qa_frontend *, qa_network_runtime *, qa_server_admin *,
    qa_bytes, frontend_qw_host **, qa_error *);
void frontend_qw_rebind(frontend_qw_host *, qa_frontend *, qa_network_runtime *, qa_server_admin *);
bool frontend_qw_source_hooks(frontend_qw_host *, const qa_net_client *,
    qa_network_qw_server_policy *, qa_network_qw_server_hooks *, qa_qw_download_admission *, qa_error *);
#endif
