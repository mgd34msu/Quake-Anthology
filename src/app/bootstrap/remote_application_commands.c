/* Port of src/app/bootstrap/remote-application.ts, the commands region (TS
 * 2908-3254 in the 2026-11-03 tree): download progress, cancel and retry.
 * Status per member: docs/remote_application_layout.md. GPL-2.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "remote_application_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include "qa/app/bootstrap/input_devices.h"
#include "qa/content/q3/presentation/audio.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
#include "qa/input/router.h"
#include "qa/input/user_command.h"
#include "qa/network/common/js_number.h"
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/foundation/support.h"

static void progress_reserve(qa_remote_application *app, size_t count) {
  if (count <= app->progress_capacity) return;
  qa_app_client_download_progress *grown = realloc(app->progress_rows, count * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  app->progress_rows = grown;
  app->progress_capacity = count;
}
/* TS primaryDownloadProgress(): Q2 remote rows, else qwDownloads?.progress ?? q3Downloads?.progress ?? []. */
size_t qa_remote_application_primary_download_progress(qa_remote_application *app, size_t at) {
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  size_t count = 0;
  if (remote->family == QA_APP_REMOTE_FAMILY_Q2) {
    count = qa_app_q2_remote_download_progress(remote->q2, NULL, 0);
    progress_reserve(app, at + count);
    count = qa_app_q2_remote_download_progress(remote->q2, app->progress_rows + at, count);
  } else if (app->qw_downloads != NULL) {
    progress_reserve(app, at + 1);
    count = qa_app_qw_download_receiver_progress(app->qw_downloads, app->progress_rows + at);
  } else if (app->q3_downloads != NULL) {
    count = qa_app_q3_client_downloads_progress(app->q3_downloads, NULL, 0);
    progress_reserve(app, at + count);
    count = qa_app_q3_client_downloads_progress(app->q3_downloads, app->progress_rows + at, count);
  }
  return count;
}
const qa_app_client_download_progress *qa_remote_application_download_progress(qa_remote_application *app,
    size_t *out_count) {
  size_t count = qa_remote_application_primary_download_progress(app, 0);
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (peer->closed) continue;
    size_t rows = 0;
    const qa_app_client_download_progress *progress = qa_app_remote_seat_source_download_progress(peer->source, &rows);
    progress_reserve(app, count + rows);
    if (rows != 0) memcpy(app->progress_rows + count, progress, rows * sizeof *progress);
    count += rows;
  }
  *out_count = count;
  return app->progress_rows;
}
void qa_remote_application_cancel_downloads(qa_remote_application *app, const qa_seat_id *seat) {
  if (seat == NULL || qa_same_seat(*seat, qa_remote_application_seat_id(app))) {
    const qa_remote_presentation *remote = qa_remote_application_remote(app);
    if (remote->family == QA_APP_REMOTE_FAMILY_Q2) qa_app_q2_remote_cancel_downloads(remote->q2);
    if (app->qw_downloads != NULL) qa_app_qw_download_receiver_cancel(app->qw_downloads);
    if (app->q3_downloads != NULL) qa_app_q3_client_downloads_cancel(app->q3_downloads);
  }
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (!peer->closed && (seat == NULL || qa_same_seat(qa_session_seat_id(peer->seat), *seat)))
      qa_app_remote_seat_source_cancel_downloads(peer->source);
  }
}
void qa_remote_application_retry_downloads(qa_remote_application *app, const qa_seat_id *seat) {
  if (seat == NULL || qa_same_seat(*seat, qa_remote_application_seat_id(app))) {
    const qa_remote_presentation *remote = qa_remote_application_remote(app);
    if (remote->family == QA_APP_REMOTE_FAMILY_Q2) qa_app_q2_remote_retry_downloads(remote->q2);
    if (app->qw_downloads != NULL) (void)qa_app_qw_download_receiver_retry(app->qw_downloads);
    if (app->q3_downloads != NULL) (void)qa_app_q3_client_downloads_retry(app->q3_downloads);
  }
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (!peer->closed && (seat == NULL || qa_same_seat(qa_session_seat_id(peer->seat), *seat)))
      qa_app_remote_seat_source_retry_downloads(peer->source);
  }
}

/* ---- dispatchCommands (TS 2929-2948) ----------------------------------- */
void qa_remote_application_dispatch_commands(qa_remote_application *app) {
  qa_remote_command *pending = app->commands;
  size_t count = app->command_count;
  app->commands = NULL;
  app->command_count = 0;
  app->command_capacity = 0;
  for (size_t index = 0; index < count; index++) {
    qa_app_video_restart *restart = qa_remote_application_video_restart(app);
    if (restart != NULL && qa_app_video_restart_pending(restart)) {
      /* this.commands.unshift(...pending.slice(index)) */
      size_t rest = count - index, total = rest + app->command_count;
      qa_remote_command *merged = calloc(total + 1, sizeof *merged);
      if (merged == NULL) qa_range_error("Out of memory");
      memcpy(merged, pending + index, rest * sizeof *merged);
      if (app->command_count > 0) memcpy(merged + rest, app->commands, app->command_count * sizeof *merged);
      free(app->commands);
      app->commands = merged;
      app->command_count = total;
      app->command_capacity = total + 1;
      free(pending);
      return;
    }
    qa_remote_command *command = &pending[index];
    qa_cleanup guard;
    qa_cleanup_push(&guard, (void (*)(void *))qa_remote_command_free, command);
    if (app->ownership.borrowed) {
      qa_command_context source;
      if (command->has_source) source = command->source;
      else {
        memset(&source, 0, sizeof source);
        source.session = qa_engine_session_id(app->session);
        source.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
      }
      qa_app_configuration_command_request request;
      memset(&request, 0, sizeof request);
      request.target = "application";
      request.name = command->name;
      request.arguments = (const char *const *)command->args;
      request.argument_count = command->arg_count;
      request.has_seat = command->has_seat;
      request.seat = command->seat;
      request.source = source;
      qa_app_client_bootstrap *client = app->ownership.client;
      client->dispatch_application_request(client->bootstrap_ctx, &request);
    } else qa_remote_application_execute_remote_command(app, command);
    qa_cleanup_pop(&guard);
    qa_remote_command_free(command);
  }
  free(pending);
}

/* ---- recording (TS 2950-3014) ------------------------------------------ */
/* TS ClientRecordingFeed of prepareRecording: the selected seat's network recorder. */
typedef struct recording_feed {
  qa_remote_application *app;
  qa_remote_seat_peer *peer;          /* NULL = TS undefined */
  qa_remote_live_network network;
  char *root;
  /* seed storage */
  qa_app_q1_recording_seed q1; qa_app_q2_recording_seed q2; qa_app_q3_recording_seed q3;
  qa_quakeworld_demo_record *qw; size_t qw_count;
  qa_app_demo_recording_packet *packets; size_t packet_count;
  bool has_seed;
} recording_feed;
typedef struct recording_attachment {
  recording_feed *feed;
  qa_app_demo_recording_sink sink;
  void *token;
} recording_attachment;
static void feed_release_seed(recording_feed *feed) {
  if (!feed->has_seed) return;
  qa_app_q1_recording_seed_free(&feed->q1);
  qa_app_q2_recording_seed_free(&feed->q2);
  free(feed->q3.message);
  memset(&feed->q3, 0, sizeof feed->q3);
  for (size_t i = 0; i < feed->qw_count; i++) qa_quakeworld_demo_record_destroy(&feed->qw[i]);
  free(feed->qw);
  feed->qw = NULL;
  feed->qw_count = 0;
  free(feed->packets);
  feed->packets = NULL;
  feed->packet_count = 0;
  feed->has_seed = false;
}
static void release_feed(void *object) {
  recording_feed *feed = object;
  feed_release_seed(feed);
  free(feed->root);
  free(feed);
}
/* TS recording.seed(): the family seed as a DemoRecordingSeed. */
static qa_app_demo_recording_seed feed_seed(recording_feed *feed) {
  feed_release_seed(feed);
  feed->has_seed = true;
  qa_app_demo_recording_seed seed;
  memset(&seed, 0, sizeof seed);
  const qa_remote_live_network *network = &feed->network;
  switch (network->family) {
    case QA_APP_REMOTE_FAMILY_Q1: {
      qa_app_q1_client_network_recording_seed(network->q1, &feed->q1);
      seed.identity.kind = QA_APP_RECORDING_Q1;
      seed.identity.protocol = feed->q1.protocol_version;
      seed.identity.track = feed->q1.track;
      feed->packets = calloc(feed->q1.packet_count + 1, sizeof *feed->packets);
      if (feed->packets == NULL) qa_range_error("Out of memory");
      for (size_t i = 0; i < feed->q1.packet_count; i++) {
        feed->packets[i].kind = QA_APP_RECORDING_Q1;
        feed->packets[i].message = feed->q1.packets[i].message;
        feed->packets[i].message_length = feed->q1.packets[i].length;
        feed->packets[i].view_angles = feed->q1.packets[i].view_angles;
      }
      feed->packet_count = feed->q1.packet_count;
      break;
    }
    case QA_APP_REMOTE_FAMILY_QW: {
      feed->qw = qa_app_qw_client_network_recording_seed(network->qw, &feed->qw_count);
      seed.identity.kind = QA_APP_RECORDING_QW;
      seed.identity.protocol = 28;
      feed->packets = calloc(feed->qw_count + 1, sizeof *feed->packets);
      if (feed->packets == NULL) qa_range_error("Out of memory");
      for (size_t i = 0; i < feed->qw_count; i++) {
        feed->packets[i].kind = QA_APP_RECORDING_QW;
        feed->packets[i].record = feed->qw[i];
      }
      feed->packet_count = feed->qw_count;
      break;
    }
    case QA_APP_REMOTE_FAMILY_Q2: {
      qa_app_q2_client_network_recording_seed(network->q2, &feed->q2);
      seed.identity.kind = QA_APP_RECORDING_Q2;
      seed.identity.q2_protocol = feed->q2.protocol;
      feed->packets = calloc(feed->q2.packet_count + 1, sizeof *feed->packets);
      if (feed->packets == NULL) qa_range_error("Out of memory");
      for (size_t i = 0; i < feed->q2.packet_count; i++) {
        feed->packets[i].kind = QA_APP_RECORDING_Q2;
        feed->packets[i].message = feed->q2.packets[i].message;
        feed->packets[i].message_length = feed->q2.packets[i].length;
      }
      feed->packet_count = feed->q2.packet_count;
      break;
    }
    case QA_APP_REMOTE_FAMILY_Q3: {
      qa_app_q3_client_network_recording_seed(network->q3, &feed->q3);
      seed.identity.kind = QA_APP_RECORDING_Q3;
      seed.identity.protocol = 68;
      feed->packets = calloc(2, sizeof *feed->packets);
      if (feed->packets == NULL) qa_range_error("Out of memory");
      feed->packets[0].kind = QA_APP_RECORDING_Q3;
      feed->packets[0].sequence = feed->q3.sequence;
      feed->packets[0].message = feed->q3.message;
      feed->packets[0].message_length = feed->q3.length;
      feed->packet_count = 1;
      break;
    }
    case QA_APP_REMOTE_FAMILY_UNIFIED:
      qa_raise("Error", 0, 0, "The selected native protocol has no demo recorder");
  }
  seed.packets = feed->packets;
  seed.packet_count = feed->packet_count;
  return seed;
}
static void append_packet(recording_attachment *attachment, qa_app_demo_recording_packet packet) {
  const char *error = attachment->sink.append(attachment->sink.ctx, packet);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
static void sink_q1(void *ctx, const uint8_t *message, size_t length, qa_vec3 view_angles) {
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q1;
  packet.message = message;
  packet.message_length = length;
  packet.view_angles = view_angles;
  append_packet(ctx, packet);
}
static void sink_qw(void *ctx, const qa_quakeworld_demo_record *record) {
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_QW;
  packet.record = *record;
  append_packet(ctx, packet);
}
static void sink_q2(void *ctx, const char *kind, const uint8_t *message, size_t length) {
  (void)kind; /* the client recorder appends "q2" packets */
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q2;
  packet.message = message;
  packet.message_length = length;
  append_packet(ctx, packet);
}
static void sink_q3(void *ctx, double sequence, const uint8_t *message, size_t length) {
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q3;
  packet.sequence = sequence;
  packet.message = message;
  packet.message_length = length;
  append_packet(ctx, packet);
}
static bool feed_network_current(recording_feed *feed) {
  qa_remote_application *app = feed->app;
  if (app->closed || app->closing || qa_remote_application_source(app)->kind != QA_REMOTE_SOURCE_LIVE) return false;
  const qa_remote_live_network *current = feed->peer == NULL ? qa_remote_application_network(app)
      : qa_app_remote_seat_source_network(feed->peer->source);
  if (feed->peer != NULL && feed->peer->closed) return false;
  return current != NULL && memcmp(current, &feed->network, sizeof *current) == 0;
}
static const void *feed_attach(recording_feed *feed, qa_app_demo_recording_sink sink) {
  if (!feed_network_current(feed)) qa_raise("Error", 0, 0, "Recording connection was retired");
  recording_attachment *attachment = calloc(1, sizeof *attachment);
  if (attachment == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(&feed->app->arena, free, attachment);
  attachment->feed = feed;
  attachment->sink = sink;
  const qa_remote_live_network *network = &feed->network;
  switch (network->family) {
    case QA_APP_REMOTE_FAMILY_Q1:
      attachment->token = qa_app_q1_client_network_recording_attach(network->q1, (qa_app_q1_recording_sink){attachment, sink_q1});
      break;
    case QA_APP_REMOTE_FAMILY_QW:
      attachment->token = qa_app_qw_client_network_recording_attach(network->qw, (qa_app_qw_recording_sink){attachment, sink_qw});
      break;
    case QA_APP_REMOTE_FAMILY_Q2:
      attachment->token = qa_app_q2_client_network_recording_attach(network->q2, (qa_app_q2_recording_sink){attachment, sink_q2});
      break;
    case QA_APP_REMOTE_FAMILY_Q3:
      attachment->token = qa_app_q3_client_network_recording_attach(network->q3, (qa_app_q3_recording_sink){attachment, sink_q3});
      break;
    case QA_APP_REMOTE_FAMILY_UNIFIED:
      qa_raise("Error", 0, 0, "The selected native protocol has no demo recorder");
  }
  return attachment;
}
static void feed_detach(const void *token) {
  const recording_attachment *attachment = token;
  const qa_remote_live_network *network = &attachment->feed->network;
  switch (network->family) {
    case QA_APP_REMOTE_FAMILY_Q1: qa_app_q1_client_network_recording_detach(network->q1, attachment->token); return;
    case QA_APP_REMOTE_FAMILY_QW: qa_app_qw_client_network_recording_detach(network->qw, attachment->token); return;
    case QA_APP_REMOTE_FAMILY_Q2: qa_app_q2_client_network_recording_detach(network->q2, attachment->token); return;
    case QA_APP_REMOTE_FAMILY_Q3: qa_app_q3_client_network_recording_detach(network->q3, attachment->token); return;
    case QA_APP_REMOTE_FAMILY_UNIFIED: return;
  }
}
/* TS prepareRecording(context): the feed (arena-owned). */
static recording_feed *prepare_feed(qa_remote_application *app, const qa_command_context *context) {
  if (context->session.token != qa_engine_session_id(app->session).token)
    qa_raise("Error", 0, 0, "Recording command belongs to another session");
  if (qa_remote_application_source(app)->kind != QA_REMOTE_SOURCE_LIVE || strcmp(qa_remote_application_network_phase(app), "active") != 0)
    qa_raise("Error", 0, 0, "Recording requires an active live connection");
  const qa_command_origin *origin = &context->origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
  qa_remote_seat_peer *peer = NULL;
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT)
    for (size_t i = 0; i < app->remote_seat_count && peer == NULL; i++)
      if (!app->remote_seats[i]->closed && qa_same_seat(qa_session_seat_id(app->remote_seats[i]->seat), origin->seat))
        peer = app->remote_seats[i];
  if (peer != NULL && (peer->closed || !qa_app_remote_seat_channel_active(peer->channel)))
    qa_raise("Error", 0, 0, "Recording seat has no active connection");
  const qa_remote_live_network *network = peer != NULL ? qa_app_remote_seat_source_network(peer->source)
      : qa_remote_application_network(app);
  if (network == NULL || network->family == QA_APP_REMOTE_FAMILY_UNIFIED)
    qa_raise("Error", 0, 0, "The selected native protocol has no demo recorder");
  recording_feed *feed = calloc(1, sizeof *feed);
  if (feed == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(&app->arena, release_feed, feed);
  feed->app = app;
  feed->peer = peer;
  feed->network = *network;
  feed->root = strdup(app->remote_content != NULL ? app->remote_content->write_root : app->configuration->settings->root);
  if (feed->root == NULL) qa_range_error("Out of memory");
  return feed;
}
/* The standalone ClientDemoRecording host (TS standaloneRecording). */
typedef struct standalone_host { qa_remote_application *app; recording_feed *feed; char error[QA_CAUGHT_MESSAGE_CAPACITY]; } standalone_host;
static const char *standalone_root(void *ctx) {
  standalone_host *host = ctx;
  if (host->feed == NULL) return "Recording has no selected source";
  return host->feed->root;
}
typedef struct seed_call { standalone_host *host; qa_command_context source; qa_app_demo_recording_seed *out; } seed_call;
static void seed_body(void *ctx) {
  seed_call *call = ctx;
  call->host->feed = prepare_feed(call->host->app, &call->source);
  *call->out = feed_seed(call->host->feed);
}
static const char *standalone_seed(void *ctx, qa_command_context source, qa_app_demo_recording_seed *out) {
  standalone_host *host = ctx;
  seed_call call = {host, source, out};
  qa_caught_error caught;
  if (qa_try(seed_body, &call, &caught)) return NULL;
  snprintf(host->error, sizeof host->error, "%s", caught.message);
  return host->error;
}
typedef struct attach_call { standalone_host *host; qa_app_demo_recording_sink sink; const void *token; } attach_call;
static void attach_body(void *ctx) {
  attach_call *call = ctx;
  if (call->host->feed == NULL) qa_raise("Error", 0, 0, "Recording has no prepared feed");
  call->token = feed_attach(call->host->feed, call->sink);
}
static const void *standalone_attach(void *ctx, qa_app_demo_recording_sink sink) {
  standalone_host *host = ctx;
  attach_call call = {host, sink, NULL};
  qa_caught_error caught;
  if (!qa_try(attach_body, &call, &caught)) qa_rethrow(&caught);
  return call.token;
}
static void standalone_detach(void *ctx, const void *token) {
  (void)ctx; /* the token carries its feed */
  feed_detach(token);
}
static const void *standalone_reconnect(void *ctx, qa_command_context source, qa_app_demo_recording_sink sink, const char **out_error) {
  (void)ctx; (void)source; (void)sink; /* TS reconnectRecording: async () => { throw ... } */
  *out_error = "rerecord requires the retained client connection owner";
  return NULL;
}
static void standalone_reconnect_detach(void *ctx, const void *token) { (void)ctx; (void)token; /* nothing is attached */ }
static void standalone_print(void *ctx, const char *text) {
  standalone_host *host = ctx;
  if (host->app->host.base.print != NULL) host->app->host.base.print(host->app->host.base.ctx, text);
}
static void standalone_stage(void *ctx, qa_app_demo_recording_intent intent) {
  standalone_host *host = ctx;
  bool no_name = strcmp(intent.kind, "stop") == 0 || strcmp(intent.kind, "mvdstop") == 0 || strcmp(intent.kind, "serverstop") == 0
      || intent.name == NULL;
  const char *args[1] = {intent.name};
  qa_remote_application_queue_command(host->app, intent.kind, args, no_name ? 0 : 1, NULL, &intent.source);
}
qa_app_client_demo_recording *qa_remote_application_standalone_recording(qa_remote_application *app) {
  if (app->ownership.borrowed) qa_raise("Error", 0, 0, "Borrowed sources use the retained client recorder");
  if (app->owned_recording != NULL) return app->owned_recording;
  standalone_host *host = calloc(1, sizeof *host);
  if (host == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(&app->arena, free, host);
  host->app = app;
  qa_app_demo_recording_host options;
  memset(&options, 0, sizeof options);
  options.ctx = host;
  options.root = standalone_root;
  options.main_feed.ctx = host;
  options.main_feed.seed = standalone_seed;
  options.main_feed.attach = standalone_attach;
  options.main_feed.detach = standalone_detach;
  options.reconnect_recording = standalone_reconnect;
  options.reconnect_detach = standalone_reconnect_detach;
  options.print = standalone_print;
  options.stage = standalone_stage;
  app->owned_recording = qa_app_client_demo_recording_create(options);
  if (app->owned_recording == NULL) qa_range_error("Out of memory");
  return app->owned_recording;
}
/* TS attachConnectingRecording(sink): the live QuakeWorld signon recorder. */
void *qa_remote_application_attach_connecting_recording(qa_remote_application *app, qa_app_qw_recording_sink sink) {
  const qa_remote_live_network *network = qa_remote_application_network(app);
  if (app->closed || app->closing || network == NULL || network->family != QA_APP_REMOTE_FAMILY_QW)
    qa_raise("Error", 0, 0, "Signon recording requires a live QuakeWorld connection");
  return qa_app_qw_client_network_recording_attach(network->qw, sink);
}
/* The TS detach closure attachConnectingRecording returns: the QuakeWorld recording that holds
 * the token removes it (a recording the token no longer names ignores it). */
void qa_remote_application_detach_connecting_recording(qa_remote_application *app, void *token) {
  const qa_remote_live_network *network = qa_remote_application_network(app);
  if (network == NULL || network->family != QA_APP_REMOTE_FAMILY_QW) return;
  qa_app_qw_client_network_recording_detach(network->qw, token);
}

/* ---- executeApplicationRequest (TS 3043-3045) -------------------------- */
void qa_remote_application_execute_application_request(qa_remote_application *app,
    const qa_app_configuration_command_request *request) {
  qa_remote_command command;
  memset(&command, 0, sizeof command);
  command.name = (char *)request->name;
  command.args = (char **)request->arguments;
  command.arg_count = request->argument_count;
  command.has_seat = request->has_seat;
  command.seat = request->seat;
  command.has_source = true;
  command.source = request->source;
  qa_remote_application_execute_remote_command(app, &command);
}

/* ---- executeRemoteCommand (TS 3047-3231) ------------------------------- */
typedef struct execute_call {
  qa_remote_application *app;
  const qa_remote_command *command;
  bool has_source; qa_command_context source;
} execute_call;
static void execute_print(execute_call *call, const char *text) {
  if (call->has_source || !call->command->has_seat) qa_remote_application_print(call->app, text, call->has_source ? &call->source : NULL);
  else if (call->app->host.base.print != NULL) call->app->host.base.print(call->app->host.base.ctx, text);
}
static void execute_print_ctx(void *ctx, const char *text) { execute_print(ctx, text); }
static void execute_printf(execute_call *call, const char *format, ...) {
  char line[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  execute_print(call, line);
}
static bool configuration_has_seat(const qa_remote_configuration *configuration, qa_seat_id id) {
  for (size_t i = 0; i < configuration->seat_count; i++)
    if (qa_same_seat(qa_session_seat_id(configuration->seats[i]), id)) return true;
  return false;
}
static qa_remote_seat_peer *open_peer(qa_remote_application *app, const qa_remote_command *command) {
  if (!command->has_seat) return NULL;
  for (size_t i = 0; i < app->remote_seat_count; i++)
    if (!app->remote_seats[i]->closed && qa_same_seat(qa_session_seat_id(app->remote_seats[i]->seat), command->seat))
      return app->remote_seats[i];
  return NULL;
}
static void raise_text(const char *text) { qa_raise("Error", 0, 0, "%s", text); }
/* TS Number.isSafeInteger(value). */
static bool is_safe_integer(double value) {
  return value == value && value >= -9007199254740991.0 && value <= 9007199254740991.0 && value == (double)(int64_t)value;
}
static bool all_digits(const char *text) {
  if (text == NULL || text[0] == '\0') return false;
  for (const char *at = text; *at != '\0'; at++) if (*at < '0' || *at > '9') return false;
  return true;
}
typedef struct cinematic_next { qa_remote_application *app; bool has_source; qa_command_context source; } cinematic_next;
static void cinematic_ended_next(void *ctx) {
  cinematic_next *next_ctx = ctx;
  qa_remote_command_owner *owner = next_ctx->app->client_command_owner;
  if (owner == NULL) return;
  char *next = qa_cvar_registry_variable_string(owner->cvars, "nextmap");
  if (next == NULL) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, next);
  qa_remote_cvar_set(owner->cvars, "nextmap", "", true);
  if (next[0] != '\0') {
    size_t length = strlen(next) + 2;
    char *text = malloc(length);
    if (text == NULL) qa_range_error("Out of memory");
    snprintf(text, length, "%s\n", next);
    qa_command_buffer_append(owner->commands, text, next_ctx->has_source ? &next_ctx->source : NULL, -1);
    free(text);
  }
  qa_cleanup_pop(&guard);
  free(next);
}
static void release_cinematic_next(void *object) {
  cinematic_next *next = object;
  if (next->has_source) {
    qa_remote_command command;
    memset(&command, 0, sizeof command);
    command.has_source = true;
    command.source = next->source;
    qa_remote_command_free(&command);
  }
  free(next);
}
static void execute_body(void *ctx) {
  execute_call *call = ctx;
  qa_remote_application *app = call->app;
  const qa_remote_command *command = call->command;
  const char *name = command->name;
  const char *const *args = (const char *const *)command->args;
  size_t arg_count = command->arg_count;
  if (call->has_source) {
    if (call->source.session.token != qa_engine_session_id(app->session).token)
      raise_text("Remote command belongs to another session");
    const qa_command_origin *origin = &call->source.origin;
    while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
    if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) raise_text("Remote gameplay commands require a local client");
    if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
      bool found = false;
      for (size_t i = 0; i < app->configuration->seat_count && !found; i++) {
        qa_session_seat *seat = app->configuration->seats[i];
        found = qa_same_seat(qa_session_seat_id(seat), origin->seat)
            && qa_same_client(qa_session_client_id(qa_session_seat_client(seat)), origin->client);
      }
      if (!found) raise_text("Remote command belongs to a retired local client");
    }
  }
  if (command->has_seat && !configuration_has_seat(app->configuration, command->seat))
    raise_text("Remote command belongs to an inactive local seat");
  if (strcmp(name, "local_join") == 0) {
    if (arg_count != 0) raise_text("Usage: local_join");
    qa_remote_application_join_remote_seat(app);
    return;
  }
  if (strcmp(name, "local_drop") == 0) {
    double index = arg_count == 0 ? (command->has_seat ? (double)command->seat.index : -1)
        : qa_js_string_to_number(args[0]) - 1;
    bool defined = arg_count != 0 || command->has_seat;
    if (arg_count > 1 || !defined || !is_safe_integer(index) || index < 0) raise_text("Usage: local_drop [player number]");
    qa_remote_application_drop_remote_seat(app, (int64_t)index);
    return;
  }
  if (strcmp(name, "color") == 0 && app->family == QA_APP_DEMO_QW) {
    qa_cvar_registry *cvars = !command->has_seat ? app->configuration->source
        : qa_remote_configuration_seat_source(app->configuration, command->seat);
    if (cvars == NULL) raise_text("Color command has no source seat");
    if (arg_count == 0) {
      char *top = qa_cvar_registry_variable_string(cvars, "topcolor"), *bottom = qa_cvar_registry_variable_string(cvars, "bottomcolor");
      if (top == NULL || bottom == NULL) { free(top); free(bottom); qa_range_error("Out of memory"); }
      execute_printf(call, "\"color\" is \"%s %s\"\n", top, bottom);
      free(top);
      free(bottom);
      return;
    }
    double top = qa_js_min(13, (double)(qa_native_atoi(args[0]) & 15));
    double bottom = qa_js_min(13, (double)(qa_native_atoi(arg_count > 1 ? args[1] : args[0]) & 15));
    char text[40];
    qa_net_js_number(top, text, sizeof text);
    qa_remote_cvar_set(cvars, "topcolor", text, false);
    qa_net_js_number(bottom, text, sizeof text);
    qa_remote_cvar_set(cvars, "bottomcolor", text, false);
    return;
  }
  if (strcmp(name, "in_restart") == 0 || strcmp(name, "midiinfo") == 0) {
    qa_app_application_input *input = app->controls;
    if (input == NULL) {
      execute_print(call, "Input devices are not active before signon.\n");
      return;
    }
    if (strcmp(name, "midiinfo") == 0) qa_app_input_devices_info(qa_app_application_input_input_devices(input));
    else {
      qa_app_application_input_release_for_profile_change(input, NULL);
      qa_app_input_devices_restart(qa_app_application_input_input_devices(input));
      qa_input_router_restart(qa_app_application_input_router(input));
    }
    return;
  }
  if (strcmp(name, "record") == 0 || strcmp(name, "stop") == 0 || strcmp(name, "stoprecord") == 0 || strcmp(name, "rerecord") == 0) {
    qa_command_context context;
    if (call->has_source) context = call->source;
    else {
      memset(&context, 0, sizeof context);
      context.session = qa_engine_session_id(app->session);
      context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
    }
    const char *error = NULL;
    if (strcmp(name, "stop") == 0 || strcmp(name, "stoprecord") == 0)
      error = qa_app_client_demo_recording_stop(qa_remote_application_standalone_recording(app));
    else {
      const char *selected = arg_count > 0 ? args[0] : NULL;
      if (arg_count > 1 || (strcmp(name, "rerecord") == 0 && selected == NULL)) qa_raise("Error", 0, 0, "Usage: %s <name>", name);
      if (strcmp(name, "rerecord") == 0) error = qa_app_client_demo_recording_rerecord(qa_remote_application_standalone_recording(app), selected, context);
      else error = qa_app_client_demo_recording_start(qa_remote_application_standalone_recording(app), selected, context);
    }
    if (error != NULL) raise_text(error);
    return;
  }
  if (strcmp(name, "cinematic") == 0) {
    const char *selected = arg_count > 0 ? args[0] : NULL, *mode = arg_count > 1 ? args[1] : NULL;
    if (selected == NULL || arg_count > 2) raise_text("Usage: cinematic <name> [loop|hold]");
    cinematic_next *next = calloc(1, sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    next->app = app;
    next->has_source = call->has_source;
    if (call->has_source) next->source = qa_remote_command_context_copy(&call->source);
    qa_remote_arena_add(&app->arena, release_cinematic_next, next);
    bool loop = mode != NULL && (strcmp(mode, "2") == 0 || strcmp(mode, "loop") == 0);
    bool hold = mode != NULL && (strcmp(mode, "1") == 0 || strcmp(mode, "hold") == 0);
    qa_remote_application_start_cinematic(app, selected, loop, hold, false, cinematic_ended_next, next);
    return;
  }
  if (strcmp(name, "cinematicpause") == 0 || strcmp(name, "stopcinematic") == 0) {
    if (app->cinematic == NULL) raise_text("No cinematic is playing");
    if (strcmp(name, "stopcinematic") == 0) qa_remote_application_stop_cinematic(app);
    else qa_app_campaign_cinematic_pause(app->cinematic->movie, strcmp(qa_app_campaign_cinematic_status(app->cinematic->movie), "paused") != 0);
    return;
  }
  const qa_remote_source *source = qa_remote_application_source(app);
  if (source->kind != QA_REMOTE_SOURCE_LIVE && (strcmp(name, "follow") == 0 || strcmp(name, "chase") == 0)) {
    const char *selected = arg_count > 0 ? args[0] : NULL;
    if (arg_count != 1 || !all_digits(selected) || !is_safe_integer(qa_js_string_to_number(selected)))
      qa_raise("Error", 0, 0, "Usage: %s <source player ID>; Recorded free camera is not available", name);
    double player = qa_js_string_to_number(selected);
    if (source->kind == QA_REMOTE_SOURCE_GTV) qa_app_gtv_remote_source_select_player(source->watch, player);
    else qa_app_recorded_source_select_player(source->playback, player);
    return;
  }
  if (strcmp(name, "demopause") == 0) {
    if (source->kind != QA_REMOTE_SOURCE_RECORDED) raise_text("Demo pause requires active playback");
    qa_app_recorded_source_set_paused(source->playback, !qa_app_recorded_source_is_paused(source->playback));
    return;
  }
  if (strcmp(name, "downloadstatus") == 0) {
    size_t count = 0;
    const qa_app_client_download_progress *progress = NULL;
    if (!command->has_seat) progress = qa_remote_application_download_progress(app, &count);
    else if (qa_same_seat(command->seat, qa_remote_application_seat_id(app))) {
      count = qa_remote_application_primary_download_progress(app, 0);
      progress = app->progress_rows;
    } else {
      qa_remote_seat_peer *peer = open_peer(app, command);
      if (peer != NULL) progress = qa_app_remote_seat_source_download_progress(peer->source, &count);
    }
    if (count == 0) execute_print(call, "No downloads are pending.\n");
    for (size_t i = 0; i < count; i++) {
      char received[40], total[40];
      qa_net_js_number(progress[i].received, received, sizeof received);
      if (progress[i].has_total) qa_net_js_number(progress[i].total, total, sizeof total);
      else snprintf(total, sizeof total, "?");
      execute_printf(call, "%s: %s %s/%s bytes (%s)\n", progress[i].path, progress[i].phase == QA_APP_DL_PENDING ? "pending" : progress[i].phase == QA_APP_DL_RUNNING ? "running" : "done",
          received, total, progress[i].transport == QA_APP_DL_HTTP ? "http" : "native");
    }
    return;
  }
  if (strcmp(name, "stopdownload") == 0) {
    qa_remote_application_cancel_downloads(app, command->has_seat ? &command->seat : NULL);
    execute_print(call, "Downloads paused. Use retrydownload to resume.\n");
    return;
  }
  if (strcmp(name, "retrydownload") == 0) {
    qa_remote_application_retry_downloads(app, command->has_seat ? &command->seat : NULL);
    return;
  }
  if (strcmp(name, "quit") == 0) {
    qa_remote_application_request_quit(app);
    return;
  }
  if (strcmp(name, "disconnect") == 0) {
    qa_remote_seat_peer *peer = open_peer(app, command);
    if (peer == NULL) qa_remote_application_request_quit(app);
    else qa_remote_application_retire_remote_seat(app, peer, true);
    return;
  }
  if (qa_remote_application_browser_command(app, name, args, arg_count, execute_print_ctx, call)) return;
  qa_remote_seat_peer *peer = open_peer(app, command);
  const qa_remote_live_network *network = peer != NULL ? qa_app_remote_seat_source_network(peer->source)
      : qa_remote_application_network(app);
  if (network != NULL && network->family == QA_APP_REMOTE_FAMILY_QW && (strcmp(name, "skins") == 0 || strcmp(name, "allskins") == 0)) {
    if (strcmp(name, "allskins") == 0) {
      const char *value = arg_count > 0 ? args[0] : "";
      if (peer == NULL) {
        char *copy = strdup(value);
        if (copy == NULL) qa_range_error("Out of memory");
        free(app->qw_all_skins);
        app->qw_all_skins = copy;
      } else qa_app_remote_seat_source_set_all_skins(peer->source, value);
    }
    qa_app_qw_client_network_refresh_skins(network->qw);
    return;
  }
  if (app->frontend != NULL) {
    qa_app_audio_command_request request;
    memset(&request, 0, sizeof request);
    request.name = name;
    request.args = args;
    request.arg_count = arg_count;
    request.has_seat = command->has_seat;
    request.seat = command->seat;
    qa_app_application_q3_client *primary_q3 = qa_remote_presentation_q3_client(qa_remote_application_presentation(app));
    if (primary_q3 != NULL)
      request.registrations = qa_q3_pres_presentation_sound_bank_registrations(
          qa_app_application_q3_assets_bank(qa_app_application_q3_client_media(primary_q3)), &request.registration_count);
    request.print = execute_print_ctx;
    request.print_ctx = call;
    if (qa_app_application_audio_command(app->frontend->audio, &request)) return;
  }
  if (strcmp(name, "map") == 0 || strcmp(name, "save") == 0 || strcmp(name, "load") == 0)
    qa_raise("Error", 0, 0, "%s requires the authoritative server console", name);
  size_t player_count = 0;
  const qa_app_local_player *players = qa_remote_application_local_players(app, &player_count);
  const qa_app_local_player *player = NULL;
  for (size_t i = 0; i < player_count && player == NULL; i++)
    if (!command->has_seat || qa_same_seat(qa_session_seat_id(players[i].seat), command->seat)) player = &players[i];
  if (player == NULL || strcmp(qa_remote_application_network_phase(app), "active") != 0)
    raise_text("Remote command requires a connected local player");
  qa_app_local_player selected = *player;
  const qa_remote_presentation *remote = qa_remote_application_source_for_actor(app, selected.actor);
  qa_app_application_q3_client *q3 = peer == NULL ? qa_remote_presentation_q3_client(qa_remote_application_presentation(app))
      : peer->view != NULL ? qa_remote_presentation_q3_client(qa_app_remote_seat_view_presentation(peer->view)) : NULL;
  if (strcmp(name, "centerview") == 0) {
    qa_app_local_input *local = NULL;
    size_t count = app->controls != NULL ? qa_app_application_input_local_count(app->controls) : 0;
    for (size_t i = 0; i < count && local == NULL; i++) {
      qa_app_local_input *candidate = qa_app_application_input_local(app->controls, i);
      if (qa_same_actor(candidate->player.actor, selected.actor)) local = candidate;
    }
    if (local != NULL) {
      bool has_state = false;
      qa_vec3 delta = {0, 0, 0};
      if (qa_input_command_builder_dialect(local->builder) == QA_COMMAND_DIALECT_Q3 && q3 != NULL) {
        const qa_q3_pres_snapshot_source *snapshots = &qa_app_application_q3_client_options_of(q3)->source->snapshots;
        qa_q3_wire_snapshot snapshot;
        if (snapshots->read(snapshots->ctx, snapshots->current(snapshots->ctx).number, &snapshot)) {
          has_state = true;
          delta = snapshot.player_state.delta_angles;
          qa_q3_wire_snapshot_destroy(&snapshot);
        }
      }
      qa_vec3 angles = qa_input_command_builder_view_angles(local->builder);
      angles.x = !has_state ? 0 : -(double)(int16_t)(uint16_t)(uint32_t)qa_js_to_int32(delta.x) * (360.0 / 65536.0);
      qa_input_command_builder_set_view_angles(local->builder, angles);
    }
    return;
  }
  if (q3 != NULL) {
    if (strcmp(name, "use") == 0) {
      qa_app_sim_player_ui ui = qa_remote_presentation_player_ui(remote, selected.actor);
      for (size_t i = 0; i < ui.item_count; i++) {
        if (arg_count == 0 || ui.items[i].id == NULL || strcmp(ui.items[i].id, args[0]) != 0) continue;
        char ordinal[40];
        qa_net_js_number(ui.items[i].source_ordinal, ordinal, sizeof ordinal);
        const char *argv[2] = {"weapon", ordinal};
        if (qa_app_application_q3_client_command(q3, argv, 2)) return;
        break;
      }
    }
    const char **argv = calloc(arg_count + 1, sizeof *argv);
    if (argv == NULL) qa_range_error("Out of memory");
    argv[0] = name;
    for (size_t i = 0; i < arg_count; i++) argv[i + 1] = args[i];
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, argv);
    bool handled = qa_app_application_q3_client_command(q3, argv, arg_count + 1);
    qa_cleanup_pop(&guard);
    free(argv);
    if (handled) return;
  }
  if (source->kind != QA_REMOTE_SOURCE_LIVE) raise_text("Gameplay commands require a live server");
  qa_remote_presentation_player_command(remote, selected.actor, name, args, arg_count);
}
void qa_remote_application_execute_remote_command(qa_remote_application *app, const qa_remote_command *command) {
  execute_call call;
  memset(&call, 0, sizeof call);
  call.app = app;
  call.command = command;
  if (command->has_source) { call.has_source = true; call.source = command->source; }
  else if (command->has_seat && app->controls != NULL) {
    size_t count = qa_app_application_input_local_count(app->controls);
    for (size_t i = 0; i < count; i++) {
      qa_app_local_input *local = qa_app_application_input_local(app->controls, i);
      if (!qa_same_seat(qa_session_seat_id(local->player.seat), command->seat)) continue;
      call.has_source = true;
      call.source.session = qa_engine_session_id(app->session);
      call.source.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
      call.source.origin.seat = qa_session_seat_id(local->player.seat);
      call.source.origin.client = qa_session_client_id(qa_session_seat_client(local->player.seat));
      break;
    }
  }
  qa_caught_error caught;
  if (!qa_try(execute_body, &call, &caught)) {
    /* TS catch: print(`${error instanceof Error ? error.message : String(error)}\n`) */
    char line[QA_CAUGHT_MESSAGE_CAPACITY + 2];
    snprintf(line, sizeof line, "%s\n", caught.message);
    execute_print(&call, line);
  }
}

/* ---- underwater (TS 3233-3253) ----------------------------------------- */
bool qa_remote_application_underwater(qa_remote_application *app, qa_scene_camera camera, qa_actor_id actor,
    const qa_scene_queries *scene) {
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(qa_remote_application_content(app));
  const qa_provider_timing *timing = NULL;
  for (size_t i = 0; i < recipe->timing_count && timing == NULL; i++)
    if (strcmp(recipe->timing[i].provider, recipe->engine_behavior.provider) == 0) timing = &recipe->timing[i];
  if (timing == NULL) qa_raise("Error", 0, 0, "Remote world has no numeric profile for camera contents");
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = camera.origin;
  query.target.is_model = false;
  query.pass_actor = &actor;
  query.numeric = timing->numeric;
  if (app->family == QA_APP_DEMO_QW || app->family == QA_APP_DEMO_Q1) {
    query.policy.kind = "q1";
    query.policy.move = "normal";
    query.policy.has_hull = false;
  } else if (app->family == QA_APP_DEMO_Q3) {
    query.policy.kind = "q3";
    query.policy.contents_mask = -1;
    query.policy.curves = true;
    query.policy.player_curve_clip = true;
  } else {
    query.policy.kind = "q2";
    query.policy.contents_mask = -1;
    query.policy.leaf_contents = "merged";
  }
  qa_point_contents_result contents = scene->point_contents(scene->ctx, query);
  if (strcmp(contents.kind, "q1") == 0) return contents.contents <= -3 && contents.contents >= -5;
  if (strcmp(contents.kind, "q3") == 0) return (qa_js_to_int32(contents.contents) & 56) != 0;
  return strcmp(contents.kind, "q2") == 0 && (qa_js_to_int32(contents.merged) & 56) != 0;
}

/* ---- input (TS 2908-2911) ---------------------------------------------- */
bool qa_remote_application_input(qa_remote_application *app, const qa_seat_input_event *event) {
  if (app->closed || app->closing) qa_raise("Error", 0, 0, "Remote application is closed");
  if (app->cinematic != NULL) return qa_app_campaign_cinematic_input(app->cinematic->movie, event);
  return app->controls != NULL ? qa_app_application_input_input(app->controls, event) : false;
}

/* ---- prepareRecording as the ClientRecordingFeed of client.source.current (TS 2989-3014) ---- */
typedef struct lifetime_feed { recording_feed *feed; qa_app_demo_recording_seed seed; qa_command_context source; } lifetime_feed;
static void lifetime_seed_body(void *ctx) {
  lifetime_feed *feed = ctx;
  feed->seed = feed_seed(feed->feed);
}
static const char *lifetime_seed(void *ctx, const void **out_seed) {
  lifetime_feed *feed = ctx;
  qa_caught_error caught;
  if (!qa_try(lifetime_seed_body, feed, &caught)) {
    snprintf(feed->feed->app->lifetime_error, sizeof feed->feed->app->lifetime_error, "%s", caught.message);
    return feed->feed->app->lifetime_error;
  }
  *out_seed = &feed->seed;
  return NULL;
}
static const void *lifetime_attach(void *ctx, const void *sink) {
  lifetime_feed *feed = ctx;
  return feed_attach(feed->feed, *(const qa_app_demo_recording_sink *)sink);
}
static void lifetime_detach(void *ctx, const void *token) {
  (void)ctx; /* the token carries its feed */
  feed_detach(token);
}
typedef struct lifetime_prepare { qa_remote_application *app; qa_command_context source; lifetime_feed *feed; } lifetime_prepare;
static void lifetime_prepare_body(void *ctx) {
  lifetime_prepare *prepare = ctx;
  prepare->feed = calloc(1, sizeof *prepare->feed);
  if (prepare->feed == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(&prepare->app->arena, free, prepare->feed);
  prepare->feed->feed = prepare_feed(prepare->app, &prepare->source);
}
const char *qa_remote_application_prepare_recording_feed(qa_remote_application *app, qa_command_context source,
    qa_app_client_recording_feed *out) {
  lifetime_prepare prepare = {app, source, NULL};
  qa_caught_error caught;
  if (!qa_try(lifetime_prepare_body, &prepare, &caught)) {
    snprintf(app->lifetime_error, sizeof app->lifetime_error, "%s", caught.message);
    return app->lifetime_error;
  }
  memset(out, 0, sizeof *out);
  out->ctx = prepare.feed;
  out->root = prepare.feed->feed->root;
  out->seed = lifetime_seed;
  out->attach = lifetime_attach;
  out->detach = lifetime_detach;
  return NULL;
}
