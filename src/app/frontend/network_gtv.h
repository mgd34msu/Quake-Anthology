#ifndef QA_FRONTEND_NETWORK_GTV_H
#define QA_FRONTEND_NETWORK_GTV_H

#include "qa/network_q2_mvd.h"
#include "qa/platform_stream.h"

typedef struct frontend_gtv_watch frontend_gtv_watch;

/* Events borrow their payload until the callback returns. The watch must not
 * be destroyed from an event callback or while one of its calls is active. */
bool frontend_gtv_watch_create(const qa_net_address *, const qa_gtv_identity *,
    qa_gtv_event_fn, void *, frontend_gtv_watch **, qa_error *);
void frontend_gtv_watch_destroy(frontend_gtv_watch *);
bool frontend_gtv_watch_collect(frontend_gtv_watch *, qa_platform_events *,
    uint64_t time_ns, double wall_ms, qa_error *);
bool frontend_gtv_watch_receive(frontend_gtv_watch *, const qa_sys_event *,
    qa_bytes payload, bool *recognized, qa_error *);
bool frontend_gtv_watch_flush(frontend_gtv_watch *, qa_error *);
bool frontend_gtv_watch_closed(const frontend_gtv_watch *);
bool frontend_gtv_watch_command(frontend_gtv_watch *, const char *, qa_error *);

#endif
