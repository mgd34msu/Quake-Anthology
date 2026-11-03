/* Port of src/render/gl/fog.ts: Q2 rerelease gl_fog.ts depth snapshot and
 * ordered global/height/sky passes. TS throws become qa_range_error aborts
 * (docs/PORTING.md). GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_FOG_H
#define QA_RENDER_GL_FOG_H
#include <stdbool.h>
#include "qa/contracts/render.h"
#include "qa/platform/gl.h"
typedef struct qa_q2_fog_pass qa_q2_fog_pass;
struct qa_q2_fog_pass { void *impl; };
/* False on OOM or when retaining the context's procedures fails. */
bool qa_q2_fog_pass_init(qa_q2_fog_pass *pass, const qa_gl_context *context, qa_gl *gl);
void qa_q2_fog_pass_draw(qa_q2_fog_pass *pass, qa_q2_fog_operation operation, double target_width, double target_height);
void qa_q2_fog_pass_close(qa_q2_fog_pass *pass);
#endif
