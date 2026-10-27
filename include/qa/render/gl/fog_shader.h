/* Port of src/render/gl/fog-shader.ts. Returned strings are malloc'd
 * (free with free()); uniform lists borrow static storage. GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_FOG_SHADER_H
#define QA_RENDER_GL_FOG_SHADER_H
#include <stddef.h>
#define QA_FOG_PASS_GLOBAL 0
#define QA_FOG_PASS_HEIGHT 1
#define QA_FOG_PASS_SKY 2
typedef double qa_fog_pass_kind;
const char *const *qa_fog_uniforms_for(double kind, size_t *out_count);
char *qa_build_fog_vertex_shader_source(double kind);
char *qa_build_fog_fragment_shader_source(double kind);
#endif
