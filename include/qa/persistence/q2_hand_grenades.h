/*
 * Port of src/persistence/q2-hand-grenades.ts. Hand-grenade loadout/action checkpoint.
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_HAND_GRENADES_H
#define QA_PERSISTENCE_Q2_HAND_GRENADES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

typedef struct qa_persist_hand_grenade_loadout {
  bool enabled;
  double initial_ammo;
  double capacity;
  bool infinite_ammo;
} qa_persist_hand_grenade_loadout;

typedef enum qa_hand_action_kind {
  QA_HAND_ACTION_IDLE, QA_HAND_ACTION_DISARMED, QA_HAND_ACTION_PREPARING,
  QA_HAND_ACTION_COOKING, QA_HAND_ACTION_RELEASING, QA_HAND_ACTION_RECOVERING
} qa_hand_action_kind;
/* TS HandAction union; only the fields of `kind` are meaningful. */
typedef struct qa_hand_action {
  qa_hand_action_kind kind;
  double frame; bool release_queued; double next_at; /* preparing */
  double expires_at; /* cooking, releasing */
  double throw_at; /* releasing */
  double ready_at; bool require_release; /* recovering */
} qa_hand_action;

typedef struct qa_hand_grenade_state {
  qa_persist_hand_grenade_loadout config;
  qa_hand_action action;
} qa_hand_grenade_state;

typedef struct qa_hand_grenades_actor {
  qa_saved_actor_id actor;
  qa_hand_grenade_state state;
} qa_hand_grenades_actor;

typedef struct qa_persist_hand_grenades_checkpoint {
  double version;
  const char *edition;
  qa_hand_grenades_actor *actors; size_t actors_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_persist_hand_grenades_checkpoint;

qa_persist_hand_grenades_checkpoint qa_read_hand_grenades_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_hand_grenades_checkpoint(const qa_persist_hand_grenades_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_hand_grenades_checkpoint(const uint8_t *bytes, size_t length, qa_persist_hand_grenades_checkpoint *out);
/* Frees actors and the decoded root (NULL from qa_read_*). */
void qa_persist_hand_grenades_checkpoint_free(qa_persist_hand_grenades_checkpoint *checkpoint);

#endif
