/* Port of src/core/omnitimer.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 199309L
#include "qa/core/omnitimer.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "qa/foundation/bounded_queue.h"
#include "qa/foundation/support.h"

/* Plain TS `Error` throws: same message form, then abort. */
static noreturn void throw_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

struct active_timer {
  char *name;
  double start;
  double children;
};

struct aggregate {
  char *name;
  int64_t calls;
  double total_milliseconds;
  double self_milliseconds;
  double maximum_milliseconds;
};

struct stored_stamp {
  char *name;
  double milliseconds;
};

struct qa_omni_timer {
  qa_timer_clock_fn clock;
  void *clock_ctx;
  struct active_timer *stack;
  size_t stack_count;
  size_t stack_capacity;
  struct aggregate *totals;
  size_t total_count;
  size_t total_capacity;
  qa_bounded_queue stamps;
  double epoch;
  bool active;
};

static double default_clock(void *ctx) {
  (void)ctx;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

qa_omni_timer *qa_omni_timer_create(qa_timer_clock_fn clock, void *clock_ctx,
                                    int64_t stamp_capacity) {
  if (stamp_capacity < 1 || stamp_capacity > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Invalid timer stamp capacity");
  }
  qa_omni_timer *timer = (qa_omni_timer *)calloc(1, sizeof(qa_omni_timer));
  if (timer == NULL) return NULL;
  timer->clock = clock != NULL ? clock : default_clock;
  timer->clock_ctx = clock_ctx;
  if (!qa_bounded_queue_init(&timer->stamps, (size_t)stamp_capacity,
                             sizeof(struct stored_stamp))) {
    free(timer);
    return NULL;
  }
  timer->epoch = timer->clock(timer->clock_ctx);
  return timer;
}

static void free_stamp_name(const void *value, size_t index, void *ctx) {
  (void)index;
  (void)ctx;
  free(((const struct stored_stamp *)value)->name);
}

void qa_omni_timer_destroy(qa_omni_timer *timer) {
  if (timer == NULL) return;
  for (size_t i = 0; i < timer->stack_count; i++) free(timer->stack[i].name);
  free(timer->stack);
  for (size_t i = 0; i < timer->total_count; i++) free(timer->totals[i].name);
  free(timer->totals);
  qa_bounded_queue_for_each(&timer->stamps, free_stamp_name, NULL);
  qa_bounded_queue_destroy(&timer->stamps);
  free(timer);
}

bool qa_omni_timer_enabled(const qa_omni_timer *timer) { return timer->active; }

void qa_omni_timer_set_enabled(qa_omni_timer *timer, bool enabled) {
  if (timer->stack_count != 0) {
    throw_error("Cannot change timer mode inside an active scope");
  }
  timer->active = enabled;
}

void qa_omni_timer_push(qa_omni_timer *timer, const char *name) {
  if (!timer->active) return;
  if (timer->stack_count == timer->stack_capacity) {
    size_t grown = timer->stack_capacity == 0 ? 8 : timer->stack_capacity * 2;
    struct active_timer *stack = (struct active_timer *)realloc(
        timer->stack, grown * sizeof(struct active_timer));
    if (stack == NULL) return; /* No TS counterpart: allocation failure. */
    timer->stack = stack;
    timer->stack_capacity = grown;
  }
  char *copy = copy_text(name);
  if (copy == NULL) return;
  struct active_timer *item = &timer->stack[timer->stack_count++];
  item->name = copy;
  item->start = timer->clock(timer->clock_ctx);
  item->children = 0;
}

static struct aggregate *find_total(qa_omni_timer *timer, const char *name) {
  for (size_t i = 0; i < timer->total_count; i++) {
    if (strcmp(timer->totals[i].name, name) == 0) return &timer->totals[i];
  }
  return NULL;
}

void qa_omni_timer_pop(qa_omni_timer *timer) {
  if (!timer->active) return;
  if (timer->stack_count == 0) throw_error("Unbalanced timer pop");
  struct active_timer item = timer->stack[--timer->stack_count];
  double elapsed = timer->clock(timer->clock_ctx) - item.start;
  if (elapsed < 0) elapsed = 0;
  if (timer->stack_count > 0) {
    timer->stack[timer->stack_count - 1].children += elapsed;
  }
  struct aggregate *total = find_total(timer, item.name);
  if (total == NULL) {
    if (timer->total_count == timer->total_capacity) {
      size_t grown =
          timer->total_capacity == 0 ? 8 : timer->total_capacity * 2;
      struct aggregate *totals = (struct aggregate *)realloc(
          timer->totals, grown * sizeof(struct aggregate));
      if (totals == NULL) {
        free(item.name);
        return;
      }
      timer->totals = totals;
      timer->total_capacity = grown;
    }
    total = &timer->totals[timer->total_count];
    total->name = copy_text(item.name);
    if (total->name == NULL) {
      free(item.name);
      return;
    }
    total->calls = 0;
    total->total_milliseconds = 0;
    total->self_milliseconds = 0;
    total->maximum_milliseconds = 0;
    timer->total_count++;
  }
  total->calls++;
  total->total_milliseconds += elapsed;
  double self = elapsed - item.children;
  total->self_milliseconds += self < 0 ? 0 : self;
  if (elapsed > total->maximum_milliseconds) {
    total->maximum_milliseconds = elapsed;
  }
  free(item.name);
}

void *qa_omni_timer_measure(qa_omni_timer *timer, const char *name,
                            qa_timer_operation_fn operation, void *ctx) {
  if (!timer->active) return operation(ctx);
  qa_omni_timer_push(timer, name);
  void *result = operation(ctx);
  qa_omni_timer_pop(timer);
  return result;
}

void qa_omni_timer_stamp(qa_omni_timer *timer, const char *name) {
  if (!timer->active) return;
  struct stored_stamp stamp;
  stamp.name = copy_text(name);
  if (stamp.name == NULL) return;
  stamp.milliseconds = timer->clock(timer->clock_ctx) - timer->epoch;
  /* The queue drops the oldest stamp when full; free its name first. */
  if (timer->stamps.count == timer->stamps.capacity) {
    const struct stored_stamp *oldest =
        (const struct stored_stamp *)qa_bounded_queue_peek_oldest(
            &timer->stamps);
    if (oldest != NULL) free(oldest->name);
  }
  qa_bounded_queue_push(&timer->stamps, &stamp);
}

qa_timer_report *qa_omni_timer_report(const qa_omni_timer *timer,
                                      size_t *out_count) {
  *out_count = timer->total_count;
  if (timer->total_count == 0) return NULL;
  qa_timer_report *rows =
      (qa_timer_report *)malloc(timer->total_count * sizeof(qa_timer_report));
  if (rows == NULL) {
    *out_count = 0;
    return NULL;
  }
  for (size_t i = 0; i < timer->total_count; i++) {
    rows[i].name = timer->totals[i].name;
    rows[i].calls = timer->totals[i].calls;
    rows[i].total_milliseconds = timer->totals[i].total_milliseconds;
    rows[i].self_milliseconds = timer->totals[i].self_milliseconds;
    rows[i].maximum_milliseconds = timer->totals[i].maximum_milliseconds;
  }
  return rows;
}

qa_timer_stamp *qa_omni_timer_stamp_list(const qa_omni_timer *timer,
                                         size_t *out_count) {
  size_t count = 0;
  struct stored_stamp *stored =
      (struct stored_stamp *)qa_bounded_queue_to_array(&timer->stamps,
                                                       &count);
  *out_count = count;
  if (count == 0) {
    free(stored);
    return NULL;
  }
  qa_timer_stamp *rows =
      (qa_timer_stamp *)malloc(count * sizeof(qa_timer_stamp));
  if (rows == NULL) {
    free(stored);
    *out_count = 0;
    return NULL;
  }
  for (size_t i = 0; i < count; i++) {
    rows[i].name = stored[i].name;
    rows[i].milliseconds = stored[i].milliseconds;
  }
  free(stored);
  return rows;
}

void qa_omni_timer_reset(qa_omni_timer *timer) {
  if (timer->stack_count != 0) {
    throw_error("Cannot reset timers inside an active scope");
  }
  for (size_t i = 0; i < timer->total_count; i++) free(timer->totals[i].name);
  timer->total_count = 0;
  qa_bounded_queue_for_each(&timer->stamps, free_stamp_name, NULL);
  qa_bounded_queue_clear(&timer->stamps);
  timer->epoch = timer->clock(timer->clock_ctx);
}

char *qa_omni_timer_format(const qa_omni_timer *timer) {
  size_t count = 0;
  qa_timer_report *rows = qa_omni_timer_report(timer, &count);
  if (count > 0 && rows == NULL) return NULL;
  const char *header = "name\tcalls\ttotal_ms\tself_ms\tmax_ms\n";
  size_t capacity = strlen(header) + 1 + 1;
  for (size_t i = 0; i < count; i++) {
    /* %.3f stands in for toFixed(3); they differ only on exact decimal
     * ties the binary value cannot represent half-way for long. */
    int needed = snprintf(NULL, 0, "%s\t%lld\t%.3f\t%.3f\t%.3f", rows[i].name,
                          (long long)rows[i].calls, rows[i].total_milliseconds,
                          rows[i].self_milliseconds,
                          rows[i].maximum_milliseconds);
    if (needed < 0) {
      free(rows);
      return NULL;
    }
    capacity += (size_t)needed + 1;
  }
  char *out = (char *)malloc(capacity);
  if (out == NULL) {
    free(rows);
    return NULL;
  }
  strcpy(out, header);
  size_t used = strlen(out);
  for (size_t i = 0; i < count; i++) {
    if (i > 0) out[used++] = '\n';
    used += (size_t)snprintf(out + used, capacity - used,
                             "%s\t%lld\t%.3f\t%.3f\t%.3f", rows[i].name,
                             (long long)rows[i].calls,
                             rows[i].total_milliseconds,
                             rows[i].self_milliseconds,
                             rows[i].maximum_milliseconds);
  }
  out[used++] = '\n';
  out[used] = '\0';
  free(rows);
  return out;
}
