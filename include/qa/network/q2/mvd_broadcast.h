/*
 * Port of src/network/q2/mvd-broadcast.ts. The Node TCP listener
 * and per-viewer promise queues become a POSIX listener pumped by
 * qa_q2_mvd_broadcast_poll; queued actions run synchronously in
 * the same order (PORTING async rule). A malformed viewer request
 * aborts with its TS message instead of retiring only that viewer
 * (the TS catch reached retire(), noted here as the one divergence).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_MVD_BROADCAST_H
#define QA_NETWORK_Q2_MVD_BROADCAST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/gtv.h"
#include "qa/network/q2/mvd_encoding.h"
#include "qa/network/q2/mvd_recording.h"

/* TS MvdBroadcastOptions (command/record NULL is TS undefined).
 * max_viewers < 0 selects the TS default 16. */
typedef struct qa_q2_mvd_broadcast_options {
  void *ctx;
  bool (*authorize)(void *ctx, const qa_q2_gtv_client_hello *hello);
  void (*command)(void *ctx, const char *text);
  qa_q2_mvd_write_fn record;
  double max_viewers;
} qa_q2_mvd_broadcast_options;

/* One TS Viewer. */
typedef struct qa_q2_mvd_viewer {
  int socket_fd;
  qa_q2_gtv_server_stream stream;
  qa_q2_mvd_message_framer frames;
  double queued_bytes;
  bool identified, authorized, active, needs_gamestate, closed;
  double flags, sequence;
} qa_q2_mvd_viewer;

/* TS MvdBroadcast (latest capture deep-copied, messages dropped
 * because gamestate re-encodes always clear them). */
typedef struct qa_q2_mvd_broadcast {
  qa_q2_mvd_broadcast_options options;
  qa_q2_mvd_encoder encoder;
  bool has_recording;
  qa_q2_mvd_recording recording;
  bool has_latest;
  double revision, flags, servercount;
  char *gamedir; /* owned */
  double dummy;
  qa_q2_mvd_capture_config *configs; /* values owned */
  size_t config_count;
  uint8_t *portal_bits; /* owned */
  size_t portal_bits_length;
  qa_q2_mvd_capture_player *players; /* owned */
  size_t player_count;
  qa_q2_wire_entity *entities; /* owned */
  size_t entity_count;
  double sequence;
  qa_q2_mvd_viewer *viewers; /* owned */
  size_t viewer_count, viewer_capacity;
  int listener_fd;
  bool closed;
} qa_q2_mvd_broadcast;

/* TS constructor; invalid viewer limits abort like the RangeError. */
void qa_q2_mvd_broadcast_init(qa_q2_mvd_broadcast *broadcast,
                              qa_q2_mvd_broadcast_options options);
void qa_q2_mvd_broadcast_destroy(qa_q2_mvd_broadcast *broadcast);
/* TS observe(capture). */
void qa_q2_mvd_broadcast_observe(qa_q2_mvd_broadcast *broadcast,
                                 const qa_q2_mvd_capture *capture);
/* TS listen(host, port): returns the bound TCP port. */
double qa_q2_mvd_broadcast_listen(qa_q2_mvd_broadcast *broadcast,
                                  const char *host, double port);
/* Pump listener + viewers once; returns sockets serviced. */
size_t qa_q2_mvd_broadcast_poll(qa_q2_mvd_broadcast *broadcast,
                                double wait_milliseconds);
size_t qa_q2_mvd_broadcast_viewer_count(const qa_q2_mvd_broadcast *broadcast);
/* TS close(). */
void qa_q2_mvd_broadcast_close(qa_q2_mvd_broadcast *broadcast);

#endif
