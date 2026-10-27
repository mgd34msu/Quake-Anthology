/* Port of src/text/world.ts. GPL-2.0-or-later. */
#include "qa/text/world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

void qa_world_text_store_init(qa_world_text_store *store) {
  memset(store, 0, sizeof *store);
}

static void free_timed(qa_world_text_timed *entry) {
  free((void *)entry->text.text);
  free((void *)entry->text.content);
  entry->text.text = NULL;
  entry->text.content = NULL;
}

void qa_world_text_store_destroy(qa_world_text_store *store) {
  for (size_t i = 0; i < store->entry_count; i++) free_timed(&store->entries[i]);
  free(store->entries);
  free(store->snapshot_views);
  memset(store, 0, sizeof *store);
}

void qa_world_text_store_clear(qa_world_text_store *store) {
  for (size_t i = 0; i < store->entry_count; i++) free_timed(&store->entries[i]);
  store->entry_count = 0;
}

static void prune(qa_world_text_store *store, double now_seconds) {
  size_t kept = 0;
  for (size_t i = 0; i < store->entry_count; i++) {
    if (store->entries[i].frame_lifetime ||
        store->entries[i].expires > now_seconds) {
      if (kept != i) store->entries[kept] = store->entries[i];
      kept++;
    } else {
      free_timed(&store->entries[i]);
    }
  }
  store->entry_count = kept;
}

bool qa_world_text_store_submit(qa_world_text_store *store,
                                const qa_world_text *text, double now_seconds,
                                double lifetime_seconds) {
  bool finite = isfinite(now_seconds) && isfinite(lifetime_seconds) &&
                isfinite(text->cell_size) && isfinite(text->origin.x) &&
                isfinite(text->origin.y) && isfinite(text->origin.z) &&
                isfinite(text->color.x) && isfinite(text->color.y) &&
                isfinite(text->color.z) && isfinite(text->color.w) &&
                (!text->has_distance_cull_factor ||
                 isfinite(text->distance_cull_factor)) &&
                (!text->orientation_fixed ||
                 (isfinite(text->angles.x) && isfinite(text->angles.y) &&
                  isfinite(text->angles.z)));
  if (!finite || lifetime_seconds < 0 || text->cell_size <= 0) {
    qa_range_error("Invalid world text geometry or lifetime");
  }
  prune(store, now_seconds);
  if (store->entry_count == store->entry_capacity) {
    size_t grown = store->entry_capacity == 0 ? 4 : store->entry_capacity * 2;
    qa_world_text_timed *entries = (qa_world_text_timed *)realloc(
        store->entries, grown * sizeof *entries);
    if (entries == NULL) return false;
    store->entries = entries;
    store->entry_capacity = grown;
  }
  qa_world_text_timed *entry = &store->entries[store->entry_count];
  memset(entry, 0, sizeof *entry);
  entry->text = *text;
  entry->text.text = dup_text(text->text);
  entry->text.content = dup_text(text->content);
  if ((text->text != NULL && entry->text.text == NULL) ||
      (text->content != NULL && entry->text.content == NULL)) {
    free_timed(entry);
    return false;
  }
  if (lifetime_seconds == 0) {
    entry->frame_lifetime = true;
    entry->has_first_frame = false;
  } else {
    entry->frame_lifetime = false;
    entry->expires = now_seconds + lifetime_seconds;
  }
  store->entry_count++;
  return true;
}

const qa_world_text *const *qa_world_text_store_snapshot(
    qa_world_text_store *store, double now_seconds, double frame,
    size_t *out_count) {
  prune(store, now_seconds);
  size_t kept = 0;
  for (size_t i = 0; i < store->entry_count; i++) {
    qa_world_text_timed *entry = &store->entries[i];
    if (entry->frame_lifetime && entry->has_first_frame &&
        entry->first_frame != frame) {
      free_timed(entry);
      continue;
    }
    if (entry->frame_lifetime && !entry->has_first_frame) {
      entry->has_first_frame = true;
      entry->first_frame = frame;
    }
    if (kept != i) store->entries[kept] = *entry;
    kept++;
  }
  store->entry_count = kept;
  if (store->snapshot_capacity < kept) {
    const qa_world_text **views = (const qa_world_text **)realloc(
        store->snapshot_views, kept * sizeof *views);
    if (views == NULL) {
      *out_count = 0;
      return NULL;
    }
    store->snapshot_views = views;
    store->snapshot_capacity = kept;
  }
  for (size_t i = 0; i < kept; i++) {
    store->snapshot_views[i] = &store->entries[i].text;
  }
  *out_count = kept;
  return store->snapshot_views;
}
