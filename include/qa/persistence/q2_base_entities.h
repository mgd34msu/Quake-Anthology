/*
 * Port of src/persistence/q2-base-entities.ts. Codec for TS
 * Q2BaseEntitiesCheckpoint, the content type
 * qa_q2_base_entities_module_checkpoint (content/q2/base/entities/index.h)
 * that qa_q2_base_entity_module_capture returns and _restore takes.
 * Decoded checkpoints borrow strings from owner->root and arrays from owner
 * blocks (release with qa_persist_decoded_free; never free(3) them).
 * Encoders return malloc'd bytes (NULL only on OOM). GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_BASE_ENTITIES_H
#define QA_PERSISTENCE_Q2_BASE_ENTITIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/base/entities/index.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/q2_movers.h"
#include "qa/persistence/value.h"

/* TS readQ2BaseEntitiesCheckpoint. */
qa_q2_base_entities_module_checkpoint qa_read_q2_base_entities_checkpoint(const qa_save_reader *reader,
                                                                          qa_persist_decoded *owner);
/* TS encodeQ2BaseEntitiesCheckpoint. */
uint8_t *qa_encode_q2_base_entities_checkpoint(const qa_q2_base_entities_module_checkpoint *checkpoint,
                                               size_t *out_length);
/* TS decodeQ2BaseEntitiesCheckpoint. False when the bytes do not parse
 * (owner untouched); malformed fields abort with the TS SaveFormatError. */
bool qa_decode_q2_base_entities_checkpoint(const uint8_t *bytes, size_t length,
                                           qa_q2_base_entities_module_checkpoint *out, qa_persist_decoded *owner);

#endif
