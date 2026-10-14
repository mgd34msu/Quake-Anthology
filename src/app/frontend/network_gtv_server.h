#ifndef QA_FRONTEND_NETWORK_GTV_SERVER_H
#define QA_FRONTEND_NETWORK_GTV_SERVER_H

#include "demo_service.h"
#include "qa/network_q2_mvd.h"
#include "qa/platform_stream.h"

typedef struct frontend_gtv_server frontend_gtv_server;
typedef struct frontend_gtv_server_options {
    qa_net_address endpoint;
    size_t max_viewers, pending_bytes;
    uint64_t timeout_ns, ghost_timeout_ns;
    const char *password;
    void *context;
    /* Delivers the latest complete MVD header/frame through this borrowed sink.
     * An empty delivery suspends the viewer until Source output is available. */
    bool (*seed)(void *, const frontend_demo_sink *, qa_error *);
    /* Viewer failures retire only that viewer. Reports are synchronous. */
    void (*report)(void *, uint64_t peer_id, const qa_error *);
} frontend_gtv_server_options;

/* The source and diagnostic callbacks must not destroy or reenter this owner.
 * Output bodies borrow their caller only until publish/seed delivery returns. */
bool frontend_gtv_server_create(const frontend_gtv_server_options *,
    frontend_gtv_server **, qa_error *);
void frontend_gtv_server_destroy(frontend_gtv_server *);
bool frontend_gtv_server_collect(frontend_gtv_server *, qa_platform_events *,
    uint64_t time_ns, double wall_ms, qa_error *);
bool frontend_gtv_server_receive(frontend_gtv_server *, const qa_sys_event *,
    qa_bytes, bool *recognized, qa_error *);
bool frontend_gtv_server_flush(frontend_gtv_server *, qa_error *);
bool frontend_gtv_server_publish(frontend_gtv_server *, qa_bytes, qa_error *);
size_t frontend_gtv_server_active_viewers(const frontend_gtv_server *);
bool frontend_gtv_server_listening(const frontend_gtv_server *);

#endif
