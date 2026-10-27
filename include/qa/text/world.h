/*
 * Port of src/text/world.ts: server-side world-anchored text entries
 * with seconds/frame lifetimes (WorldTextStore).
 * C accommodations:
 *  - WorldTextInput and WorldText are one struct; `content` carries the
 *    input too (the TS only adds it in the extended interface).
 *  - Optional distanceCullFactor is has_distance_cull_factor === false
 *    for TS undefined. Orientation "billboard"/"fixed" is
 *    orientation_fixed; font "classic"/"selected" is font_selected.
 *  - The store owns deep copies (malloc'd text/content), as the TS
 *    freezes spreads. submit returns false only on OOM; invalid
 *    geometry aborts with the TS RangeError message.
 *  - snapshot() returns borrowed pointers to the stored texts plus a
 *    count (the TS returns a frozen array of the same objects). The
 *    array is allocated once inside the store and stays valid until
 *    the next submit/snapshot/clear/destroy.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_WORLD_H
#define QA_TEXT_WORLD_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/math.h"

typedef struct qa_world_text {
  const char *text;              /* borrowed on input; owned in store */
  qa_vec3 origin;
  qa_vec4 color;
  double cell_size;
  bool has_distance_cull_factor; /* false is TS undefined */
  double distance_cull_factor;
  bool orientation_fixed;        /* false is "billboard" */
  qa_vec3 angles;                /* fixed orientation only */
  bool depth_test;
  bool font_selected;            /* false is "classic" */
  qa_content_id content;         /* borrowed on input; owned in store */
} qa_world_text;

typedef struct qa_world_text_timed {
  qa_world_text text; /* owned strings */
  bool frame_lifetime;
  double expires;        /* seconds lifetime */
  bool has_first_frame;  /* false is TS firstFrame === null */
  double first_frame;
} qa_world_text_timed;

/* TS WorldTextStore. One server world owns these entries. */
typedef struct qa_world_text_store {
  qa_world_text_timed *entries;
  size_t entry_count;
  size_t entry_capacity;
  const qa_world_text **snapshot_views;
  size_t snapshot_capacity;
} qa_world_text_store;

void qa_world_text_store_init(qa_world_text_store *store);
void qa_world_text_store_destroy(qa_world_text_store *store);
/* False only on OOM. Copies text/content and all vectors. */
bool qa_world_text_store_submit(qa_world_text_store *store,
                                const qa_world_text *text, double now_seconds,
                                double lifetime_seconds);
/* Borrowed views of the live texts; also stamps frame lifetimes. */
const qa_world_text *const *qa_world_text_store_snapshot(
    qa_world_text_store *store, double now_seconds, double frame,
    size_t *out_count);
void qa_world_text_store_clear(qa_world_text_store *store);

#endif
