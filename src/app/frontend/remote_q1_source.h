#ifndef QA_FRONTEND_REMOTE_Q1_SOURCE_H
#define QA_FRONTEND_REMOTE_Q1_SOURCE_H
#include "client_source.h"
#include "remote_q1_client.h"
typedef struct frontend_remote_q1_source frontend_remote_q1_source;
bool frontend_remote_q1_source_defaults(qa_cvars *, uint64_t command_owner, uint32_t authored_seat, qa_error *);
/* Owns the opened selected read view; root borrows the genuine catalog's
 * shared qw write authority. The child retains both at construction. */
bool frontend_remote_q1_skin_recipe(qa_catalog *, qa_product_id selected,
    qa_vfs **owned_view, qa_fs_root **borrowed_root, qa_error *);
typedef struct frontend_remote_q1_source_options {
    frontend_client_source *physical;
    qa_net_protocol_id protocol;
    void *context;
    bool (*load_content)(void *, const qa_application_client_source *, const qa_nq_serverinfo *,
        const qa_qw_serverdata *, frontend_remote_q1_content *, qa_error *);
    bool (*service)(void *, const qa_application_client_source *, qa_net_protocol_id,
        const qa_nq_message *, double, uint64_t, qa_error *);
    bool (*disconnected)(void *, const qa_application_client_source *, const char *, qa_error *);
    bool (*sample_seconds)(void *, const qa_application_client_source *, double *, qa_error *);
    int32_t demo_forced_track;
    const struct frontend_remote_q1_skin_bindings *skin_bindings;
} frontend_remote_q1_source_options;
typedef struct frontend_remote_q1_source_view {
    const frontend_remote_q1_source *owner;
    frontend_client_source_view physical;
    frontend_remote_q1 *receiver;
    frontend_remote_q1_domain domain;
} frontend_remote_q1_source_view;
/* The Network owner constructs/configures the genuine physical CLIENT first.
 * This adapter retains that exact owner and creates no registry or console. */
bool frontend_remote_q1_source_create(qa_frontend *, const frontend_remote_q1_source_options *,
    frontend_remote_q1_source **, qa_error *);
bool frontend_remote_q1_source_read(const frontend_remote_q1_source *, frontend_remote_q1_source_view *, qa_error *);
bool frontend_remote_q1_source_metadata_read(const frontend_remote_q1_source *, frontend_remote_q1_source_view *, qa_error *);
bool frontend_remote_q1_source_current(const frontend_remote_q1_source_view *);
bool frontend_remote_q1_source_hooks(frontend_remote_q1_source *, qa_network_q1_client_hooks *, qa_error *);
bool frontend_remote_q1_source_bind(frontend_remote_q1_source *, qa_net_client_id, qa_net_seat_id, uint64_t, qa_error *);
bool frontend_remote_q1_source_entity_current(const frontend_remote_q1_source *, uint32_t, uint64_t *);
bool frontend_remote_q1_source_idle(const frontend_remote_q1_source *);
bool frontend_remote_q1_source_destroy(frontend_remote_q1_source **, qa_error *);
#endif
