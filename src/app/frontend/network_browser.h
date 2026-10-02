#ifndef QA_FRONTEND_NETWORK_BROWSER_H
#define QA_FRONTEND_NETWORK_BROWSER_H
#include "qa/frontend.h"
#include "qa/q3_host.h"
#include "qa/q3_host_browser.h"
#include "qa/server_browser_q3.h"
#include "qa/q3_ui_client_state.h"

typedef struct frontend_q3_browser frontend_q3_browser;
typedef struct frontend_q3_browser_options {
    qa_server_browser *browser;
    void *context;
    bool (*current)(void *, qa_error *);
    uint64_t (*now_ns)(void *);
    bool (*resolve)(void *, const char *, uint16_t, qa_net_address *, bool *present, qa_error *);
    bool (*cache_read)(void *, qa_buffer *, bool *present, qa_error *);
    bool (*cache_write)(void *, qa_bytes, qa_error *);
    bool (*print)(void *, const char *, qa_error *);
} frontend_q3_browser_options;
typedef struct frontend_q3_browser_access {
    frontend_q3_browser *browser;
    const qa_cvars *cvars;
    void *context;
    bool (*current)(void *, qa_error *);
} frontend_q3_browser_access;
bool frontend_q3_browser_create(const frontend_q3_browser_options *, frontend_q3_browser **empty, qa_error *);
void frontend_q3_browser_destroy(frontend_q3_browser *);
bool frontend_q3_browser_services(frontend_q3_browser_access *, qa_q3_host_browser_services *, qa_error *);
bool frontend_q3_browser_poll(frontend_q3_browser *, qa_error *);
bool frontend_q3_browser_scan(frontend_q3_browser *, qa_error *);
bool frontend_q3_browser_load_cache(frontend_q3_browser *, qa_error *);
bool frontend_q3_browser_master(frontend_q3_browser *, int32_t source, const char *remote,
    int32_t protocol, const char *const *keywords, size_t count, qa_error *);
bool frontend_q3_browser_ping(frontend_q3_browser_access *, const char *, qa_error *);
bool frontend_q3_browser_status_command(frontend_q3_browser_access *, const char *, qa_error *);
bool frontend_q3_browser_checkpoint(const frontend_q3_browser *, qa_buffer *empty, qa_error *);
bool frontend_q3_browser_restore(const frontend_q3_browser_options *, qa_bytes,
    frontend_q3_browser **empty, qa_error *);

/* Retained by the genuine per-role UI lease through checked host destruction.
 * qualified proves its active source or exact entered Factory UI namespace. */
typedef struct frontend_network_browser_binding {
    qa_frontend *frontend;
    qa_application *application;
    const void *network;
    qa_q3_host_client_context ui;
    uint32_t authored_seat;
    uint64_t epoch;
    void *context;
    bool (*qualified)(void *, const qa_q3_host_client_context *, qa_error *);
    frontend_q3_browser_access access;
} frontend_network_browser_binding;
bool frontend_network_browser_services(qa_frontend *, const qa_q3_host_client_context *actual_ui,
    uint32_t authored_seat, uint64_t epoch, void *lease_context,
    bool (*qualified)(void *, const qa_q3_host_client_context *, qa_error *),
    frontend_network_browser_binding *, qa_q3_host_browser_services *, qa_error *);
/* UI44 proves the exact actual host separately from the reserved constructor
 * namespace admitted by the browser facade. */
bool frontend_network_ui_client_state(frontend_network_browser_binding *, const qa_q3_host *,
    void *proof_context,
    bool (*host_current)(void *, const qa_q3_host *, const qa_q3_host_client_context *, qa_error *),
    qa_q3_ui_client_state *, qa_error *);
#endif
