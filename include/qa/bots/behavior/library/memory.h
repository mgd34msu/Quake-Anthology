/* Port of src/bots/behavior/library/memory.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_MEMORY_H
#define QA_BOTS_BEHAVIOR_LIBRARY_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
void qa_bot_memory_reset_hunk(qa_bot_memory *m);
void qa_bot_memory_dispose(qa_bot_memory *m);
double qa_bot_memory_available_memory(qa_bot_memory *m);
size_t qa_bot_memory_get_live_allocations(const qa_bot_memory *m);
size_t qa_bot_memory_get_allocated_bytes(const qa_bot_memory *m);
void qa_bot_memory_print_used_memory_size(qa_bot_memory *m);
void qa_bot_memory_print_memory_labels(qa_bot_memory *m);
int64_t qa_bot_memory_memory_byte_size(qa_bot_memory *m, qa_bot_memory_allocation *a);
void qa_bot_memory_dump_memory(qa_bot_memory *m);
#endif
