/* Port of src/render/gl/fog.ts. Reduced: program compilation and the
 * fullscreen passes are retained as validated state; the depth-copy
 * draw is a no-op until the GL renderer FBO path lands (see slice
 * unresolved). The CPU path (cpu/fog) is the bit-exact reference.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_FOG_H
#define QA_RENDER_GL_FOG_H
#include <stdbool.h>
#include "qa/contracts/render.h"
#include "qa/platform/gl.h"
typedef struct qa_q2_fog_pass qa_q2_fog_pass;
struct qa_q2_fog_pass { void *impl; };
bool qa_q2_fog_pass_init(qa_q2_fog_pass *pass, const qa_gl_context *context, qa_gl *gl);
void qa_q2_fog_pass_draw(qa_q2_fog_pass *pass, qa_q2_fog_operation operation, double target_width, double target_height);
void qa_q2_fog_pass_close(qa_q2_fog_pass *pass);
#endif
