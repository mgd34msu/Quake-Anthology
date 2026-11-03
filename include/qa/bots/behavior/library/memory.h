/* Port of src/bots/behavior/library/memory.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_MEMORY_H
#define QA_BOTS_BEHAVIOR_LIBRARY_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"
typedef enum qa_bot_memory_kind { QA_BOT_MEMORY_HEAP, QA_BOT_MEMORY_HUNK } qa_bot_memory_kind;
typedef struct qa_bot_memory_allocation {
  uint8_t *bytes;
  size_t length;
  void *record;
} qa_bot_memory_allocation;
typedef struct qa_bot_memory_provenance {
  const char *file;
  int64_t line;
  const char *label;
} qa_bot_memory_provenance;
typedef struct qa_bot_memory {
  void *records;
  size_t live_count;
  size_t allocated_bytes;
  bool disposed;
  void (*debug_print)(void *ctx, const char *severity, const char *text);
  void *debug_ctx;
  bool has_debug;
  bool debug_is_debug_kind;
  void (*write_log)(void *ctx, const char *text);
  void *log_ctx;
  double (*available_fn)(void *ctx);
  void *available_ctx;
  bool has_host;
} qa_bot_memory;
void qa_bot_memory_init(qa_bot_memory *m);
void qa_bot_memory_destroy(qa_bot_memory *m);
/* size must fit int32-4 else RangeError with TS message. Returns false only on OOM. */
bool qa_bot_memory_allocate(qa_bot_memory *m, int64_t size, qa_bot_memory_kind kind, bool clear,
                            qa_bot_memory_allocation *out);
void qa_bot_memory_free(qa_bot_memory *m, qa_bot_memory_allocation *a);
/* TS allocation.bytes getter: aborts once the allocation has been freed, also
 * through a copy of the handle (qa_bot_memory_free clears only the one passed). */
uint8_t *qa_bot_memory_allocation_bytes(const qa_bot_memory_allocation *a);
void qa_bot_memory_reset_hunk(qa_bot_memory *m);
void qa_bot_memory_dispose(qa_bot_memory *m);
double qa_bot_memory_available_memory(qa_bot_memory *m);
size_t qa_bot_memory_get_live_allocations(const qa_bot_memory *m);
size_t qa_bot_memory_get_allocated_bytes(const qa_bot_memory *m);
void qa_bot_memory_print_used_memory_size(qa_bot_memory *m);
int64_t qa_bot_memory_memory_byte_size(qa_bot_memory *m, qa_bot_memory_allocation *a);
/** TS BotMemoryCapture: owned save image plus the live allocations
 * snapshot (allocation order) taken at checkpoint time. */
typedef struct qa_bot_memory_capture {
  qa_save_value *image; /* owned; caller may take it and NULL the field */
  qa_bot_memory_allocation *allocations;
  size_t count;
} qa_bot_memory_capture;
/* TS BotMemory.checkpoint(). False only on OOM. Aborts when disposed. */
bool qa_bot_memory_checkpoint(qa_bot_memory *m, qa_bot_memory_capture *out);
/* Frees the image (if still owned) and the snapshot array only. */
void qa_bot_memory_capture_destroy(qa_bot_memory_capture *capture);
/* TS BotMemoryCapture.reference(); aborts for unowned allocations. */
int64_t qa_bot_memory_capture_reference(qa_bot_memory_capture *capture,
                                        qa_bot_memory_allocation *allocation);
/** TS BotMemoryRestore: allocations created by a restore, in image order. */
typedef struct qa_bot_memory_restored {
  qa_bot_memory_allocation *allocations;
  size_t count;
} qa_bot_memory_restored;
/* TS BotMemory.restore(): requires a fresh owner; aborts with the TS
 * error strings on an invalid image or OOM. */
void qa_bot_memory_restore(qa_bot_memory *m, const qa_save_value *value,
                           qa_bot_memory_restored *out);
/* Frees the array only; the allocations stay owned by the bot memory. */
void qa_bot_memory_restored_destroy(qa_bot_memory_restored *restored);
/* TS BotMemoryRestore.allocation(); aborts on an invalid reference. */
qa_bot_memory_allocation qa_bot_memory_restored_allocation(
    qa_bot_memory_restored *restored, int64_t reference);
#endif
