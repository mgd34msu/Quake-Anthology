#ifndef QA_FRONTEND_NETWORK_Q2_CLIENT_H
#define QA_FRONTEND_NETWORK_Q2_CLIENT_H
#include "internal.h"
#include "qa/network_q2_bootstrap.h"
#include "remote_q2_source.h"
#include "qa/application_client.h"
#include "qa/application_client_save.h"
#include "qa/network_q2_kex.h"
typedef struct frontend_network_q2_client frontend_network_q2_client;
typedef struct frontend_network_q2_client_options {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_net_address remote;
    qa_net_protocol_id protocol;
    uint16_t qport;
    uint32_t physical_seat;
    qa_kex_lan *lobby;
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
bool frontend_network_q2_client_owns_input(const frontend_network_q2_client *,uint32_t physical_seat);
bool frontend_network_q2_client_configuration_primary(const frontend_network_q2_client *,
    const qa_application_client_source *);
bool frontend_network_q2_client_configuration_advance(frontend_network_q2_client *,
    qa_application_client_preparation *,bool *,qa_error *);
bool frontend_network_q2_client_configuration_read(const frontend_network_q2_client *,
    qa_application_client_source *,bool *ready,qa_error *);
bool frontend_network_q2_client_admit(frontend_network_q2_client *,
    const qa_net_connect *, bool *recognized, qa_error *);
void frontend_network_q2_client_disconnected(frontend_network_q2_client *, qa_net_client_id);
bool frontend_network_q2_client_destroy(frontend_network_q2_client **, qa_error *);
bool frontend_network_q2_client_commands_owned(const frontend_network_q2_client *,const qa_application *,
    const qa_application_console_scope *,const qa_console *);
bool frontend_network_q2_client_commands_capture(frontend_network_q2_client *,qa_application *,
    const qa_application_console_scope *,const qa_console *,qa_buffer *,qa_error *);
bool frontend_network_q2_client_commands_restore(frontend_network_q2_client *,qa_application *,
    const qa_application_console_scope *,qa_console *,qa_bytes,qa_error *);
bool frontend_network_q2_client_finish_restore(frontend_network_q2_client *,qa_error *);
bool frontend_network_q2_client_publication_ready(const frontend_network_q2_client *,qa_error *);
void frontend_network_q2_client_publish(frontend_network_q2_client *);
bool frontend_network_q2_client_qualified(const frontend_network_q2_client *,const qa_network_runtime *,
    bool complete,qa_error *);
typedef struct frontend_network_q2_client_state {
    qa_net_address remote;
    qa_net_protocol_id protocol;
    uint16_t qport;
    uint32_t physical_seat;
    frontend_remote_q2_source_state source;
    qa_application_client_state application;
    qa_q2_connect_request negotiated;
    qa_network_q2_client_policy policy;
    qa_sha256_digest composition;
    qa_buffer receiver, bootstrap;
} frontend_network_q2_client_state;
bool frontend_network_q2_client_capture(frontend_network_q2_client *,
    const frontend_remote_q2_restore_refs *,frontend_network_q2_client_state *,qa_error *);
void frontend_network_q2_client_state_free(frontend_network_q2_client_state *);
typedef struct frontend_network_q2_client_restore {
    frontend_remote_q2_source_restore source;
    const qa_application_client_state *application;
    qa_q2_connect_request negotiated;
    qa_network_q2_client_policy policy;
    qa_sha256_digest composition;
    qa_bytes bootstrap;
} frontend_network_q2_client_restore;
bool frontend_network_q2_client_restore_prepare(const frontend_network_q2_client_options *,
    const frontend_network_q2_client_restore *,frontend_network_q2_client **,qa_error *);
bool frontend_network_q2_client_importing(const frontend_network_q2_client *);
bool frontend_network_q2_client_restore_hooks(frontend_network_q2_client *,qa_network_runtime *,
    const qa_net_client *,qa_network_q2_client_policy *,qa_network_q2_client_hooks *,qa_error *);
bool frontend_network_q2_client_source_read(const frontend_network_q2_client *,
    frontend_remote_q2_source_view *,qa_error *);
bool frontend_network_q2_client_content_visit(const frontend_network_q2_client *,
    const qa_application_content_visitor *,qa_error *);
#endif
