/*
 * Port of src/persistence/q2-rerelease-state.ts over the content checkpoint
 * types: TS Q2FogState is qa_q2_fog_state (rerelease/types.h),
 * Q2RereleasePlayersCheckpoint is qa_q2_rerelease_players_checkpoint and
 * Q2RereleaseModuleCheckpoint is qa_q2_rerelease_module_checkpoint
 * (rerelease/checkpoint.h), Q2RereleaseQ64Checkpoint is
 * qa_q2_rerelease_q64_checkpoint (rerelease/q64/index.h).
 * Encoders write the keys in the order TS encodeCheckpointValue sees them in
 * the captured object (the content types carry the few data-dependent
 * orders as flags) and return malloc'd bytes (NULL only on OOM). Readers
 * and decoders borrow strings from the parsed tree and allocate arrays
 * through the qa_persist_decoded owner; release everything with
 * qa_persist_decoded_free(owner) once the value has been restored (restore
 * copies what it keeps). Do not pass a decoded value to the content
 * *_checkpoint_clear functions. A decoder that returns false leaves owner
 * untouched. socialId/dogtag/helpImage are copied into the player state's
 * fixed buffers.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_RERELEASE_STATE_H
#define QA_PERSISTENCE_Q2_RERELEASE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/rerelease/checkpoint.h"
#include "qa/content/q2/rerelease/q64/index.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/value.h"

/* TS readQ2FogState. */
qa_q2_fog_state qa_read_q2_fog_state(const qa_save_reader *reader);
/* TS readQ2RereleasePlayersCheckpoint / encode / decode. */
qa_q2_rerelease_players_checkpoint qa_read_q2_rerelease_players_checkpoint(const qa_save_reader *reader,
                                                                          qa_persist_decoded *owner);
uint8_t *qa_encode_q2_rerelease_players_checkpoint(const qa_q2_rerelease_players_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_rerelease_players_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rerelease_players_checkpoint *out,
                                               qa_persist_decoded *owner);
/* TS readQ2RereleaseQ64Checkpoint. */
qa_q2_rerelease_q64_checkpoint qa_read_q2_rerelease_q64_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner);
/* TS readQ2RereleaseModuleCheckpoint / encode / decode. */
qa_q2_rerelease_module_checkpoint qa_read_q2_rerelease_module_checkpoint(const qa_save_reader *reader,
                                                                        qa_persist_decoded *owner);
uint8_t *qa_encode_q2_rerelease_module_checkpoint(const qa_q2_rerelease_module_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_rerelease_module_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rerelease_module_checkpoint *out,
                                              qa_persist_decoded *owner);

#endif
