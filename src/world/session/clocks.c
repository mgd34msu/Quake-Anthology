/* Port of src/world/session/clocks.ts. GPL-2.0-or-later. */
#include "qa/world/session/clocks.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void qa_errorf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  abort();
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
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

/* TS copyRandomState: deep-copies word/byte payloads. NULL on OOM. */
static bool copy_random_state(qa_random_state *state) {
  switch (state->kind) {
    case QA_RANDOM_STATE_Q3_LCG:
    case QA_RANDOM_STATE_MSVCRT_RAND:
      return true;
    case QA_RANDOM_STATE_GLIBC_RANDOM:
    case QA_RANDOM_STATE_Q2_RERELEASE_MT19937: {
      if (state->words.count == 0) {
        state->words.values = NULL;
        return true;
      }
      double *words = malloc(state->words.count * sizeof *words);
      if (words == NULL) return false;
      memcpy(words, state->words.values,
             state->words.count * sizeof *words);
      state->words.values = words;
      return true;
    }
    case QA_RANDOM_STATE_GUEST: {
      if (state->bytes.length == 0) {
        state->bytes.bytes = NULL;
        return true;
      }
      uint8_t *bytes = malloc(state->bytes.length);
      if (bytes == NULL) return false;
      memcpy(bytes, state->bytes.bytes, state->bytes.length);
      state->bytes.bytes = bytes;
      return true;
    }
  }
  return true;
}

void qa_provider_runtime_checkpoint_dispose(
    qa_provider_runtime_checkpoint *checkpoint) {
  if (checkpoint == NULL) return;
  for (size_t i = 0; i < checkpoint->random_count; i++) {
    qa_random_state *state = &checkpoint->random[i].state;
    if (state->kind == QA_RANDOM_STATE_GUEST)
      free((void *)state->bytes.bytes);
    else if (state->kind == QA_RANDOM_STATE_GLIBC_RANDOM ||
             state->kind == QA_RANDOM_STATE_Q2_RERELEASE_MT19937)
      free((void *)state->words.values);
  }
  free(checkpoint->clocks);
  free(checkpoint->random);
  checkpoint->clocks = NULL;
  checkpoint->random = NULL;
  checkpoint->clock_count = 0;
  checkpoint->random_count = 0;
}

void qa_provider_runtime_state_init(qa_provider_runtime_state *state) {
  state->entries = NULL;
  state->count = 0;
  state->capacity = 0;
  state->closed = false;
}
void qa_provider_runtime_state_destroy(qa_provider_runtime_state *state) {
  free(state->entries);
  qa_provider_runtime_state_init(state);
}

static void assert_open(const qa_provider_runtime_state *state) {
  if (state->closed) qa_errorf("Provider runtime state is closed");
}

void qa_provider_runtime_state_register(qa_provider_runtime_state *state,
                                        qa_provider_id provider,
                                        qa_source_clock *clock,
                                        qa_random_source random) {
  assert_open(state);
  for (size_t i = 0; i < state->count; i++)
    if (str_eq(state->entries[i].provider, provider))
      qa_errorf("Provider runtime already registered: %s", provider);
  for (size_t i = 0; i < state->count; i++)
    if (state->entries[i].clock == clock ||
        state->entries[i].random.ctx == random.ctx)
      qa_errorf("Providers must own distinct clocks and random generators");
  if (state->count == state->capacity) {
    state->capacity = state->capacity != 0 ? state->capacity * 2 : 8;
    qa_provider_runtime_entry *next =
        realloc(state->entries, state->capacity * sizeof *next);
    if (next == NULL) abort();
    state->entries = next;
  }
  state->entries[state->count].provider = provider;
  state->entries[state->count].clock = clock;
  state->entries[state->count].random = random;
  state->count++;
}

static const qa_provider_runtime_entry *runtime_of(
    qa_provider_runtime_state *state, qa_provider_id provider) {
  assert_open(state);
  for (size_t i = 0; i < state->count; i++)
    if (str_eq(state->entries[i].provider, provider))
      return &state->entries[i];
  qa_errorf("Unknown provider runtime: %s", provider);
}

qa_source_clock *qa_provider_runtime_state_clock(
    qa_provider_runtime_state *state, qa_provider_id provider) {
  return runtime_of(state, provider)->clock;
}
qa_random_source qa_provider_runtime_state_random(
    qa_provider_runtime_state *state, qa_provider_id provider) {
  return runtime_of(state, provider)->random;
}

bool qa_provider_runtime_state_checkpoint(
    qa_provider_runtime_state *state,
    qa_provider_runtime_checkpoint *out) {
  assert_open(state);
  out->clocks = NULL;
  out->random = NULL;
  out->clock_count = 0;
  out->random_count = 0;
  if (state->count != 0) {
    out->clocks = malloc(state->count * sizeof *out->clocks);
    out->random = malloc(state->count * sizeof *out->random);
    if (out->clocks == NULL || out->random == NULL) {
      free(out->clocks);
      free(out->random);
      out->clocks = NULL;
      out->random = NULL;
      return false;
    }
  }
  for (size_t i = 0; i < state->count; i++) {
    out->clocks[i].provider = state->entries[i].provider;
    out->clocks[i].time =
        qa_copy_time(state->entries[i].clock->current.time);
    out->clock_count++;
  }
  for (size_t i = 0; i < state->count; i++) {
    out->random[i].provider = state->entries[i].provider;
    out->random[i].state =
        state->entries[i].random.checkpoint(state->entries[i].random.ctx);
    if (!copy_random_state(&out->random[i].state)) {
      qa_provider_runtime_checkpoint_dispose(out);
      return false;
    }
    out->random_count++;
  }
  return true;
}

void qa_provider_runtime_state_close(qa_provider_runtime_state *state) {
  state->closed = true;
  state->count = 0;
}
