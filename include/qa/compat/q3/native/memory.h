/*
 * Port of src/compat/q3/native/memory.ts: NativeGameMemory, the Q3GameMemory
 * of a native i386 game module. Pointer arguments are absolute 32-bit
 * addresses in its guest process, not offsets into one allocation. Regions in
 * read-only mappings are copies that refuse stores. C accommodations: the TS
 * WeakSet of read-only copies becomes the region's read_only flag; the copies
 * are owned by this memory (released by destroy) where TS lets GC collect them.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_Q3_NATIVE_MEMORY_H
#define QA_COMPAT_Q3_NATIVE_MEMORY_H

#include "qa/compat/qvm/game_memory.h"
#include "qa/guest/core/memory.h"

typedef struct qa_native_game_memory {
  qa_sparse_guest_memory *memory; /* borrowed */
  uint8_t **copies;
  size_t copy_count, copy_cap;
} qa_native_game_memory;

/* RangeError when the memory is not a 32-bit address space. */
void qa_native_game_memory_init(qa_native_game_memory *native, qa_sparse_guest_memory *memory);
void qa_native_game_memory_destroy(qa_native_game_memory *native);
/* The Q3GameMemory view of this memory; borrows native. */
qa_qvm_game_memory qa_native_game_memory_game_memory(qa_native_game_memory *native);

#endif
