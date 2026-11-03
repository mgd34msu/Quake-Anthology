/* The RecordedRemoteSource inputs RemoteApplication.recordedSource builds
 * (remote-application.ts 703-723 over recorded-source.ts 44-69): the C
 * RecordedRemoteSource takes its per-family demo input as a playback table,
 * so the table here is TS RecordedRemoteSource's construction over the
 * application's presentation: NetQuakeDemoInput(NetQuakeDemoReader(bytes), remote),
 * QuakeWorldDemoInput(QuakeWorldDemoReader(bytes), remote), Q2DemoPlayback(bytes,
 * remote, mvdPresentation?, selectRecordedView) and Q3DemoPlayback({ host: remote,
 * clock: remote.clock, reader: DemoReader(bytes, path) }). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/q1/demos.h"
#include "qa/network/q3/demo.h"

/* ---- Q1: the Q1RemotePresentation as NetQuakeDemoInput's remote -------- */
static bool q1_has_player(void *ctx) {
  qa_app_q1_player player;
  return qa_app_q1_remote_player(ctx, &player);
}
static bool q1_has_output(void *ctx) { return qa_app_q1_remote_output(ctx) != NULL; }
static double q1_recorded_seconds(void *ctx) { return qa_app_q1_remote_recorded_seconds(ctx); }
static void q1_receive(void *ctx, qa_netquake_message *messages, size_t count, double now, qa_app_demo_assert_fn assert_current,
    void *assert_ctx) {
  qa_app_q1_remote_receive(ctx, messages, count, now, assert_current, assert_ctx);
}
static void q1_view_angles(void *ctx, qa_vec3 angles, bool demo) { qa_app_q1_remote_set_demo_view_angles(ctx, angles, demo); }
static void q1_sample_demo(void *ctx, double seconds) { (void)qa_app_q1_remote_sample_demo(ctx, seconds); }
static qa_app_q1_demo_remote q1_demo_remote(qa_app_q1_remote *remote) {
  qa_app_q1_demo_remote out = {remote, q1_has_player, q1_has_output, q1_recorded_seconds, q1_receive, q1_view_angles, q1_sample_demo};
  return out;
}

/* ---- QW: the QwRemotePresentation (and its shared Q1 state) ----------- */
static bool qw_has_player(void *ctx) {
  qa_app_q1_player player;
  return qa_app_qw_remote_player(ctx, &player);
}
static bool qw_has_output(void *ctx) { return qa_app_qw_remote_output(ctx) != NULL; }
static void qw_server_data(void *ctx, const qa_quakeworld_message *data) { qa_app_qw_remote_server_data(ctx, data); }
static void qw_game_state(void *ctx, const qa_quakeworld_message *data, const char *const *models, size_t model_count,
    const char *const *sounds, size_t sound_count, qa_app_demo_assert_fn assert_current, void *assert_ctx) {
  (void)qa_app_qw_remote_game_state(ctx, data, models, model_count, sounds, sound_count, assert_current, assert_ctx);
}
static void qw_receive(void *ctx, qa_quakeworld_message *messages, size_t count, double now, qa_app_demo_assert_fn assert_current,
    void *assert_ctx) {
  qa_app_qw_remote_receive(ctx, messages, count, now, assert_current, assert_ctx);
}
static void qw_view_angles(void *ctx, qa_vec3 angles, bool demo) {
  qa_app_q1_remote_set_demo_view_angles(qa_app_qw_remote_shared(ctx), angles, demo);
}
static void qw_sample_demo(void *ctx, double seconds) { (void)qa_app_q1_remote_sample_demo(qa_app_qw_remote_shared(ctx), seconds); }
static void qw_sample_presentation(void *ctx, double now) { (void)qa_app_qw_remote_sample_presentation(ctx, now); }
static void qw_prediction_sent(void *ctx, double sequence, qa_qw_user_command command, double now) {
  qa_app_qw_remote_prediction_sent(ctx, sequence, &command, now);
}
static void qw_prediction_acknowledged(void *ctx, double sequence, double now) {
  qa_app_qw_remote_prediction_acknowledged(ctx, sequence, now);
}
static qa_app_qw_demo_remote qw_demo_remote(qa_app_qw_remote *remote) {
  qa_app_qw_demo_remote out = {remote, qw_has_player, qw_has_output, qw_server_data, qw_game_state, qw_receive, qw_view_angles,
      qw_sample_demo, qw_sample_presentation, qw_prediction_sent, qw_prediction_acknowledged};
  return out;
}

/* ---- the playback table ------------------------------------------------ */
static qa_app_recorded_q1_result advance_q1(void *ctx, double elapsed_seconds, double frame, bool timedemo) {
  qa_remote_recorded_inputs *inputs = ctx;
  qa_app_q1_demo_frame step = {elapsed_seconds, frame, timedemo};
  qa_app_q1_demo_progress progress = inputs->family == QA_APP_REMOTE_FAMILY_QW
      ? qa_app_quakeworld_demo_input_advance(inputs->qw, step) : qa_app_netquake_demo_input_advance(inputs->q1, step);
  qa_app_recorded_q1_result result = {progress.phase, progress.reason};
  return result;
}
static qa_app_recorded_q2_result advance_q2(void *ctx, double target) {
  qa_app_q2_demo_advance advance = qa_app_q2_demo_playback_advance(((qa_remote_recorded_inputs *)ctx)->q2, target);
  qa_app_recorded_q2_result result = {advance.kind, advance.time_milliseconds};
  return result;
}
static qa_app_recorded_q2_result next_frame_q2(void *ctx) {
  qa_app_q2_demo_advance advance = qa_app_q2_demo_playback_next_frame(((qa_remote_recorded_inputs *)ctx)->q2);
  qa_app_recorded_q2_result result = {advance.kind, advance.time_milliseconds};
  return result;
}
static void sample_demo_q2(void *ctx, double milliseconds) {
  (void)qa_app_q2_remote_sample_demo(((qa_remote_recorded_inputs *)ctx)->remote->q2, milliseconds);
}
static void select_player_q2(void *ctx, double clientnum) {
  qa_app_q2_demo_playback_select_player(((qa_remote_recorded_inputs *)ctx)->q2, clientnum);
}
static const char *q3_end_reason(qa_q3_demo_end_reason reason) {
  switch (reason) {
    case QA_Q3_DEMO_TERMINATOR: return "terminator";
    case QA_Q3_DEMO_EOF: return "eof";
    case QA_Q3_DEMO_TRUNCATED_HEADER: return "truncated-header";
    case QA_Q3_DEMO_TRUNCATED_PAYLOAD: return "truncated-payload";
  }
  return "eof";
}
static qa_app_recorded_q3_result advance_frame_q3(void *ctx, double real_time, qa_app_recorded_q3_options options) {
  qa_app_q3_demo_frame frame = qa_app_q3_demo_playback_advance_frame(((qa_remote_recorded_inputs *)ctx)->q3, real_time, options);
  qa_app_recorded_q3_result result;
  memset(&result, 0, sizeof result);
  result.kind = frame.kind;
  if (frame.has_end) result.end_reason = q3_end_reason(frame.end.reason);
  if (frame.has_timing) result.timing = frame.timing;
  return result;
}
static void sample_presentation_q3(void *ctx, double real_time) {
  (void)qa_app_q3_remote_sample_presentation(((qa_remote_recorded_inputs *)ctx)->remote->q3, real_time);
}
static const char *phase_q3(void *ctx) { return qa_app_q3_demo_playback_phase(((qa_remote_recorded_inputs *)ctx)->q3); }
static void close_inputs(void *ctx) {
  qa_remote_recorded_inputs *inputs = ctx;
  if (inputs->q1 != NULL) qa_app_netquake_demo_input_close(inputs->q1);
  if (inputs->qw != NULL) qa_app_quakeworld_demo_input_close(inputs->qw);
  if (inputs->q3 != NULL) qa_app_q3_demo_playback_close(inputs->q3);
}
static void select_recorded_view(void *ctx, double clientnum) { qa_app_q2_remote_select_recorded_view(ctx, clientnum); }

qa_app_recorded_playback qa_remote_application_recorded_playback(qa_remote_application *app,
    const qa_app_recorded_demo_resource *resource, qa_remote_recorded_inputs *inputs) {
  const qa_remote_presentation *remote = &app->initial_remote;
  inputs->remote = remote;
  inputs->family = remote->family;
  qa_app_recorded_playback playback;
  memset(&playback, 0, sizeof playback);
  playback.ctx = inputs;
  playback.family = resource->kind;
  playback.close = close_inputs;
  if (resource->kind == QA_APP_REC_Q1 && remote->family == QA_APP_REMOTE_FAMILY_Q1) {
    qa_netquake_demo_reader *reader = calloc(1, sizeof *reader);
    if (reader == NULL) qa_range_error("Out of memory");
    inputs->reader = reader;
    qa_netquake_demo_reader_init(reader, resource->bytes, resource->length, 64000); /* TS default maxMessageBytes */
    inputs->q1 = qa_app_netquake_demo_input_create(reader, q1_demo_remote(remote->q1));
    if (inputs->q1 == NULL) qa_range_error("Out of memory");
    playback.advance_q1 = advance_q1;
  } else if (resource->kind == QA_APP_REC_QW && remote->family == QA_APP_REMOTE_FAMILY_QW) {
    qa_quakeworld_demo_reader *reader = calloc(1, sizeof *reader);
    if (reader == NULL) qa_range_error("Out of memory");
    inputs->reader = reader;
    qa_quakeworld_demo_reader_init(reader, resource->bytes, resource->length, 1450); /* TS default maxMessageBytes */
    inputs->qw = qa_app_quakeworld_demo_input_create(reader, qw_demo_remote(remote->qw));
    if (inputs->qw == NULL) qa_range_error("Out of memory");
    playback.advance_q1 = advance_q1;
  } else if (resource->kind == QA_APP_REC_Q2 && remote->family == QA_APP_REMOTE_FAMILY_Q2) {
    qa_app_q2_playback_header header;
    memset(&header, 0, sizeof header);
    qa_app_q2_read_playback_header(resource->bytes, resource->length, &header);
    bool mvd = header.is_mvd;
    qa_app_q2_playback_header_free(&header);
    qa_app_q2_mvd_presentation presentation;
    if (mvd) presentation = qa_remote_q2_mvd_presentation(remote->q2);
    inputs->q2 = qa_app_q2_demo_playback_create(resource->bytes, resource->length, qa_app_q2_remote_as_client_host(remote->q2),
        mvd ? &presentation : NULL, select_recorded_view, remote->q2);
    if (inputs->q2 == NULL) qa_range_error("Out of memory");
    playback.advance_q2 = advance_q2;
    playback.next_frame_q2 = next_frame_q2;
    playback.sample_demo_q2 = sample_demo_q2;
    playback.select_player_q2 = select_player_q2;
  } else if (resource->kind == QA_APP_REC_Q3 && remote->family == QA_APP_REMOTE_FAMILY_Q3) {
    qa_q3_demo_reader *reader = NULL;
    if (!qa_q3_demo_reader_init(&reader, resource->bytes, resource->length, resource->path)) qa_range_error("Out of memory");
    inputs->reader = reader;
    inputs->q3_reader = true;
    qa_app_q3_demo_playback_options options = {qa_app_q3_remote_as_client_host(remote->q3), qa_app_q3_remote_clock(remote->q3), reader};
    inputs->q3 = qa_app_q3_demo_playback_create(options);
    if (inputs->q3 == NULL) qa_range_error("Out of memory");
    playback.advance_frame_q3 = advance_frame_q3;
    playback.sample_presentation_q3 = sample_presentation_q3;
    playback.phase_q3 = phase_q3;
  } else qa_raise("Error", 0, 0, "Recording and remote presentation families differ");
  return playback;
}
void qa_remote_recorded_inputs_release(void *object) {
  qa_remote_recorded_inputs *inputs = object;
  if (inputs->q1 != NULL) qa_app_netquake_demo_input_destroy(inputs->q1);
  if (inputs->qw != NULL) qa_app_quakeworld_demo_input_destroy(inputs->qw);
  if (inputs->q2 != NULL) qa_app_q2_demo_playback_destroy(inputs->q2);
  if (inputs->q3 != NULL) qa_app_q3_demo_playback_destroy(inputs->q3);
  if (inputs->q3_reader) qa_q3_demo_reader_destroy(inputs->reader);
  else free(inputs->reader);
  free(inputs);
}
