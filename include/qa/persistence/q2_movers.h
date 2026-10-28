/*
 * Port of src/persistence/q2-movers.ts. Linear/angular motion and door/train mover checkpoints.
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_MOVERS_H
#define QA_PERSISTENCE_Q2_MOVERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_linear_curve {
  double *positions; size_t positions_count;
  double frame;
  double subframe;
  double subframes;
} qa_q2_linear_curve;

typedef struct qa_q2_linear_motion_state {
  qa_vec3 direction;
  qa_vec3 destination;
  qa_vec3 reference;
  double remaining;
  double current_speed;
  double move_speed;
  double next_speed;
  double decel_distance;
  const char *done;
  bool has_curve;
  qa_q2_linear_curve curve;
} qa_q2_linear_motion_state;

typedef struct qa_persist_q2_linear_motion_entry {
  qa_saved_actor_id actor;
  qa_q2_linear_motion_state state;
} qa_persist_q2_linear_motion_entry;

typedef struct qa_persist_q2_linear_motion_checkpoint {
  qa_persist_q2_linear_motion_entry *entries; size_t count;
} qa_persist_q2_linear_motion_checkpoint;

typedef struct qa_persist_q2_angular_motion_entry {
  qa_saved_actor_id actor;
  qa_vec3 destination;
  double speed;
  const char *done;
} qa_persist_q2_angular_motion_entry;

typedef struct qa_persist_q2_angular_motion_checkpoint {
  qa_persist_q2_angular_motion_entry *entries; size_t count;
} qa_persist_q2_angular_motion_checkpoint;

typedef struct qa_q2_door_state {
  qa_vec3 start;
  qa_vec3 end;
  double distance;
  bool button;
  bool angular;
  bool water;
  qa_vec3 safe_direction;
  double water_divisor;
  bool reversed;
  bool activated;
  const char *phase;
  double debounce;
} qa_q2_door_state;

typedef struct qa_q2_door_entry {
  qa_saved_actor_id actor;
  qa_saved_actor_id master;
  qa_saved_actor_id *team; size_t team_count;
  qa_q2_door_state state;
} qa_q2_door_entry;

typedef struct qa_q2_train_entry {
  qa_saved_actor_id actor;
  bool has_destination;
  qa_saved_actor_id destination;
  double debounce;
  bool ship;
} qa_q2_train_entry;

typedef struct qa_persist_q2_movers_checkpoint {
  qa_q2_door_entry *doors; size_t doors_count;
  qa_q2_train_entry *trains; size_t trains_count;
  qa_persist_q2_linear_motion_checkpoint linear;
  qa_persist_q2_angular_motion_checkpoint angular;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_persist_q2_movers_checkpoint;

qa_persist_q2_linear_motion_checkpoint qa_read_q2_linear_motion_checkpoint(const qa_save_reader *reader);
/* Builds the checkpoint tree (C encoder helper); NULL on OOM. */
qa_save_value *qa_q2_linear_motion_checkpoint_value(const qa_persist_q2_linear_motion_checkpoint *checkpoint);
qa_persist_q2_angular_motion_checkpoint qa_read_q2_angular_motion_checkpoint(const qa_save_reader *reader);
/* Builds the checkpoint tree (C encoder helper); NULL on OOM. */
qa_save_value *qa_q2_angular_motion_checkpoint_value(const qa_persist_q2_angular_motion_checkpoint *checkpoint);
qa_persist_q2_movers_checkpoint qa_read_q2_movers_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_movers_checkpoint(const qa_persist_q2_movers_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_movers_checkpoint(const uint8_t *bytes, size_t length, qa_persist_q2_movers_checkpoint *out);

#endif
