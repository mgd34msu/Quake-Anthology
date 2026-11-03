/* Port of src/app/bootstrap/frame-loop.ts. The TS loop awaits setImmediate()
 * after each frame to yield to the event loop; the synchronous C loop has no
 * other tasks to yield to. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/frame_loop.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include "qa/foundation/js.h"

static double monotonic_now(void *ctx) {
  (void)ctx;
  struct timespec now;
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}
static void monotonic_sleep(void *ctx, double milliseconds) {
  (void)ctx;
  struct timespec duration;
  duration.tv_sec = (time_t)(milliseconds / 1000.0);
  duration.tv_nsec = (long)((milliseconds - (double)duration.tv_sec * 1000.0) * 1e6);
  (void)nanosleep(&duration, NULL);
}

typedef struct step_call { qa_app_frame_loop_host *host; double elapsed; } step_call;
static void run_step(void *ctx) {
  step_call *call = ctx;
  call->host->step(call->host->ctx, call->elapsed);
}

void qa_app_run_frame_loop_clocked(qa_app_frame_loop_host host, qa_app_frame_loop_clock clock) {
  double previous = clock.now(clock.ctx);
  char state[128], after[128];
  while (host.alive(host.ctx)) {
    double now = clock.now(clock.ctx), elapsed = now - previous, minimum = host.minimum_milliseconds(host.ctx);
    if (elapsed < minimum) { clock.sleep(clock.ctx, qa_js_max(1, minimum - elapsed)); continue; }
    previous = now;
    host.clock_state(host.ctx, state, sizeof state);
    step_call call = { &host, elapsed };
    qa_caught_error caught;
    if (!qa_try(run_step, &call, &caught)) {
      if (host.recover == NULL) qa_rethrow(&caught);
      host.recover(host.ctx, &caught);
      previous = clock.now(clock.ctx);
    }
    host.clock_state(host.ctx, after, sizeof after);
    if (strcmp(after, state) != 0) previous = clock.now(clock.ctx);
  }
}

void qa_app_run_frame_loop(qa_app_frame_loop_host host) {
  qa_app_frame_loop_clock clock = { NULL, monotonic_now, monotonic_sleep };
  qa_app_run_frame_loop_clocked(host, clock);
}
