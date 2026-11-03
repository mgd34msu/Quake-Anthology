/* Port of src/app/bootstrap/network/q3-demo.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q3_demo.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/q3/snapshot_history.h"
struct qa_app_q3_demo_playback {
  qa_app_q3_demo_playback_options options;
  qa_app_q3_client_bindings_storage bindings;
  qa_q3_client_connection *connection;
  const char *state;
  bool has_end; qa_q3_demo_result end;
  bool first_frame_skipped, busy;
};
static void assert_current(qa_app_q3_demo_playback *p) {
  if (strcmp(p->state, "closed") == 0)
    qa_raise("Error", 0, 0, "Q3 demo belongs to a retired source");
}
static void lc_assert(void *ctx) { assert_current(ctx); }
static void lc_cleared(void *ctx) {
  qa_app_q3_demo_playback *p = ctx;
  p->state = "loading";
}
static void lc_primed(void *ctx) {
  qa_app_q3_demo_playback *p = ctx;
  if (p->options.host.downloading(p->options.host.ctx))
    qa_range_error("Q3 demo cannot wait for package downloads");
  p->state = "primed";
}
static void lc_snapshot(void *ctx, const qa_q3_wire_snapshot *snapshot, double ping) {
  (void)ctx; (void)snapshot; (void)ping;
}
static bool reader_next(void *ctx, qa_q3_demo_sequence_fn on_sequence,
    void *seq_ctx, qa_q3_demo_result *out) {
  return qa_q3_demo_reader_next(ctx, on_sequence, seq_ctx, out);
}
qa_app_q3_demo_playback *qa_app_q3_demo_playback_create(qa_app_q3_demo_playback_options options) {
  qa_app_q3_demo_playback *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->options = options;
  p->state = "loading";
  qa_app_q3_client_lifecycle lifecycle = {p, lc_assert, lc_cleared, lc_primed, lc_snapshot};
  qa_app_q3_application_client_bindings(&p->bindings, options.host, lifecycle);
  qa_q3_client_mode mode = {0};
  mode.kind = QA_Q3_CLIENT_MODE_DEMO;
  mode.reader.next = reader_next;
  mode.reader.ctx = options.reader;
  if (!qa_q3_client_connection_init(&p->connection, options.host.identity,
          QA_Q3_PRODUCT_BASEQ3, &mode, &p->bindings.bindings)) {
    free(p); return NULL;
  }
  options.host.attach(options.host.ctx, p->connection);
  return p;
}
void qa_app_q3_demo_playback_destroy(qa_app_q3_demo_playback *playback) {
  if (!playback) return;
  qa_q3_client_connection_destroy(playback->connection);
  free(playback);
}
qa_q3_client_connection *qa_app_q3_demo_playback_connection(qa_app_q3_demo_playback *playback) {
  return playback->connection;
}
const qa_app_q3_demo_playback_options *qa_app_q3_demo_playback_options_of(
    const qa_app_q3_demo_playback *playback) { return &playback->options; }
const char *qa_app_q3_demo_playback_phase(const qa_app_q3_demo_playback *playback) {
  return playback->state;
}
static void begin(qa_app_q3_demo_playback *p) {
  assert_current(p);
  if (p->busy) qa_raise("Error", 0, 0, "Q3 demo message processing is already in progress");
  p->busy = true;
}
static const qa_q3_demo_result *read_one(qa_app_q3_demo_playback *p, double real_time) {
  assert_current(p);
  if (p->has_end) return &p->end;
  bool is_end = false;
  qa_q3_demo_result end;
  qa_q3_server_message message;
  qa_q3_client_connection_read_demo(p->connection, real_time, &is_end, &end, &message);
  /* Landed message out owns storage when filled; destroy it. */
  extern void qa_q3_server_message_destroy(qa_q3_server_message *message);
  if (!is_end) qa_q3_server_message_destroy(&message);
  assert_current(p);
  if (is_end) {
    p->end = end; p->has_end = true; p->state = "ended";
    return &p->end;
  }
  return NULL;
}
typedef struct prime_scope { qa_app_q3_demo_playback *p; double real_time; const qa_q3_demo_result *result; } prime_scope;
static void prime_body(void *ctx) {
  prime_scope *scope = ctx;
  qa_app_q3_demo_playback *playback = scope->p;
  const qa_q3_demo_result *result = NULL;
  while (strcmp(playback->state, "loading") == 0) {
    result = read_one(playback, scope->real_time);
    if (result) break;
  }
  if (!result && playback->has_end) result = &playback->end;
  scope->result = result;
}
/* TS: begin(); try { ... } finally { busy = false }. */
const qa_q3_demo_result *qa_app_q3_demo_playback_prime(
    qa_app_q3_demo_playback *playback, double real_time) {
  begin(playback);
  prime_scope scope = { playback, real_time, NULL };
  qa_caught_error caught;
  bool ok = qa_try(prime_body, &scope, &caught);
  playback->busy = false;
  if (!ok) qa_rethrow(&caught);
  return scope.result;
}
typedef struct frame_scope { qa_app_q3_demo_playback *p; double real_time; qa_app_recorded_q3_options options; qa_app_q3_demo_frame frame; } frame_scope;
static qa_app_q3_demo_frame advance_frame(qa_app_q3_demo_playback *p, double real_time, qa_app_recorded_q3_options options);
static void frame_body(void *ctx) {
  frame_scope *scope = ctx;
  scope->frame = advance_frame(scope->p, scope->real_time, scope->options);
}
qa_app_q3_demo_frame qa_app_q3_demo_playback_advance_frame(
    qa_app_q3_demo_playback *p, double real_time, qa_app_recorded_q3_options options) {
  begin(p);
  frame_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.p = p; scope.real_time = real_time; scope.options = options;
  qa_caught_error caught;
  bool ok = qa_try(frame_body, &scope, &caught);
  p->busy = false;
  if (!ok) qa_rethrow(&caught);
  return scope.frame;
}
static qa_app_q3_demo_frame advance_frame(
    qa_app_q3_demo_playback *p, double real_time, qa_app_recorded_q3_options options) {
  qa_app_q3_demo_frame frame = {0};
  frame.kind = "pending";
  if (p->has_end) goto completed;
  while (strcmp(p->state, "loading") == 0) {
    if (read_one(p, real_time)) goto completed;
  }
  if (!p->first_frame_skipped) { p->first_frame_skipped = true; goto done; }
  if (strcmp(p->state, "primed") == 0) {
    if (read_one(p, real_time)) goto completed;
    qa_q3_wire_snapshot snapshot;
    if (qa_q3_snapshot_history_latest(qa_q3_client_connection_history(p->connection), &snapshot)) {
      bool active = (snapshot.flags & 2) == 0;
      extern void qa_q3_wire_snapshot_destroy(qa_q3_wire_snapshot *snapshot);
      qa_q3_wire_snapshot_destroy(&snapshot);
      if (active) p->state = "active";
    }
  }
  if (strcmp(p->state, "active") != 0) goto done;
  {
    qa_q3_clock_options clock_options = {options.paused, options.time_nudge,
        options.timescale, true, options.freeze_demo, options.timedemo};
    int64_t time = 0;
    if (!qa_q3_client_clock_advance(p->options.clock, (int64_t)real_time, &clock_options, &time))
      goto done;
    while (qa_q3_client_clock_needs_demo_message(p->options.clock)) {
      if (read_one(p, real_time)) goto completed;
      if (strcmp(p->state, "active") != 0) goto done;
    }
    frame.kind = "frame";
    frame.server_time = (double)time;
  }
  goto done;
completed:
  frame.kind = "end";
  frame.end = p->end; frame.has_end = true;
  if (options.timedemo) {
    int64_t frames = 0, elapsed = 0;
    if (qa_q3_client_clock_demo_timing(p->options.clock, (int64_t)real_time, &frames, &elapsed)) {
      frame.has_timing = true;
      frame.timing.frames = (double)frames;
      frame.timing.elapsed_milliseconds = (double)elapsed;
    }
  }
done:
  return frame;
}
void qa_app_q3_demo_playback_close(qa_app_q3_demo_playback *playback) {
  playback->state = "closed";
}
