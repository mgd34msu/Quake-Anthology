/* Port of src/network/q3/clock.ts. GPL-2.0-or-later. */
#include "qa/network/q3/clock.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
struct qa_q3_client_clock {
  int64_t time, delta, old_time, old_frame_server_time;
  bool pending, extrapolated, active;
  const qa_q3_wire_snapshot *current;
  int64_t demo_base_time, demo_frames, demo_start;
};
static int64_t source_clock(int64_t v) {
  if (v < -((int64_t)0x80000000) || v > 0x7fffffff) qa_range_error("Q3 source clock arithmetic exceeds signed int32");
  return v;
}
static int64_t sc_add(int64_t a, int64_t b) { return source_clock(a + b); } /* int64 headroom ok: inputs are int32-scale */
bool qa_q3_client_clock_init(qa_q3_client_clock **out) { *out = calloc(1, sizeof **out); return *out != NULL; }
void qa_q3_client_clock_destroy(qa_q3_client_clock *c) { free(c); }
int64_t qa_q3_client_clock_time(const qa_q3_client_clock *c) { return c->time; }
int64_t qa_q3_client_clock_delta(const qa_q3_client_clock *c) { return c->delta; }
void qa_q3_client_clock_set_time(qa_q3_client_clock *c, int64_t t) { c->time = t; }
void qa_q3_client_clock_set_delta(qa_q3_client_clock *c, int64_t d) { c->delta = d; }
void qa_q3_client_clock_publish(qa_q3_client_clock *c, const qa_q3_wire_snapshot *s) { c->current = s; c->pending = true; }
void qa_q3_client_clock_clear(qa_q3_client_clock *c) {
  c->time = 0; c->delta = 0; c->old_time = 0; c->old_frame_server_time = 0;
  c->pending = false; c->extrapolated = false; c->current = NULL; c->active = false;
  c->demo_base_time = 0; c->demo_frames = 0; c->demo_start = 0;
}
bool qa_q3_client_clock_advance(qa_q3_client_clock *c, int64_t real_time, const qa_q3_clock_options *options, int64_t *out) {
  source_clock(real_time);
  const qa_q3_wire_snapshot *snapshot = c->current;
  if (!c->active) {
    if (!c->pending || !snapshot || (snapshot->flags & 2) != 0) return false;
    c->pending = false; c->active = true;
    c->delta = source_clock(snapshot->server_time - real_time);
    c->old_time = snapshot->server_time; c->demo_base_time = snapshot->server_time;
  }
  if (!snapshot) qa_common_error(QA_COMMON_ERROR_DROP, "CL_SetCGameTime: !cl.snap.valid");
  if (options->paused) { *out = c->time; return true; }
  if (snapshot->server_time < c->old_frame_server_time)
    qa_common_error(QA_COMMON_ERROR_DROP, "cl.snap.serverTime < cl.oldFrameServerTime");
  c->old_frame_server_time = snapshot->server_time;
  if (!options->demo || !options->freeze_demo) {
    int64_t nudge = (int64_t)trunc(options->time_nudge);
    if (nudge < -30) nudge = -30;
    if (nudge > 30) nudge = 30;
    c->time = source_clock(sc_add(real_time, c->delta) - nudge);
    if (c->time < c->old_time) c->time = c->old_time;
    c->old_time = c->time;
    if (sc_add(real_time, c->delta) >= source_clock(snapshot->server_time - 5)) c->extrapolated = true;
  }
  if (c->pending) {
    c->pending = false;
    if (!options->demo) {
      int64_t next = source_clock(snapshot->server_time - real_time);
      int64_t diff = source_clock(next - c->delta);
      int64_t distance = source_clock(diff < 0 ? -diff : diff);
      if (distance > 500) { c->delta = next; c->old_time = snapshot->server_time; c->time = snapshot->server_time; }
      else if (distance > 100) c->delta = sc_add(c->delta, next) >> 1;
      else if (options->timescale == 0 || options->timescale == 1) {
        if (c->extrapolated) { c->extrapolated = false; c->delta = source_clock(c->delta - 2); }
        else c->delta = source_clock(c->delta + 1);
      }
    }
  }
  if (options->demo && options->timedemo) {
    if (c->demo_start == 0) c->demo_start = real_time;
    c->demo_frames = source_clock(c->demo_frames + 1);
    c->time = source_clock(c->demo_base_time + source_clock(c->demo_frames * 50));
  }
  *out = c->time; return true;
}
bool qa_q3_client_clock_needs_demo_message(const qa_q3_client_clock *c) {
  return c->current && c->time >= c->current->server_time;
}
bool qa_q3_client_clock_demo_timing(const qa_q3_client_clock *c, int64_t milliseconds, int64_t *frames, int64_t *elapsed_ms) {
  int64_t elapsed = source_clock(milliseconds - c->demo_start);
  if (elapsed <= 0) return false;
  *frames = c->demo_frames; *elapsed_ms = elapsed; return true;
}
