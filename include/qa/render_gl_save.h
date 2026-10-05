#ifndef QA_RENDER_GL_SAVE_H
#define QA_RENDER_GL_SAVE_H
#include "qa/render_gl.h"
typedef struct qa_gl_restore_guard qa_gl_restore_guard;
/* Fresh logical renderer with empty GPU residency. The actual native output
 * cut is retained until publication; separate programs/resources prepare late. */
bool qa_gl_create_detached(const qa_gl_options *,float gamma,qa_gl_renderer *,
    qa_gl_renderer **,qa_gl_restore_guard **,qa_error *);
bool qa_gl_handoff_prepare(qa_gl_restore_guard *,qa_error *);
bool qa_gl_handoff_ready(const qa_gl_restore_guard *,qa_error *);
void qa_gl_handoff(qa_gl_restore_guard *);
void qa_gl_restore_guard_destroy(qa_gl_restore_guard *);
#endif
