/* Port of src/render/scene/q1-fog.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_Q1_FOG_H
#define QA_RENDER_SCENE_Q1_FOG_H
#include <stddef.h>
#include "qa/contracts/render.h"
typedef struct qa_scene_fogged_operations {
  qa_render_operation *operations; size_t count; /* owned when owned flag */
  qa_draw_batch **batches; qa_batch_fog **fogs; /* per-op owned (NULL entries ok) */
  int owned;
} qa_scene_fogged_operations;
void qa_scene_fogged_operations_destroy(qa_scene_fogged_operations *ops);
/* fog NULL is TS undefined: result borrows input (owned=0). */
const qa_render_operation *qa_fog_scene_operations(const qa_render_operation *operations, size_t count, const qa_scene_fog *fog, qa_scene_fogged_operations *out, size_t *out_count);
#endif
