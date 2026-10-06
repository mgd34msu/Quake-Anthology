#ifndef QA_FRONTEND_NETWORK_Q1_CLIENT_H
#define QA_FRONTEND_NETWORK_Q1_CLIENT_H
#include "remote_q1_source.h"
#include "remote_q1_skins.h"
#include "qa/input.h"
#include "qa/network_q1_channel.h"

typedef struct frontend_network_q1_client frontend_network_q1_client;
typedef struct frontend_network_q1_client_options {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_net_address remote;
    qa_net_protocol_id protocol;
    qa_product_id profile;
    uint32_t physical_seat;
    uint16_t qport;
    bool demo_playback;
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
bool frontend_network_q1_client_disconnect(frontend_network_q1_client *, const char *reason, qa_error *);
/* Physical ownership is also returned while connecting/loading. Only a real
 * active received player produces a command; no local GAME actor is read. */
bool frontend_network_q1_client_frame_time(frontend_network_q1_client *,
    const qa_cvars **,uint64_t *source_ns,bool *handled,qa_error *);
bool frontend_network_q1_client_input_prepare(const frontend_network_q1_client *,uint32_t physical_seat,
    bool *accepted,uint64_t *source_ns,uint64_t *wall_ns,qa_error *);
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
bool frontend_network_q1_client_demo_record(frontend_network_q1_client *, frontend_demo_record_source *, qa_error *);
/* The genuine reconnect constructor retains this exact recording sink until
 * its physical Source is configured and bound to the newly admitted CLIENT. */
bool frontend_network_q1_client_demo_follow(frontend_network_q1_client *, const frontend_demo_sink *, bool *attached, qa_error *);
bool frontend_network_q1_client_demo_unfollow(frontend_network_q1_client *, const frontend_demo_sink *, qa_error *);
bool frontend_network_q1_client_demo_playback(frontend_network_q1_client *, frontend_demo_reader *, frontend_demo_playback_source *, qa_error *);
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
    uint64_t composition;
    qa_net_seat_id seat;
} frontend_network_q1_client_view;
/* Retained constructor topology, including genuine pending configuration.
 * No transport current callback or native I/O occurs. */
bool frontend_network_q1_client_metadata_read(const frontend_network_q1_client *,
    frontend_network_q1_client_view *, qa_error *);
bool frontend_network_q1_client_content_visit(const frontend_network_q1_client *,
    const qa_application_content_visitor *, qa_error *);
bool frontend_network_q1_client_qualified(const frontend_network_q1_client *,const qa_network_runtime *,
    bool complete,qa_error *);
bool frontend_network_q1_client_publication_ready(const frontend_network_q1_client *,qa_error *);
void frontend_network_q1_client_publish(frontend_network_q1_client *);
#endif
