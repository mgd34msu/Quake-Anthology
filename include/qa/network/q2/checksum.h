/* Port of src/network/q2/checksum.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q2_CHECKSUM_H
#define QA_NETWORK_Q2_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

/* TS blockSequenceChecksum(bytes, sequence). sequence must be a safe
 * non-negative integer (TS RangeError otherwise). */
int32_t qa_q2_block_sequence_checksum(const uint8_t *bytes, size_t length,
                                      int64_t sequence);

#endif
