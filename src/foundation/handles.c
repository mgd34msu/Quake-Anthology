/* Port of src/foundation/handles.ts. GPL-2.0-or-later. */
#include "qa/foundation/handles.h"

#include "qa/foundation/support.h"

#define SLOT_SPAN (1LL << QA_ACTOR_SLOT_BITS)

int64_t qa_pack_actor_key(int64_t slot, int64_t generation) {
  if (slot < 0 || slot >= SLOT_SPAN) {
    qa_range_error("Actor slot is outside the packed range: %lld", (long long)slot);
  }
  if (generation < 0 || generation > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Actor generation must be a nonnegative safe integer: %lld",
                   (long long)generation);
  }
  if (generation > (QA_MAX_SAFE_INTEGER - slot) / SLOT_SPAN) {
    qa_range_error("Actor generation exceeds the packed key range");
  }
  return generation * SLOT_SPAN + slot;
}

int64_t qa_actor_key_slot(int64_t key) {
  if (key == QA_INVALID_ACTOR_KEY) return -1;
  return key % SLOT_SPAN;
}

int64_t qa_actor_key_generation(int64_t key) {
  if (key == QA_INVALID_ACTOR_KEY) return -1;
  /* TypeScript uses Math.floor(key / SPAN); keys are nonnegative here. */
  return key / SLOT_SPAN;
}

bool qa_is_valid_actor_key(int64_t key) {
  return key >= 0 && key <= QA_MAX_SAFE_INTEGER;
}
