/* Port of src/render/gl/programs.ts. Uniform value caching from the TS
 * is dropped (values are always uploaded; observable GL state matches).
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_PROGRAMS_H
#define QA_RENDER_GL_PROGRAMS_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/platform/gl_programs.h"
extern const char *const QA_GL_STAGE_VERTEX_SHADER;
/* Malloc'd (shadow lines are interpolated); free with free(). */
char *qa_gl_stage_fragment_shader(void);
uint32_t qa_gl_compile_program(qa_gl_programs *library, const char *vertex, const char *fragment);
typedef struct qa_gl_stage_program qa_gl_stage_program;
struct qa_gl_stage_program { void *impl; };
bool qa_gl_stage_program_init(qa_gl_stage_program *program, const qa_gl_context *context);
/* environment NULL = TS null; fog NULL = TS undefined. */
void qa_gl_stage_program_use(qa_gl_stage_program *program, const char *environment, const char *alpha_test, qa_batch_lighting lighting, bool luminance_alpha, const qa_batch_fog *fog);
void qa_gl_stage_program_use_depth(qa_gl_stage_program *program);
void qa_gl_stage_program_restore(qa_gl_stage_program *program, double gl_program);
void qa_gl_stage_program_close(qa_gl_stage_program *program);
#endif
