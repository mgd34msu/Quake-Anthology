/*
 * Port of src/world/session/clocks.ts: source-time helpers, the
 * SourceClock, and per-provider runtime state. Frozen TS copies are
 * plain value copies here. TS plain Error throws abort identically;
 * RangeError throws use qa_range_error.
 * Provider/clock/random identity: TS compares object identity; here
 * clocks compare by pointer and random sources by ctx pointer.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_CLOCKS_H
#define QA_WORLD_SESSION_CLOCKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/session.h"
#include "qa/contracts/time.h"

qa_source_time qa_copy_time(qa_source_time time);
void qa_validate_time(qa_source_time time);
void qa_same_time_unit(qa_source_time left, qa_source_time right);
qa_frame_context qa_copy_frame(qa_frame_context frame);

/** TS SourceClock. */
typedef struct qa_source_clock {
  qa_frame_context current;
} qa_source_clock;

/* frame must be a nonnegative safe integer (TS RangeError). */
void qa_source_clock_init(qa_source_clock *clock, qa_source_time initial,
                          double frame);
qa_frame_context qa_source_clock_frame(const qa_source_clock *clock);
qa_frame_context qa_source_clock_advance(qa_source_clock *clock,
                                         qa_source_time elapsed,
                                         qa_frame_phase phase);
qa_frame_context qa_source_clock_enter(qa_source_clock *clock,
                                       qa_frame_phase phase);

#endif
