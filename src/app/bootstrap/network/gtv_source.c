/* Port of src/app/bootstrap/network/gtv-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/gtv_source.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_gtv_remote_source {
  qa_q2_gtv_connection connection; bool connection_live;
  qa_app_q2_client_receiver *receiver;
  qa_q2_mvd_playback decoder; bool decoder_live;
  qa_app_q2_mvd_presentation presentation;
  uint8_t *pending; size_t pending_length; bool has_pending;
  bool has_header; qa_q2_mvd_header header;
  bool ended, polling;
  char *failure;
  bool ready_settled;
};
static void source_close(qa_app_gtv_remote_source *s);
static void receive_event(void *ctx, const qa_q2_gtv_event *event) {
  qa_app_gtv_remote_source *s = ctx;
  if (s->ended) return;
  if (event->kind == QA_Q2_GTV_EVENT_CLOSED) {
    free(s->failure);
    s->failure = event->reason ? strdup(event->reason) : NULL;
    source_close(s);
    return;
  }
  if (event->kind != QA_Q2_GTV_EVENT_DATA) return;
  if (s->has_pending) qa_range_error("GTV DATA dispatch overlapped its consumer");
  if (!s->has_header) {
    qa_q2_mvd_header header;
    if (!qa_q2_read_mvd_header(event->bytes, event->bytes_length, &header))
      qa_range_error("out of memory");
    s->header = header;
    s->has_header = true;
    s->ready_settled = true;
  }
  s->pending = malloc(event->bytes_length ? event->bytes_length : 1);
  if (!s->pending) qa_range_error("out of memory");
  memcpy(s->pending, event->bytes, event->bytes_length);
  s->pending_length = event->bytes_length;
  s->has_pending = true;
}
typedef struct prepare_scope {
  qa_app_gtv_remote_source *s;
  qa_q2_gtv_connection_options connect;
  bool ok;
} prepare_scope;
static void prepare_body(void *arg) {
  prepare_scope *scope = (prepare_scope *)arg;
  qa_app_gtv_remote_source *s = scope->s;
  /* A connect that raises has released its own sockets; the client it initialized is
   * destroyed with the source. */
  s->connection_live = true;
  if (!qa_q2_gtv_connection_connect(&s->connection, scope->connect)) { scope->ok = false; return; }
  if (s->ended) {
    qa_q2_gtv_connection_close(&s->connection);
    qa_raise("Error", 0, 0, "GTV source retired during admission");
  }
  qa_q2_gtv_connection_start(&s->connection, -1);
  /* TS awaits the ready promise: pump until the first DATA header. */
  while (!s->ready_settled && !s->ended)
    qa_q2_gtv_connection_poll(&s->connection, 100);
  if (!s->has_header)
    qa_raise("Error", 0, 0, "%s", s->failure ? s->failure : "GTV source closed before admission");
}
qa_app_gtv_remote_source *qa_app_gtv_remote_source_prepare(qa_app_gtv_source_options options) {
  qa_app_gtv_remote_source *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->connection.socket_fd = -1; /* no socket until connect opens one */
  qa_q2_gtv_connection_options connect = {0};
  connect.host = options.host;
  connect.port = options.port;
  connect.identity = options.identity;
  connect.aborted = options.aborted;
  connect.timeout_milliseconds = -1;
  connect.event = receive_event;
  connect.event_ctx = s;
  /* TS try { await Promise.all([connect, source.ready]) } catch (error) { source.close(); throw error; } */
  prepare_scope scope = {s, connect, true};
  qa_caught_error caught;
  bool ok = qa_try(prepare_body, &scope, &caught);
  if (ok && scope.ok) return s;
  source_close(s);
  qa_app_gtv_remote_source_destroy(s);
  if (!ok) qa_rethrow(&caught);
  return NULL;
}
void qa_app_gtv_remote_source_destroy(qa_app_gtv_remote_source *source) {
  if (!source) return;
  source_close(source);
  if (source->connection_live) qa_q2_gtv_connection_destroy(&source->connection);
  qa_app_q2_client_receiver_destroy(source->receiver);
  if (source->decoder_live) qa_q2_mvd_playback_destroy(&source->decoder);
  if (source->has_header) qa_q2_mvd_header_free(&source->header);
  free(source->pending); free(source->failure); free(source);
}
const qa_q2_mvd_header *qa_app_gtv_remote_source_header(qa_app_gtv_remote_source *source) {
  if (!source->has_header) qa_range_error("GTV source has no admitted header");
  return &source->header;
}
const char *qa_app_gtv_remote_source_phase(qa_app_gtv_remote_source *source) {
  if (source->ended) return "closed";
  return source->receiver ? qa_app_q2_client_receiver_phase(source->receiver) : "loading";
}
bool qa_app_gtv_remote_source_time(qa_app_gtv_remote_source *source, double *out) {
  if (!source->receiver) return false;
  return qa_app_q2_client_receiver_recorded_time(source->receiver, out);
}
void qa_app_gtv_remote_source_bind(qa_app_gtv_remote_source *source,
    qa_app_q2_client_host host, qa_app_q2_mvd_presentation presentation) {
  if (source->ended || source->receiver || !source->has_header)
    qa_range_error("GTV presentation admission is not available");
  qa_app_q2_receiver_source demo = {0};
  demo.is_demo = true;
  source->receiver = qa_app_q2_client_receiver_create(host, demo);
  if (!source->receiver) qa_range_error("out of memory");
  if (!qa_q2_mvd_playback_init(&source->decoder, presentation.visibility))
    qa_range_error("out of memory");
  source->decoder_live = true;
  source->presentation = presentation;
}
typedef struct poll_scope {
  qa_app_gtv_remote_source *source;
  const uint8_t *bytes;
  size_t length;
  qa_q2_server_record *records;
  size_t count;
} poll_scope;
static void record_release(void *arg) {
  qa_q2_server_record *record = arg;
  qa_q2_server_event_clear(&record->event);
  free(record->raw);
  record->raw = NULL;
}
static void gtv_record(void *ctx, qa_q2_server_record *record) {
  qa_app_gtv_remote_source *source = ctx;
  qa_cleanup node;
  qa_cleanup_push(&node, record_release, record);
  if (!source->ended) {
    if (record->event.kind == QA_Q2_SERVER_FRAME)
      source->presentation.select_view(source->presentation.ctx,
          qa_q2_mvd_playback_get_selected_player(&source->decoder));
    qa_app_q2_client_receiver_receive_records(source->receiver, record, 1, 0);
  }
  qa_cleanup_pop(&node);
  record_release(record);
}
static void poll_body(void *arg) {
  poll_scope *scope = (poll_scope *)arg;
  qa_app_gtv_remote_source *source = scope->source;
  /* TS iterates readRecords, selecting the view between yielded records. */
  if (!qa_q2_mvd_playback_read_records(&source->decoder, scope->bytes, scope->length,
          gtv_record, source))
    qa_range_error("out of memory");
}
void qa_app_gtv_remote_source_poll(qa_app_gtv_remote_source *source) {
  if (source->failure) qa_raise("Error", 0, 0, "%s", source->failure);
  if (source->ended) return;
  if (source->connection_live && !source->has_pending)
    qa_q2_gtv_connection_poll(&source->connection, 0);
  if (source->ended || !source->has_pending) return;
  if (source->polling) qa_range_error("GTV source poll is already in progress");
  if (!source->receiver) qa_range_error("GTV source has no presentation owner");
  uint8_t *pending = source->pending;
  size_t length = source->pending_length;
  /* The poll owns the bytes now; close() must not free them under the decoder. */
  source->pending = NULL;
  source->polling = true;
  poll_scope scope = {source, pending, length, NULL, 0};
  qa_caught_error caught;
  bool ok = qa_try(poll_body, &scope, &caught);
  qa_q2_server_records_free(scope.records, scope.count);
  /* TS finally: the DATA message is released whether or not delivery failed. */
  source->has_pending = false;
  free(pending);
  source->polling = false;
  if (!ok) {
    source_close(source);
    qa_rethrow(&caught);
  }
}
void qa_app_gtv_remote_source_select_player(qa_app_gtv_remote_source *source, double clientnum) {
  if (!source->receiver || source->ended) qa_range_error("GTV source has no active viewer");
  qa_q2_mvd_playback_select_player(&source->decoder, clientnum);
}
static void source_close(qa_app_gtv_remote_source *s) {
  if (s->ended) return;
  s->ended = true;
  if (!s->ready_settled) s->ready_settled = true;
  if (s->receiver) qa_app_q2_client_receiver_close(s->receiver);
  if (s->connection_live) qa_q2_gtv_connection_close(&s->connection);
  free(s->pending); s->pending = NULL; s->has_pending = false;
}
void qa_app_gtv_remote_source_close(qa_app_gtv_remote_source *source) { source_close(source); }
