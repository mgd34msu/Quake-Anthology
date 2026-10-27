/* Port of src/content/q3/base/game/memory.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_MEMORY_H
#define QA_CONTENT_Q3_BASE_GAME_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#include "qa/persistence/value.h"
enum { QA_Q3_GAME_MEMORY_BYTES = 256 * 1024 };
typedef struct qa_q3_game_memory qa_q3_game_memory;
/* TS GameMemoryAllocation: view into pool; pool pointer retained for readString tail reads. */
typedef struct qa_q3_game_memory_allocation {
  uint8_t *bytes; size_t length; qa_q3_game_memory *pool; size_t offset;
} qa_q3_game_memory_allocation;
struct qa_q3_game_memory {
  uint8_t pool[QA_Q3_GAME_MEMORY_BYTES]; double alloc_point;
  void *ctx; double (*debug_integer)(void *ctx); void (*print)(void *ctx, const char *text);
};
void qa_q3_game_memory_init(qa_q3_game_memory *mem, void *ctx,
    double (*debug_integer)(void *ctx), void (*print)(void *ctx, const char *text));
double qa_q3_game_memory_allocated_bytes(const qa_q3_game_memory *mem);
qa_q3_game_memory_allocation qa_q3_game_memory_allocate(qa_q3_game_memory *mem, double size);
void qa_q3_game_memory_initialize(qa_q3_game_memory *mem);
void qa_q3_game_memory_status(qa_q3_game_memory *mem);
/* Malloc'd string (NULL=OOM); RangeErrors abort. */
char *qa_q3_game_memory_allocation_read_string(qa_q3_game_memory_allocation alloc);
void qa_q3_game_memory_allocation_write_string(qa_q3_game_memory_allocation alloc, const char *value);
typedef struct qa_q3_game_memory_pointer { double offset, length; } qa_q3_game_memory_pointer;
qa_q3_game_memory_pointer qa_q3_game_memory_capture_allocation(qa_q3_game_memory *mem, qa_q3_game_memory_allocation alloc);
/* SaveReader-backed restore: value is qa_save_value tree (TS unknown). */
qa_q3_game_memory_allocation qa_q3_game_memory_restore_allocation(qa_q3_game_memory *mem, const qa_save_value *value);
typedef struct qa_q3_game_memory_save_state { uint8_t *pool; size_t pool_length; double alloc_point; } qa_q3_game_memory_save_state;
qa_q3_game_memory_save_state qa_q3_game_memory_capture_save_state(qa_q3_game_memory *mem);
void qa_q3_game_memory_save_state_destroy(qa_q3_game_memory_save_state *state);
void qa_q3_game_memory_restore_save_state(qa_q3_game_memory *mem, const qa_save_value *value);
#endif
