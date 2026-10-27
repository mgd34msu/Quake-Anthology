/* Port of src/render/gl/object-opacity.ts. Reduced: opacity scopes draw
 * directly (exact for 0 and 1; intermediate values lack the FBO
 * composite until the GL FBO path lands). CPU opacity is exact.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_OBJECT_OPACITY_H
#define QA_RENDER_GL_OBJECT_OPACITY_H
#include <stdbool.h>
#include "qa/platform/gl.h"
typedef struct qa_gl_precision { double red_bits, green_bits, blue_bits, alpha_bits, depth_bits, stencil_bits; } qa_gl_precision;
typedef struct qa_gl_object_opacity qa_gl_object_opacity;
struct qa_gl_object_opacity { void *impl; };
bool qa_gl_object_opacity_init(qa_gl_object_opacity *opacity, const qa_gl_context *context, qa_gl *gl, qa_gl_precision precision);
bool qa_gl_object_opacity_bind(qa_gl_object_opacity *opacity);
typedef void (*qa_gl_opacity_draw_fn)(void *ctx);
void qa_gl_object_opacity_draw(qa_gl_object_opacity *opacity, double value, double width, double height, qa_gl_opacity_draw_fn draw, void *draw_ctx);
void qa_gl_object_opacity_close(qa_gl_object_opacity *opacity);
#endif
