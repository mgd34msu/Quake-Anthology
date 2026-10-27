/*
 * Port of src/core/omnitimer.ts (OmniTimer): portable replacement for
 * Q3's optional macOS OmniTimer stack and stamp lists, over the
 * foundation BoundedQueue. C accommodations:
 *  - The clock closure becomes a function pointer plus ctx; NULL
 *    selects a monotonic millisecond clock (the TS default
 *    performance.now). Timer and stamp names are owned copies.
 *  - The generic measure<T> becomes a void* operation callback; the
 *    TS try/finally unwind has no C counterpart, so an operation that
 *    aborts or long-jumps does not pop its timer.
 *  - report/stampList return malloc'd arrays (borrowed name pointers,
 *    caller frees the array); format returns a malloc'd string. All
 *    are NULL only on allocation failure. Report order is the TS
 *    Map's first-completion insertion order.
 *  - Plain TS `Error` throws print "Error: <message>" and abort, like
 *    qa_range_error does for RangeError.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_OMNITIMER_H
#define QA_CORE_OMNITIMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_timer_report {
  const char *name; /* borrowed from the timer */
  int64_t calls;
  double total_milliseconds;
  double self_milliseconds;
  double maximum_milliseconds;
} qa_timer_report;

typedef struct qa_timer_stamp {
  const char *name; /* borrowed from the timer */
  double milliseconds;
} qa_timer_stamp;

typedef double (*qa_timer_clock_fn)(void *ctx);
typedef void *(*qa_timer_operation_fn)(void *ctx);

typedef struct qa_omni_timer qa_omni_timer; /* opaque */

/* Returns NULL only on allocation failure; invalid capacity aborts
 * first, before the epoch clock read, as in the TS constructor. */
qa_omni_timer *qa_omni_timer_create(qa_timer_clock_fn clock, void *clock_ctx,
                                    int64_t stamp_capacity);
void qa_omni_timer_destroy(qa_omni_timer *timer);

bool qa_omni_timer_enabled(const qa_omni_timer *timer);
void qa_omni_timer_set_enabled(qa_omni_timer *timer, bool enabled);
void qa_omni_timer_push(qa_omni_timer *timer, const char *name);
void qa_omni_timer_pop(qa_omni_timer *timer);
void *qa_omni_timer_measure(qa_omni_timer *timer, const char *name,
                            qa_timer_operation_fn operation, void *ctx);
void qa_omni_timer_stamp(qa_omni_timer *timer, const char *name);
qa_timer_report *qa_omni_timer_report(const qa_omni_timer *timer,
                                      size_t *out_count);
qa_timer_stamp *qa_omni_timer_stamp_list(const qa_omni_timer *timer,
                                         size_t *out_count);
void qa_omni_timer_reset(qa_omni_timer *timer);
char *qa_omni_timer_format(const qa_omni_timer *timer);

#endif
