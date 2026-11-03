/*
 * Port of src/persistence/q2-foundation.ts. Codec for TS Q2AttackCheckpoint
 * and Q2FoundationCheckpoint, the content types qa_q2_attack_checkpoint and
 * qa_q2_foundation_checkpoint (content/q2/foundation/checkpoint.h) that
 * qa_q2_entity_services_capture returns and _restore takes. Decoded
 * checkpoints borrow strings from owner->root and arrays from owner blocks
 * (release with qa_persist_decoded_free after restore; restore copies what
 * it keeps). Encoders return malloc'd bytes (NULL only on OOM).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_FOUNDATION_H
#define QA_PERSISTENCE_Q2_FOUNDATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

/* TS readQ2AttackCheckpoint (strings borrow from the reader's tree). */
qa_q2_attack_checkpoint qa_read_q2_attack_checkpoint(const qa_save_reader *reader);
/* The value tree TS encodes for an attack checkpoint (NULL on OOM). */
qa_save_value *qa_q2_attack_checkpoint_value(const qa_q2_attack_checkpoint *checkpoint);
/* TS readQ2FoundationCheckpoint / encodeQ2FoundationCheckpoint /
 * decodeQ2FoundationCheckpoint. Decode returns false when the bytes do not
 * parse (owner untouched); malformed fields abort with the TS
 * SaveFormatError. */
qa_q2_foundation_checkpoint qa_read_q2_foundation_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner);
uint8_t *qa_encode_q2_foundation_checkpoint(const qa_q2_foundation_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_foundation_checkpoint(const uint8_t *bytes, size_t length, qa_q2_foundation_checkpoint *out,
                                        qa_persist_decoded *owner);

#endif
