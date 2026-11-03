/*
 * Port of src/persistence/q2-movers.ts. Codec for TS Q2MoversCheckpoint,
 * Q2LinearMotionCheckpoint and Q2AngularMotionCheckpoint: the content types
 * qa_q2_movers_checkpoint (content/q2/foundation/movers.h),
 * qa_q2_linear_motion_checkpoint (motion.h) and
 * qa_q2_angular_motion_checkpoint (angular_motion.h) that the module
 * capture functions return and the restore functions take. Decoded
 * checkpoints borrow strings from owner->root and arrays from owner blocks
 * (release with qa_persist_decoded_free). Encoders return malloc'd bytes
 * (NULL only on OOM). GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_MOVERS_H
#define QA_PERSISTENCE_Q2_MOVERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/foundation/movers.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/value.h"

/* TS readQ2LinearMotionCheckpoint / readQ2AngularMotionCheckpoint. */
qa_q2_linear_motion_checkpoint qa_read_q2_linear_motion_checkpoint(const qa_save_reader *reader,
                                                                   qa_persist_decoded *owner);
qa_q2_angular_motion_checkpoint qa_read_q2_angular_motion_checkpoint(const qa_save_reader *reader,
                                                                     qa_persist_decoded *owner);
/* The value trees TS encodes for these checkpoints (NULL on OOM). */
qa_save_value *qa_q2_linear_motion_checkpoint_value(const qa_q2_linear_motion_checkpoint *checkpoint);
qa_save_value *qa_q2_angular_motion_checkpoint_value(const qa_q2_angular_motion_checkpoint *checkpoint);
/* TS readQ2MoversCheckpoint / encodeQ2MoversCheckpoint / decodeQ2MoversCheckpoint.
 * Decode returns false when the bytes do not parse (owner untouched);
 * malformed fields abort with the TS SaveFormatError. */
qa_q2_movers_checkpoint qa_read_q2_movers_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner);
uint8_t *qa_encode_q2_movers_checkpoint(const qa_q2_movers_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_movers_checkpoint(const uint8_t *bytes, size_t length, qa_q2_movers_checkpoint *out,
                                    qa_persist_decoded *owner);

#endif
