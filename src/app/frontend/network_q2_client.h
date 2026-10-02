#ifndef QA_FRONTEND_NETWORK_Q2_CLIENT_H
#define QA_FRONTEND_NETWORK_Q2_CLIENT_H
#include "internal.h"
#include "qa/network_q2_bootstrap.h"
#include "remote_q2_source.h"
#include "qa/application_client.h"
typedef struct frontend_network_q2_client frontend_network_q2_client;
typedef struct frontend_network_q2_client_options {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_net_address remote;
    qa_net_protocol_id protocol;
    uint16_t qport;
    uint32_t physical_seat;
    void *context;
    bool (*current)(void *, const frontend_network_q2_client *);
    bool (*download_nonce)(void *, uint64_t *, qa_error *);
    bool (*records)(void *, const qa_application_client_source *,
        const qa_q2_server_record *, size_t, qa_error *);
} frontend_network_q2_client_options;
bool frontend_network_q2_client_create(const frontend_network_q2_client_options *,
    frontend_network_q2_client **, qa_error *);
bool frontend_network_q2_client_receive(frontend_network_q2_client *,
    const qa_net_datagram *, bool *, qa_error *);
bool frontend_network_q2_client_tick(frontend_network_q2_client *, uint64_t, qa_error *);
bool frontend_network_q2_client_idle(const frontend_network_q2_client *);
bool frontend_network_q2_client_admit(frontend_network_q2_client *,
    const qa_net_connect *, bool *recognized, qa_error *);
void frontend_network_q2_client_disconnected(frontend_network_q2_client *, qa_net_client_id);
bool frontend_network_q2_client_destroy(frontend_network_q2_client **, qa_error *);
#endif
