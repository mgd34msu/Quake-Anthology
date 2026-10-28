/* Port of src/app/bootstrap/network/q2-client-receiver.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/common_parse.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/frames.h"
#include "qa/network/q2/server_write.h"
typedef struct pending_state {
  qa_q2_server_data_result data;
  qa_app_q2_config_snapshot configs;
  qa_q2_baseline_list baselines;
  qa_app_q2_game_state view;
} pending_state;
struct qa_app_q2_client_receiver {
  qa_app_q2_client_host host; qa_app_q2_receiver_source source;
  qa_q2_server_message_reader reader;
  const char *state;
  bool has_server_data; qa_q2_server_data_result server_data;
  double last_frame; bool has_recorded_time; double recorded_time;
  bool demo_disconnected;
  pending_state *pending;
  double loading_generation;
};
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1; char *out = malloc(n);
  if (out) memcpy(out, s, n);
  return out;
}
static void pending_free(pending_state *pending) {
  if (!pending) return;
  for (size_t i = 0; i < pending->configs.count; i++) free(pending->configs.entries[i].value);
  free(pending->configs.entries);
  qa_q2_baseline_list_free(&pending->baselines);
  free(pending);
}
static void cancel_loading(qa_app_q2_client_receiver *r) {
  r->loading_generation++;
  pending_free(r->pending); r->pending = NULL;
  if (!r->source.is_demo && r->source.has_downloads)
    r->source.downloads.close(r->source.downloads.ctx);
}
static void assert_current(qa_app_q2_client_receiver *r, double generation) {
  if (generation != r->loading_generation || strcmp(r->state, "closed") == 0
      || (!r->source.is_demo && r->source.closed(r->source.ctx)))
    qa_range_error("Q2 server directory selection was retired");
}
typedef struct assert_ctx { qa_app_q2_client_receiver *r; double generation; } assert_ctx;
static void assert_thunk(void *ctx) {
  assert_ctx *a = ctx;
  assert_current(a->r, a->generation);
}
qa_app_q2_client_receiver *qa_app_q2_client_receiver_create(
    qa_app_q2_client_host host, qa_app_q2_receiver_source source) {
  qa_app_q2_client_receiver *r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->host = host; r->source = source; r->state = "loading"; r->last_frame = -1;
  qa_q2_server_message_options options = *(const qa_q2_server_message_options *)host.message_options;
  options.read_mode_demo = source.is_demo;
  if (!qa_q2_server_message_reader_init(&r->reader, host.protocol, options)) {
    free(r); return NULL;
  }
  return r;
}
void qa_app_q2_client_receiver_destroy(qa_app_q2_client_receiver *receiver) {
  if (!receiver) return;
  pending_free(receiver->pending);
  qa_q2_server_message_reader_destroy(&receiver->reader);
  free(receiver);
}
qa_q2_server_message_reader *qa_app_q2_client_receiver_reader(qa_app_q2_client_receiver *receiver) {
  return &receiver->reader;
}
const char *qa_app_q2_client_receiver_phase(const qa_app_q2_client_receiver *receiver) {
  return receiver->state;
}
double qa_app_q2_client_receiver_acknowledged_frame(const qa_app_q2_client_receiver *receiver) {
  return receiver->last_frame;
}
double qa_app_q2_client_receiver_world_generation(const qa_app_q2_client_receiver *receiver) {
  return receiver->loading_generation;
}
bool qa_app_q2_client_receiver_recorded_time(const qa_app_q2_client_receiver *receiver, double *out) {
  if (!receiver->has_recorded_time) return false;
  *out = receiver->recorded_time; return true;
}
bool qa_app_q2_client_receiver_disconnected_demo(const qa_app_q2_client_receiver *receiver) {
  return receiver->demo_disconnected;
}
void qa_app_q2_recording_seed_free(qa_app_q2_recording_seed *seed) {
  for (size_t i = 0; i < seed->packet_count; i++) free(seed->packets[i].message);
  free(seed->packets);
  seed->packets = NULL; seed->packet_count = 0;
}
typedef struct seed_builder { qa_app_q2_recording_packet *packets; size_t count, cap; } seed_builder;
static void seed_add(seed_builder *b, uint8_t *message, size_t length) {
  if (!message) qa_range_error("out of memory");
  if (b->count == b->cap) {
    b->cap = b->cap ? b->cap * 2 : 16;
    b->packets = realloc(b->packets, b->cap * sizeof *b->packets);
    if (!b->packets) qa_range_error("out of memory");
  }
  b->packets[b->count].message = message;
  b->packets[b->count].length = length;
  b->count++;
}
void qa_app_q2_client_receiver_seed(qa_app_q2_client_receiver *receiver, qa_app_q2_recording_seed *out) {
  if (!receiver->has_server_data || strcmp(receiver->state, "active") != 0)
    qa_range_error("Recording requires an active Q2 server");
  qa_q2_wire_codec *wire = &receiver->reader.wire;
  qa_q2_protocol_identity protocol = receiver->host.protocol;
  bool kex = protocol.kind && (strcmp(protocol.kind, "q2-kex") == 0
      || strcmp(protocol.kind, "q2-kex-demo") == 0);
  seed_builder builder = {0};
  size_t length = 0;
  qa_q2_server_event event = {0};
  event.kind = QA_Q2_SERVER_DATA; event.data = receiver->server_data;
  event.data.attractloop = true;
  seed_add(&builder, qa_q2_encode_server_event(wire, &event, &length), length);
  for (size_t i = 0; i < receiver->reader.config_count; i++) {
    qa_q2_server_event config = {0};
    config.kind = QA_Q2_SERVER_CONFIG_STRING;
    config.index = receiver->reader.config_strings[i].index;
    config.text = receiver->reader.config_strings[i].value;
    seed_add(&builder, qa_q2_encode_server_event(wire, &config, &length), length);
  }
  qa_q2_frame_history *history = qa_q2_server_message_reader_get_history_default(&receiver->reader);
  (void)history;
  for (size_t i = 0; i < receiver->reader.baselines.count; i++) {
    qa_q2_server_event baseline = {0};
    baseline.kind = QA_Q2_SERVER_BASELINE;
    baseline.entity = receiver->reader.baselines.entries[i].state;
    seed_add(&builder, qa_q2_encode_server_event(wire, &baseline, &length), length);
  }
  qa_q2_server_event precache = {0};
  precache.kind = QA_Q2_SERVER_COMMAND_TEXT; precache.text = (char *)"precache\n";
  seed_add(&builder, qa_q2_encode_server_event(wire, &precache, &length), length);
  qa_q2_latest_frame *frames = NULL; size_t frame_count = 0;
  if (!qa_q2_server_message_reader_latest_frames(&receiver->reader, &frames, &frame_count))
    qa_range_error("out of memory");
  for (size_t i = 0; i < frame_count; i++) {
    if (kex) {
      qa_q2_server_event seat = {0};
      seat.kind = QA_Q2_SERVER_SEAT; seat.seat = frames[i].seat;
      seed_add(&builder, qa_q2_encode_server_event(wire, &seat, &length), length);
    }
    seed_add(&builder, qa_q2_encode_frame(wire, frames[i].frame, NULL,
        &receiver->reader.baselines, 0, &length), length);
  }
  free(frames);
  if (kex) {
    qa_q2_server_event seat = {0};
    seat.kind = QA_Q2_SERVER_SEAT;
    seat.seat = qa_q2_server_message_reader_get_seat(&receiver->reader);
    seed_add(&builder, qa_q2_encode_server_event(wire, &seat, &length), length);
  }
  out->protocol = protocol;
  out->packets = builder.packets; out->packet_count = builder.count;
}
void qa_app_q2_client_receiver_request_full_frame(qa_app_q2_client_receiver *receiver) {
  receiver->last_frame = -1;
}
void qa_app_q2_client_receiver_close(qa_app_q2_client_receiver *receiver) {
  cancel_loading(receiver);
  receiver->state = "closed";
}
void qa_app_q2_client_receiver_prepare_game_state(qa_app_q2_client_receiver *r) {
  pending_state *state = r->pending;
  if (!state) return;
  double generation = r->loading_generation;
  qa_app_q2_download_preparation preparation = QA_APP_Q2_PREP_READY;
  if (!r->source.is_demo && r->source.has_downloads)
    preparation = r->source.downloads.prepare(r->source.downloads.ctx, &state->view);
  assert_current(r, generation);
  if (r->pending != state || preparation != QA_APP_Q2_PREP_READY) return;
  r->host.game_state(r->host.ctx, state->view);
  assert_current(r, generation);
  if (r->pending != state) return;
  r->pending = NULL;
  if (!r->source.is_demo) {
    char command[64];
    snprintf(command, sizeof command, "begin %.0f", state->data.servercount);
    r->source.command(r->source.ctx, command);
  }
  pending_free(state);
  r->state = "active";
}
static double signon_integer(const char *text) {
  if (!text) qa_range_error("Invalid Q2 signon number");
  const char *p = text;
  if (*p == '-') p++;
  if (!*p) qa_range_error("Invalid Q2 signon number");
  for (const char *q = p; *q; q++)
    if (*q < '0' || *q > '9') qa_range_error("Invalid Q2 signon number");
  double result = strtod(text, NULL);
  if (result != trunc(result) || fabs(result) > 9007199254740991.0)
    qa_range_error("Q2 signon number outside range");
  return result;
}
static void server_commands(qa_app_q2_client_receiver *r, const char *text) {
  const char *cursor = text;
  while (cursor) {
    const char *end = cursor;
    while (*end && *end != '\n' && *end != ';') end++;
    size_t line_length = (size_t)(end - cursor);
    char *line = malloc(line_length + 1);
    if (!line) qa_range_error("out of memory");
    memcpy(line, cursor, line_length); line[line_length] = '\0';
    qa_legacy_parse_state parse = {line, line_length, 0};
    char **words = NULL; size_t word_count = 0, word_cap = 0;
    while (parse.index < line_length) {
      size_t before = parse.index;
      char *value = qa_parse_q2_token(&parse, 8192);
      if (parse.index == before) { free(value); break; }
      if (word_count == word_cap) {
        word_cap = word_cap ? word_cap * 2 : 8;
        words = realloc(words, word_cap * sizeof *words);
        if (!words) qa_range_error("out of memory");
      }
      words[word_count++] = value;
    }
    const char *name = word_count ? words[0] : NULL;
    if (name && strcmp(name, "cmd") == 0 && word_count > 1
        && (strcmp(words[1], "configstrings") == 0 || strcmp(words[1], "baselines") == 0)) {
      if (!r->source.is_demo) {
        size_t total = 1;
        for (size_t i = 1; i < word_count; i++) total += strlen(words[i]) + 1;
        char *joined = malloc(total);
        if (!joined) qa_range_error("out of memory");
        size_t used = 0;
        for (size_t i = 1; i < word_count; i++) {
          if (i > 1) joined[used++] = ' ';
          size_t len = strlen(words[i]);
          memcpy(joined + used, words[i], len); used += len;
        }
        joined[used] = '\0';
        r->source.command(r->source.ctx, joined);
        free(joined);
      }
    } else if (name && strcmp(name, "precache") == 0) {
      bool checked = !r->source.is_demo || word_count > 1;
      if (!r->has_server_data
          || (checked && signon_integer(word_count > 1 ? words[1] : NULL) != r->server_data.servercount))
        qa_range_error("Q2 precache refers to another server generation");
      cancel_loading(r);
      pending_state *pending = calloc(1, sizeof *pending);
      if (!pending) qa_range_error("out of memory");
      pending->data = r->server_data;
      pending->configs.count = r->reader.config_count;
      pending->configs.entries = malloc(
          (pending->configs.count ? pending->configs.count : 1) * sizeof *pending->configs.entries);
      if (!pending->configs.entries) qa_range_error("out of memory");
      for (size_t i = 0; i < pending->configs.count; i++) {
        pending->configs.entries[i].index = r->reader.config_strings[i].index;
        pending->configs.entries[i].value = copy_text(r->reader.config_strings[i].value);
      }
      for (size_t i = 0; i < r->reader.baselines.count; i++)
        if (!qa_q2_baseline_list_set(&pending->baselines,
                r->reader.baselines.entries[i].number, &r->reader.baselines.entries[i].state))
          qa_range_error("out of memory");
      pending->view.data = &pending->data;
      pending->view.config_strings = &pending->configs;
      pending->view.baselines = &pending->baselines;
      r->pending = pending;
      qa_app_q2_client_receiver_prepare_game_state(r);
    } else if (name && strcmp(name, "changing") == 0) {
      cancel_loading(r);
      r->last_frame = -1;
      r->state = "loading";
    } else if (name && name[0]) {
      size_t n = strlen(line) + 48;
      char *message = malloc(n);
      if (!message) qa_range_error("out of memory");
      snprintf(message, n, "Server command requires application binding: %s\n", line);
      r->host.print(r->host.ctx, message);
      free(message);
    }
    for (size_t i = 0; i < word_count; i++) free(words[i]);
    free(words);
    free(line);
    if (strcmp(r->state, "closed") == 0) return;
    cursor = *end ? end + 1 : NULL;
  }
}
static void server_records(qa_app_q2_client_receiver *r, qa_q2_server_record *records,
    size_t count, double now) {
  for (size_t i = 0; i < count; i++) {
    qa_q2_server_record *record = &records[i];
    switch (record->event.kind) {
      case QA_Q2_SERVER_DATA: {
        cancel_loading(r);
        assert_ctx assertion = {r, r->loading_generation};
        if (r->host.server_data)
          r->host.server_data(r->host.ctx, &record->event.data, assert_thunk, &assertion);
        assert_current(r, assertion.generation);
        r->server_data = record->event.data; r->has_server_data = true;
        r->last_frame = -1; r->has_recorded_time = false;
        if (!r->source.is_demo) r->source.reset_commands(r->source.ctx);
        r->state = "loading";
        break;
      }
      case QA_Q2_SERVER_COMMAND_TEXT:
        server_commands(r, record->event.text);
        if (strcmp(r->state, "closed") == 0) return;
        break;
      case QA_Q2_SERVER_FRAME: {
        qa_q2_wire_frame *frame = &record->event.frame;
        bool invalid = frame->has_valid && !frame->valid;
        r->last_frame = invalid ? -1 : frame->server_frame;
        if (!invalid) {
          double fps = r->has_server_data && r->server_data.has_server_fps
              ? r->server_data.server_fps : 10;
          r->recorded_time = frame->server_frame * (1000.0 / fps);
          r->has_recorded_time = true;
          const void **ptrs = malloc((count ? count : 1) * sizeof *ptrs);
          if (!ptrs) qa_range_error("out of memory");
          for (size_t j = 0; j < count; j++) ptrs[j] = &records[j];
          r->host.frame(r->host.ctx, frame, ptrs, count,
              r->source.is_demo ? r->recorded_time : now);
          free(ptrs);
        }
        break;
      }
      case QA_Q2_SERVER_DISCONNECT:
        cancel_loading(r);
        r->state = "closed";
        if (r->source.is_demo) {
          r->demo_disconnected = true;
          const void **ptrs = malloc((i + 1) * sizeof *ptrs);
          if (!ptrs) qa_range_error("out of memory");
          for (size_t j = 0; j <= i; j++) ptrs[j] = &records[j];
          r->host.records(r->host.ctx, ptrs, i + 1);
          free(ptrs);
          return;
        }
        r->host.disconnected(r->host.ctx, "Server disconnected");
        break;
      case QA_Q2_SERVER_RECONNECT:
        cancel_loading(r);
        r->last_frame = -1;
        r->state = "loading";
        if (!r->source.is_demo) r->source.command(r->source.ctx, "new");
        break;
      case QA_Q2_SERVER_PRINT:
        r->host.print(r->host.ctx, record->event.text);
        break;
      case QA_Q2_SERVER_DOWNLOAD:
        if (!r->source.is_demo && r->source.has_downloads
            && r->source.downloads.receive(r->source.downloads.ctx, &record->event)
                == QA_APP_Q2_DL_COMPLETE)
          qa_app_q2_client_receiver_prepare_game_state(r);
        break;
      default: break;
    }
  }
  const void **ptrs = malloc((count ? count : 1) * sizeof *ptrs);
  if (!ptrs) qa_range_error("out of memory");
  for (size_t j = 0; j < count; j++) ptrs[j] = &records[j];
  r->host.records(r->host.ctx, ptrs, count);
  free(ptrs);
}
qa_q2_server_record *qa_app_q2_client_receiver_receive(qa_app_q2_client_receiver *receiver,
    const uint8_t *bytes, size_t length, double now_milliseconds, size_t *out_count) {
  *out_count = 0;
  if (strcmp(receiver->state, "closed") == 0) return NULL;
  qa_q2_server_record *records = NULL;
  if (!qa_q2_server_message_reader_read(&receiver->reader, bytes, length, &records, out_count))
    qa_range_error("out of memory");
  server_records(receiver, records, *out_count, now_milliseconds);
  return records;
}
void qa_app_q2_client_receiver_receive_records(qa_app_q2_client_receiver *receiver,
    qa_q2_server_record *records, size_t count, double now_milliseconds) {
  if (strcmp(receiver->state, "closed") == 0) return;
  qa_q2_server_message_reader_accept_decoded(&receiver->reader, records, count);
  server_records(receiver, records, count, now_milliseconds);
}
