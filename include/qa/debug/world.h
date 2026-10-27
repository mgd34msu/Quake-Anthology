/* Port of src/debug/world.ts (WorldDebugLineStore).
 * Server milliseconds own expiry; all seats share one
 * presentation-frame snapshot.
 * C accommodations: init returns false only on OOM (invalid capacity
 * still aborts via qa_range_error, as the TS RangeError); the frozen
 * snapshot array becomes a store-owned buffer valid until the next
 * submit/snapshot/clear/destroy; submitted lines are copied by value
 * (the TS Object.freeze({...line}) copies). The TS `frame: number`
 * stays a double so first-frame equality matches JS. */
#ifndef QA_DEBUG_WORLD_H
#define QA_DEBUG_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/debug/shapes.h"

typedef struct qa_world_debug_line_store qa_world_debug_line_store;

struct qa_world_debug_line_store {
  int64_t capacity;
  void *entries; /* opaque timed entries */
  size_t entry_count;
  size_t entry_capacity;
  qa_debug_line *snapshot_lines;
  size_t snapshot_capacity;
};

#define QA_WORLD_DEBUG_LINE_DEFAULT_CAPACITY 9216

bool qa_world_debug_line_store_init(qa_world_debug_line_store *store,
                                    int64_t capacity);
bool qa_world_debug_line_store_init_default(qa_world_debug_line_store *store);
void qa_world_debug_line_store_destroy(qa_world_debug_line_store *store);

/* TS submit: lifetime is the checked safe integer. Returns false only
 * on OOM; invalid clock/lifetime/geometry abort via qa_range_error
 * before any mutation of the entry list (entries are filtered only
 * after validation, as in TS; on OOM the filter may have run). */
bool qa_world_debug_line_store_submit(qa_world_debug_line_store *store,
                                      const qa_debug_line *lines,
                                      size_t line_count,
                                      double now_milliseconds,
                                      int64_t lifetime_milliseconds);

/* TS snapshot. Sets *out_count; returns store-owned lines. */
const qa_debug_line *qa_world_debug_line_store_snapshot(
    qa_world_debug_line_store *store, double now_milliseconds, double frame,
    size_t *out_count);

void qa_world_debug_line_store_clear(qa_world_debug_line_store *store);

#endif
