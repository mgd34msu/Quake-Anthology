/*
 * Port of src/world/scheduler.ts: the frame think scheduler.
 * The clocks helpers (copyTime/validateTime/sameTimeUnit/copyFrame,
 * TS owner src/world/session/clocks.ts, unported) are mirrored as
 * static helpers in the .c. Implements contracts qa_actor_schedule
 * through the adapter. TS Error throws abort identically.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SCHEDULER_H
#define QA_WORLD_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/time.h"
#include "qa/contracts/world.h"

/** TS ThinkCallback. */
typedef struct qa_think_callback {
  void *ctx;
  void (*call)(void *ctx, qa_owned_actor self, qa_frame_context frame);
} qa_think_callback;
typedef struct qa_think_callback_result {
  bool has_value;
  qa_think_callback value;
} qa_think_callback_result;

typedef struct qa_scheduler_clock {
  qa_provider_id provider;
  qa_clock_profile profile;
} qa_scheduler_clock;

/** TS SchedulerOptions. source_slot / execution_provider may be NULL. */
typedef struct qa_scheduler_options {
  qa_actor_registry actors;
  qa_frame_ordering ordering;
  const qa_scheduler_clock *clocks;
  size_t clock_count;
  void *ctx;
  bool (*source_slot)(void *ctx, qa_actor_id actor, int64_t *out); /* false = TS null */
  bool (*execution_provider)(void *ctx, qa_actor_id actor, qa_provider_id *out);
  qa_think_callback_result (*resolve)(void *ctx, qa_provider_id provider,
                                      qa_callback_id callback);
} qa_scheduler_options;

/** TS ProviderFrame. */
typedef struct qa_provider_frame {
  qa_provider_id provider;
  qa_frame_context frame;
} qa_provider_frame;

/* TS ThinkResult: kind "not-run"(reason) | "ran"(invocations, alive). */
typedef struct qa_think_result {
  bool ran;
  const char *reason; /* "unscheduled"|"boundary"|"not-due"|"stale" */
  double invocations;
  bool alive;
} qa_think_result;

typedef struct qa_think_advance_result {
  qa_actor_id actor;
  qa_think_result result;
} qa_think_advance_result;

/* source_slot may be NULL (TS default actor.slot): the fn returns the
 * slot for an actor. */
typedef int64_t (*qa_source_slot_fn)(void *ctx, qa_actor_id actor);
double qa_compare_invocation_order(qa_frame_ordering ordering, qa_invocation_order left,
                                   qa_invocation_order right, qa_source_slot_fn source_slot,
                                   void *source_slot_ctx);

typedef struct qa_source_time_result { bool has_value; qa_source_time value; } qa_source_time_result;
qa_source_time_result qa_think_callback_time(qa_clock_profile profile, qa_source_time due,
                                             qa_frame_context frame);

typedef struct qa_frame_scheduler qa_frame_scheduler;
/* NULL only on allocation failure. Options are copied. */
qa_frame_scheduler *qa_frame_scheduler_create(qa_scheduler_options options);
void qa_frame_scheduler_destroy(qa_frame_scheduler *scheduler);
void qa_frame_scheduler_schedule(qa_frame_scheduler *scheduler, qa_owned_actor actor,
                                 qa_callback_id callback, qa_think_timing timing);
void qa_frame_scheduler_cancel(qa_frame_scheduler *scheduler, qa_owned_actor actor);
qa_scheduled_think_result qa_frame_scheduler_pending(qa_frame_scheduler *scheduler,
                                                     qa_actor_id actor);
qa_think_result qa_frame_scheduler_run(qa_frame_scheduler *scheduler, qa_actor_id actor,
                                       qa_frame_context frame, qa_think_boundary boundary);
/* Results are scheduler-owned scratch. */
const qa_think_advance_result *qa_frame_scheduler_advance(qa_frame_scheduler *scheduler,
                                                          const qa_provider_frame *frames,
                                                          size_t frame_count,
                                                          qa_think_boundary boundary,
                                                          size_t *out_count);
void qa_frame_scheduler_close(qa_frame_scheduler *scheduler);
qa_actor_schedule qa_frame_scheduler_as_contract(qa_frame_scheduler *scheduler);

#endif
