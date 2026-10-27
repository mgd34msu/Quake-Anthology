/* Port of src/render/gl/output-gamma.ts. Reduced: gamma table updates
 * are retained; the offscreen bind/finish blit is a no-op until the GL
 * FBO path lands. Render-level table math is qa_output_gamma_table.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_OUTPUT_GAMMA_H
#define QA_RENDER_GL_OUTPUT_GAMMA_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/platform/gl.h"
#include "qa/render/gl/object_opacity.h"
typedef struct qa_gl_output_gamma qa_gl_output_gamma;
struct qa_gl_output_gamma { void *impl; };
bool qa_gl_output_gamma_init(qa_gl_output_gamma *gamma, const qa_gl_context *context, qa_gl *gl, qa_gl_precision precision);
void qa_gl_output_gamma_update(qa_gl_output_gamma *gamma, const uint8_t table[256]);
bool qa_gl_output_gamma_bind(qa_gl_output_gamma *gamma, double buffer, double width, double height);
void qa_gl_output_gamma_select_default(qa_gl_output_gamma *gamma, double buffer);
void qa_gl_output_gamma_finish(qa_gl_output_gamma *gamma, double buffer);
void qa_gl_output_gamma_restore(qa_gl_output_gamma *gamma, double buffer);
void qa_gl_output_gamma_close(qa_gl_output_gamma *gamma);
#endif
