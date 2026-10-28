/* Port of src/app/bootstrap/network/q2-demo.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_demo.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/mvd.h"
#include "qa/network/q2/mvd_recording.h"
static bool is_mvd(const uint8_t *bytes, size_t length) {
  if (length < 4) return false;
  uint32_t magic = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
      | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
  return magic == QA_Q2_MVD_MAGIC;
}
typedef struct mvd_cursor { size_t offset; } mvd_cursor;
/* TS readMvdFile generator step. Returns false at the terminator. */
static bool mvd_next(const uint8_t *bytes, size_t length, mvd_cursor *cursor,
    qa_q2_demo_record *out) {
  size_t offset = cursor->offset;
  if (offset >= length) qa_range_error("MVD recording lacks its terminator");
  if (offset + 2 > length) qa_range_error("Truncated MVD record header");
  size_t start = offset;
  size_t record_length = bytes[offset] | (bytes[offset + 1] << 8);
  offset += 2;
  if (record_length == 0) {
    if (offset != length) qa_range_error("Data after MVD terminator");
    cursor->offset = offset;
    return false;
  }
  if (record_length > QA_Q2_MVD_MAX_MESSAGE || offset + record_length > length)
    qa_range_error("Truncated or oversized MVD packet");
  out->offset = (double)start;
  out->bytes = malloc(record_length);
  if (!out->bytes) qa_range_error("out of memory");
  memcpy(out->bytes, bytes + offset, record_length);
  out->bytes_length = record_length;
  cursor->offset = offset + record_length;
  return true;
}
void qa_app_q2_read_playback_header(const uint8_t *bytes, size_t length,
    qa_app_q2_playback_header *out) {
  memset(out, 0, sizeof *out);
  if (!is_mvd(bytes, length)) {
    out->is_mvd = false;
    if (!qa_q2_read_demo_header(bytes, length, &out->dm2)) qa_range_error("out of memory");
    out->protocol = out->dm2.protocol;
    out->data = out->dm2.data;
    return;
  }
  mvd_cursor cursor = {4};
  qa_q2_demo_record record;
  if (!mvd_next(bytes, length, &cursor, &record))
    qa_range_error("MVD recording has no gamestate");
  out->is_mvd = true;
  if (!qa_q2_read_mvd_header(record.bytes, record.bytes_length, &out->profile)) {
    free(record.bytes);
    qa_range_error("out of memory");
  }
  free(record.bytes);
  out->protocol = out->profile.profile.protocol;
  out->data = out->profile.data;
}
void qa_app_q2_playback_header_free(qa_app_q2_playback_header *header) {
  if (header->is_mvd) qa_q2_mvd_header_free(&header->profile);
}
struct qa_app_q2_demo_playback {
  qa_app_q2_client_receiver *receiver;
  qa_app_q2_client_host host;
  const uint8_t *bytes; size_t length;
  bool mvd;
  qa_q2_mvd_playback decoder; bool decoder_live;
  qa_q2_demo_record *records; size_t record_count, record_index;
  mvd_cursor cursor; bool records_done;
  double header_bytes;
  const char *terminal; bool closed, advancing;
  double offset;
  double recorded_players[256]; size_t recorded_player_count;
  double selected_seat;
  qa_app_q2_mvd_presentation presentation; bool has_presentation;
  void (*select_recorded_view)(void *, double); void *select_ctx;
};
static bool next_record(qa_app_q2_demo_playback *p, qa_q2_demo_record *out) {
  if (p->mvd) {
    if (p->records_done) return false;
    bool has = mvd_next(p->bytes, p->length, &p->cursor, out);
    if (!has) p->records_done = true;
    return has;
  }
  if (p->record_index >= p->record_count) return false;
  *out = p->records[p->record_index++];
  return true;
}
static bool view_filtered(qa_app_q2_demo_playback *p, const qa_q2_server_record *record) {
  switch (record->event.kind) {
    case QA_Q2_SERVER_LAYOUT: case QA_Q2_SERVER_INVENTORY:
    case QA_Q2_SERVER_CENTER_PRINT: case QA_Q2_SERVER_DAMAGE:
    case QA_Q2_SERVER_FOG: case QA_Q2_SERVER_POI:
    case QA_Q2_SERVER_HELP_PATH: case QA_Q2_SERVER_LOCALIZED_PRINT:
      return record->seat == 0 || record->seat == p->selected_seat + 1;
    default: return true;
  }
}
/* Wrapped host installed on the receiver. */
static void wrap_server_data(void *ctx, const void *data,
    void (*assert_current)(void *), void *assert_ctx) {
  qa_app_q2_demo_playback *p = ctx;
  const qa_q2_server_data_result *result = data;
  if (result->has_clientnums) {
    p->recorded_player_count = result->clientnum_count;
    for (size_t i = 0; i < p->recorded_player_count && i < 256; i++)
      p->recorded_players[i] = result->clientnums[i];
  } else {
    p->recorded_player_count = 1;
    p->recorded_players[0] = result->clientnum;
  }
  if (p->selected_seat >= (double)p->recorded_player_count) p->selected_seat = 0;
  if (p->host.server_data) p->host.server_data(p->host.ctx, data, assert_current, assert_ctx);
}
static void wrap_game_state(void *ctx, qa_app_q2_game_state state) {
  qa_app_q2_demo_playback *p = ctx;
  p->host.game_state(p->host.ctx, state);
}
static void wrap_frame(void *ctx, const void *frame, const void *const *records,
    size_t count, double now) {
  qa_app_q2_demo_playback *p = ctx;
  if (p->mvd) { p->host.frame(p->host.ctx, frame, records, count, now); return; }
  if ((size_t)p->selected_seat >= p->recorded_player_count)
    qa_range_error("Recorded viewpoint has no source player");
  double selected = p->recorded_players[(size_t)p->selected_seat];
  const qa_q2_wire_frame *wire = frame;
  qa_q2_wire_frame adjusted;
  const void *effective = frame;
  if (p->selected_seat != 0) {
    size_t split_index = (size_t)p->selected_seat - 1;
    if (!wire->has_split || split_index >= wire->split_count)
      qa_range_error("Recorded viewpoint has no player state");
    adjusted = *wire;
    adjusted.player = wire->split_players[split_index].player;
    adjusted.area_bits = wire->split_players[split_index].area_bits;
    adjusted.area_bits_length = wire->split_players[split_index].area_bits_length;
    effective = &adjusted;
  }
  if (p->select_recorded_view) p->select_recorded_view(p->select_ctx, selected);
  /* viewRecords filter */
  const void **filtered = malloc((count ? count : 1) * sizeof *filtered);
  if (!filtered) qa_range_error("out of memory");
  size_t kept = 0;
  for (size_t i = 0; i < count; i++)
    if (view_filtered(p, records[i])) filtered[kept++] = records[i];
  p->host.frame(p->host.ctx, effective, (const void *const *)filtered, kept, now);
  free(filtered);
}
static void wrap_records(void *ctx, const void *const *records, size_t count) {
  qa_app_q2_demo_playback *p = ctx;
  if (p->mvd) { p->host.records(p->host.ctx, records, count); return; }
  const void **filtered = malloc((count ? count : 1) * sizeof *filtered);
  if (!filtered) qa_range_error("out of memory");
  size_t kept = 0;
  for (size_t i = 0; i < count; i++)
    if (view_filtered(p, records[i])) filtered[kept++] = records[i];
  p->host.records(p->host.ctx, (const void *const *)filtered, kept);
  free(filtered);
}
static void wrap_disconnected(void *ctx, const char *reason) {
  qa_app_q2_demo_playback *p = ctx;
  p->host.disconnected(p->host.ctx, reason);
}
static void wrap_print(void *ctx, const char *text) {
  qa_app_q2_demo_playback *p = ctx;
  p->host.print(p->host.ctx, text);
}
qa_app_q2_demo_playback *qa_app_q2_demo_playback_create(
    const uint8_t *bytes, size_t length, qa_app_q2_client_host host,
    const qa_app_q2_mvd_presentation *presentation,
    void (*select_recorded_view)(void *ctx, double clientnum), void *select_ctx) {
  qa_app_q2_demo_playback *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->bytes = bytes; p->length = length; p->host = host;
  p->select_recorded_view = select_recorded_view; p->select_ctx = select_ctx;
  p->mvd = is_mvd(bytes, length);
  if (p->mvd) {
    if (!presentation)
      qa_range_error("MVD playback requires source BSP visibility and viewer binding");
    p->presentation = *presentation; p->has_presentation = true;
    if (!qa_q2_mvd_playback_init(&p->decoder, presentation->visibility)) { free(p); return NULL; }
    p->decoder_live = true;
    p->cursor.offset = 4;
    p->header_bytes = 2;
  } else {
    if (!qa_q2_read_demo(bytes, length, &p->records, &p->record_count)) { free(p); return NULL; }
    p->header_bytes = 4;
  }
  qa_app_q2_client_host wrapped = host;
  wrapped.ctx = p;
  wrapped.server_data = wrap_server_data;
  wrapped.game_state = wrap_game_state;
  wrapped.frame = wrap_frame;
  wrapped.records = wrap_records;
  wrapped.disconnected = wrap_disconnected;
  wrapped.print = wrap_print;
  qa_app_q2_receiver_source source = {0};
  source.is_demo = true;
  p->receiver = qa_app_q2_client_receiver_create(wrapped, source);
  if (!p->receiver) { qa_app_q2_demo_playback_destroy(p); return NULL; }
  return p;
}
void qa_app_q2_demo_playback_destroy(qa_app_q2_demo_playback *playback) {
  if (!playback) return;
  qa_app_q2_client_receiver_destroy(playback->receiver);
  if (playback->decoder_live) qa_q2_mvd_playback_destroy(&playback->decoder);
  qa_q2_demo_records_free(playback->records, playback->record_count);
  free(playback);
}
qa_app_q2_client_receiver *qa_app_q2_demo_playback_receiver(qa_app_q2_demo_playback *playback) {
  return playback->receiver;
}
void qa_app_q2_demo_playback_select_player(qa_app_q2_demo_playback *playback, double clientnum) {
  if (playback->mvd) {
    qa_q2_mvd_playback_select_player(&playback->decoder, clientnum);
    return;
  }
  for (size_t i = 0; i < playback->recorded_player_count; i++)
    if (playback->recorded_players[i] == clientnum) {
      playback->selected_seat = (double)i;
      return;
    }
  qa_range_error("Player has no viewpoint in this recording");
}
bool qa_app_q2_demo_playback_recorded_time(qa_app_q2_demo_playback *playback, double *out) {
  return qa_app_q2_client_receiver_recorded_time(playback->receiver, out);
}
double qa_app_q2_demo_playback_consumed_bytes(const qa_app_q2_demo_playback *playback) {
  return playback->offset;
}
static qa_app_q2_demo_advance read_until(qa_app_q2_demo_playback *p, const double *target_in) {
  qa_app_q2_demo_advance result = {"eof", 0};
  if (p->closed) { result.kind = "closed"; return result; }
  if (p->terminal) { result.kind = p->terminal; return result; }
  if (p->advancing) qa_range_error("Q2 demo advance already in progress");
  p->advancing = true;
  bool delivered = false;
  bool has_target = target_in != NULL;
  double target = has_target ? *target_in : 0;
  for (;;) {
    double time = 0;
    bool has_time = qa_app_q2_demo_playback_recorded_time(p, &time);
    if (has_time && (has_target ? time > target : delivered)) {
      result.kind = "frame"; result.time_milliseconds = time;
      break;
    }
    qa_q2_demo_record record;
    if (!next_record(p, &record)) {
      p->terminal = "eof";
      if (p->offset < (double)p->length) p->offset += p->header_bytes;
      if (delivered && has_time) { result.kind = "frame"; result.time_milliseconds = time; }
      else result.kind = "eof";
      break;
    }
    p->offset = record.offset + p->header_bytes + (double)record.bytes_length;
    double generation = qa_app_q2_client_receiver_world_generation(p->receiver);
    if (!p->mvd) {
      size_t count = 0;
      qa_q2_server_record *records = qa_app_q2_client_receiver_receive(
          p->receiver, record.bytes, record.bytes_length, 0, &count);
      qa_q2_server_records_free(records, count);
    } else {
      qa_q2_server_record *records = NULL; size_t count = 0;
      if (!qa_q2_mvd_playback_read(&p->decoder, record.bytes, record.bytes_length,
              &records, &count))
        qa_range_error("out of memory");
      free(record.bytes);
      bool stop = false;
      for (size_t i = 0; i < count; i++) {
        if (records[i].event.kind == QA_Q2_SERVER_FRAME && p->has_presentation)
          p->presentation.select_view(p->presentation.ctx,
              qa_q2_mvd_playback_get_selected_player(&p->decoder));
        qa_app_q2_client_receiver_receive_records(p->receiver, &records[i], 1, 0);
        if (p->closed) { stop = true; break; }
      }
      qa_q2_server_records_free(records, count);
      if (stop) { result.kind = "closed"; break; }
    }
    if (has_time && qa_app_q2_client_receiver_world_generation(p->receiver) != generation)
      has_target = false;
    if (p->closed) { result.kind = "closed"; break; }
    double new_time = 0;
    bool has_new = qa_app_q2_demo_playback_recorded_time(p, &new_time);
    if (has_new && (!has_time || new_time != time)) delivered = true;
    if (qa_app_q2_client_receiver_disconnected_demo(p->receiver)) {
      p->terminal = "disconnected";
      if (delivered && has_new) { result.kind = "frame"; result.time_milliseconds = new_time; }
      else result.kind = "disconnected";
      break;
    }
    if (strcmp(qa_app_q2_client_receiver_phase(p->receiver), "closed") == 0) {
      result.kind = "closed";
      break;
    }
  }
  p->advancing = false;
  return result;
}
qa_app_q2_demo_advance qa_app_q2_demo_playback_advance(
    qa_app_q2_demo_playback *playback, double target_milliseconds) {
  if (!isfinite(target_milliseconds) || target_milliseconds < 0)
    qa_range_error("Invalid Q2 demo presentation time");
  return read_until(playback, &target_milliseconds);
}
qa_app_q2_demo_advance qa_app_q2_demo_playback_next_frame(qa_app_q2_demo_playback *playback) {
  return read_until(playback, NULL);
}
void qa_app_q2_demo_playback_close(qa_app_q2_demo_playback *playback) {
  if (playback->closed) return;
  playback->closed = true;
  qa_app_q2_client_receiver_close(playback->receiver);
}
