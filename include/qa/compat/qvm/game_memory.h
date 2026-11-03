/*
 * Port of src/compat/qvm/game-memory.ts: Q3GameMemory, the Quake III
 * game-module pointer arguments as the shared server-game traps read and
 * write them. C accommodations: the TS interface becomes an ops table plus
 * ctx; a TS Uint8Array region becomes qa_qvm_game_region (live bytes, or a
 * read-only copy owned by the memory); a TS DataView becomes
 * qa_qvm_writable_view (memory_writes.h), whose writes pointer is NULL for a
 * plain view and whose read_only flag refuses stores. Implementations:
 * QvmMemory (qa_qvm_memory_game_memory, compat/qvm/memory.h) and
 * NativeGameMemory (compat/q3/native/memory.h). GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_GAME_MEMORY_H
#define QA_COMPAT_QVM_GAME_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory_writes.h"

/* A TS Uint8Array returned by pointer() or span(). */
typedef struct qa_qvm_game_region {
  uint8_t *bytes;
  size_t length;
  bool read_only;
} qa_qvm_game_region;

typedef struct qa_qvm_game_memory_ops {
  /* The readable allocation from a nonnull pointer to its end; false for a null pointer. */
  bool (*pointer)(void *ctx, int64_t pointer, qa_qvm_game_region *out);
  /* The live bytes of an exact range; ranges that leave the allocation raise. */
  qa_qvm_game_region (*span)(void *ctx, int64_t pointer, int64_t length, int64_t relative_offset);
  void (*view)(void *ctx, int64_t pointer, int64_t length, int64_t relative_offset, qa_qvm_writable_view *out);
  /* Malloc'd NUL-terminated copy; caller frees. */
  char *(*read_string)(void *ctx, int64_t pointer);
  /* Q_strncpyz: at most capacity - 1 characters and a terminator, the rest of the capacity zeroed. */
  void (*write_string)(void *ctx, int64_t pointer, const char *text, int64_t capacity);
  /* The flat address a nonnull pointer names; false for a null pointer. */
  bool (*address)(void *ctx, int64_t pointer, int64_t *out);
  /* The pointer word that denotes a flat address (has_address false: TS null). */
  int64_t (*pointer_word)(void *ctx, bool has_address, int64_t address);
  /* A live view by flat address; ranges that leave the allocation raise. */
  void (*view_at)(void *ctx, int64_t address, int64_t length, qa_qvm_writable_view *out);
  /* A view of part of a region; length < 0 covers the rest of the region (TS default). */
  void (*region_view)(void *ctx, const qa_qvm_game_region *region, int64_t offset, int64_t length,
                      qa_qvm_writable_view *out);
  void (*region_write)(void *ctx, const qa_qvm_game_region *region, const uint8_t *bytes, size_t length);
  void (*region_fill)(void *ctx, const qa_qvm_game_region *region, int64_t start, int64_t length, int32_t value);
} qa_qvm_game_memory_ops;

typedef struct qa_qvm_game_memory {
  const qa_qvm_game_memory_ops *ops;
  void *ctx;
} qa_qvm_game_memory;

bool qa_qvm_game_memory_pointer(const qa_qvm_game_memory *memory, int64_t pointer, qa_qvm_game_region *out);
qa_qvm_game_region qa_qvm_game_memory_span(const qa_qvm_game_memory *memory, int64_t pointer, int64_t length,
                                         int64_t relative_offset);
void qa_qvm_game_memory_view(const qa_qvm_game_memory *memory, int64_t pointer, int64_t length, int64_t relative_offset,
                            qa_qvm_writable_view *out);
char *qa_qvm_game_memory_read_string(const qa_qvm_game_memory *memory, int64_t pointer);
void qa_qvm_game_memory_write_string(const qa_qvm_game_memory *memory, int64_t pointer, const char *text,
                                    int64_t capacity);
bool qa_qvm_game_memory_address(const qa_qvm_game_memory *memory, int64_t pointer, int64_t *out);
int64_t qa_qvm_game_memory_pointer_word(const qa_qvm_game_memory *memory, bool has_address, int64_t address);
void qa_qvm_game_memory_view_at(const qa_qvm_game_memory *memory, int64_t address, int64_t length,
                               qa_qvm_writable_view *out);
void qa_qvm_game_memory_region_view(const qa_qvm_game_memory *memory, const qa_qvm_game_region *region, int64_t offset,
                                   int64_t length, qa_qvm_writable_view *out);
void qa_qvm_game_memory_region_write(const qa_qvm_game_memory *memory, const qa_qvm_game_region *region,
                                    const uint8_t *bytes, size_t length);
void qa_qvm_game_memory_region_fill(const qa_qvm_game_memory *memory, const qa_qvm_game_region *region, int64_t start,
                                   int64_t length, int32_t value);

#endif
