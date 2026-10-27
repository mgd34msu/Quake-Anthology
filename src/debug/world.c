/* Port of src/debug/world.ts. GPL-2.0-or-later. */
#include "qa/debug/world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

typedef struct timed_line {
  qa_debug_line line;
  uint32_t expires; /* valid when has_expires (TS expires: number|null) */
  bool has_expires;
  double first_frame; /* valid when has_first_frame (TS number|null) */
  bool has_first_frame;
} timed_line;

/* Math.trunc(v) >>> 0 (JS ToUint32 of the truncated double). */
static uint32_t to_uint32_clock(double value) {
  double m = fmod(trunc(value), 4294967296.0);
  if (m < 0) m += 4294967296.0;
  return (uint32_t)m;
}

static bool ensure_entries(qa_world_debug_line_store *store, size_t need) {
  if (need <= store->entry_capacity) return true;
  size_t cap = store->entry_capacity ? store->entry_capacity : 16;
  while (cap < need) cap *= 2;
  timed_line *grown = realloc(store->entries, cap * sizeof *grown);
  if (grown == NULL) return false;
  store->entries = grown;
  store->entry_capacity = cap;
  return true;
}

bool qa_world_debug_line_store_init(qa_world_debug_line_store *store,
                                    int64_t capacity) {
  /* !Number.isSafeInteger(capacity) || capacity <= 0 */
  if (capacity <= 0 || capacity > QA_MAX_SAFE_INTEGER)
    qa_range_error("Invalid debug line capacity");
  store->capacity = capacity;
  store->entries = NULL;
  store->entry_count = 0;
  store->entry_capacity = 0;
  store->snapshot_lines = NULL;
  store->snapshot_capacity = 0;
  return true;
}

bool qa_world_debug_line_store_init_default(qa_world_debug_line_store *store) {
  return qa_world_debug_line_store_init(store,
                                        QA_WORLD_DEBUG_LINE_DEFAULT_CAPACITY);
}

void qa_world_debug_line_store_destroy(qa_world_debug_line_store *store) {
  free(store->entries);
  free(store->snapshot_lines);
  store->entries = NULL;
  store->snapshot_lines = NULL;
  store->entry_count = 0;
  store->entry_capacity = 0;
  store->snapshot_capacity = 0;
}

bool qa_world_debug_line_store_submit(qa_world_debug_line_store *store,
                                      const qa_debug_line *lines,
                                      size_t line_count,
                                      double now_milliseconds,
                                      int64_t lifetime_milliseconds) {
  if (!isfinite(now_milliseconds) || lifetime_milliseconds < 0 ||
      lifetime_milliseconds > 0xffffffffLL)
    qa_range_error("Invalid debug line lifetime");
  for (size_t i = 0; i < line_count; i++) {
    const qa_debug_line *line = &lines[i];
    double v[10] = {line->start.x, line->start.y, line->start.z,
                    line->end.x,   line->end.y,   line->end.z,
                    line->color.x, line->color.y, line->color.z,
                    line->color.w};
    for (int k = 0; k < 10; k++)
      if (!isfinite(v[k])) qa_range_error("Invalid debug line geometry");
  }
  uint32_t now = to_uint32_clock(now_milliseconds);
  uint32_t deadline =
      lifetime_milliseconds == 0
          ? 0
          : now + (uint32_t)lifetime_milliseconds; /* (now + lt) >>> 0 */
  timed_line *entries = store->entries;
  size_t kept = 0;
  for (size_t i = 0; i < store->entry_count; i++)
    if (!entries[i].has_expires || entries[i].expires > now)
      entries[kept++] = entries[i];
  store->entry_count = kept;
  if (!ensure_entries(store, kept + line_count)) return false;
  entries = store->entries;
  for (size_t i = 0; i < line_count; i++) {
    timed_line *entry = &entries[store->entry_count++];
    entry->line = lines[i]; /* frozen copy in TS */
    entry->has_expires = deadline != 0;
    entry->expires = deadline;
    entry->has_first_frame = false;
    entry->first_frame = 0;
  }
  if (store->entry_count > (size_t)store->capacity) {
    size_t excess = store->entry_count - (size_t)store->capacity;
    memmove(entries, entries + excess,
            (store->entry_count - excess) * sizeof *entries);
    store->entry_count -= excess;
  }
  return true;
}

const qa_debug_line *qa_world_debug_line_store_snapshot(
    qa_world_debug_line_store *store, double now_milliseconds, double frame,
    size_t *out_count) {
  if (!isfinite(now_milliseconds))
    qa_range_error("Invalid debug line clock");
  uint32_t now = to_uint32_clock(now_milliseconds);
  timed_line *entries = store->entries;
  size_t kept = 0;
  for (size_t i = 0; i < store->entry_count; i++) {
    timed_line entry = entries[i];
    bool keep = !entry.has_expires
                    ? (!entry.has_first_frame || entry.first_frame == frame)
                    : entry.expires > now;
    if (!keep) continue;
    if (!entry.has_expires && !entry.has_first_frame) {
      entry.has_first_frame = true;
      entry.first_frame = frame;
    }
    entries[kept++] = entry;
  }
  store->entry_count = kept;
  if (kept > store->snapshot_capacity) {
    qa_debug_line *grown = realloc(store->snapshot_lines,
                                   kept * sizeof *grown);
    if (grown == NULL) {
      *out_count = 0;
      return NULL; /* OOM only; no TS counterpart */
    }
    store->snapshot_lines = grown;
    store->snapshot_capacity = kept;
  }
  for (size_t i = 0; i < kept; i++) store->snapshot_lines[i] = entries[i].line;
  *out_count = kept;
  return store->snapshot_lines;
}

void qa_world_debug_line_store_clear(qa_world_debug_line_store *store) {
  store->entry_count = 0;
}
