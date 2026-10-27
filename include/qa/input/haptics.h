/*
 * Port of src/input/haptics.ts: BNVIB format and motor downmix,
 * per-seat scheduling and device ownership. C accommodations:
 * the scheduler borrows its playing pattern (the seat cache owns
 * patterns); SeatHaptics' async load is a synchronous callback
 * (false is the TS null), with the generation/requested ordering
 * checks retained. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_HAPTICS_H
#define QA_INPUT_HAPTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/platform/controller.h"

/** TS BnvibSample. */
typedef struct qa_bnvib_sample {
  double amp_low;
  double freq_low;
  double amp_high;
  double freq_high;
} qa_bnvib_sample;

/** TS BnvibPattern. samples is owned; dispose after use. */
typedef struct qa_bnvib_pattern {
  double sample_rate_hz;
  qa_bnvib_sample *samples;
  size_t sample_count;
  bool has_loop;
  double loop_start_sample;
  double loop_end_sample;
  double loop_interval_samples;
} qa_bnvib_pattern;

/* False only on allocation failure; malformed data aborts with
 * the TS Error messages. */
bool qa_parse_bnvib(const uint8_t *bytes, size_t length, qa_bnvib_pattern *out);
void qa_bnvib_pattern_dispose(qa_bnvib_pattern *pattern);
double qa_bnvib_frequency(double byte);
/* Malloc'd tactile path, or NULL for the TS null. */
char *qa_tactile_path_for_sound(const char *sound);

/** TS RumbleSink. */
typedef struct qa_rumble_sink {
  void *ctx;
  qa_controller_operation_result (*set_motors)(void *ctx, double low, double high,
                                               double duration_milliseconds);
} qa_rumble_sink;

/** TS HapticsResult: a controller result or {kind:"unchanged"}. */
typedef struct qa_haptics_result {
  bool unchanged;
  qa_controller_operation_result result;
} qa_haptics_result;

typedef struct qa_bnvib_scheduler {
  qa_rumble_sink sink;
  const qa_bnvib_pattern *playing; /* borrowed; NULL when idle */
  double started;
  double gain;
  double last_index;
  double last_output;
} qa_bnvib_scheduler;

void qa_bnvib_scheduler_init(qa_bnvib_scheduler *scheduler, qa_rumble_sink sink);
double qa_bnvib_scheduler_strength(const qa_bnvib_scheduler *scheduler);
qa_haptics_result qa_bnvib_scheduler_set_strength(qa_bnvib_scheduler *scheduler,
                                                  double value,
                                                  double now_milliseconds);
bool qa_bnvib_scheduler_active(const qa_bnvib_scheduler *scheduler);
qa_haptics_result qa_bnvib_scheduler_play(qa_bnvib_scheduler *scheduler,
                                          const qa_bnvib_pattern *pattern,
                                          double now_milliseconds);
qa_controller_operation_result qa_bnvib_scheduler_stop(
    qa_bnvib_scheduler *scheduler);
qa_haptics_result qa_bnvib_scheduler_update(qa_bnvib_scheduler *scheduler,
                                            double now_milliseconds);

/** TS SeatHapticsOptions. load returns false for the TS null,
 * else true with malloc'd bytes. controller returns false for
 * the TS null. */
typedef struct qa_seat_haptics_options {
  qa_seat_id seat;
  qa_sdl_controllers *controllers;
  bool (*controller)(void *ctx, qa_seat_id seat, double *out_device);
  void *controller_ctx;
  bool (*load)(void *ctx, qa_resource_request request, uint8_t **out_bytes,
               size_t *out_length);
  void *load_ctx;
  double (*now)(void *ctx);
  void *now_ctx;
} qa_seat_haptics_options;

typedef struct qa_seat_haptics qa_seat_haptics;

/* NULL only on allocation failure. */
qa_seat_haptics *qa_seat_haptics_create(qa_seat_haptics_options options);
void qa_seat_haptics_destroy(qa_seat_haptics *haptics);
qa_bnvib_scheduler *qa_seat_haptics_scheduler(qa_seat_haptics *haptics);
bool qa_seat_haptics_enabled(const qa_seat_haptics *haptics);
double qa_seat_haptics_strength(const qa_seat_haptics *haptics);
qa_haptics_result qa_seat_haptics_set_strength(qa_seat_haptics *haptics,
                                               double value);
qa_haptics_result qa_seat_haptics_set_enabled(qa_seat_haptics *haptics,
                                              bool enabled);
void qa_seat_haptics_set_active(qa_seat_haptics *haptics, bool active);
qa_controller_operation_result qa_seat_haptics_cancel(qa_seat_haptics *haptics);
/* False is the TS null (no playback). */
bool qa_seat_haptics_sound(qa_seat_haptics *haptics, qa_content_id content,
                           const char *sound, qa_haptics_result *out);
qa_haptics_result qa_seat_haptics_update(qa_seat_haptics *haptics);
void qa_seat_haptics_invalidate_assets(qa_seat_haptics *haptics);
qa_controller_operation_result qa_seat_haptics_close(qa_seat_haptics *haptics);

#endif
