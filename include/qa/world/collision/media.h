/* Port of src/world/collision/media.ts. Brushes/distance are fn+ctx.
 * MediumBrush planes are full qa_bsp_plane as in TS. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_MEDIA_H
#define QA_WORLD_COLLISION_MEDIA_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/numeric.h"
#include "qa/contracts/scene.h"
#include "qa/world/spatial/index.h"
typedef struct qa_medium_brush { double contents; const qa_bsp_plane *planes; size_t plane_count; } qa_medium_brush;
typedef struct qa_trace_media { bool in_open; bool in_water; } qa_trace_media;
/* endpoint: 0=start, 1=end (TS 'start'|'end'). */
typedef double (*qa_media_distance_fn)(void *ctx, qa_bsp_plane plane, int endpoint);
qa_trace_media qa_trace_brush_media(const qa_medium_brush *brushes, size_t brush_count,
  qa_collision_family family, qa_media_distance_fn distance, void *distance_ctx,
  const qa_numeric_operations *numeric, double fraction);
#endif
