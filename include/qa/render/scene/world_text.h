/* Port of src/render/scene/world-text.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_WORLD_TEXT_H
#define QA_RENDER_SCENE_WORLD_TEXT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/text/atlas.h"
#include "qa/text/world.h"
typedef qa_text_font_selection (*qa_scene_font_for_fn)(void *ctx, const qa_world_text *text);
typedef struct qa_scene_world_text_batches {
  qa_draw_batch *batches; size_t count;
  qa_render_vertex *vertices; double *indices; /* shared owned blocks */
} qa_scene_world_text_batches;
void qa_scene_world_text_batches_destroy(qa_scene_world_text_batches *b);
/* has_factor false is TS distanceCullFactor undefined. */
qa_draw_batch *qa_prepare_world_text(const qa_world_text *texts, size_t count, qa_scene_camera camera, qa_scene_font_for_fn font_for, void *font_ctx, bool has_factor, double distance_cull_factor, qa_scene_world_text_batches *out, size_t *out_count);
#endif
