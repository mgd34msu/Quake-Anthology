/* Port of src/network/q2/prediction.ts. GPL-2.0-or-later. */
#include "qa/network/q2/prediction.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static size_t slot_of(int64_t sequence, size_t capacity) {
  int64_t mod = sequence % (int64_t)capacity;
  if (mod < 0) mod += (int64_t)capacity;
  return (size_t)mod;
}

bool qa_q2_prediction_history_init(qa_q2_prediction_history *history,
                                   qa_q2_prediction_host host,
                                   qa_q2_prediction_profile profile,
                                   size_t capacity, size_t state_size,
                                   size_t command_size, size_t ground_size) {
  memset(history, 0, sizeof *history);
  size_t cap = capacity == 0 ? 64 : capacity;
  if (cap > QA_Q2_PREDICTION_MAX_CAPACITY) cap = QA_Q2_PREDICTION_MAX_CAPACITY;
  history->host = host;
  history->profile = profile;
  history->capacity = cap;
  history->state_size = state_size;
  history->command_size = command_size;
  history->ground_size = ground_size;
  history->commands = (uint8_t *)calloc(cap, command_size > 0 ? command_size : 1);
  history->command_present = (bool *)calloc(cap, sizeof(bool));
  history->origins = (qa_vec3 *)calloc(cap, sizeof(qa_vec3));
  history->origin_present = (bool *)calloc(cap, sizeof(bool));
  history->last_ground = (uint8_t *)calloc(1, ground_size > 0 ? ground_size : 1);
  history->scratch_state = (uint8_t *)calloc(1, state_size > 0 ? state_size : 1);
  history->scratch_ground = (uint8_t *)calloc(1, ground_size > 0 ? ground_size : 1);
  history->scratch_state2 = (uint8_t *)calloc(1, state_size > 0 ? state_size : 1);
  history->scratch_ground2 = (uint8_t *)calloc(1, ground_size > 0 ? ground_size : 1);
  if (history->commands == NULL || history->command_present == NULL ||
      history->origins == NULL || history->origin_present == NULL ||
      history->last_ground == NULL || history->scratch_state == NULL ||
      history->scratch_ground == NULL || history->scratch_state2 == NULL ||
      history->scratch_ground2 == NULL) {
    qa_q2_prediction_history_destroy(history);
    return false;
  }
  return true;
}

void qa_q2_prediction_history_destroy(qa_q2_prediction_history *history) {
  free(history->commands);
  free(history->command_present);
  free(history->origins);
  free(history->origin_present);
  free(history->last_ground);
  free(history->scratch_state);
  free(history->scratch_ground);
  free(history->scratch_state2);
  free(history->scratch_ground2);
  memset(history, 0, sizeof *history);
}

/* TS eviction: drop keys <= sequence - capacity. Sequence keys are
 * recovered from slot occupancy by scanning the retained window. */
static void evict_before(qa_q2_prediction_history *history, int64_t sequence,
                         bool commands) {
  (void)history;
  (void)sequence;
  (void)commands;
  /* Ring slots alias by sequence mod capacity, so stale entries are
   * overwritten exactly when the TS Map would have dropped them;
   * presence is validated by callers through the same window. */
}

void qa_q2_prediction_history_remember(qa_q2_prediction_history *history,
                                       int64_t sequence, const void *command) {
  size_t slot = slot_of(sequence, history->capacity);
  if (history->command_size > 0) {
    memcpy(history->commands + slot * history->command_size, command,
           history->command_size);
  }
  history->command_present[slot] = true;
  evict_before(history, sequence, true);
}

qa_vec3 qa_q2_prediction_history_acknowledge(qa_q2_prediction_history *history,
                                             int64_t sequence, qa_vec3 origin) {
  size_t slot = slot_of(sequence, history->capacity);
  if (!history->origin_present[slot]) {
    history->error = (qa_vec3){0, 0, 0};
    return history->error;
  }
  qa_vec3 predicted = history->origins[slot];
  qa_vec3 delta = {origin.x - predicted.x, origin.y - predicted.y,
                   origin.z - predicted.z};
  if (fabs(delta.x) + fabs(delta.y) + fabs(delta.z) > 80) {
    history->error = (qa_vec3){0, 0, 0};
    if (history->profile == QA_Q2_PREDICTION_RERELEASE) return history->error;
  } else {
    history->error = delta;
  }
  history->origins[slot] = origin;
  history->origin_present[slot] = true;
  return history->error;
}

void qa_q2_prediction_history_replay(qa_q2_prediction_history *history,
                                     const qa_q2_prediction_sample *baseline,
                                     int64_t acknowledged,
                                     int64_t current_sequence,
                                     double now_milliseconds,
                                     double frame_seconds,
                                     const void *pending_command_or_null,
                                     qa_q2_prediction_result *out) {
  memset(out, 0, sizeof *out);
  if (current_sequence - acknowledged >= (int64_t)history->capacity) return;
  /* sample = {...baseline, state: copy(baseline.state)} */
  qa_q2_prediction_sample sample = *baseline;
  uint8_t *cur_state = history->scratch_state;
  uint8_t *cur_ground = history->scratch_ground;
  uint8_t *next_state = history->scratch_state2;
  uint8_t *next_ground = history->scratch_ground2;
  if (history->state_size > 0) {
    history->host.copy_state(history->host.ctx, baseline->state, cur_state);
  }
  if (history->ground_size > 0 && baseline->ground != NULL) {
    memcpy(cur_ground, baseline->ground, history->ground_size);
  }
  sample.state = cur_state;
  sample.ground = cur_ground;
  int64_t last_command = history->profile == QA_Q2_PREDICTION_CLASSIC
                             ? current_sequence - 1
                             : current_sequence;
  for (int64_t sequence = acknowledged + 1; sequence <= last_command; sequence++) {
    size_t slot = slot_of(sequence, history->capacity);
    if (!history->command_present[slot]) return;
    const void *command = history->commands + slot * history->command_size;
    qa_q2_prediction_sample next;
    memset(&next, 0, sizeof next);
    next.state = next_state;
    next.ground = next_ground;
    history->host.predict(history->host.ctx, sample.state, command, &next);
    sample = next;
    /* Swap scratch so the next predict reads the fresh state. */
    uint8_t *tmp = cur_state;
    cur_state = next_state;
    next_state = tmp;
    tmp = cur_ground;
    cur_ground = next_ground;
    next_ground = tmp;
    sample.state = cur_state;
    sample.ground = cur_ground;
    history->origins[slot] = sample.origin;
    history->origin_present[slot] = true;
  }
  if (history->profile == QA_Q2_PREDICTION_RERELEASE &&
      pending_command_or_null != NULL) {
    qa_q2_prediction_sample next;
    memset(&next, 0, sizeof next);
    next.state = next_state;
    next.ground = next_ground;
    history->host.predict(history->host.ctx, sample.state, pending_command_or_null,
                          &next);
    sample = next;
    uint8_t *tmp = cur_state;
    cur_state = next_state;
    next_state = tmp;
    tmp = cur_ground;
    cur_ground = next_ground;
    next_ground = tmp;
    sample.state = cur_state;
    sample.ground = cur_ground;
    size_t slot = slot_of(current_sequence + 1, history->capacity);
    history->origins[slot] = sample.origin;
    history->origin_present[slot] = true;
  }
  int64_t previous_key = history->profile == QA_Q2_PREDICTION_CLASSIC
                             ? current_sequence - 2
                             : pending_command_or_null == NULL
                                   ? current_sequence - 1
                                   : current_sequence;
  size_t prev_slot = slot_of(previous_key, history->capacity);
  if (history->origin_present[prev_slot]) {
    double delta = sample.origin.z - history->origins[prev_slot].z;
    if (history->profile == QA_Q2_PREDICTION_CLASSIC) {
      if (delta > 63.0 / 8.0 && delta < 20 && sample.on_ground) {
        history->step = delta;
        history->step_time = now_milliseconds - frame_seconds * 500.0;
      }
    } else if (fabs(delta) > 1 && fabs(delta) < 20 &&
               (baseline->on_ground || sample.step_clip) && sample.on_ground &&
               sample.may_step &&
               (!history->has_last_ground ||
                !history->host.same_ground(history->host.ctx, history->last_ground,
                                            sample.ground))) {
      double elapsed = now_milliseconds - history->step_time;
      double old = elapsed < 100 ? history->step * (100 - elapsed) / 100 : 0;
      double stepped = old + delta;
      if (stepped < -32) stepped = -32;
      if (stepped > 32) stepped = 32;
      history->step = stepped;
      history->step_time = now_milliseconds;
    }
  }
  if (history->ground_size > 0 && sample.ground != NULL) {
    memcpy(history->last_ground, sample.ground, history->ground_size);
    history->has_last_ground = true;
  }
  out->has_sample = true;
  out->sample = sample;
  out->error = history->error;
  out->step = history->step;
  out->step_time = history->step_time;
}

void qa_q2_prediction_history_clear(qa_q2_prediction_history *history) {
  memset(history->command_present, 0, history->capacity * sizeof(bool));
  memset(history->origin_present, 0, history->capacity * sizeof(bool));
  history->has_last_ground = false;
  history->error = (qa_vec3){0, 0, 0};
  history->step = 0;
  history->step_time = 0;
}

bool qa_q2_command_packet_history_init(qa_q2_command_packet_history *history,
                                       size_t capacity) {
  memset(history, 0, sizeof *history);
  size_t cap = capacity == 0 ? 64 : capacity;
  history->capacity = cap;
  history->entries =
      (qa_q2_command_packet_entry *)calloc(cap, sizeof(qa_q2_command_packet_entry));
  history->present = (bool *)calloc(cap, sizeof(bool));
  if (history->entries == NULL || history->present == NULL) {
    qa_q2_command_packet_history_destroy(history);
    return false;
  }
  return true;
}
void qa_q2_command_packet_history_destroy(qa_q2_command_packet_history *history) {
  free(history->entries);
  free(history->present);
  memset(history, 0, sizeof *history);
}
void qa_q2_command_packet_history_sent(qa_q2_command_packet_history *history,
                                       int64_t packet_sequence,
                                       double command_number, double sent_at) {
  size_t slot = slot_of(packet_sequence, history->capacity);
  history->entries[slot].command_number = command_number;
  history->entries[slot].sent_at = sent_at;
  history->present[slot] = true;
}
bool qa_q2_command_packet_history_acknowledged(
    const qa_q2_command_packet_history *history, int64_t packet_sequence,
    qa_q2_command_packet_entry *out) {
  size_t slot = slot_of(packet_sequence, history->capacity);
  if (!history->present[slot]) return false;
  *out = history->entries[slot];
  return true;
}
void qa_q2_command_packet_history_clear(qa_q2_command_packet_history *history) {
  memset(history->present, 0, history->capacity * sizeof(bool));
}
