/* Port of src/world/scheduler.ts. GPL-2.0-or-later. */
#include "qa/world/scheduler.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/world/session/clocks.h"

static noreturn void qa_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}

/* validateTime/sameTimeUnit are qa_validate_time/qa_same_time_unit
 * (TS owner: src/world/session/clocks.ts); copyTime is a value copy
 * under C semantics. */

double qa_compare_invocation_order(qa_frame_ordering ordering, qa_invocation_order left,
                                   qa_invocation_order right, qa_source_slot_fn source_slot,
                                   void *source_slot_ctx) {
  if (ordering.kind == QA_FRAME_ORDERING_MIXED) {
    int64_t left_provider = -1, right_provider = -1;
    /* Array.prototype.indexOf: the first match. */
    for (size_t i = 0; i < ordering.provider_count; i++) {
      if (left_provider < 0 && str_eq(ordering.providers[i], left.provider)) left_provider = (int64_t)i;
      if (right_provider < 0 && str_eq(ordering.providers[i], right.provider)) right_provider = (int64_t)i;
    }
    if (left_provider < 0 || right_provider < 0)
      qa_range_error("Provider is absent from mixed frame ordering");
    if (left_provider != right_provider) return (double)(left_provider - right_provider);
  }
  int64_t left_slot = source_slot != NULL ? source_slot(source_slot_ctx, left.actor) : left.actor.slot;
  int64_t right_slot = source_slot != NULL ? source_slot(source_slot_ctx, right.actor) : right.actor.slot;
  double by_slot = (double)left_slot - (double)right_slot;
  return by_slot != 0 ? by_slot : left.sequence - right.sequence;
}

qa_source_time_result qa_think_callback_time(qa_clock_profile profile, qa_source_time due,
                                             qa_frame_context frame) {
  qa_validate_time(due);
  qa_validate_time(frame.time);
  qa_validate_time(frame.elapsed);
  qa_same_time_unit(due, frame.time);
  qa_same_time_unit(frame.time, frame.elapsed);
  if (frame.elapsed.value < 0) qa_range_error("Frame elapsed time cannot be negative");
  bool seconds = profile.kind == QA_CLOCK_Q1_NETQUAKE || profile.kind == QA_CLOCK_Q1_QUAKEWORLD ||
                 profile.kind == QA_CLOCK_Q2_CLASSIC;
  if (due.kind != (seconds ? QA_SOURCE_TIME_SECONDS : QA_SOURCE_TIME_MILLISECONDS))
    qa_range_error("Think clock has the wrong source unit");
  qa_source_time_result result;
  result.has_value = false;
  result.value = frame.time;
  if (due.value <= 0) return result;
  switch (profile.kind) {
    case QA_CLOCK_Q1_NETQUAKE:
    case QA_CLOCK_Q1_QUAKEWORLD:
      if (due.value > frame.time.value + frame.elapsed.value) return result;
      result.has_value = true;
      result.value.kind = due.kind;
      result.value.value = due.value > frame.time.value ? due.value : frame.time.value;
      return result;
    case QA_CLOCK_Q2_CLASSIC:
      if (due.value > frame.time.value + 0.001) return result;
      result.has_value = true;
      return result;
    case QA_CLOCK_Q2_RERELEASE:
      if (due.value > frame.time.value) return result;
      result.has_value = true;
      return result;
    case QA_CLOCK_Q3:
      /* TS Math.fround(due.value). */
      if ((double)(float)due.value > frame.time.value) return result;
      result.has_value = true;
      return result;
  }
  return result;
}

typedef struct pending_think {
  qa_actor_id actor;
  qa_owned_actor owned;
  qa_callback_id callback;
  qa_think_timing timing;
} pending_think;

struct qa_frame_scheduler {
  qa_scheduler_options options;
  qa_provider_id *ordering_providers; /* owned copy of pointers */
  pending_think *scheduled; /* keyed by actor slot via linear scan */
  size_t scheduled_count;
  size_t scheduled_capacity;
  bool closed;
};

static pending_think *find_pending(qa_frame_scheduler *scheduler, int64_t slot_value) {
  for (size_t i = 0; i < scheduler->scheduled_count; i++) {
    if (scheduler->scheduled[i].actor.slot == slot_value) return &scheduler->scheduled[i];
  }
  return NULL;
}
static void remove_pending(qa_frame_scheduler *scheduler, int64_t slot_value) {
  for (size_t i = 0; i < scheduler->scheduled_count; i++) {
    if (scheduler->scheduled[i].actor.slot == slot_value) {
      memmove(&scheduler->scheduled[i], &scheduler->scheduled[i + 1],
              (scheduler->scheduled_count - i - 1) * sizeof *scheduler->scheduled);
      scheduler->scheduled_count--;
      return;
    }
  }
}
static void set_pending(qa_frame_scheduler *scheduler, pending_think value) {
  pending_think *existing = find_pending(scheduler, value.actor.slot);
  if (existing != NULL) { *existing = value; return; }
  if (scheduler->scheduled_count == scheduler->scheduled_capacity) {
    scheduler->scheduled_capacity =
        scheduler->scheduled_capacity != 0 ? scheduler->scheduled_capacity * 2 : 8;
    scheduler->scheduled = xrealloc(scheduler->scheduled,
                                    scheduler->scheduled_capacity * sizeof *scheduler->scheduled);
  }
  scheduler->scheduled[scheduler->scheduled_count++] = value;
}

static const qa_clock_profile *find_profile(qa_frame_scheduler *scheduler, qa_provider_id provider) {
  for (size_t i = 0; i < scheduler->options.clock_count; i++) {
    if (str_eq(scheduler->options.clocks[i].provider, provider))
      return &scheduler->options.clocks[i].profile;
  }
  return NULL;
}
static qa_clock_profile profile_for(qa_frame_scheduler *scheduler, qa_provider_id provider) {
  const qa_clock_profile *profile = find_profile(scheduler, provider);
  if (profile != NULL) return *profile;
  if (scheduler->options.ordering.kind == QA_FRAME_ORDERING_NATIVE)
    return scheduler->options.ordering.clock;
  char message[1024];
  (void)snprintf(message, sizeof message, "Missing scheduler clock: %s",
                 provider != NULL ? provider : "");
  qa_error(message);
}

static void scheduler_release(void *arg) {
  qa_frame_scheduler *scheduler = arg;
  free(scheduler->ordering_providers);
  free(scheduler);
}
qa_frame_scheduler *qa_frame_scheduler_create(qa_scheduler_options options) {
  qa_frame_scheduler *scheduler = calloc(1, sizeof *scheduler);
  if (scheduler == NULL) return NULL;
  scheduler->options = options;
  if (options.ordering.kind == QA_FRAME_ORDERING_MIXED && options.ordering.provider_count != 0) {
    scheduler->ordering_providers =
        xrealloc(NULL, options.ordering.provider_count * sizeof *scheduler->ordering_providers);
    memcpy(scheduler->ordering_providers, options.ordering.providers,
           options.ordering.provider_count * sizeof *scheduler->ordering_providers);
    scheduler->options.ordering.providers = scheduler->ordering_providers;
  }
  /* A constructor throw leaves nothing behind in TS. */
  qa_cleanup node;
  qa_cleanup_push(&node, scheduler_release, scheduler);
  char message[1024];
  for (size_t i = 0; i < options.clock_count; i++) {
    for (size_t j = 0; j < i; j++) {
      if (str_eq(options.clocks[i].provider, options.clocks[j].provider)) {
        (void)snprintf(message, sizeof message, "Duplicate scheduler clock: %s",
                       options.clocks[i].provider);
        qa_error(message);
      }
    }
  }
  if (options.ordering.kind == QA_FRAME_ORDERING_MIXED) {
    for (size_t rank = 0; rank < options.ordering.provider_count; rank++) {
      for (size_t j = 0; j < rank; j++) {
        if (str_eq(options.ordering.providers[rank], options.ordering.providers[j])) {
          (void)snprintf(message, sizeof message, "Duplicate provider in frame order: %s",
                         options.ordering.providers[rank]);
          qa_error(message);
        }
      }
      (void)profile_for(scheduler, options.ordering.providers[rank]);
    }
  }
  qa_cleanup_pop(&node);
  return scheduler;
}
void qa_frame_scheduler_destroy(qa_frame_scheduler *scheduler) {
  if (scheduler == NULL) return;
  free(scheduler->ordering_providers);
  free(scheduler->scheduled);
  free(scheduler);
}

static void assert_open(qa_frame_scheduler *scheduler) {
  if (scheduler->closed) qa_error("Frame scheduler is closed");
}
static void assert_owned(qa_frame_scheduler *scheduler, qa_owned_actor actor) {
  qa_actor_observation_result observed =
      scheduler->options.actors.observe(scheduler->options.actors.ctx, actor.id);
  if (!observed.has_value || !str_eq(observed.value.owner, actor.owner))
    qa_range_error("Cannot schedule a stale or foreign actor");
}

void qa_frame_scheduler_schedule(qa_frame_scheduler *scheduler, qa_owned_actor actor,
                                 qa_callback_id callback, qa_think_timing timing) {
  assert_open(scheduler);
  assert_owned(scheduler, actor);
  qa_provider_id execution_provider =
      timing.has_execution_provider ? timing.execution_provider : actor.owner;
  qa_provider_id registered = actor.owner;
  if (scheduler->options.execution_provider != NULL) {
    qa_provider_id resolved = NULL;
    if (scheduler->options.execution_provider(scheduler->options.ctx, actor.id, &resolved))
      registered = resolved;
  }
  if (!str_eq(execution_provider, registered))
    qa_range_error("Think execution provider disagrees with the actor's registered continuation");
  if (!str_eq(execution_provider, actor.owner) && find_profile(scheduler, execution_provider) == NULL)
    qa_range_error("Selected execution requires its registered source clock");
  (void)profile_for(scheduler, execution_provider);
  qa_validate_time(timing.due);
  if (!qa_same_actor(actor.id, timing.order.actor) || !str_eq(actor.owner, timing.order.provider))
    qa_range_error("Think order must name its owning actor and provider");
  if (!(timing.order.sequence >= 0) || timing.order.sequence > 9007199254740991.0 ||
      timing.order.sequence != (double)(int64_t)timing.order.sequence)
    qa_range_error("Think invocation sequence must be a nonnegative safe integer");
  int64_t provider_rank = 0;
  if (scheduler->options.ordering.kind == QA_FRAME_ORDERING_MIXED) {
    provider_rank = -1;
    for (size_t i = 0; i < scheduler->options.ordering.provider_count; i++) {
      if (str_eq(scheduler->options.ordering.providers[i], actor.owner)) provider_rank = (int64_t)i;
    }
    if (provider_rank < 0) qa_range_error("Provider is absent from mixed frame ordering");
  }
  int64_t source_slot = actor.id.slot;
  if (scheduler->options.source_slot != NULL) {
    int64_t resolved = 0;
    if (scheduler->options.source_slot(scheduler->options.ctx, actor.id, &resolved))
      source_slot = resolved;
  }
  if (source_slot < 0 || source_slot > QA_MAX_SAFE_INTEGER)
    qa_range_error("Source slot must be a nonnegative safe integer");
  pending_think value;
  value.actor = actor.id;
  value.owned = actor;
  value.callback = callback;
  value.timing = timing;
  set_pending(scheduler, value);
}

void qa_frame_scheduler_cancel(qa_frame_scheduler *scheduler, qa_owned_actor actor) {
  assert_open(scheduler);
  pending_think *pending = find_pending(scheduler, actor.id.slot);
  if (pending != NULL && qa_same_actor(pending->actor, actor.id)) {
    if (!str_eq(pending->owned.owner, actor.owner))
      qa_range_error("Cannot cancel another provider's think");
    remove_pending(scheduler, actor.id.slot);
  }
}

qa_scheduled_think_result qa_frame_scheduler_pending(qa_frame_scheduler *scheduler,
                                                     qa_actor_id actor) {
  assert_open(scheduler);
  qa_scheduled_think_result result;
  result.has_value = false;
  memset(&result.value, 0, sizeof result.value);
  pending_think *pending = find_pending(scheduler, actor.slot);
  if (pending == NULL || !qa_same_actor(pending->actor, actor)) return result;
  if (!scheduler->options.actors.is_live(scheduler->options.actors.ctx, actor)) {
    remove_pending(scheduler, actor.slot);
    return result;
  }
  result.has_value = true;
  result.value.actor = pending->actor;
  result.value.callback = pending->callback;
  result.value.timing = pending->timing;
  return result;
}

static qa_think_result make_result(qa_frame_scheduler *scheduler, double invocations,
                                   qa_actor_id actor, const char *reason) {
  qa_think_result result;
  if (invocations == 0) {
    result.ran = false;
    result.reason = reason;
    result.invocations = 0;
    result.alive = false;
  } else {
    result.ran = true;
    result.reason = NULL;
    result.invocations = invocations;
    result.alive = scheduler->options.actors.is_live(scheduler->options.actors.ctx, actor);
  }
  return result;
}

qa_think_result qa_frame_scheduler_run(qa_frame_scheduler *scheduler, qa_actor_id actor,
                                       qa_frame_context frame, qa_think_boundary boundary) {
  assert_open(scheduler);
  double invocations = 0;
  for (;;) {
    pending_think *pending = find_pending(scheduler, actor.slot);
    if (pending == NULL || !qa_same_actor(pending->actor, actor))
      return make_result(scheduler, invocations, actor, "unscheduled");
    if (!scheduler->options.actors.is_live(scheduler->options.actors.ctx, actor)) {
      remove_pending(scheduler, actor.slot);
      return make_result(scheduler, invocations, actor, "stale");
    }
    if (pending->timing.boundary != boundary)
      return make_result(scheduler, invocations, actor, "boundary");
    pending_think current = *pending;
    qa_provider_id execution_provider =
        current.timing.has_execution_provider ? current.timing.execution_provider
                                              : current.owned.owner;
    qa_clock_profile profile = profile_for(scheduler, execution_provider);
    qa_source_time_result time = qa_think_callback_time(profile, current.timing.due, frame);
    if (!time.has_value) return make_result(scheduler, invocations, actor, "not-due");
    remove_pending(scheduler, actor.slot);
    qa_think_callback_result callback =
        scheduler->options.resolve(scheduler->options.ctx, execution_provider, current.callback);
    if (!callback.has_value) {
      char message[1024];
      (void)snprintf(message, sizeof message, "Unknown think callback: %s",
                     current.callback != NULL ? current.callback : "");
      qa_error(message);
    }
    qa_frame_context call_frame = frame;
    call_frame.time = time.value;
    call_frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
    callback.value.call(callback.value.ctx, current.owned, call_frame);
    invocations++;
    if (scheduler->closed ||
        !scheduler->options.actors.is_live(scheduler->options.actors.ctx, actor) ||
        profile.kind != QA_CLOCK_Q1_QUAKEWORLD) {
      qa_think_result result;
      result.ran = true;
      result.reason = NULL;
      result.invocations = invocations;
      result.alive = scheduler->options.actors.is_live(scheduler->options.actors.ctx, actor);
      return result;
    }
  }
}

void qa_frame_scheduler_close(qa_frame_scheduler *scheduler) {
  scheduler->closed = true;
  scheduler->scheduled_count = 0;
}

static void c_schedule(void *ctx, qa_owned_actor actor, qa_callback_id callback,
                       qa_think_timing timing) {
  qa_frame_scheduler_schedule(ctx, actor, callback, timing);
}
static void c_cancel(void *ctx, qa_owned_actor actor) { qa_frame_scheduler_cancel(ctx, actor); }
static qa_scheduled_think_result c_pending(void *ctx, qa_actor_id actor) {
  return qa_frame_scheduler_pending(ctx, actor);
}
qa_actor_schedule qa_frame_scheduler_as_contract(qa_frame_scheduler *scheduler) {
  qa_actor_schedule table;
  table.ctx = scheduler;
  table.schedule = c_schedule;
  table.cancel = c_cancel;
  table.pending = c_pending;
  return table;
}
