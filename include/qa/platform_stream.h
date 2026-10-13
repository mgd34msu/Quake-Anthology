#ifndef QA_PLATFORM_STREAM_H
#define QA_PLATFORM_STREAM_H
#include "qa/platform_events.h"
#include "qa/network.h"

typedef struct qa_platform_stream qa_platform_stream;
/* A loopback listener owns one active peer. Intake pauses until its queued
 * record is consumed; payload bytes belong to the common platform queue. */
bool qa_platform_stream_create(uint16_t port, int backlog, size_t maximum_read,
    qa_platform_stream **, qa_error *);
/* Outbound peers use the same stream intake. ACCEPT reports that a peer is
 * open, whether accepted by a listener or connected to the resolved endpoint. */
bool qa_platform_stream_connect(const qa_net_address *,size_t maximum_read,
    qa_platform_stream **,qa_error *);
void qa_platform_stream_destroy(qa_platform_stream *);
void qa_platform_stream_stop_listener(qa_platform_stream *);
void qa_platform_stream_close_peer(qa_platform_stream *);
void qa_platform_stream_read_room(qa_platform_stream *, size_t);
uint64_t qa_platform_stream_source_id(const qa_platform_stream *);
bool qa_platform_stream_collect(qa_platform_stream *, qa_platform_events *,
    uint64_t time_ns, double wall_ms, qa_error *);
void qa_platform_stream_consumed(qa_platform_stream *, const qa_sys_event *);
bool qa_platform_stream_write(qa_platform_stream *, qa_bytes,
    size_t *sent, bool *open, qa_error *);
#endif
