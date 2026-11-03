/* Port of src/app/bootstrap/frame-loop.ts: the wall-clock loop shared by the
 * local and remote applications. Every member receives ctx. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_FRAME_LOOP_H
#define QA_APP_BOOTSTRAP_FRAME_LOOP_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/foundation/support.h"
/* TS FrameLoopHost. clock_state writes a NUL-terminated state text that
 * changes whenever a video restart or world operation happened. recover NULL
 * is TS absent (a faulting step ends the loop). */
typedef struct qa_app_frame_loop_host {
  void *ctx;
  bool (*alive)(void *ctx);
  double (*minimum_milliseconds)(void *ctx);
  void (*step)(void *ctx, double elapsed);
  void (*clock_state)(void *ctx, char *out, size_t capacity);
  void (*recover)(void *ctx, const qa_caught_error *error);
} qa_app_frame_loop_host;
/* TS runFrameLoop. Monotonic milliseconds come from now(ctx) when set
 * (tests), else CLOCK_MONOTONIC; sleep(ctx, ms) likewise (TS Bun.sleep). */
typedef struct qa_app_frame_loop_clock {
  void *ctx;
  double (*now)(void *ctx);
  void (*sleep)(void *ctx, double milliseconds);
} qa_app_frame_loop_clock;
void qa_app_run_frame_loop(qa_app_frame_loop_host host);
void qa_app_run_frame_loop_clocked(qa_app_frame_loop_host host, qa_app_frame_loop_clock clock);
#endif
