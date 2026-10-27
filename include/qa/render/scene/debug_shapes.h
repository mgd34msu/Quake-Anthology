/* Port of src/render/scene/debug-shapes.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_DEBUG_SHAPES_H
#define QA_RENDER_SCENE_DEBUG_SHAPES_H
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/debug/shapes.h"
typedef struct qa_scene_debug_batches {
  qa_draw_batch *batches; size_t count;
  qa_render_vertex **vertices; double **indices; /* per-batch owned */
} qa_scene_debug_batches;
void qa_scene_debug_batches_destroy(qa_scene_debug_batches *batches);
/* line_width default in TS is 2 (caller passes). */
qa_draw_batch *qa_prepare_debug_shapes(const qa_debug_line *lines, size_t count, qa_scene_camera camera, qa_renderer_image white_image, double line_width, qa_scene_debug_batches *out, size_t *out_count);
#endif
