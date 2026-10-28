/*
 * Port of src/network/q2/prediction.ts. The generic host becomes
 * fn + ctx over caller-sized state/command/ground blobs; histories
 * are fixed-capacity rings indexed by sequence. Sample origins and
 * errors use contracts qa_vec3 (TS Vec3). GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_PREDICTION_H
#define QA_NETWORK_Q2_PREDICTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"

/* TS profile: "classic" | "rerelease". */
typedef enum qa_q2_prediction_profile {
  QA_Q2_PREDICTION_CLASSIC, QA_Q2_PREDICTION_RERELEASE
} qa_q2_prediction_profile;

/* TS Q2PredictionSample over blob state/ground. */
typedef struct qa_q2_prediction_sample {
  void *state; /* state_size bytes, owned by the sample holder */
  qa_vec3 origin;
  qa_vec3 view_angles;
  bool on_ground;
  void *ground; /* ground_size bytes */
  bool step_clip;
  bool may_step;
} qa_q2_prediction_sample;

/* TS Q2PredictionHost. predict writes the next sample into out. */
typedef struct qa_q2_prediction_host {
  void *ctx;
  void (*copy_state)(void *ctx, const void *state, void *out);
  void (*predict)(void *ctx, const void *state, const void *command,
                  qa_q2_prediction_sample *out);
  bool (*same_ground)(void *ctx, const void *left, const void *right);
} qa_q2_prediction_host;

/* TS Q2PredictionResult (has_sample false is "history-exhausted"). */
typedef struct qa_q2_prediction_result {
  bool has_sample;
  qa_q2_prediction_sample sample;
  qa_vec3 error;
  double step;
  double step_time;
} qa_q2_prediction_result;

#define QA_Q2_PREDICTION_MAX_CAPACITY 4096

/* TS Q2PredictionHistory. Blobs are calloc'd per slot at init. */
typedef struct qa_q2_prediction_history {
  qa_q2_prediction_host host;
  qa_q2_prediction_profile profile;
  size_t capacity;
  size_t state_size, command_size, ground_size;
  uint8_t *commands;
  bool *command_present;
  int64_t *command_sequence; /* Sequence stored per slot (TS Map key). */
  qa_vec3 *origins;
  bool *origin_present;
  int64_t *origin_sequence;
  uint8_t *last_ground;
  bool has_last_ground;
  qa_vec3 error;
  double step;
  double step_time;
  /* Scratch state/ground for replay samples. */
  uint8_t *scratch_state;
  uint8_t *scratch_ground;
  uint8_t *scratch_state2;
  uint8_t *scratch_ground2;
} qa_q2_prediction_history;

/* False only on OOM. capacity 0 is TS default 64. */
bool qa_q2_prediction_history_init(qa_q2_prediction_history *history,
                                   qa_q2_prediction_host host,
                                   qa_q2_prediction_profile profile,
                                   size_t capacity, size_t state_size,
                                   size_t command_size, size_t ground_size);
void qa_q2_prediction_history_destroy(qa_q2_prediction_history *history);
/* TS remember / acknowledge / replay / clear. replay's sample blobs
 * point into history scratch (valid until the next replay). */
void qa_q2_prediction_history_remember(qa_q2_prediction_history *history,
                                       int64_t sequence, const void *command);
qa_vec3 qa_q2_prediction_history_acknowledge(qa_q2_prediction_history *history,
                                             int64_t sequence, qa_vec3 origin);
void qa_q2_prediction_history_replay(qa_q2_prediction_history *history,
                                     const qa_q2_prediction_sample *baseline,
                                     int64_t acknowledged,
                                     int64_t current_sequence,
                                     double now_milliseconds,
                                     double frame_seconds,
                                     const void *pending_command_or_null,
                                     qa_q2_prediction_result *out);
void qa_q2_prediction_history_clear(qa_q2_prediction_history *history);

/* TS Q2CommandPacketHistory. */
typedef struct qa_q2_command_packet_entry {
  double command_number;
  double sent_at;
} qa_q2_command_packet_entry;
typedef struct qa_q2_command_packet_history {
  size_t capacity;
  qa_q2_command_packet_entry *entries;
  bool *present;
  int64_t *sequence; /* Packet sequence stored per slot (TS Map key). */
} qa_q2_command_packet_history;

/* False only on OOM. capacity 0 is TS default 64. */
bool qa_q2_command_packet_history_init(qa_q2_command_packet_history *history,
                                       size_t capacity);
void qa_q2_command_packet_history_destroy(qa_q2_command_packet_history *history);
void qa_q2_command_packet_history_sent(qa_q2_command_packet_history *history,
                                       int64_t packet_sequence,
                                       double command_number, double sent_at);
/* False is TS null. */
bool qa_q2_command_packet_history_acknowledged(
    const qa_q2_command_packet_history *history, int64_t packet_sequence,
    qa_q2_command_packet_entry *out);
void qa_q2_command_packet_history_clear(qa_q2_command_packet_history *history);

#endif
