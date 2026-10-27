/*
 * Port of src/network/q2/gtv.ts. The async zlib stream becomes a
 * synchronous zlib z_stream (deflate with Z_SYNC_FLUSH per push,
 * exactly the TS flush discipline); receive() returns a malloc'd
 * event list. Sends are fn + ctx. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_GTV_H
#define QA_NETWORK_Q2_GTV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zlib.h>

#include "qa/network/q2/mvd_recording.h"

/* TS GtvIdentity (borrowed strings). */
typedef struct qa_q2_gtv_identity {
  const char *username;
  const char *password;
  const char *version;
} qa_q2_gtv_identity;

/* TS GtvEvent kinds (reason/bytes owned on the matching kinds). */
typedef enum qa_q2_gtv_event_kind {
  QA_Q2_GTV_EVENT_HELLO, QA_Q2_GTV_EVENT_DATA, QA_Q2_GTV_EVENT_PONG,
  QA_Q2_GTV_EVENT_STARTED, QA_Q2_GTV_EVENT_STOPPED,
  QA_Q2_GTV_EVENT_SUSPENDED, QA_Q2_GTV_EVENT_RESUMED, QA_Q2_GTV_EVENT_CLOSED
} qa_q2_gtv_event_kind;
typedef struct qa_q2_gtv_event {
  qa_q2_gtv_event_kind kind;
  double flags;   /* hello */
  uint8_t *bytes; /* data (owned) */
  size_t bytes_length;
  char *reason; /* closed (owned) */
} qa_q2_gtv_event;
void qa_q2_gtv_events_free(qa_q2_gtv_event *events, size_t count);

typedef void (*qa_q2_gtv_send_fn)(void *ctx, const uint8_t *bytes, size_t length);

/* TS GtvCompression (one stream across every packet). */
typedef struct qa_q2_gtv_compression {
  bool active;
  bool encode;
  bool failed;
  char failure[128];
  z_stream stream;
} qa_q2_gtv_compression;

typedef enum qa_q2_gtv_phase {
  QA_Q2_GTV_MAGIC, QA_Q2_GTV_HELLO, QA_Q2_GTV_CONNECTED, QA_Q2_GTV_STARTING,
  QA_Q2_GTV_READING, QA_Q2_GTV_WAITING, QA_Q2_GTV_STOPPING, QA_Q2_GTV_CLOSED
} qa_q2_gtv_phase;

/* TS GtvClient. Identity strings are copied. */
typedef struct qa_q2_gtv_client {
  qa_q2_gtv_send_fn send;
  void *send_ctx;
  char *username; /* owned */
  char *password; /* owned */
  char *version;  /* owned */
  double requested_flags;
  qa_q2_gtv_phase phase;
  uint8_t magic[4];
  size_t magic_length;
  qa_q2_mvd_message_framer frames;
  qa_q2_gtv_compression compression;
  double flags;
  bool busy;
  bool started;
} qa_q2_gtv_client;

/* requested_flags < 0 selects TS default (DEFLATE|STRINGCMDS). */
bool qa_q2_gtv_client_init(qa_q2_gtv_client *client, qa_q2_gtv_send_fn send,
                           void *ctx, qa_q2_gtv_identity identity,
                           double requested_flags);
void qa_q2_gtv_client_destroy(qa_q2_gtv_client *client);
void qa_q2_gtv_client_start(qa_q2_gtv_client *client);
/* max_buffered < 0 selects TS default 10. */
void qa_q2_gtv_client_request_start(qa_q2_gtv_client *client,
                                    double max_buffered_packets);
void qa_q2_gtv_client_request_stop(qa_q2_gtv_client *client);
void qa_q2_gtv_client_ping(qa_q2_gtv_client *client);
void qa_q2_gtv_client_command(qa_q2_gtv_client *client, const char *text);
/* TS receive(input). False only on OOM. */
bool qa_q2_gtv_client_receive(qa_q2_gtv_client *client, const uint8_t *input,
                              size_t length, qa_q2_gtv_event **out,
                              size_t *out_count);
void qa_q2_gtv_client_close(qa_q2_gtv_client *client);

/* TS GtvClientHello. */
typedef struct qa_q2_gtv_client_hello {
  double flags;
  char username[2048];
  char password[2048];
  char version[2048];
} qa_q2_gtv_client_hello;

/* TS GtvRequest. */
typedef enum qa_q2_gtv_request_kind {
  QA_Q2_GTV_REQUEST_HELLO, QA_Q2_GTV_REQUEST_START, QA_Q2_GTV_REQUEST_PING,
  QA_Q2_GTV_REQUEST_STOP, QA_Q2_GTV_REQUEST_COMMAND
} qa_q2_gtv_request_kind;
typedef struct qa_q2_gtv_request {
  qa_q2_gtv_request_kind kind;
  qa_q2_gtv_client_hello hello;
  double max_buffered_packets; /* start */
  char text[2048];              /* command */
} qa_q2_gtv_request;

/* TS readGtvRequest. False only on OOM. */
bool qa_q2_read_gtv_request(const uint8_t *bytes, size_t length,
                            qa_q2_gtv_request *out);

/* TS GtvServerStream. */
typedef struct qa_q2_gtv_server_stream {
  qa_q2_gtv_compression compression;
  bool greeted;
  bool closed;
  bool busy;
} qa_q2_gtv_server_stream;

void qa_q2_gtv_server_stream_init(qa_q2_gtv_server_stream *stream);
/* hello/message: malloc'd bytes or NULL only on OOM. body may be NULL
 * when body_length is 0 (TS default empty body). */
uint8_t *qa_q2_gtv_server_stream_hello(qa_q2_gtv_server_stream *stream,
                                       double flags, size_t *out_length);
uint8_t *qa_q2_gtv_server_stream_message(qa_q2_gtv_server_stream *stream,
                                         double opcode, const uint8_t *body,
                                         size_t body_length,
                                         size_t *out_length);
void qa_q2_gtv_server_stream_close(qa_q2_gtv_server_stream *stream);

#endif
