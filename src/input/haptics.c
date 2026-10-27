/* Port of src/input/haptics.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/input/haptics.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/input/support.h"

bool qa_parse_bnvib(const uint8_t *bytes, size_t length, qa_bnvib_pattern *out) {
  memset(out, 0, sizeof *out);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, "BNVIB")) {
    return false;
  }
  uint32_t size = qa_binary_reader_u32(&reader);
  if (size != 4 && size != 12 && size != 16) {
    char *text = qa_input_js_number(size);
    qa_input_error("Unsupported BNVIB metadata size %s", text != NULL ? text : "");
  }
  if (qa_binary_reader_u16(&reader) != 3) {
    qa_input_error("Unsupported BNVIB format");
  }
  double sample_rate_hz = qa_binary_reader_u16(&reader);
  if (sample_rate_hz == 0) qa_input_error("BNVIB sample rate must be positive");
  bool has_loop = size != 4;
  double loop_start = 0, loop_end = 0, loop_interval = 0;
  if (has_loop) {
    loop_start = qa_binary_reader_u32(&reader);
    loop_end = qa_binary_reader_u32(&reader);
    loop_interval = size == 16 ? qa_binary_reader_u32(&reader) : 0;
  }
  uint32_t data_size = qa_binary_reader_u32(&reader);
  if (data_size % 4 != 0) {
    qa_input_error("BNVIB data size is not a multiple of four");
  }
  size_t count = data_size / 4;
  qa_bnvib_sample *samples = NULL;
  if (count != 0) {
    samples = (qa_bnvib_sample *)malloc(count * sizeof *samples);
    if (samples == NULL) {
      qa_binary_reader_destroy(&reader);
      return false;
    }
  }
  for (size_t i = 0; i < count; i++) {
    samples[i].amp_low = qa_binary_reader_u8(&reader);
    samples[i].freq_low = qa_binary_reader_u8(&reader);
    samples[i].amp_high = qa_binary_reader_u8(&reader);
    samples[i].freq_high = qa_binary_reader_u8(&reader);
  }
  qa_binary_reader_destroy(&reader);
  if (has_loop && (loop_start >= loop_end || loop_end > (double)count)) {
    free(samples);
    qa_input_error("BNVIB loop exceeds sample data");
  }
  out->sample_rate_hz = sample_rate_hz;
  out->samples = samples;
  out->sample_count = count;
  out->has_loop = has_loop;
  out->loop_start_sample = loop_start;
  out->loop_end_sample = loop_end;
  out->loop_interval_samples = loop_interval;
  return true;
}

void qa_bnvib_pattern_dispose(qa_bnvib_pattern *pattern) {
  free(pattern->samples);
  memset(pattern, 0, sizeof *pattern);
}

double qa_bnvib_frequency(double byte) { return 10 * pow(2, byte / 32); }

char *qa_tactile_path_for_sound(const char *sound) {
  const char *path = sound[0] == '#' ? sound + 1 : sound;
  size_t length = strlen(path);
  if (length <= 4 || strcmp(path + length - 4, ".wav") != 0) return NULL;
  size_t out_length = strlen("tactile/") + (length - 4) + strlen(".bnvib") + 1;
  char *out = (char *)malloc(out_length);
  if (out == NULL) return NULL;
  snprintf(out, out_length, "tactile/%.*s.bnvib", (int)(length - 4), path);
  return out;
}

static qa_haptics_result unchanged_result(void) {
  qa_haptics_result result;
  memset(&result, 0, sizeof result);
  result.unchanged = true;
  return result;
}

static qa_haptics_result wrapped_result(qa_controller_operation_result inner) {
  qa_haptics_result result;
  result.unchanged = false;
  result.result = inner;
  return result;
}

void qa_bnvib_scheduler_init(qa_bnvib_scheduler *scheduler, qa_rumble_sink sink) {
  memset(scheduler, 0, sizeof *scheduler);
  scheduler->sink = sink;
  scheduler->gain = 1;
  scheduler->last_index = -1;
  scheduler->last_output = -INFINITY;
}

double qa_bnvib_scheduler_strength(const qa_bnvib_scheduler *scheduler) {
  return scheduler->gain;
}

qa_haptics_result qa_bnvib_scheduler_set_strength(qa_bnvib_scheduler *scheduler,
                                                  double value,
                                                  double now_milliseconds) {
  if (!isfinite(value) || value < 0 || value > 1) {
    qa_input_error("Vibration strength must be between 0 and 1");
  }
  if (value == scheduler->gain) return unchanged_result();
  scheduler->gain = value;
  scheduler->last_index = -1;
  return qa_bnvib_scheduler_update(scheduler, now_milliseconds);
}

bool qa_bnvib_scheduler_active(const qa_bnvib_scheduler *scheduler) {
  return scheduler->playing != NULL;
}

qa_haptics_result qa_bnvib_scheduler_play(qa_bnvib_scheduler *scheduler,
                                          const qa_bnvib_pattern *pattern,
                                          double now_milliseconds) {
  scheduler->playing = pattern;
  scheduler->started = now_milliseconds;
  scheduler->last_index = -1;
  return qa_bnvib_scheduler_update(scheduler, now_milliseconds);
}

qa_controller_operation_result qa_bnvib_scheduler_stop(
    qa_bnvib_scheduler *scheduler) {
  scheduler->playing = NULL;
  scheduler->last_index = -1;
  return scheduler->sink.set_motors(scheduler->sink.ctx, 0, 0, 0);
}

qa_haptics_result qa_bnvib_scheduler_update(qa_bnvib_scheduler *scheduler,
                                            double now_milliseconds) {
  const qa_bnvib_pattern *pattern = scheduler->playing;
  if (pattern == NULL) return unchanged_result();
  double started = scheduler->started;
  double period = 1000 / pattern->sample_rate_hz;
  double index = fmax(0, floor((now_milliseconds - started) / period));
  if (index >= (double)pattern->sample_count) {
    if (!pattern->has_loop) {
      return wrapped_result(qa_bnvib_scheduler_stop(scheduler));
    }
    double loop_length = pattern->loop_end_sample - pattern->loop_start_sample;
    double position = fmod(index - (double)pattern->sample_count,
                           loop_length + pattern->loop_interval_samples);
    index = position >= loop_length ? -2 : pattern->loop_start_sample + position;
  }
  double hold = fmax(50, ceil(period) + 20);
  if (index == scheduler->last_index &&
      now_milliseconds - scheduler->last_output < hold / 2) {
    return unchanged_result();
  }
  scheduler->last_index = index;
  scheduler->last_output = now_milliseconds;
  if (index == -2) {
    return wrapped_result(
        scheduler->sink.set_motors(scheduler->sink.ctx, 0, 0, hold));
  }
  if (index < 0 || index != trunc(index) || index >= (double)pattern->sample_count) {
    return wrapped_result(qa_bnvib_scheduler_stop(scheduler));
  }
  const qa_bnvib_sample *sample = &pattern->samples[(size_t)index];
  return wrapped_result(scheduler->sink.set_motors(
      scheduler->sink.ctx, sample->amp_low / 255 * scheduler->gain,
      sample->amp_high / 255 * scheduler->gain, hold));
}

typedef struct cache_entry {
  char *key;
  bool has_pattern;
  qa_bnvib_pattern pattern;
} cache_entry;

struct qa_seat_haptics {
  qa_seat_haptics_options options;
  qa_bnvib_scheduler scheduler;
  cache_entry *cache;
  size_t cache_count;
  size_t cache_capacity;
  double generation;
  double requested;
  double accepted;
  bool has_device;
  double device;
  bool active;
  bool preference;
  bool closed;
};

static qa_controller_operation_result seat_set_motors(void *ctx, double low,
                                                      double high,
                                                      double duration) {
  qa_seat_haptics *haptics = (qa_seat_haptics *)ctx;
  if (!haptics->has_device) {
    qa_controller_operation_result result;
    memset(&result, 0, sizeof result);
    result.kind = QA_CONTROLLER_OPERATION_DISCONNECTED;
    snprintf(result.reason, sizeof result.reason, "Seat has no assigned controller");
    return result;
  }
  return qa_sdl_controllers_rumble(haptics->options.controllers,
                                   (int32_t)haptics->device, low, high, duration);
}

qa_seat_haptics *qa_seat_haptics_create(qa_seat_haptics_options options) {
  qa_seat_haptics *haptics = (qa_seat_haptics *)calloc(1, sizeof *haptics);
  if (haptics == NULL) return NULL;
  haptics->options = options;
  haptics->active = true;
  haptics->preference = true;
  qa_rumble_sink sink = {haptics, seat_set_motors};
  qa_bnvib_scheduler_init(&haptics->scheduler, sink);
  return haptics;
}

static void cache_clear(qa_seat_haptics *haptics) {
  for (size_t i = 0; i < haptics->cache_count; i++) {
    free(haptics->cache[i].key);
    if (haptics->cache[i].has_pattern) {
      qa_bnvib_pattern_dispose(&haptics->cache[i].pattern);
    }
  }
  haptics->cache_count = 0;
}

void qa_seat_haptics_destroy(qa_seat_haptics *haptics) {
  if (haptics == NULL) return;
  cache_clear(haptics);
  free(haptics->cache);
  free(haptics);
}

qa_bnvib_scheduler *qa_seat_haptics_scheduler(qa_seat_haptics *haptics) {
  return &haptics->scheduler;
}

bool qa_seat_haptics_enabled(const qa_seat_haptics *haptics) {
  return haptics->preference;
}

double qa_seat_haptics_strength(const qa_seat_haptics *haptics) {
  return qa_bnvib_scheduler_strength(&haptics->scheduler);
}

qa_haptics_result qa_seat_haptics_set_strength(qa_seat_haptics *haptics,
                                               double value) {
  return qa_bnvib_scheduler_set_strength(&haptics->scheduler, value,
                                         haptics->options.now(
                                             haptics->options.now_ctx));
}

qa_haptics_result qa_seat_haptics_set_enabled(qa_seat_haptics *haptics,
                                              bool enabled) {
  haptics->preference = enabled;
  return enabled ? unchanged_result()
                 : wrapped_result(qa_seat_haptics_cancel(haptics));
}

void qa_seat_haptics_set_active(qa_seat_haptics *haptics, bool active) {
  if (haptics->active == active) return;
  haptics->active = active;
  if (!active) (void)qa_seat_haptics_cancel(haptics);
}

static void synchronize_device(qa_seat_haptics *haptics) {
  double device = 0;
  bool has = haptics->options.controller(haptics->options.controller_ctx,
                                         haptics->options.seat, &device);
  if (has == haptics->has_device && (!has || device == haptics->device)) return;
  (void)qa_seat_haptics_cancel(haptics);
  haptics->has_device = has;
  haptics->device = device;
}

qa_controller_operation_result qa_seat_haptics_cancel(qa_seat_haptics *haptics) {
  haptics->generation++;
  return qa_bnvib_scheduler_stop(&haptics->scheduler);
}

bool qa_seat_haptics_sound(qa_seat_haptics *haptics, qa_content_id content,
                           const char *sound, qa_haptics_result *out) {
  if (haptics->closed) return false;
  synchronize_device(haptics);
  if (!haptics->preference || !haptics->active || !haptics->has_device) return false;
  const char *relative = strncmp(sound, "sound/", 6) == 0 ? sound + 6 : sound;
  char *path = qa_tactile_path_for_sound(relative);
  if (path == NULL) return false;
  double generation = haptics->generation;
  double requested = ++haptics->requested;
  size_t key_length = strlen(content) + strlen(path) + 2;
  char *key = (char *)malloc(key_length);
  if (key == NULL) {
    free(path);
    return false;
  }
  snprintf(key, key_length, "%s/%s", content, path);
  ssize_t cached = -1;
  for (size_t i = 0; i < haptics->cache_count; i++) {
    if (strcmp(haptics->cache[i].key, key) == 0) cached = (ssize_t)i;
  }
  if (cached < 0) {
    if (haptics->cache_count == haptics->cache_capacity) {
      size_t grown =
          haptics->cache_capacity == 0 ? 8 : haptics->cache_capacity * 2;
      cache_entry *cache =
          (cache_entry *)realloc(haptics->cache, grown * sizeof *cache);
      if (cache == NULL) {
        free(key);
        free(path);
        return false;
      }
      haptics->cache = cache;
      haptics->cache_capacity = grown;
    }
    cached = (ssize_t)haptics->cache_count++;
    memset(&haptics->cache[cached], 0, sizeof haptics->cache[cached]);
    haptics->cache[cached].key = qa_input_dup(key);
    qa_resource_request request = {content, path};
    uint8_t *bytes = NULL;
    size_t length = 0;
    if (haptics->options.load(haptics->options.load_ctx, request, &bytes, &length)) {
      qa_bnvib_pattern pattern;
      if (qa_parse_bnvib(bytes, length, &pattern)) {
        haptics->cache[cached].pattern = pattern;
        haptics->cache[cached].has_pattern = true;
      }
      free(bytes);
    }
  }
  bool has_pattern = haptics->cache[cached].has_pattern;
  const qa_bnvib_pattern *pattern =
      has_pattern ? &haptics->cache[cached].pattern : NULL;
  free(key);
  free(path);
  if (haptics->closed || generation != haptics->generation) return false;
  synchronize_device(haptics);
  if (pattern == NULL || generation != haptics->generation ||
      requested < haptics->accepted || haptics->closed || !haptics->preference ||
      !haptics->active) {
    return false;
  }
  haptics->accepted = requested;
  *out = qa_bnvib_scheduler_play(&haptics->scheduler, pattern,
                                 haptics->options.now(haptics->options.now_ctx));
  return true;
}

qa_haptics_result qa_seat_haptics_update(qa_seat_haptics *haptics) {
  if (haptics->closed) return unchanged_result();
  synchronize_device(haptics);
  return qa_bnvib_scheduler_update(&haptics->scheduler,
                                   haptics->options.now(haptics->options.now_ctx));
}

void qa_seat_haptics_invalidate_assets(qa_seat_haptics *haptics) {
  (void)qa_seat_haptics_cancel(haptics);
  cache_clear(haptics);
}

qa_controller_operation_result qa_seat_haptics_close(qa_seat_haptics *haptics) {
  haptics->closed = true;
  cache_clear(haptics);
  return qa_seat_haptics_cancel(haptics);
}
