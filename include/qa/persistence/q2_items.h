/*
 * Port of src/persistence/q2-items.ts. Codec for TS Q2ItemsCheckpoint,
 * the content type qa_q2_items_checkpoint (content/q2/foundation/items.h)
 * that qa_q2_item_module_capture returns and _restore takes.
 * Decoded checkpoints borrow strings from owner->root and arrays from
 * owner blocks (release with qa_persist_decoded_free). Encoders return
 * malloc'd bytes (NULL only on OOM). GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_ITEMS_H
#define QA_PERSISTENCE_Q2_ITEMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/foundation/items.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/value.h"

/* TS readQ2ItemsCheckpoint. */
qa_q2_items_checkpoint qa_read_q2_items_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner);
/* TS encodeQ2ItemsCheckpoint. */
uint8_t *qa_encode_q2_items_checkpoint(const qa_q2_items_checkpoint *checkpoint, size_t *out_length);
/* TS decodeQ2ItemsCheckpoint. False when the bytes do not parse (owner
 * untouched); malformed fields abort with the TS SaveFormatError. */
bool qa_decode_q2_items_checkpoint(const uint8_t *bytes, size_t length, qa_q2_items_checkpoint *out,
                                   qa_persist_decoded *owner);

#endif
