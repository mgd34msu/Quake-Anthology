/*
 * Port of src/network/q2/gtv-transport.ts. The Node socket/promise
 * flow becomes a POSIX TCP socket owned by the connection: connect
 * is synchronous (it returns once the GTV hello lands, dispatching
 * that batch of events first) and qa_q2_gtv_connection_poll pumps
 * later traffic (PORTING async rule). The AbortSignal becomes a
 * borrowed bool flag checked in connect/poll. The 60 s keepalive
 * interval becomes a deadline checked in poll. Transport failures
 * abort with the TS rejection messages (they reached the TS
 * top-level handler the same way); after the hello they instead
 * deliver one closed event like TS fail().
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_GTV_TRANSPORT_H
#define QA_NETWORK_Q2_GTV_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/gtv.h"

/* TS GtvConnectionOptions.event (event borrowed during the call). */
typedef void (*qa_q2_gtv_event_fn)(void *ctx, const qa_q2_gtv_event *event);

/* TS GtvConnectionOptions. timeout < 0 selects the TS 90000 default. */
typedef struct qa_q2_gtv_connection_options {
  const char *host;
  double port;
  qa_q2_gtv_identity identity;
  const bool *aborted; /* TS signal (borrowed, may be NULL) */
  double timeout_milliseconds;
  qa_q2_gtv_event_fn event;
  void *event_ctx;
} qa_q2_gtv_connection_options;

/* TS GtvConnection. */
typedef struct qa_q2_gtv_connection {
  int socket_fd;
  qa_q2_gtv_client client;
  bool client_ready;
  bool closed;
  bool opened;
  double next_ping_ms; /* 0 when no keepalive is armed */
  qa_q2_gtv_connection_options options;
} qa_q2_gtv_connection;

/* TS GtvConnection.connect. False only on OOM. */
bool qa_q2_gtv_connection_connect(qa_q2_gtv_connection *connection,
                                  qa_q2_gtv_connection_options options);
void qa_q2_gtv_connection_destroy(qa_q2_gtv_connection *connection);
/* Pump the socket once (wait up to wait_milliseconds for data; the
 * keepalive and abort flag are checked too). Returns events
 * dispatched. */
size_t qa_q2_gtv_connection_poll(qa_q2_gtv_connection *connection,
                                 double wait_milliseconds);
bool qa_q2_gtv_connection_get_closed(const qa_q2_gtv_connection *connection);
/* TS start(maxBufferedPackets=10; <0 is the default) / stop /
 * command / ping / close. */
void qa_q2_gtv_connection_start(qa_q2_gtv_connection *connection,
                                double max_buffered_packets);
void qa_q2_gtv_connection_stop(qa_q2_gtv_connection *connection);
void qa_q2_gtv_connection_command(qa_q2_gtv_connection *connection,
                                  const char *text);
void qa_q2_gtv_connection_ping(qa_q2_gtv_connection *connection);
void qa_q2_gtv_connection_close(qa_q2_gtv_connection *connection);

#endif
