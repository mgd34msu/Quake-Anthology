/* Port of src/app/bootstrap/q2-damage-blend.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q2_DAMAGE_BLEND_H
#define QA_APP_BOOTSTRAP_Q2_DAMAGE_BLEND_H
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
/* previous NULL is TS null. Math.fround staging preserved. */
qa_vec4 qa_app_interpolate_q2_damage_blend(const qa_vec4 *previous, qa_vec4 current, double fraction);
/* Writes at most one batch into out (vertices/indices into caller storage:
 * 8 vertices, 24 indices). Returns batch count (0 or 1). */
size_t qa_app_prepare_q2_damage_blend(qa_vec4 blend, qa_rect viewport, qa_renderer_image white,
    double fraction, qa_draw_batch *out, qa_render_vertex *vertices, double *indices);
#endif
