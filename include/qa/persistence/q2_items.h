/*
 * Port of src/persistence/q2-items.ts. Pickup and powerup timer checkpoint.
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_ITEMS_H
#define QA_PERSISTENCE_Q2_ITEMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_pickup_entry {
  qa_saved_actor_id actor;
  const char *classname;
  bool targets_used;
  bool retained;
  bool has_expires_at;
  double expires_at;
} qa_q2_pickup_entry;

typedef struct qa_persist_q2_player_powerups {
  double quad_until;
  double invulnerability_until;
  double breather_until;
  double enviro_until;
  double haste_until;
  double regen_until;
  double flight_until;
  double invis_until;
} qa_persist_q2_player_powerups;

typedef struct qa_q2_power_entry {
  qa_saved_actor_id actor;
  qa_persist_q2_player_powerups state;
} qa_q2_power_entry;

typedef struct qa_persist_q2_items_checkpoint {
  double power_cube_count;
  qa_q2_pickup_entry *pickups; size_t pickups_count;
  qa_q2_power_entry *powers; size_t powers_count;
  qa_saved_actor_id *power_armor_bindings; size_t power_armor_bindings_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_persist_q2_items_checkpoint;

qa_persist_q2_items_checkpoint qa_read_q2_items_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_items_checkpoint(const qa_persist_q2_items_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_items_checkpoint(const uint8_t *bytes, size_t length, qa_persist_q2_items_checkpoint *out);

#endif
