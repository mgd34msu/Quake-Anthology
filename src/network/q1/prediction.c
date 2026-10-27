/* Port of src/network/q1/prediction.ts. GPL-2.0-or-later. */
#include "qa/network/q1/prediction.h"
#include "qa/contracts/time.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct qa_qw_prediction_history {
  struct { double sequence; qa_qw_prediction_frame frame; } *frames;
  size_t count, capacity;
  double latency_seconds;
};
void qa_qw_prediction_history_init(qa_qw_prediction_history *h) { memset(h, 0, sizeof *h); }
void qa_qw_prediction_history_destroy(qa_qw_prediction_history *h) { free(h->frames); memset(h, 0, sizeof *h); }
double qa_qw_prediction_history_latency(const qa_qw_prediction_history *h) { return h->latency_seconds; }
void qa_qw_prediction_history_set_latency(qa_qw_prediction_history *h, double seconds) { h->latency_seconds = seconds; }
static qa_qw_prediction_frame *find_frame(qa_qw_prediction_history *h, double sequence) {
  for (size_t i = 0; i < h->count; i++) if (h->frames[i].sequence == sequence) return &h->frames[i].frame;
  return NULL;
}
void qa_qw_prediction_history_record(qa_qw_prediction_history *h, qa_qw_prediction_frame frame) {
  qa_qw_prediction_frame *old = find_frame(h, frame.sequence);
  if (old) *old = frame;
  else {
    if (h->count == h->capacity) { size_t nc = h->capacity ? h->capacity * 2 : 16; h->frames = realloc(h->frames, nc * sizeof *h->frames); h->capacity = nc; }
    h->frames[h->count].sequence = frame.sequence; h->frames[h->count].frame = frame; h->count++;
  }
  size_t w = 0;
  for (size_t i = 0; i < h->count; i++)
    if (!(h->frames[i].sequence <= frame.sequence - 64)) h->frames[w++] = h->frames[i];
  h->count = w;
}
void qa_qw_prediction_history_acknowledged(qa_qw_prediction_history *h, double sequence, double received_at_seconds) {
  qa_qw_prediction_frame *frame = find_frame(h, sequence);
  if (!frame) return;
  double latency = received_at_seconds - frame->sent_at_seconds;
  if (latency < 0 || latency > 1) return;
  h->latency_seconds = latency < h->latency_seconds ? latency : h->latency_seconds + 0.001;
}
qa_quakeworld_move qa_qw_prediction_history_bundle(qa_qw_prediction_history *h, double sequence, double loss_percent) {
  qa_qw_prediction_frame *current = find_frame(h, sequence);
  qa_qw_prediction_frame *previous = find_frame(h, sequence - 1);
  qa_qw_prediction_frame *oldest = find_frame(h, sequence - 2);
  if (!current || !previous || !oldest) qa_range_error("Three recorded commands are required");
  qa_quakeworld_move out;
  out.oldest = oldest->command; out.previous = previous->command; out.current = current->command;
  out.loss_percent = loss_percent;
  return out;
}
static qa_vec3 lerp_vec(qa_vec3 a, qa_vec3 b, double f) {
  qa_vec3 o;
  o.x = (float)(a.x + f * (b.x - a.x));
  o.y = (float)(a.y + f * (b.y - a.y));
  o.z = (float)(a.z + f * (b.z - a.z));
  return o;
}
struct split_acc { qa_qw_user_command *cmds; size_t count, capacity; };
static void split_push(void *ctx, const qa_qw_user_command *command) {
  struct split_acc *a = ctx;
  if (a->count == a->capacity) { size_t nc = a->capacity ? a->capacity * 2 : 4; a->cmds = realloc(a->cmds, nc * sizeof *a->cmds); a->capacity = nc; }
  a->cmds[a->count++] = *command;
}
static qa_user_command as_user_command(const qa_qw_user_command *c) {
  qa_user_command out; memset(&out, 0, sizeof out);
  out.kind = QA_USER_COMMAND_QW; out.as.qw = *c; return out;
}
static qa_qw_prediction unavailable(const char *reason) {
  qa_qw_prediction out; memset(&out, 0, sizeof out);
  out.predicted = false; out.reason = reason; return out;
}
qa_qw_prediction qa_qw_prediction_history_predict(qa_qw_prediction_history *h, qa_qw_movement_input input,
                                                  double acknowledged_sequence, double outgoing_sequence,
                                                  double realtime_seconds, double push_latency_ms,
                                                  qa_movement_services services, qa_q1_movement_options options) {
  if (outgoing_sequence - acknowledged_sequence >= 63) return unavailable("history-overrun");
  double push = push_latency_ms < 0 ? push_latency_ms : 0;
  double candidate = realtime_seconds - h->latency_seconds - push / 1000;
  double target = candidate < realtime_seconds ? candidate : realtime_seconds;
  qa_qw_prediction_frame *ack_frame = find_frame(h, acknowledged_sequence);
  double previous_time = ack_frame ? ack_frame->sent_at_seconds : target;
  qa_vec3 previous_origin = input.state.as.qw.origin, previous_velocity = input.state.as.qw.velocity;
  qa_qw_movement_input current = input;
  qa_movement_result last; memset(&last, 0, sizeof last);
  bool has_last = false; double last_sequence = acknowledged_sequence;
  for (double sequence = acknowledged_sequence + 1; sequence < outgoing_sequence; sequence++) {
    qa_qw_prediction_frame *frame = find_frame(h, sequence);
    if (!frame) return unavailable("missing-command");
    struct split_acc acc; memset(&acc, 0, sizeof acc);
    qa_split_quakeworld_command(&frame->command, split_push, &acc);
    bool removed = false;
    for (size_t i = 0; i < acc.count; i++) {
      qa_qw_movement_input next_input = current;
      next_input.command = as_user_command(&acc.cmds[i]);
      next_input.command_sequence = sequence;
      next_input.execution = "prediction";
      next_input.frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS;
      next_input.frame.elapsed.value = acc.cmds[i].milliseconds;
      qa_movement_result result = qa_move_quakeworld(next_input, services, options);
      if (result.status && strcmp(result.status, "actor-removed") == 0) { removed = true; break; }
      last = result; has_last = true;
      current.state = result.state; current.arsenal = result.arsenal; current.animation = result.animation;
      current.command = next_input.command;
      current.command_sequence = sequence;
      current.execution = "prediction";
      current.frame = next_input.frame;
    }
    free(acc.cmds);
    if (removed) return unavailable("actor-removed");
    last_sequence = sequence;
    if (frame->sent_at_seconds >= target) {
      if (!has_last) return unavailable("no-pending-command");
      double f = frame->sent_at_seconds == previous_time ? 0
        : fmax(0, fmin(1, (target - previous_time) / (frame->sent_at_seconds - previous_time)));
      qa_vec3 no = last.state.as.qw.origin, nv = last.state.as.qw.velocity;
      bool teleport = fabs(previous_origin.x - no.x) > 128 || fabs(previous_origin.y - no.y) > 128 || fabs(previous_origin.z - no.z) > 128;
      qa_qw_prediction out; memset(&out, 0, sizeof out);
      out.predicted = true; out.result = last;
      out.origin = teleport ? no : lerp_vec(previous_origin, no, f);
      out.velocity = teleport ? nv : lerp_vec(previous_velocity, nv, f);
      out.sequence = sequence;
      return out;
    }
    previous_time = frame->sent_at_seconds;
    previous_origin = current.state.as.qw.origin;
    previous_velocity = current.state.as.qw.velocity;
  }
  if (!has_last) return unavailable("no-pending-command");
  qa_qw_prediction out; memset(&out, 0, sizeof out);
  out.predicted = true; out.result = last;
  out.origin = current.state.as.qw.origin; out.velocity = current.state.as.qw.velocity;
  out.sequence = last_sequence;
  return out;
}
