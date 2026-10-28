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
    if (!qa_q2_read_mvd_header(event->bytes, event->bytes_length, &s->header))
      qa_range_error("out of memory");
    s->has_header = true;
    s->ready_settled = true;
  }
  s->pending = malloc(event->bytes_length ? event->bytes_length : 1);
  if (!s->pending) qa_range_error("out of memory");
  memcpy(s->pending, event->bytes, event->bytes_length);
  s->pending_length = event->bytes_length;
  s->has_pending = true;
}
qa_app_gtv_remote_source *qa_app_gtv_remote_source_prepare(qa_app_gtv_source_options options) {
  qa_app_gtv_remote_source *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  qa_q2_gtv_connection_options connect = {0};
  connect.host = options.host;
  connect.port = options.port;
  connect.identity = options.identity;
  connect.aborted = options.aborted;
  connect.timeout_milliseconds = -1;
  connect.event = receive_event;
  connect.event_ctx = s;
  if (!qa_q2_gtv_connection_connect(&s->connection, connect)) { free(s); return NULL; }
  s->connection_live = true;
  if (s->ended) {
    qa_q2_gtv_connection_close(&s->connection);
    qa_app_gtv_remote_source_destroy(s);
    qa_range_error("GTV source retired during admission");
  }
  qa_q2_gtv_connection_start(&s->connection, -1);
  /* TS awaits the ready promise: pump until the first DATA header. */
  while (!s->ready_settled && !s->ended)
    qa_q2_gtv_connection_poll(&s->connection, 100);
  if (!s->has_header) {
    const char *message = s->failure ? s->failure : "GTV source closed before admission";
    char *copy = strdup(message);
    source_close(s);
    qa_app_gtv_remote_source_destroy(s);
    qa_range_error("%s", copy);
  }
  return s;
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
void qa_app_gtv_remote_source_poll(qa_app_gtv_remote_source *source) {
  if (source->failure) qa_range_error("%s", source->failure);
  if (source->ended) return;
  if (source->connection_live && !source->has_pending)
    qa_q2_gtv_connection_poll(&source->connection, 0);
  if (source->ended || !source->has_pending) return;
  if (source->polling) qa_range_error("GTV source poll is already in progress");
  if (!source->receiver) qa_range_error("GTV source has no presentation owner");
  uint8_t *pending = source->pending;
  size_t length = source->pending_length;
  source->polling = true;
  qa_q2_server_record *records = NULL; size_t count = 0;
  if (!qa_q2_mvd_playback_read(&source->decoder, pending, length, &records, &count)) {
    source->has_pending = false; free(pending); source->polling = false;
    qa_range_error("out of memory");
  }
  for (size_t i = 0; i < count; i++) {
    if (records[i].event.kind == QA_Q2_SERVER_FRAME)
      source->presentation.select_view(source->presentation.ctx,
          qa_q2_mvd_playback_get_selected_player(&source->decoder));
    qa_app_q2_client_receiver_receive_records(source->receiver, &records[i], 1, 0);
    if (source->ended) break;
  }
  qa_q2_server_records_free(records, count);
  if (source->pending == pending) { source->has_pending = false; source->pending = NULL; }
  free(pending);
  source->polling = false;
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
