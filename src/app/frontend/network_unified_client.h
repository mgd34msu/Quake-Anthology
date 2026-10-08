#ifndef QA_FRONTEND_NETWORK_UNIFIED_CLIENT_H
#define QA_FRONTEND_NETWORK_UNIFIED_CLIENT_H
#include "client_source.h"
#include "remote_unified.h"

typedef struct frontend_network_unified_client_service frontend_network_unified_client_service;
typedef struct frontend_network_unified_client_options {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_net_address remote;
    qa_product_id profile, selected;
    uint32_t physical_seat;
    qa_net_seat_id seat;
    frontend_client_source_options configuration;
    void *context;
    bool (*current)(void *, const frontend_network_unified_client_service *);
    bool (*disconnected)(void *, const qa_application_client_source *, const char *, qa_error *);
} frontend_network_unified_client_options;
typedef struct frontend_network_unified_client_view {
    const frontend_network_unified_client_service *owner;
    frontend_client_source_view physical;
    qa_net_address remote;
    qa_net_seat_id seat;
    qa_product_id profile;
    bool retired;
} frontend_network_unified_client_view;

/* Configuration custody transfers with the returned partial service. */
bool frontend_network_unified_client_create(const frontend_network_unified_client_options *,
    frontend_network_unified_client_service **, qa_error *);
bool frontend_network_unified_client_advance(frontend_network_unified_client_service *, bool *ready, qa_error *);
bool frontend_network_unified_client_drain(frontend_network_unified_client_service *, size_t budget,
    size_t *executed, qa_error *);
bool frontend_network_unified_client_dispatch(frontend_network_unified_client_service *,
    const qa_application_client_source *,const char *,qa_error *);
bool frontend_network_unified_client_options_read(frontend_network_unified_client_service *,
    frontend_remote_unified_options *, qa_error *);
bool frontend_network_unified_client_import_options_read(frontend_network_unified_client_service *,
    frontend_remote_unified_options *, qa_error *);
bool frontend_network_unified_client_retirement_options_read(frontend_network_unified_client_service *,
    frontend_remote_unified_options *,qa_error *);
bool frontend_network_unified_client_bind(frontend_network_unified_client_service *, qa_net_client_id,
    qa_net_seat_id, frontend_remote_unified *, qa_error *);
bool frontend_network_unified_client_bind_restored(frontend_network_unified_client_service *,qa_net_client_id,
    qa_net_seat_id,frontend_remote_unified *,qa_error *);
bool frontend_network_unified_client_restart_adopt(frontend_network_unified_client_service *,
    const frontend_remote_unified_domain *,uint64_t,qa_error *);
bool frontend_network_unified_client_request_retirement(frontend_network_unified_client_service *,
    const qa_application_client_source *,qa_error *);
bool frontend_network_unified_client_retired(const frontend_network_unified_client_service *);
bool frontend_network_unified_client_retirement_current(void *,const qa_application_client_source *);
bool frontend_network_unified_client_source_read(const frontend_network_unified_client_service *,
    frontend_client_source_view *, qa_error *);
bool frontend_network_unified_client_metadata_read(const frontend_network_unified_client_service *,
    frontend_network_unified_client_view *, qa_error *);
bool frontend_network_unified_client_idle(const frontend_network_unified_client_service *);
bool frontend_network_unified_client_publication_ready(const frontend_network_unified_client_service *,qa_error *);
void frontend_network_unified_client_publish(frontend_network_unified_client_service *);
bool frontend_network_unified_client_destroy(frontend_network_unified_client_service **, qa_error *);
#endif
