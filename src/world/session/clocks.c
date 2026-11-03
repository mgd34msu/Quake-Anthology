/* Port of src/world/session/clocks.ts. GPL-2.0-or-later. */
#include "qa/world/session/clocks.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static bool is_safe_integer(double value) {
  return isfinite(value) && trunc(value) == value &&
         value >= -9007199254740991.0 && value <= 9007199254740991.0;
}

qa_source_time qa_copy_time(qa_source_time time) { return time; }

void qa_validate_time(qa_source_time time) {
  if (!isfinite(time.value)) qa_range_error("Source time must be finite");
}

void qa_same_time_unit(qa_source_time left, qa_source_time right) {
  if (left.kind != right.kind)
    qa_range_error("Source clocks need an explicit unit conversion");
}

qa_frame_context qa_copy_frame(qa_frame_context frame) {
  frame.time = qa_copy_time(frame.time);
  frame.elapsed = qa_copy_time(frame.elapsed);
  return frame;
}

void qa_source_clock_init(qa_source_clock *clock, qa_source_time initial,
                          double frame) {
  qa_validate_time(initial);
  if (!is_safe_integer(frame) || frame < 0)
    qa_range_error("Frame must be a nonnegative safe integer");
  qa_source_time zero = {initial.kind, 0};
  qa_frame_context start = {frame, initial, zero, QA_FRAME_PHASE_FRAME_ENTRY};
  clock->current = qa_copy_frame(start);
}

qa_frame_context qa_source_clock_frame(const qa_source_clock *clock) {
  return clock->current;
}

qa_frame_context qa_source_clock_advance(qa_source_clock *clock,
                                         qa_source_time elapsed,
                                         qa_frame_phase phase) {
  qa_validate_time(elapsed);
  qa_same_time_unit(clock->current.time, elapsed);
  if (elapsed.value < 0)
    qa_range_error("Elapsed source time cannot be negative");
  qa_source_time time = {elapsed.kind,
                         clock->current.time.value + elapsed.value};
  qa_validate_time(time);
  if (!is_safe_integer(clock->current.frame + 1))
    qa_range_error("Frame counter exhausted");
  qa_frame_context next = {clock->current.frame + 1, time, elapsed, phase};
  clock->current = qa_copy_frame(next);
  return clock->current;
}

qa_frame_context qa_source_clock_enter(qa_source_clock *clock,
                                       qa_frame_phase phase) {
  qa_frame_context next = clock->current;
  next.phase = phase;
  clock->current = qa_copy_frame(next);
  return clock->current;
}
