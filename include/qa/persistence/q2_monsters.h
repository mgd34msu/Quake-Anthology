/*
 * Port of src/persistence/q2-monsters.ts. Codec for TS Q2MonstersCheckpoint,
 * the content type qa_q2m_monsters_checkpoint
 * (content/q2/foundation/monsters/checkpoint.h) that qa_q2m_monsters_capture
 * fills and qa_q2m_monsters_restore takes. Decoded checkpoints borrow
 * strings from owner->root and arrays from owner blocks (release with
 * qa_persist_decoded_free; never qa_q2m_monsters_checkpoint_free, which is
 * for captures). Encoders return malloc'd bytes (NULL only on OOM).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_MONSTERS_H
#define QA_PERSISTENCE_Q2_MONSTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/foundation/monsters/checkpoint.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/value.h"

/* TS readQ2MonstersCheckpoint / encodeQ2MonstersCheckpoint /
 * decodeQ2MonstersCheckpoint. Decode returns false when the bytes do not
 * parse (owner untouched); malformed fields abort with the TS
 * SaveFormatError. */
qa_q2m_monsters_checkpoint qa_read_q2_monsters_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner);
uint8_t *qa_encode_q2_monsters_checkpoint(const qa_q2m_monsters_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_monsters_checkpoint(const uint8_t *bytes, size_t length, qa_q2m_monsters_checkpoint *out,
                                      qa_persist_decoded *owner);

#endif
