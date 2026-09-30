#ifndef QA_FRONTEND_NETWORK_NQ_H
#define QA_FRONTEND_NETWORK_NQ_H
#include "internal.h"
#include "qa/network_q1_runtime.h"
typedef struct frontend_nq_host frontend_nq_host;
bool frontend_nq_create(qa_frontend *, qa_network_runtime *, const qa_sha256_digest *, frontend_nq_host **, qa_error *);
void frontend_nq_destroy(frontend_nq_host *);
bool frontend_nq_receive(frontend_nq_host *, const qa_net_datagram *, bool *recognized, qa_error *);
bool frontend_nq_pump(frontend_nq_host *, qa_error *);
bool frontend_nq_prepare(frontend_nq_host *, qa_error *);
bool frontend_nq_tick(frontend_nq_host *, uint64_t elapsed_ns, bool retiring_map, qa_error *);
bool frontend_nq_publish(frontend_nq_host *, qa_error *);
void frontend_nq_disconnected(frontend_nq_host *, qa_net_client_id);
bool frontend_nq_idle(const frontend_nq_host *);
#endif
