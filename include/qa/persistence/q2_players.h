/*
 * Port of src/persistence/q2-players.ts over the content checkpoint types
 * (content/q2/base/player/checkpoint.h): TS Q2PlayersCheckpoint is
 * qa_q2_base_players_checkpoint, Q2PlayerStateCheckpoint is
 * qa_q2_base_player_state_checkpoint, Q2CharacterCheckpoint is
 * qa_q2_base_character_checkpoint.
 * Encoders write the keys in the order TS encodeCheckpointValue sees them
 * in the captured object (class field and capture-literal order) and return
 * malloc'd bytes (NULL only on OOM). Readers and decoders borrow strings
 * from the parsed tree and allocate arrays through the qa_persist_decoded
 * owner; release everything with qa_persist_decoded_free(owner) once the
 * value has been restored (restore copies what it keeps). A decoder that
 * returns false leaves owner untouched. userinfo/name/skin are copied into
 * the checkpoint's fixed buffers (longer saved values are truncated to
 * them).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_PLAYERS_H
#define QA_PERSISTENCE_Q2_PLAYERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/base/player/checkpoint.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/value.h"

/* TS readQ2PlayerStateCheckpoint. */
qa_q2_base_player_state_checkpoint qa_read_q2_player_state_checkpoint(const qa_save_reader *reader,
                                                                      qa_persist_decoded *owner);
/* The encoded value of a Q2PlayerStateCheckpoint (NULL only on OOM). */
qa_save_value *qa_q2_player_state_checkpoint_value(const qa_q2_base_player_state_checkpoint *checkpoint);
/* TS readQ2PlayersCheckpoint / encodeQ2PlayersCheckpoint / decodeQ2PlayersCheckpoint. */
qa_q2_base_players_checkpoint qa_read_q2_players_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner);
uint8_t *qa_encode_q2_players_checkpoint(const qa_q2_base_players_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_players_checkpoint(const uint8_t *bytes, size_t length, qa_q2_base_players_checkpoint *out,
                                     qa_persist_decoded *owner);
/* TS readQ2CharacterCheckpoint / encodeQ2CharacterCheckpoint / decodeQ2CharacterCheckpoint. */
qa_q2_base_character_checkpoint qa_read_q2_character_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner);
uint8_t *qa_encode_q2_character_checkpoint(const qa_q2_base_character_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_character_checkpoint(const uint8_t *bytes, size_t length, qa_q2_base_character_checkpoint *out,
                                       qa_persist_decoded *owner);

#endif
