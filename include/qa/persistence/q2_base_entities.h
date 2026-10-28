/*
 * Port of src/persistence/q2-base-entities.ts. Base platform/secret/scenery/turret entity checkpoints. The linear-motion list reuses q2_movers.
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_BASE_ENTITIES_H
#define QA_PERSISTENCE_Q2_BASE_ENTITIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/q2_movers.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_platform_state {
  qa_vec3 top;
  qa_vec3 bottom;
  const char *phase;
} qa_q2_platform_state;

typedef struct qa_q2_platform_entry {
  qa_saved_actor_id actor;
  qa_q2_platform_state state;
} qa_q2_platform_entry;

typedef struct qa_q2_secret_state {
  qa_vec3 first;
  qa_vec3 second;
  qa_vec3 home;
  bool shootable;
  double blocked_time;
  double message_time;
} qa_q2_secret_state;

typedef struct qa_q2_secret_entry {
  qa_saved_actor_id actor;
  qa_q2_secret_state state;
} qa_q2_secret_entry;

typedef struct qa_q2_base_movers {
  qa_q2_platform_entry *platforms; size_t platforms_count;
  qa_q2_secret_entry *secrets; size_t secrets_count;
  qa_persist_q2_linear_motion_checkpoint linear;
} qa_q2_base_movers;

typedef struct qa_q2_scenery_animation {
  qa_saved_actor_id actor;
  double first;
  double end;
} qa_q2_scenery_animation;

typedef struct qa_q2_scenery_clock {
  qa_saved_actor_id actor;
  double value;
} qa_q2_scenery_clock;

typedef struct qa_q2_scenery {
  qa_q2_scenery_animation *animations; size_t animations_count;
  qa_q2_scenery_clock *clocks; size_t clocks_count;
} qa_q2_scenery;

typedef struct qa_q2_turret_breach_state {
  qa_vec3 goal;
  qa_vec3 muzzle;
  double pitch_max;
  double pitch_min;
  double yaw_min;
  double yaw_max;
} qa_q2_turret_breach_state;

typedef struct qa_q2_turret_breach_entry {
  qa_saved_actor_id actor;
  qa_q2_turret_breach_state state;
} qa_q2_turret_breach_entry;

typedef struct qa_q2_turret_driver_entry {
  qa_saved_actor_id actor;
  bool has_breach;
  qa_saved_actor_id breach;
  double radius;
  double yaw_offset;
  double height;
  const char *monster_die;
} qa_q2_turret_driver_entry;

typedef struct qa_q2_turrets {
  qa_q2_turret_breach_entry *breaches; size_t breaches_count;
  qa_q2_turret_driver_entry *drivers; size_t drivers_count;
} qa_q2_turrets;

typedef struct qa_q2_wind_time_entry {
  qa_saved_actor_id actor;
  double until;
} qa_q2_wind_time_entry;

typedef struct qa_q2_base_entities_checkpoint {
  double version;
  qa_q2_base_movers movers;
  qa_q2_scenery scenery;
  qa_q2_turrets turrets;
  qa_q2_wind_time_entry *wind_times; size_t wind_times_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_base_entities_checkpoint;

qa_q2_base_entities_checkpoint qa_read_q2_base_entities_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_base_entities_checkpoint(const qa_q2_base_entities_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_base_entities_checkpoint(const uint8_t *bytes, size_t length, qa_q2_base_entities_checkpoint *out);

#endif
