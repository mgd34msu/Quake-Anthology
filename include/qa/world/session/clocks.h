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

/** TS ProviderRuntimeState checkpoint() result. Arrays are malloc'd;
 * random states are deep copies. Release with the dispose call. */
typedef struct qa_provider_runtime_checkpoint {
  qa_save_clock *clocks;
  size_t clock_count;
  qa_save_random *random;
  size_t random_count;
} qa_provider_runtime_checkpoint;
void qa_provider_runtime_checkpoint_dispose(
    qa_provider_runtime_checkpoint *checkpoint);

/** TS ProviderRuntimeState. Registered clocks are borrowed. */
typedef struct qa_provider_runtime_entry {
  qa_provider_id provider;
  qa_source_clock *clock;
  qa_random_source random;
} qa_provider_runtime_entry;
typedef struct qa_provider_runtime_state {
  qa_provider_runtime_entry *entries;
  size_t count;
  size_t capacity;
  bool closed;
} qa_provider_runtime_state;

void qa_provider_runtime_state_init(qa_provider_runtime_state *state);
void qa_provider_runtime_state_destroy(qa_provider_runtime_state *state);
void qa_provider_runtime_state_register(qa_provider_runtime_state *state,
                                        qa_provider_id provider,
                                        qa_source_clock *clock,
                                        qa_random_source random);
qa_source_clock *qa_provider_runtime_state_clock(
    qa_provider_runtime_state *state, qa_provider_id provider);
qa_random_source qa_provider_runtime_state_random(
    qa_provider_runtime_state *state, qa_provider_id provider);
/* False only on allocation failure. */
bool qa_provider_runtime_state_checkpoint(
    qa_provider_runtime_state *state,
    qa_provider_runtime_checkpoint *out);
void qa_provider_runtime_state_close(qa_provider_runtime_state *state);
/* The state viewed as a TS {close} resource is session.h's concern;
 * WorldLifetime closes it directly. */

#endif
