#ifndef QA_FRONTEND_NETWORK_Q1_CLIENT_H
#define QA_FRONTEND_NETWORK_Q1_CLIENT_H
#include "remote_q1_source.h"
#include "remote_q1_skins.h"
#include "qa/input.h"
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
    bool (*downloads)(void *, bool *allowed, bool *recording, bool *playback, qa_error *);
    bool (*service)(void *, const qa_application_client_source *, qa_net_protocol_id,
        const qa_nq_message *, double, uint64_t, qa_error *);
} frontend_network_q1_client_options;

/* Configuration ownership transfers when *out becomes non-NULL, including
 * partial failure. Before that point the caller owns template cancellation.
 * Policy name/spawn-parameter declarations are copied before source creation. */
bool frontend_network_q1_client_create(const frontend_network_q1_client_options *,
    frontend_network_q1_client **, qa_error *);
bool frontend_network_q1_client_receive(frontend_network_q1_client *, const qa_net_datagram *,
    bool *recognized, qa_error *);
bool frontend_network_q1_client_tick(frontend_network_q1_client *, uint64_t now_ns, qa_error *);
/* Physical ownership is also returned while connecting/loading. Only a real
 * active received player produces a command; no local GAME actor is read. */
bool frontend_network_q1_client_input(frontend_network_q1_client *,uint32_t physical_seat,
    const qa_seat_input_sample *,uint64_t physical_sequence,double source_frame_ms,bool *handled,qa_error *);
bool frontend_network_q1_client_admit(frontend_network_q1_client *, const qa_net_connect *,
    bool *recognized, qa_error *);
void frontend_network_q1_client_disconnected(frontend_network_q1_client *, qa_net_client_id);
bool frontend_network_q1_client_idle(const frontend_network_q1_client *);
bool frontend_network_q1_client_owns_input(const frontend_network_q1_client *,uint32_t physical_seat);
bool frontend_network_q1_client_retired(const frontend_network_q1_client *);
bool frontend_network_q1_client_destroy(frontend_network_q1_client **, qa_error *);
bool frontend_network_q1_client_source_read(const frontend_network_q1_client *,
    frontend_remote_q1_source_view *, qa_error *);
typedef struct frontend_network_q1_client_view {
    const frontend_network_q1_client *owner;
    frontend_client_source_view physical;
    qa_net_client_id client;
    qa_net_protocol_id protocol;
    qa_product_id profile;
    qa_net_address remote;
    uint64_t epoch;
    uint32_t physical_seat;
    uint16_t qport;
    bool configured, retired;
    qa_network_q1_client_policy policy;
    /* A nonzero client carries the actual admitted endpoint/composition,
     * including NQ's accepted server port; the single seat is retained. */
    qa_net_address connected_remote;
    qa_sha256_digest composition;
    qa_net_seat_id seat;
} frontend_network_q1_client_view;
/* Retained constructor topology, including genuine pending configuration.
 * No transport current callback or native I/O occurs. */
bool frontend_network_q1_client_metadata_read(const frontend_network_q1_client *,
    frontend_network_q1_client_view *, qa_error *);
bool frontend_network_q1_client_content_visit(const frontend_network_q1_client *,
    const qa_application_content_visitor *, qa_error *);
typedef struct frontend_network_q1_client_state {
    qa_buffer physical, receiver, handshake, controller;
} frontend_network_q1_client_state;
bool frontend_network_q1_client_capture(frontend_network_q1_client *,
    const frontend_remote_q1_restore_refs *, frontend_network_q1_client_state *, qa_error *);
void frontend_network_q1_client_state_free(frontend_network_q1_client_state *);
bool frontend_network_q1_client_restore_prepare(const frontend_network_q1_client_options *,
    const frontend_remote_q1_restore_refs *, const qa_console_save_resolvers *,
    const frontend_network_q1_client_state *, frontend_network_q1_client **, qa_error *);
bool frontend_network_q1_client_restore_hooks(frontend_network_q1_client *,
    const qa_net_client *, qa_network_q1_client_policy *, qa_network_q1_client_hooks *, qa_error *);
bool frontend_network_q1_client_restore_finish(frontend_network_q1_client *,
    const frontend_remote_q1_restore_refs *, qa_error *);
bool frontend_network_q1_client_importing(const frontend_network_q1_client *);
bool frontend_network_q1_client_qualified(const frontend_network_q1_client *,const qa_network_runtime *,
    bool complete,qa_error *);
bool frontend_network_q1_client_publication_ready(const frontend_network_q1_client *,qa_error *);
void frontend_network_q1_client_publish(frontend_network_q1_client *);
#endif
