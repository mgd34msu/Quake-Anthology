/* Port of src/render/gl/shadow-shader.ts. Lines borrow static storage
 * except the three receiver-dependent lines, which are written into
 * caller buffers. GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_SHADOW_SHADER_H
#define QA_RENDER_GL_SHADOW_SHADER_H
#include <stddef.h>
/* receiver: "world"|"model". Writes line pointers to out (capacity
 * >= count); dyn buffers hold the interpolated lines. Returns count. */
size_t qa_shadow_factor_lines(const char *receiver, const char **out, size_t capacity, char dyn[3][256]);
#endif
