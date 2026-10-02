#ifndef QA_FRONTEND_NETWORK_Q1_CLIENT_H
#define QA_FRONTEND_NETWORK_Q1_CLIENT_H
#include "remote_q1_source.h"
#include "remote_q1_skins.h"
#include "qa/network_q1_connection_save.h"

typedef struct frontend_network_q1_client frontend_network_q1_client;
typedef struct frontend_network_q1_client_options {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_net_address remote;
    qa_net_protocol_id protocol;
    qa_product_id profile;
    uint32_t physical_seat;
    uint16_t qport;
    qa_network_q1_client_policy policy;
    /* The actual configuration owner supplies programme completion and its
     * canonical routing. Metadata, physical identity and declarations are
     * produced by this CLIENT factory. */
    frontend_client_source_options configuration;
    void *context;
    bool (*current)(void *, const frontend_network_q1_client *);
    bool (*download_nonce)(void *, uint64_t *, qa_error *);
    bool (*downloads)(void *, bool *allowed, bool *demo, qa_error *);
    bool (*service)(void *, const qa_application_client_source *, qa_net_protocol_id,
        const qa_nq_message *, double, uint64_t, qa_error *);
} frontend_network_q1_client_options;

bool frontend_network_q1_client_create(const frontend_network_q1_client_options *,
    frontend_network_q1_client **, qa_error *);
bool frontend_network_q1_client_receive(frontend_network_q1_client *, const qa_net_datagram *,
    bool *recognized, qa_error *);
bool frontend_network_q1_client_tick(frontend_network_q1_client *, uint64_t now_ns, qa_error *);
bool frontend_network_q1_client_admit(frontend_network_q1_client *, const qa_net_connect *,
    bool *recognized, qa_error *);
void frontend_network_q1_client_disconnected(frontend_network_q1_client *, qa_net_client_id);
bool frontend_network_q1_client_idle(const frontend_network_q1_client *);
bool frontend_network_q1_client_destroy(frontend_network_q1_client **, qa_error *);
bool frontend_network_q1_client_source_read(const frontend_network_q1_client *,
    frontend_remote_q1_source_view *, qa_error *);
bool frontend_network_q1_client_content_visit(const frontend_network_q1_client *,
    const qa_application_content_visitor *, qa_error *);
#endif
