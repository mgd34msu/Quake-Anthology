/*
 * Port of src/persistence/q2-weapons.ts. Codec for TS Q2WeaponsCheckpoint,
 * the content type qa_q2w_weapons_checkpoint
 * (content/q2/foundation/weapons/checkpoint.h) that qa_q2w_weapons_capture
 * fills and qa_q2w_weapons_restore takes, with Q2WeaponState (qa_q2w_state)
 * and Q2WeaponInput (qa_q2w_input). Decoded checkpoints borrow strings from
 * owner->root and arrays from owner blocks (release with
 * qa_persist_decoded_free). Encoders return malloc'd bytes (NULL only on
 * OOM). GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_WEAPONS_H
#define QA_PERSISTENCE_Q2_WEAPONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/foundation/weapons/checkpoint.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

/* TS readQ2WeaponInput / readQ2WeaponState (strings borrow from the tree
 * the reader walks). */
qa_q2w_input qa_read_q2_weapon_input(const qa_save_reader *reader);
qa_q2w_state qa_read_q2_weapon_state(const qa_save_reader *reader);
/* TS readQ2WeaponsCheckpoint. */
qa_q2w_weapons_checkpoint qa_read_q2_weapons_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner);
/* TS encodeQ2WeaponsCheckpoint. */
uint8_t *qa_encode_q2_weapons_checkpoint(const qa_q2w_weapons_checkpoint *checkpoint, size_t *out_length);
/* TS decodeQ2WeaponsCheckpoint. False when the bytes do not parse (owner
 * untouched); malformed fields abort with the TS SaveFormatError. */
bool qa_decode_q2_weapons_checkpoint(const uint8_t *bytes, size_t length, qa_q2w_weapons_checkpoint *out,
                                     qa_persist_decoded *owner);

#endif
