/*
 * Port of src/foundation/handles.ts: packed integer actor keys. Keys are
 * meaningful within one registry/session. GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_HANDLES_H
#define QA_FOUNDATION_HANDLES_H

#include <stdbool.h>
#include <stdint.h>

/** The invalid packed actor key, mirroring the C zero-registry handle. */
#define QA_INVALID_ACTOR_KEY (-1LL)
/** Slot bits in the default packing: 1,048,576 slots, generations to 2^33. */
#define QA_ACTOR_SLOT_BITS 20

int64_t qa_pack_actor_key(int64_t slot, int64_t generation);
int64_t qa_actor_key_slot(int64_t key);
int64_t qa_actor_key_generation(int64_t key);
bool qa_is_valid_actor_key(int64_t key);

#endif
