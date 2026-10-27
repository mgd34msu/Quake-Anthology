/* Port of src/render/commands/material2d.ts. currentRemap
 * (render/scene/material-registrations.ts, sibling slice) is treated as
 * no-remap until that slice lands. GPL-2.0-or-later. */
#ifndef QA_RENDER_COMMANDS_MATERIAL2D_H
#define QA_RENDER_COMMANDS_MATERIAL2D_H
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/materials/evaluate.h"
#include "qa/text/draw2d.h"
/* Returned batches are owned by the caller (qa_mat_draw_batches_destroy);
 * NULL with *out_count 0 when clipped away (TS []). False only on OOM. */
int qa_prepare_material_text(qa_material_text_draw draw, qa_rect viewport, qa_material_draw_context context, qa_draw_batch **out_batches, size_t *out_count);
#endif
